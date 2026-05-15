/*  Havok Format Library
    Copyright(C) 2016-2022 Lukas Cone

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

#include "internal/hk_rootlevelcontainer.hpp"
#include "base.hpp"
#include "format_old.hpp"
#include "hk_rootlevelcontainer.inl"
#include "spike/util/endian.hpp"
#include <cstring>

namespace {

void ApplyVariantStringPadding(BinWritterRef_e wr, hkToolset version) {
  switch (version) {
  case HK550:
  case HK2010_1:
  case HK2010_2:
  case HK2012_1:
  case HK2012_2:
    wr.ApplyPadding();
    break;
  default:
    break;
  }
}

void ConvertSceneVectorBlock(std::string &data, uint8 sourceLittleEndian,
                             uint8 targetLittleEndian) {
  if (sourceLittleEndian == targetLittleEndian) {
    return;
  }

  for (size_t offset = 0x50; offset < 0x80; offset += sizeof(uint32)) {
    uint32 value = 0;
    std::memcpy(&value, data.data() + offset, sizeof(value));
    FByteswapper(value);
    std::memcpy(data.data() + offset, &value, sizeof(value));
  }
}

hkLegacySceneBlob MakeLegacySceneBlob(const hkPreservedSceneBlob &sceneBlob,
                                      uint8 targetLittleEndian,
                                      hkToolset targetVersion) {
  hkLegacySceneBlob blob;
  blob.name = sceneBlob.name;
  blob.className = sceneBlob.className;
  blob.data = sceneBlob.data;

  if (targetVersion == HK550 && sceneBlob.data.size() >= 0x100) {
    blob.data.assign(sceneBlob.data.size() - 0x30, 0);
    std::memcpy(blob.data.data() + 0x50, sceneBlob.data.data() + 0x80, 0x30);
    if (sceneBlob.data.size() > 0xb0) {
      std::memcpy(blob.data.data() + 0x80, sceneBlob.data.data() + 0xb0,
                  sceneBlob.data.size() - 0xb0);
    }
    ConvertSceneVectorBlock(blob.data, sceneBlob.sourceLittleEndian,
                            targetLittleEndian);

    for (const auto &lf : sceneBlob.localFixups) {
      if (lf.pointer >= 8 && lf.destination >= 0x30) {
        blob.localFixups.push_back({lf.pointer - 8, lf.destination - 0x30});
      }
    }
    return blob;
  }

  for (const auto &lf : sceneBlob.localFixups) {
    blob.localFixups.push_back({lf.pointer, lf.destination});
  }
  return blob;
}

} // namespace

struct hkRootLevelContainerSaver {
  const hkRootLevelContainerInternalInterface *in;
  const clgen::hkRootLevelContainer::Interface *out;

  void Save(BinWritterRef_e wr, hkFixups &fixups) {
    const size_t sBegin = wr.Tell();
    auto &locals = fixups.locals;
    auto &lay = *out->layout;

    wr.WriteBuffer(out->data, lay.totalSize);

    if (in->Size()) {
      wr.ApplyPadding();
      locals.emplace_back(sBegin, wr.Tell());
      size_t curFixup = locals.size();
      const auto varType =
          clgen::GetLayout(clgen::hkNamedVariant::LAYOUTS, out->lookup);
      using vm = clgen::hkNamedVariant::Members;
      constexpr size_t kNameFixup = 0;
      constexpr size_t kClassNameFixup = 1;
      constexpr size_t kVariantFixup = 2;

      auto reserveVariant = [&] {
        const size_t varBegin = wr.Tell();
        wr.Skip(varType->totalSize);
        locals.emplace_back(varBegin + varType->vtable[vm::name]);
        locals.emplace_back(varBegin + varType->vtable[vm::className]);
        locals.emplace_back(varBegin + varType->vtable[vm::variant]);
      };

      for ([[maybe_unused]] auto &v : *in) {
        if (!v.pointer && std::string_view(v.className) != "hkxScene") {
          continue;
        }
        reserveVariant();
      }

      auto writeVariant = [&](const hkNamedVariant &i) {
        auto applyStringPadding = [&] {
          ApplyVariantStringPadding(
              wr, static_cast<hkToolset>(out->LayoutVersion()));
        };

        locals[curFixup + kNameFixup].destination = wr.Tell();
        wr.WriteBuffer(i.name.data(), i.name.size() + 1);
        applyStringPadding();

        locals[curFixup + kClassNameFixup].destination = wr.Tell();
        wr.WriteBuffer(i.className.data(), i.className.size() + 1);
        applyStringPadding();

        if (i.pointer) {
          locals[curFixup + kVariantFixup].destClass = i.pointer;
        } else if (std::string_view(i.className) == "hkxScene") {
          if (const auto *sceneBlob = in->GetPreservedSceneBlob(i.name)) {
            const uint8 hostLittleEndian = LittleEndian() ? 1 : 0;
            const uint8 targetLittleEndian =
                wr.SwappedEndian() ? uint8(1 - hostLittleEndian)
                                   : hostLittleEndian;
            hkLegacySceneBlob blob = MakeLegacySceneBlob(
                *sceneBlob, targetLittleEndian,
                static_cast<hkToolset>(out->LayoutVersion()));
            blob.variantPtrOff = locals[curFixup + kVariantFixup].strOffset;

            fixups.legacyScene = std::move(blob);
          }

          locals[curFixup + kVariantFixup].destination = static_cast<size_t>(-1);
        }

        curFixup += vm::_count_;
      };

      for (auto &i : *in) {
        if (!i.pointer && std::string_view(i.className) != "hkxScene") {
          continue;
        }
        writeVariant(i);
      }
    }
  }
};

struct hkRootLevelContainerMidInterface
    : hkRootLevelContainerInternalInterface {
  clgen::hkRootLevelContainer::Interface interface;
  std::unique_ptr<hkRootLevelContainerSaver> saver;
  std::optional<hkPreservedSceneBlob> preservedSceneBlob;

  hkRootLevelContainerMidInterface(clgen::LayoutLookup rules, char *data)
      : interface {
    data, rules
  } {
  }

  void SetDataPointer(void *ptr) override {
    interface.data = static_cast<char *>(ptr);
  }

  const void *GetPointer() const override { return interface.data; }

  const hkPreservedSceneBlob *
  GetPreservedSceneBlob(std::string_view name) const override {
    if (preservedSceneBlob && preservedSceneBlob->name == name) {
      return &*preservedSceneBlob;
    }

    return nullptr;
  }

  size_t Size() const override { return interface.NumVariants(); }
  const hkNamedVariant At(size_t id) const override {
    auto item = interface.Variants().Next(id);
    if (interface.LayoutVersion() >= HK700) {
      return {item.Name(), item.ClassName(),
              header->GetClass(item.VariantHK700())};
    }

    return {item.Name(), item.ClassName(),
            header->GetClass(item.Variant().Object())};
  }

  void Process() override {
    if (!header) {
      return;
    }

    auto oldHeader = dynamic_cast<hkxHeader *>(header);
    if (!oldHeader) {
      return;
    }

    auto *dataSection = oldHeader->GetDataSection();
    if (!dataSection || dataSection->buffer.empty()) {
      return;
    }

    const char *base = dataSection->buffer.data();

    for (size_t i = 0; i < Size(); i++) {
      auto item = interface.Variants().Next(i);
      if (std::string_view(item.ClassName()) != "hkxScene") {
        continue;
      }

      const char *rawObject = interface.LayoutVersion() >= HK700
                                  ? item.VariantHK700()
                                  : item.Variant().Object();
      if (!rawObject || header->GetClass(rawObject)) {
        continue;
      }

      const size_t start = static_cast<size_t>(rawObject - base);
      size_t end = dataSection->buffer.size();

      for (const auto &vf : dataSection->rawVirtualFixups) {
        if (vf.dataoffset > static_cast<int32>(start) &&
            vf.dataoffset < static_cast<int32>(end)) {
          end = static_cast<size_t>(vf.dataoffset);
        }
      }

      if (start >= end || end > dataSection->buffer.size()) {
        continue;
      }

      hkPreservedSceneBlob blob;
      blob.name = item.Name();
      blob.className = item.ClassName();
      blob.data.assign(base + start, base + end);
      blob.sourceLittleEndian = oldHeader->layout.littleEndian ? 1 : 0;

      for (const auto &lf : dataSection->rawLocalFixups) {
        if (lf.pointer >= static_cast<int32>(start) &&
            lf.pointer < static_cast<int32>(end) &&
            lf.destination >= static_cast<int32>(start) &&
            lf.destination < static_cast<int32>(end)) {
          const int32 relPointer = lf.pointer - static_cast<int32>(start);
          blob.localFixups.push_back(
              {relPointer, lf.destination - static_cast<int32>(start)});

          if (relPointer >= 0 &&
              static_cast<size_t>(relPointer) + sizeof(uint32) <=
                  blob.data.size()) {
            std::memset(blob.data.data() + relPointer, 0, sizeof(uint32));
          }
        }
      }

      preservedSceneBlob = std::move(blob);
      break;
    }
  }

  void Reflect(const IhkVirtualClass *other) override {
    interface.data =
        static_cast<char *>(calloc(1, interface.layout->totalSize));
    saver = std::make_unique<hkRootLevelContainerSaver>();
    saver->in = static_cast<const hkRootLevelContainerInternalInterface *>(
        checked_deref_cast<const hkRootLevelContainer>(other));
    saver->out = &interface;
    size_t validCount = 0;
    for (size_t i = 0; i < saver->in->Size(); i++) {
      auto v = saver->in->At(i);
      if (v.pointer || std::string_view(v.className) == "hkxScene") {
        validCount++;
      }
    }
    interface.NumVariants(validCount);
    if (interface.LayoutVersion() >= HK700 &&
        interface.m(clgen::hkRootLevelContainer::Members::numVariants) >= 0) {
      *reinterpret_cast<uint32 *>(interface.data +
                                  interface.m(clgen::hkRootLevelContainer::Members::numVariants) + 4) =
          0x80000000u | static_cast<uint32>(validCount);
    }
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }

  void SwapEndian() override { clgen::EndianSwap(interface); }

  ~hkRootLevelContainerMidInterface() {
    if (saver) {
      free(interface.data);
    }
  }
};

IhkVirtualClass *hkRootLevelContainerInternalInterface::Create(CRule rule) {
  return new hkRootLevelContainerMidInterface{
      clgen::LayoutLookup{rule.version, rule.x64, rule.reusePadding}, nullptr};
}
