/*  Havok Format Library
    Copyright(C) 2016-2023 Lukas Cone

    This program is free software : you can redistribute it and / or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.If not, see <https://www.gnu.org/licenses/>.
*/

#include "spike/util/endian.hpp"

#include "fixups.hpp"
#include "format_old.hpp"
#include "hklib/hka_animation.hpp"
#include "hklib/hka_skeleton.hpp"
#include "hklib/hk_rootlevelcontainer.hpp"
#include "internal/hk_internal_api.hpp"
#include "spike/crypto/jenkinshash.hpp"
#include "spike/except.hpp"
#include "spike/master_printer.hpp"
#include "spike/type/pointer.hpp"
#include <algorithm>
#include <ctype.h>
#include <string>
#include <unordered_map>

#include "spike/io/binreader.hpp"
#include "spike/io/binwritter.hpp"

void hkxHeader::Load(BinReaderRef_e rd) {
  rd.Read<hkxHeaderData>(*this);

  if (magic1 != hkMagic1) {
    throw es::InvalidHeaderError(magic1);
  }

  if (magic2 != hkMagic2) {
    throw es::InvalidHeaderError(magic2);
  }

  uint32 currentSectionID = 0;

  if (!layout.littleEndian) {
    SwapEndian();
    rd.SwapEndian(true);
  }

  if (maxpredicate != -1) {
    rd.Skip(predicateArraySizePlusPadding);
  }

  GenerateToolset();

  sections.resize(numSections);

  for (auto &s : sections) {
    s.header = this;
    rd.Read<hkxSectionHeaderData>(s);

    if (version > 9) {
      rd.Seek(16, std::ios_base::cur);
    }

    s.sectionID = currentSectionID;
    currentSectionID++;
  }

  for (auto &s : sections) {
    s.Load(rd);
  }

  for (auto &s : sections) {
    s.LoadBuffer(rd);
  }

  if (layout.bytesInPointer == 4) {
    for (auto &s : sections) {
      s.LinkBuffer86();
      s.Finalize();
    }
  } else {
    for (auto &s : sections) {
      s.LinkBuffer();
      s.Finalize();
    }
  }
}

void hkxHeader::GenerateToolset() {
  uint32 versions[3] = {};
  char *cc = contentsVersion;
  size_t cVer = 0;

  while (cc && *cc && cVer < 3) {
    if (isdigit(*cc)) {
      char *endPtr = nullptr;
      versions[cVer++] = std::strtol(cc, &endPtr, 10);
      cc = endPtr;
    } else {
      cc++;
    }
  }

  auto convert = [&]() {
    switch (versions[0]) {
    case 5: {
      switch (versions[1]) {
      case 0:
        return HK500;
      case 1:
        return HK510;
      case 5:
        return HK550;
      }
      return HKUNKVER;
    }

    case 6: {
      switch (versions[1]) {
      case 0:
        return HK600;
      case 1:
        return HK610;
      case 5:
        return HK650;
      case 6:
        return HK660;
      }
      return HKUNKVER;
    }

    case 7: {
      switch (versions[1]) {
      case 0:
        return HK700;
      case 1:
        return HK710;
      }
      return HKUNKVER;
    }

    case 2010: {
      switch (versions[1]) {
      case 1:
        return HK2010_1;
      case 2:
        return HK2010_2;
      }
      return HKUNKVER;
    }

    case 2011: {
      switch (versions[1]) {
      case 1:
        return HK2011_1;
      case 2:
        return HK2011_2;
      case 3:
        return HK2011_3;
      }
      return HKUNKVER;
    }

    case 2012: {
      switch (versions[1]) {
      case 1:
        return HK2012_1;
      case 2:
        return HK2012_2;
      }
      return HKUNKVER;
    }

    case 2013: {
      switch (versions[1]) {
      case 1:
        return HK2013;
      }
      return HKUNKVER;
    }

    case 2014: {
      switch (versions[1]) {
      case 1:
        return HK2014;
      }
      return HKUNKVER;
    }

    default:
      return HKUNKVER;
    }
  };

  toolset = convert();

  if (toolset == HKUNKVER) {
    throw es::InvalidVersionError(toolset);
  }
}

