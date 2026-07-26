/*  Havok Format Library
    Copyright(C) 2016-2025 Lukas Cone

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

#pragma once
#include "hklib/hk_packfile.hpp"
#include "spike/io/bincore_fwd.hpp"
#include <set>
#include <variant>

static constexpr uint32 HK_HEADER_TAG = CompileFourCC("TAG0");
static constexpr uint32 HK_HEADER_TCM = CompileFourCC("TCM0");
static constexpr uint32 HK_HEADER_OLD_TAG_0 = 0xcab00d1e;
static constexpr uint32 HK_HEADER_OLD_TAG_1 = 0xd011face;

struct hkxNewHeader;

struct hkChunk {
  uint32 sizeAndFlags;
  uint32 tag;

  void Reorder();
  uint32 Size() const { return sizeAndFlags & 0xffffff; }
  bool IsSubChunk() const { return (sizeAndFlags & 0x40000000) != 0; }
};

struct ClassName;

struct ClassTemplateArgument {
  std::string_view argName;
  ClassName *argType{};
  uint64 value{};
};

struct ClassMember {
  std::string_view name;
  uint32 flags{};
  uint32 offset{};
  ClassName *type{};
};

struct ClassInterface {
  ClassName *type{};
  uint32 offset{};
};

struct ClassName {
  std::string_view className;
  std::vector<ClassTemplateArgument> templateArguments;
  ClassName *parent{};
  ClassName *subType{};
  uint32 optionals{};
  uint32 format{};
  uint32 version{};
  uint32 byteSize{};
  uint32 alignment{};
  uint32 flags{};
  uint32 attributeString{};
  std::vector<ClassMember> members;
  std::vector<ClassInterface> interfaces;

  const ClassMember *FindMember(std::string_view name) const {
    for (const auto &member : members) {
      if (member.name == name) {
        return &member;
      }
    }
    return parent ? parent->FindMember(name) : nullptr;
  }
};

struct classEntryFixup : hkChunk {
  uint32 count;
};

struct hkCompendiumData {
  using StrVec = std::vector<std::string_view>;
  std::string classNamesBuffer;
  std::string memberNamesBuffer;
  StrVec classNames;
  StrVec memberNames;
  std::vector<ClassName> weldedClassNames;
  std::vector<classEntryFixup> classEntries;
};

struct hkCompendium : IhkPackFile, hkCompendiumData {
  std::set<uint64> signatures;

  VirtualClasses &GetAllClasses() override {
    static VirtualClasses DUMMY;
    return DUMMY;
  }
  hkToolset GetToolset() const override { return HKUNKVER; }

  void Load(BinReaderRef rd);
};

struct hkxNewHeader : IhkPackFile {
  struct ClassBinding {
    const void *object{};
    const ClassName *type{};
  };

  hkToolset toolset;
  std::string dataBuffer;
  std::variant<hkCompendiumData, hkCompendium *> compendium = hkCompendiumData{};

  hkCompendiumData &Compendium() {
    return std::visit(
        [](auto &item) -> hkCompendiumData & {
          if constexpr (std::is_pointer_v<std::decay_t<decltype(item)>>) {
            return *item;
          } else {
            return item;
          }
        },
        compendium);
  }

  VirtualClasses virtualClasses;
  std::vector<ClassBinding> classBindings;

  VirtualClasses &GetAllClasses() override { return virtualClasses; }
  hkToolset GetToolset() const override { return toolset; }
  void SetCompendium(IhkPackFile *input) {
    compendium = static_cast<hkCompendium *>(input);
  }
  void Load(BinReaderRef rd);
  void DumpClassNames(std::ostream &str);
  const ClassName *GetClassType(const void *object) const;
};

IhkPackFile::Ptr ReadLegacyTagfile(BinReaderRef_e rd);
