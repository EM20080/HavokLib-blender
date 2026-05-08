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

#include "hka_animation.hpp"
#include "hka_spline_decompressor.hpp"

#include "base.hpp"
#include "hka_animation_spline.inl"
#include <algorithm>
#include <span>

namespace {

struct AnnotationFrameData {
  float time;
  std::string text;
};

struct AnnotationTrackData {
  std::string name;
  std::vector<AnnotationFrameData> frames;
  std::vector<char> header;
};

void SetLockedArrayCapacity(char *data, int16 countOffset, uint32 count) {
  if (countOffset < 0) {
    return;
  }

  *reinterpret_cast<uint32 *>(data + countOffset + 4) = 0x80000000u | count;
}

bool UseLegacy16BytePacking(const clgen::LayoutLookup &lookup, uint8 ptrSize) {
  return ptrSize == 4 && lookup.version >= HK700;
}

AnnotationTrackData ExtractAnnotationTrack(const hkaAnnotationTrack *track,
                                          clgen::LayoutLookup lookup) {
  AnnotationTrackData result;
  result.name = std::string(track->GetName());
  result.header.resize(
      clgen::GetLayout(clgen::hkaAnnotationTrack::LAYOUTS, lookup)->totalSize);

  clgen::hkaAnnotationTrack::Interface outTrack(result.header.data(), lookup);
  const auto numFrames = static_cast<uint32>(track->Size());
  outTrack.NumAnnotations(numFrames);

  if (lookup.version >= HK700) {
    SetLockedArrayCapacity(result.header.data(),
                           outTrack.m(clgen::hkaAnnotationTrack::Members::
                                          numAnnotations),
                           numFrames);
  }

  result.frames.reserve(numFrames);

  for (size_t i = 0; i < track->Size(); i++) {
    auto frame = track->At(i);
    result.frames.push_back({frame.time, std::string(frame.text)});
  }

  return result;
}

void WriteAnnotationTrackTrailing(const AnnotationTrackData &track,
                                  BinWritterRef_e wr, hkFixups &fixups,
                                  clgen::LayoutLookup lookup,
                                  size_t trackBegin) {
  auto &locals = fixups.locals;
  clgen::hkaAnnotationTrack::Interface outTrack(
      const_cast<char *>(track.header.data()), lookup);

  if (outTrack.m(clgen::hkaAnnotationTrack::Members::name) >= 0) {
    wr.ApplyPadding();
    locals.emplace_back(trackBegin +
                            outTrack.m(clgen::hkaAnnotationTrack::Members::name),
                        wr.Tell());
    wr.WriteBuffer(track.name.data(), track.name.size());
    wr.Skip(1);
  }

  if (track.frames.empty() ||
      outTrack.m(clgen::hkaAnnotationTrack::Members::annotations) < 0) {
    return;
  }

  wr.ApplyPadding();
  locals.emplace_back(
      trackBegin + outTrack.m(clgen::hkaAnnotationTrack::Members::annotations),
      wr.Tell());

  const auto annotationType =
      clgen::GetLayout(clgen::hkaAnnotation::LAYOUTS, lookup);
  std::vector<size_t> textFixups;
  textFixups.reserve(track.frames.size());

  for (const auto &frame : track.frames) {
    std::vector<char> frameData(annotationType->totalSize);
    clgen::hkaAnnotation::Interface outFrame(frameData.data(), lookup);
    outFrame.Time(frame.time);
    const size_t frameBegin = wr.Tell();
    wr.WriteBuffer(frameData.data(), frameData.size());

    if (!frame.text.empty() &&
        outFrame.m(clgen::hkaAnnotation::Members::text) >= 0) {
      textFixups.push_back(locals.size());
      locals.emplace_back(frameBegin +
                          outFrame.m(clgen::hkaAnnotation::Members::text));
    } else {
      textFixups.push_back(static_cast<size_t>(-1));
    }
  }

  for (size_t i = 0; i < track.frames.size(); i++) {
    if (textFixups[i] == static_cast<size_t>(-1)) {
      continue;
    }

    wr.ApplyPadding();
    locals[textFixups[i]].destination = wr.Tell();
    wr.WriteBuffer(track.frames[i].text.data(), track.frames[i].text.size());
    wr.Skip(1);
  }
}

struct hkaSplineCompressedAnimationSaver {
  const hkaSplineCompressedAnimationInternalInterface *in;
  const clgen::hkaSplineCompressedAnimation::Interface *out;

