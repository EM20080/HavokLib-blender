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
#include "hklib/hka_skeleton.hpp"
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
#include "types_embed.hpp"

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

  auto embedded = GetEmbeddedTypes(toolset, layout.littleEndian == 0);
  bool hasTypes = embedded.data != nullptr;
  const bool embeddedIsBE = embedded.isBigEndian;
  const uint8_t *typesBody = nullptr, *typesLf = nullptr, *typesGf = nullptr;
  const uint8_t *typesVf = nullptr, *typesCn = nullptr;
  uint32 typesBodySize = 0, typesLfSize = 0, typesGfSize = 0;
  uint32 typesVfSize = 0, typesCnSize = 0;
  std::unordered_map<std::string, int32> typesClassOffsets;
  std::unordered_map<std::string, size_t> cnOffsetMap;
  bool hasSceneBlob = false;
  const uint8_t *sceneBlobBody = nullptr;
  uint32 sceneBlobBodySize = 0;
  uint32 sceneBlobLfCount = 0;
  const uint8_t *sceneBlobLfData = nullptr;
  uint32 sceneBlobCnOff = 0;

  if (hasTypes) {
    memcpy(&typesBodySize, embedded.data, 4);
    memcpy(&typesLfSize, embedded.data + 4, 4);
    memcpy(&typesGfSize, embedded.data + 8, 4);
    memcpy(&typesVfSize, embedded.data + 12, 4);
    memcpy(&typesCnSize, embedded.data + 16, 4);
    typesBody = embedded.data + 20;
    typesLf = typesBody + typesBodySize;
    typesGf = typesLf + typesLfSize;
    typesVf = typesGf + typesGfSize;
    typesCn = typesVf + typesVfSize;

    const uint8_t *afterCn = typesCn + typesCnSize;
    if (afterCn + 4 <= embedded.data + embedded.size &&
        memcmp(afterCn, "SCEN", 4) == 0) {
      hasSceneBlob = true;
      const uint8_t *p = afterCn + 4;
      uint32 sceneGfCount, sceneVfCount;
      memcpy(&sceneBlobBodySize, p, 4); p += 4;
      memcpy(&sceneBlobLfCount,  p, 4); p += 4;
      memcpy(&sceneGfCount,      p, 4); p += 4;
      memcpy(&sceneVfCount,      p, 4); p += 4;
      sceneBlobBody   = p;
      sceneBlobLfData = p + sceneBlobBodySize;
      const uint8_t *vfPtr = sceneBlobLfData + 8 * sceneBlobLfCount;
      memcpy(&sceneBlobCnOff, vfPtr + 4, 4);
    }

    size_t cnOff = 0;
    while (cnOff + 5 < typesCnSize) {
      size_t nameStart = cnOff + 5;
      size_t nameEnd = nameStart;
      while (nameEnd < typesCnSize && typesCn[nameEnd] != 0)
        nameEnd++;
      std::string name(reinterpret_cast<const char *>(typesCn + nameStart),
                       nameEnd - nameStart);
      cnOffsetMap[name] = nameStart;
      cnOff = nameEnd + 1;
    }

    std::unordered_map<uint32, uint32> lfMap;
    for (size_t i = 0; i + 7 < typesLfSize; i += 8) {
      int32 src, dst;
      memcpy(&src, typesLf + i, 4);
      memcpy(&dst, typesLf + i + 4, 4);
      if (src >= 0 && dst >= 0)
        lfMap[src] = dst;
    }

    for (size_t i = 0; i + 11 < typesVfSize; i += 12) {
      int32 dataOff, secId, cnOffV;
      memcpy(&dataOff, typesVf + i, 4);
      memcpy(&secId, typesVf + i + 4, 4);
      memcpy(&cnOffV, typesVf + i + 8, 4);
      if (dataOff < 0 || cnOffV != 5)
        continue;
      uint32 namePtrOff = dataOff;
      auto it = lfMap.find(namePtrOff);
      if (it != lfMap.end() && it->second < typesBodySize) {
        const char *nameStr =
            reinterpret_cast<const char *>(typesBody + it->second);
        typesClassOffsets[nameStr] = dataOff;
      }
    }
  }

  const int32 dataSectionId = hasTypes ? 2 : 1;

  wr.SwapEndian((layout.littleEndian != 0) != LittleEndian());

  hkxHeaderData hdr = *this;
  if (hasTypes) {
    hdr.numSections = 3;
    hdr.contentsSectionIndex = 2;
    auto rootIt = cnOffsetMap.find("hkRootLevelContainer");
    if (rootIt != cnOffsetMap.end())
      hdr.contentsClassNameSectionOffset =
          static_cast<int32>(rootIt->second);
    if (toolset == HK550)
      hdr.flags = 0xFFFFFFFF;
    hdr.maxpredicate = -1;
    hdr.predicateArraySizePlusPadding = -1;
  }
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

  hkxSectionHeader typesSection{};
  if (hasTypes) {
    std::string_view typesSectionTag = "__types__";
    memset(typesSection.sectionTag, 0, sizeof(typesSection.sectionTag));
    typesSection.sectionTag[sizeof(typesSection.sectionTag) - 1] = char(0xFF);
    memcpy(typesSection.sectionTag, typesSectionTag.data(),
           typesSectionTag.size() + 1);
    wr.Write<hkxSectionHeaderData>(typesSection);
    if (version == 11) {
      wr.Skip(16);
    }
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

  if (hasTypes && typesCnSize > 0) {
    wr.WriteBuffer(reinterpret_cast<const char *>(typesCn), typesCnSize);
  } else {
    static const std::string_view reqClassNames[] = {
        "hkClass", "hkClassMember", "hkClassEnum", "hkClassEnumItem"};
    for (auto &c : reqClassNames) {
      wr.Write<uint32>(0);
      wr.Write('\t');
      cnOffsetMap[std::string(c)] = wr.Tell();
      wr.WriteContainer(c);
      wr.Skip(1);
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
      wr.Write<uint32>(0);
      wr.Write('\t');
      size_t nameOff = wr.Tell();
      fixups.finals.emplace_back(nameOff);
      cnOffsetMap[std::string(clName)] = nameOff;
      wr.WriteContainer(clName);
      wr.Skip(1);
    }

    if (toolset < HK700 && clName == "hkaSkeleton") {
      size_t boneOff;
      auto boneIt = cnOffsetMap.find("hkaBone");
      if (boneIt != cnOffsetMap.end()) {
        boneOff = boneIt->second;
      } else {
        wr.Write<uint32>(0);
        wr.Write('\t');
        boneOff = wr.Tell();
        cnOffsetMap["hkaBone"] = boneOff;
        wr.WriteT("hkaBone");
      }

      const size_t numBones =
          checked_deref_cast<const hkaSkeleton>(c.get())->GetNumBones();

      for (size_t i = 0; i < numBones; i++) {
        fixups.finals.emplace_back(boneOff, c.get());
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

  auto swap32Blob = [](const uint8_t *src, uint32 size) {
    std::vector<uint8_t> buf(src, src + size);
    for (uint32 i = 0; i + 3 < size; i += 4) {
      std::swap(buf[i + 0], buf[i + 3]);
      std::swap(buf[i + 1], buf[i + 2]);
    }
    return buf;
  };

  if (hasTypes) {
    typesSection.absoluteDataStart = static_cast<uint32>(wr.Tell());
    wr.SetRelativeOrigin(wr.Tell(), false);

    if (wr.SwappedEndian() && !embeddedIsBE) {
      auto swapped = swap32Blob(typesBody, typesBodySize);
      wr.WriteBuffer(reinterpret_cast<const char *>(swapped.data()), typesBodySize);
    } else {
      wr.WriteBuffer(reinterpret_cast<const char *>(typesBody), typesBodySize);
    }
    wr.ApplyPadding();

    typesSection.localFixupsOffset = static_cast<uint32>(wr.Tell());
    if (wr.SwappedEndian()) {
      auto swapped = swap32Blob(typesLf, typesLfSize);
      wr.WriteBuffer(reinterpret_cast<const char *>(swapped.data()), typesLfSize);
    } else {
      wr.WriteBuffer(reinterpret_cast<const char *>(typesLf), typesLfSize);
    }
    wr.ApplyPadding();

    typesSection.globalFixupsOffset = static_cast<uint32>(wr.Tell());
    if (wr.SwappedEndian()) {
      auto swapped = swap32Blob(typesGf, typesGfSize);
      wr.WriteBuffer(reinterpret_cast<const char *>(swapped.data()), typesGfSize);
    } else {
      wr.WriteBuffer(reinterpret_cast<const char *>(typesGf), typesGfSize);
    }

    typesSection.virtualFixupsOffset = static_cast<uint32>(wr.Tell());
    if (wr.SwappedEndian()) {
      auto swapped = swap32Blob(typesVf, typesVfSize);
      wr.WriteBuffer(reinterpret_cast<const char *>(swapped.data()), typesVfSize);
    } else {
      wr.WriteBuffer(reinterpret_cast<const char *>(typesVf), typesVfSize);
    }

    const size_t typesPad = GetPadding(wr.Tell(), 16) / 4;
    for (size_t p = 0; p < typesPad; p++) {
      wr.Write<int32>(-1);
    }

    typesSection.bufferSize = static_cast<uint32>(wr.Tell());
    typesSection.exportsOffset = typesSection.bufferSize;
    typesSection.importsOffset = typesSection.bufferSize;

    wr.ResetRelativeOrigin(false);
    wr.ApplyPadding();
  }

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

  size_t sceneOffset = 0;
  if (hasSceneBlob) {
    wr.ApplyPadding();
    sceneOffset = wr.Tell();
    if (wr.SwappedEndian() && !embeddedIsBE) {
      auto swapped = swap32Blob(sceneBlobBody, sceneBlobBodySize);
      wr.WriteBuffer(reinterpret_cast<const char *>(swapped.data()), sceneBlobBodySize);
    } else {
      wr.WriteBuffer(reinterpret_cast<const char *>(sceneBlobBody), sceneBlobBodySize);
    }
    for (uint32 i = 0; i < sceneBlobLfCount; i++) {
      uint32 ptrRel, dstRel;
      memcpy(&ptrRel, sceneBlobLfData + i * 8, 4);
      memcpy(&dstRel, sceneBlobLfData + i * 8 + 4, 4);
      hkxSectionHeader::hkxLocalFixup lFix;
      lFix.pointer     = static_cast<int32>(sceneOffset + ptrRel);
      lFix.destination = static_cast<int32>(sceneOffset + dstRel);
      mainSection.localFixups.push_back(lFix);
    }
    const auto cnIt = cnOffsetMap.find("hkxScene");
    fixups.finals.emplace_back(
        cnIt != cnOffsetMap.end() ? cnIt->second
                                  : static_cast<size_t>(sceneBlobCnOff),
        sceneOffset);
    if (fixups.hasHkxSceneVariant) {
      hkxSectionHeader::hkxGlobalFixup gFix;
      gFix.sectionid   = dataSectionId;
      gFix.pointer     = static_cast<int32>(fixups.hkxScenePtrOff);
      gFix.destination = static_cast<int32>(sceneOffset);
      mainSection.globalFixups.push_back(gFix);
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

  if (hasTypes) {
    for (auto &[ptr, name] : fixups.classDescs) {
      auto it = typesClassOffsets.find(name);
      if (it != typesClassOffsets.end()) {
        hkxSectionHeader::hkxGlobalFixup gFix;
        gFix.sectionid = 1;
        gFix.pointer = static_cast<int32>(ptr);
        gFix.destination = it->second;
        mainSection.globalFixups.push_back(gFix);
      }
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

  if (hasTypes) {
    wr.Write<hkxSectionHeaderData>(typesSection);
    if (version == 11) {
      wr.Skip(16);
    }
  }

  wr.Write<hkxSectionHeaderData>(mainSection);
}
