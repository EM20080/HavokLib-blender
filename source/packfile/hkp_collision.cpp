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

#include "internal/hkp_collision.hpp"
#include "../format_old.hpp"
#include "../format_new.hpp"
#include "base.hpp"
#include "hklib/hk_packfile.hpp"
#include "spike/type/pointer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace {
auto MultiplySize(size_t left, size_t right, size_t &out) {
  if (left == 0 || right == 0) {
    out = 0;
    return true;
  }

  if (left > (std::numeric_limits<size_t>::max)() / right) {
    return false;
  }

  out = left * right;
  return true;
}

uint16 FloatToHalfBits(float value) {
  uint32 bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));

  const uint32 sign = (bits >> 16) & 0x8000u;
  int32 exponent = static_cast<int32>((bits >> 23) & 0xffu) - 127 + 15;
  uint32 mantissa = bits & 0x7fffffu;

  if (exponent <= 0) {
    if (exponent < -10) {
      return static_cast<uint16>(sign);
    }

    mantissa |= 0x800000u;
    const uint32 shift = static_cast<uint32>(14 - exponent);
    uint32 halfMantissa = mantissa >> shift;
    if ((mantissa >> (shift - 1)) & 1u) {
      halfMantissa++;
    }

    return static_cast<uint16>(sign | halfMantissa);
  }

  if (exponent >= 31) {
    return static_cast<uint16>(sign | 0x7c00u | (mantissa ? 0x0200u : 0u));
  }

  uint32 half = sign | (static_cast<uint32>(exponent) << 10) | (mantissa >> 13);
  if (mantissa & 0x1000u) {
    half++;
  }

  return static_cast<uint16>(half);
}

float HalfBitsToFloat(uint16 value) {
  const uint32 sign = (static_cast<uint32>(value & 0x8000u)) << 16;
  int32 exponent = static_cast<int32>((value >> 10) & 0x1fu);
  uint32 mantissa = value & 0x03ffu;
  uint32 bits = 0;

  if (exponent == 0) {
    if (mantissa == 0) {
      bits = sign;
    } else {
      exponent = 1;
      while ((mantissa & 0x0400u) == 0) {
        mantissa <<= 1;
        exponent--;
      }
      mantissa &= 0x03ffu;
      bits = sign |
             (static_cast<uint32>(exponent + 127 - 15) << 23) |
             (mantissa << 13);
    }
  } else if (exponent == 31) {
    bits = sign | 0x7f800000u | (mantissa << 13);
  } else {
    bits = sign | (static_cast<uint32>(exponent + 127 - 15) << 23) |
           (mantissa << 13);
  }

  float output = 0.0f;
  std::memcpy(&output, &bits, sizeof(output));
  return output;
}

auto PointerCoveredByHeader(const IhkPackFile *header, const void *ptr,
                            size_t size) {
  if (!header || !ptr) {
    return false;
  }

  const char *p = static_cast<const char *>(ptr);

  if (const auto *oldHeader = dynamic_cast<const hkxHeader *>(header)) {
    for (const auto &section : oldHeader->sections) {
      const char *base = section.buffer.data();
      const size_t length = section.buffer.size();
      if (size <= length && p >= base &&
          static_cast<size_t>(p - base) <= length - size) {
        return true;
      }
    }
  }

  if (const auto *newHeader = dynamic_cast<const hkxNewHeader *>(header)) {
    const char *base = newHeader->dataBuffer.data();
    const size_t length = newHeader->dataBuffer.size();
    if (size <= length && p >= base &&
        static_cast<size_t>(p - base) <= length - size) {
      return true;
    }
  }

  return false;
}
uint16 ByteSwap16(uint16 value) {
  return static_cast<uint16>(((value & 0x00FFu) << 8) |
                             ((value & 0xFF00u) >> 8));
}

uint32 ByteSwap32(uint32 value) {
  return ((value & 0x000000FFu) << 24) | ((value & 0x0000FF00u) << 8) |
         ((value & 0x00FF0000u) >> 8) | ((value & 0xFF000000u) >> 24);
}

uint64 ByteSwap64(uint64 value) {
  return ((value & 0x00000000000000FFull) << 56) |
         ((value & 0x000000000000FF00ull) << 40) |
         ((value & 0x0000000000FF0000ull) << 24) |
         ((value & 0x00000000FF000000ull) << 8) |
         ((value & 0x000000FF00000000ull) >> 8) |
         ((value & 0x0000FF0000000000ull) >> 24) |
         ((value & 0x00FF000000000000ull) >> 40) |
         ((value & 0xFF00000000000000ull) >> 56);
}

float ByteSwapFloat(float value) {
  uint32 bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  bits = ByteSwap32(bits);
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

float FloatFromBits(uint32 bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

Vector4A16 ByteSwapVector4(Vector4A16 value) {
  value._arr[0] = ByteSwapFloat(value._arr[0]);
  value._arr[1] = ByteSwapFloat(value._arr[1]);
  value._arr[2] = ByteSwapFloat(value._arr[2]);
  value._arr[3] = ByteSwapFloat(value._arr[3]);
  return value;
}

es::Matrix44 ByteSwapMatrix44(es::Matrix44 value) {
  for (size_t row = 0; row < 4; row++) {
    value[row] = ByteSwapVector4(value[row]);
  }

  return value;
}

auto RequiresEndianSwap(const IhkPackFile *header) {
  const auto *oldHeader = dynamic_cast<const hkxHeader *>(header);
  return oldHeader && oldHeader->layout.littleEndian == 0;
}

auto ValueNeedsEndianSwap(const IhkVirtualClass *value) {
  const auto *virtualClass = dynamic_cast<const hkVirtualClass *>(value);
  return virtualClass && RequiresEndianSwap(virtualClass->header);
}

template <class C> C ReadValue(const char *data, size_t offset,
                               uint8 swapEndian = false) {
  if (!data) {
    return C{};
  }

  C value = *reinterpret_cast<const C *>(data + offset);
  if (swapEndian && sizeof(C) > 1) {
    if constexpr (std::is_same_v<C, uint16>) {
      value = ByteSwap16(value);
    } else if constexpr (std::is_same_v<C, uint32>) {
      value = ByteSwap32(value);
    } else if constexpr (std::is_same_v<C, uint64>) {
      value = ByteSwap64(value);
    } else if constexpr (std::is_same_v<C, int32>) {
      value = static_cast<int32>(ByteSwap32(static_cast<uint32>(value)));
    } else if constexpr (std::is_same_v<C, float>) {
      value = ByteSwapFloat(value);
    } else if constexpr (std::is_same_v<C, Vector4A16>) {
      value = ByteSwapVector4(value);
    } else if constexpr (std::is_same_v<C, es::Matrix44>) {
      value = ByteSwapMatrix44(value);
    }
  }

  return value;
}

template <class C>
const C *ReadPointer(const char *data, size_t offset, uint8 x64) {
  if (!data) {
    return nullptr;
  }

  if (x64) {
    return *reinterpret_cast<C *const *>(data + offset);
  }

  return *reinterpret_cast<const es::PointerX86<C> *>(data + offset);
}

struct ArrayView {
  const char *data{};
  uint32 count{};
};

struct ArrayViewWithMode {
  ArrayView view{};
  uint8 x64{};
  uint8 swapEndian{};
};

size_t PointerSize(uint8 x64) { return x64 ? 8u : 4u; }

size_t ArrayStorageSize(uint8 x64) { return PointerSize(x64) + 8u; }

size_t AlignOffset(size_t value, size_t alignment) {
  if (alignment == 0) {
    return value;
  }

  const size_t mask = alignment - 1;
  return (value + mask) & ~mask;
}

auto ArrayViewCoveredByHeader(const ArrayViewWithMode &view, size_t elementSize,
                              const IhkPackFile *header) {
  if (view.view.count == 0) {
    return true;
  }

  size_t totalSize = 0;
  if (!MultiplySize(static_cast<size_t>(view.view.count), elementSize,
                    totalSize)) {
    return false;
  }

  return PointerCoveredByHeader(header, view.view.data, totalSize);
}

auto ArrayViewReasonable(const ArrayView &view, uint32 countLimit) {
  if (view.count > countLimit) {
    return false;
  }

  if (view.count > 0 && !view.data) {
    return false;
  }

  return true;
}

ArrayView ReadArray(const char *data, size_t offset, uint8 x64,
                    uint8 swapEndian = false) {
  ArrayView output;
  output.data = ReadPointer<char>(data, offset, x64);
  output.count =
      ReadValue<uint32>(data, offset + (x64 ? static_cast<size_t>(8) : 4),
                        swapEndian);
  return output;
}

ArrayViewWithMode ReadArrayWithFallback(const char *data, size_t offset,
                                        uint8 x64, uint8 swapEndianHint,
                                        uint32 countLimit) {
  const ArrayViewWithMode candidates[] = {
      {ReadArray(data, offset, x64, swapEndianHint), x64, swapEndianHint},
      {ReadArray(data, offset, !x64, swapEndianHint), !x64, swapEndianHint},
      {ReadArray(data, offset, x64, !swapEndianHint), x64, !swapEndianHint},
      {ReadArray(data, offset, !x64, !swapEndianHint), !x64, !swapEndianHint},
  };

  for (const auto &candidate : candidates) {
    if (ArrayViewReasonable(candidate.view, countLimit)) {
      return candidate;
    }
  }

  return candidates[0];
}

const void *ReadPointerArrayItem(const ArrayView &arr, size_t id, uint8 x64) {
  if (!arr.data || id >= arr.count) {
    return nullptr;
  }

  if (x64) {
    return reinterpret_cast<const void *const *>(arr.data)[id];
  }

  return reinterpret_cast<const es::PointerX86<char> *>(arr.data)[id];
}

size_t RigidBodyCollidableOffset() { return 16; }

size_t RigidBodyNameOffset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 120;
  case HK550:
  default:
    return 112;
  }
}

size_t RigidBodyPropertiesOffset(hkToolset version, uint8 x64) {
  return RigidBodyNameOffset(version) + PointerSize(x64);
}

size_t RigidBodyMaterialOffset(hkToolset version, uint8 x64) {
  const size_t offset =
      RigidBodyPropertiesOffset(version, x64) + ArrayStorageSize(x64);
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return offset + 4;
  case HK550:
  default:
    return offset;
  }
}

size_t RigidBodyMotionOffset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 224;
  case HK550:
  default:
    return 208;
  }
}

size_t RigidBodyFixedSize(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 544;
  case HK550:
  default:
    return 512;
  }
}

size_t CdBodySize(uint8 x64) { return x64 ? 32u : 16u; }

size_t BroadPhaseHandleOffset(uint8 x64) {
  return RigidBodyCollidableOffset() + CdBodySize(x64) + 4u;
}

size_t RigidBodyHk550AllowedPenetrationOffset() { return 88; }

size_t RigidBodyHk550MultiThreadCheckOffset() { return 100; }

uint16 StoredQualityType(hkToolset version, uint8 quality) {
  if (version == HK550) {
    if (quality == 0xffu) {
      return 0;
    }

    return static_cast<uint16>(quality) + 1;
  }

  return quality;
}

uint8 RuntimeQualityType(hkToolset version, const char *data, size_t offset,
                         uint8 swapEndian) {
  if (version == HK550) {
    const uint16 stored = ReadValue<uint16>(data, offset, swapEndian);
    return stored ? static_cast<uint8>(stored - 1) : 0;
  }

  return ReadValue<uint8>(data, offset);
}

size_t ConvexRadiusOffset() { return 16; }

size_t ConvexVerticesArrayOffset() { return 64; }

size_t ConvexVerticesNumVerticesOffset(uint8 x64) {
  return ConvexVerticesArrayOffset() + ArrayStorageSize(x64);
}

size_t ConvexVerticesPlaneEquationsOffset(CRule rule) {
  size_t offset = AlignOffset(ConvexVerticesNumVerticesOffset(rule.x64) +
                                  sizeof(int32),
                              PointerSize(rule.x64));
  switch (rule.version) {
  case HK2010_2:
    return offset + PointerSize(rule.x64) * 2;
  case HK2012_2:
    return AlignOffset(offset + 1, PointerSize(rule.x64));
  case HK550:
  default:
    return offset;
  }
}

size_t ConvexVerticesShapeFixedSize(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 112;
  case HK550:
  default:
    return 96;
  }
}

size_t MoppCodePointerOffset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 20;
  case HK550:
  default:
    return 16;
  }
}

size_t MoppCodeFixedSize(hkToolset) { return 48; }

size_t MoppCodeInfoOffset(hkToolset) { return 16; }

size_t MoppCodeDataOffset(hkToolset) { return 32; }

size_t MoppCodeBuildTypeOffset(hkToolset) { return 44; }

size_t StorageMeshArrayOffset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 240;
  case HK550:
  default:
    return 192;
  }
}

size_t StorageShapeArrayOffset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 252;
  case HK550:
  default:
    return 204;
  }
}

size_t StorageTrianglesArrayOffset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 184;
  case HK550:
  default:
    return 80;
  }
}

size_t StorageShapeRecordsArrayOffset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 196;
  case HK550:
  default:
    return 88;
  }
}

size_t StorageWeldingArrayOffset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 208;
  case HK550:
  default:
    return 96;
  }
}

size_t StorageWeldingTypeOffset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 220;
  case HK550:
  default:
    return 108;
  }
}

size_t StorageAabbHalfExtentsOffset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 144;
  case HK550:
  default:
    return 48;
  }
}

size_t StorageAabbCenterOffset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 160;
  case HK550:
  default:
    return 64;
  }
}

size_t StorageMeshShapeFixedSize(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 272;
  case HK550:
  default:
    return 216;
  }
}

size_t ShapeSubpartShapeArrayOffset(hkToolset version) {
  switch (version) {
  case HK550:
    return 8;
  case HK2010_2:
  case HK2012_2:
  default:
    return static_cast<size_t>(-1);
  }
}

size_t ShapeRecordChildShapesOffset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 20;
  case HK550:
  default:
    return 16;
  }
}

size_t MeshSubpartIndices8Offset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 20;
  case HK550:
  default:
    return static_cast<size_t>(-1);
  }
}

size_t MeshSubpartStorageFixedSize(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 112;
  case HK550:
  default:
    return 80;
  }
}

size_t MeshSubpartIndices16Offset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 32;
  case HK550:
  default:
    return 20;
  }
}

size_t MeshSubpartIndices32Offset(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 44;
  case HK550:
  default:
    return 32;
  }
}

size_t TriangleSubpartFixedSize(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 112;
  case HK550:
  default:
    return 64;
  }
}

size_t ShapeSubpartFixedSize(hkToolset version) {
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    return 112;
  case HK550:
  default:
    return 64;
  }
}

constexpr uint32 kLockedArrayCapacityMask = 0xc0000000u;
constexpr size_t kCollisionPtrSize = 4;
constexpr size_t kPhysicsDataFixedSize = 32;
constexpr size_t kPhysicsSystemFixedSize = 80;
constexpr size_t kRigidBodyFixedSize = 512;
constexpr size_t kMoppBvTreeShapeFixedSize = 64;
constexpr size_t kStaticCompoundShapeFixedSize = 112;
constexpr size_t kStaticCompoundInstanceSize = 64;
constexpr size_t kStaticCompoundTreeNodeSize = 6;
constexpr size_t kStorageMeshShapeFixedSize = 216;
constexpr size_t kMeshSubpartStorageFixedSize = 80;
constexpr size_t kShapeSubpartStorageFixedSize = 56;
constexpr size_t kListShapeFixedSize = 112;
constexpr size_t kBoxShapeFixedSize = 48;
constexpr size_t kCylinderShapeFixedSize = 80;
constexpr size_t kConvexTranslateShapeFixedSize = 48;
constexpr size_t kConvexTransformShapeFixedSize = 96;
constexpr size_t kTriangleSubpartFixedSize = 64;
constexpr size_t kShapeSubpartFixedSize = 64;

uint32 LockedArrayCapacity(size_t count) {
  return kLockedArrayCapacityMask | static_cast<uint32>(count);
}

uint32 LockedArrayCapacity(hkToolset version, size_t count) {
  uint32 capacityMask = kLockedArrayCapacityMask;
  switch (version) {
  case HK2010_2:
  case HK2012_2:
    capacityMask = 0x80000000u;
    break;
  case HK550:
  default:
    break;
  }
  return capacityMask | static_cast<uint32>(count);
}

struct BufferField {
  size_t offset{};
  size_t size{};
  size_t stride{};
};

std::unordered_map<const std::vector<char> *, std::vector<BufferField>> &
BufferFields() {
  static thread_local std::unordered_map<const std::vector<char> *,
                                        std::vector<BufferField>>
      fields;
  return fields;
}

std::unordered_map<const void *, std::vector<const hkpShape *>> &
ShapeSubpartChildren() {
  static thread_local std::unordered_map<const void *,
                                        std::vector<const hkpShape *>>
      children;
  return children;
}

template <class T> size_t BufferFieldStride() {
  if constexpr (std::is_same_v<T, Vector4A16> ||
                std::is_same_v<T, es::Matrix44>) {
    return sizeof(uint32);
  }
  return sizeof(T);
}

template <class T>
void MarkBufferField(std::vector<char> &buffer, size_t offset) {
  if constexpr (sizeof(T) > 1) {
    BufferFields()[&buffer].push_back(
        {offset, sizeof(T), BufferFieldStride<T>()});
  }
}

void MarkBufferField(std::vector<char> &buffer, size_t offset, size_t size,
                     size_t stride) {
  if (size > 1 && stride > 1) {
    BufferFields()[&buffer].push_back({offset, size, stride});
  }
}

void CopyBufferFields(std::vector<char> &dst, size_t dstOffset,
                      const std::vector<char> &src) {
  auto found = BufferFields().find(&src);
  if (found == BufferFields().end()) {
    return;
  }

  auto &dstFields = BufferFields()[&dst];
  for (auto field : found->second) {
    field.offset += dstOffset;
    dstFields.push_back(field);
  }
}

void AppendBuffer(std::vector<char> &dst, const std::vector<char> &src) {
  const size_t dstOffset = dst.size();
  dst.insert(dst.end(), src.begin(), src.end());
  CopyBufferFields(dst, dstOffset, src);
}

void CopyBuffer(std::vector<char> &dst, size_t dstOffset,
                const std::vector<char> &src) {
  if (dstOffset + src.size() > dst.size()) {
    return;
  }
  std::memcpy(dst.data() + dstOffset, src.data(), src.size());
  CopyBufferFields(dst, dstOffset, src);
}

void SwapBufferFields(std::vector<char> &buffer) {
  auto found = BufferFields().find(&buffer);
  if (found == BufferFields().end()) {
    return;
  }

  for (const auto &field : found->second) {
    if (field.offset + field.size > buffer.size()) {
      continue;
    }

    for (size_t offset = field.offset; offset < field.offset + field.size;
         offset += field.stride) {
      if (field.stride == sizeof(uint16)) {
        auto value = *reinterpret_cast<uint16 *>(buffer.data() + offset);
        FByteswapper(value, true);
        std::memcpy(buffer.data() + offset, &value, sizeof(value));
      } else if (field.stride == sizeof(uint32)) {
        auto value = *reinterpret_cast<uint32 *>(buffer.data() + offset);
        FByteswapper(value, true);
        std::memcpy(buffer.data() + offset, &value, sizeof(value));
      } else if (field.stride == sizeof(uint64)) {
        auto value = *reinterpret_cast<uint64 *>(buffer.data() + offset);
        FByteswapper(value, true);
        std::memcpy(buffer.data() + offset, &value, sizeof(value));
      }
    }
  }
}