  void SaveU32Array(BinWritterRef_e wr, hkFixups &fixups, size_t sBegin,
                    int16 memberOffset, std::span<const uint32> values) {
    if (memberOffset < 0 || values.empty()) {
      return;
    }

    wr.ApplyPadding();
    fixups.locals.emplace_back(sBegin + memberOffset, wr.Tell());

    for (uint32 value : values) {
      wr.Write(value);
    }
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) {
    const size_t sBegin = wr.Tell();
    auto &locals = fixups.locals;
    auto &lay = *out->layout;
    using mm = clgen::hkaSplineCompressedAnimation::Members;
    using am = clgen::hkaAnimation::Members;
    const bool is2012_2 = lay.ptrSize == 4 && out->lookup.version == HK2012_2;

    wr.WriteBuffer(out->data, lay.totalSize);
    if (is2012_2) {
      wr.Skip(16);
    }
    const auto anim = out->BasehkaAnimation();

    if (const auto *extractedMotion = in->GetExtractedMotion();
        extractedMotion &&
        anim.m(am::extractedMotion) >= 0) {
      locals.emplace_back(sBegin + anim.m(am::extractedMotion), extractedMotion);
    }

    if (const auto numAnnotations = in->GetNumAnnotations(); numAnnotations) {
      std::vector<AnnotationTrackData> tracks;
      tracks.reserve(numAnnotations);

      for (size_t i = 0; i < numAnnotations; i++) {
        auto track = in->GetAnnotation(i);
        tracks.push_back(ExtractAnnotationTrack(track.get(), out->lookup));
      }

      if (UseLegacy16BytePacking(out->lookup, lay.ptrSize) && !is2012_2) {
        wr.ApplyPadding(32);
      } else {
        wr.ApplyPadding();
      }
      locals.emplace_back(sBegin + anim.m(am::annotations), wr.Tell());

      if (anim.LayoutVersion() >= HK700) {
        std::vector<size_t> trackBegins;
        trackBegins.reserve(tracks.size());

        for (const auto &track : tracks) {
          trackBegins.push_back(wr.Tell());
          wr.WriteBuffer(track.header.data(), track.header.size());
        }

        for (size_t i = 0; i < tracks.size(); i++) {
          WriteAnnotationTrackTrailing(tracks[i], wr, fixups, out->lookup,
                                       trackBegins[i]);
        }
      } else {
        size_t curGFixup = fixups.globals.size();
        for (size_t i = 0; i < tracks.size(); i++) {
          fixups.globals.emplace_back(wr.Tell());
          wr.Skip(lay.ptrSize);
        }

        auto fndFinal =
            std::find_if(fixups.finals.begin(), fixups.finals.end(),
                         [&](const hkFixup &f) { return f.destClass == in; });

        if (es::IsEnd(fixups.finals, fndFinal)) {
          throw std::runtime_error(
              "hkaAnnotationTrack final was not found for spline animation export");
        }

        for (const auto &track : tracks) {
          wr.ApplyPadding(16);
          const size_t trackBegin = wr.Tell();
          fixups.globals[curGFixup++].destination = trackBegin;
          fndFinal->destination = trackBegin;
          ++fndFinal;
          wr.WriteBuffer(track.header.data(), track.header.size());
          WriteAnnotationTrackTrailing(track, wr, fixups, out->lookup,
                                       trackBegin);
        }
      }
    }

    SaveU32Array(wr, fixups, sBegin, out->m(mm::blockOffsets),
                 in->GetBlockOffsets());
    SaveU32Array(wr, fixups, sBegin, out->m(mm::floatBlockOffsets),
                 in->GetFloatBlockOffsets());
    SaveU32Array(wr, fixups, sBegin, out->m(mm::transformBlockOffsets),
                 in->GetTransformOffsets());
    SaveU32Array(wr, fixups, sBegin, out->m(mm::floatOffsets),
                 in->GetFloatOffsets());

    if (const uint32 numData = in->GetNumDataBuffer();
        numData && out->m(mm::dataBuffer) >= 0) {
      wr.ApplyPadding();
      locals.emplace_back(sBegin + out->m(mm::dataBuffer), wr.Tell());
      wr.WriteBuffer(in->GetData(), numData);
    }
  }
};

} // namespace