void hkxHeaderData::SwapEndian() {
  FByteswapper(version);
  FByteswapper(numSections);
  FByteswapper(contentsSectionIndex);
  FByteswapper(contentsSectionOffset);
  FByteswapper(contentsClassNameSectionIndex);
  FByteswapper(contentsClassNameSectionOffset);
  FByteswapper(flags);
  FByteswapper(maxpredicate);
  FByteswapper(predicateArraySizePlusPadding);
}

void hkxSectionHeader::Load(BinReaderRef_e rd) {
  rd.SetRelativeOrigin(absoluteDataStart);

  const int32 virtualEOF =
      (exportsOffset == -1U ? importsOffset : exportsOffset);
  const int32 circaNumLocalFixps =
      (globalFixupsOffset - localFixupsOffset) / sizeof(hkxLocalFixup);
  const int32 circaNumGlobalFixps =
      (virtualFixupsOffset - globalFixupsOffset) / sizeof(hkxGlobalFixup);
  const int32 circaNumVirtualFixps =
      (virtualEOF - virtualFixupsOffset) / sizeof(hkxVirtualFixup);

  localFixups.reserve(circaNumLocalFixps);
  globalFixups.reserve(circaNumGlobalFixps);
  virtualFixups.reserve(circaNumVirtualFixps);

  rd.Seek(localFixupsOffset);
  rd.ReadContainer(localFixups, circaNumLocalFixps);
  rd.Seek(globalFixupsOffset);
  rd.ReadContainer(globalFixups, circaNumGlobalFixps);
  rd.Seek(virtualFixupsOffset);
  rd.ReadContainer(virtualFixups, circaNumVirtualFixps);
  rawLocalFixups = localFixups;
  rawGlobalFixups = globalFixups;
  rawVirtualFixups = virtualFixups;

  rd.ResetRelativeOrigin();
}

void hkxSectionHeader::LoadBuffer(BinReaderRef_e rd) {

  if (!bufferSize)
    return;

  rd.Seek(absoluteDataStart);
  rd.ReadContainer(buffer, localFixupsOffset);
}

void hkxSectionHeader::LinkBuffer() {
  char *sectionBuffer = &buffer[0];
  using ptrType = esPointerX64<char>;

  for (auto &lf : localFixups) {
    if (lf.pointer != -1) {
      ptrType *ptrPtr = reinterpret_cast<ptrType *>(sectionBuffer + lf.pointer);
      *ptrPtr = sectionBuffer + lf.destination;
    }
  }

  for (auto &gf : globalFixups) {
    if (gf.pointer != -1) {
      ptrType *ptrPtr = reinterpret_cast<ptrType *>(sectionBuffer + gf.pointer);
      *ptrPtr = &header->sections[gf.sectionid].buffer[0] + gf.destination;
    }
  }

  es::Dispose(localFixups);
  es::Dispose(globalFixups);
}

void hkxSectionHeader::LinkBuffer86() {
  char *sectionBuffer = &buffer[0];
  using ptrType = esPointerX86<char>;

  for (auto &lf : localFixups) {
    if (lf.pointer != -1) {
      ptrType *ptrPtr = reinterpret_cast<ptrType *>(sectionBuffer + lf.pointer);
      *ptrPtr = sectionBuffer + lf.destination;
    }
  }

  for (auto &gf : globalFixups) {
    if (gf.pointer != -1) {
      ptrType *ptrPtr = reinterpret_cast<ptrType *>(sectionBuffer + gf.pointer);
      *ptrPtr = &header->sections[gf.sectionid].buffer[0] + gf.destination;
    }
  }

  es::Dispose(localFixups);
  es::Dispose(globalFixups);
}

void hkxSectionHeader::Finalize() {
  char *sectionBuffer = &buffer[0];

  for (auto &vf : virtualFixups) {
    if (vf.dataoffset != -1) {
      std::string_view clName =
          header->sections[vf.sectionid].buffer.data() + vf.classnameoffset;
      const JenHash chash(clName);
      CRule rule(header->toolset, header->layout.reusePaddingOptimization,
                 header->layout.bytesInPointer > 4);
      IhkVirtualClass *clsn = hkVirtualClass::Create(chash, rule);
      auto cls = const_cast<hkVirtualClass *>(
          safe_deref_cast<const hkVirtualClass>(clsn));

      if (cls) {
        cls->SetDataPointer(sectionBuffer + vf.dataoffset);
        cls->className = clName;
        cls->AddHash(clName);
        cls->header = header;
        if (!header->layout.littleEndian)
          cls->SwapEndian();
        virtualClasses.emplace_back(clsn);
        cls->Process();
      }
    }
  }

  es::Dispose(virtualFixups);
}

