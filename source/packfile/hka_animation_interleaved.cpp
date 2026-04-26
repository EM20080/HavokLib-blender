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

#include "internal/hka_interleavedanimation.hpp"
#include <span>

#include "base.hpp"
#include "hka_animation.hpp"
#include "hka_animation_interleaved.inl"

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

struct hkaInterleavedAnimationSaver {
  const hkaInterleavedAnimationInternalInterface *in;
  const clgen::hkaInterleavedAnimation::Interface *out;

  void Save(BinWritterRef_e wr, hkFixups &fixups) {
    const size_t sBegin = wr.Tell();
    auto &locals = fixups.locals;
    auto &lay = *out->layout;
    using mm = clgen::hkaInterleavedAnimation::Members;
    using am = clgen::hkaAnimation::Members;

    wr.WriteBuffer(out->data, lay.totalSize);
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

      wr.ApplyPadding();
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
              "hkaAnnotationTrack final was not found for animation export");
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

    if (const auto numTransforms = in->GetNumTransforms(); numTransforms) {
      wr.ApplyPadding();
      locals.emplace_back(sBegin + out->m(mm::transforms), wr.Tell());

      for (size_t i = 0; i < numTransforms; i++) {
        wr.Write(*in->GetTransform(i));
      }
    }

    if (const auto numFloats = in->GetNumFloats(); numFloats) {
      wr.ApplyPadding();
      locals.emplace_back(sBegin + out->m(mm::floats), wr.Tell());

      for (size_t i = 0; i < numFloats; i++) {
        wr.Write(in->GetFloat(i));
      }
    }
  }
};

} // namespace

struct hkaInterleavedAnimationMidInterface
    : hkaInterleavedAnimationInternalInterface,
      hkaAnimationMidInterface<hkaAnimationLerpSampler> {
  clgen::hkaInterleavedAnimation::Interface interface;
  std::unique_ptr<hkaInterleavedAnimationSaver> saver;

  hkaInterleavedAnimationMidInterface(clgen::LayoutLookup rules, char *data)
      : interface {
    data, rules
  } {
  }

  void SetDataPointer(void *ptr) override {
    interface.data = static_cast<char *>(ptr);
  }

  const void *GetPointer() const override { return interface.data; }

  clgen::hkaAnimation::Interface Anim() const override {
    return interface.BasehkaAnimation();
  }

  void SwapEndian() override {
    hkaAnimationMidInterface<hkaAnimationLerpSampler>::SwapEndian();
    clgen::EndianSwap(interface);

    if (auto transforms = interface.Transforms()) {
      for (std::span<hkQTransform> tms(transforms, interface.NumTransforms());
           auto &i : tms) {
        FByteswapper(i.rotation);
        FByteswapper(i.scale);
        FByteswapper(i.translation);
      }
    }

    if (auto floats = interface.Floats()) {
      for (std::span<float> refs(floats, interface.NumFloats()); auto &i : refs) {
        FByteswapper(i);
      }
    }
  }

  void Process() override {
    numFrames =
        static_cast<uint32>(GetNumTransforms() / GetNumOfTransformTracks());
    frameRate = static_cast<uint32>(numFrames / Duration());
  }

  void GetFrame(size_t trackID, int32 frame, hkQTransform &out) const override {
    out = *GetTransform(frame * GetNumOfTransformTracks() + trackID);
  }

  size_t GetNumTransforms() const override { return interface.NumTransforms(); }
  size_t GetNumFloats() const override { return interface.NumFloats(); }

  const hkQTransform *GetTransform(size_t id) const override {
    return interface.Transforms() + id;
  }
  float GetFloat(size_t id) const override { return interface.Floats()[id]; }

  void Reflect(const IhkVirtualClass *other) override {
    auto source =
        dynamic_cast<const hkaInterleavedAnimationInternalInterface *>(other);

    if (!source) {
      throw std::bad_cast{};
    }

    interface.data =
        static_cast<char *>(calloc(1, interface.layout->totalSize));
    saver = std::make_unique<hkaInterleavedAnimationSaver>();
    saver->in = source;
    saver->out = &interface;

    auto anim = interface.BasehkaAnimation();
    anim.AnimationType(HK_INTERLEAVED_ANIMATION);
    anim.Duration(source->Duration());
    anim.NumOfTransformTracks(
        static_cast<uint32>(source->GetNumOfTransformTracks()));
    anim.NumOfFloatTracks(static_cast<uint32>(source->GetNumOfFloatTracks()));
    anim.NumAnnotations(static_cast<uint32>(source->GetNumAnnotations()));
    interface.NumTransforms(static_cast<uint32>(source->GetNumTransforms()));
    interface.NumFloats(static_cast<uint32>(source->GetNumFloats()));

    if (interface.LayoutVersion() >= HK700) {
      SetLockedArrayCapacity(
          anim.data,
          anim.m(clgen::hkaAnimation::Members::numAnnotations),
          static_cast<uint32>(source->GetNumAnnotations()));
      SetLockedArrayCapacity(
          interface.data,
          interface.m(clgen::hkaInterleavedAnimation::Members::numTransforms),
          static_cast<uint32>(source->GetNumTransforms()));
      SetLockedArrayCapacity(
          interface.data,
          interface.m(clgen::hkaInterleavedAnimation::Members::numFloats),
          static_cast<uint32>(source->GetNumFloats()));
    }
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }

  ~hkaInterleavedAnimationMidInterface() {
    if (saver) {
      free(interface.data);
    }
  }
};

IhkVirtualClass *hkaInterleavedAnimationInternalInterface::Create(CRule rule) {
  return new hkaInterleavedAnimationMidInterface{
      clgen::LayoutLookup{rule.version, rule.x64, rule.reusePadding}, nullptr};
}
