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

#include "internal/hka_spline_compressor.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace {

struct Scalar4 {
  double v[4]{};

  static Scalar4 Translation(const hkQTransform &tm) {
    return {{tm.translation.X, tm.translation.Y, tm.translation.Z, 0.0}};
  }

  static Scalar4 Rotation(const hkQTransform &tm) {
    return {{tm.rotation.X, tm.rotation.Y, tm.rotation.Z, tm.rotation.W}};
  }

  static Scalar4 Scale(const hkQTransform &tm) {
    return {{tm.scale.X, tm.scale.Y, tm.scale.Z, 1.0}};
  }
};

constexpr double kEpsilonVector = 1.0e-3;
constexpr double kEpsilonFloat = 1.0e-4;
constexpr double kQuatLimit = 0.7071067811865475244;
constexpr double kQuatPackScale40 = 2894.8951621777255648970568184573;
constexpr double kReducePositionError = 5.0e-4;
constexpr double kReduceScaleError = 5.0e-4;
constexpr double kReduceFloatError = 1.0e-4;
constexpr double kReduceQuaternionError = 1.0e-4;

template <class T> T clampValue(T value, T minValue, T maxValue) {
  return std::max(minValue, std::min(maxValue, value));
}

Scalar4 lerpScalar4(const Scalar4 &a, const Scalar4 &b, double t) {
  Scalar4 result;
  for (int32 c = 0; c < 4; c++) {
    result.v[c] = a.v[c] + (b.v[c] - a.v[c]) * t;
  }
  return result;
}