struct hkaSplineCompressedAnimationMidInterface
    : hkaSplineCompressedAnimationInternalInterface,
      hkaAnimationMidInterface<hkaAnimationInternalInterface> {
  clgen::hkaSplineCompressedAnimation::Interface interface;
  hkaSplineDecompressor decomp;
  std::unique_ptr<hkaSplineCompressedAnimationSaver> saver;

  hkaSplineCompressedAnimationMidInterface(clgen::LayoutLookup rules,
                                           char *data)
      : interface{data, rules} {}

  void SetDataPointer(void *ptr) override {
    interface.data = static_cast<char *>(ptr);
  }

  const void *GetPointer() const override { return interface.data; }

  clgen::hkaAnimation::Interface Anim() const override {
    return interface.BasehkaAnimation();
  }

  void SwapEndian() override {
    hkaAnimationMidInterface<hkaAnimationInternalInterface>::SwapEndian();
    clgen::EndianSwap(interface);

    if (auto offsets = interface.FloatBlockOffsets()) {
      for (std::span<uint32> tms(offsets, interface.NumFloatBlockOffsets());
           auto &i : tms) {
        FByteswapper(i);
      }
    }

    if (auto offsets = interface.BlockOffsets()) {
      for (std::span<uint32> tms(offsets, interface.NumBlockOffsets());
           auto &i : tms) {
        FByteswapper(i);
      }
    }

    if (auto offsets = interface.FloatOffsets()) {
      for (std::span<uint32> tms(offsets, interface.NumFloatOffsets());
           auto &i : tms) {
        FByteswapper(i);
      }
    }

    if (auto offsets = interface.TransformBlockOffsets()) {
      for (std::span<uint32> tms(offsets,
                                 interface.NumTransformBlockOffsets());
           auto &i : tms) {
        FByteswapper(i);
      }
    }
  }

  void Process() override {
    decomp.Assign(this, static_cast<hkToolset>(interface.LayoutVersion()));
    const float duration = this->Duration();
    const uint32 numFrames = this->GetNumFrames();
    this->frameRate = duration > 0.0f && numFrames > 1
                          ? static_cast<uint32>(
                                (static_cast<float>(numFrames - 1) / duration) +
                                0.5f)
                          : 30;
  }

  uint32 GetNumFrames() const override { return interface.NumFrames(); }
  uint32 GetNumBlocks() const override { return interface.NumBlocks(); }
  uint32 GetMaxFramesPerBlock() const override {
    return interface.MaxFramesPerBlock();
  }
  uint32 GetMaskAndQuantizationSize() const override {
    return interface.MaskAndQuantizationSize();
  }
  uint32 GetNumDataBuffer() const override {
    return interface.NumDataBuffer();
  }
  float GetBlockDuration() const override { return interface.BlockDuration(); }
  float GetBlockInverseDuration() const override {
    return interface.BlockInverseDuration();
  }
  float GetFrameDuration() const override { return interface.FrameDuration(); }
  char *GetData() const override {
    return const_cast<char *>(interface.DataBuffer());
  }
  std::span<const uint32> GetBlockOffsets() const override {
    return {interface.BlockOffsets(), interface.NumBlockOffsets()};
  }
  std::span<const uint32> GetFloatBlockOffsets() const override {
    return {interface.FloatBlockOffsets(), interface.NumFloatBlockOffsets()};
  }
  std::span<const uint32> GetTransformOffsets() const override {
    return {interface.TransformBlockOffsets(),
            interface.NumTransformBlockOffsets()};
  }
  std::span<const uint32> GetFloatOffsets() const override {
    return {interface.FloatOffsets(), interface.NumFloatOffsets()};
  }

  void GetValue(uni::RTSValue &output, float time,
                size_t trackID) const override {
    const float blockInverseDuration = GetBlockInverseDuration();
    const float blockDuration = GetBlockDuration();
    size_t blockID = static_cast<size_t>(time * blockInverseDuration);

    if (blockID >= decomp.blocks.size()) [[unlikely]] {
      blockID = decomp.blocks.size() - 1;
    }

    float localTime = time - (static_cast<float>(blockID) * blockDuration);

    if (localTime < 0.f) {
      localTime = 0.f;
    }

    decomp.blocks[blockID].GetValue(trackID, localTime * frameRate, output);
  }

  void Reflect(const IhkVirtualClass *other) override {
    auto source =
        dynamic_cast<const hkaSplineCompressedAnimationInternalInterface *>(
            other);

    if (!source) {
      throw std::bad_cast{};
    }

    interface.data =
        static_cast<char *>(calloc(1, interface.layout->totalSize));
    saver = std::make_unique<hkaSplineCompressedAnimationSaver>();
    saver->in = source;
    saver->out = &interface;

    auto anim = interface.BasehkaAnimation();
    anim.AnimationType(interface.LayoutVersion() >= HK2011_1
                           ? 3
                           : HK_SPLINE_COMPRESSED_ANIMATION);
    anim.Duration(source->Duration());
    anim.NumOfTransformTracks(
        static_cast<uint32>(source->GetNumOfTransformTracks()));
    anim.NumOfFloatTracks(static_cast<uint32>(source->GetNumOfFloatTracks()));
    anim.NumAnnotations(static_cast<uint32>(source->GetNumAnnotations()));

    interface.NumFrames(source->GetNumFrames());
    interface.NumBlocks(source->GetNumBlocks());
    interface.MaxFramesPerBlock(source->GetMaxFramesPerBlock());
    interface.MaskAndQuantizationSize(source->GetMaskAndQuantizationSize());
    interface.BlockDuration(source->GetBlockDuration());
    interface.BlockInverseDuration(source->GetBlockInverseDuration());
    interface.FrameDuration(source->GetFrameDuration());
    interface.NumBlockOffsets(static_cast<uint32>(source->GetBlockOffsets().size()));
    interface.NumFloatBlockOffsets(
        static_cast<uint32>(source->GetFloatBlockOffsets().size()));
    interface.NumTransformBlockOffsets(
        static_cast<uint32>(source->GetTransformOffsets().size()));
    interface.NumFloatOffsets(
        static_cast<uint32>(source->GetFloatOffsets().size()));
    interface.NumDataBuffer(source->GetNumDataBuffer());

    if (interface.LayoutVersion() >= HK700) {
      SetLockedArrayCapacity(
          anim.data,
          anim.m(clgen::hkaAnimation::Members::numAnnotations),
          static_cast<uint32>(source->GetNumAnnotations()));
      SetLockedArrayCapacity(
          interface.data,
          interface.m(clgen::hkaSplineCompressedAnimation::Members::
                          numBlockOffsets),
          static_cast<uint32>(source->GetBlockOffsets().size()));
      SetLockedArrayCapacity(
          interface.data,
          interface.m(clgen::hkaSplineCompressedAnimation::Members::
                          numFloatBlockOffsets),
          static_cast<uint32>(source->GetFloatBlockOffsets().size()));
      SetLockedArrayCapacity(
          interface.data,
          interface.m(clgen::hkaSplineCompressedAnimation::Members::
                          numTransformBlockOffsets),
          static_cast<uint32>(source->GetTransformOffsets().size()));
      SetLockedArrayCapacity(
          interface.data,
          interface.m(clgen::hkaSplineCompressedAnimation::Members::
                          numFloatOffsets),
          static_cast<uint32>(source->GetFloatOffsets().size()));
      SetLockedArrayCapacity(
          interface.data,
          interface.m(clgen::hkaSplineCompressedAnimation::Members::
                          numDataBuffer),
          source->GetNumDataBuffer());
    }
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }

  ~hkaSplineCompressedAnimationMidInterface() {
    if (saver) {
      free(interface.data);
    }
  }
};

IhkVirtualClass *
hkaSplineCompressedAnimationInternalInterface::Create(CRule rule) {
  return new hkaSplineCompressedAnimationMidInterface{
      clgen::LayoutLookup{rule.version, rule.x64, rule.reusePadding}, nullptr};
}
