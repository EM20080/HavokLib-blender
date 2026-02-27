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

#include "hka_animation.hpp"
#include "hklib/hka_skeleton.hpp"
#include "internal/hka_quantizedanimation.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "hka_animation_quantized.inl"

namespace {
struct QuantizedAnimationHeader {
  uint16 headerSize;
  uint16 numBones;
  uint16 numFloats;
  uint16 numFrames;
  float duration;
  uint16 numStaticTranslations;
  uint16 numStaticRotations;
  uint16 numStaticScales;
  uint16 numStaticFloats;
  uint16 numDynamicTranslations;
  uint16 numDynamicRotations;
  uint16 numDynamicScales;
  uint16 numDynamicFloats;
  uint16 frameSize;
  uint16 staticElementsOffset;
  uint16 dynamicElementsOffset;
  uint16 staticValuesOffset;
  uint16 dynamicRangeMinimumsOffset;
  uint16 dynamicRangeSpansOffset;
};

inline uint16 ReadU16(const char *buffer, bool bigEndian) {
  const uint8 b0 = static_cast<uint8>(buffer[0]);
  const uint8 b1 = static_cast<uint8>(buffer[1]);

  if (bigEndian) {
    return static_cast<uint16>((static_cast<uint16>(b0) << 8) | b1);
  }

  return static_cast<uint16>((static_cast<uint16>(b1) << 8) | b0);
}

inline float ReadF32(const char *buffer, bool bigEndian) {
  uint8 packed[4]{};

  if (bigEndian) {
    packed[0] = static_cast<uint8>(buffer[3]);
    packed[1] = static_cast<uint8>(buffer[2]);
    packed[2] = static_cast<uint8>(buffer[1]);
    packed[3] = static_cast<uint8>(buffer[0]);
  } else {
    packed[0] = static_cast<uint8>(buffer[0]);
    packed[1] = static_cast<uint8>(buffer[1]);
    packed[2] = static_cast<uint8>(buffer[2]);
    packed[3] = static_cast<uint8>(buffer[3]);
  }

  float value = 0.0f;
  std::memcpy(&value, packed, sizeof(value));
  return value;
}

inline QuantizedAnimationHeader ReadHeader(const char *buffer, bool bigEndian) {
  QuantizedAnimationHeader output{};

  output.headerSize = ReadU16(buffer + 0, bigEndian);
  output.numBones = ReadU16(buffer + 2, bigEndian);
  output.numFloats = ReadU16(buffer + 4, bigEndian);
  output.numFrames = ReadU16(buffer + 6, bigEndian);
  output.duration = ReadF32(buffer + 8, bigEndian);
  output.numStaticTranslations = ReadU16(buffer + 12, bigEndian);
  output.numStaticRotations = ReadU16(buffer + 14, bigEndian);
  output.numStaticScales = ReadU16(buffer + 16, bigEndian);
  output.numStaticFloats = ReadU16(buffer + 18, bigEndian);
  output.numDynamicTranslations = ReadU16(buffer + 20, bigEndian);
  output.numDynamicRotations = ReadU16(buffer + 22, bigEndian);
  output.numDynamicScales = ReadU16(buffer + 24, bigEndian);
  output.numDynamicFloats = ReadU16(buffer + 26, bigEndian);
  output.frameSize = ReadU16(buffer + 28, bigEndian);
  output.staticElementsOffset = ReadU16(buffer + 30, bigEndian);
  output.dynamicElementsOffset = ReadU16(buffer + 32, bigEndian);
  output.staticValuesOffset = ReadU16(buffer + 34, bigEndian);
  output.dynamicRangeMinimumsOffset = ReadU16(buffer + 36, bigEndian);
  output.dynamicRangeSpansOffset = ReadU16(buffer + 38, bigEndian);
  return output;
}

inline std::string NormalizeName(std::string_view name) {
  if (name.empty()) {
    return {};
  }

  std::string out{name};
  const size_t ltPos = out.find("@LT");
  if (ltPos != std::string::npos) {
    out.resize(ltPos);
  }

  for (char &ch : out) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }

  return out;
}

inline void InitPose(std::vector<hkQTransform> &pose) {
  for (auto &p : pose) {
    p.translation = Vector4A16(0.0f);
    p.rotation = Vector4A16(0.0f, 0.0f, 0.0f, 1.0f);
    p.scale = Vector4A16(1.0f, 1.0f, 1.0f, 0.0f);
  }
}