template <class T> void WriteField(std::vector<char> &buffer, size_t offset,
                                   const T &value) {
  if (offset + sizeof(T) > buffer.size()) {
    return;
  }

  std::memcpy(buffer.data() + offset, &value, sizeof(T));
  MarkBufferField<T>(buffer, offset);
}

void WriteVectorField(std::vector<char> &buffer, size_t offset,
                      const Vector4A16 &value) {
  WriteField(buffer, offset, value);
}

void WriteMatrixField(std::vector<char> &buffer, size_t offset,
                      const es::Matrix44 &value) {
  WriteField(buffer, offset, value);
}

void WriteArrayHeader(std::vector<char> &buffer, size_t offset, size_t count) {
  WriteField<uint32>(buffer, offset, 0);
  WriteField<uint32>(buffer, offset + 4, static_cast<uint32>(count));
  WriteField<uint32>(buffer, offset + 8, LockedArrayCapacity(count));
}

void WriteArrayHeader(std::vector<char> &buffer, size_t offset, size_t count,
                      hkToolset version) {
  WriteField<uint32>(buffer, offset, 0);
  WriteField<uint32>(buffer, offset + 4, static_cast<uint32>(count));
  WriteField<uint32>(buffer, offset + 8, LockedArrayCapacity(version, count));
}

void WriteSimpleArrayHeader(std::vector<char> &buffer, size_t offset,
                            size_t count) {
  WriteField<uint32>(buffer, offset, 0);
  WriteField<uint32>(buffer, offset + 4, static_cast<uint32>(count));
}

std::vector<char> FixedObject(size_t fixedSize, const char *rawData,
                              CRule rawRule, CRule targetRule) {
  std::vector<char> output(fixedSize, 0);

  if (rawData && rawRule.version == targetRule.version &&
      rawRule.x64 == targetRule.x64 && !targetRule.x64) {
    std::memcpy(output.data(), rawData, fixedSize);
  }

  return output;
}

ArrayView ReadSimpleArray(const char *data, size_t offset, uint8 x64,
                          uint8 swapEndian = false) {
  ArrayView output;
  output.data = ReadPointer<char>(data, offset, x64);
  output.count =
      ReadValue<uint32>(data, offset + (x64 ? static_cast<size_t>(8) : 4),
                        swapEndian);
  return output;
}

ArrayView ReadSimpleArrayX86(const char *data, size_t offset) {
  return ReadSimpleArray(data, offset, false);
}

ArrayView ReadArrayX86(const char *data, size_t offset) {
  return ReadArray(data, offset, false);
}

void WriteBuffer(BinWritterRef_e wr, const std::vector<char> &buffer) {
  if (buffer.empty()) {
    return;
  }

  if (wr.SwappedEndian()) {
    auto output = buffer;
    CopyBufferFields(output, 0, buffer);
    SwapBufferFields(output);
    wr.WriteBuffer(output.data(), output.size());
    BufferFields().erase(&output);
  } else {
    wr.WriteBuffer(buffer.data(), buffer.size());
  }
  BufferFields().erase(&buffer);
}

void WriteRawBytes(BinWritterRef_e wr, const void *data, size_t size) {
  if (data && size) {
    wr.WriteBuffer(static_cast<const char *>(data), size);
  }
}

template <class T>
void WriteValueArray(BinWritterRef_e wr, const void *data, size_t count) {
  const auto *values = static_cast<const T *>(data);
  for (size_t i = 0; i < count; i++) {
    wr.Write<T>(values[i]);
  }
}

template <class Getter>
void SavePointerArray(BinWritterRef_e wr, hkFixups &fixups, size_t objectBegin,
                      size_t pointerOffset, size_t count, Getter getter) {
  if (!count) {
    return;
  }

  wr.ApplyPadding();
  fixups.locals.emplace_back(objectBegin + pointerOffset, wr.Tell());

  for (size_t i = 0; i < count; i++) {
    fixups.locals.emplace_back(wr.Tell(), getter(i));
    wr.Skip(kCollisionPtrSize);
  }
}

void SaveValueArray(BinWritterRef_e wr, hkFixups &fixups, size_t objectBegin,
                    size_t pointerOffset, const void *data, size_t count,
                    size_t elementSize, size_t alignment = 16) {
  if (!count || !data || !elementSize) {
    return;
  }

  wr.ApplyPadding(static_cast<int>(alignment));
  fixups.locals.emplace_back(objectBegin + pointerOffset, wr.Tell());
  if (elementSize == sizeof(uint16)) {
    WriteValueArray<uint16>(wr, data, count);
  } else if (elementSize == sizeof(uint32)) {
    WriteValueArray<uint32>(wr, data, count);
  } else if (elementSize == sizeof(uint64)) {
    WriteValueArray<uint64>(wr, data, count);
  } else if (elementSize == sizeof(Vector4A16)) {
    WriteValueArray<Vector4A16>(wr, data, count);
  } else {
    WriteRawBytes(wr, data, count * elementSize);
  }
}

void SaveRigidBodyPropertyArray(BinWritterRef_e wr, hkFixups &fixups,
                                size_t objectBegin, size_t pointerOffset,
                                const hkpRigidBodyProperty *properties,
                                size_t count) {
  if (!count || !properties) {
    return;
  }

  wr.ApplyPadding();
  fixups.locals.emplace_back(objectBegin + pointerOffset, wr.Tell());
  for (size_t i = 0; i < count; i++) {
    wr.Write<uint32>(properties[i].key);
    wr.Write<uint32>(properties[i].alignmentPadding);
    wr.Write<uint64>(properties[i].data);
  }
}

void SaveString(BinWritterRef_e wr, hkFixups &fixups, size_t objectBegin,
                size_t pointerOffset, std::string_view text) {
  if (text.empty()) {
    return;
  }

  wr.ApplyPadding();
  fixups.locals.emplace_back(objectBegin + pointerOffset, wr.Tell());
  wr.WriteT(text);
}

struct Bounds {
  size_t count{};
  float min[3]{};
  float max[3]{};

  void Add(const Vector4A16 &value) {
    if (!count) {
      min[0] = max[0] = value._arr[0];
      min[1] = max[1] = value._arr[1];
      min[2] = max[2] = value._arr[2];
      count++;
      return;
    }

    for (size_t i = 0; i < 3; i++) {
      min[i] = (std::min)(min[i], value._arr[i]);
      max[i] = (std::max)(max[i], value._arr[i]);
    }
    count++;
  }

  Vector4A16 HalfExtents(float radius = 0.05f) const {
    if (!count) {
      return Vector4A16(radius, radius, radius, radius);
    }

    return Vector4A16((max[0] - min[0]) * 0.5f + radius,
                      (max[1] - min[1]) * 0.5f + radius,
                      (max[2] - min[2]) * 0.5f + radius, radius);
  }

  Vector4A16 Center() const {
    if (!count) {
      return Vector4A16(0.0f, 0.0f, 0.0f, 0.0f);
    }

    return Vector4A16((max[0] + min[0]) * 0.5f, (max[1] + min[1]) * 0.5f,
                      (max[2] + min[2]) * 0.5f, 0.0f);
  }
};

void AddAabb(Bounds &bounds, const Vector4A16 &halfExtents,
             const Vector4A16 &center) {
  bounds.Add(Vector4A16(center._arr[0] - halfExtents._arr[0],
                        center._arr[1] - halfExtents._arr[1],
                        center._arr[2] - halfExtents._arr[2], 0.0f));
  bounds.Add(Vector4A16(center._arr[0] + halfExtents._arr[0],
                        center._arr[1] + halfExtents._arr[1],
                        center._arr[2] + halfExtents._arr[2], 0.0f));
}

Vector4A16 AddRadius(Vector4A16 halfExtents, float radius) {
  halfExtents._arr[0] += radius;
  halfExtents._arr[1] += radius;
  halfExtents._arr[2] += radius;
  return halfExtents;
}

void AddShapeAabb(const hkpShape *shape, Bounds &bounds) {
  if (!shape) {
    return;
  }

  if (const auto *mopp = safe_deref_cast<const hkpMoppBvTreeShape>(shape)) {
    AddShapeAabb(mopp->GetChildShape(), bounds);
    return;
  }

  if (const auto *mesh =
          safe_deref_cast<const hkpStorageExtendedMeshShape>(shape)) {
    for (auto subpart : mesh->MeshSubparts()) {
      if (!subpart || !subpart->GetVertices()) {
        continue;
      }
      const auto *vertices = subpart->GetVertices();
      for (size_t i = 0; i < subpart->GetNumVertices(); i++) {
        bounds.Add(vertices[i]);
      }
    }
    for (auto subpart : mesh->ShapeSubparts()) {
      if (!subpart) {
        continue;
      }
      for (size_t i = 0; i < subpart->GetNumShapes(); i++) {
        AddShapeAabb(subpart->GetShape(i), bounds);
      }
    }
    return;
  }

  if (const auto *list = safe_deref_cast<const hkpListShape>(shape)) {
    for (size_t i = 0; i < list->GetNumChildren(); i++) {
      AddShapeAabb(list->GetChild(i), bounds);
    }
    return;
  }

  if (const auto *translated =
          safe_deref_cast<const hkpConvexTranslateShape>(shape)) {
    Bounds childBounds;
    AddShapeAabb(translated->GetChildShape(), childBounds);
    if (!childBounds.count) {
      return;
    }
    const Vector4A16 halfExtents = childBounds.HalfExtents(0.0f);
    Vector4A16 center = childBounds.Center();
    const Vector4A16 translation = translated->GetTranslation();
    center._arr[0] += translation._arr[0];
    center._arr[1] += translation._arr[1];
    center._arr[2] += translation._arr[2];
    AddAabb(bounds, halfExtents, center);
    return;
  }

  if (const auto *box = safe_deref_cast<const hkpBoxShape>(shape)) {
    AddAabb(bounds, AddRadius(box->GetHalfExtents(), box->GetRadius()),
            Vector4A16(0.0f, 0.0f, 0.0f, 0.0f));
    return;
  }

  if (const auto *cylinder = safe_deref_cast<const hkpCylinderShape>(shape)) {
    const Vector4A16 a = cylinder->GetVertexA();
    const Vector4A16 b = cylinder->GetVertexB();
    const float radius = cylinder->GetRadius();
    bounds.Add(Vector4A16((std::min)(a._arr[0], b._arr[0]) - radius,
                          (std::min)(a._arr[1], b._arr[1]) - radius,
                          (std::min)(a._arr[2], b._arr[2]) - radius, 0.0f));
    bounds.Add(Vector4A16((std::max)(a._arr[0], b._arr[0]) + radius,
                          (std::max)(a._arr[1], b._arr[1]) + radius,
                          (std::max)(a._arr[2], b._arr[2]) + radius, 0.0f));
    return;
  }

  if (const auto *convex =
          safe_deref_cast<const hkpConvexVerticesShape>(shape)) {
    AddAabb(bounds, AddRadius(convex->GetAabbHalfExtents(),
                              convex->GetRadius()),
            convex->GetAabbCenter());
  }
}

Vector4A16 RotateVector(const Vector4A16 &rotation, const Vector4A16 &value) {
  const float x = value._arr[0];
  const float y = value._arr[1];
  const float z = value._arr[2];
  const float qx = rotation._arr[0];
  const float qy = rotation._arr[1];
  const float qz = rotation._arr[2];
  const float qw = rotation._arr[3];

  const float tx = 2.0f * (qy * z - qz * y);
  const float ty = 2.0f * (qz * x - qx * z);
  const float tz = 2.0f * (qx * y - qy * x);

  return Vector4A16(x + qw * tx + (qy * tz - qz * ty),
                    y + qw * ty + (qz * tx - qx * tz),
                    z + qw * tz + (qx * ty - qy * tx), value._arr[3]);
}

Bounds TransformBounds(const Bounds &bounds,
                       const hkpStaticCompoundShapeInstance &instance) {
  Bounds output;
  if (!bounds.count) {
    output.Add(instance.translation);
    return output;
  }

  for (size_t x = 0; x < 2; x++) {
    for (size_t y = 0; y < 2; y++) {
      for (size_t z = 0; z < 2; z++) {
        Vector4A16 corner(x ? bounds.max[0] : bounds.min[0],
                          y ? bounds.max[1] : bounds.min[1],
                          z ? bounds.max[2] : bounds.min[2], 0.0f);
        corner._arr[0] *= instance.scale._arr[0];
        corner._arr[1] *= instance.scale._arr[1];
        corner._arr[2] *= instance.scale._arr[2];
        corner = RotateVector(instance.rotation, corner);
        corner._arr[0] += instance.translation._arr[0];
        corner._arr[1] += instance.translation._arr[1];
        corner._arr[2] += instance.translation._arr[2];
        output.Add(corner);
      }
    }
  }

  return output;
}

void ExpandBounds(Bounds &bounds, float margin) {
  if (!bounds.count) {
    return;
  }

  for (size_t i = 0; i < 3; i++) {
    bounds.min[i] -= margin;
    bounds.max[i] += margin;
  }
}

Bounds MergeBounds(const std::vector<Bounds> &bounds,
                   const std::vector<size_t> &ids) {
  Bounds output;
  for (size_t id : ids) {
    if (!bounds[id].count) {
      continue;
    }
    output.Add(Vector4A16(bounds[id].min[0], bounds[id].min[1],
                          bounds[id].min[2], 0.0f));
    output.Add(Vector4A16(bounds[id].max[0], bounds[id].max[1],
                          bounds[id].max[2], 0.0f));
  }
  return output;
}

struct StaticTreeNode {
  Bounds bounds;
  int data = -1;
  int delta = 0;
};

size_t BuildStaticTreeNodes(const std::vector<Bounds> &leafBounds,
                            std::vector<size_t> ids,
                            std::vector<StaticTreeNode> &nodes) {
  const size_t nodeIndex = nodes.size();
  nodes.emplace_back();
  nodes[nodeIndex].bounds = MergeBounds(leafBounds, ids);

  if (ids.size() <= 1) {
    nodes[nodeIndex].data = ids.empty() ? 0 : static_cast<int>(ids.front());
    return nodeIndex;
  }

  size_t axis = 0;
  float bestSpan = -1.0f;
  for (size_t i = 0; i < 3; i++) {
    float minCenter = 0.0f;
    float maxCenter = 0.0f;
    auto hasCenter = false;
    for (size_t id : ids) {
      const float center = (leafBounds[id].min[i] + leafBounds[id].max[i]) *
                           0.5f;
      if (!hasCenter) {
        minCenter = maxCenter = center;
        hasCenter = true;
      } else {
        minCenter = (std::min)(minCenter, center);
        maxCenter = (std::max)(maxCenter, center);
      }
    }
    const float span = maxCenter - minCenter;
    if (span > bestSpan) {
      bestSpan = span;
      axis = i;
    }
  }

  const size_t mid = ids.size() / 2;
  std::sort(ids.begin(), ids.end(), [&](size_t left, size_t right) {
    const float leftCenter =
        (leafBounds[left].min[axis] + leafBounds[left].max[axis]) * 0.5f;
    const float rightCenter =
        (leafBounds[right].min[axis] + leafBounds[right].max[axis]) * 0.5f;
    if (leftCenter == rightCenter) {
      return left < right;
    }
    return leftCenter < rightCenter;
  });

  std::vector<size_t> leftIds(ids.begin(), ids.begin() + mid);
  std::vector<size_t> rightIds(ids.begin() + mid, ids.end());
  BuildStaticTreeNodes(leafBounds, std::move(leftIds), nodes);
  const size_t rightIndex =
      BuildStaticTreeNodes(leafBounds, std::move(rightIds), nodes);
  nodes[nodeIndex].delta = static_cast<int>(rightIndex - nodeIndex);
  return nodeIndex;
}

uint8 PackAabbAxis(float parentMin, float parentMax, float childMin,
                  float childMax) {
  const float extent = (parentMax - parentMin) / 226.0f;
  uint8 minNibble = 0;
  uint8 maxNibble = 0;

  while (minNibble < 15) {
    const uint8 next = static_cast<uint8>(minNibble + 1);
    const float decodedMin = parentMin + extent * float(next * next);
    if (decodedMin > childMin) {
      break;
    }
    minNibble = next;
  }

  while (maxNibble < 15) {
    const uint8 next = static_cast<uint8>(maxNibble + 1);
    const float decodedMax = parentMax - extent * float(next * next);
    if (decodedMax < childMax) {
      break;
    }
    maxNibble = next;
  }

  return static_cast<uint8>((minNibble << 4) | maxNibble);
}

std::vector<char> BuildAabb6TreeBuffer(const std::vector<StaticTreeNode> &nodes) {
  std::vector<char> output(nodes.size() * kStaticCompoundTreeNodeSize, 0);
  if (nodes.empty()) {
    return output;
  }

  auto writeNode = [&](size_t id, const Bounds &parentBounds) {
    const auto &node = nodes[id];
    const size_t offset = id * kStaticCompoundTreeNodeSize;
    output[offset + 0] =
        PackAabbAxis(parentBounds.min[0], parentBounds.max[0],
                     node.bounds.min[0], node.bounds.max[0]);
    output[offset + 1] =
        PackAabbAxis(parentBounds.min[1], parentBounds.max[1],
                     node.bounds.min[1], node.bounds.max[1]);
    output[offset + 2] =
        PackAabbAxis(parentBounds.min[2], parentBounds.max[2],
                     node.bounds.min[2], node.bounds.max[2]);

    if (node.delta) {
      const int encoded = node.delta >> 1;
      output[offset + 3] = static_cast<char>(0x80 | ((encoded >> 16) & 0x7f));
      const uint16 lo = static_cast<uint16>(encoded);
      std::memcpy(output.data() + offset + 4, &lo, sizeof(lo));
    } else {
      output[offset + 3] = static_cast<char>((node.data >> 16) & 0x7f);
      const uint16 lo = static_cast<uint16>(node.data);
      std::memcpy(output.data() + offset + 4, &lo, sizeof(lo));
    }
  };

  writeNode(0, nodes[0].bounds);
  for (size_t i = 0; i < nodes.size(); i++) {
    if (!nodes[i].delta) {
      continue;
    }

    writeNode(i + 1, nodes[i].bounds);
    writeNode(i + static_cast<size_t>(nodes[i].delta), nodes[i].bounds);
  }

  MarkBufferField(output, 4, output.size() > 4 ? output.size() - 4 : 0,
                  kStaticCompoundTreeNodeSize);
  return output;
}

uint32 StaticCompoundInstanceBits(const hkpStaticCompoundShapeInstance &inst) {
  constexpr uint32 kInstanceBitIsLeaf = 0x01;
  constexpr uint32 kInstanceBitHasTransform = 0x02;
  constexpr uint32 kInstanceBitHasScale = 0x04;
  constexpr uint32 kInstanceBitHasFlip = 0x08;

  uint32 bits = 0;
  if (!safe_deref_cast<const hkpMoppBvTreeShape>(inst.shape) &&
      !safe_deref_cast<const hkpStorageExtendedMeshShape>(inst.shape) &&
      !safe_deref_cast<const hkpListShape>(inst.shape)) {
    bits |= kInstanceBitIsLeaf;
  }

  const auto hasTranslation = inst.translation._arr[0] != 0.0f ||
                              inst.translation._arr[1] != 0.0f ||
                              inst.translation._arr[2] != 0.0f;
  const auto hasRotation = inst.rotation._arr[0] != 0.0f ||
                           inst.rotation._arr[1] != 0.0f ||
                           inst.rotation._arr[2] != 0.0f ||
                           inst.rotation._arr[3] != 1.0f;
  if (hasTranslation || hasRotation) {
    bits |= kInstanceBitHasTransform;
  }

  const auto hasScale = inst.scale._arr[0] != 1.0f ||
                        inst.scale._arr[1] != 1.0f ||
                        inst.scale._arr[2] != 1.0f;
  if (hasScale) {
    bits |= kInstanceBitHasScale;
  }
  if (inst.scale._arr[0] < 0.0f || inst.scale._arr[1] < 0.0f ||
      inst.scale._arr[2] < 0.0f) {
    bits |= kInstanceBitHasFlip;
  }

  return bits;
}

