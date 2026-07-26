/*  Havok Format Library
    Copyright(C) 2016-2026 Lukas Cone

    This program is free software : you can redistribute it and / or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "format_new.hpp"
#include "../format_old.hpp"
#include "../toolset.hpp"
#include "internal/hk_internal_api.hpp"
#include "spike/except.hpp"
#include "spike/io/binreader.hpp"
#include "spike/type/float.hpp"
#include "spike/type/pointer.hpp"
 // This file was written in april 22. it is still unfinished (i.e, requires an xml reading implementation)
 // i do not see the point in a writer if said game using havok version will read packfiles just fine.
namespace {
enum FieldType : int32 {
  FieldVoid = 0,
  FieldByte = 1,
  FieldInt = 2,
  FieldReal = 3,
  FieldVector4 = 4,
  FieldVector8 = 5,
  FieldVector12 = 6,
  FieldVector16 = 7,
  FieldObject = 8,
  FieldStruct = 9,
  FieldString = 10,
  FieldTypeMask = 0xf,
  FieldArray = 0x10,
  FieldTuple = 0x20,
};

struct Field {
  std::string name;
  int32 type{};
  int32 tupleCount{};
  std::string className;
};

struct Class {
  std::string name;
  int32 version{};
  int32 parent{};
  std::vector<Field> fields;
};

struct Object {
  int32 classIndex{};
  size_t offset{};
  size_t className{};
};

struct PendingPointer {
  size_t offset{};
  int32 object{};
};

uint16 MetadataValue16(const unsigned char *data, size_t offset) {
  return uint16(data[offset]) | uint16(data[offset + 1]) << 8;
}

uint32 MetadataValue32(const unsigned char *data, size_t offset) {
  return uint32(data[offset]) | uint32(data[offset + 1]) << 8 |
         uint32(data[offset + 2]) << 16 | uint32(data[offset + 3]) << 24;
}

class PackfileMetadata {
public:
  explicit PackfileMetadata(hkxTypeInfo info) : info(info) {}

  bool Valid() const { return info.data != nullptr; }

  size_t Class(std::string_view name) const {
    for (size_t offset = info.virtualFixups; offset + 12 <= info.end;
         offset += 12) {
      const uint32 object = Value(offset);
      if (object == uint32(-1)) {
        break;
      }
      const size_t className = Local(object);
      if (className != Invalid &&
          name == reinterpret_cast<const char *>(info.data + className)) {
        return object;
      }
    }
    return Invalid;
  }

  size_t Member(size_t type, std::string_view name) const {
    if (type == Invalid) {
      return Invalid;
    }
    const size_t members = Local(type + 24);
    const uint32 count = Value(type + 28);
    if (members != Invalid) {
      for (uint32 index = 0; index < count; index++) {
        const size_t member = members + index * 24;
        if (name == Name(member)) {
          return member;
        }
      }
    }
    return Member(Destination(type + 4), name);
  }

  size_t Member(size_t type, size_t index) const {
    if (type == Invalid) {
      return Invalid;
    }
    const size_t parent = Destination(type + 4);
    const size_t parentCount = NumMembers(parent);
    if (index < parentCount) {
      return Member(parent, index);
    }
    const size_t members = Local(type + 24);
    const size_t declared = index - parentCount;
    return members != Invalid && declared < Value(type + 28)
               ? members + declared * 24
               : Invalid;
  }

  size_t Local(size_t source) const {
    for (size_t offset = info.localFixups; offset + 8 <= info.globalFixups;
         offset += 8) {
      if (Value(offset) == source) {
        return Value(offset + 4);
      }
    }
    return Invalid;
  }

  size_t Destination(size_t source) const {
    const size_t local = Local(source);
    if (local != Invalid) {
      return local;
    }
    for (size_t offset = info.globalFixups; offset + 12 <= info.virtualFixups;
         offset += 12) {
      if (Value(offset) == source) {
        return Value(offset + 8);
      }
    }
    return Invalid;
  }

  const char *Name(size_t source) const {
    const size_t offset = Local(source);
    return offset == Invalid
               ? ""
               : reinterpret_cast<const char *>(info.data + offset);
  }

  uint32 Value(size_t offset) const {
    return MetadataValue32(info.data, offset);
  }
  uint16 Short(size_t offset) const {
    return MetadataValue16(info.data, offset);
  }
  uint8 Byte(size_t offset) const { return info.data[offset]; }

  size_t ElementSize(uint8 type, size_t member) const {
    switch (type) {
    case 1:
    case 2:
    case 3:
    case 4:
      return 1;
    case 5:
    case 6:
    case 32:
      return 2;
    case 7:
    case 8:
    case 11:
    case 20:
    case 21:
    case 29:
    case 30:
    case 33:
      return 4;
    case 9:
    case 10:
    case 28:
      return 8;
    case 12:
    case 13:
      return 16;
    case 14:
    case 15:
    case 16:
      return 48;
    case 17:
    case 18:
      return 64;
    case 25: {
      const size_t typeClass = Destination(member + 4);
      return typeClass == Invalid ? 0 : Value(typeClass + 8);
    }
    case 24:
    case 31:
      return ElementSize(Byte(member + 13), member);
    default:
      return 0;
    }
  }

  static constexpr size_t Invalid = ~size_t{};

private:
  size_t NumMembers(size_t type) const {
    return type == Invalid
               ? 0
               : NumMembers(Destination(type + 4)) + Value(type + 28);
  }

  hkxTypeInfo info;
};

hkToolset MetadataToolset(hkToolset toolset) {
  switch (toolset) {
  case HK700:
    return HK710;
  case HK2010_1:
    return HK2010_2;
  case HK2012_1:
    return HK2012_2;
  case HK2013_2:
    return HK2013;
  case HK2014_2:
    return HK2014;
  default:
    return toolset;
  }
}

class LegacyTagfile {
public:
  IhkPackFile::Ptr Read(BinReaderRef_e rd) {
    const size_t size = rd.GetSize();
    input.resize(size);
    rd.Seek(0);
    rd.BaseStream().read(input.data(), input.size());

    ReadTypes();
    SelectMetadata();

    ReadObjects(false);
    header = std::make_unique<hkxHeader>();
    InitializeHeader();
    header->sections[0].buffer.resize(cursor);
    buffer = header->sections[0].buffer.data();
    ReadObjects(true);
    ResolvePointers();
    CreateClasses();
    return std::move(header);
  }

private:
  uint8 Read8() {
    if (position >= input.size()) {
      throw std::runtime_error("Unexpected end of legacy tagfile");
    }
    return static_cast<uint8>(input[position++]);
  }

  uint16 Read16() {
    uint16 value = uint16(Read8()) | uint16(Read8()) << 8;
    if (swapEndian) {
      value = uint16(value >> 8) | uint16(value << 8);
    }
    return value;
  }

  uint32 Read32() {
    uint32 value = uint32(Read8()) | uint32(Read8()) << 8 |
                   uint32(Read8()) << 16 | uint32(Read8()) << 24;
    if (swapEndian) {
      value = (value >> 24) | ((value >> 8) & 0xff00) |
              ((value << 8) & 0xff0000) | (value << 24);
    }
    return value;
  }

  uint64 Read64() {
    uint64 value = 0;
    for (uint32 index = 0; index < 8; index++) {
      value |= uint64(Read8()) << (index * 8);
    }
    if (swapEndian) {
      uint64 swapped = 0;
      for (uint32 index = 0; index < 8; index++) {
        swapped |= ((value >> (index * 8)) & 0xff) << ((7 - index) * 8);
      }
      return swapped;
    }
    return value;
  }

  int64 ReadInt() {
    uint64 byte = Read8();
    uint64 value = (byte & ~uint64(0x80)) >> 1;
    const bool negative = (byte & 1) != 0;
    uint32 shift = 6;
    while (byte & 0x80) {
      byte = Read8();
      value |= (byte & ~uint64(0x80)) << shift;
      shift += 7;
    }
    return negative ? -static_cast<int64>(value) : static_cast<int64>(value);
  }

  double ReadReal() {
    if (realIsDouble) {
      const uint64 value = Read64();
      return reinterpret_cast<const double &>(value);
    }
    const uint32 value = Read32();
    return reinterpret_cast<const float &>(value);
  }

  std::string ReadString() {
    const int32 length = static_cast<int32>(ReadInt());
    if (length > 0) {
      if (position + static_cast<size_t>(length) > input.size()) {
        throw std::runtime_error("Invalid legacy tagfile string");
      }
      std::string value(input.data() + position, static_cast<size_t>(length));
      position += static_cast<size_t>(length);
      strings.push_back(value);
      return value;
    }
    const int32 index = -length;
    if (index < 0 || static_cast<size_t>(index) >= strings.size()) {
      throw std::runtime_error("Invalid legacy tagfile string reference");
    }
    return strings[static_cast<size_t>(index)];
  }

  std::vector<uint8> ReadBitfield(size_t count) {
    std::vector<uint8> bits(count);
    for (size_t byteIndex = 0; byteIndex < (count + 7) / 8; byteIndex++) {
      const uint8 value = Read8();
      for (size_t bit = 0; bit < 8 && byteIndex * 8 + bit < count; bit++) {
        bits[byteIndex * 8 + bit] = (value >> bit) & 1;
      }
    }
    return bits;
  }

  void ReadTypes() {
    if (input.size() < 8) {
      throw es::InvalidHeaderError(0);
    }

    position = 0;
    swapEndian = false;
    const uint32 magic0 = Read32();
    const uint32 magic1 = Read32();
    if (magic0 != HK_HEADER_OLD_TAG_0 || magic1 != HK_HEADER_OLD_TAG_1) {
      swapEndian = true;
      position = 0;
      if (Read32() != HK_HEADER_OLD_TAG_0 ||
          Read32() != HK_HEADER_OLD_TAG_1) {
        throw es::InvalidHeaderError(magic0);
      }
    }

    strings = {"", ""};
    classes.emplace_back();

    while (position < input.size()) {
      const size_t tagPosition = position;
      const int32 tag = static_cast<int32>(ReadInt());
      if (tag == 1) {
        version = static_cast<int32>(ReadInt());
        if (version < 0 || version > 6 || version == 1) {
          throw es::InvalidVersionError(version);
        }
        if (version >= 4) {
          const std::string sdk = ReadString();
          for (const auto &item : xmlToolsetProps) {
            if (sdk == item.second.name) {
              toolset = item.first;
              break;
            }
          }
        }
        if (version >= 5) {
          Read16();
          const uint16 numPredicates = Read16();
          for (uint16 index = 0; index < numPredicates; index++) {
            Read16();
          }
        }
        realIsDouble = version >= 6;
      } else if (tag == 2) {
        ReadClass();
      } else if (tag == 3 || tag == 4 || tag == 6) {
        objectsPosition = tagPosition;
        break;
      } else {
        throw es::InvalidHeaderError(static_cast<uint32>(tag));
      }
    }
  }

  void ReadClass() {
    Class type;
    type.name = ReadString();
    type.version = static_cast<int32>(ReadInt());
    type.parent = static_cast<int32>(ReadInt());
    const int32 count = static_cast<int32>(ReadInt());
    if (count < 0) {
      throw std::runtime_error("Invalid legacy tagfile member count");
    }
    type.fields.reserve(static_cast<size_t>(count));
    for (int32 index = 0; index < count; index++) {
      Field field;
      field.name = ReadString();
      field.type = static_cast<int32>(ReadInt());
      if (field.type & FieldTuple) {
        field.tupleCount = static_cast<int32>(ReadInt());
      }
      const int32 baseType = field.type & FieldTypeMask;
      if (baseType == FieldObject || baseType == FieldStruct) {
        field.className = ReadString();
      }
      type.fields.push_back(std::move(field));
    }
    classes.push_back(std::move(type));
  }

  void SelectMetadata() {
    size_t bestScore = 0;
    for (const auto &properties : xmlToolsetProps) {
      if ((toolset != HKUNKVER && properties.first != toolset) ||
          properties.first < HK650 || properties.first > HK2014_2) {
        continue;
      }
      for (uint8 profile = 0; profile < 3; profile++) {
        PackfileMetadata candidate(
            GetPackfileTypes(MetadataToolset(properties.first), profile));
        if (!candidate.Valid()) {
          continue;
        }
        size_t score = 0;
        for (size_t index = 1; index < classes.size(); index++) {
          const size_t type = candidate.Class(classes[index].name);
          if (type == PackfileMetadata::Invalid) {
            continue;
          }
          score++;
          score += candidate.Value(type + 44) ==
                   static_cast<uint32>(classes[index].version);
          score += candidate.Value(type + 28) == classes[index].fields.size();
          const size_t parent = candidate.Destination(type + 4);
          if (classes[index].parent > 0) {
            score += parent != PackfileMetadata::Invalid &&
                     classes[static_cast<size_t>(classes[index].parent)].name ==
                         candidate.Name(parent);
          } else {
            score += parent == PackfileMetadata::Invalid;
          }
          for (const Field &field : classes[index].fields) {
            score += candidate.Member(type, field.name) !=
                     PackfileMetadata::Invalid;
          }
        }
        if (!metadata || score > bestScore) {
          metadata = std::make_unique<PackfileMetadata>(
              GetPackfileTypes(MetadataToolset(properties.first), profile));
          toolset = properties.first;
          bestScore = score;
        }
      }
    }
    if (!metadata) {
      throw es::InvalidVersionError(toolset);
    }
  }

  void InitializeHeader() {
    header->toolset = toolset;
    const xmlToolsetProp &properties = xmlToolsetProps.at(toolset);
    header->version = properties.version;
    for (size_t index = 0; index < sizeof(header->contentsVersion); index++) {
      header->contentsVersion[index] = properties.name[index];
    }
    header->layout = {4, 1, 0, 1};
    header->contentsSectionIndex = 0;
    header->numSections = 1;
    header->sections.resize(1);
    header->sections[0].header = header.get();
    header->sections[0].sectionID = 0;
  }

  size_t Allocate(size_t size, size_t alignment) {
    const size_t mask = alignment - 1;
    cursor = (cursor + mask) & ~mask;
    const size_t offset = cursor;
    cursor += size;
    return offset;
  }

  void ReadObjects(bool write) {
    writing = write;
    position = objectsPosition;
    cursor = 0;
    remembered.clear();
    if (version >= 2) {
      remembered.push_back(PackfileMetadata::Invalid);
    }
    pendingPointers.clear();
    objects.clear();

    while (position < input.size()) {
      const int32 tag = static_cast<int32>(ReadInt());
      if (tag == 7 || tag == -1) {
        break;
      }
      if (tag != 3 && tag != 4 && tag != 6) {
        throw es::InvalidHeaderError(static_cast<uint32>(tag));
      }
      if (tag == 6) {
        continue;
      }
      ReadObject(tag);
      if (version < 2) {
        break;
      }
    }
  }

  void ReadObject(int32 tag) {
    const int32 classIndex = static_cast<int32>(ReadInt());
    if (classIndex <= 0 ||
        static_cast<size_t>(classIndex) >= classes.size()) {
      throw std::runtime_error("Invalid legacy tagfile class index");
    }
    const size_t targetClass = metadata->Class(classes[classIndex].name);
    const size_t offset =
        targetClass == PackfileMetadata::Invalid
            ? PackfileMetadata::Invalid
            : Allocate(metadata->Value(targetClass + 8), 16);
    const std::string &name = classes[classIndex].name;
    const size_t className = Allocate(name.size() + 1, 2);
    if (writing) {
      for (size_t index = 0; index < name.size(); index++) {
        buffer[className + index] = name[index];
      }
      buffer[className + name.size()] = 0;
    }
    if (tag == 4) {
      remembered.push_back(offset);
    }
    objects.push_back({classIndex, offset, className});
    ReadObjectFields(classIndex, targetClass, offset);
  }

  void CollectFields(int32 classIndex,
                     std::vector<const Field *> &output) const {
    if (classIndex <= 0 ||
        static_cast<size_t>(classIndex) >= classes.size()) {
      return;
    }
    const Class &type = classes[static_cast<size_t>(classIndex)];
    CollectFields(type.parent, output);
    for (const Field &field : type.fields) {
      output.push_back(&field);
    }
  }

  int32 FindClass(std::string_view name) const {
    for (size_t index = 1; index < classes.size(); index++) {
      if (classes[index].name == name) {
        return static_cast<int32>(index);
      }
    }
    return 0;
  }

  void ReadObjectFields(int32 classIndex, size_t targetClass,
                        size_t destination) {
    std::vector<const Field *> fields;
    CollectFields(classIndex, fields);
    const std::vector<uint8> present = ReadBitfield(fields.size());
    for (size_t index = 0; index < fields.size(); index++) {
      if (!present[index]) {
        continue;
      }
      const size_t member =
          targetClass == PackfileMetadata::Invalid
              ? PackfileMetadata::Invalid
              : metadata->Member(targetClass, fields[index]->name);
      const size_t resolvedMember =
          member == PackfileMetadata::Invalid
              ? metadata->Member(targetClass, index)
              : member;
      const size_t memberDestination =
          resolvedMember == PackfileMetadata::Invalid ||
                  destination == PackfileMetadata::Invalid
              ? PackfileMetadata::Invalid
              : destination + metadata->Short(resolvedMember + 18);
      ReadValue(*fields[index], resolvedMember, memberDestination);
    }
  }

  size_t TargetSize(size_t member, bool element) const {
    if (member == PackfileMetadata::Invalid) {
      return 0;
    }
    const uint8 type =
        metadata->Byte(member + (element ? 13 : 12));
    return metadata->ElementSize(type, member);
  }

  void Store(size_t destination, uint64 value, size_t size) {
    if (!writing || destination == PackfileMetadata::Invalid) {
      return;
    }
    for (size_t index = 0; index < size; index++) {
      buffer[destination + index] =
          static_cast<char>(value >> (index * 8));
    }
  }

  void StoreInteger(size_t member, size_t destination, int64 value,
                    bool element) {
    Store(destination, static_cast<uint64>(value),
          TargetSize(member, element));
  }

  void StoreReal(size_t destination, double value, size_t size) {
    if (!writing || destination == PackfileMetadata::Invalid) {
      return;
    }
    if (size == 2) {
      Store(destination, float16(static_cast<float>(value)).value, 2);
    } else if (size >= 4) {
      const float output = static_cast<float>(value);
      Store(destination, reinterpret_cast<const uint32 &>(output), 4);
    }
  }

  void StorePointer(size_t destination, size_t target) {
    if (!writing || destination == PackfileMetadata::Invalid ||
        target == PackfileMetadata::Invalid) {
      return;
    }
    *reinterpret_cast<es::PointerX86<char> *>(buffer + destination) =
        buffer + target;
  }

  void ReadPointer(size_t destination) {
    if (version < 2) {
      ReadNestedObject(destination, false);
      return;
    }
    const int32 object = static_cast<int32>(ReadInt());
    if (destination != PackfileMetadata::Invalid) {
      pendingPointers.push_back({destination, object});
    }
  }

  void ReadNestedObject(size_t destination, bool inPlace) {
    const int32 tag = static_cast<int32>(ReadInt());
    if (tag == 5) {
      const int32 object = static_cast<int32>(ReadInt());
      if (object < 0 || static_cast<size_t>(object) >= remembered.size()) {
        throw std::runtime_error("Invalid legacy tagfile object reference");
      }
      if (!inPlace) {
        StorePointer(destination, remembered[static_cast<size_t>(object)]);
      }
      return;
    }
    if (tag == 6) {
      return;
    }
    if (tag != 3 && tag != 4) {
      throw es::InvalidHeaderError(static_cast<uint32>(tag));
    }

    const int32 classIndex = static_cast<int32>(ReadInt());
    if (classIndex <= 0 ||
        static_cast<size_t>(classIndex) >= classes.size()) {
      throw std::runtime_error("Invalid legacy tagfile class index");
    }
    const size_t targetClass = metadata->Class(classes[classIndex].name);
    const size_t offset =
        inPlace
            ? destination
            : targetClass == PackfileMetadata::Invalid
                  ? PackfileMetadata::Invalid
                  : Allocate(metadata->Value(targetClass + 8), 16);
    if (tag == 4) {
      remembered.push_back(offset);
    }
    if (!inPlace) {
      StorePointer(destination, offset);
      const std::string &name = classes[classIndex].name;
      const size_t className = Allocate(name.size() + 1, 2);
      if (writing) {
        for (size_t index = 0; index < name.size(); index++) {
          buffer[className + index] = name[index];
        }
        buffer[className + name.size()] = 0;
      }
      objects.push_back({classIndex, offset, className});
    }
    ReadObjectFields(classIndex, targetClass, offset);
  }

  void ReadStringValue(size_t destination) {
    const std::string value = ReadString();
    if (destination == PackfileMetadata::Invalid) {
      return;
    }
    const size_t stringOffset = Allocate(value.size() + 1, 2);
    StorePointer(destination, stringOffset);
    if (writing) {
      for (size_t index = 0; index < value.size(); index++) {
        buffer[stringOffset + index] = value[index];
      }
      buffer[stringOffset + value.size()] = 0;
    }
  }

  void ReadVector(int32 count, size_t member, size_t destination,
                  bool element, bool componentCount) {
    int32 serializedCount = count;
    if (componentCount && count == 4) {
      serializedCount = static_cast<int32>(ReadInt());
      if (serializedCount < 0 || serializedCount > count) {
        throw std::runtime_error("Invalid legacy tagfile vector size");
      }
    }
    const size_t targetSize = TargetSize(member, element);
    for (int32 index = 0; index < serializedCount; index++) {
      const double value = ReadReal();
      if (static_cast<size_t>(index) * 4 < targetSize) {
        StoreReal(destination == PackfileMetadata::Invalid
                      ? PackfileMetadata::Invalid
                      : destination + static_cast<size_t>(index) * 4,
                  value, 4);
      }
    }
  }

  void ReadValue(const Field &field, size_t member,
                 size_t destination) {
    if (field.type & FieldArray) {
      ReadArray(field, member, destination);
      return;
    }
    if (field.type & FieldTuple) {
      ReadTuple(field, member, destination);
      return;
    }

    switch (field.type & FieldTypeMask) {
    case FieldVoid:
      break;
    case FieldByte:
      StoreInteger(member, destination, Read8(), false);
      break;
    case FieldInt:
      StoreInteger(member, destination, ReadInt(), false);
      break;
    case FieldReal:
      StoreReal(destination, ReadReal(), TargetSize(member, false));
      break;
    case FieldVector4:
      ReadVector(4, member, destination, false, false);
      break;
    case FieldVector8:
      ReadVector(8, member, destination, false, false);
      break;
    case FieldVector12:
      ReadVector(12, member, destination, false, false);
      break;
    case FieldVector16:
      ReadVector(16, member, destination, false, false);
      break;
    case FieldObject:
      ReadPointer(destination);
      break;
    case FieldStruct: {
      if (version < 2) {
        ReadNestedObject(destination, true);
        break;
      }
      const int32 sourceClass = FindClass(field.className);
      const size_t targetClass =
          member == PackfileMetadata::Invalid
              ? PackfileMetadata::Invalid
              : metadata->Destination(member + 4);
      ReadObjectFields(sourceClass, targetClass, destination);
      break;
    }
    case FieldString:
      ReadStringValue(destination);
      break;
    default:
      throw std::runtime_error("Invalid legacy tagfile member type");
    }
  }

  void ReadTuple(const Field &field, size_t member,
                 size_t destination) {
    const int32 baseType = field.type & FieldTypeMask;
    if (baseType == FieldReal) {
      const size_t targetSize = TargetSize(member, false);
      for (int32 index = 0; index < field.tupleCount; index++) {
        StoreReal(destination == PackfileMetadata::Invalid
                      ? PackfileMetadata::Invalid
                      : destination + static_cast<size_t>(index) * 4,
                  ReadReal(), targetSize >= static_cast<size_t>(index + 1) * 4
                                  ? 4
                                  : 0);
      }
      return;
    }

    Field element = field;
    element.type = baseType;
    const size_t stride =
        field.tupleCount > 0
            ? TargetSize(member, false) /
                  static_cast<size_t>(field.tupleCount)
            : 0;
    ReadRepeated(element, field.tupleCount, member, destination, stride, false);
  }

  void SetArray(size_t member, size_t destination, size_t values,
                int32 count) {
    if (!writing || member == PackfileMetadata::Invalid ||
        destination == PackfileMetadata::Invalid) {
      return;
    }
    const uint8 type = metadata->Byte(member + 12);
    if (type == 34) {
      Store(destination, count, 2);
      Store(destination + 2, count ? values - destination : 0, 2);
      return;
    }
    StorePointer(destination, count ? values : PackfileMetadata::Invalid);
    Store(destination + 4, count, 4);
    if (type != 26) {
      Store(destination + 8, static_cast<uint32>(count) | 0x80000000u, 4);
    }
  }

  void ReadArray(const Field &field, size_t member,
                 size_t destination) {
    const int32 count = static_cast<int32>(ReadInt());
    if (count < 0) {
      throw std::runtime_error("Invalid legacy tagfile array size");
    }

    int32 sourceClass = FindClass(field.className);
    if ((field.type & FieldTypeMask) == FieldStruct &&
        field.className.empty()) {
      sourceClass = static_cast<int32>(ReadInt());
    }

    const size_t stride = TargetSize(member, true);
    const size_t values =
        member == PackfileMetadata::Invalid || !stride || !count
            ? PackfileMetadata::Invalid
            : Allocate(stride * static_cast<size_t>(count), 16);
    SetArray(member, destination, values, count);

    Field element = field;
    element.type &= ~FieldArray;
    if (sourceClass > 0) {
      element.className = classes[static_cast<size_t>(sourceClass)].name;
    }
    ReadRepeated(element, count, member, values, stride, true);
  }

  void ReadRepeated(const Field &field, int32 count, size_t member,
                    size_t destination, size_t stride, bool element) {
    if (field.type & FieldArray) {
      for (int32 index = 0; index < count; index++) {
        ReadArray(field, member,
                  destination == PackfileMetadata::Invalid
                      ? PackfileMetadata::Invalid
                      : destination + static_cast<size_t>(index) * stride);
      }
      return;
    }
    if (field.type & FieldTuple) {
      const int32 baseType = field.type & FieldTypeMask;
      if (baseType == FieldReal && field.tupleCount == 4) {
        const int32 serializedCount = static_cast<int32>(ReadInt());
        if (serializedCount < 0 || serializedCount > 4) {
          throw std::runtime_error("Invalid legacy tagfile vector size");
        }
        for (int32 item = 0; item < count; item++) {
          for (int32 value = 0; value < serializedCount; value++) {
            StoreReal(destination == PackfileMetadata::Invalid
                          ? PackfileMetadata::Invalid
                          : destination + static_cast<size_t>(item) * stride +
                                static_cast<size_t>(value) * 4,
                      ReadReal(), 4);
          }
        }
      } else {
        for (int32 index = 0; index < count; index++) {
          ReadTuple(field, member,
                    destination == PackfileMetadata::Invalid
                        ? PackfileMetadata::Invalid
                        : destination + static_cast<size_t>(index) * stride);
        }
      }
      return;
    }

    switch (field.type & FieldTypeMask) {
    case FieldVoid:
      break;
    case FieldByte:
      for (int32 index = 0; index < count; index++) {
        StoreInteger(member,
                     destination == PackfileMetadata::Invalid
                         ? PackfileMetadata::Invalid
                         : destination + static_cast<size_t>(index) * stride,
                     Read8(), element);
      }
      break;
    case FieldInt:
      if (version >= 3) {
        ReadInt();
      }
      for (int32 index = 0; index < count; index++) {
        StoreInteger(member,
                     destination == PackfileMetadata::Invalid
                         ? PackfileMetadata::Invalid
                         : destination + static_cast<size_t>(index) * stride,
                     ReadInt(), element);
      }
      break;
    case FieldReal:
      for (int32 index = 0; index < count; index++) {
        StoreReal(destination == PackfileMetadata::Invalid
                      ? PackfileMetadata::Invalid
                      : destination + static_cast<size_t>(index) * stride,
                  ReadReal(), TargetSize(member, element));
      }
      break;
    case FieldVector4:
    case FieldVector8:
    case FieldVector12:
    case FieldVector16: {
      const int32 realCount =
          ((field.type & FieldTypeMask) - FieldVector4 + 1) * 4;
      int32 serializedCount = realCount;
      if (realCount == 4) {
        serializedCount = static_cast<int32>(ReadInt());
        if (serializedCount < 0 || serializedCount > 4) {
          throw std::runtime_error("Invalid legacy tagfile vector size");
        }
      }
      for (int32 item = 0; item < count; item++) {
        for (int32 value = 0; value < serializedCount; value++) {
          StoreReal(destination == PackfileMetadata::Invalid
                        ? PackfileMetadata::Invalid
                        : destination + static_cast<size_t>(item) * stride +
                              static_cast<size_t>(value) * 4,
                    ReadReal(), 4);
        }
      }
      break;
    }
    case FieldObject:
      for (int32 index = 0; index < count; index++) {
        ReadPointer(destination == PackfileMetadata::Invalid
                        ? PackfileMetadata::Invalid
                        : destination + static_cast<size_t>(index) * stride);
      }
      break;
    case FieldStruct: {
      const int32 sourceClass = FindClass(field.className);
      const size_t targetClass =
          member == PackfileMetadata::Invalid
              ? PackfileMetadata::Invalid
              : metadata->Destination(member + 4);
      ReadStructArray(sourceClass, targetClass, count, destination, stride);
      break;
    }
    case FieldString:
      for (int32 index = 0; index < count; index++) {
        ReadStringValue(destination == PackfileMetadata::Invalid
                            ? PackfileMetadata::Invalid
                            : destination +
                                  static_cast<size_t>(index) * stride);
      }
      break;
    default:
      throw std::runtime_error("Invalid legacy tagfile array type");
    }
  }

  void ReadStructArray(int32 sourceClass, size_t targetClass, int32 count,
                       size_t destination, size_t stride) {
    std::vector<const Field *> fields;
    CollectFields(sourceClass, fields);
    const std::vector<uint8> present = ReadBitfield(fields.size());
    for (size_t index = 0; index < fields.size(); index++) {
      if (!present[index]) {
        continue;
      }
      const size_t member =
          targetClass == PackfileMetadata::Invalid
              ? PackfileMetadata::Invalid
              : metadata->Member(targetClass, fields[index]->name);
      const size_t resolvedMember =
          member == PackfileMetadata::Invalid
              ? metadata->Member(targetClass, index)
              : member;
      const size_t memberDestination =
          resolvedMember == PackfileMetadata::Invalid ||
                  destination == PackfileMetadata::Invalid
              ? PackfileMetadata::Invalid
              : destination + metadata->Short(resolvedMember + 18);
      ReadRepeated(*fields[index], count, resolvedMember, memberDestination,
                   stride, false);
    }
  }

  void ResolvePointers() {
    for (const PendingPointer &pointer : pendingPointers) {
      if (pointer.object >= 0 &&
          static_cast<size_t>(pointer.object) < remembered.size()) {
        StorePointer(pointer.offset,
                     remembered[static_cast<size_t>(pointer.object)]);
      }
    }
  }

  void CreateClasses() {
    CRule rule(toolset, false, false);
    hkxSectionHeader &section = header->sections[0];
    for (const Object &object : objects) {
      if (object.offset == PackfileMetadata::Invalid) {
        continue;
      }
      const std::string &name =
          classes[static_cast<size_t>(object.classIndex)].name;
      IhkVirtualClass *created = hkVirtualClass::Create(JenHash(name), rule);
      if (!created) {
        continue;
      }
      hkVirtualClass *type = const_cast<hkVirtualClass *>(
          checked_deref_cast<const hkVirtualClass>(created));
      type->SetDataPointer(buffer + object.offset);
      type->className =
          std::string_view(buffer + object.className, name.size());
      type->AddHash(JenHash(name));
      type->header = header.get();
      section.virtualClasses.emplace_back(created);
    }
    for (auto &item : section.virtualClasses) {
      const_cast<hkVirtualClass *>(
          checked_deref_cast<const hkVirtualClass>(item.get()))
          ->Process();
    }
  }

  std::string input;
  size_t position{};
  size_t objectsPosition{};
  bool swapEndian{};
  bool realIsDouble{};
  bool writing{};
  int32 version{};
  hkToolset toolset = HKUNKVER;
  std::vector<std::string> strings;
  std::vector<Class> classes;
  std::unique_ptr<PackfileMetadata> metadata;
  std::unique_ptr<hkxHeader> header;
  char *buffer{};
  size_t cursor{};
  std::vector<size_t> remembered;
  std::vector<PendingPointer> pendingPointers;
  std::vector<Object> objects;
};

} // namespace

IhkPackFile::Ptr ReadLegacyTagfile(BinReaderRef_e rd) {
  return LegacyTagfile().Read(rd);
}