inline void InitPoseFromSkeleton(std::vector<hkQTransform> &pose,
                                 const hkaSkeleton *skeleton) {
  InitPose(pose);

  if (!skeleton) {
    return;
  }

  const size_t numBones = skeleton->GetNumBones();
  const size_t numTracks = std::min(pose.size(), numBones);

  for (size_t i = 0; i < numTracks; i++) {
    if (const hkQTransform *tm = skeleton->GetBoneTM(i)) {
      pose[i] = *tm;
    }
  }
}

inline void AssignScalar(std::vector<hkQTransform> &pose, uint16 element,
                         float value) {
  if (pose.empty()) {
    return;
  }

  const size_t trackIndex = element / 12;
  const size_t component = element % 12;

  if (trackIndex >= pose.size()) {
    return;
  }

  auto &track = pose[trackIndex];

  switch (component) {
  case 0:
  case 1:
  case 2:
    track.translation[component] = value;
    return;
  case 8:
  case 9:
  case 10:
    track.scale[component - 8] = value;
    return;
  default:
    return;
  }
}

inline void AssignRotation(std::vector<hkQTransform> &pose, uint16 element,
                           const Vector4A16 &value) {
  if (pose.empty()) {
    return;
  }

  const size_t trackIndex = element / 3;
  if (trackIndex >= pose.size()) {
    return;
  }

  pose[trackIndex].rotation = value;
}

inline Vector4A16 Read48Quat(const char *&buffer, bool bigEndian) {
  constexpr uint64 mask = (1 << 15) - 1;
  constexpr float fractal = 0.000043161f;
  const Vector4A16 fract(fractal, fractal, fractal, 0.0f);

  const uint16 in0 = ReadU16(buffer, bigEndian);
  const uint16 in1 = ReadU16(buffer + 2, bigEndian);
  const uint16 in2 = ReadU16(buffer + 4, bigEndian);
  const int resultShift = ((in1 >> 14) & 2) | ((in0 >> 15) & 1);
  const bool rSign = (in2 >> 15) != 0;

  const IVector4A16 retVal0(in0, in1, in2, 0.f);
  auto retVal1 = Vector4A16((retVal0 & mask) - (mask >> 1)) * fract;
  retVal1.QComputeElement();

  const Vector4A16 wmul(1.f, 1.f, 1.f, rSign ? -1.f : 1.f);
  const auto retVal = (wmul * retVal1)._data;
  buffer += 6;

  switch (resultShift) {
  case 0:
    return _mm_shuffle_ps(retVal, retVal, _MM_SHUFFLE(2, 1, 0, 3));
  case 1:
    return _mm_shuffle_ps(retVal, retVal, _MM_SHUFFLE(2, 1, 3, 0));
  case 2:
    return _mm_shuffle_ps(retVal, retVal, _MM_SHUFFLE(2, 3, 1, 0));
  default:
    return retVal;
  }
}
} // namespace