float PackedInt24W(uint32 value) {
  return FloatFromBits(0x3f000000u | (value & 0x00ffffffu));
}

std::vector<char> MakeTriangleSubpart(
    const hkpStorageExtendedMeshShapeMeshSubpartStorage *subpart,
    hkToolset version) {
  std::vector<char> record(TriangleSubpartFixedSize(version), 0);
  if (!subpart) {
    return record;
  }

  const uint32 numTriangles = static_cast<uint32>(subpart->GetNumTriangles());
  const uint32 numVertices = static_cast<uint32>(subpart->GetNumVertices());
  uint32 indexStride = 0;
  uint8 stridingType = 0;

  if (subpart->GetNumIndices32()) {
    indexStride = 16;
    stridingType = version == HK550 ? 2 : 3;
  } else if (subpart->GetNumIndices16()) {
    indexStride = 8;
    stridingType = version == HK550 ? 1 : 2;
  } else if (subpart->GetNumIndices8()) {
    indexStride = 4;
    stridingType = 1;
  }

  if (version == HK2010_2 || version == HK2012_2) {
    WriteField<uint16>(record, 0, 8);
    WriteField<uint16>(record, 2, 0);
    WriteField<uint16>(record, 6, 0);
    WriteField<uint32>(record, 16, 0);
    WriteField<uint32>(record, 20, numTriangles);
    WriteField<uint32>(record, 28, numVertices);
    WriteField<uint16>(record, 36, sizeof(Vector4A16));
    WriteField<int32>(record, 40, 0);
    WriteField<uint16>(record, 44, static_cast<uint16>(indexStride));
    WriteField<uint8>(record, 46, stridingType);
    WriteField<uint8>(record, 47, 0);
    WriteVectorField(record, 48, Vector4A16(0.0f, 0.0f, 0.0f, 0.0f));
    WriteVectorField(record, 64, Vector4A16(0.0f, 0.0f, 0.0f, 0.0f));
    WriteVectorField(record, 80, Vector4A16(0.0f, 0.0f, 0.0f, 1.0f));
    WriteVectorField(record, 96, Vector4A16(1.0f, 1.0f, 1.0f, 0.0f));
    return record;
  }

  record[0] = 0;
  record[1] = 1;
  WriteField<uint16>(record, 8, 0);
  WriteField<uint16>(record, 10, 1);
  WriteField<uint32>(record, 16, numTriangles);
  WriteField<uint32>(record, 24, sizeof(Vector4A16));
  WriteField<uint32>(record, 28, numVertices);
  WriteField<uint32>(record, 52, indexStride);
  WriteField<uint8>(record, 56, stridingType);
  WriteField<uint8>(record, 57, 0);
  WriteField<int32>(record, 60, 0);
  return record;
}

std::vector<char> MakeShapeSubpart(size_t numShapes) {
  std::vector<char> record(kShapeSubpartFixedSize, 0);
  record[0] = 1;
  record[1] = 1;
  WriteField<uint16>(record, 10, 1);
  WriteSimpleArrayHeader(record, 16, numShapes);
  WriteVectorField(record, 32, Vector4A16(0.0f, 0.0f, 0.0f, 1.0f));
  WriteVectorField(record, 48, Vector4A16(0.0f, 0.0f, 0.0f, 0.0f));
  return record;
}

void ClearOldTriangleSubpartPointers(std::vector<char> &buffer,
                                      size_t offset) {
  WriteField<uint32>(buffer, offset + 4, 0);
  WriteField<uint32>(buffer, offset + 12, 0);
  WriteField<uint32>(buffer, offset + 20, 0);
  WriteField<uint32>(buffer, offset + 48, 0);
}

void ClearOldShapeSubpartPointers(std::vector<char> &buffer, size_t offset) {
  WriteField<uint32>(buffer, offset + 4, 0);
  WriteField<uint32>(buffer, offset + 12, 0);
  WriteField<uint32>(buffer, offset + 16, 0);
}

void MarkTriangleSubpartArray(std::vector<char> &buffer, hkToolset version) {
  const size_t recordSize = TriangleSubpartFixedSize(version);
  if (!recordSize) {
    return;
  }
  for (size_t base = 0; base + recordSize <= buffer.size();
       base += recordSize) {
    if (version == HK2010_2 || version == HK2012_2) {
      MarkBufferField<uint16>(buffer, base + 0);
      MarkBufferField<uint16>(buffer, base + 2);
      MarkBufferField<uint16>(buffer, base + 6);
      MarkBufferField<uint32>(buffer, base + 16);
      MarkBufferField<uint32>(buffer, base + 20);
      MarkBufferField<uint32>(buffer, base + 28);
      MarkBufferField<uint16>(buffer, base + 36);
      MarkBufferField<int32>(buffer, base + 40);
      MarkBufferField<uint16>(buffer, base + 44);
      MarkBufferField<Vector4A16>(buffer, base + 48);
      MarkBufferField<Vector4A16>(buffer, base + 64);
      MarkBufferField<Vector4A16>(buffer, base + 80);
      MarkBufferField<Vector4A16>(buffer, base + 96);
      continue;
    }

    MarkBufferField<uint16>(buffer, base + 8);
    MarkBufferField<uint16>(buffer, base + 10);
    MarkBufferField<uint32>(buffer, base + 16);
    MarkBufferField<uint32>(buffer, base + 24);
    MarkBufferField<uint32>(buffer, base + 28);
    MarkBufferField<uint32>(buffer, base + 52);
    MarkBufferField<int32>(buffer, base + 60);
  }
}

void MarkShapeSubpartArray(std::vector<char> &buffer, size_t recordSize) {
  if (!recordSize) {
    return;
  }
  for (size_t base = 0; base + recordSize <= buffer.size();
       base += recordSize) {
    MarkBufferField<uint32>(buffer, base + 16);
    MarkBufferField<uint32>(buffer, base + 20);
    MarkBufferField<uint16>(buffer, base + 10);
    MarkBufferField<Vector4A16>(buffer, base + 32);
    MarkBufferField<Vector4A16>(buffer, base + 48);
  }
}

constexpr uint32 kStorageTriangleKey = 0;
constexpr uint32 kStorageShapeKey = 0x80000000u;

uint32 StorageShapeKey(uint32 subpartIndex, uint32 terminalIndex,
                       uint32 shapeBit) {
  constexpr uint32 bitsForSubpart = 12;
  const uint32 key =
      (subpartIndex << (32 - bitsForSubpart)) | terminalIndex;
  return shapeBit | key;
}

void GatherPrimitiveKeys(const hkpShape *shape, std::vector<uint32> &keys) {
  if (!shape) {
    return;
  }

  if (const auto *mopp = safe_deref_cast<const hkpMoppBvTreeShape>(shape)) {
    GatherPrimitiveKeys(mopp->GetChildShape(), keys);
    return;
  }

  if (const auto *mesh = safe_deref_cast<const hkpStorageExtendedMeshShape>(shape)) {
    for (size_t subpartIndex = 0; subpartIndex < mesh->GetNumMeshSubparts();
         subpartIndex++) {
      const auto *subpart = mesh->GetMeshSubpart(subpartIndex);
      const size_t numTriangles = subpart ? subpart->GetNumTriangles() : 0;
      for (size_t triangle = 0; triangle < numTriangles; triangle++) {
        keys.emplace_back(StorageShapeKey(static_cast<uint32>(subpartIndex),
                                          static_cast<uint32>(triangle),
                                          kStorageTriangleKey));
      }
    }

    for (size_t subpartIndex = 0; subpartIndex < mesh->GetNumShapeSubparts();
         subpartIndex++) {
      const auto *subpart = mesh->GetShapeSubpart(subpartIndex);
      const size_t numShapes = subpart ? subpart->GetNumShapes() : 0;
      for (size_t child = 0; child < numShapes; child++) {
        keys.emplace_back(StorageShapeKey(static_cast<uint32>(subpartIndex),
                                          static_cast<uint32>(child),
                                          kStorageShapeKey));
      }
    }
    return;
  }

  if (const auto *list = safe_deref_cast<const hkpListShape>(shape)) {
    for (size_t i = 0; i < list->GetNumChildren(); i++) {
      keys.emplace_back(static_cast<uint32>(i));
    }
    return;
  }

  keys.emplace_back(0);
}

void EncodeTerminal(uint32 key, std::vector<uint8> &code) {
  if (key < 0x20) {
    code.emplace_back(static_cast<uint8>(0x30u + key));
  } else if (key <= 0xff) {
    code.emplace_back(static_cast<uint8>(0x50));
    code.emplace_back(static_cast<uint8>(key));
  } else if (key <= 0xffff) {
    code.emplace_back(static_cast<uint8>(0x51));
    code.emplace_back(static_cast<uint8>(key >> 8));
    code.emplace_back(static_cast<uint8>(key));
  } else if (key <= 0xffffff) {
    code.emplace_back(static_cast<uint8>(0x52));
    code.emplace_back(static_cast<uint8>(key >> 16));
    code.emplace_back(static_cast<uint8>(key >> 8));
    code.emplace_back(static_cast<uint8>(key));
  } else {
    code.emplace_back(static_cast<uint8>(0x53));
    code.emplace_back(static_cast<uint8>(key >> 24));
    code.emplace_back(static_cast<uint8>(key >> 16));
    code.emplace_back(static_cast<uint8>(key >> 8));
    code.emplace_back(static_cast<uint8>(key));
  }
}

void EncodeTerminalList(const std::vector<uint32> &keys,
                        std::vector<uint8> &code) {
  if (keys.empty()) {
    code.emplace_back(uint8{});
    return;
  }

  if (keys.size() == 1) {
    EncodeTerminal(keys.front(), code);
    return;
  }

  uint32 maxKey = 0;
  for (uint32 key : keys) {
    maxKey = (std::max)(maxKey, key);
  }

  if (keys.size() <= 0xff && maxKey <= 0xff) {
    code.emplace_back(static_cast<uint8>(0x54));
    code.emplace_back(static_cast<uint8>(keys.size()));
    for (uint32 key : keys) {
      code.emplace_back(static_cast<uint8>(key));
    }
    return;
  }

  if (keys.size() <= 0xffff && maxKey <= 0xffff) {
    code.emplace_back(static_cast<uint8>(0x55));
    code.emplace_back(static_cast<uint8>(keys.size() >> 8));
    code.emplace_back(static_cast<uint8>(keys.size()));
    for (uint32 key : keys) {
      code.emplace_back(static_cast<uint8>(key >> 8));
      code.emplace_back(static_cast<uint8>(key));
    }
    return;
  }

  if (keys.size() <= 0xffffff && maxKey <= 0xffffff) {
    code.emplace_back(static_cast<uint8>(0x56));
    code.emplace_back(static_cast<uint8>(keys.size() >> 16));
    code.emplace_back(static_cast<uint8>(keys.size() >> 8));
    code.emplace_back(static_cast<uint8>(keys.size()));
    for (uint32 key : keys) {
      code.emplace_back(static_cast<uint8>(key >> 16));
      code.emplace_back(static_cast<uint8>(key >> 8));
      code.emplace_back(static_cast<uint8>(key));
    }
    return;
  }

  code.emplace_back(static_cast<uint8>(0x57));
  code.emplace_back(static_cast<uint8>(keys.size() >> 24));
  code.emplace_back(static_cast<uint8>(keys.size() >> 16));
  code.emplace_back(static_cast<uint8>(keys.size() >> 8));
  code.emplace_back(static_cast<uint8>(keys.size()));
  for (uint32 key : keys) {
    code.emplace_back(static_cast<uint8>(key >> 24));
    code.emplace_back(static_cast<uint8>(key >> 16));
    code.emplace_back(static_cast<uint8>(key >> 8));
    code.emplace_back(static_cast<uint8>(key));
  }
}

struct GeneratedMopp {
  Vector4A16 offset{};
  std::vector<uint8> code;
};

struct MoppPrimitive {
  uint32 key{};
  float min[3]{};
  float max[3]{};
  uint8 qmin[3]{};
  uint8 qmax[3]{};
};

Vector4A16 BuildMoppOffset(const hkpShape *childShape) {
  Bounds bounds;
  AddShapeAabb(childShape, bounds);
  if (!bounds.count) {
    return Vector4A16(0.0f, 0.0f, 0.0f, 1.0f);
  }

  constexpr float margin = 0.05f;
  constexpr float quantizedRange = 16646144.0f;
  const float spanX = bounds.max[0] - bounds.min[0];
  const float spanY = bounds.max[1] - bounds.min[1];
  const float spanZ = bounds.max[2] - bounds.min[2];
  const float span = (std::max)((std::max)(spanX, spanY), spanZ);
  const float scale = quantizedRange / ((std::max)(span, margin) + margin * 2.0f);
  return Vector4A16(bounds.min[0] - margin, bounds.min[1] - margin,
                   bounds.min[2] - margin, scale);
}

Bounds GetShapeBounds(const hkpShape *shape) {
  Bounds bounds;
  AddShapeAabb(shape, bounds);
  return bounds;
}

void AddPrimitive(std::vector<MoppPrimitive> &primitives, uint32 key,
                  const Bounds &bounds) {
  if (!bounds.count) {
    return;
  }

  MoppPrimitive primitive;
  primitive.key = key;
  for (size_t axis = 0; axis < 3; axis++) {
    primitive.min[axis] = bounds.min[axis];
    primitive.max[axis] = bounds.max[axis];
  }
  primitives.emplace_back(primitive);
}

void GatherMoppPrimitives(const hkpShape *shape,
                          std::vector<MoppPrimitive> &primitives) {
  if (!shape) {
    return;
  }

  if (const auto *mopp = safe_deref_cast<const hkpMoppBvTreeShape>(shape)) {
    GatherMoppPrimitives(mopp->GetChildShape(), primitives);
    return;
  }

  if (const auto *mesh =
          safe_deref_cast<const hkpStorageExtendedMeshShape>(shape)) {
    for (size_t subpartIndex = 0; subpartIndex < mesh->GetNumMeshSubparts();
         subpartIndex++) {
      const auto *subpart = mesh->GetMeshSubpart(subpartIndex);
      if (!subpart || !subpart->GetVertices()) {
        continue;
      }

      const auto *vertices = subpart->GetVertices();
      const size_t numVertices = subpart->GetNumVertices();
      for (size_t triangle = 0; triangle < subpart->GetNumTriangles();
           triangle++) {
        uint32 a = 0;
        uint32 b = 0;
        uint32 c = 0;
        if (!subpart->GetTriangleIndices(triangle, a, b, c) ||
            a >= numVertices || b >= numVertices || c >= numVertices) {
          continue;
        }

        Bounds bounds;
        bounds.Add(vertices[a]);
        bounds.Add(vertices[b]);
        bounds.Add(vertices[c]);
        AddPrimitive(
            primitives,
            StorageShapeKey(static_cast<uint32>(subpartIndex),
                            static_cast<uint32>(triangle),
                            kStorageTriangleKey),
            bounds);
      }
    }

    for (size_t subpartIndex = 0; subpartIndex < mesh->GetNumShapeSubparts();
         subpartIndex++) {
      const auto *subpart = mesh->GetShapeSubpart(subpartIndex);
      if (!subpart) {
        continue;
      }
      for (size_t child = 0; child < subpart->GetNumShapes(); child++) {
        AddPrimitive(
            primitives,
            StorageShapeKey(static_cast<uint32>(subpartIndex),
                            static_cast<uint32>(child), kStorageShapeKey),
            GetShapeBounds(subpart->GetShape(child)));
      }
    }
    return;
  }

  if (const auto *list = safe_deref_cast<const hkpListShape>(shape)) {
    for (size_t i = 0; i < list->GetNumChildren(); i++) {
      AddPrimitive(primitives, static_cast<uint32>(i),
                   GetShapeBounds(list->GetChild(i)));
    }
    return;
  }

  AddPrimitive(primitives, 0, GetShapeBounds(shape));
}

uint8 ClampMoppByteCoord(double rounded) {
  int quantized = static_cast<int>(rounded) >> 16;
  quantized = (std::max)(0, (std::min)(254, quantized));
  return static_cast<uint8>(quantized);
}

uint8 QuantizeMoppMinCoord(float value, float offset, float scale) {
  const double fixedValue =
      (static_cast<double>(value) - static_cast<double>(offset)) *
      static_cast<double>(scale);
  return ClampMoppByteCoord(std::floor(fixedValue));
}

uint8 QuantizeMoppMaxCoord(float value, float offset, float scale) {
  const double fixedValue =
      (static_cast<double>(value) - static_cast<double>(offset)) *
      static_cast<double>(scale);
  return ClampMoppByteCoord(std::ceil(fixedValue));
}

void QuantizeMoppPrimitives(std::vector<MoppPrimitive> &primitives,
                            const Vector4A16 &offset) {
  for (auto &primitive : primitives) {
    for (size_t axis = 0; axis < 3; axis++) {
      primitive.qmin[axis] =
          QuantizeMoppMinCoord(primitive.min[axis], offset._arr[axis],
                               offset._arr[3]);
      primitive.qmax[axis] =
          QuantizeMoppMaxCoord(primitive.max[axis], offset._arr[axis],
                               offset._arr[3]);
      if (primitive.qmax[axis] < primitive.qmin[axis]) {
        primitive.qmax[axis] = primitive.qmin[axis];
      }
    }
  }
}

void EmitJump32(std::vector<uint8> &code, uint32 offset) {
  code.emplace_back(static_cast<uint8>(0x08));
  code.emplace_back(static_cast<uint8>(offset >> 24));
  code.emplace_back(static_cast<uint8>(offset >> 16));
  code.emplace_back(static_cast<uint8>(offset >> 8));
  code.emplace_back(static_cast<uint8>(offset));
}

uint8 MoppSplitJumpCommand(size_t axis) {
  return static_cast<uint8>(0x23u + axis);
}

uint8 MoppSplitCommand(size_t axis) { return static_cast<uint8>(0x10u + axis); }

uint8 MoppDoubleCutCommand(size_t axis) {
  return static_cast<uint8>(0x26u + axis);
}

size_t MoppTerminalListSize(const std::vector<MoppPrimitive> &primitives,
                            const std::vector<size_t> &ids, size_t begin,
                            size_t count) {
  if (count == 0) {
    return 1;
  }

  if (count == 1) {
    const uint32 key = primitives[ids[begin]].key;
    if (key < 0x20) {
      return 1;
    }
    if (key <= 0xff) {
      return 2;
    }
    if (key <= 0xffff) {
      return 3;
    }
    if (key <= 0xffffff) {
      return 4;
    }
    return 5;
  }

  uint32 maxKey = 0;
  for (size_t i = 0; i < count; i++) {
    maxKey = (std::max)(maxKey, primitives[ids[begin + i]].key);
  }

  if (count <= 0xff && maxKey <= 0xff) {
    return 2 + count;
  }
  if (count <= 0xffff && maxKey <= 0xffff) {
    return 3 + count * sizeof(uint16);
  }
  if (count <= 0xffffff && maxKey <= 0xffffff) {
    return 4 + count * 3;
  }
  return 5 + count * sizeof(uint32);
}

uint8 MoppCutMin(const MoppPrimitive &primitive, size_t axis) {
  return primitive.qmin[axis];
}

