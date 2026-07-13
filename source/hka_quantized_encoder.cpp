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

#include "internal/hka_quantized_encoder.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace {

struct Range {
  float minimum;
  float span;
};

struct StaticScalar {
  uint16 element;
  float value;
};

struct DynamicScalar {
  uint16 sourceElement;
  uint16 element;
  Range range;
};

struct StaticRotation {
  uint16 element;
  Vector4A16 value;
};

struct DynamicRotation {
  uint16 sourceTrack;
  uint16 element;
};

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

uint32 alignTo(uint32 value, uint32 alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

void align(std::vector<char> &data, uint32 alignment) {
  data.resize(alignTo(uint32(data.size()), alignment));
}

void appendU8(std::vector<char> &data, uint8 value) {
  data.push_back(char(value));
}

void appendU16(std::vector<char> &data, uint16 value) {
  appendU8(data, uint8(value));
  appendU8(data, uint8(value >> 8));
}

void appendF32(std::vector<char> &data, float value) {
  const uint32 bits = std::bit_cast<uint32>(value);
  appendU8(data, uint8(bits));
  appendU8(data, uint8(bits >> 8));
  appendU8(data, uint8(bits >> 16));
  appendU8(data, uint8(bits >> 24));
}

float transformElement(const hkQTransform &transform, uint32 element) {
  if (element < 4) {
    return transform.translation[element];
  }
  if (element < 8) {
    return transform.rotation[element - 4];
  }
  return transform.scale[element - 8];
}

float referenceElement(const hkQTransform *referencePose, uint32 bone,
                       uint32 element) {
  if (referencePose) {
    return transformElement(referencePose[bone], element);
  }
  if (element == 7 || (element >= 8 && element < 11)) {
    return 1.0f;
  }
  return 0.0f;
}

Range scalarRange(const hkaQuantizedEncoderInput &input, uint32 element) {
  const uint32 stride = input.numTransformTracks;
  const uint32 track = element / 12;
  const uint32 component = element % 12;
  float minimum = transformElement(input.transforms[track], component);
  float maximum = minimum;
  for (uint32 frame = 1; frame < input.numFrames; frame++) {
    const float value =
        transformElement(input.transforms[frame * stride + track], component);
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
  }
  return {minimum, maximum - minimum};
}

Range floatRange(const hkaQuantizedEncoderInput &input, uint32 track) {
  float minimum = input.floats[track];
  float maximum = minimum;
  for (uint32 frame = 1; frame < input.numFrames; frame++) {
    const float value = input.floats[frame * input.numFloatTracks + track];
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
  }
  return {minimum, maximum - minimum};
}

void identifyScalar(uint16 sourceElement, uint16 element, Range range,
                    float reference, bool hasReference, float tolerance,
                    std::vector<StaticScalar> &statics,
                    std::vector<DynamicScalar> &dynamics) {
  if (range.span > tolerance + tolerance) {
    dynamics.push_back({sourceElement, element, range});
  } else if (!hasReference || range.minimum < reference - tolerance ||
             range.minimum + range.span > reference + tolerance) {
    statics.push_back({element, range.minimum + range.span * 0.5f});
  }
}

Vector4A16 normalized(Vector4A16 value) {
  const float length = std::sqrt(value.Dot(value));
  if (length > 0.0f) {
    value *= 1.0f / length;
  } else {
    value = Vector4A16(0.0f, 0.0f, 0.0f, 1.0f);
  }
  return value;
}

void identifyRotation(const hkaQuantizedEncoderInput &input, uint16 track,
                      uint16 bone, std::vector<StaticRotation> &statics,
                      std::vector<DynamicRotation> &dynamics) {
  Range ranges[4];
  bool referencePositive = true;
  bool referenceNegative = true;
  const float tolerance = input.settings.rotationTolerance;

  for (uint32 component = 0; component < 4; component++) {
    ranges[component] = scalarRange(input, track * 12 + component + 4);
    if (ranges[component].span > tolerance + tolerance) {
      dynamics.push_back({uint16(track * 3 + 1), uint16(bone * 3 + 1)});
      return;
    }

    const float reference = referenceElement(input.referencePose, bone,
                                             component + 4);
    const float minimum = ranges[component].minimum;
    const float maximum = minimum + ranges[component].span;
    referencePositive &= minimum >= reference - tolerance &&
                         maximum <= reference + tolerance;
    referenceNegative &= minimum >= -reference - tolerance &&
                         maximum <= -reference + tolerance;
  }

  if (referencePositive || referenceNegative) {
    return;
  }

  Vector4A16 value;
  for (uint32 component = 0; component < 4; component++) {
    value[component] =
        ranges[component].minimum + ranges[component].span * 0.5f;
  }
  statics.push_back({uint16(bone * 3 + 1), normalized(value)});
}

void packQuaternion(std::vector<char> &data, Vector4A16 value) {
  value = normalized(value);
  uint32 largest = 0;
  for (uint32 component = 1; component < 4; component++) {
    if (std::abs(value[component]) > std::abs(value[largest])) {
      largest = component;
    }
  }

  constexpr float scale = 23169.060792358416184518066368727f;
  uint16 packed[3];
  uint32 output = 0;
  for (uint32 component = 0; component < 4; component++) {
    if (component == largest) {
      continue;
    }
    int32 quantized = int32(value[component] * scale) + 16383;
    quantized = std::clamp(quantized, 0, 32767);
    packed[output++] = uint16(quantized);
  }

  packed[0] |= uint16((largest & 1) << 15);
  packed[1] |= uint16((largest >> 1) << 15);
  packed[2] |= uint16((value[largest] < 0.0f) << 15);
  appendU16(data, packed[0]);
  appendU16(data, packed[1]);
  appendU16(data, packed[2]);
}

uint16 quantize(float value, Range range) {
  const float normalizedValue = (value - range.minimum) / range.span;
  return uint16(std::clamp(normalizedValue * 65535.0f, 0.0f, 65535.0f));
}

template <class T>
void appendElements(std::vector<char> &data, const std::vector<T> &elements) {
  align(data, 2);
  for (const auto &element : elements) {
    appendU16(data, element.element);
  }
}

void appendHeader(std::vector<char> &data,
                  const QuantizedAnimationHeader &header) {
  appendU16(data, header.headerSize);
  appendU16(data, header.numBones);
  appendU16(data, header.numFloats);
  appendU16(data, header.numFrames);
  appendF32(data, header.duration);
  appendU16(data, header.numStaticTranslations);
  appendU16(data, header.numStaticRotations);
  appendU16(data, header.numStaticScales);
  appendU16(data, header.numStaticFloats);
  appendU16(data, header.numDynamicTranslations);
  appendU16(data, header.numDynamicRotations);
  appendU16(data, header.numDynamicScales);
  appendU16(data, header.numDynamicFloats);
  appendU16(data, header.frameSize);
  appendU16(data, header.staticElementsOffset);
  appendU16(data, header.dynamicElementsOffset);
  appendU16(data, header.staticValuesOffset);
  appendU16(data, header.dynamicRangeMinimumsOffset);
  appendU16(data, header.dynamicRangeSpansOffset);
}

} // namespace

