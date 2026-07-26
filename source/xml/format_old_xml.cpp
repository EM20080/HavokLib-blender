/*  Havok Format Library
    Copyright(C) 2016-2026 Lukas Cone

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

#include "format_old.hpp"
#include "internal/hk_internal_api.hpp"
#include "spike/io/binreader.hpp"
#include "spike/type/float.hpp"
#include "spike/type/pointer.hpp"
#include "spike/util/pugiex.hpp"
#include "toolset.hpp"
#include <cstdio>

namespace {

struct XmlObject {
  pugi::xml_node node;
  pugi::xml_attribute id;
  pugi::xml_attribute className;
  size_t offset = ~size_t{};
  size_t classNameOffset = ~size_t{};
};

uint16 Value16(const unsigned char *data, size_t offset) {
  return uint16(data[offset]) | uint16(data[offset + 1]) << 8;
}

uint32 Value32(const unsigned char *data, size_t offset) {
  return uint32(data[offset]) | uint32(data[offset + 1]) << 8 |
         uint32(data[offset + 2]) << 16 | uint32(data[offset + 3]) << 24;
}

pugi::xml_node Param(pugi::xml_node object, const char *name) {
  return object.find_child_by_attribute("hkparam", "name", name);
}

class Metadata {
public:
  explicit Metadata(hkxTypeInfo info) : info(info) {}

  size_t Class(const char *name) const {
    for (size_t offset = info.virtualFixups; offset + 12 <= info.end;
         offset += 12) {
      const uint32 object = Value32(info.data, offset);
      if (object == uint32(-1)) {
        break;
      }
      const size_t className = Local(object);
      if (className != ~size_t{} &&
          std::string_view(name) ==
              reinterpret_cast<const char *>(info.data + className)) {
        return object;
      }
    }
    return ~size_t{};
  }

  size_t Local(size_t source) const {
    for (size_t offset = info.localFixups; offset + 8 <= info.globalFixups;
         offset += 8) {
      if (Value32(info.data, offset) == source) {
        return Value32(info.data, offset + 4);
      }
    }
    return ~size_t{};
  }

  size_t Destination(size_t source) const {
    const size_t local = Local(source);
    if (local != ~size_t{}) {
      return local;
    }
    for (size_t offset = info.globalFixups; offset + 12 <= info.virtualFixups;
         offset += 12) {
      if (Value32(info.data, offset) == source) {
        return Value32(info.data, offset + 8);
      }
    }
    return ~size_t{};
  }

  const char *Name(size_t source) const {
    const size_t offset = Local(source);
    return offset == ~size_t{}
               ? ""
               : reinterpret_cast<const char *>(info.data + offset);
  }

  uint32 Value(size_t offset) const { return Value32(info.data, offset); }
  uint16 Short(size_t offset) const { return Value16(info.data, offset); }
  uint8 Byte(size_t offset) const { return info.data[offset]; }

private:
  hkxTypeInfo info;
};

class XmlPackfile {
public:
  IhkPackFile::Ptr Read(pugi::xml_document &document) {
    pugi::xml_node packfile = document.child("hkpackfile");
    if (!packfile) {
      throw XMLBaseException("Invalid packfile", document);
    }

    pugi::xml_node data =
        packfile.find_child_by_attribute("hksection", "name", "__data__");
    if (!data) {
      throw XMLMissingNodeException("__data__", packfile);
    }

    toolset = Toolset(packfile.attribute("contentsversion"));
    ReadObjects(data);
    if (toolset == HKUNKVER || objects.empty()) {
      throw XMLBaseException("Invalid packfile", packfile);
    }

    uint8 profile = 0;
    size_t profileMatches = 0;
    for (uint8 candidate = 0; candidate < 3; candidate++) {
      const hkxTypeInfo candidateInfo = GetPackfileTypes(toolset, candidate);
      if (!candidateInfo.data) {
        continue;
      }
      const Metadata candidateMetadata(candidateInfo);
      size_t matches = 0;
      for (const XmlObject &object : objects) {
        matches += candidateMetadata.Class(object.className.as_string()) !=
                   ~size_t{};
      }
      if (matches > profileMatches) {
        profile = candidate;
        profileMatches = matches;
      }
    }

    const hkxTypeInfo typeInfo = GetPackfileTypes(toolset, profile);
    if (!typeInfo.data) {
      throw XMLBaseException("Unsupported packfile version", packfile);
    }
    metadata = std::make_unique<Metadata>(typeInfo);

    header = std::make_unique<hkxHeader>();
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

    for (XmlObject &object : objects) {
      const size_t type = metadata->Class(object.className.as_string());
      if (type != ~size_t{}) {
        object.offset = Allocate(metadata->Value(type + 8), 16);
        object.classNameOffset =
            Allocate(std::string_view(object.className.as_string()).size() + 1,
                     2);
      }
    }

    const size_t valuesOffset = cursor;
    for (const XmlObject &object : objects) {
      const size_t type = metadata->Class(object.className.as_string());
      if (type != ~size_t{}) {
        WriteClass(object.node, type, object.offset);
      }
    }

    hkxSectionHeader &section = header->sections[0];
    section.buffer.resize(cursor);
    buffer = section.buffer.data();
    for (const XmlObject &object : objects) {
      if (object.offset == ~size_t{}) {
        continue;
      }
      const std::string_view className = object.className.as_string();
      for (size_t index = 0; index < className.size(); index++) {
        buffer[object.classNameOffset + index] = className[index];
      }
      buffer[object.classNameOffset + className.size()] = 0;
      section.rawVirtualFixups.push_back(
          {static_cast<int32>(object.offset), 0,
           static_cast<int32>(object.classNameOffset)});
    }

    cursor = valuesOffset;
    for (const XmlObject &object : objects) {
      const size_t type = metadata->Class(object.className.as_string());
      if (type != ~size_t{}) {
        WriteClass(object.node, type, object.offset);
      }
    }

    CRule rule(toolset, false, false);
    for (const XmlObject &object : objects) {
      if (object.offset == ~size_t{}) {
        continue;
      }
      IhkVirtualClass *created =
          hkVirtualClass::Create(JenHash(object.className.as_string()), rule);
      if (!created) {
        continue;
      }
      hkVirtualClass *virtualClass = const_cast<hkVirtualClass *>(
          checked_deref_cast<const hkVirtualClass>(created));
      virtualClass->SetDataPointer(buffer + object.offset);
      const size_t classNameSize =
          std::string_view(object.className.as_string()).size();
      virtualClass->className = std::string_view(
          buffer + object.classNameOffset, classNameSize);
      virtualClass->AddHash(JenHash(object.className.as_string()));
      virtualClass->header = header.get();
      section.virtualClasses.emplace_back(created);
    }

    for (auto &item : section.virtualClasses) {
      const_cast<hkVirtualClass *>(
          checked_deref_cast<const hkVirtualClass>(item.get()))
          ->Process();
    }

    return std::move(header);
  }

private:
  hkToolset Toolset(pugi::xml_attribute contentsVersion) const {
    for (const auto &item : xmlToolsetProps) {
      if (std::string_view(contentsVersion.as_string()) == item.second.name) {
        return item.first;
      }
    }
    return HKUNKVER;
  }

  void ReadObjects(pugi::xml_node section) {
    for (pugi::xml_node node : section.children("hkobject")) {
      pugi::xml_attribute id = node.attribute("name");
      pugi::xml_attribute className = node.attribute("class");
      if (id && className) {
        objects.push_back({node, id, className});
      }
    }
  }

  const XmlObject *FindObject(std::string_view id) const {
    for (const XmlObject &object : objects) {
      if (id == object.id.as_string()) {
        return &object;
      }
    }
    return nullptr;
  }

  size_t Allocate(size_t size, size_t alignment) {
    const size_t mask = alignment - 1;
    cursor = (cursor + mask) & ~mask;
    const size_t output = cursor;
    cursor += size;
    return output;
  }

  void Pointer(size_t source, size_t destination) {
    if (!buffer || destination == ~size_t{}) {
      return;
    }
    *reinterpret_cast<es::PointerX86<char> *>(buffer + source) =
        buffer + destination;
    header->sections[0].rawLocalFixups.push_back(
        {static_cast<int32>(source), static_cast<int32>(destination)});
  }

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
      const size_t typeClass = metadata->Destination(member + 4);
      return typeClass == ~size_t{} ? 0 : metadata->Value(typeClass + 8);
    }
    case 24:
    case 31:
      return ElementSize(metadata->Byte(member + 13), member);
    default:
      return 0;
    }
  }

  void WriteClass(pugi::xml_node object, size_t type, size_t offset) {
    const size_t parent = metadata->Destination(type + 4);
    if (parent != ~size_t{}) {
      WriteClass(object, parent, offset);
    }

    const size_t members = metadata->Local(type + 24);
    const uint32 numMembers = metadata->Value(type + 28);
    if (members == ~size_t{}) {
      return;
    }
    for (uint32 index = 0; index < numMembers; index++) {
      const size_t member = members + index * 24;
      pugi::xml_node value = Param(object, metadata->Name(member));
      if (value) {
        WriteMember(value, member, offset + metadata->Short(member + 18));
      }
    }
  }

  void WriteMember(pugi::xml_node value, size_t member, size_t offset) {
    const uint8 type = metadata->Byte(member + 12);
    if (type == 22 || type == 23 || type == 26 || type == 34) {
      WriteArray(value, member, offset);
      return;
    }
    if (type == 25) {
      const size_t typeClass = metadata->Destination(member + 4);
      const size_t elementSize = ElementSize(type, member);
      uint16 count = metadata->Short(member + 14);
      if (!count) {
        count = 1;
      }
      uint16 index = 0;
      for (pugi::xml_node child : value.children("hkobject")) {
        if (index == count || typeClass == ~size_t{}) {
          break;
        }
        WriteClass(child, typeClass, offset + index * elementSize);
        index++;
      }
      return;
    }
    if ((type == 29 || type == 33 ||
         (type == 20 && metadata->Byte(member + 13) == 2)) &&
        !metadata->Short(member + 14)) {
      const std::string_view text = value.child_value();
      if (text != "null") {
        const size_t stringOffset = Allocate(text.size() + 1, 2);
        Pointer(offset, stringOffset);
        if (buffer) {
          for (size_t index = 0; index < text.size(); index++) {
            buffer[stringOffset + index] = text[index];
          }
          buffer[stringOffset + text.size()] = 0;
        }
      }
      return;
    }

    std::string values = value.child_value();
    for (char &character : values) {
      if (character == '(' || character == ')' || character == ',') {
        character = ' ';
      }
    }
    const char *text = values.c_str();
    uint16 count = metadata->Short(member + 14);
    if (!count) {
      count = 1;
    }
    const size_t elementSize = ElementSize(type, member);
    for (uint16 index = 0; index < count; index++) {
      WriteValue(text, type, member, offset + index * elementSize);
    }
  }

  void WriteArray(pugi::xml_node value, size_t member, size_t offset) {
    const uint8 type = metadata->Byte(member + 12);
    const uint8 subtype = metadata->Byte(member + 13);
    const uint32 count = value.attribute("numelements").as_uint();
    const size_t elementSize = ElementSize(subtype, member);
    if (!elementSize) {
      return;
    }

    const size_t values = count ? Allocate(elementSize * count, 16) : 0;
    if (buffer) {
      if (type == 34) {
        *reinterpret_cast<uint16 *>(buffer + offset) =
            static_cast<uint16>(count);
        *reinterpret_cast<uint16 *>(buffer + offset + 2) =
            count ? static_cast<uint16>(values - offset) : 0;
      } else {
        *reinterpret_cast<uint32 *>(buffer + offset + 4) = count;
        if (type != 26) {
          *reinterpret_cast<uint32 *>(buffer + offset + 8) =
              count | 0x80000000u;
        }
      }
    }
    if (!count) {
      return;
    }
    if (type != 34) {
      Pointer(offset, values);
    }

    if (subtype == 25) {
      const size_t typeClass = metadata->Destination(member + 4);
      uint32 index = 0;
      for (pugi::xml_node child : value.children("hkobject")) {
        if (index == count || typeClass == ~size_t{}) {
          break;
        }
        WriteClass(child, typeClass, values + index * elementSize);
        index++;
      }
      return;
    }
    if (subtype == 29 || subtype == 33) {
      uint32 index = 0;
      for (pugi::xml_node child : value.children("hkcstring")) {
        if (index == count) {
          break;
        }
        const std::string_view text = child.child_value();
        if (text != "null") {
          const size_t stringOffset = Allocate(text.size() + 1, 2);
          Pointer(values + index * elementSize, stringOffset);
          if (buffer) {
            for (size_t character = 0; character < text.size(); character++) {
              buffer[stringOffset + character] = text[character];
            }
            buffer[stringOffset + text.size()] = 0;
          }
        }
        index++;
      }
      return;
    }

    std::string valuesText = value.child_value();
    for (char &character : valuesText) {
      if (character == '(' || character == ')' || character == ',') {
        character = ' ';
      }
    }
    const char *text = valuesText.c_str();
    for (uint32 index = 0; index < count; index++) {
      WriteValue(text, subtype, member, values + index * elementSize);
    }
  }

  int32 EnumValue(size_t member, std::string_view name) const {
    const size_t type = metadata->Destination(member + 8);
    if (type != ~size_t{}) {
      const size_t items = metadata->Local(type + 4);
      const uint32 count = metadata->Value(type + 8);
      if (items != ~size_t{}) {
        for (uint32 index = 0; index < count; index++) {
          const size_t item = items + index * 8;
          if (metadata->Name(item + 4) == name) {
            return static_cast<int32>(metadata->Value(item));
          }
        }
      }
    }

    int32 output = 0;
    int consumed = 0;
    std::sscanf(name.data(), "%d%n", &output, &consumed);
    return output;
  }

  void WriteFloats(const char *&text, size_t offset, const uint8 *indices,
                   size_t count) {
    for (size_t index = 0; index < count; index++) {
      float value = 0;
      int consumed = 0;
      std::sscanf(text, " %f%n", &value, &consumed);
      text += consumed;
      if (buffer) {
        *reinterpret_cast<float *>(buffer + offset +
                                   indices[index] * sizeof(float)) = value;
      }
    }
  }

  void WriteValue(const char *&text, uint8 type, size_t member,
                  size_t offset) {
    if (type == 12 || type == 13) {
      const uint8 indices[] = {0, 1, 2, 3};
      WriteFloats(text, offset, indices, 4);
      return;
    }
    if (type == 14 || type == 15) {
      const uint8 indices[] = {0, 1, 2, 4, 5, 6, 8, 9, 10};
      WriteFloats(text, offset, indices, 9);
      return;
    }
    if (type == 16) {
      const uint8 indices[] = {0, 1, 2, 4, 5, 6, 7, 8, 9, 10};
      WriteFloats(text, offset, indices, 10);
      return;
    }
    if (type == 17) {
      const uint8 indices[] = {0, 1, 2, 3, 4, 5, 6, 7,
                               8, 9, 10, 11, 12, 13, 14, 15};
      WriteFloats(text, offset, indices, 16);
      return;
    }
    if (type == 18) {
      const uint8 indices[] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
      WriteFloats(text, offset, indices, 12);
      if (buffer) {
        *reinterpret_cast<float *>(buffer + offset + 15 * sizeof(float)) = 1;
      }
      return;
    }

    int begin = 0;
    int end = 0;
    std::sscanf(text, " %n%*s%n", &begin, &end);
    const std::string_view value(text + begin,
                                 static_cast<size_t>(end - begin));
    text += end;
    if (type == 20 || type == 21) {
      const XmlObject *object = FindObject(value);
      Pointer(offset, object ? object->offset : ~size_t{});
      return;
    }
    if (type == 28) {
      const XmlObject *object = FindObject(value);
      Pointer(offset, object ? object->offset : ~size_t{});
      int consumed = 0;
      std::sscanf(text, " %*s%n", &consumed);
      text += consumed;
      return;
    }
    if (type == 29 || type == 33) {
      if (value != "null") {
        const size_t stringOffset = Allocate(value.size() + 1, 2);
        Pointer(offset, stringOffset);
        if (buffer) {
          for (size_t index = 0; index < value.size(); index++) {
            buffer[stringOffset + index] = value[index];
          }
          buffer[stringOffset + value.size()] = 0;
        }
      }
      return;
    }

    if (!buffer || value.empty() || type == 19) {
      return;
    }
    if (type == 1) {
      buffer[offset] =
          value == "true" || (value != "false" && value != "0");
      return;
    }
    if (type == 2) {
      buffer[offset] = value.front();
      return;
    }
    if (type == 24 || type == 31) {
      int32 output = 0;
      size_t begin = 0;
      while (begin < value.size()) {
        const size_t end = value.find('|', begin);
        output |= EnumValue(member, value.substr(
                                        begin, end == std::string_view::npos
                                                   ? value.size() - begin
                                                   : end - begin));
        if (end == std::string_view::npos) {
          break;
        }
        begin = end + 1;
      }
      const size_t size = ElementSize(metadata->Byte(member + 13), member);
      for (size_t index = 0; index < size; index++) {
        buffer[offset + index] =
            static_cast<char>(uint32(output) >> (index * 8));
      }
      return;
    }

    int consumed = 0;
    if (type == 11 || type == 32) {
      float output = 0;
      std::sscanf(value.data(), "%f%n", &output, &consumed);
      if (type == 11) {
        *reinterpret_cast<float *>(buffer + offset) = output;
      } else {
        *reinterpret_cast<uint16 *>(buffer + offset) = float16(output).value;
      }
    } else if (type == 9) {
      long long output = 0;
      std::sscanf(value.data(), "%lld%n", &output, &consumed);
      *reinterpret_cast<int64 *>(buffer + offset) = output;
    } else if (type == 10) {
      unsigned long long output = 0;
      std::sscanf(value.data(), "%llu%n", &output, &consumed);
      *reinterpret_cast<uint64 *>(buffer + offset) = output;
    } else if (type == 4 || type == 6 || type == 8 || type == 30) {
      unsigned int output = 0;
      std::sscanf(value.data(), "%u%n", &output, &consumed);
      const size_t size = ElementSize(type, member);
      for (size_t index = 0; index < size; index++) {
        buffer[offset + index] =
            static_cast<char>(output >> (index * 8));
      }
    } else {
      int output = 0;
      std::sscanf(value.data(), "%d%n", &output, &consumed);
      const size_t size = ElementSize(type, member);
      for (size_t index = 0; index < size; index++) {
        buffer[offset + index] =
            static_cast<char>(uint32(output) >> (index * 8));
      }
    }
  }

  std::vector<XmlObject> objects;
  std::unique_ptr<Metadata> metadata;
  std::unique_ptr<hkxHeader> header;
  hkToolset toolset = HKUNKVER;
  char *buffer{};
  size_t cursor{};
};

} // namespace

IhkPackFile::Ptr IhkPackFile::ReadXML(BinReaderRef_e rd) {
  pugi::xml_document document;
  document.load(rd.BaseStream(), pugi::parse_default);
  return XmlPackfile().Read(document);
}