uint8 MoppCutMax(const MoppPrimitive &primitive, size_t axis) {
  return static_cast<uint8>((std::min)(255, static_cast<int>(
                                                primitive.qmax[axis]) +
                                                1));
}

void EmitMoppBounds(const std::vector<MoppPrimitive> &primitives,
                    const std::vector<size_t> &ids, std::vector<uint8> &code) {
  for (size_t axis = 0; axis < 3; axis++) {
    uint8 minValue = 255;
    uint8 maxValue = 0;
    for (size_t id : ids) {
      minValue = (std::min)(minValue, MoppCutMin(primitives[id], axis));
      maxValue = (std::max)(maxValue, MoppCutMax(primitives[id], axis));
    }

    code.emplace_back(MoppDoubleCutCommand(axis));
    code.emplace_back(minValue);
    code.emplace_back(maxValue);
  }
}

size_t MoppLeafCount(const std::vector<MoppPrimitive> &primitives,
                     const std::vector<size_t> &ids) {
  size_t count = 1;
  while (count < ids.size()) {
    const size_t next = count + 1;
    const size_t leafSize =
        9 + MoppTerminalListSize(primitives, ids, 0, next);
    if (leafSize > 250) {
      break;
    }
    count = next;
  }

  return count;
}

std::vector<uint8> EmitMoppLeaf(const std::vector<MoppPrimitive> &primitives,
                                const std::vector<size_t> &ids) {
  std::vector<uint8> code;
  code.reserve(9 + MoppTerminalListSize(primitives, ids, 0, ids.size()));
  EmitMoppBounds(primitives, ids, code);

  std::vector<uint32> keys;
  keys.reserve(ids.size());
  for (size_t id : ids) {
    keys.emplace_back(primitives[id].key);
  }
  EncodeTerminalList(keys, code);
  return code;
}

std::vector<uint8> EmitMoppLeafHk550(
    const std::vector<MoppPrimitive> &primitives, size_t id) {
  std::vector<uint8> code;
  code.reserve(5);
  EncodeTerminal(primitives[id].key, code);
  return code;
}

std::vector<uint8> EmitMoppNodeHk550(
    const std::vector<MoppPrimitive> &primitives, std::vector<size_t> ids) {
  if (ids.empty()) {
    return {0x00};
  }

  if (ids.size() == 1) {
    return EmitMoppLeafHk550(primitives, ids.front());
  }

  size_t axis = 0;
  int bestSpan = -1;
  for (size_t i = 0; i < 3; i++) {
    uint8 minValue = 255;
    uint8 maxValue = 0;
    for (size_t id : ids) {
      const auto &primitive = primitives[id];
      const uint8 center =
          static_cast<uint8>((static_cast<uint16>(primitive.qmin[i]) +
                              static_cast<uint16>(primitive.qmax[i])) /
                             2);
      minValue = (std::min)(minValue, center);
      maxValue = (std::max)(maxValue, center);
    }

    const int span = static_cast<int>(maxValue) - static_cast<int>(minValue);
    if (span > bestSpan) {
      bestSpan = span;
      axis = i;
    }
  }

  std::sort(ids.begin(), ids.end(), [&](size_t left, size_t right) {
    const auto &l = primitives[left];
    const auto &r = primitives[right];
    const uint16 lc = static_cast<uint16>(l.qmin[axis]) +
                      static_cast<uint16>(l.qmax[axis]);
    const uint16 rc = static_cast<uint16>(r.qmin[axis]) +
                      static_cast<uint16>(r.qmax[axis]);
    if (lc == rc) {
      return l.key < r.key;
    }
    return lc < rc;
  });

  size_t leftCount = (std::min)(ids.size() / 2, static_cast<size_t>(2400));
  std::vector<size_t> leftIds;
  std::vector<size_t> rightIds;
  std::vector<uint8> left;
  std::vector<uint8> right;

  do {
    leftIds.assign(ids.begin(), ids.begin() + leftCount);
    rightIds.assign(ids.begin() + leftCount, ids.end());
    left = EmitMoppNodeHk550(primitives, leftIds);
    if (left.size() <= 0xffff || leftCount == 1) {
      break;
    }
    leftCount = (std::max)(static_cast<size_t>(1), leftCount / 2);
  } while (true);

  right = EmitMoppNodeHk550(primitives, rightIds);

  uint8 leftMax = 0;
  uint8 rightMin = 254;
  for (size_t id : leftIds) {
    leftMax = (std::max)(leftMax, primitives[id].qmax[axis]);
  }
  for (size_t id : rightIds) {
    rightMin = (std::min)(rightMin, primitives[id].qmin[axis]);
  }

  const uint8 leftCut = static_cast<uint8>((std::min)(
      255, static_cast<int>(leftMax) + 1));
  const uint8 rightCut = rightMin;
  const uint8 offsetHigh = (std::max)(leftCut, rightCut);
  const uint8 offsetLow = (std::min)(leftCut, rightCut);

  std::vector<uint8> code;
  EmitMoppBounds(primitives, ids, code);
  if (left.size() <= 0xff) {
    code.reserve(13 + left.size() + right.size());
    code.emplace_back(MoppSplitCommand(axis));
    code.emplace_back(offsetHigh);
    code.emplace_back(offsetLow);
    code.emplace_back(static_cast<uint8>(left.size()));
  } else {
    code.reserve(16 + left.size() + right.size());
    code.emplace_back(MoppSplitJumpCommand(axis));
    code.emplace_back(offsetHigh);
    code.emplace_back(offsetLow);
    code.emplace_back(uint8{});
    code.emplace_back(uint8{});
    code.emplace_back(static_cast<uint8>(left.size() >> 8));
    code.emplace_back(static_cast<uint8>(left.size()));
  }
  code.insert(code.end(), left.begin(), left.end());
  code.insert(code.end(), right.begin(), right.end());
  return code;
}

std::vector<uint8> EmitMoppNode(const std::vector<MoppPrimitive> &primitives,
                                std::vector<size_t> ids, size_t depth) {
  if (ids.empty()) {
    return {0x00};
  }

  if (ids.size() == 1) {
    std::vector<uint8> terminal;
    EncodeTerminal(primitives[ids.front()].key, terminal);
    return terminal;
  }

  size_t axis = depth % 3;
  int bestSpan = -1;
  for (size_t i = 0; i < 3; i++) {
    uint8 minValue = 255;
    uint8 maxValue = 0;
    for (size_t id : ids) {
      const auto &primitive = primitives[id];
      const uint8 center =
          static_cast<uint8>((static_cast<uint16>(primitive.qmin[i]) +
                              static_cast<uint16>(primitive.qmax[i])) /
                             2);
      minValue = (std::min)(minValue, center);
      maxValue = (std::max)(maxValue, center);
    }

    const int span = static_cast<int>(maxValue) - static_cast<int>(minValue);
    if (span > bestSpan) {
      bestSpan = span;
      axis = i;
    }
  }

  const size_t mid = ids.size() / 2;
  std::sort(ids.begin(), ids.end(), [&](size_t left, size_t right) {
    const auto &l = primitives[left];
    const auto &r = primitives[right];
    const uint16 lc =
        static_cast<uint16>(l.qmin[axis]) + static_cast<uint16>(l.qmax[axis]);
    const uint16 rc =
        static_cast<uint16>(r.qmin[axis]) + static_cast<uint16>(r.qmax[axis]);
    if (lc == rc) {
      return l.key < r.key;
    }
    return lc < rc;
  });

  std::vector<size_t> leftIds(ids.begin(), ids.begin() + mid);
  std::vector<size_t> rightIds(ids.begin() + mid, ids.end());
  if (leftIds.empty() || rightIds.empty()) {
    std::vector<uint8> terminal;
    EncodeTerminal(primitives[ids.front()].key, terminal);
    return terminal;
  }

  uint8 leftMax = 0;
  uint8 rightMin = 254;
  for (size_t id : leftIds) {
    leftMax = (std::max)(leftMax, primitives[id].qmax[axis]);
  }
  for (size_t id : rightIds) {
    rightMin = (std::min)(rightMin, primitives[id].qmin[axis]);
  }

  const uint8 offsetHigh =
      static_cast<uint8>((std::min)(255, static_cast<int>(leftMax) + 1));
  const uint8 offsetLow = rightMin;
  std::vector<uint8> left = EmitMoppNode(primitives, std::move(leftIds),
                                         depth + 1);
  std::vector<uint8> right = EmitMoppNode(primitives, std::move(rightIds),
                                          depth + 1);

  std::vector<uint8> code;
  if (left.size() <= 0xffff) {
    code.reserve(7 + left.size() + right.size());
    code.emplace_back(MoppSplitJumpCommand(axis));
    code.emplace_back(offsetHigh);
    code.emplace_back(offsetLow);
    code.emplace_back(uint8{});
    code.emplace_back(uint8{});
    code.emplace_back(static_cast<uint8>(left.size() >> 8));
    code.emplace_back(static_cast<uint8>(left.size()));
    code.insert(code.end(), left.begin(), left.end());
    code.insert(code.end(), right.begin(), right.end());
    return code;
  }

  code.reserve(17 + left.size() + right.size());
  code.emplace_back(MoppSplitJumpCommand(axis));
  code.emplace_back(offsetHigh);
  code.emplace_back(offsetLow);
  code.emplace_back(uint8{});
  code.emplace_back(uint8{});
  code.emplace_back(uint8{});
  code.emplace_back(static_cast<uint8>(5));
  EmitJump32(code, 5);
  EmitJump32(code, static_cast<uint32>(left.size()));
  code.insert(code.end(), left.begin(), left.end());
  code.insert(code.end(), right.begin(), right.end());
  return code;
}

GeneratedMopp BuildMoppCode(const hkpShape *childShape) {
  std::vector<MoppPrimitive> primitives;
  GatherMoppPrimitives(childShape, primitives);

  GeneratedMopp out;
  out.offset = BuildMoppOffset(childShape);
  if (primitives.empty()) {
    return out;
  }

  QuantizeMoppPrimitives(primitives, out.offset);

  std::vector<size_t> ids(primitives.size());
  for (size_t i = 0; i < ids.size(); i++) {
    ids[i] = i;
  }

  out.code = EmitMoppNode(primitives, std::move(ids), 0);
  out.code.insert(out.code.end(), 3, 0xcd);
  return out;
}

GeneratedMopp BuildMoppCodeHk550(const hkpShape *childShape) {
  std::vector<MoppPrimitive> primitives;
  GatherMoppPrimitives(childShape, primitives);

  GeneratedMopp out;
  out.offset = BuildMoppOffset(childShape);
  if (primitives.empty()) {
    return out;
  }

  QuantizeMoppPrimitives(primitives, out.offset);

  std::vector<size_t> ids(primitives.size());
  for (size_t i = 0; i < ids.size(); i++) {
    ids[i] = i;
  }

  out.code = EmitMoppNodeHk550(primitives, std::move(ids));
  return out;
}

std::unordered_map<const hkpMoppCode *, GeneratedMopp> &GeneratedMoppCode() {
  static std::unordered_map<const hkpMoppCode *, GeneratedMopp> cache;
  return cache;
}
}

struct hkpRawCollisionBytes {
  virtual const char *GetRawCollisionData() const = 0;
  virtual CRule GetRawCollisionRule() const = 0;
  virtual ~hkpRawCollisionBytes() = default;
};

template <class C> struct hkpMidBase : C, hkpRawCollisionBytes {
  char *data = nullptr;

  explicit hkpMidBase(CRule) {}

  void SetDataPointer(void *ptr) override { data = static_cast<char *>(ptr); }
  const void *GetPointer() const override { return data; }
  const char *GetRawCollisionData() const override { return data; }
  CRule GetRawCollisionRule() const override { return this->rule; }
  void SwapEndian() override {}

  auto DataNeedsEndianSwap() const { return RequiresEndianSwap(this->header); }
};

const hkpRawCollisionBytes *RawCollision(const IhkVirtualClass *value) {
  return dynamic_cast<const hkpRawCollisionBytes *>(value);
}

template <class C> const hkpRawCollisionBytes *RawCollision(const C *value) {
  return dynamic_cast<const hkpRawCollisionBytes *>(value);
}

float RawFloatField(const IhkVirtualClass *value, size_t offset,
                    float fallback) {
  const auto *raw = RawCollision(value);
  if (!raw || raw->GetRawCollisionRule().x64) {
    return fallback;
  }
  const char *data = raw->GetRawCollisionData();
  return data ? ReadValue<float>(data, offset, ValueNeedsEndianSwap(value))
              : fallback;
}

Vector4A16 RawVectorField(const IhkVirtualClass *value, size_t offset,
                          const Vector4A16 &fallback) {
  const auto *raw = RawCollision(value);
  if (!raw || raw->GetRawCollisionRule().x64) {
    return fallback;
  }
  const char *data = raw->GetRawCollisionData();
  return data ? ReadValue<Vector4A16>(data, offset, ValueNeedsEndianSwap(value))
              : fallback;
}

template <class C> struct HkpSaver {
  CRule rule{};
  const C *in{};

  HkpSaver(CRule rule_, const C *in_) : rule(rule_), in(in_) {
  }

  std::vector<char> Fixed(size_t fixedSize) const {
    const auto *raw = RawCollision(in);
    return FixedObject(fixedSize, raw ? raw->GetRawCollisionData() : nullptr,
                       raw ? raw->GetRawCollisionRule() : CRule{}, rule);
  }
};

struct hkpPhysicsDataSaver : HkpSaver<hkpPhysicsData> {
  using HkpSaver::HkpSaver;

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    auto fixed = Fixed(kPhysicsDataFixedSize);
    WriteArrayHeader(fixed, 12, in->GetNumSystems(), rule.version);

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);
    SavePointerArray(wr, fixups, begin, 12, in->GetNumSystems(),
                     [&](size_t i) { return in->GetSystem(i); });
  }
};

struct hkpPhysicsSystemSaver : HkpSaver<hkpPhysicsSystem> {
  using HkpSaver::HkpSaver;

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    auto fixed = Fixed(kPhysicsSystemFixedSize);
    WriteArrayHeader(fixed, 8, in->GetNumRigidBodies(), rule.version);
    WriteArrayHeader(fixed, 20, 0, rule.version);
    WriteArrayHeader(fixed, 32, 0, rule.version);
    WriteArrayHeader(fixed, 44, 0, rule.version);
    WriteField<uint32>(fixed, 56, 0);
    WriteField<uint32>(fixed, 60, 0);
    WriteField<uint8>(fixed, 64, in->GetActive() ? 1 : 0);

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);
    SavePointerArray(wr, fixups, begin, 8, in->GetNumRigidBodies(),
                     [&](size_t i) { return in->GetRigidBody(i); });
    SaveString(wr, fixups, begin, 56, in->GetName());
  }
};

struct hkpRigidBodySaver : HkpSaver<hkpRigidBody> {
  using HkpSaver::HkpSaver;

  std::vector<char> FixedRigidBody() const {
    auto fixed = Fixed(RigidBodyFixedSize(rule.version));
    const auto *raw = RawCollision(in);
    if (!raw || rule.version != HK550 || rule.x64) {
      return fixed;
    }

    const CRule rawRule = raw->GetRawCollisionRule();
    const char *src = raw->GetRawCollisionData();
    if (!src || rawRule.x64 || RigidBodyFixedSize(rawRule.version) != 544) {
      return fixed;
    }

    auto copyRange = [&](size_t dst, size_t srcOff, size_t size) {
      if (dst + size <= fixed.size()) {
        std::memcpy(fixed.data() + dst, src + srcOff, size);
      }
    };

    copyRange(0, 0, 88);
    copyRange(88, 92, 16);
    copyRange(104, 108, 8);
    copyRange(112, 120, 16);
    copyRange(128, 140, 16);
    copyRange(144, 164, 64);
    copyRange(208, 224, 304);

    WriteField<uint16>(fixed, BroadPhaseHandleOffset(false) + 6, 3);
    WriteField<uint32>(fixed, BroadPhaseHandleOffset(false) + 12, 1);
    WriteField<uint32>(fixed, 388, 0);
    WriteField<float>(fixed, 392, 0.05f);
    copyRange(396, 410, 4);
    WriteField<uint16>(fixed, 494, 0);
    return fixed;
  }

  std::vector<hkpRigidBodyProperty> Properties() const {
    std::vector<hkpRigidBodyProperty> output;
    output.reserve(in->GetNumProperties());
    for (size_t i = 0; i < in->GetNumProperties(); i++) {
      output.emplace_back(in->GetProperty(i));
    }
    return output;
  }

  void WriteMotion(std::vector<char> &fixed) const {
    const size_t motion = RigidBodyMotionOffset(rule.version);
    const size_t state = motion + 16;
    const size_t sweptTransform = state + 64;

    WriteField<uint8>(fixed, motion + 8, in->GetMotionType());
    WriteField<uint8>(fixed, motion + 9, in->GetDeactivationIntegrateCounter());
    WriteField<uint16>(fixed, motion + 10,
                       in->GetDeactivationNumInactiveFrames(0));
    WriteField<uint16>(fixed, motion + 12,
                       in->GetDeactivationNumInactiveFrames(1));
    WriteMatrixField(fixed, state, in->GetTransform());

    WriteVectorField(fixed, sweptTransform, in->GetCenterOfMassWorld());
    WriteVectorField(fixed, sweptTransform + 16, in->GetCenterOfMassWorld());
    WriteVectorField(fixed, sweptTransform + 32, in->GetRotation());
    WriteVectorField(fixed, sweptTransform + 48, in->GetRotation());
    WriteVectorField(fixed, sweptTransform + 64, in->GetCenterOfMassLocal());
    WriteVectorField(fixed, state + 144,
                     Vector4A16(0.0f, 0.0f, 0.0f, 0.0f));
    WriteField<float>(fixed, state + 160, in->GetMotionObjectRadius());

    if (rule.version == HK550) {
      WriteField<float>(fixed, state + 164, in->GetLinearDamping());
      WriteField<float>(fixed, state + 168, in->GetAngularDamping());
      WriteField<uint8>(fixed, state + 172, in->GetMaxLinearVelocity());
      WriteField<uint8>(fixed, state + 173, in->GetMaxAngularVelocity());
      WriteField<uint8>(fixed, state + 174, in->GetDeactivationClass());
    } else {
      WriteField<uint16>(fixed, state + 164,
                         FloatToHalfBits(in->GetLinearDamping()));
      WriteField<uint16>(fixed, state + 166,
                         FloatToHalfBits(in->GetAngularDamping()));
      WriteField<uint16>(fixed, state + 168,
                         FloatToHalfBits(in->GetTimeFactor()));
      WriteField<uint8>(fixed, state + 170, in->GetMaxLinearVelocity());
      WriteField<uint8>(fixed, state + 171, in->GetMaxAngularVelocity());
      WriteField<uint8>(fixed, state + 172, in->GetDeactivationClass());
    }

    WriteVectorField(fixed, motion + 192, in->GetInertiaAndMassInv());
    WriteVectorField(fixed, motion + 208, in->GetLinearVelocity());
    WriteVectorField(fixed, motion + 224, in->GetAngularVelocity());
    WriteVectorField(fixed, motion + 240,
                     Vector4A16(0.0f, 0.0f, 0.0f, 0.0f));
    WriteVectorField(fixed, motion + 256,
                     Vector4A16(0.0f, 0.0f, 0.0f, 0.0f));
    WriteField<uint32>(fixed, motion + 272, 0);
    WriteField<uint32>(fixed, motion + 276, 0);
    WriteField<uint32>(fixed, motion + 280, 0);
    WriteField<uint16>(fixed, motion + 284, in->GetSavedQualityTypeIndex());

    if (rule.version != HK550) {
      WriteField<uint16>(fixed, motion + 286,
                         FloatToHalfBits(in->GetGravityFactor()));
    }
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    auto fixed = FixedRigidBody();
    const auto properties = Properties();

    WriteField<uint32>(fixed, 0, 0);
    WriteField<uint32>(fixed, 16, 0);
    WriteField<uint32>(fixed, 20, in->GetShapeKey());
    WriteField<uint32>(fixed, 24, 0);

    const size_t broadPhase = BroadPhaseHandleOffset(false);
    WriteField<uint8>(fixed, broadPhase + 4, 1);
    WriteField<uint16>(fixed, broadPhase + 6,
                       StoredQualityType(rule.version,
                                         in->GetObjectQualityType()));
    if (rule.version == HK550) {
      WriteField<uint32>(fixed, broadPhase + 12, 1);
      WriteField<uint32>(fixed, RigidBodyHk550AllowedPenetrationOffset(),
                         0x7f7fffeeu);
      WriteField<uint32>(fixed, RigidBodyHk550MultiThreadCheckOffset(),
                         0x80000000u);
    }
    WriteField<uint32>(fixed, broadPhase + 8, in->GetCollisionFilterInfo());

    WriteField<uint32>(fixed, RigidBodyNameOffset(rule.version), 0);
    WriteArrayHeader(fixed, RigidBodyPropertiesOffset(rule.version, false),
                     properties.size(), rule.version);

    const size_t material = RigidBodyMaterialOffset(rule.version, false);
    WriteField<uint8>(fixed, material, in->GetMaterialResponseType());
    WriteField<float>(fixed, material + 4, in->GetMaterialFriction());
    WriteField<float>(fixed, material + 8, in->GetMaterialRestitution());

    WriteMotion(fixed);

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);

