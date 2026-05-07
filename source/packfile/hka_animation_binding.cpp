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

#include "base.hpp"
#include "hklib/hka_animation.hpp"
#include "internal/hka_animationbinding.hpp"
#include <span>

#include "hka_animation_binding.inl"

namespace {

void SetLockedArrayCapacity(char *data, int16 countOffset, uint32 count) {
  if (countOffset < 0) {
    return;
  }

  *reinterpret_cast<uint32 *>(data + countOffset + 4) = 0x80000000u | count;
}

struct hkaAnimationBindingSaver {
  const hkaAnimationBindingInternalInterface *in;
  const clgen::hkaAnimationBinding::Interface *out;

  void Save(BinWritterRef_e wr, hkFixups &fixups) {
    const size_t sBegin = wr.Tell();
    auto &locals = fixups.locals;
    auto &lay = *out->layout;
    using mm = clgen::hkaAnimationBinding::Members;

    wr.WriteBuffer(out->data, lay.totalSize);

    if (out->m(mm::animation) >= 0 && in->GetAnimation()) {
      locals.emplace_back(sBegin + out->m(mm::animation), in->GetAnimation());
    }

    if (out->m(mm::skeletonName) >= 0) {
      auto skeletonName = in->GetSkeletonName();
      if (!skeletonName.empty()) {
        wr.ApplyPadding();
        locals.emplace_back(sBegin + out->m(mm::skeletonName), wr.Tell());
        wr.WriteBuffer(skeletonName.data(), skeletonName.size());
        wr.Skip(1);
      }
    }

    if (const auto numTracks = in->GetNumTransformTrackToBoneIndices();
        numTracks) {
      wr.ApplyPadding();
      locals.emplace_back(sBegin + out->m(mm::transformTrackToBoneIndices),
                          wr.Tell());

      for (auto index : in->TransformTrackToBoneIndices()) {
        wr.Write(index);
      }
    }

    if (const auto numTracks = in->GetNumFloatTrackToFloatSlotIndices();
        numTracks) {
      wr.ApplyPadding();
      locals.emplace_back(sBegin + out->m(mm::floatTrackToFloatSlotIndices),
                          wr.Tell());

      for (auto index : in->FloatTrackToFloatSlotIndices()) {
        wr.Write(index);
      }
    }

    if (const auto numTracks = in->GetNumPartitionIndices(); numTracks) {
      wr.ApplyPadding();
      locals.emplace_back(sBegin + out->m(mm::partitionIndices), wr.Tell());

      for (auto index : in->PartitionIndices()) {
        wr.Write(index);
      }
    }
  }
};

} // namespace

struct hkaAnimationBindingMidInterface : hkaAnimationBindingInternalInterface {
  clgen::hkaAnimationBinding::Interface interface;
  std::unique_ptr<hkaAnimationBindingSaver> saver;

  hkaAnimationBindingMidInterface(clgen::LayoutLookup rules, char *data)
      : interface {
    data, rules
  } {
  }

  void SetDataPointer(void *ptr) override {
    interface.data = static_cast<char *>(ptr);
  }

  const void *GetPointer() const override { return interface.data; }

  void SwapEndian() override {
    clgen::EndianSwap(interface);

    if (auto indicesData = interface.TransformTrackToBoneIndices()) {
      for (std::span<int16> indices(indicesData,
                                    interface.NumTransformTrackToBoneIndices());
           auto &i : indices) {
        FByteswapper(i);
      }
    }

    if (auto indicesData = interface.FloatTrackToFloatSlotIndices()) {
      for (std::span<int16> indices(
               indicesData, interface.NumFloatTrackToFloatSlotIndices());
           auto &i : indices) {
        FByteswapper(i);
      }
    }

    if (auto indicesData = interface.PartitionIndices()) {
      for (std::span<int16> indices(indicesData, interface.NumPartitionIndices());
           auto &i : indices) {
        FByteswapper(i);
      }
    }
  }

  std::string_view GetSkeletonName() const override {
    const char *name = interface.SkeletonName();
    return name ? std::string_view{name} : std::string_view{};
  }

  const hkaAnimation *GetAnimation() const override {
    return safe_deref_cast<const hkaAnimation>(
        header->GetClass(interface.Animation()));
  }

  BlendHint GetBlendHint() const override { return interface.BlendHint(); }

  size_t GetNumTransformTrackToBoneIndices() const override {
    return interface.NumTransformTrackToBoneIndices();
  }
  int16 GetTransformTrackToBoneIndex(size_t id) const override {
    return interface.TransformTrackToBoneIndices()[id];
  }
  size_t GetNumFloatTrackToFloatSlotIndices() const override {
    return interface.NumFloatTrackToFloatSlotIndices();
  }
  int16 GetFloatTrackToFloatSlotIndex(size_t id) const override {
    return interface.FloatTrackToFloatSlotIndices()[id];
  }
  size_t GetNumPartitionIndices() const override {
    return interface.NumPartitionIndices();
  }
  int16 GetPartitionIndex(size_t id) const override {
    return interface.PartitionIndices()[id];
  }

  void Reflect(const IhkVirtualClass *other) override {
    auto source = dynamic_cast<const hkaAnimationBindingInternalInterface *>(
        other);

    if (!source) {
      throw std::bad_cast{};
    }

    interface.data =
        static_cast<char *>(calloc(1, interface.layout->totalSize));
    saver = std::make_unique<hkaAnimationBindingSaver>();
    saver->in = source;
    saver->out = &interface;

    interface.BlendHint(source->GetBlendHint());
    interface.NumTransformTrackToBoneIndices(
        static_cast<uint32>(source->GetNumTransformTrackToBoneIndices()));
    interface.NumFloatTrackToFloatSlotIndices(
        static_cast<uint32>(source->GetNumFloatTrackToFloatSlotIndices()));
    interface.NumPartitionIndices(
        static_cast<uint32>(source->GetNumPartitionIndices()));

    if (interface.LayoutVersion() >= HK700) {
      SetLockedArrayCapacity(
          interface.data,
          interface.m(clgen::hkaAnimationBinding::Members::
                          numTransformTrackToBoneIndices),
          static_cast<uint32>(source->GetNumTransformTrackToBoneIndices()));
      SetLockedArrayCapacity(
          interface.data,
          interface.m(clgen::hkaAnimationBinding::Members::
                          numFloatTrackToFloatSlotIndices),
          static_cast<uint32>(source->GetNumFloatTrackToFloatSlotIndices()));
    }

    if (interface.LayoutVersion() >= HK2012_1) {
      SetLockedArrayCapacity(
          interface.data,
          interface.m(clgen::hkaAnimationBinding::Members::numPartitionIndices),
          static_cast<uint32>(source->GetNumPartitionIndices()));
    }
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }

  ~hkaAnimationBindingMidInterface() {
    if (saver) {
      free(interface.data);
    }
  }
};

IhkVirtualClass *hkaAnimationBindingInternalInterface::Create(CRule rule) {
  return new hkaAnimationBindingMidInterface{
      clgen::LayoutLookup{rule.version, rule.x64, rule.reusePadding}, nullptr};
}
