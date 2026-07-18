/*  Havok Format Library
    Copyright(C) 2016-2026 Lukas Cone

    This program is free software : you can redistribute it and / or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "fixups.hpp"
#include "hklib/hk_packfile.hpp"
#include "internal/hk_internal_api.hpp"
#include "spike/io/binwritter.hpp"
#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace {

struct TagfileMember {
  const char *name;
  uint8 flags;
  uint16 offset;
  uint16 type;
};

struct TagfileTemplate {
  const char *name;
  uint16 value;
};

struct TagfileInterface {
  uint16 type;
  uint8 flags;
};

struct TagfileType {
  const char *name;
  uint16 parent;
  uint8 flags;
  uint32 subTypeFlags;
  uint16 pointer;
  uint16 version;
  uint16 byteSize;
  uint8 alignment;
  uint32 abstractValue;
  uint32 hash;
  uint16 memberBegin;
  uint16 numMembers;
  uint16 templateBegin;
  uint16 numTemplates;
  uint16 interfaceBegin;
  uint16 numInterfaces;
};

#include "metadata_new.inl"

enum TagfileSubType : uint8 {
  TagfileString = 3,
  TagfilePointer = 6,
  TagfileClass = 7,
  TagfileArray = 8,
  TagfileTuple = 0x28,
};

struct ReflectedObject {
  const IhkVirtualClass *source;
  uint16 type;
  uint32 offset;
  std::string data;
  std::vector<hkFixup> fixups;
};

struct TagItem {
  uint16 type;
  uint32 offset;
  uint32 count;
  uint32 kind;
};

struct Chunk {
  size_t offset;
  uint32 flags;
};

enum : uint32 {
  BranchChunk = 0,
  LeafChunk = 0x40000000,
  PointerItem = 0x10000000,
  ValueItem = 0x20000000,
};

void WriteBigEndian(BinWritterRef_e wr, uint32 value) {
  const char data[] = {
      static_cast<char>(value >> 24), static_cast<char>(value >> 16),
      static_cast<char>(value >> 8), static_cast<char>(value)};
  wr.WriteBuffer(data, sizeof(data));
}

void WriteLittleEndian(BinWritterRef_e wr, uint32 value) {
  const char data[] = {
      static_cast<char>(value), static_cast<char>(value >> 8),
      static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
  wr.WriteBuffer(data, sizeof(data));
}

void Pad(BinWritterRef_e wr, size_t alignment) {
  const size_t amount = (alignment - wr.Tell() % alignment) % alignment;
  wr.Skip(amount);
}

void Pad(std::string &data, size_t alignment) {
  data.resize((data.size() + alignment - 1) & ~(alignment - 1));
}

Chunk BeginChunk(BinWritterRef_e wr, const char *tag, uint32 flags) {
  Chunk chunk{wr.Tell(), flags};
  wr.Skip(4);
  wr.WriteBuffer(tag, 4);
  return chunk;
}

void EndChunk(BinWritterRef_e wr, Chunk chunk) {
  Pad(wr, 4);
  const size_t end = wr.Tell();
  wr.Seek(chunk.offset);
  WriteBigEndian(wr,
                 static_cast<uint32>(end - chunk.offset) | chunk.flags);
  wr.Seek(end);
}

void WritePacked(BinWritterRef_e wr, uint32 value) {
  if (value < 0x80) {
    wr.Write<uint8>(static_cast<uint8>(value));
  } else if (value < 0x4000) {
    value |= 0x8000;
    const char data[] = {static_cast<char>(value >> 8),
                         static_cast<char>(value)};
    wr.WriteBuffer(data, sizeof(data));
  } else if (value < 0x200000) {
    wr.Write<uint8>(static_cast<uint8>((value >> 16) | 0xc0));
    const char data[] = {static_cast<char>(value >> 8),
                         static_cast<char>(value)};
    wr.WriteBuffer(data, sizeof(data));
  } else {
    WriteBigEndian(wr, value | 0xe0000000);
  }
}

uint16 FindType(std::string_view name) {
  for (uint16 i = 1; i < std::size(tagfileTypes); i++) {
    if (name == tagfileTypes[i].name) {
      return i;
    }
  }
  return 0;
}

uint16 SuperType(uint16 type) {
  while (type && !(tagfileTypes[type].flags & 1)) {
    type = tagfileTypes[type].parent;
  }
  return type;
}

uint8 SubType(uint16 type) {
  type = SuperType(type);
  return type ? static_cast<uint8>(tagfileTypes[type].subTypeFlags & 0x7f)
              : 0;
}

void CorrectSplineLayout(ReflectedObject &object) {
  if (std::string_view(tagfileTypes[object.type].name) !=
          "hkaSplineCompressedAnimation" ||
      object.data.size() < 176) {
    return;
  }

  object.data.insert(176, 8, 0);
  for (hkFixup &fixup : object.fixups) {
    if (fixup.strOffset >= 176) {
      fixup.strOffset += 8;
    }
    if (!fixup.destClass && fixup.destination != static_cast<size_t>(-1) &&
        fixup.destination >= 176) {
      fixup.destination += 8;
    }
  }
}

// Only animation tagfiles with SDKV 20150100, 20150200, 20160100, or
// 20160200 are written here.
const char *SdkVersion(hkToolset toolset) {
  switch (toolset) {
  case HK2015_1:
    return "20150100";
  case HK2015_2:
    return "20150200";
  case HK2016_1:
    return "20160100";
  case HK2016_2:
    return "20160200";
  default:
    return nullptr;
  }
}

class TagWriter {
public:
  TagWriter(BinWritterRef_e writer,
            const IhkPackFile::VirtualClasses &classes,
            const char *sdkVersion)
      : wr(writer), sourceClasses(classes), sdkVersion(sdkVersion),
        metadataToType(std::size(tagfileTypes)),
        scannedTypes(std::size(tagfileTypes)) {}

  void Write() {
    ReflectClasses();
    BuildData();
    BuildItems();

    const Chunk root = BeginChunk(wr, "TAG0", BranchChunk);
    const Chunk sdk = BeginChunk(wr, "SDKV", LeafChunk);
    wr.WriteBuffer(sdkVersion, 8);
    EndChunk(wr, sdk);

    const Chunk dataChunk = BeginChunk(wr, "DATA", LeafChunk);
    wr.WriteBuffer(data.data(), data.size());
    Pad(wr, 16);
    EndChunk(wr, dataChunk);

    WriteTypes();
    WriteIndex();
    EndChunk(wr, root);
  }

private:
  void ReflectClass(const IhkVirtualClass *source) {
    const auto *sourceClass = dynamic_cast<const hkVirtualClass *>(source);
    if (!sourceClass) {
      return;
    }

    const std::string_view className = sourceClass->GetClassName(HK2016_1);
    const uint16 type = FindType(className);
    if (!type) {
      return;
    }

    std::unique_ptr<IhkVirtualClass> reflected(
        hkVirtualClass::Create(JenHash(className), {HK2016_1, 0, 1}));
    auto *reflectedClass = dynamic_cast<hkVirtualClass *>(reflected.get());
    if (!reflectedClass) {
      return;
    }

    reflectedClass->Reflect(source);
    std::ostringstream stream(std::ios::out | std::ios::binary);
    BinWritterRef_e objectWriter(stream);
    hkFixups fixups;
    reflectedClass->Save(objectWriter, fixups);

    ReflectedObject object{source, type, 0, stream.str(),
                           std::move(fixups.locals)};
    CorrectSplineLayout(object);
    objects.emplace_back(std::move(object));
  }

  void ReflectClasses() {
    for (const auto &source : sourceClasses) {
      const auto *sourceClass = dynamic_cast<const hkVirtualClass *>(source.get());
      if (sourceClass &&
          sourceClass->GetClassName(HK2016_1) == "hkRootLevelContainer") {
        ReflectClass(source.get());
      }
    }
    for (const auto &source : sourceClasses) {
      const auto *sourceClass = dynamic_cast<const hkVirtualClass *>(source.get());
      if (!sourceClass ||
          sourceClass->GetClassName(HK2016_1) != "hkRootLevelContainer") {
        ReflectClass(source.get());
      }
    }
  }

  void BuildData() {
    for (ReflectedObject &object : objects) {
      Pad(data, 16);
      object.offset = static_cast<uint32>(data.size());
      data.append(object.data);

      for (hkFixup fixup : object.fixups) {
        fixup.strOffset += object.offset;
        if (!fixup.destClass &&
            fixup.destination != static_cast<size_t>(-1)) {
          fixup.destination += object.offset;
        }
        fixups.emplace_back(fixup);
      }
    }
    Pad(data, 16);
    for (const hkFixup &fixup : fixups) {
      fixupByOffset[fixup.strOffset] = &fixup;
    }
  }

  void ScanType(uint16 type) {
    if (!type || scannedTypes[type]) {
      return;
    }

    scannedTypes[type] = 1;
    metadataToType[type] = static_cast<uint16>(types.size());
    types.push_back(type);
    const TagfileType &value = tagfileTypes[type];

    for (uint16 i = 0; i < value.numTemplates; i++) {
      const TagfileTemplate &item =
          tagfileTemplates[value.templateBegin + i];
      if (item.name[0] == 't') {
        ScanType(item.value);
      }
    }
    ScanType(value.parent);
    ScanType(value.pointer);
    for (uint16 i = 0; i < value.numMembers; i++) {
      ScanType(tagfileMembers[value.memberBegin + i].type);
    }
    for (uint16 i = 0; i < value.numInterfaces; i++) {
      ScanType(tagfileInterfaces[value.interfaceBegin + i].type);
    }
  }

  void BuildItems() {
    items.push_back({});
    types.push_back(0);

    for (const ReflectedObject &object : objects) {
      const uint32 item = static_cast<uint32>(items.size());
      items.push_back({object.type, object.offset, 1, PointerItem});
      objectItems[object.source] = item;
      destinationItems[object.offset] = item;
      ScanType(object.type);
    }

    for (size_t item = 1; item < items.size(); item++) {
      const TagItem value = items[item];
      const uint16 element = SuperType(value.type);
      if (!element || !tagfileTypes[element].byteSize) {
        continue;
      }
      for (uint32 i = 0; i < value.count; i++) {
        ProcessValue(value.type,
                     value.offset + i * tagfileTypes[element].byteSize);
      }
    }
  }

  void ProcessClass(uint16 type, uint32 offset) {
    const TagfileType &value = tagfileTypes[type];
    if (value.parent) {
      ProcessClass(value.parent, offset);
    }
    for (uint16 i = 0; i < value.numMembers; i++) {
      const TagfileMember &member =
          tagfileMembers[value.memberBegin + i];
      if (!(member.flags & 1)) {
        ProcessValue(member.type, offset + member.offset);
      }
    }
  }

  void ProcessValue(uint16 type, uint32 offset) {
    const uint16 base = SuperType(type);
    if (!base || offset >= data.size()) {
      return;
    }

    const uint8 subType = SubType(base);
    if (subType == TagfileString || subType == TagfilePointer ||
        subType == TagfileArray) {
      ProcessReference(type, base, offset);
    } else if (subType == TagfileClass) {
      ProcessClass(type, offset);
    } else if (subType == TagfileTuple) {
      const TagfileType &tuple = tagfileTypes[base];
      const uint16 element = SuperType(tuple.pointer);
      if (!element || !tagfileTypes[element].byteSize) {
        return;
      }
      const uint32 count = tuple.subTypeFlags >> 8;
      for (uint32 i = 0; i < count; i++) {
        ProcessValue(tuple.pointer,
                     offset + i * tagfileTypes[element].byteSize);
      }
    }
  }

  void ProcessReference(uint16 type, uint16 base, uint32 offset) {
    const auto foundFixup = fixupByOffset.find(offset);
    if (foundFixup == fixupByOffset.end()) {
      return;
    }

    const hkFixup &fixup = *foundFixup->second;
    uint32 item = 0;
    const uint8 subType = SubType(base);

    if (subType == TagfilePointer) {
      if (fixup.destClass) {
        const auto foundItem = objectItems.find(fixup.destClass);
        if (foundItem != objectItems.end()) {
          item = foundItem->second;
        }
      }
    } else if (fixup.destination != static_cast<size_t>(-1) &&
               fixup.destination < data.size()) {
      const uint32 destination = static_cast<uint32>(fixup.destination);
      const auto foundItem = destinationItems.find(destination);
      if (foundItem != destinationItems.end()) {
        item = foundItem->second;
      } else if (subType == TagfileString) {
        uint32 count = 1;
        while (destination + count <= data.size() &&
               data[destination + count - 1]) {
          count++;
        }
        const uint16 charType = FindType("char");
        item = static_cast<uint32>(items.size());
        items.push_back({charType, destination, count, ValueItem});
        destinationItems[destination] = item;
        ScanType(charType);
      } else if (subType == TagfileArray && offset + 12 <= data.size()) {
        uint32 count = 0;
        std::memcpy(&count, data.data() + offset + 8, sizeof(count));
        if (count) {
          const uint16 element = tagfileTypes[base].pointer;
          item = static_cast<uint32>(items.size());
          items.push_back({element, destination, count,
                           SubType(element) == TagfilePointer
                               ? PointerItem
                               : ValueItem});
          destinationItems[destination] = item;
          ScanType(element);
        }
      }
    }

    if (!item || offset + sizeof(item) > data.size()) {
      return;
    }

    std::memcpy(data.data() + offset, &item, sizeof(item));
    patches[type].push_back(offset);
  }

  uint32 TypeString(std::vector<std::string_view> &strings,
                    std::string_view value) {
    const auto found = std::find(strings.begin(), strings.end(), value);
    if (found != strings.end()) {
      return static_cast<uint32>(found - strings.begin());
    }
    strings.push_back(value);
    return static_cast<uint32>(strings.size() - 1);
  }

  void WriteTypes() {
    const Chunk typeChunk = BeginChunk(wr, "TYPE", BranchChunk);

    const Chunk pointers = BeginChunk(wr, "TPTR", LeafChunk);
    wr.Skip(types.size() * 8);
    EndChunk(wr, pointers);

    std::vector<std::string_view> typeStrings;
    std::vector<std::string_view> fieldStrings;
    for (size_t i = 1; i < types.size(); i++) {
      const TagfileType &type = tagfileTypes[types[i]];
      TypeString(typeStrings, type.name);
      for (uint16 t = 0; t < type.numTemplates; t++) {
        TypeString(typeStrings,
                   tagfileTemplates[type.templateBegin + t].name);
      }
      for (uint16 m = 0; m < type.numMembers; m++) {
        TypeString(fieldStrings, tagfileMembers[type.memberBegin + m].name);
      }
    }

    const Chunk typeStringChunk = BeginChunk(wr, "TSTR", LeafChunk);
    for (std::string_view value : typeStrings) {
      wr.WriteBuffer(value.data(), value.size());
      wr.Write<uint8>(0);
    }
    EndChunk(wr, typeStringChunk);

    const Chunk typeNames = BeginChunk(wr, "TNAM", LeafChunk);
    WritePacked(wr, static_cast<uint32>(types.size()));
    for (size_t i = 1; i < types.size(); i++) {
      const TagfileType &type = tagfileTypes[types[i]];
      WritePacked(wr, TypeString(typeStrings, type.name));
      WritePacked(wr, type.numTemplates);
      for (uint16 t = 0; t < type.numTemplates; t++) {
        const TagfileTemplate &item =
            tagfileTemplates[type.templateBegin + t];
        WritePacked(wr, TypeString(typeStrings, item.name));
        WritePacked(wr, item.name[0] == 't' ? metadataToType[item.value]
                                            : item.value);
      }
    }
    EndChunk(wr, typeNames);

    const Chunk fieldStringChunk = BeginChunk(wr, "FSTR", LeafChunk);
    for (std::string_view value : fieldStrings) {
      wr.WriteBuffer(value.data(), value.size());
      wr.Write<uint8>(0);
    }
    EndChunk(wr, fieldStringChunk);

    const Chunk typeBodies = BeginChunk(wr, "TBOD", LeafChunk);
    for (size_t i = 1; i < types.size(); i++) {
      const TagfileType &type = tagfileTypes[types[i]];
      WritePacked(wr, static_cast<uint32>(i));
      WritePacked(wr, metadataToType[type.parent]);
      WritePacked(wr, type.flags);
      if (type.flags & 1) {
        WritePacked(wr, type.subTypeFlags);
      }
      if (type.flags & 2) {
        WritePacked(wr, metadataToType[type.pointer]);
      }
      if (type.flags & 4) {
        WritePacked(wr, type.version);
      }
      if (type.flags & 8) {
        WritePacked(wr, type.byteSize);
        WritePacked(wr, type.alignment);
      }
      if (type.flags & 0x10) {
        WritePacked(wr, type.abstractValue);
      }
      if (type.flags & 0x20) {
        WritePacked(wr, type.numMembers);
        for (uint16 m = 0; m < type.numMembers; m++) {
          const TagfileMember &member =
              tagfileMembers[type.memberBegin + m];
          WritePacked(wr, TypeString(fieldStrings, member.name));
          WritePacked(wr, member.flags);
          WritePacked(wr, member.offset);
          WritePacked(wr, metadataToType[member.type]);
        }
      }
      if (type.flags & 0x40) {
        WritePacked(wr, type.numInterfaces);
        for (uint16 n = 0; n < type.numInterfaces; n++) {
          const TagfileInterface &item =
              tagfileInterfaces[type.interfaceBegin + n];
          WritePacked(wr, metadataToType[item.type]);
          WritePacked(wr, item.flags);
        }
      }
    }
    EndChunk(wr, typeBodies);

    const Chunk hashes = BeginChunk(wr, "THSH", LeafChunk);
    uint32 numHashes = 0;
    for (size_t i = 1; i < types.size(); i++) {
      numHashes += tagfileTypes[types[i]].hash != 0;
    }
    WritePacked(wr, numHashes);
    for (size_t i = 1; i < types.size(); i++) {
      const TagfileType &type = tagfileTypes[types[i]];
      if (type.hash) {
        WritePacked(wr, static_cast<uint32>(i));
        WriteLittleEndian(wr, type.hash);
      }
    }
    EndChunk(wr, hashes);

    const Chunk padding = BeginChunk(wr, "TPAD", LeafChunk);
    EndChunk(wr, padding);
    EndChunk(wr, typeChunk);
  }

  void WriteIndex() {
    const Chunk index = BeginChunk(wr, "INDX", BranchChunk);
    const Chunk itemChunk = BeginChunk(wr, "ITEM", LeafChunk);
    wr.Skip(12);
    for (size_t i = 1; i < items.size(); i++) {
      const TagItem &item = items[i];
      WriteLittleEndian(wr, metadataToType[item.type] | item.kind);
      WriteLittleEndian(wr, item.offset);
      WriteLittleEndian(wr, item.count);
    }
    EndChunk(wr, itemChunk);

    std::vector<std::pair<uint16, std::vector<uint32> *>> orderedPatches;
    for (auto &patch : patches) {
      orderedPatches.emplace_back(metadataToType[patch.first], &patch.second);
    }
    std::sort(orderedPatches.begin(), orderedPatches.end(),
              [](const auto &left, const auto &right) {
                return left.first < right.first;
              });

    const Chunk patchChunk = BeginChunk(wr, "PTCH", LeafChunk);
    for (auto &patch : orderedPatches) {
      auto &offsets = *patch.second;
      std::sort(offsets.begin(), offsets.end());
      offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
      WriteLittleEndian(wr, patch.first);
      WriteLittleEndian(wr, static_cast<uint32>(offsets.size()));
      for (uint32 offset : offsets) {
        WriteLittleEndian(wr, offset);
      }
    }
    EndChunk(wr, patchChunk);
    EndChunk(wr, index);
  }

  BinWritterRef_e wr;
  const IhkPackFile::VirtualClasses &sourceClasses;
  const char *sdkVersion;
  std::vector<ReflectedObject> objects;
  std::string data;
  std::vector<hkFixup> fixups;
  std::unordered_map<size_t, const hkFixup *> fixupByOffset;
  std::vector<TagItem> items;
  std::unordered_map<const IhkVirtualClass *, uint32> objectItems;
  std::unordered_map<uint32, uint32> destinationItems;
  std::vector<uint16> types;
  std::vector<uint16> metadataToType;
  std::vector<uint8> scannedTypes;
  std::unordered_map<uint16, std::vector<uint32>> patches;
};

} // namespace

void IhkPackFile::ToTagFile(const std::string &fileName, hkToolset toolset) {
  const char *sdkVersion = SdkVersion(toolset);
  if (!sdkVersion) {
    return;
  }

  BinWritter writer(fileName);
  TagWriter(writer, GetAllClasses(), sdkVersion).Write();
}