    if (in->GetShape()) {
      fixups.locals.emplace_back(begin + 16, in->GetShape());
    }

    SaveString(wr, fixups, begin, RigidBodyNameOffset(rule.version),
               in->GetName());
    SaveRigidBodyPropertyArray(wr, fixups, begin,
                               RigidBodyPropertiesOffset(rule.version, false),
                               properties.data(), properties.size());
  }
};

struct hkpShapeSaver : HkpSaver<hkpShape> {
  using HkpSaver::HkpSaver;

  void Save(BinWritterRef_e wr, hkFixups &) const {
    auto fixed = Fixed(16);
    WriteField<uint32>(fixed, 12, in->GetShapeType());
    WriteBuffer(wr, fixed);
  }
};

struct hkpMoppCodeSaver : HkpSaver<hkpMoppCode> {
  using HkpSaver::HkpSaver;

  std::vector<uint8> Data() const {
    auto found = GeneratedMoppCode().find(in);
    if (found != GeneratedMoppCode().end()) {
      return found->second.code;
    }

    if (const uint8 *data = in->GetData(); data && in->GetDataSize()) {
      return std::vector<uint8>(data, data + in->GetDataSize());
    }

    return {};
  }

  Vector4A16 Offset() const {
    auto found = GeneratedMoppCode().find(in);
    if (found != GeneratedMoppCode().end()) {
      return found->second.offset;
    }

    return in->GetOffset();
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    auto fixed = Fixed(MoppCodeFixedSize(rule.version));
    const auto data = Data();
    WriteVectorField(fixed, MoppCodeInfoOffset(rule.version), Offset());
    WriteArrayHeader(fixed, MoppCodeDataOffset(rule.version), data.size(),
                     rule.version);
    WriteField<uint8>(fixed, MoppCodeBuildTypeOffset(rule.version), 1);

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);
    SaveValueArray(wr, fixups, begin, MoppCodeDataOffset(rule.version),
                   data.data(), data.size(), 1);
  }
};

struct hkpMoppBvTreeShapeSaver : HkpSaver<hkpMoppBvTreeShape> {
  using HkpSaver::HkpSaver;

  hkpMoppBvTreeShapeSaver(CRule rule_, const hkpMoppBvTreeShape *in_)
      : HkpSaver(rule_, in_) {
    const auto *code = in->GetCode();
    if (code &&
        (rule.version == HK550 || !code->GetData() || !code->GetDataSize())) {
      auto generated = rule.version == HK550
                           ? BuildMoppCodeHk550(in->GetChildShape())
                           : BuildMoppCode(in->GetChildShape());
      if (!generated.code.empty()) {
        GeneratedMoppCode()[code] = std::move(generated);
      }
    }
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    auto fixed = Fixed(kMoppBvTreeShapeFixedSize);
    WriteField<uint32>(fixed, 12, in->GetShapeType());
    WriteField<uint32>(fixed, MoppCodePointerOffset(rule.version), 0);
    WriteField<uint32>(fixed, 52, 0);

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);
    if (in->GetCode()) {
      fixups.locals.emplace_back(begin + MoppCodePointerOffset(rule.version),
                                 in->GetCode());
    }
    if (in->GetChildShape()) {
      fixups.locals.emplace_back(begin + 52, in->GetChildShape());
    }
  }
};

struct hkpStaticCompoundShapeSaver : HkpSaver<hkpStaticCompoundShape> {
  using HkpSaver::HkpSaver;

  std::vector<hkpStaticCompoundShapeInstance> Instances() const {
    std::vector<hkpStaticCompoundShapeInstance> output;
    output.reserve(in->GetNumInstances());
    for (size_t i = 0; i < in->GetNumInstances(); i++) {
      auto instance = in->GetInstance(i);
      if (instance.shape) {
        output.emplace_back(instance);
      }
    }
    return output;
  }

  std::vector<Bounds> InstanceBounds(
      const std::vector<hkpStaticCompoundShapeInstance> &instances) const {
    std::vector<Bounds> output;
    output.reserve(instances.size());
    for (const auto &instance : instances) {
      Bounds child;
      AddShapeAabb(instance.shape, child);
      Bounds transformed = TransformBounds(child, instance);
      ExpandBounds(transformed, 0.01f);
      output.emplace_back(transformed);
    }
    return output;
  }

  std::vector<char> InstanceRecords(
      const std::vector<hkpStaticCompoundShapeInstance> &instances) const {
    std::vector<char> output(instances.size() * kStaticCompoundInstanceSize, 0);
    for (size_t i = 0; i < instances.size(); i++) {
      const auto &instance = instances[i];
      const size_t base = i * kStaticCompoundInstanceSize;
      const uint32 bits = StaticCompoundInstanceBits(instance);

      WriteField<float>(output, base + 0, instance.translation._arr[0]);
      WriteField<float>(output, base + 4, instance.translation._arr[1]);
      WriteField<float>(output, base + 8, instance.translation._arr[2]);
      WriteField<float>(output, base + 12, PackedInt24W(bits));
      WriteVectorField(output, base + 16, instance.rotation);
      WriteField<float>(output, base + 32, instance.scale._arr[0]);
      WriteField<float>(output, base + 36, instance.scale._arr[1]);
      WriteField<float>(output, base + 40, instance.scale._arr[2]);
      WriteField<float>(output, base + 44, PackedInt24W(0));
      WriteField<uint32>(output, base + 48, 0);
      WriteField<uint32>(output, base + 52, instance.filterInfo);
      WriteField<uint32>(output, base + 56, instance.childFilterInfoMask);
      WriteField<uint32>(output, base + 60, instance.userData);
    }
    return output;
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    auto instances = Instances();
    auto leafBounds = InstanceBounds(instances);
    std::vector<size_t> ids(leafBounds.size());
    for (size_t i = 0; i < ids.size(); i++) {
      ids[i] = i;
    }

    std::vector<StaticTreeNode> nodes;
    if (!ids.empty()) {
      BuildStaticTreeNodes(leafBounds, std::move(ids), nodes);
    }

    auto fixed = Fixed(kStaticCompoundShapeFixedSize);
    const Bounds treeBounds = nodes.empty() ? Bounds{} : nodes.front().bounds;

    WriteField<uint32>(fixed, 8, 0x00000400u);
    WriteField<uint32>(fixed, 16, 2);
    WriteField<uint8>(fixed, 24, 14);
    WriteArrayHeader(fixed, 32, instances.size(), rule.version);
    WriteArrayHeader(fixed, 44, 0, rule.version);
    WriteArrayHeader(fixed, 64, nodes.size(), rule.version);
    if (treeBounds.count) {
      WriteVectorField(fixed, 80,
                       Vector4A16(treeBounds.min[0], treeBounds.min[1],
                                  treeBounds.min[2], 0.0f));
      WriteVectorField(fixed, 96,
                       Vector4A16(treeBounds.max[0], treeBounds.max[1],
                                  treeBounds.max[2], 0.0f));
    }

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);

    auto instanceRecords = InstanceRecords(instances);
    if (!instanceRecords.empty()) {
      wr.ApplyPadding();
      const size_t instancesBegin = wr.Tell();
      fixups.locals.emplace_back(begin + 32, instancesBegin);
      WriteBuffer(wr, instanceRecords);
      for (size_t i = 0; i < instances.size(); i++) {
        fixups.locals.emplace_back(
            instancesBegin + (i * kStaticCompoundInstanceSize) + 48,
            instances[i].shape);
      }
    }

    auto tree = BuildAabb6TreeBuffer(nodes);
    if (!tree.empty()) {
      wr.ApplyPadding();
      fixups.locals.emplace_back(begin + 64, wr.Tell());
      WriteBuffer(wr, tree);
    }
  }
};

struct hkpStorageExtendedMeshShapeSaver
    : HkpSaver<hkpStorageExtendedMeshShape> {
  using HkpSaver::HkpSaver;

  Bounds ComputeBounds() const {
    Bounds bounds;
    for (auto subpart : in->MeshSubparts()) {
      if (!subpart || !subpart->GetVertices()) {
        continue;
      }
      const auto *vertices = subpart->GetVertices();
      for (size_t i = 0; i < subpart->GetNumVertices(); i++) {
        bounds.Add(vertices[i]);
      }
    }
    return bounds;
  }

  std::vector<char> TriangleSubparts() const {
    std::vector<char> output;
    output.reserve(in->GetNumMeshSubparts() *
                   TriangleSubpartFixedSize(rule.version));
    for (auto subpart : in->MeshSubparts()) {
      auto record = MakeTriangleSubpart(subpart, rule.version);
      AppendBuffer(output, record);
      BufferFields().erase(&record);
    }
    BufferFields().erase(&output);
    return output;
  }

  std::vector<char> ShapeSubparts() const {
    std::vector<char> output;
    output.reserve(in->GetNumShapeSubparts() *
                   ShapeSubpartFixedSize(rule.version));
    for (auto subpart : in->ShapeSubparts()) {
      auto record = MakeShapeSubpart(subpart ? subpart->GetNumShapes() : 0);
      record.resize(ShapeSubpartFixedSize(rule.version), 0);
      AppendBuffer(output, record);
      BufferFields().erase(&record);
    }
    BufferFields().erase(&output);
    return output;
  }

  std::vector<uint16> WeldingInfo() const {
    if (const auto *raw = RawCollision(in)) {
      if (const char *rawData = raw->GetRawCollisionData()) {
        const CRule rawRule = raw->GetRawCollisionRule();
        const auto swapEndian = ValueNeedsEndianSwap(in);
        const auto *rawClass = dynamic_cast<const hkVirtualClass *>(in);
        const IhkPackFile *rawHeader = rawClass ? rawClass->header : nullptr;
        const ArrayViewWithMode candidates[] = {
            {ReadArray(rawData, StorageWeldingArrayOffset(rawRule.version),
                       rawRule.x64, swapEndian),
             rawRule.x64, swapEndian},
            {ReadArray(rawData, StorageWeldingArrayOffset(rawRule.version),
                       !rawRule.x64, swapEndian),
             !rawRule.x64, swapEndian},
            {ReadArray(rawData, StorageWeldingArrayOffset(rawRule.version),
                       rawRule.x64, !swapEndian),
             rawRule.x64, !swapEndian},
            {ReadArray(rawData, StorageWeldingArrayOffset(rawRule.version),
                       !rawRule.x64, !swapEndian),
             !rawRule.x64, !swapEndian},
        };

        for (const auto &rawWelding : candidates) {
          if (!ArrayViewReasonable(rawWelding.view, 1u << 24) ||
              !ArrayViewCoveredByHeader(rawWelding, sizeof(uint16), rawHeader)) {
            continue;
          }

          std::vector<uint16> output;
          output.reserve(rawWelding.view.count);
          for (size_t i = 0; i < rawWelding.view.count; i++) {
            output.emplace_back(ReadValue<uint16>(
                rawWelding.view.data + i * sizeof(uint16), 0,
                rawWelding.swapEndian));
          }
          return output;
        }
        return {};
      }
    }

    size_t numTriangles = 0;
    for (auto subpart : in->MeshSubparts()) {
      if (subpart) {
        numTriangles += subpart->GetNumTriangles();
      }
    }
    return std::vector<uint16>(numTriangles, 0);
  }

  struct RawStorageContent {
    std::vector<char> fixed;
    std::vector<char> triangles;
    std::vector<char> shapes;
    std::vector<char> welding;
    uint32 numTriangles{};
    uint32 numShapes{};
    uint32 numWelding{};
  };

  const hkpRawCollisionBytes *RawMatchingOldX86Storage() const {
    if (rule.x64) {
      return nullptr;
    }

    const auto *raw = RawCollision(in);
    if (!raw || !raw->GetRawCollisionData()) {
      return nullptr;
    }

    const CRule rawRule = raw->GetRawCollisionRule();
    if (rawRule.version != rule.version || rawRule.x64) {
      return nullptr;
    }

    return raw;
  }

  std::vector<char> CopyRawArray(const ArrayView &view,
                                 size_t elementSize) const {
    size_t totalSize = 0;
    if (!MultiplySize(static_cast<size_t>(view.count), elementSize,
                      totalSize)) {
      return {};
    }
    if (!totalSize) {
      return {};
    }
    if (!view.data) {
      return {};
    }

    return std::vector<char>(view.data, view.data + totalSize);
  }

  std::unique_ptr<RawStorageContent> BuildRawStorage() const {
    const auto *raw = RawMatchingOldX86Storage();
    if (!raw) {
      return nullptr;
    }

    const char *rawData = raw->GetRawCollisionData();
    const auto *rawClass = dynamic_cast<const hkVirtualClass *>(in);
    const IhkPackFile *rawHeader = rawClass ? rawClass->header : nullptr;
    if (!PointerCoveredByHeader(rawHeader, rawData,
                           StorageMeshShapeFixedSize(rule.version))) {
      return nullptr;
    }

    const ArrayView rawTriangles =
        ReadArrayX86(rawData, StorageTrianglesArrayOffset(rule.version));
    const ArrayView rawShapes =
        ReadArrayX86(rawData, StorageShapeRecordsArrayOffset(rule.version));
    const ArrayView rawWelding =
        ReadArrayX86(rawData, StorageWeldingArrayOffset(rule.version));

    size_t rawTrianglesSize = 0;
    size_t rawShapesSize = 0;
    size_t rawWeldingSize = 0;
    if (!MultiplySize(static_cast<size_t>(rawTriangles.count),
                      TriangleSubpartFixedSize(rule.version),
                      rawTrianglesSize) ||
        !MultiplySize(static_cast<size_t>(rawShapes.count),
                      ShapeSubpartFixedSize(rule.version), rawShapesSize) ||
        !MultiplySize(static_cast<size_t>(rawWelding.count), sizeof(uint16),
                      rawWeldingSize)) {
      return nullptr;
    }

    if ((rawTrianglesSize &&
         !PointerCoveredByHeader(rawHeader, rawTriangles.data, rawTrianglesSize)) ||
        (rawShapesSize &&
         !PointerCoveredByHeader(rawHeader, rawShapes.data, rawShapesSize)) ||
        (rawWeldingSize &&
         !PointerCoveredByHeader(rawHeader, rawWelding.data, rawWeldingSize))) {
      return nullptr;
    }

    auto content = std::make_unique<RawStorageContent>();
    content->triangles =
        CopyRawArray(rawTriangles, TriangleSubpartFixedSize(rule.version));
    content->shapes =
        CopyRawArray(rawShapes, ShapeSubpartFixedSize(rule.version));
    content->welding = CopyRawArray(rawWelding, sizeof(uint16));
    content->numTriangles = rawTriangles.count;
    content->numShapes = rawShapes.count;
    content->numWelding = rawWelding.count;

    if ((content->numTriangles && content->triangles.empty()) ||
        (content->numShapes && content->shapes.empty()) ||
        (content->numWelding && content->welding.empty())) {
      return nullptr;
    }

    switch (rule.version) {
    case HK550:
      for (size_t i = 0; i < content->numTriangles; i++) {
        ClearOldTriangleSubpartPointers(content->triangles,
                                        i * kTriangleSubpartFixedSize);
      }

      for (size_t i = 0; i < content->numShapes; i++) {
        const size_t record = i * kShapeSubpartFixedSize;
        const auto *subpart =
            i < in->GetNumShapeSubparts() ? in->GetShapeSubpart(i) : nullptr;
        ClearOldShapeSubpartPointers(content->shapes, record);
        WriteSimpleArrayHeader(content->shapes, record + 16,
                               subpart ? subpart->GetNumShapes() : 0);
      }
      break;
    default:
      break;
    }

    content->fixed = Fixed(StorageMeshShapeFixedSize(rule.version));
    if (rule.version == HK550) {
      WriteSimpleArrayHeader(content->fixed,
                             StorageTrianglesArrayOffset(rule.version),
                             content->numTriangles);
      WriteSimpleArrayHeader(content->fixed,
                             StorageShapeRecordsArrayOffset(rule.version),
                             content->numShapes);
    } else {
      WriteArrayHeader(content->fixed,
                       StorageTrianglesArrayOffset(rule.version),
                       content->numTriangles, rule.version);
      WriteArrayHeader(content->fixed,
                       StorageShapeRecordsArrayOffset(rule.version),
                       content->numShapes, rule.version);
    }
    WriteArrayHeader(content->fixed, StorageWeldingArrayOffset(rule.version),
                     content->numWelding, rule.version);
    switch (rule.version) {
    case HK550:
      ClearOldTriangleSubpartPointers(content->fixed, 112);
      break;
    default:
      break;
    }
    WriteArrayHeader(content->fixed, StorageMeshArrayOffset(rule.version),
                     in->GetNumMeshSubparts(), rule.version);
    WriteArrayHeader(content->fixed, StorageShapeArrayOffset(rule.version),
                     in->GetNumShapeSubparts(), rule.version);
    return content;
  }

  void SaveRawStorage(BinWritterRef_e wr, hkFixups &fixups,
                      const RawStorageContent &content) const {
    const size_t begin = wr.Tell();
    WriteBuffer(wr, content.fixed);

    if (!content.triangles.empty()) {
      wr.ApplyPadding();
      fixups.locals.emplace_back(
          begin + StorageTrianglesArrayOffset(rule.version), wr.Tell());
      WriteBuffer(wr, content.triangles);
    }

    size_t shapeRecordsBegin = 0;
    if (!content.shapes.empty()) {
      wr.ApplyPadding();
      shapeRecordsBegin = wr.Tell();
      fixups.locals.emplace_back(
          begin + StorageShapeRecordsArrayOffset(rule.version),
          shapeRecordsBegin);
      WriteBuffer(wr, content.shapes);
      SaveShapeSubpartChildArrays(wr, fixups, shapeRecordsBegin,
                                  content.numShapes);
    }

    SaveValueArray(wr, fixups, begin, StorageWeldingArrayOffset(rule.version),
                   content.welding.data(), content.numWelding,
                   sizeof(uint16));
    SavePointerArray(wr, fixups, begin, StorageMeshArrayOffset(rule.version),
                     in->GetNumMeshSubparts(),
                     [&](size_t i) { return in->GetMeshSubpart(i); });
    SavePointerArray(wr, fixups, begin, StorageShapeArrayOffset(rule.version),
                     in->GetNumShapeSubparts(),
                     [&](size_t i) { return in->GetShapeSubpart(i); });
  }

  void SaveShapeSubpartChildArrays(BinWritterRef_e wr, hkFixups &fixups,
                                   size_t shapeRecordsBegin,
                                   size_t recordCount) const {
    const size_t numSubparts =
        (std::min)(in->GetNumShapeSubparts(), recordCount);
    for (size_t subpartIndex = 0; subpartIndex < numSubparts;
         subpartIndex++) {
      const auto *subpart = in->GetShapeSubpart(subpartIndex);
      const size_t numShapes = subpart ? subpart->GetNumShapes() : 0;
      if (!numShapes) {
        continue;
      }

      wr.ApplyPadding();
      fixups.locals.emplace_back(
          shapeRecordsBegin +
              (subpartIndex * ShapeSubpartFixedSize(rule.version)) + 16,
          wr.Tell());
      for (size_t shapeIndex = 0; shapeIndex < numShapes; shapeIndex++) {
        fixups.locals.emplace_back(wr.Tell(), subpart->GetShape(shapeIndex));
        wr.Skip(kCollisionPtrSize);
      }
    }
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    if (auto rawStorage = BuildRawStorage()) {
      SaveRawStorage(wr, fixups, *rawStorage);
      return;
    }

    auto fixed = Fixed(StorageMeshShapeFixedSize(rule.version));
    auto triangles = TriangleSubparts();
    auto shapes = ShapeSubparts();
    MarkTriangleSubpartArray(triangles, rule.version);
    MarkShapeSubpartArray(shapes, ShapeSubpartFixedSize(rule.version));
    const auto welding = WeldingInfo();
    const Bounds bounds = ComputeBounds();
    Vector4A16 halfExtents = bounds.HalfExtents();
    Vector4A16 center = bounds.Center();
    uint8 collectionFlags = 1;
    uint8 weldingType = 0;
    if (const auto *raw = RawCollision(in)) {
      const CRule rawRule = raw->GetRawCollisionRule();
      if (const char *rawData = raw->GetRawCollisionData();
          rawData && !rawRule.x64) {
        halfExtents = RawVectorField(
            in, StorageAabbHalfExtentsOffset(rawRule.version), halfExtents);
        center =
            RawVectorField(in, StorageAabbCenterOffset(rawRule.version), center);
        collectionFlags = static_cast<uint8>(rawData[20]);
        weldingType =
            static_cast<uint8>(rawData[StorageWeldingTypeOffset(rawRule.version)]);
      }
    }

    size_t cachedNumChildShapes = 0;
    for (auto subpart : in->MeshSubparts()) {
      if (subpart) {
        cachedNumChildShapes += subpart->GetNumTriangles();
      }
    }
    for (auto subpart : in->ShapeSubparts()) {
      if (subpart) {
        cachedNumChildShapes += subpart->GetNumShapes();
      }
    }

    WriteField<uint32>(fixed, 12, in->GetShapeType());
    WriteField<uint8>(fixed, 20, collectionFlags);
    if (rule.version == HK550) {
      WriteField<int32>(fixed, 24, 12);
      WriteVectorField(fixed, 32, Vector4A16(1.0f, 1.0f, 1.0f, 1.0f));
      WriteVectorField(fixed, 48, halfExtents);
      WriteVectorField(fixed, 64, center);
    } else {
      if (in->GetNumMeshSubparts()) {
        auto embedded = MakeTriangleSubpart(in->GetMeshSubpart(0),
                                            rule.version);
        CopyBuffer(fixed, 32, embedded);
        BufferFields().erase(&embedded);
      }
      WriteVectorField(fixed, StorageAabbHalfExtentsOffset(rule.version),
                       halfExtents);
      WriteVectorField(fixed, StorageAabbCenterOffset(rule.version), center);
      WriteField<int32>(fixed, 180, 12);
      WriteField<uint8>(fixed, StorageWeldingTypeOffset(rule.version),
                        weldingType);
      WriteField<uint32>(fixed, 224, 0);
      WriteField<int32>(fixed, 228, static_cast<int32>(cachedNumChildShapes));
      WriteField<float>(fixed, 232, 0.05f);
    }
    if (rule.version == HK550) {
      WriteSimpleArrayHeader(fixed, StorageTrianglesArrayOffset(rule.version),
                             in->GetNumMeshSubparts());
      WriteSimpleArrayHeader(fixed,
                             StorageShapeRecordsArrayOffset(rule.version),
                             in->GetNumShapeSubparts());
    } else {
      WriteArrayHeader(fixed, StorageTrianglesArrayOffset(rule.version),
                       in->GetNumMeshSubparts(), rule.version);
      WriteArrayHeader(fixed, StorageShapeRecordsArrayOffset(rule.version),
                       in->GetNumShapeSubparts(), rule.version);
    }
    WriteArrayHeader(fixed, StorageWeldingArrayOffset(rule.version),
                     welding.size(), rule.version);
    switch (rule.version) {
    case HK550:
      WriteField<uint8>(fixed, StorageWeldingTypeOffset(rule.version),
                        weldingType);

      if (in->GetNumMeshSubparts()) {
        auto embedded = MakeTriangleSubpart(in->GetMeshSubpart(0),
                                            rule.version);
        CopyBuffer(fixed, 112, embedded);
        BufferFields().erase(&embedded);
      }
      WriteField<float>(fixed, 176, 0.005f);
      break;
    default:
      break;
    }
    WriteArrayHeader(fixed, StorageMeshArrayOffset(rule.version),
                     in->GetNumMeshSubparts(), rule.version);
    WriteArrayHeader(fixed, StorageShapeArrayOffset(rule.version),
                     in->GetNumShapeSubparts(), rule.version);

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);

    if (!triangles.empty()) {
      wr.ApplyPadding();
      fixups.locals.emplace_back(
          begin + StorageTrianglesArrayOffset(rule.version), wr.Tell());
      WriteBuffer(wr, triangles);
    }

    size_t shapeRecordsBegin = 0;
    if (!shapes.empty()) {
      wr.ApplyPadding();
      shapeRecordsBegin = wr.Tell();
      fixups.locals.emplace_back(
          begin + StorageShapeRecordsArrayOffset(rule.version),
          shapeRecordsBegin);
      WriteBuffer(wr, shapes);
      SaveShapeSubpartChildArrays(wr, fixups, shapeRecordsBegin,
                                  in->GetNumShapeSubparts());
    }

    SaveValueArray(wr, fixups, begin, StorageWeldingArrayOffset(rule.version),
                   welding.data(), welding.size(), sizeof(uint16));
    SavePointerArray(wr, fixups, begin, StorageMeshArrayOffset(rule.version),
                     in->GetNumMeshSubparts(),
                     [&](size_t i) { return in->GetMeshSubpart(i); });
    SavePointerArray(wr, fixups, begin, StorageShapeArrayOffset(rule.version),
                     in->GetNumShapeSubparts(),
                     [&](size_t i) { return in->GetShapeSubpart(i); });
  }
};