struct hkaQuantizedAnimationMidInterface
    : hkaQuantizedAnimationInternalInterface,
      hkaAnimationMidInterface<hkaAnimationLerpSampler> {
  clgen::hkaQuantizedAnimation::Interface interface;
  QuantizedAnimationHeader quantHeader{};
  const char *data = nullptr;
  size_t dataSize = 0;
  bool bigEndian = false;
  bool valid = false;
  const hkaSkeleton *referenceSkeleton = nullptr;

  size_t numDynamicScalars = 0;
  size_t numDynamicRotations = 0;
  std::vector<uint16> dynamicScalarElements;
  std::vector<uint16> dynamicRotationElements;
  std::vector<float> dynamicScalarMins;
  std::vector<float> dynamicScalarSpans;
  std::vector<int32> trackToBone;
  bool hasTrackMap = false;

  std::vector<hkQTransform> staticPose;
  mutable std::vector<hkQTransform> frameCache;
  mutable int32 cachedFrame = -1;

  hkaQuantizedAnimationMidInterface(clgen::LayoutLookup rules, char *inData)
      : interface{inData, rules} {}

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
  }

  const char *GetData() const override { return data; }
  size_t GetDataSize() const override { return dataSize; }
  void SetReferenceSkeleton(const hkaSkeleton *skeleton) override {
    referenceSkeleton = skeleton;
    if (interface.Data() && interface.NumData()) {
      Process();
    }
  }

  void Process() override {
    data = interface.Data();
    dataSize = interface.NumData();
    bigEndian = interface.Endian() != 0;
    valid = false;
    cachedFrame = -1;

    const size_t numTracks = GetNumOfTransformTracks();
    const hkaSkeleton *sampleSkeleton = referenceSkeleton;

    if (!sampleSkeleton && interface.Skeleton() && this->header) {
      sampleSkeleton = safe_deref_cast<const hkaSkeleton>(
          this->header->GetClass(interface.Skeleton()));
    }

    staticPose.resize(numTracks);
    frameCache.resize(numTracks);
    InitPoseFromSkeleton(staticPose, sampleSkeleton);
    InitPose(frameCache);
    BuildTrackMap(sampleSkeleton);

    if (!data || dataSize < sizeof(QuantizedAnimationHeader)) {
      return;
    }

    quantHeader = ReadHeader(data, bigEndian);

    if (!quantHeader.frameSize || !quantHeader.numFrames) {
      return;
    }

    const size_t dynamicElementCount = static_cast<size_t>(
        quantHeader.numDynamicTranslations + quantHeader.numDynamicRotations +
        quantHeader.numDynamicScales + quantHeader.numDynamicFloats);

    const size_t staticElementCount = static_cast<size_t>(
        quantHeader.numStaticTranslations + quantHeader.numStaticRotations +
        quantHeader.numStaticScales + quantHeader.numStaticFloats);

    if (quantHeader.staticElementsOffset + staticElementCount * sizeof(uint16) >
            dataSize ||
        quantHeader.dynamicElementsOffset + dynamicElementCount * sizeof(uint16) >
            dataSize ||
        quantHeader.staticValuesOffset > dataSize ||
        quantHeader.dynamicRangeMinimumsOffset > dataSize ||
        quantHeader.dynamicRangeSpansOffset > dataSize ||
        quantHeader.headerSize > dataSize) {
      return;
    }

    const size_t numStaticScalars = static_cast<size_t>(
        quantHeader.numStaticTranslations + quantHeader.numStaticScales +
        quantHeader.numStaticFloats);
    const size_t numStaticRotations = quantHeader.numStaticRotations;

    std::vector<uint16> staticElements(staticElementCount);
    for (size_t i = 0; i < staticElementCount; i++) {
      staticElements[i] =
          ReadU16(data + quantHeader.staticElementsOffset + i * 2,
                                  bigEndian);
    }

    for (size_t i = 0; i < numStaticScalars; i++) {
      const size_t scalarOffset =
          quantHeader.staticValuesOffset + i * sizeof(float);
      if (scalarOffset + sizeof(float) > dataSize) {
        break;
      }

      const float value = ReadF32(data + scalarOffset, bigEndian);
      AssignScalar(staticPose, staticElements[i], value);
    }

    const char *staticRotationsPtr =
        data + quantHeader.staticValuesOffset + numStaticScalars * sizeof(float);

    for (size_t i = 0; i < numStaticRotations; i++) {
      if (staticRotationsPtr + 6 > data + dataSize) {
        break;
      }

      const Vector4A16 rot = Read48Quat(staticRotationsPtr, bigEndian);
      AssignRotation(staticPose, staticElements[numStaticScalars + i], rot);
    }

    numDynamicScalars = static_cast<size_t>(quantHeader.numDynamicTranslations +
                                            quantHeader.numDynamicScales +
                                            quantHeader.numDynamicFloats);
    numDynamicRotations = quantHeader.numDynamicRotations;
    dynamicScalarElements.resize(numDynamicScalars);
    dynamicRotationElements.resize(numDynamicRotations);
    dynamicScalarMins.resize(numDynamicScalars);
    dynamicScalarSpans.resize(numDynamicScalars);

    for (size_t i = 0; i < numDynamicScalars; i++) {
      dynamicScalarElements[i] =
          ReadU16(data + quantHeader.dynamicElementsOffset + i * 2, bigEndian);
    }

    for (size_t i = 0; i < numDynamicRotations; i++) {
      dynamicRotationElements[i] = ReadU16(
          data + quantHeader.dynamicElementsOffset + (numDynamicScalars + i) * 2,
          bigEndian);
    }

    for (size_t i = 0; i < numDynamicScalars; i++) {
      const size_t minimumOffset =
          quantHeader.dynamicRangeMinimumsOffset + i * sizeof(float);
      const size_t spanOffset =
          quantHeader.dynamicRangeSpansOffset + i * sizeof(float);

      if (minimumOffset + sizeof(float) > dataSize ||
          spanOffset + sizeof(float) > dataSize) {
        dynamicScalarMins.resize(i);
        dynamicScalarSpans.resize(i);
        dynamicScalarElements.resize(i);
        numDynamicScalars = i;
        break;
      }

      dynamicScalarMins[i] = ReadF32(data + minimumOffset, bigEndian);
      dynamicScalarSpans[i] = ReadF32(data + spanOffset, bigEndian);
    }

    frameCache = staticPose;
    cachedFrame = -1;

    numFrames = quantHeader.numFrames;
    if (Duration() > 0.0f) {
      frameRate = static_cast<uint32>(numFrames / Duration());
    } else {
      frameRate = 0;
    }
    valid = true;
  }

  void BuildTrackMap(const hkaSkeleton *skeleton) {
    hasTrackMap = false;
    trackToBone.clear();

    if (!skeleton) {
      return;
    }

    const size_t numTracks = GetNumOfTransformTracks();
    if (!numTracks) {
      return;
    }

    if (GetNumAnnotations() < numTracks) {
      return;
    }

    const size_t numBones = skeleton->GetNumBones();
    std::unordered_map<std::string, size_t> boneMap;
    boneMap.reserve(numBones);

    for (size_t i = 0; i < numBones; i++) {
      std::string name = NormalizeName(skeleton->GetBoneName(i));
      if (name.empty()) {
        continue;
      }
      if (boneMap.find(name) == boneMap.end()) {
        boneMap.emplace(std::move(name), i);
      }
    }

    trackToBone.resize(numTracks, -1);
    for (size_t i = 0; i < numTracks; i++) {
      auto annot = GetAnnotation(i);
      if (!annot) {
        continue;
      }
      std::string name = NormalizeName(annot->GetName());
      if (name.empty()) {
        continue;
      }
      auto it = boneMap.find(name);
      if (it != boneMap.end()) {
        trackToBone[i] = static_cast<int32>(it->second);
      }
    }

    for (int32 v : trackToBone) {
      if (v >= 0) {
        hasTrackMap = true;
        break;
      }
    }

    if (!hasTrackMap) {
      trackToBone.clear();
    }
  }

  size_t BoneIndex(size_t trackID) const override {
    if (trackID < trackToBone.size()) {
      const int32 mapped = trackToBone[trackID];
      if (mapped >= 0) {
        return static_cast<size_t>(mapped);
      }
    }
    return trackID;
  }

  void DecodeFrame(int32 frame) const {
    if (!valid || frame == cachedFrame || numFrames == 0 || frameCache.empty()) {
      return;
    }

    frame = std::clamp<int32>(frame, 0, static_cast<int32>(numFrames - 1));
    frameCache = staticPose;

    const size_t frameStart =
        quantHeader.headerSize + static_cast<size_t>(frame) * quantHeader.frameSize;
    if (frameStart >= dataSize) {
      cachedFrame = frame;
      return;
    }

    const size_t frameEnd = std::min(dataSize, frameStart + quantHeader.frameSize);
    const char *frameData = data + frameStart;

    const float fractal = 1.0f / 65535.0f;
    const char *current = frameData;

    for (size_t i = 0; i < numDynamicScalars; i++) {
      if (current + 2 > data + frameEnd) {
        break;
      }

      const uint16 quantized = ReadU16(current, bigEndian);
      current += 2;

      const float value =
          dynamicScalarMins[i] + dynamicScalarSpans[i] * (quantized * fractal);
      AssignScalar(frameCache, dynamicScalarElements[i], value);
    }

    for (size_t i = 0; i < numDynamicRotations; i++) {
      if (current + 6 > data + frameEnd) {
        break;
      }

      const Vector4A16 rot = Read48Quat(current, bigEndian);
      AssignRotation(frameCache, dynamicRotationElements[i], rot);
    }

    cachedFrame = frame;
  }

  void GetFrame(size_t trackID, int32 frame, hkQTransform &out) const override {
    if (trackID >= frameCache.size()) [[unlikely]] {
      out.translation = Vector4A16(0.0f);
      out.rotation = Vector4A16(0.0f, 0.0f, 0.0f, 1.0f);
      out.scale = Vector4A16(1.0f, 1.0f, 1.0f, 0.0f);
      return;
    }

    DecodeFrame(frame);
    out = frameCache[trackID];
  }
};

IhkVirtualClass *hkaQuantizedAnimationInternalInterface::Create(CRule rule) {
  return new hkaQuantizedAnimationMidInterface{
      clgen::LayoutLookup{rule.version, rule.x64, rule.reusePadding}, nullptr};
}