void hkxSectionHeader::hkxLocalFixup::SwapEndian() {
  FByteswapper(pointer);
  FByteswapper(destination);
}

void hkxSectionHeader::hkxGlobalFixup::SwapEndian() {
  FByteswapper(pointer);
  FByteswapper(destination);
  FByteswapper(sectionid);
}

void hkxSectionHeader::hkxVirtualFixup::SwapEndian() {
  FByteswapper(dataoffset);
  FByteswapper(sectionid);
  FByteswapper(classnameoffset);
}

void hkxSectionHeaderData::SwapEndian() {
  FByteswapper(absoluteDataStart);
  FByteswapper(localFixupsOffset);
  FByteswapper(globalFixupsOffset);
  FByteswapper(virtualFixupsOffset);
  FByteswapper(exportsOffset);
  FByteswapper(importsOffset);
  FByteswapper(bufferSize);
}

void hkxHeader::Save(BinWritterRef_e wr, const VirtualClasses &classes) const {
  if (!sections.empty()) {
    throw std::logic_error(
        "Cannot save loaded header! Use IhkPackFile::ToPackFile().");
  }

  std::unordered_map<std::string, size_t> cnOffsetMap;
  const int32 dataSectionId = 2;

  wr.SwapEndian((layout.littleEndian != 0) != LittleEndian());

  hkxHeaderData hdr = *this;
  hdr.numSections = 3;
  hdr.contentsSectionIndex = dataSectionId;
  if (toolset == HK550)
    hdr.flags = 0xFFFFFFFF;
  hdr.maxpredicate = -1;
  hdr.predicateArraySizePlusPadding = -1;
  hdr.contentsVersion[sizeof(hdr.contentsVersion) - 1] = char(0xFF);
  wr.Write<hkxHeaderData>(hdr);

  hkxSectionHeader classSection{};
  std::string_view classSectionTag = "__classnames__";
  memset(classSection.sectionTag, 0, sizeof(classSection.sectionTag));
  classSection.sectionTag[sizeof(classSection.sectionTag) - 1] = char(0xFF);
  memcpy(classSection.sectionTag, classSectionTag.data(),
         classSectionTag.size() + 1);

  wr.Push();
  wr.Write<hkxSectionHeaderData>(classSection);

  if (version == 11) {
    wr.Skip(16);
  }

  hkxSectionHeader typeSection{};
  std::string_view typeSectionTag = "__types__";
  memset(typeSection.sectionTag, 0, sizeof(typeSection.sectionTag));
  typeSection.sectionTag[sizeof(typeSection.sectionTag) - 1] = char(0xFF);
  memcpy(typeSection.sectionTag, typeSectionTag.data(),
         typeSectionTag.size() + 1);

  wr.Write<hkxSectionHeaderData>(typeSection);

  if (version == 11) {
    wr.Skip(16);
  }

  hkxSectionHeader mainSection{};
  std::string_view sectionTag = "__data__";
  memset(mainSection.sectionTag, 0, sizeof(mainSection.sectionTag));
  mainSection.sectionTag[sizeof(mainSection.sectionTag) - 1] = char(0xFF);
  memcpy(mainSection.sectionTag, sectionTag.data(), sectionTag.size() + 1);

  wr.Write<hkxSectionHeaderData>(mainSection);

  if (version == 11) {
    wr.Skip(16);
  }

  classSection.absoluteDataStart = static_cast<uint32>(wr.Tell());
  wr.SetRelativeOrigin(wr.Tell(), false);

  VirtualClasses refClasses;
  hkFixups fixups;
  std::unordered_map<const IhkVirtualClass *, IhkVirtualClass *> clsRemap;

  auto getClassSignature = [&](std::string_view name) {
    if (toolset == HK550) {
      static const std::pair<std::string_view, uint32> signatures[] = {
          {"hkClass", 0x38771F8E},
          {"hkClassMember", 0xA5240F57},
          {"hkClassEnum", 0x8A3609CF},
          {"hkClassEnumItem", 0xCE6F8A6C},
          {"hkRootLevelContainer", 0xF598A34E},
          {"hkaAnimatedReferenceFrame", 0xDA8C7D7D},
          {"hkxMeshUserChannelInfo", 0x64E9A03C},
          {"hkxAttribute", 0x914DA6C1},
          {"hkaAnnotationTrackAnnotation", 0x731888CA},
          {"hkaSkeletalAnimation", 0x98F9313D},
          {"hkBaseObject", 0xE0708A00},
          {"hkxAttributeHolder", 0x445A443A},
          {"hkxMaterial", 0xF2EC0C9C},
          {"hkxIndexBuffer", 0x1C8A8C37},
          {"hkxMaterialTextureStage", 0xE085BA9F},
          {"hkaInterleavedSkeletalAnimation", 0x62B02E7B},
          {"hkaBone", 0xA74011F0},
          {"hkxMesh", 0x72E8E849},
          {"hkxMeshSection", 0x912C8863},
          {"hkaBoneAttachment", 0x8BDD3E9A},
          {"hkaDefaultAnimatedReferenceFrame", 0x122F506B},
          {"hkaSkeleton", 0x334DBE6C},
          {"hkRootLevelContainerNamedVariant", 0x853A899C},
          {"hkxVertexFormat", 0x379FD194},
          {"hkaMeshBindingMapping", 0x4DA6A6F4},
          {"hkxAttributeGroup", 0x1667C01C},
          {"hkxVertexBuffer", 0x57061454},
          {"hkaMeshBinding", 0xCDB31E0C},
          {"hkReferencedObject", 0x3B1C1113},
          {"hkaAnimationContainer", 0xF456626D},
          {"hkaAnnotationTrack", 0x846FC690},
          {"hkaAnimationBinding", 0xFB496074},
      };

      for (auto &sig : signatures) {
        if (sig.first == name) {
          return sig.second;
        }
      }
    } else if (toolset == HK2010_1 || toolset == HK2010_2) {
      static const std::pair<std::string_view, uint32> signatures[] = {
          {"hkClass", 0x75585EF6},
          {"hkClassMember", 0x5C7EA4C2},
          {"hkClassEnum", 0x8A3609CF},
          {"hkClassEnumItem", 0xCE6F8A6C},
          {"hkRootLevelContainer", 0x2772C11E},
          {"hkaAnimationContainer", 0x8DC20333},
          {"hkaInterleavedUncompressedAnimation", 0x930AF031},
          {"hkaDefaultAnimatedReferenceFrame", 0x6D85E445},
          {"hkaAnimationBinding", 0x66EAC971},
      };

      for (auto &sig : signatures) {
        if (sig.first == name) {
          return sig.second;
        }
      }
    }

    return JenHash(name).raw();
  };

  auto writeClassName = [&](std::string_view name) {
    wr.Write(getClassSignature(name));
    wr.Write('\t');
    cnOffsetMap[std::string(name)] = wr.Tell();
    wr.WriteContainer(name);
    wr.Skip(1);
  };

  static const std::string_view reqClassNames[] = {
      "hkClass", "hkClassMember", "hkClassEnum", "hkClassEnumItem"};
  for (auto &c : reqClassNames) {
    writeClassName(c);
  }

  if (toolset == HK550) {
    static const std::string_view legacy550ClassNames[] = {
        "hkRootLevelContainer",
        "hkaAnimatedReferenceFrame",
        "hkxMeshUserChannelInfo",
        "hkxAttribute",
        "hkaAnnotationTrackAnnotation",
        "hkaSkeletalAnimation",
        "hkBaseObject",
        "hkxAttributeHolder",
        "hkxMaterial",
        "hkxIndexBuffer",
        "hkxMaterialTextureStage",
        "hkaInterleavedSkeletalAnimation",
        "hkaBone",
        "hkxMesh",
        "hkxMeshSection",
        "hkaBoneAttachment",
        "hkaDefaultAnimatedReferenceFrame",
        "hkaSkeleton",
        "hkRootLevelContainerNamedVariant",
        "hkxVertexFormat",
        "hkaMeshBindingMapping",
        "hkxAttributeGroup",
        "hkxVertexBuffer",
        "hkaMeshBinding",
        "hkReferencedObject",
        "hkaAnimationContainer",
        "hkaAnnotationTrack",
        "hkaAnimationBinding",
    };

    for (auto &c : legacy550ClassNames) {
      if (!cnOffsetMap.contains(std::string(c))) {
        writeClassName(c);
      }
    }
  }

  CRule rule(toolset, layout.reusePaddingOptimization,
             layout.bytesInPointer > 4);

  for (auto &c : classes) {
    auto dc = checked_deref_cast<const hkVirtualClass>(c.get());
    auto clName = dc->GetClassName(toolset);
    auto nClass = hkVirtualClass::Create(clName, rule);

    if (!nClass) {
      printerror("[Havok] Cannot export unregistered class: " << clName);
      continue;
    }

    auto cls = const_cast<hkVirtualClass *>(
        checked_deref_cast<const hkVirtualClass>(nClass));

    clsRemap[c.get()] = nClass;
    cls->Reflect(c.get());

    if (wr.SwappedEndian()) {
      cls->SwapEndian();
    }

    refClasses.emplace_back(nClass);

    auto mapIt = cnOffsetMap.find(std::string(clName));
    if (mapIt != cnOffsetMap.end()) {
      fixups.finals.emplace_back(mapIt->second);
    } else {
      const size_t entryOff = wr.Tell();
      writeClassName(clName);
      size_t nameOff = entryOff + 5;
      fixups.finals.emplace_back(nameOff);
    }

    if (toolset < HK700 && clName == "hkaSkeleton") {
      size_t boneOff;
      auto boneIt = cnOffsetMap.find("hkaBone");
      if (boneIt != cnOffsetMap.end()) {
        boneOff = boneIt->second;
      } else {
        const size_t entryOff = wr.Tell();
        writeClassName("hkaBone");
        boneOff = entryOff + 5;
      }

      const size_t numBones =
          checked_deref_cast<const hkaSkeleton>(c.get())->GetNumBones();

      for (size_t i = 0; i < numBones; i++) {
        fixups.finals.emplace_back(boneOff, c.get());
      }
    }

    if (toolset < HK700) {
      auto hkAnim = safe_deref_cast<const hkaAnimation>(c.get());

      if (hkAnim && hkAnim->GetAnimationType() == HK_INTERLEAVED_ANIMATION) {
        size_t trackOff;
        auto trackIt = cnOffsetMap.find("hkaAnnotationTrack");
        if (trackIt != cnOffsetMap.end()) {
          trackOff = trackIt->second;
        } else {
          const size_t entryOff = wr.Tell();
          writeClassName("hkaAnnotationTrack");
          trackOff = entryOff + 5;
        }

        for (size_t i = 0; i < hkAnim->GetNumAnnotations(); i++) {
          fixups.finals.emplace_back(trackOff, c.get());
        }
      }
    }

    if (toolset < HK700) {
      if (auto root = dynamic_cast<const hkRootLevelContainer *>(c.get())) {
        for (auto &variant : *root) {
          if (variant.pointer ||
              std::string_view(variant.className) != "hkxScene") {
            continue;
          }

          if (cnOffsetMap.contains("hkxScene")) {
            break;
          }

          writeClassName("hkxScene");
          break;
        }
      }
    }
  }

  wr.ResetRelativeOrigin(false);
  wr.ApplyPadding();
  classSection.bufferSize =
      static_cast<uint32>(wr.Tell() - classSection.absoluteDataStart);
  classSection.exportsOffset = classSection.bufferSize;
  classSection.globalFixupsOffset = classSection.bufferSize;
  classSection.importsOffset = classSection.bufferSize;
  classSection.localFixupsOffset = classSection.bufferSize;
  classSection.virtualFixupsOffset = classSection.bufferSize;
  wr.ApplyPadding();

  typeSection.absoluteDataStart = static_cast<uint32>(wr.Tell());
  typeSection.bufferSize = 0;
  typeSection.exportsOffset = 0;
  typeSection.globalFixupsOffset = 0;
  typeSection.importsOffset = 0;
  typeSection.localFixupsOffset = 0;
  typeSection.virtualFixupsOffset = 0;
  wr.ApplyPadding();

  mainSection.absoluteDataStart = static_cast<uint32>(wr.Tell());
  wr.SetRelativeOrigin(wr.Tell(), false);

  size_t curFixup = 0;
  std::unordered_map<IhkVirtualClass *, size_t> savedClasses;

  for (auto &c : refClasses) {
    wr.ApplyPadding();
    const auto clsOffset = wr.Tell();
    savedClasses[c.get()] = clsOffset;
    fixups.finals[curFixup++].destination = clsOffset;

    auto cls = checked_deref_cast<const hkVirtualClass>(c.get());
    cls->Save(wr, fixups);

    while (curFixup < fixups.finals.size() &&
           fixups.finals[curFixup].destClass) {
      curFixup++;
    }
  }

  for (auto &l : fixups.locals) {
    if (l.destClass) {
      auto sClass = clsRemap[l.destClass];
      if (sClass) {
        hkxSectionHeader::hkxGlobalFixup gFix;
        gFix.sectionid = dataSectionId;
        gFix.pointer = static_cast<int32>(l.strOffset);
        gFix.destination = static_cast<int32>(savedClasses[sClass]);
        mainSection.globalFixups.push_back(gFix);
      }
    } else if (l.destination != static_cast<size_t>(-1)) {
      hkxSectionHeader::hkxLocalFixup lFix;
      lFix.pointer = static_cast<int32>(l.strOffset);
      lFix.destination = static_cast<int32>(l.destination);
      mainSection.localFixups.push_back(lFix);
    }
  }

  for (auto &l : fixups.finals) {
    hkxSectionHeader::hkxVirtualFixup lFix;
    lFix.sectionid = 0;
    lFix.classnameoffset = l.strOffset;
    lFix.dataoffset = l.destination;
    mainSection.virtualFixups.push_back(lFix);
  }

  for (auto &l : fixups.globals) {
    hkxSectionHeader::hkxGlobalFixup lFix;
    lFix.sectionid = dataSectionId;
    lFix.pointer = l.strOffset;
    lFix.destination = l.destination;
    mainSection.globalFixups.push_back(lFix);
  }

  if (fixups.legacyScene && !fixups.legacyScene->data.empty()) {
    wr.ApplyPadding();
    const size_t sceneOffset = wr.Tell();
    wr.WriteBuffer(fixups.legacyScene->data.data(),
                   fixups.legacyScene->data.size());

    for (const auto &lf : fixups.legacyScene->localFixups) {
      hkxSectionHeader::hkxLocalFixup lFix;
      lFix.pointer = static_cast<int32>(sceneOffset + lf.pointer);
      lFix.destination = static_cast<int32>(sceneOffset + lf.destination);
      mainSection.localFixups.push_back(lFix);
    }

    auto cnIt = cnOffsetMap.find(fixups.legacyScene->className);
    if (cnIt != cnOffsetMap.end()) {
      hkxSectionHeader::hkxVirtualFixup vFix;
      vFix.sectionid = 0;
      vFix.classnameoffset = static_cast<int32>(cnIt->second);
      vFix.dataoffset = static_cast<int32>(sceneOffset);
      mainSection.virtualFixups.push_back(vFix);
    }

    if (fixups.legacyScene->variantPtrOff != static_cast<size_t>(-1)) {
      hkxSectionHeader::hkxGlobalFixup gFix;
      gFix.sectionid = dataSectionId;
      gFix.pointer = static_cast<int32>(fixups.legacyScene->variantPtrOff);
      gFix.destination = static_cast<int32>(sceneOffset);
      mainSection.globalFixups.push_back(gFix);
    }
  }

  wr.ApplyPadding();
  mainSection.localFixupsOffset = static_cast<int32>(wr.Tell());
  wr.WriteContainer(mainSection.localFixups);
  if (mainSection.localFixups.size() & 1) {
    wr.Write<int64>(-1);
  }

  wr.ApplyPadding();
  mainSection.globalFixupsOffset = static_cast<int32>(wr.Tell());
  wr.WriteContainer(mainSection.globalFixups);
  const size_t preVirtualPad = GetPadding(wr.Tell(), 16) / 4;
  for (size_t p = 0; p < preVirtualPad; p++) {
    wr.Write<int32>(-1);
  }
  mainSection.virtualFixupsOffset = static_cast<int32>(wr.Tell());
  wr.WriteContainer(mainSection.virtualFixups);

  const size_t pad = GetPadding(wr.Tell(), 16) / 4;

  for (size_t p = 0; p < pad; p++) {
    wr.Write<int32>(-1);
  }

  mainSection.bufferSize = static_cast<int32>(wr.Tell());
  mainSection.exportsOffset = mainSection.bufferSize;
  mainSection.importsOffset = mainSection.bufferSize;

  wr.ResetRelativeOrigin(false);
  wr.Pop();
  wr.Write<hkxSectionHeaderData>(classSection);

  if (version == 11) {
    wr.Skip(16);
  }

  wr.Write<hkxSectionHeaderData>(typeSection);

  if (version == 11) {
    wr.Skip(16);
  }

  wr.Write<hkxSectionHeaderData>(mainSection);
}