struct hkpStorageExtendedMeshShapeMeshSubpartStorageSaver
    : HkpSaver<hkpStorageExtendedMeshShapeMeshSubpartStorage> {
  using HkpSaver::HkpSaver;

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    auto fixed = Fixed(MeshSubpartStorageFixedSize(rule.version));
    WriteArrayHeader(fixed, 8, in->GetNumVertices(), rule.version);
    if (MeshSubpartIndices8Offset(rule.version) != static_cast<size_t>(-1)) {
      WriteArrayHeader(fixed, MeshSubpartIndices8Offset(rule.version),
                       in->GetNumIndices8(), rule.version);
    }
    WriteArrayHeader(fixed, MeshSubpartIndices16Offset(rule.version),
                     in->GetNumIndices16(), rule.version);
    WriteArrayHeader(fixed, MeshSubpartIndices32Offset(rule.version),
                     in->GetNumIndices32(), rule.version);
    if (MeshSubpartIndices8Offset(rule.version) == static_cast<size_t>(-1)) {
      WriteArrayHeader(fixed, 44, 0, rule.version);
    }
    WriteArrayHeader(fixed, 56, 0, rule.version);
    WriteArrayHeader(fixed, 68, 0, rule.version);

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);
    SaveValueArray(wr, fixups, begin, 8, in->GetVertices(),
                   in->GetNumVertices(), sizeof(Vector4A16));
    if (MeshSubpartIndices8Offset(rule.version) != static_cast<size_t>(-1)) {
      SaveValueArray(wr, fixups, begin, MeshSubpartIndices8Offset(rule.version),
                     in->GetIndices8(), in->GetNumIndices8(), sizeof(uint8));
    }
    SaveValueArray(wr, fixups, begin, MeshSubpartIndices16Offset(rule.version),
                   in->GetIndices16(),
                   in->GetNumIndices16(), sizeof(uint16));
    SaveValueArray(wr, fixups, begin, MeshSubpartIndices32Offset(rule.version),
                   in->GetIndices32(),
                   in->GetNumIndices32(), sizeof(uint32));
  }
};

struct hkpStorageExtendedMeshShapeShapeSubpartStorageSaver
    : HkpSaver<hkpStorageExtendedMeshShapeShapeSubpartStorage> {
  using HkpSaver::HkpSaver;

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    auto fixed = Fixed(kShapeSubpartStorageFixedSize);
    WriteArrayHeader(fixed, 8, in->GetNumShapes(), rule.version);
    WriteArrayHeader(fixed, 20, 0, rule.version);
    WriteArrayHeader(fixed, 32, 0, rule.version);
    WriteArrayHeader(fixed, 44, 0, rule.version);

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);
    SavePointerArray(wr, fixups, begin, 8, in->GetNumShapes(),
                     [&](size_t i) { return in->GetShape(i); });
  }
};

struct hkpListShapeSaver : HkpSaver<hkpListShape> {
  using HkpSaver::HkpSaver;

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    auto fixed = Fixed(kListShapeFixedSize);
    Bounds bounds;
    for (size_t i = 0; i < in->GetNumChildren(); i++) {
      AddShapeAabb(in->GetChild(i), bounds);
    }
    Vector4A16 halfExtents = bounds.HalfExtents(0.05f);
    Vector4A16 center = bounds.Center();
    if (const auto *raw = RawCollision(in)) {
      const CRule rawRule = raw->GetRawCollisionRule();
      if (const char *rawData = raw->GetRawCollisionData();
          rawData && !rawRule.x64) {
        halfExtents =
            ReadValue<Vector4A16>(rawData, 48, ValueNeedsEndianSwap(in));
        center = ReadValue<Vector4A16>(rawData, 64, ValueNeedsEndianSwap(in));
      }
    }

    WriteField<uint32>(fixed, 12, in->GetShapeType());
    WriteField<uint8>(fixed, 20, 0);
    WriteArrayHeader(fixed, 24, in->GetNumChildren(), rule.version);
    WriteVectorField(fixed, 48, halfExtents);
    WriteVectorField(fixed, 64, center);
    for (size_t i = 0; i < 8; i++) {
      WriteField<uint32>(fixed, 80 + (i * sizeof(uint32)), 0xffffffffu);
    }

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);

    if (!in->GetNumChildren()) {
      return;
    }

    wr.ApplyPadding();
    fixups.locals.emplace_back(begin + 24, wr.Tell());
    const uint32 numChildren = static_cast<uint32>(in->GetNumChildren());
    for (size_t i = 0; i < in->GetNumChildren(); i++) {
      fixups.locals.emplace_back(wr.Tell(), in->GetChild(i));
      wr.Skip(kCollisionPtrSize);
      wr.Write<uint32>(0);
      wr.Write<uint32>(0);
      wr.Write<uint32>(numChildren);
    }
  }
};

struct hkpConvexTransformShapeSaver : HkpSaver<hkpConvexTransformShape> {
  using HkpSaver::HkpSaver;

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    auto fixed = Fixed(kConvexTransformShapeFixedSize);
    WriteField<uint32>(fixed, 12, in->GetShapeType());
    WriteField<float>(fixed, ConvexRadiusOffset(),
                      RawFloatField(in, ConvexRadiusOffset(), 0.0f));
    WriteField<uint32>(fixed, 24, 0);
    WriteMatrixField(fixed, 32, in->GetTransform());

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);
    if (in->GetChildShape()) {
      fixups.locals.emplace_back(begin + 24, in->GetChildShape());
    }
  }
};

struct hkpConvexTranslateShapeSaver : HkpSaver<hkpConvexTranslateShape> {
  using HkpSaver::HkpSaver;

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    auto fixed = Fixed(kConvexTranslateShapeFixedSize);
    WriteField<uint32>(fixed, 12, in->GetShapeType());
    WriteField<float>(fixed, ConvexRadiusOffset(),
                      RawFloatField(in, ConvexRadiusOffset(), 0.0f));
    WriteField<uint32>(fixed, 24, 0);
    WriteVectorField(fixed, 32, in->GetTranslation());

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);
    if (in->GetChildShape()) {
      fixups.locals.emplace_back(begin + 24, in->GetChildShape());
    }
  }
};

struct hkpBoxShapeSaver : HkpSaver<hkpBoxShape> {
  using HkpSaver::HkpSaver;

  void Save(BinWritterRef_e wr, hkFixups &) const {
    auto fixed = Fixed(kBoxShapeFixedSize);
    WriteField<uint32>(fixed, 12, in->GetShapeType());
    WriteField<float>(fixed, ConvexRadiusOffset(), in->GetRadius());
    WriteVectorField(fixed, 32, in->GetHalfExtents());
    WriteBuffer(wr, fixed);
  }
};

struct hkpCylinderShapeSaver : HkpSaver<hkpCylinderShape> {
  using HkpSaver::HkpSaver;

  void Save(BinWritterRef_e wr, hkFixups &) const {
    auto fixed = Fixed(kCylinderShapeFixedSize);
    WriteField<uint32>(fixed, 12, in->GetShapeType());
    WriteField<float>(fixed, 20, in->GetRadius());
    WriteVectorField(fixed, 32, in->GetVertexA());
    WriteVectorField(fixed, 48, in->GetVertexB());
    WriteVectorField(fixed, 64, Vector4A16(1.0f, 0.0f, 0.0f, 0.0f));
    WriteBuffer(wr, fixed);
  }
};

struct hkpConvexVerticesShapeSaver : HkpSaver<hkpConvexVerticesShape> {
  using HkpSaver::HkpSaver;

  struct FourVectors {
    Vector4A16 x;
    Vector4A16 y;
    Vector4A16 z;
  };

  std::vector<FourVectors> PackedVertices() const {
    std::vector<FourVectors> output((in->GetNumVertices() + 3) / 4);
    Vector4A16 lastVertex{};

    for (size_t i = 0; i < in->GetNumVertices(); i++) {
      Vector4A16 vertex{};
      if (!in->GetVertex(i, vertex)) {
        continue;
      }

      lastVertex = vertex;
      const size_t packed = i >> 2;
      const size_t lane = i & 3;
      output[packed].x._arr[lane] = vertex._arr[0];
      output[packed].y._arr[lane] = vertex._arr[1];
      output[packed].z._arr[lane] = vertex._arr[2];
    }

    const size_t remainder = in->GetNumVertices() & 3;
    if (!output.empty() && remainder) {
      auto &tail = output.back();
      for (size_t lane = remainder; lane < 4; lane++) {
        tail.x._arr[lane] = lastVertex._arr[0];
        tail.y._arr[lane] = lastVertex._arr[1];
        tail.z._arr[lane] = lastVertex._arr[2];
      }
    }

    return output;
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const {
    auto fixed = Fixed(ConvexVerticesShapeFixedSize(rule.version));
    const auto packed = PackedVertices();
    CRule arrayRule = rule;
    arrayRule.x64 = false;

    WriteField<uint32>(fixed, 12, in->GetShapeType());
    WriteField<float>(fixed, ConvexRadiusOffset(), in->GetRadius());
    WriteVectorField(fixed, 32, in->GetAabbHalfExtents());
    WriteVectorField(fixed, 48, in->GetAabbCenter());
    WriteArrayHeader(fixed, ConvexVerticesArrayOffset(), packed.size(),
                     rule.version);
    WriteField<int32>(fixed, ConvexVerticesNumVerticesOffset(false),
                      static_cast<int32>(in->GetNumVertices()));
    WriteArrayHeader(fixed, ConvexVerticesPlaneEquationsOffset(arrayRule),
                     in->GetNumPlaneEquations(), rule.version);

    const size_t begin = wr.Tell();
    WriteBuffer(wr, fixed);
    SaveValueArray(wr, fixups, begin, ConvexVerticesArrayOffset(),
                   packed.data(), packed.size() * 3, sizeof(Vector4A16));
    SaveValueArray(wr, fixups, begin, ConvexVerticesPlaneEquationsOffset(arrayRule),
                   in->GetPlaneEquations(), in->GetNumPlaneEquations(),
                   sizeof(Vector4A16));
  }
};

struct hkpPhysicsDataMidInterface : hkpMidBase<hkpPhysicsDataInternalInterface> {
  using Base = hkpMidBase<hkpPhysicsDataInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpPhysicsDataSaver> saver;

  static constexpr uint32 kMaxSystems = 1 << 14;

  ArrayViewWithMode GetSystemsArray() const {
    ArrayViewWithMode systems = ReadArrayWithFallback(
        data, 12, this->rule.x64, this->DataNeedsEndianSwap(), kMaxSystems);
    const size_t pointerSize = systems.x64 ? 8u : 4u;
    if (!ArrayViewCoveredByHeader(systems, pointerSize, this->header)) {
      return {};
    }
    return systems;
  }

  size_t GetNumSystems() const override {
    if (!PointerCoveredByHeader(this->header, data, 16)) {
      return 0;
    }
    return GetSystemsArray().view.count;
  }