bool hkaEncodeQuantizedAnimation(const hkaQuantizedEncoderInput &input,
                                 std::vector<char> &output) {
  output.clear();
  if (!input.transforms || !input.numFrames || !input.numTransformTracks ||
      !input.numBones || input.numFrames > 0xffff || input.numBones > 0xffff ||
      input.numFloats > 0xffff ||
      (input.numFloatTracks && !input.floats)) {
    return false;
  }

  std::vector<StaticScalar> staticTranslations;
  std::vector<StaticScalar> staticScales;
  std::vector<StaticRotation> staticRotations;
  std::vector<StaticScalar> staticFloats;
  std::vector<DynamicScalar> dynamicTranslations;
  std::vector<DynamicScalar> dynamicScales;
  std::vector<DynamicRotation> dynamicRotations;
  std::vector<DynamicScalar> dynamicFloats;

  for (uint32 track = 0; track < input.numTransformTracks; track++) {
    const uint32 bone = input.transformTrackToBoneIndices
                            ? input.transformTrackToBoneIndices[track]
                            : track;
    if (bone >= input.numBones) {
      return false;
    }

    for (uint32 component = 0; component < 3; component++) {
      const uint16 sourceElement = uint16(track * 12 + component);
      const uint16 element = uint16(bone * 12 + component);
      identifyScalar(sourceElement, element, scalarRange(input, sourceElement),
                     referenceElement(input.referencePose, bone, component),
                     true, input.settings.translationTolerance,
                     staticTranslations, dynamicTranslations);
    }

    for (uint32 component = 8; component < 11; component++) {
      const uint16 sourceElement = uint16(track * 12 + component);
      const uint16 element = uint16(bone * 12 + component);
      identifyScalar(sourceElement, element, scalarRange(input, sourceElement),
                     referenceElement(input.referencePose, bone, component),
                     true, input.settings.scaleTolerance, staticTranslations,
                     dynamicTranslations);
    }

    identifyRotation(input, uint16(track), uint16(bone), staticRotations,
                     dynamicRotations);
  }

  for (uint32 track = 0; track < input.numFloatTracks; track++) {
    const uint32 slot = input.floatTrackToFloatSlotIndices
                            ? input.floatTrackToFloatSlotIndices[track]
                            : track;
    if (slot >= input.numFloats) {
      return false;
    }
    identifyScalar(uint16(track), uint16(slot), floatRange(input, track),
                   input.referenceFloats ? input.referenceFloats[slot] : 0.0f,
                   input.referenceFloats != nullptr,
                   input.settings.floatTolerance, staticFloats, dynamicFloats);
  }

  const size_t staticCount = staticTranslations.size() + staticScales.size() +
                             staticRotations.size() + staticFloats.size();
  const size_t dynamicCount =
      dynamicTranslations.size() + dynamicScales.size() +
      dynamicRotations.size() + dynamicFloats.size();
  if (staticCount > 0xffff || dynamicCount > 0xffff) {
    return false;
  }

  QuantizedAnimationHeader header{};
  header.numBones = uint16(input.numBones);
  header.numFloats = uint16(input.numFloats);
  header.numFrames = uint16(input.numFrames);
  header.duration = input.duration;
  header.numStaticTranslations = uint16(staticTranslations.size());
  header.numStaticRotations = uint16(staticRotations.size());
  header.numStaticScales = uint16(staticScales.size());
  header.numStaticFloats = uint16(staticFloats.size());
  header.numDynamicTranslations = uint16(dynamicTranslations.size());
  header.numDynamicRotations = uint16(dynamicRotations.size());
  header.numDynamicScales = uint16(dynamicScales.size());
  header.numDynamicFloats = uint16(dynamicFloats.size());

  uint32 headerSize = 40 + input.numBones + input.numFloats;
  headerSize = alignTo(headerSize, 2);
  header.staticElementsOffset = uint16(headerSize);
  headerSize += uint32(staticCount * 2);
  headerSize = alignTo(headerSize, 2);
  header.dynamicElementsOffset = uint16(headerSize);
  headerSize += uint32(dynamicCount * 2);
  headerSize = alignTo(headerSize, 16);
  header.staticValuesOffset = uint16(headerSize);
  headerSize += uint32((staticTranslations.size() + staticScales.size()) * 4 +
                       staticRotations.size() * 6);
  headerSize = alignTo(headerSize, 16);
  headerSize += uint32(staticFloats.size() * 4);
  headerSize = alignTo(headerSize, 16);
  header.dynamicRangeMinimumsOffset = uint16(headerSize);
  headerSize +=
      uint32((dynamicTranslations.size() + dynamicScales.size()) * 4);
  headerSize = alignTo(headerSize, 16);
  headerSize += uint32(dynamicFloats.size() * 4);
  headerSize = alignTo(headerSize, 16);
  header.dynamicRangeSpansOffset = uint16(headerSize);
  headerSize +=
      uint32((dynamicTranslations.size() + dynamicScales.size()) * 4);
  headerSize = alignTo(headerSize, 16);
  headerSize += uint32(dynamicFloats.size() * 4);
  headerSize = alignTo(headerSize, 16);

  const uint32 frameValues = uint32(dynamicTranslations.size() +
                                    dynamicScales.size() +
                                    dynamicRotations.size() * 3 +
                                    dynamicFloats.size());
  const uint32 frameSize = alignTo(frameValues * 2, 16);
  if (headerSize > 0xffff || frameSize > 0xffff) {
    return false;
  }
  header.headerSize = uint16(headerSize);
  header.frameSize = uint16(frameSize);

  output.reserve(headerSize + frameSize * input.numFrames + 64);
  appendHeader(output, header);

  std::vector<uint8> weights(input.numBones + input.numFloats);
  for (uint32 track = 0; track < input.numTransformTracks; track++) {
    const uint32 bone = input.transformTrackToBoneIndices
                            ? input.transformTrackToBoneIndices[track]
                            : track;
    weights[bone] = 0xff;
  }
  for (uint32 track = 0; track < input.numFloatTracks; track++) {
    const uint32 slot = input.floatTrackToFloatSlotIndices
                            ? input.floatTrackToFloatSlotIndices[track]
                            : track;
    weights[input.numBones + slot] = 0xff;
  }
  for (uint8 weight : weights) {
    appendU8(output, weight);
  }

  appendElements(output, staticTranslations);
  appendElements(output, staticScales);
  appendElements(output, staticRotations);
  appendElements(output, staticFloats);
  appendElements(output, dynamicTranslations);
  appendElements(output, dynamicScales);
  appendElements(output, dynamicRotations);
  appendElements(output, dynamicFloats);

  align(output, 16);
  for (const auto &element : staticTranslations) {
    appendF32(output, element.value);
  }
  for (const auto &element : staticScales) {
    appendF32(output, element.value);
  }
  for (const auto &element : staticRotations) {
    packQuaternion(output, element.value);
  }
  align(output, 16);
  for (const auto &element : staticFloats) {
    appendF32(output, element.value);
  }

  align(output, 16);
  for (const auto &element : dynamicTranslations) {
    appendF32(output, element.range.minimum);
  }
  for (const auto &element : dynamicScales) {
    appendF32(output, element.range.minimum);
  }
  align(output, 16);
  for (const auto &element : dynamicFloats) {
    appendF32(output, element.range.minimum);
  }

  align(output, 16);
  for (const auto &element : dynamicTranslations) {
    appendF32(output, element.range.span / 65535.0f);
  }
  for (const auto &element : dynamicScales) {
    appendF32(output, element.range.span / 65535.0f);
  }
  align(output, 16);
  for (const auto &element : dynamicFloats) {
    appendF32(output, element.range.span / 65535.0f);
  }
  align(output, 16);

  for (uint32 frame = 0; frame < input.numFrames; frame++) {
    const uint32 frameStart = uint32(output.size());
    for (const auto &element : dynamicTranslations) {
      const uint32 track = element.sourceElement / 12;
      const uint32 component = element.sourceElement % 12;
      appendU16(output,
                quantize(transformElement(
                             input.transforms[frame * input.numTransformTracks +
                                              track],
                             component),
                         element.range));
    }
    for (const auto &element : dynamicScales) {
      const uint32 track = element.sourceElement / 12;
      const uint32 component = element.sourceElement % 12;
      appendU16(output,
                quantize(transformElement(
                             input.transforms[frame * input.numTransformTracks +
                                              track],
                             component),
                         element.range));
    }
    for (const auto &element : dynamicRotations) {
      const hkQTransform &sample =
          input.transforms[frame * input.numTransformTracks +
                           element.sourceTrack / 3];
      packQuaternion(output, sample.rotation);
    }
    for (const auto &element : dynamicFloats) {
      appendU16(output,
                quantize(input.floats[frame * input.numFloatTracks +
                                      element.sourceElement],
                         element.range));
    }
    output.resize(frameStart + frameSize);
  }

  align(output, 64);
  return true;
}