uint32 alignTo(uint32 value, uint32 alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

void appendU8(std::vector<char> &data, uint8 value) {
  data.push_back(static_cast<char>(value));
}

void appendU16(std::vector<char> &data, uint16 value) {
  data.push_back(static_cast<char>(value & 0xff));
  data.push_back(static_cast<char>((value >> 8) & 0xff));
}

void appendF32(std::vector<char> &data, float value) {
  uint32 raw = 0;
  static_assert(sizeof(raw) == sizeof(value));
  std::memcpy(&raw, &value, sizeof(raw));
  appendU8(data, static_cast<uint8>(raw & 0xff));
  appendU8(data, static_cast<uint8>((raw >> 8) & 0xff));
  appendU8(data, static_cast<uint8>((raw >> 16) & 0xff));
  appendU8(data, static_cast<uint8>((raw >> 24) & 0xff));
}

void appendPadding(std::vector<char> &data, uint32 alignment) {
  const uint32 pad = static_cast<uint32>(data.size()) & (alignment - 1);
  if (!pad) {
    return;
  }

  const uint32 need = alignment - pad;
  for (uint32 i = 0; i < need; i++) {
    appendU8(data, 0);
  }
}

uint16 quantize16(double value, double minValue, double maxValue) {
  if (maxValue <= minValue) {
    return 0;
  }

  const double t = clampValue((value - minValue) / (maxValue - minValue), 0.0,
                              1.0);
  const auto q = static_cast<int32>(std::floor(t * 65535.0 + 0.5));
  return static_cast<uint16>(clampValue<int32>(q, 0, 65535));
}

uint8 quantize8(double value, double minValue, double maxValue) {
  if (maxValue <= minValue) {
    return 0;
  }

  const double t = clampValue((value - minValue) / (maxValue - minValue), 0.0,
                              1.0);
  const auto q = static_cast<int32>(std::floor(t * 255.0 + 0.5));
  return static_cast<uint8>(clampValue<int32>(q, 0, 255));
}

void normalizeQuaternion(Scalar4 &q) {
  for (double &component : q.v) {
    if (!std::isfinite(component)) {
      q = {{0.0, 0.0, 0.0, 1.0}};
      return;
    }
  }

  double len = 0.0;
  for (double component : q.v) {
    len += component * component;
  }

  if (len <= std::numeric_limits<double>::epsilon()) {
    q = {{0.0, 0.0, 0.0, 1.0}};
    return;
  }

  const double invLen = 1.0 / std::sqrt(len);
  for (double &component : q.v) {
    component *= invLen;
  }
}

double quatDot(const Scalar4 &a, const Scalar4 &b) {
  return a.v[0] * b.v[0] + a.v[1] * b.v[1] + a.v[2] * b.v[2] +
         a.v[3] * b.v[3];
}

void keepQuaternionHemisphere(std::vector<Scalar4> &keys) {
  if (keys.empty()) {
    return;
  }

  normalizeQuaternion(keys[0]);
  if (keys[0].v[3] < 0.0) {
    for (double &component : keys[0].v) {
      component = -component;
    }
  }

  for (size_t i = 1; i < keys.size(); i++) {
    normalizeQuaternion(keys[i]);
    if (quatDot(keys[i - 1], keys[i]) < 0.0) {
      for (double &component : keys[i].v) {
        component = -component;
      }
    }
  }
}

uint8 frameToKnot(uint32 frame) {
  return static_cast<uint8>(std::min<uint32>(frame, 255));
}

void buildClampedKnots(const std::vector<uint8> &times, int32 degree,
                       std::vector<uint8> &knots) {
  knots.clear();
  if (times.empty()) {
    return;
  }

  int32 n = static_cast<int32>(times.size()) - 1;
  degree = clampValue<int32>(degree, 0, 3);
  if (degree > n) {
    degree = n;
  }

  for (int32 i = 0; i < degree + 1; i++) {
    knots.push_back(times.front());
  }

  const int32 interiorCount = n - degree;
  for (int32 k = 1; k <= interiorCount; k++) {
    double value = 0.0;
    const int32 averageCount = std::max<int32>(1, degree);
    for (int32 i = 0; i < averageCount; i++) {
      value += times[static_cast<size_t>(k + i)];
    }
    value /= static_cast<double>(averageCount);
    knots.push_back(static_cast<uint8>(
        clampValue<int32>(static_cast<int32>(std::floor(value + 0.5)), 0,
                          255)));
  }

  for (int32 i = 0; i < degree + 1; i++) {
    knots.push_back(times.back());
  }
}

void writeCurveHeader(std::vector<char> &data, int32 n, int32 degree,
                      const std::vector<uint8> &knots) {
  appendU16(data, static_cast<uint16>(n));
  appendU8(data, static_cast<uint8>(degree));
  for (uint8 knot : knots) {
    appendU8(data, knot);
  }
}

template <class T>
void reduceKeysByStep(const std::vector<T> &input, uint32 step,
                      std::vector<T> &output, std::vector<uint8> &times) {
  output.clear();
  times.clear();
  const uint32 count = static_cast<uint32>(input.size());
  if (!count) {
    return;
  }

  if (step <= 1 || count <= 2) {
    output = input;
    times.reserve(count);
    for (uint32 i = 0; i < count; i++) {
      times.push_back(frameToKnot(i));
    }
    return;
  }

  output.push_back(input.front());
  times.push_back(0);
  for (uint32 i = step; i + 1 < count; i += step) {
    output.push_back(input[i]);
    times.push_back(frameToKnot(i));
  }
  output.push_back(input.back());
  times.push_back(frameToKnot(count - 1));
}

template <class T, class ErrorFunc>
void simplifyLinearRange(const std::vector<T> &input, size_t begin, size_t end,
                         double tolerance, std::vector<uint8> &keep,
                         ErrorFunc &&errorFunc) {
  if (end <= begin + 1) {
    return;
  }

  double worstError = 0.0;
  size_t worstIndex = begin;

  for (size_t i = begin + 1; i < end; i++) {
    const double t = static_cast<double>(i - begin) /
                     static_cast<double>(end - begin);
    const double error = errorFunc(input[begin], input[end], input[i], t);
    if (error > worstError) {
      worstError = error;
      worstIndex = i;
    }
  }

  if (worstError <= tolerance) {
    return;
  }

  keep[worstIndex] = 1;
  simplifyLinearRange(input, begin, worstIndex, tolerance, keep, errorFunc);
  simplifyLinearRange(input, worstIndex, end, tolerance, keep, errorFunc);
}

template <class T, class ErrorFunc>
void reduceKeysAdaptive(const std::vector<T> &input, double tolerance,
                        std::vector<T> &output, std::vector<uint8> &times,
                        ErrorFunc &&errorFunc) {
  output.clear();
  times.clear();

  const size_t count = input.size();
  if (!count) {
    return;
  }

  if (count <= 2) {
    output = input;
    times.reserve(count);
    for (size_t i = 0; i < count; i++) {
      times.push_back(frameToKnot(static_cast<uint32>(i)));
    }
    return;
  }

  std::vector<uint8> keep(count);
  keep.front() = 1;
  keep.back() = 1;
  simplifyLinearRange(input, 0, count - 1, tolerance, keep,
                      std::forward<ErrorFunc>(errorFunc));

  for (size_t i = 0; i < count; i++) {
    if (!keep[i]) {
      continue;
    }
    output.push_back(input[i]);
    times.push_back(frameToKnot(static_cast<uint32>(i)));
  }
}

double vectorComponentError(const Scalar4 &expected, const Scalar4 &actual,
                            uint8 mask) {
  double error = 0.0;
  for (int32 c = 0; c < 3; c++) {
    const uint8 dynamicBit = static_cast<uint8>(1 << (c + 4));
    if (!(mask & dynamicBit)) {
      continue;
    }
    error = std::max(error, std::fabs(expected.v[c] - actual.v[c]));
  }
  return error;
}

void reduceVectorKeys(const std::vector<Scalar4> &input, uint32 step,
                      uint8 mask, double tolerance,
                      std::vector<Scalar4> &output,
                      std::vector<uint8> &times) {
  if ((mask & 0x70) == 0) {
    output = input;
    times.clear();
    return;
  }

  if (step > 1) {
    reduceKeysByStep(input, step, output, times);
    return;
  }

  reduceKeysAdaptive(
      input, tolerance, output, times,
      [&](const Scalar4 &start, const Scalar4 &end, const Scalar4 &sample,
          double t) {
        return vectorComponentError(lerpScalar4(start, end, t), sample, mask);
      });
}

double quaternionError(const Scalar4 &expected, const Scalar4 &actual) {
  Scalar4 e = expected;
  Scalar4 a = actual;
  normalizeQuaternion(e);
  normalizeQuaternion(a);
  return 1.0 - std::fabs(quatDot(e, a));
}

void reduceQuaternionKeys(const std::vector<Scalar4> &input, uint32 step,
                          uint8 mask, std::vector<Scalar4> &output,
                          std::vector<uint8> &times) {
  if ((mask & 0xf0) == 0) {
    output = input;
    times.clear();
    return;
  }

  std::vector<Scalar4> working = input;
  keepQuaternionHemisphere(working);

  if (step > 1) {
    reduceKeysByStep(working, step, output, times);
    return;
  }

  reduceKeysAdaptive(
      working, kReduceQuaternionError, output, times,
      [&](const Scalar4 &start, const Scalar4 &end, const Scalar4 &sample,
          double t) {
        return quaternionError(lerpScalar4(start, end, t), sample);
      });
}

void reduceFloatKeys(const std::vector<double> &input, uint32 step,
                     uint8 mask, std::vector<double> &output,
                     std::vector<uint8> &times) {
  if ((mask & 0x10) == 0) {
    output = input;
    times.clear();
    return;
  }

  if (step > 1) {
    reduceKeysByStep(input, step, output, times);
    return;
  }

  reduceKeysAdaptive(
      input, kReduceFloatError, output, times,
      [](double start, double end, double sample, double t) {
        const double expected = start + (end - start) * t;
        return std::fabs(expected - sample);
      });
}

uint8 analyzeStaticMask(const Scalar4 &mean, const Scalar4 &minp,
                        const Scalar4 &maxp, const Scalar4 &identity,
                        int32 components, double eps) {
  uint8 mask = 0;

  for (int32 c = 0; c < components; c++) {
    if (std::fabs(mean.v[c] - minp.v[c]) <= eps &&
        std::fabs(mean.v[c] - maxp.v[c]) <= eps) {
      if (std::fabs(mean.v[c] - identity.v[c]) > eps) {
        mask = static_cast<uint8>(mask | (1 << c));
      }
    } else {
      mask = static_cast<uint8>(mask | (1 << (c + 4)));
    }
  }

  return mask;
}

uint8 analyzeVectorMask(const std::vector<Scalar4> &keys,
                        const Scalar4 &identity) {
  if (keys.empty()) {
    return 0;
  }

  Scalar4 minp = keys.front();
  Scalar4 maxp = keys.front();
  Scalar4 mean{};

  for (const Scalar4 &value : keys) {
    for (int32 c = 0; c < 3; c++) {
      minp.v[c] = std::min(minp.v[c], value.v[c]);
      maxp.v[c] = std::max(maxp.v[c], value.v[c]);
      mean.v[c] += value.v[c];
    }
  }

  for (int32 c = 0; c < 3; c++) {
    mean.v[c] /= static_cast<double>(keys.size());
  }

  return analyzeStaticMask(mean, minp, maxp, identity, 3, kEpsilonVector);
}

uint8 analyzeScaleMask(const std::vector<Scalar4> &keys) {
  if (keys.empty()) {
    return 0;
  }

  bool uniform = true;
  for (const Scalar4 &value : keys) {
    if (std::fabs(value.v[0] - value.v[1]) > kEpsilonVector ||
        std::fabs(value.v[0] - value.v[2]) > kEpsilonVector) {
      uniform = false;
      break;
    }
  }

  if (!uniform) {
    return analyzeVectorMask(keys, {{1.0, 1.0, 1.0, 1.0}});
  }

  double minValue = keys.front().v[0];
  double maxValue = keys.front().v[0];
  double meanValue = 0.0;

  for (const Scalar4 &value : keys) {
    minValue = std::min(minValue, value.v[0]);
    maxValue = std::max(maxValue, value.v[0]);
    meanValue += value.v[0];
  }

  meanValue /= static_cast<double>(keys.size());

  return analyzeStaticMask({{meanValue, 1.0, 1.0, 1.0}},
                           {{minValue, 1.0, 1.0, 1.0}},
                           {{maxValue, 1.0, 1.0, 1.0}},
                           {{1.0, 1.0, 1.0, 1.0}}, 1,
                           kEpsilonVector);
}

uint8 analyzeQuaternionMask(std::vector<Scalar4> keys) {
  if (keys.empty()) {
    return 0;
  }

  keepQuaternionHemisphere(keys);

  Scalar4 mean{};
  Scalar4 minp = keys.front();
  Scalar4 maxp = keys.front();

  for (const Scalar4 &value : keys) {
    for (int32 c = 0; c < 4; c++) {
      mean.v[c] += value.v[c];
      minp.v[c] = std::min(minp.v[c], value.v[c]);
      maxp.v[c] = std::max(maxp.v[c], value.v[c]);
    }
  }

  normalizeQuaternion(mean);
  return analyzeStaticMask(mean, minp, maxp, {{0.0, 0.0, 0.0, 1.0}}, 4,
                           kEpsilonVector);
}

void writeVectorCurve(std::vector<char> &data,
                      const std::vector<Scalar4> &keys,
                      const std::vector<uint8> &times, uint8 mask) {
  if (keys.empty() || mask == 0) {
    return;
  }

  const uint8 dynamicMask = static_cast<uint8>(mask & 0x70);

  if (dynamicMask) {
    const int32 n = static_cast<int32>(keys.size()) - 1;
    const int32 degree = std::min<int32>(3, n);
    std::vector<uint8> knots;
    buildClampedKnots(times, degree, knots);
    writeCurveHeader(data, n, degree, knots);
  }

  appendPadding(data, 4);

  double minValues[3]{};
  double maxValues[3]{};

  for (int32 c = 0; c < 3; c++) {
    if (dynamicMask & (1 << (c + 4))) {
      minValues[c] = keys.front().v[c];
      maxValues[c] = keys.front().v[c];

      for (const Scalar4 &key : keys) {
        minValues[c] = std::min(minValues[c], key.v[c]);
        maxValues[c] = std::max(maxValues[c], key.v[c]);
      }
    }
  }

  for (int32 c = 0; c < 3; c++) {
    const uint8 staticBit = static_cast<uint8>(1 << c);
    const uint8 dynamicBit = static_cast<uint8>(1 << (c + 4));

    if (mask & staticBit) {
      appendF32(data, static_cast<float>(keys.front().v[c]));
    } else if (mask & dynamicBit) {
      appendF32(data, static_cast<float>(minValues[c]));
      appendF32(data, static_cast<float>(maxValues[c]));
    }
  }

  if (!dynamicMask) {
    return;
  }

  appendPadding(data, 2);
  for (const Scalar4 &key : keys) {
    for (int32 c = 0; c < 3; c++) {
      const uint8 dynamicBit = static_cast<uint8>(1 << (c + 4));
      if (mask & dynamicBit) {
        appendU16(data, quantize16(key.v[c], minValues[c], maxValues[c]));
      }
    }
  }
  appendPadding(data, 4);
}

void packQuaternionThreeComp40(const Scalar4 &input, uint8 out[5]) {
  Scalar4 q = input;
  normalizeQuaternion(q);

  int32 largest = 0;
  double largestAbs = std::fabs(q.v[0]);
  for (int32 i = 1; i < 4; i++) {
    const double current = std::fabs(q.v[i]);
    if (current > largestAbs) {
      largest = i;
      largestAbs = current;
    }
  }

  int32 packed[3]{};
  int32 cursor = 0;
  for (int32 i = 0; i < 4; i++) {
    if (i == largest) {
      continue;
    }

    const double component = clampValue(q.v[i], -kQuatLimit, kQuatLimit);
    const int32 quantized =
        static_cast<int32>(component * kQuatPackScale40) + 2047;
    packed[cursor++] = clampValue<int32>(quantized, 0, 4095);
  }

  const int32 sign = q.v[largest] < 0.0 ? 1 : 0;
  out[0] = static_cast<uint8>(packed[0] & 0xff);
  out[1] = static_cast<uint8>(((packed[0] >> 8) & 0x0f) |
                              ((packed[1] & 0x0f) << 4));
  out[2] = static_cast<uint8>((packed[1] >> 4) & 0xff);
  out[3] = static_cast<uint8>(packed[2] & 0xff);
  out[4] = static_cast<uint8>(((packed[2] >> 8) & 0x0f) |
                              ((largest & 0x03) << 4) | (sign << 6));
}

void writeQuaternionCurve(std::vector<char> &data,
                          std::vector<Scalar4> keys,
                          const std::vector<uint8> &times, uint8 mask) {
  if (keys.empty() || mask == 0) {
    return;
  }

  keepQuaternionHemisphere(keys);

  if ((mask & 0xf0) == 0) {
    Scalar4 mean{};
    for (const Scalar4 &key : keys) {
      for (int32 c = 0; c < 4; c++) {
        mean.v[c] += key.v[c];
      }
    }
    normalizeQuaternion(mean);

    uint8 packed[5]{};
    packQuaternionThreeComp40(mean, packed);
    for (uint8 value : packed) {
      appendU8(data, value);
    }
    return;
  }

  const int32 n = static_cast<int32>(keys.size()) - 1;
  const int32 degree = std::min<int32>(3, n);
  std::vector<uint8> knots;
  buildClampedKnots(times, degree, knots);
  writeCurveHeader(data, n, degree, knots);

  for (const Scalar4 &key : keys) {
    uint8 packed[5]{};
    packQuaternionThreeComp40(key, packed);
    for (uint8 value : packed) {
      appendU8(data, value);
    }
  }

  appendPadding(data, 4);
}

uint8 analyzeFloatMask(const std::vector<double> &keys) {
  if (keys.empty()) {
    return 0;
  }

  double minValue = keys.front();
  double maxValue = keys.front();
  for (double value : keys) {
    minValue = std::min(minValue, value);
    maxValue = std::max(maxValue, value);
  }

  if (std::fabs(minValue) <= kEpsilonFloat &&
      std::fabs(maxValue) <= kEpsilonFloat) {
    return 0;
  }

  if (std::fabs(maxValue - minValue) <= kEpsilonFloat) {
    return 0x01;
  }

  return 0x10;
}

void writeFloatCurve(std::vector<char> &data, const std::vector<double> &keys,
                     const std::vector<uint8> &times, uint8 mask,
                     bool use16Bit) {
  if (keys.empty() || mask == 0) {
    return;
  }

  if (mask & 0x10) {
    const int32 n = static_cast<int32>(keys.size()) - 1;
    const int32 degree = std::min<int32>(3, n);
    std::vector<uint8> knots;
    buildClampedKnots(times, degree, knots);
    writeCurveHeader(data, n, degree, knots);
  }

  appendPadding(data, 4);

  if (mask & 0x01) {
    appendF32(data, static_cast<float>(keys.front()));
    return;
  }

  double minValue = keys.front();
  double maxValue = keys.front();
  for (double value : keys) {
    minValue = std::min(minValue, value);
    maxValue = std::max(maxValue, value);
  }

  appendF32(data, static_cast<float>(minValue));
  appendF32(data, static_cast<float>(maxValue));

  if (use16Bit) {
    appendPadding(data, 2);
    for (double value : keys) {
      appendU16(data, quantize16(value, minValue, maxValue));
    }
  } else {
    for (double value : keys) {
      appendU8(data, quantize8(value, minValue, maxValue));
    }
  }

  appendPadding(data, 4);
}

void setError(std::string *error, std::string message) {
  if (error) {
    *error = std::move(message);
  }
}

} // namespace