  const hkpPhysicsSystem *GetSystem(size_t id) const override {
    const ArrayViewWithMode systems = GetSystemsArray();
    const void *item = ReadPointerArrayItem(systems.view, id, systems.x64);
    if (!PointerCoveredByHeader(this->header, item, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpPhysicsSystem>(this->header->GetClass(item));
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpPhysicsDataSaver>(
        this->rule, checked_deref_cast<const hkpPhysicsData>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpPhysicsSystemMidInterface
    : hkpMidBase<hkpPhysicsSystemInternalInterface> {
  using Base = hkpMidBase<hkpPhysicsSystemInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpPhysicsSystemSaver> saver;

  static constexpr uint32 kMaxRigidBodies = 1 << 16;

  ArrayViewWithMode GetRigidBodiesArray() const {
    ArrayViewWithMode bodies = ReadArrayWithFallback(
        data, 8, this->rule.x64, this->DataNeedsEndianSwap(), kMaxRigidBodies);
    const size_t pointerSize = bodies.x64 ? 8u : 4u;
    if (!ArrayViewCoveredByHeader(bodies, pointerSize, this->header)) {
      return {};
    }
    return bodies;
  }

  std::string_view GetName() const override {
    const char *name = ReadPointer<char>(data, 56, this->rule.x64);
    if (!PointerCoveredByHeader(this->header, name, 1)) {
      return {};
    }

    return name;
  }

  decltype(0 == 0) GetActive() const override {
    return ReadValue<uint8>(data, 64) != 0;
  }

  size_t GetNumRigidBodies() const override {
    if (!PointerCoveredByHeader(this->header, data, 16)) {
      return 0;
    }
    return GetRigidBodiesArray().view.count;
  }

  const hkpRigidBody *GetRigidBody(size_t id) const override {
    const ArrayViewWithMode rigidBodies = GetRigidBodiesArray();
    const void *item = ReadPointerArrayItem(rigidBodies.view, id, rigidBodies.x64);
    if (!PointerCoveredByHeader(this->header, item, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpRigidBody>(this->header->GetClass(item));
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpPhysicsSystemSaver>(
        this->rule, checked_deref_cast<const hkpPhysicsSystem>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpRigidBodyMidInterface : hkpMidBase<hkpRigidBodyInternalInterface> {
  using Base = hkpMidBase<hkpRigidBodyInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpRigidBodySaver> saver;

  static constexpr uint32 kMaxProperties = 1 << 16;

  ArrayViewWithMode GetPropertiesArray() const {
    ArrayViewWithMode properties = ReadArrayWithFallback(
        data, RigidBodyPropertiesOffset(this->rule.version, this->rule.x64),
        this->rule.x64, this->DataNeedsEndianSwap(), kMaxProperties);
    if (!ArrayViewCoveredByHeader(properties, sizeof(hkpRigidBodyProperty),
                             this->header)) {
      return {};
    }
    return properties;
  }

  size_t MotionOffset() const {
    return RigidBodyMotionOffset(this->rule.version);
  }

  size_t MotionStateOffset() const { return MotionOffset() + 16; }

  size_t SweptTransformOffset() const { return MotionStateOffset() + 64; }

  float ReadHalfFloat(size_t offset) const {
    return HalfBitsToFloat(ReadValue<uint16>(data, offset, this->DataNeedsEndianSwap()));
  }

  std::string_view GetName() const override {
    const char *name =
        ReadPointer<char>(data, RigidBodyNameOffset(this->rule.version), this->rule.x64);
    if (!PointerCoveredByHeader(this->header, name, 1)) {
      return {};
    }

    return name;
  }

  uint32 GetShapeKey() const override {
    const size_t offset = RigidBodyCollidableOffset() + PointerSize(this->rule.x64);
    return ReadValue<uint32>(data, offset, this->DataNeedsEndianSwap());
  }

  uint32 GetCollisionFilterInfo() const override {
    const size_t offset = BroadPhaseHandleOffset(this->rule.x64) + 8;
    return ReadValue<uint32>(data, offset, this->DataNeedsEndianSwap());
  }

  uint8 GetObjectQualityType() const override {
    return RuntimeQualityType(this->rule.version, data,
                              BroadPhaseHandleOffset(this->rule.x64) + 6,
                              this->DataNeedsEndianSwap());
  }

  uint8 GetMotionType() const override {
    const size_t motionOffset = RigidBodyMotionOffset(this->rule.version);
    return ReadValue<uint8>(data, motionOffset + 8);
  }

  Vector4A16 GetCenterOfMassLocal() const override {
    return ReadValue<Vector4A16>(data, SweptTransformOffset() + 64,
                                 this->DataNeedsEndianSwap());
  }

  Vector4A16 GetCenterOfMassWorld() const override {
    return ReadValue<Vector4A16>(data, SweptTransformOffset(),
                                 this->DataNeedsEndianSwap());
  }

  Vector4A16 GetRotation() const override {
    return ReadValue<Vector4A16>(data, SweptTransformOffset() + 32,
                                 this->DataNeedsEndianSwap());
  }

  Vector4A16 GetInertiaAndMassInv() const override {
    return ReadValue<Vector4A16>(data, MotionOffset() + 192,
                                 this->DataNeedsEndianSwap());
  }

  Vector4A16 GetLinearVelocity() const override {
    return ReadValue<Vector4A16>(data, MotionOffset() + 208,
                                 this->DataNeedsEndianSwap());
  }

  Vector4A16 GetAngularVelocity() const override {
    return ReadValue<Vector4A16>(data, MotionOffset() + 224,
                                 this->DataNeedsEndianSwap());
  }

  float GetMotionObjectRadius() const override {
    return ReadValue<float>(data, MotionStateOffset() + 160,
                            this->DataNeedsEndianSwap());
  }

  float GetLinearDamping() const override {
    if (this->rule.version == HK550) {
      return ReadValue<float>(data, MotionStateOffset() + 164,
                              this->DataNeedsEndianSwap());
    }

    return ReadHalfFloat(MotionStateOffset() + 164);
  }

  float GetAngularDamping() const override {
    if (this->rule.version == HK550) {
      return ReadValue<float>(data, MotionStateOffset() + 168,
                              this->DataNeedsEndianSwap());
    }

    return ReadHalfFloat(MotionStateOffset() + 166);
  }

  float GetTimeFactor() const override {
    return this->rule.version == HK550 ? 1.0f
                                       : ReadHalfFloat(MotionStateOffset() + 168);
  }

  float GetGravityFactor() const override {
    return this->rule.version == HK550 ? 1.0f : ReadHalfFloat(MotionOffset() + 286);
  }

  uint8 GetDeactivationIntegrateCounter() const override {
    return ReadValue<uint8>(data, MotionOffset() + 9);
  }

  uint16 GetDeactivationNumInactiveFrames(size_t id) const override {
    if (id > 1) {
      return 0;
    }

    return ReadValue<uint16>(data, MotionOffset() + 10 + (id * sizeof(uint16)),
                             this->DataNeedsEndianSwap());
  }

  uint8 GetMaxLinearVelocity() const override {
    return ReadValue<uint8>(data,
                            MotionStateOffset() +
                                (this->rule.version == HK550 ? 172 : 170));
  }

  uint8 GetMaxAngularVelocity() const override {
    return ReadValue<uint8>(data,
                            MotionStateOffset() +
                                (this->rule.version == HK550 ? 173 : 171));
  }

  uint8 GetDeactivationClass() const override {
    return ReadValue<uint8>(data,
                            MotionStateOffset() +
                                (this->rule.version == HK550 ? 174 : 172));
  }

  uint16 GetSavedQualityTypeIndex() const override {
    return ReadValue<uint16>(data, MotionOffset() + 284, this->DataNeedsEndianSwap());
  }

  uint8 GetMaterialResponseType() const override {
    return ReadValue<uint8>(
        data, RigidBodyMaterialOffset(this->rule.version, this->rule.x64));
  }

  float GetMaterialFriction() const override {
    return ReadValue<float>(
        data, RigidBodyMaterialOffset(this->rule.version, this->rule.x64) + 4,
        this->DataNeedsEndianSwap());
  }

  float GetMaterialRestitution() const override {
    return ReadValue<float>(
        data, RigidBodyMaterialOffset(this->rule.version, this->rule.x64) + 8,
        this->DataNeedsEndianSwap());
  }

  size_t GetNumProperties() const override {
    return GetPropertiesArray().view.count;
  }

  hkpRigidBodyProperty GetProperty(size_t id) const override {
    const ArrayViewWithMode properties = GetPropertiesArray();
    if (!properties.view.data || id >= properties.view.count) {
      return {};
    }

    const char *propertyData = properties.view.data + id * sizeof(hkpRigidBodyProperty);
    hkpRigidBodyProperty property;
    property.key = ReadValue<uint32>(propertyData, 0, properties.swapEndian);
    property.alignmentPadding =
        ReadValue<uint32>(propertyData, 4, properties.swapEndian);
    property.data = ReadValue<uint64>(propertyData, 8, properties.swapEndian);
    return property;
  }

  es::Matrix44 GetTransform() const override {
    return ReadValue<es::Matrix44>(data, MotionStateOffset(),
                                   this->DataNeedsEndianSwap());
  }

  const hkpShape *GetShape() const override {
    const void *shapePtr = ReadPointer<char>(data, 16, this->rule.x64);
    if (!PointerCoveredByHeader(this->header, shapePtr, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpShape>(this->header->GetClass(shapePtr));
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpRigidBodySaver>(
        this->rule, checked_deref_cast<const hkpRigidBody>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpShapeMidInterface : hkpMidBase<hkpShapeInternalInterface> {
  using Base = hkpMidBase<hkpShapeInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpShapeSaver> saver;

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->DataNeedsEndianSwap());
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpShapeSaver>(
        this->rule, checked_deref_cast<const hkpShape>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpMoppCodeMidInterface : hkpMidBase<hkpMoppCodeInternalInterface> {
  using Base = hkpMidBase<hkpMoppCodeInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpMoppCodeSaver> saver;

  static constexpr uint32 kMaxCodeBytes = 1 << 27;
  mutable Vector4A16 offsetStorage{};

  ArrayViewWithMode GetDataArray() const {
    return ReadArrayWithFallback(data, MoppCodeDataOffset(this->rule.version),
                                 this->rule.x64, this->DataNeedsEndianSwap(),
                                 kMaxCodeBytes);
  }

  const Vector4A16 &GetOffset() const override {
    static const Vector4A16 defaultValue{};

    if (!data) {
      return defaultValue;
    }

    offsetStorage = ReadValue<Vector4A16>(
        data, MoppCodeInfoOffset(this->rule.version), this->DataNeedsEndianSwap());
    return offsetStorage;
  }

  size_t GetDataSize() const override {
    return GetDataArray().view.count;
  }

  const uint8 *GetData() const override {
    return reinterpret_cast<const uint8 *>(GetDataArray().view.data);
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpMoppCodeSaver>(
        this->rule, checked_deref_cast<const hkpMoppCode>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpMoppBvTreeShapeMidInterface
    : hkpMidBase<hkpMoppBvTreeShapeInternalInterface> {
  using Base = hkpMidBase<hkpMoppBvTreeShapeInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpMoppBvTreeShapeSaver> saver;

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->DataNeedsEndianSwap());
  }

  const hkpMoppCode *GetCode() const override {
    const void *codePtr = ReadPointer<char>(
        data, MoppCodePointerOffset(this->rule.version), this->rule.x64);
    if (!PointerCoveredByHeader(this->header, codePtr, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpMoppCode>(this->header->GetClass(codePtr));
  }

  const hkpShape *GetChildShape() const override {
    const size_t childPointerOffset = 48 + (this->rule.x64 ? 8 : 4);
    const void *shapePtr = ReadPointer<char>(data, childPointerOffset, this->rule.x64);
    if (!PointerCoveredByHeader(this->header, shapePtr, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpShape>(this->header->GetClass(shapePtr));
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpMoppBvTreeShapeSaver>(
        this->rule, checked_deref_cast<const hkpMoppBvTreeShape>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpStaticCompoundShapeMidInterface
    : hkpMidBase<hkpStaticCompoundShapeInternalInterface> {
  using Base = hkpMidBase<hkpStaticCompoundShapeInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpStaticCompoundShapeSaver> saver;

  static constexpr uint32 kMaxInstances = 1 << 16;

  ArrayViewWithMode GetInstancesArray() const {
    ArrayViewWithMode instances = ReadArrayWithFallback(
        data, 32, this->rule.x64, this->DataNeedsEndianSwap(), kMaxInstances);
    if (!ArrayViewCoveredByHeader(instances, kStaticCompoundInstanceSize,
                             this->header)) {
      return {};
    }
    return instances;
  }

  uint32 GetShapeType() const override { return 16; }

  size_t GetNumInstances() const override {
    return GetInstancesArray().view.count;
  }

  hkpStaticCompoundShapeInstance GetInstance(size_t id) const override {
    hkpStaticCompoundShapeInstance instance;
    const ArrayViewWithMode instances = GetInstancesArray();
    if (!instances.view.data || id >= instances.view.count) {
      return instance;
    }

    const char *record =
        instances.view.data + (id * kStaticCompoundInstanceSize);
    instance.translation =
        ReadValue<Vector4A16>(record, 0, instances.swapEndian);
    instance.translation._arr[3] = 0.0f;
    instance.rotation =
        ReadValue<Vector4A16>(record, 16, instances.swapEndian);
    instance.scale = ReadValue<Vector4A16>(record, 32, instances.swapEndian);
    instance.scale._arr[3] = 0.0f;
    instance.filterInfo = ReadValue<uint32>(record, 52, instances.swapEndian);
    instance.childFilterInfoMask =
        ReadValue<uint32>(record, 56, instances.swapEndian);
    instance.userData = ReadValue<uint32>(record, 60, instances.swapEndian);

    const void *shapePtr = ReadPointer<char>(record, 48, instances.x64);
    if (PointerCoveredByHeader(this->header, shapePtr, 1)) {
      instance.shape =
          safe_deref_cast<const hkpShape>(this->header->GetClass(shapePtr));
    }
    return instance;
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpStaticCompoundShapeSaver>(
        this->rule, checked_deref_cast<const hkpStaticCompoundShape>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpStorageExtendedMeshShapeMidInterface
    : hkpMidBase<hkpStorageExtendedMeshShapeInternalInterface> {
  using Base = hkpMidBase<hkpStorageExtendedMeshShapeInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpStorageExtendedMeshShapeSaver> saver;

  static constexpr uint32 kMaxSubparts = 1 << 16;

  ArrayViewWithMode GetMeshStorage() const {
    ArrayViewWithMode mesh = ReadArrayWithFallback(
        data, StorageMeshArrayOffset(this->rule.version), this->rule.x64,
        this->DataNeedsEndianSwap(), kMaxSubparts);
    const size_t pointerSize = mesh.x64 ? 8u : 4u;
    if (!ArrayViewCoveredByHeader(mesh, pointerSize, this->header)) {
      return {};
    }
    return mesh;
  }

  ArrayViewWithMode GetShapeStorage() const {
    ArrayViewWithMode shapes = ReadArrayWithFallback(
        data, StorageShapeArrayOffset(this->rule.version), this->rule.x64,
        this->DataNeedsEndianSwap(), kMaxSubparts);
    const size_t pointerSize = shapes.x64 ? 8u : 4u;
    if (!ArrayViewCoveredByHeader(shapes, pointerSize, this->header)) {
      return {};
    }
    return shapes;
  }

  ArrayViewWithMode GetShapeRecords() const {
    ArrayViewWithMode shapes = ReadArrayWithFallback(
        data, StorageShapeRecordsArrayOffset(this->rule.version),
        this->rule.x64, this->DataNeedsEndianSwap(), kMaxSubparts);
    if (!ArrayViewCoveredByHeader(shapes, ShapeSubpartFixedSize(this->rule.version),
                             this->header)) {
      return {};
    }
    return shapes;
  }

  void CacheShapeSubpartChildren(
      size_t subpartIndex,
      const hkpStorageExtendedMeshShapeShapeSubpartStorage *subpart) const {
    if (!subpart) {
      return;
    }

    const ArrayViewWithMode records = GetShapeRecords();
    if (subpartIndex >= records.view.count) {
      return;
    }

    const char *record = records.view.data +
                         subpartIndex *
                             ShapeSubpartFixedSize(this->rule.version);
    ArrayViewWithMode children = ReadArrayWithFallback(
        record, ShapeRecordChildShapesOffset(this->rule.version), records.x64,
        records.swapEndian, kMaxSubparts);
    if (!ArrayViewCoveredByHeader(children, PointerSize(children.x64),
                             this->header)) {
      return;
    }

    const auto *rawSubpart = RawCollision(subpart);
    auto &cached = ShapeSubpartChildren()[rawSubpart
                                             ? rawSubpart->GetRawCollisionData()
                                             : static_cast<const void *>(
                                                   subpart)];
    cached.clear();
    cached.reserve(children.view.count);
    for (size_t childIndex = 0; childIndex < children.view.count;
         childIndex++) {
      const void *child =
          ReadPointerArrayItem(children.view, childIndex, children.x64);
      if (!PointerCoveredByHeader(this->header, child, 1)) {
        continue;
      }

      if (const auto *shape =
              safe_deref_cast<const hkpShape>(this->header->GetClass(child))) {
        cached.emplace_back(shape);
      }
    }
  }

  void CacheShapeSubpartChildren() const {
    const ArrayViewWithMode records = GetShapeRecords();
    const ArrayViewWithMode storage = GetShapeStorage();
    const size_t count =
        (std::min)(static_cast<size_t>(records.view.count),
                   static_cast<size_t>(storage.view.count));

    for (size_t subpartIndex = 0; subpartIndex < count; subpartIndex++) {
      const void *item =
          ReadPointerArrayItem(storage.view, subpartIndex, storage.x64);
      if (!PointerCoveredByHeader(this->header, item, 1)) {
        continue;
      }

      const auto *subpart =
          safe_deref_cast<const hkpStorageExtendedMeshShapeShapeSubpartStorage>(
              this->header->GetClass(item));
      if (!subpart) {
        continue;
      }

      CacheShapeSubpartChildren(subpartIndex, subpart);
    }
  }

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->DataNeedsEndianSwap());
  }

  size_t GetNumMeshSubparts() const override {
    return GetMeshStorage().view.count;
  }

  const hkpStorageExtendedMeshShapeMeshSubpartStorage *
  GetMeshSubpart(size_t id) const override {
    const ArrayViewWithMode array = GetMeshStorage();
    const void *item = ReadPointerArrayItem(array.view, id, array.x64);
    if (!PointerCoveredByHeader(this->header, item, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpStorageExtendedMeshShapeMeshSubpartStorage>(
        this->header->GetClass(item));
  }

  size_t GetNumShapeSubparts() const override {
    return GetShapeStorage().view.count;
  }

  const hkpStorageExtendedMeshShapeShapeSubpartStorage *
  GetShapeSubpart(size_t id) const override {
    const ArrayViewWithMode array = GetShapeStorage();
    const void *item = ReadPointerArrayItem(array.view, id, array.x64);
    if (!PointerCoveredByHeader(this->header, item, 1)) {
      return nullptr;
    }
    const auto *subpart =
        safe_deref_cast<const hkpStorageExtendedMeshShapeShapeSubpartStorage>(
            this->header->GetClass(item));
    CacheShapeSubpartChildren(id, subpart);
    return subpart;
  }

  void Reflect(const IhkVirtualClass *other) override {
    if (const auto *source =
            dynamic_cast<const hkpStorageExtendedMeshShapeMidInterface *>(
                other)) {
      source->CacheShapeSubpartChildren();
    }
    saver = std::make_unique<hkpStorageExtendedMeshShapeSaver>(
        this->rule, checked_deref_cast<const hkpStorageExtendedMeshShape>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpStorageExtendedMeshShapeMeshSubpartStorageMidInterface
    : hkpMidBase<hkpStorageExtendedMeshShapeMeshSubpartStorageInternalInterface> {
  using Base =
      hkpMidBase<hkpStorageExtendedMeshShapeMeshSubpartStorageInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpStorageExtendedMeshShapeMeshSubpartStorageSaver> saver;

  static constexpr uint32 kMaxVertices = 1 << 22;
  static constexpr uint32 kMaxIndices = 1 << 24;

  mutable const Vector4A16 *cachedVertexSource = nullptr;
  mutable std::vector<Vector4A16> cachedVertices;
  mutable const uint16 *cachedIndices16Source = nullptr;
  mutable std::vector<uint16> cachedIndices16;
  mutable const uint32 *cachedIndices32Source = nullptr;
  mutable std::vector<uint32> cachedIndices32;

  ArrayViewWithMode GetVerticesArray() const {
    ArrayViewWithMode vertices = ReadArrayWithFallback(
        data, 8, this->rule.x64, this->DataNeedsEndianSwap(), kMaxVertices);
    if (!ArrayViewCoveredByHeader(vertices, sizeof(Vector4A16), this->header)) {
      return {};
    }
    return vertices;
  }

  ArrayViewWithMode GetIndices8Array() const {
    const size_t offset = MeshSubpartIndices8Offset(this->rule.version);
    if (offset == static_cast<size_t>(-1)) {
      return {};
    }

    ArrayViewWithMode indices = ReadArrayWithFallback(
        data, offset, this->rule.x64, this->DataNeedsEndianSwap(), kMaxIndices);
    if (!ArrayViewCoveredByHeader(indices, sizeof(uint8), this->header)) {
      return {};
    }
    return indices;
  }

  ArrayViewWithMode GetIndices16Array() const {
    ArrayViewWithMode indices = ReadArrayWithFallback(
        data, MeshSubpartIndices16Offset(this->rule.version), this->rule.x64,
        this->DataNeedsEndianSwap(), kMaxIndices);
    if (!ArrayViewCoveredByHeader(indices, sizeof(uint16), this->header)) {
      return {};
    }
    return indices;
  }

  ArrayViewWithMode GetIndices32Array() const {
    ArrayViewWithMode indices = ReadArrayWithFallback(
        data, MeshSubpartIndices32Offset(this->rule.version), this->rule.x64,
        this->DataNeedsEndianSwap(), kMaxIndices);
    if (!ArrayViewCoveredByHeader(indices, sizeof(uint32), this->header)) {
      return {};
    }
    return indices;
  }

  size_t GetNumVertices() const override {
    return GetVerticesArray().view.count;
  }

  const Vector4A16 *GetVertices() const override {
    const ArrayViewWithMode vertices = GetVerticesArray();
    const auto *rawVertices =
        reinterpret_cast<const Vector4A16 *>(vertices.view.data);
    if (!rawVertices || vertices.view.count == 0 || !vertices.swapEndian) {
      return rawVertices;
    }

    if (cachedVertexSource != rawVertices ||
        cachedVertices.size() != vertices.view.count) {
      cachedVertexSource = rawVertices;
      cachedVertices.assign(rawVertices, rawVertices + vertices.view.count);

      for (auto &value : cachedVertices) {
        value = ByteSwapVector4(value);
      }
    }

    return cachedVertices.data();
  }

  size_t GetNumIndices8() const override {
    const size_t offset = MeshSubpartIndices8Offset(this->rule.version);
    if (offset == static_cast<size_t>(-1)) {
      return 0;
    }

    return GetIndices8Array().view.count;
  }

  const uint8 *GetIndices8() const override {
    const size_t offset = MeshSubpartIndices8Offset(this->rule.version);
    if (offset == static_cast<size_t>(-1)) {
      return nullptr;
    }

    return reinterpret_cast<const uint8 *>(GetIndices8Array().view.data);
  }

  size_t GetNumIndices16() const override {
    return GetIndices16Array().view.count;
  }

  const uint16 *GetIndices16() const override {
    const ArrayViewWithMode indices = GetIndices16Array();
    const auto *rawIndices = reinterpret_cast<const uint16 *>(indices.view.data);
    if (!rawIndices || indices.view.count == 0 || !indices.swapEndian) {
      return rawIndices;
    }

    if (cachedIndices16Source != rawIndices ||
        cachedIndices16.size() != indices.view.count) {
      cachedIndices16Source = rawIndices;
      cachedIndices16.assign(rawIndices, rawIndices + indices.view.count);

      for (auto &value : cachedIndices16) {
        value = ByteSwap16(value);
      }
    }

    return cachedIndices16.data();
  }

  size_t GetNumIndices32() const override {
    return GetIndices32Array().view.count;
  }

  const uint32 *GetIndices32() const override {
    const ArrayViewWithMode indices = GetIndices32Array();
    const auto *rawIndices = reinterpret_cast<const uint32 *>(indices.view.data);
    if (!rawIndices || indices.view.count == 0 || !indices.swapEndian) {
      return rawIndices;
    }

    if (cachedIndices32Source != rawIndices ||
        cachedIndices32.size() != indices.view.count) {
      cachedIndices32Source = rawIndices;
      cachedIndices32.assign(rawIndices, rawIndices + indices.view.count);

      for (auto &value : cachedIndices32) {
        value = ByteSwap32(value);
      }
    }

    return cachedIndices32.data();
  }

  size_t GetNumTriangles() const override {
    const size_t numIndices32 = GetNumIndices32();
    if (numIndices32 >= 4) {
      return numIndices32 / 4;
    }

    const size_t numIndices16 = GetNumIndices16();
    if (numIndices16 >= 4) {
      return numIndices16 / 4;
    }

    const size_t numIndices8 = GetNumIndices8();
    if (numIndices8 >= 4) {
      return numIndices8 / 4;
    }

    return 0;
  }

  decltype(0 == 0) GetTriangleIndices(size_t id, uint32 &a, uint32 &b,
                                      uint32 &c) const override {
    const uint32 *indices32 = GetIndices32();
    const size_t numIndices32 = GetNumIndices32();
    if (indices32 && id < numIndices32 / 4) {
      const size_t base = id * 4;
      a = indices32[base + 0];
      b = indices32[base + 1];
      c = indices32[base + 2];
      return true;
    }

    const uint16 *indices16 = GetIndices16();
    const size_t numIndices16 = GetNumIndices16();
    if (indices16 && id < numIndices16 / 4) {
      const size_t base = id * 4;
      a = indices16[base + 0];
      b = indices16[base + 1];
      c = indices16[base + 2];
      return true;
    }

    const uint8 *indices8 = GetIndices8();
    const size_t numIndices8 = GetNumIndices8();
    if (indices8 && id < numIndices8 / 4) {
      const size_t base = id * 4;
      a = indices8[base + 0];
      b = indices8[base + 1];
      c = indices8[base + 2];
      return true;
    }

    return false;
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpStorageExtendedMeshShapeMeshSubpartStorageSaver>(
        this->rule,
        checked_deref_cast<const hkpStorageExtendedMeshShapeMeshSubpartStorage>(
            other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpStorageExtendedMeshShapeShapeSubpartStorageMidInterface
    : hkpMidBase<hkpStorageExtendedMeshShapeShapeSubpartStorageInternalInterface> {
  using Base =
      hkpMidBase<hkpStorageExtendedMeshShapeShapeSubpartStorageInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpStorageExtendedMeshShapeShapeSubpartStorageSaver> saver;

  static constexpr uint32 kMaxShapes = 1 << 16;

  ArrayViewWithMode GetShapesArray() const {
    const size_t offset = ShapeSubpartShapeArrayOffset(this->rule.version);
    if (offset == static_cast<size_t>(-1)) {
      return {};
    }

    ArrayViewWithMode shapes = ReadArrayWithFallback(
        data, offset, this->rule.x64, this->DataNeedsEndianSwap(), kMaxShapes);
    const size_t pointerSize = shapes.x64 ? 8u : 4u;
    if (!ArrayViewCoveredByHeader(shapes, pointerSize, this->header)) {
      return {};
    }
    return shapes;
  }

  size_t GetNumShapes() const override {
    const auto cached = ShapeSubpartChildren().find(data);
    if (cached != ShapeSubpartChildren().end()) {
      return cached->second.size();
    }

    const size_t offset = ShapeSubpartShapeArrayOffset(this->rule.version);
    if (offset == static_cast<size_t>(-1)) {
      return 0;
    }

    return GetShapesArray().view.count;
  }

  const hkpShape *GetShape(size_t id) const override {
    const auto cached = ShapeSubpartChildren().find(data);
    if (cached != ShapeSubpartChildren().end()) {
      if (id >= cached->second.size()) {
        return nullptr;
      }
      return cached->second[id];
    }

    const size_t offset = ShapeSubpartShapeArrayOffset(this->rule.version);
    if (offset == static_cast<size_t>(-1)) {
      return nullptr;
    }

    const ArrayViewWithMode shapes = GetShapesArray();
    const void *item = ReadPointerArrayItem(shapes.view, id, shapes.x64);
    if (!PointerCoveredByHeader(this->header, item, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpShape>(this->header->GetClass(item));
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpStorageExtendedMeshShapeShapeSubpartStorageSaver>(
        this->rule,
        checked_deref_cast<const hkpStorageExtendedMeshShapeShapeSubpartStorage>(
            other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpListShapeMidInterface : hkpMidBase<hkpListShapeInternalInterface> {
  using Base = hkpMidBase<hkpListShapeInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpListShapeSaver> saver;

  static constexpr uint32 kMaxChildren = 1 << 16;

  ArrayViewWithMode GetChildrenArray() const {
    ArrayViewWithMode children = ReadArrayWithFallback(
        data, 24, this->rule.x64, this->DataNeedsEndianSwap(), kMaxChildren);
    const size_t entrySize = children.x64 ? 24u : 16u;
    if (!ArrayViewCoveredByHeader(children, entrySize, this->header)) {
      return {};
    }
    return children;
  }

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->DataNeedsEndianSwap());
  }

  size_t GetNumChildren() const override {
    return GetChildrenArray().view.count;
  }

  const hkpShape *GetChild(size_t id) const override {
    const ArrayViewWithMode children = GetChildrenArray();
    if (!children.view.data || id >= children.view.count) {
      return nullptr;
    }

    const size_t entrySize = children.x64 ? 24 : 16;
    const char *entryData = children.view.data + (id * entrySize);
    const void *shapePtr = ReadPointer<char>(entryData, 0, children.x64);
    if (!PointerCoveredByHeader(this->header, shapePtr, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpShape>(this->header->GetClass(shapePtr));
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpListShapeSaver>(
        this->rule, checked_deref_cast<const hkpListShape>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpConvexTransformShapeMidInterface
    : hkpMidBase<hkpConvexTransformShapeInternalInterface> {
  using Base = hkpMidBase<hkpConvexTransformShapeInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpConvexTransformShapeSaver> saver;

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->DataNeedsEndianSwap());
  }

  const hkpShape *GetChildShape() const override {
    const size_t childPointerOffset = 20 + (this->rule.x64 ? 8 : 4);
    const void *shapePtr = ReadPointer<char>(data, childPointerOffset, this->rule.x64);
    if (!PointerCoveredByHeader(this->header, shapePtr, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpShape>(this->header->GetClass(shapePtr));
  }

  es::Matrix44 GetTransform() const override {
    return ReadValue<es::Matrix44>(data, 32, this->DataNeedsEndianSwap());
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpConvexTransformShapeSaver>(
        this->rule, checked_deref_cast<const hkpConvexTransformShape>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpConvexTranslateShapeMidInterface
    : hkpMidBase<hkpConvexTranslateShapeInternalInterface> {
  using Base = hkpMidBase<hkpConvexTranslateShapeInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpConvexTranslateShapeSaver> saver;

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->DataNeedsEndianSwap());
  }

  const hkpShape *GetChildShape() const override {
    const size_t childPointerOffset = 20 + (this->rule.x64 ? 8 : 4);
    const void *shapePtr = ReadPointer<char>(data, childPointerOffset, this->rule.x64);
    if (!PointerCoveredByHeader(this->header, shapePtr, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpShape>(this->header->GetClass(shapePtr));
  }

  Vector4A16 GetTranslation() const override {
    return ReadValue<Vector4A16>(data, 32, this->DataNeedsEndianSwap());
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpConvexTranslateShapeSaver>(
        this->rule, checked_deref_cast<const hkpConvexTranslateShape>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpBoxShapeMidInterface : hkpMidBase<hkpBoxShapeInternalInterface> {
  using Base = hkpMidBase<hkpBoxShapeInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpBoxShapeSaver> saver;

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->DataNeedsEndianSwap());
  }
  float GetRadius() const override {
    return ReadValue<float>(data, ConvexRadiusOffset(), this->DataNeedsEndianSwap());
  }
  Vector4A16 GetHalfExtents() const override {
    return ReadValue<Vector4A16>(data, 32, this->DataNeedsEndianSwap());
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpBoxShapeSaver>(
        this->rule, checked_deref_cast<const hkpBoxShape>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpCylinderShapeMidInterface : hkpMidBase<hkpCylinderShapeInternalInterface> {
  using Base = hkpMidBase<hkpCylinderShapeInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpCylinderShapeSaver> saver;

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->DataNeedsEndianSwap());
  }
  float GetRadius() const override {
    return ReadValue<float>(data, 20, this->DataNeedsEndianSwap());
  }
  Vector4A16 GetVertexA() const override {
    return ReadValue<Vector4A16>(data, 32, this->DataNeedsEndianSwap());
  }
  Vector4A16 GetVertexB() const override {
    return ReadValue<Vector4A16>(data, 48, this->DataNeedsEndianSwap());
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpCylinderShapeSaver>(
        this->rule, checked_deref_cast<const hkpCylinderShape>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

struct hkpConvexVerticesShapeMidInterface
    : hkpMidBase<hkpConvexVerticesShapeInternalInterface> {
  using Base = hkpMidBase<hkpConvexVerticesShapeInternalInterface>;
  using Base::Base;
  std::unique_ptr<hkpConvexVerticesShapeSaver> saver;

  struct FourVectors {
    Vector4A16 x;
    Vector4A16 y;
    Vector4A16 z;
  };

  static constexpr uint32 kMaxPackedVertices = 1 << 20;
  static constexpr uint32 kMaxPlaneEquations = 1 << 20;
  mutable const Vector4A16 *cachedPlaneSource = nullptr;
  mutable std::vector<Vector4A16> cachedPlanes;

  ArrayViewWithMode GetPackedVertices() const {
    ArrayViewWithMode packed = ReadArrayWithFallback(
        data, ConvexVerticesArrayOffset(), this->rule.x64,
        this->DataNeedsEndianSwap(), kMaxPackedVertices);
    if (!ArrayViewCoveredByHeader(packed, sizeof(FourVectors), this->header)) {
      return {};
    }
    return packed;
  }

  ArrayViewWithMode GetPlaneEquationsArray() const {
    const ArrayViewWithMode packedVertices = GetPackedVertices();
    CRule activeRule = this->rule;
    if (packedVertices.view.data) {
      activeRule.x64 = packedVertices.x64;
    }
    ArrayViewWithMode planes = ReadArrayWithFallback(
        data, ConvexVerticesPlaneEquationsOffset(activeRule), activeRule.x64,
        this->DataNeedsEndianSwap(), kMaxPlaneEquations);
    if (!ArrayViewCoveredByHeader(planes, sizeof(Vector4A16), this->header)) {
      return {};
    }
    return planes;
  }

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->DataNeedsEndianSwap());
  }

  float GetRadius() const override {
    return ReadValue<float>(data, ConvexRadiusOffset(), this->DataNeedsEndianSwap());
  }

  Vector4A16 GetAabbHalfExtents() const override {
    return ReadValue<Vector4A16>(data, 32, this->DataNeedsEndianSwap());
  }

  Vector4A16 GetAabbCenter() const override {
    return ReadValue<Vector4A16>(data, 48, this->DataNeedsEndianSwap());
  }

  size_t GetNumVertices() const override {
    const ArrayViewWithMode values = GetPackedVertices();
    CRule activeRule = this->rule;
    if (values.view.data) {
      activeRule.x64 = values.x64;
    }
    const int32 count = ReadValue<int32>(
        data, ConvexVerticesNumVerticesOffset(activeRule.x64),
        this->DataNeedsEndianSwap());
    if (count <= 0) {
      return 0;
    }

    return static_cast<size_t>(count);
  }

  decltype(0 == 0) GetVertex(size_t id, Vector4A16 &out) const override {
    if (id >= GetNumVertices()) {
      return false;
    }

    const ArrayViewWithMode values = GetPackedVertices();
    if (!values.view.data) {
      return false;
    }

    const size_t packedIndex = id >> 2;
    if (packedIndex >= values.view.count) {
      return false;
    }

    const auto packed =
        reinterpret_cast<const FourVectors *>(values.view.data) + packedIndex;
    const size_t lane = id & 3;
    float x = packed->x._arr[lane];
    float y = packed->y._arr[lane];
    float z = packed->z._arr[lane];
    if (values.swapEndian) {
      x = ByteSwapFloat(x);
      y = ByteSwapFloat(y);
      z = ByteSwapFloat(z);
    }

    out = Vector4A16(x, y, z, 1.0f);
    return true;
  }

  size_t GetNumPlaneEquations() const override {
    return GetPlaneEquationsArray().view.count;
  }

  const Vector4A16 *GetPlaneEquations() const override {
    const ArrayViewWithMode planes = GetPlaneEquationsArray();
    const auto *rawPlanes = reinterpret_cast<const Vector4A16 *>(planes.view.data);
    if (!rawPlanes || planes.view.count == 0 || !planes.swapEndian) {
      return rawPlanes;
    }

    if (cachedPlaneSource != rawPlanes ||
        cachedPlanes.size() != planes.view.count) {
      cachedPlaneSource = rawPlanes;
      cachedPlanes.assign(rawPlanes, rawPlanes + planes.view.count);

      for (auto &value : cachedPlanes) {
        value = ByteSwapVector4(value);
      }
    }

    return cachedPlanes.data();
  }

  void Reflect(const IhkVirtualClass *other) override {
    saver = std::make_unique<hkpConvexVerticesShapeSaver>(
        this->rule, checked_deref_cast<const hkpConvexVerticesShape>(other));
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }
};

IhkVirtualClass *hkpPhysicsDataInternalInterface::Create(CRule rule) {
  return new hkpPhysicsDataMidInterface{rule};
}

IhkVirtualClass *hkpPhysicsSystemInternalInterface::Create(CRule rule) {
  return new hkpPhysicsSystemMidInterface{rule};
}

IhkVirtualClass *hkpRigidBodyInternalInterface::Create(CRule rule) {
  return new hkpRigidBodyMidInterface{rule};
}

IhkVirtualClass *hkpShapeInternalInterface::Create(CRule rule) {
  return new hkpShapeMidInterface{rule};
}

IhkVirtualClass *hkpMoppCodeInternalInterface::Create(CRule rule) {
  return new hkpMoppCodeMidInterface{rule};
}

IhkVirtualClass *hkpMoppBvTreeShapeInternalInterface::Create(CRule rule) {
  return new hkpMoppBvTreeShapeMidInterface{rule};
}

IhkVirtualClass *hkpStaticCompoundShapeInternalInterface::Create(CRule rule) {
  return new hkpStaticCompoundShapeMidInterface{rule};
}

IhkVirtualClass *hkpStorageExtendedMeshShapeInternalInterface::Create(CRule rule) {
  return new hkpStorageExtendedMeshShapeMidInterface{rule};
}

IhkVirtualClass *
hkpStorageExtendedMeshShapeMeshSubpartStorageInternalInterface::Create(CRule rule) {
  return new hkpStorageExtendedMeshShapeMeshSubpartStorageMidInterface{rule};
}

IhkVirtualClass *
hkpStorageExtendedMeshShapeShapeSubpartStorageInternalInterface::Create(CRule rule) {
  return new hkpStorageExtendedMeshShapeShapeSubpartStorageMidInterface{rule};
}

IhkVirtualClass *hkpListShapeInternalInterface::Create(CRule rule) {
  return new hkpListShapeMidInterface{rule};
}

IhkVirtualClass *hkpConvexTransformShapeInternalInterface::Create(CRule rule) {
  return new hkpConvexTransformShapeMidInterface{rule};
}

IhkVirtualClass *hkpConvexTranslateShapeInternalInterface::Create(CRule rule) {
  return new hkpConvexTranslateShapeMidInterface{rule};
}

IhkVirtualClass *hkpBoxShapeInternalInterface::Create(CRule rule) {
  return new hkpBoxShapeMidInterface{rule};
}

IhkVirtualClass *hkpCylinderShapeInternalInterface::Create(CRule rule) {
  return new hkpCylinderShapeMidInterface{rule};
}

IhkVirtualClass *hkpConvexVerticesShapeInternalInterface::Create(CRule rule) {
  return new hkpConvexVerticesShapeMidInterface{rule};
}
