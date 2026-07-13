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

#include "format_old.hpp"
#include "internal/hk_internal_api.hpp"
#include "spike/crypto/jenkinshash.hpp"
#include "spike/except.hpp"
#include "spike/type/pointer.hpp"
#include "spike/io/binreader.hpp"

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
      case 2:
        return HK2013_2;
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