bool hkaCompressSplineAnimation(const hkaSplineCompressionInput &input,
                                hkaSplineCompressedData &output,
                                std::string *error) {
  output = {};

  if (!input.transforms) {
    setError(error, "Spline compression needs transform samples");
    return false;
  }

  if (!input.numFrames || !input.numTransformTracks) {
    setError(error, "Spline compression needs at least one frame and track");
    return false;
  }

  if (input.numFloatTracks && !input.floats) {
    setError(error, "Spline compression got float tracks without samples");
    return false;
  }

  const uint32 maxFramesPerBlock =
      std::max<uint32>(2, input.settings.maxFramesPerBlock);
  const uint32 blockStride = maxFramesPerBlock - 1;
  const uint32 numBlocks =
      std::max<uint32>(1, (input.numFrames - 1 + blockStride - 1) /
                              blockStride);

  output.numFrames = input.numFrames;
  output.numBlocks = numBlocks;
  output.maxFramesPerBlock = maxFramesPerBlock;
  output.maskAndQuantizationSize =
      alignTo(4 * input.numTransformTracks + input.numFloatTracks, 4);

  if (input.duration > 0.0f && input.numFrames > 1) {
    output.frameDuration =
        input.duration / static_cast<float>(input.numFrames - 1);
  } else {
    output.frameDuration = 1.0f / 60.0f;
  }

  output.blockDuration =
      output.frameDuration * static_cast<float>(maxFramesPerBlock - 1);
  output.blockInverseDuration =
      output.blockDuration > 0.0f ? 1.0f / output.blockDuration : 0.0f;

  output.blockOffsets.reserve(numBlocks);
  output.floatBlockOffsets.reserve(numBlocks);

  const uint8 packedTransformQuantization =
      static_cast<uint8>((1 << 0) | (1 << 2) | (1 << 6));
  const bool use16BitFloats = input.settings.use16BitFloats;
  const uint32 keyStep = std::max<uint32>(1, input.settings.keyStep);
  uint32 minSafeSize = 0;

  for (uint32 block = 0; block < numBlocks; block++) {
    const uint32 startFrame = block * blockStride;
    const uint32 endFrame =
        std::min(startFrame + blockStride, input.numFrames - 1);
    const uint32 controlCount = endFrame - startFrame + 1;

    std::vector<uint8> translationMasks(input.numTransformTracks);
    std::vector<uint8> rotationMasks(input.numTransformTracks);
    std::vector<uint8> scaleMasks(input.numTransformTracks);
    std::vector<uint8> floatMasks(input.numFloatTracks);

    for (uint32 track = 0; track < input.numTransformTracks; track++) {
      std::vector<Scalar4> translations;
      std::vector<Scalar4> rotations;
      std::vector<Scalar4> scales;
      translations.reserve(controlCount);
      rotations.reserve(controlCount);
      scales.reserve(controlCount);

      for (uint32 i = 0; i < controlCount; i++) {
        const uint32 frame = startFrame + i;
        const hkQTransform &sample =
            input.transforms[frame * input.numTransformTracks + track];
        translations.push_back(Scalar4::Translation(sample));
        rotations.push_back(Scalar4::Rotation(sample));
        scales.push_back(input.settings.forceIdentityScale
                             ? Scalar4{{1.0, 1.0, 1.0, 1.0}}
                             : Scalar4::Scale(sample));
      }

      translationMasks[track] =
          analyzeVectorMask(translations, {{0.0, 0.0, 0.0, 0.0}});
      rotationMasks[track] = analyzeQuaternionMask(rotations);
      scaleMasks[track] = analyzeScaleMask(scales);
    }

    for (uint32 track = 0; track < input.numFloatTracks; track++) {
      std::vector<double> floats;
      floats.reserve(controlCount);

      for (uint32 i = 0; i < controlCount; i++) {
        const uint32 frame = startFrame + i;
        floats.push_back(input.floats[frame * input.numFloatTracks + track]);
      }

      const uint8 mask = analyzeFloatMask(floats);
      floatMasks[track] =
          static_cast<uint8>(mask | (use16BitFloats ? (1 << 1) : 0));
    }

    const uint32 blockStart = static_cast<uint32>(output.dataBuffer.size());
    output.blockOffsets.push_back(blockStart);

    for (uint32 track = 0; track < input.numTransformTracks; track++) {
      appendU8(output.dataBuffer, packedTransformQuantization);
      appendU8(output.dataBuffer, translationMasks[track]);
      appendU8(output.dataBuffer, rotationMasks[track]);
      appendU8(output.dataBuffer, scaleMasks[track]);
    }

    for (uint8 mask : floatMasks) {
      appendU8(output.dataBuffer, mask);
    }

    while (output.dataBuffer.size() <
           static_cast<size_t>(blockStart + output.maskAndQuantizationSize)) {
      appendU8(output.dataBuffer, 0);
    }

    for (uint32 track = 0; track < input.numTransformTracks; track++) {
      std::vector<Scalar4> translations;
      std::vector<Scalar4> rotations;
      std::vector<Scalar4> scales;
      translations.reserve(controlCount);
      rotations.reserve(controlCount);
      scales.reserve(controlCount);

      for (uint32 i = 0; i < controlCount; i++) {
        const uint32 frame = startFrame + i;
        const hkQTransform &sample =
            input.transforms[frame * input.numTransformTracks + track];
        translations.push_back(Scalar4::Translation(sample));
        rotations.push_back(Scalar4::Rotation(sample));
        scales.push_back(input.settings.forceIdentityScale
                             ? Scalar4{{1.0, 1.0, 1.0, 1.0}}
                             : Scalar4::Scale(sample));
      }

      std::vector<Scalar4> reducedTranslations;
      std::vector<Scalar4> reducedRotations;
      std::vector<Scalar4> reducedScales;
      std::vector<uint8> translationTimes;
      std::vector<uint8> rotationTimes;
      std::vector<uint8> scaleTimes;
      reduceVectorKeys(translations, keyStep, translationMasks[track],
                       kReducePositionError, reducedTranslations,
                       translationTimes);
      reduceQuaternionKeys(rotations, keyStep, rotationMasks[track],
                           reducedRotations, rotationTimes);
      reduceVectorKeys(scales, keyStep, scaleMasks[track], kReduceScaleError,
                       reducedScales, scaleTimes);

      writeVectorCurve(output.dataBuffer, reducedTranslations,
                       translationTimes, translationMasks[track]);
      writeQuaternionCurve(output.dataBuffer, reducedRotations,
                           rotationTimes, rotationMasks[track]);
      appendPadding(output.dataBuffer, 4);
      minSafeSize =
          std::max<uint32>(minSafeSize,
                           static_cast<uint32>(output.dataBuffer.size()) + 3);
      writeVectorCurve(output.dataBuffer, reducedScales, scaleTimes,
                       scaleMasks[track]);
    }

    output.floatBlockOffsets.push_back(
        static_cast<uint32>(output.dataBuffer.size()) - blockStart);

    for (uint32 track = 0; track < input.numFloatTracks; track++) {
      std::vector<double> floats;
      floats.reserve(controlCount);

      for (uint32 i = 0; i < controlCount; i++) {
        const uint32 frame = startFrame + i;
        floats.push_back(input.floats[frame * input.numFloatTracks + track]);
      }

      std::vector<double> reducedFloats;
      std::vector<uint8> floatTimes;
      const uint8 floatMask = static_cast<uint8>(floatMasks[track] & ~0x06);
      reduceFloatKeys(floats, keyStep, floatMask, reducedFloats, floatTimes);
      writeFloatCurve(output.dataBuffer, reducedFloats, floatTimes, floatMask,
                      use16BitFloats);
    }

    while (output.dataBuffer.size() < minSafeSize) {
      appendU8(output.dataBuffer, 0);
    }
    appendPadding(output.dataBuffer, 16);
  }

  return true;
}
