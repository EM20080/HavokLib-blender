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
#include "hklib/hk_packfile.hpp"
#include "spike/type/pointer.hpp"
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace {
bool MultiplySize(size_t left, size_t right, size_t &out) {
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

bool IsPointerInRange(const void *ptr, size_t size, const char *base,
                      size_t length) {
  if (!ptr) {
    return false;
  }
  if (size > length) {
    return false;
  }

  const char *p = static_cast<const char *>(ptr);
  if (p < base) {
    return false;
  }

  const size_t offset = static_cast<size_t>(p - base);
  return offset <= length - size;
}

bool IsPointerInHeader(const IhkPackFile *header, const void *ptr,
                       size_t size) {
  if (!header || !ptr) {
    return false;
  }

  if (const auto *oldHeader = dynamic_cast<const hkxHeader *>(header)) {
    for (const auto &section : oldHeader->sections) {
      const char *base = section.buffer.data();
      const size_t length = section.buffer.size();
      if (IsPointerInRange(ptr, size, base, length)) {
        return true;
      }
    }
  }

  if (const auto *newHeader = dynamic_cast<const hkxNewHeader *>(header)) {
    const char *base = newHeader->dataBuffer.data();
    const size_t length = newHeader->dataBuffer.size();
    if (IsPointerInRange(ptr, size, base, length)) {
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

bool RequiresEndianSwap(const IhkPackFile *header) {
  const auto *oldHeader = dynamic_cast<const hkxHeader *>(header);
  return oldHeader && oldHeader->layout.littleEndian == 0;
}

template <class C> C ReadValue(const char *data, size_t offset,
                               bool swapEndian = false) {
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
const C *ReadPointer(const char *data, size_t offset, bool x64) {
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
  bool x64{};
  bool swapEndian{};
};

size_t PointerSize(bool x64) { return x64 ? 8u : 4u; }

size_t ArrayStorageSize(bool x64) { return PointerSize(x64) + 8u; }

size_t AlignOffset(size_t value, size_t alignment) {
  if (alignment == 0) {
    return value;
  }

  const size_t mask = alignment - 1;
  return (value + mask) & ~mask;
}

bool IsArrayViewInHeader(const ArrayViewWithMode &view, size_t elementSize,
                         const IhkPackFile *header) {
  if (view.view.count == 0) {
    return true;
  }

  size_t totalSize = 0;
  if (!MultiplySize(static_cast<size_t>(view.view.count), elementSize,
                    totalSize)) {
    return false;
  }

  return IsPointerInHeader(header, view.view.data, totalSize);
}

bool IsArrayViewValid(const ArrayView &view, uint32 countLimit) {
  if (view.count > countLimit) {
    return false;
  }

  if (view.count > 0 && !view.data) {
    return false;
  }

  return true;
}

ArrayView ReadArray(const char *data, size_t offset, bool x64,
                   bool swapEndian = false) {
  ArrayView output;
  output.data = ReadPointer<char>(data, offset, x64);
  output.count =
      ReadValue<uint32>(data, offset + (x64 ? static_cast<size_t>(8) : 4),
                        swapEndian);
  return output;
}

ArrayViewWithMode ReadArrayWithFallback(const char *data, size_t offset, bool x64,
                                        bool swapEndianHint,
                                        uint32 countLimit) {
  const ArrayViewWithMode candidates[] = {
      {ReadArray(data, offset, x64, swapEndianHint), x64, swapEndianHint},
      {ReadArray(data, offset, !x64, swapEndianHint), !x64, swapEndianHint},
      {ReadArray(data, offset, x64, !swapEndianHint), x64, !swapEndianHint},
      {ReadArray(data, offset, !x64, !swapEndianHint), !x64, !swapEndianHint},
  };

  for (const auto &candidate : candidates) {
    if (IsArrayViewValid(candidate.view, countLimit)) {
      return candidate;
    }
  }

  return candidates[0];
}

const void *ReadPointerArrayItem(const ArrayView &arr, size_t id, bool x64) {
  if (!arr.data || id >= arr.count) {
    return nullptr;
  }

  if (x64) {
    return reinterpret_cast<const void *const *>(arr.data)[id];
  }

  return reinterpret_cast<const es::PointerX86<char> *>(arr.data)[id];
}

bool IsModernCollision(hkToolset version) { return version >= HK2010_1; }

size_t RigidBodyCollidableOffset() { return 16; }

size_t RigidBodyNameOffset(hkToolset version) {
  return IsModernCollision(version) ? 120 : 112;
}

size_t RigidBodyPropertiesOffset(hkToolset version, bool x64) {
  return RigidBodyNameOffset(version) + PointerSize(x64);
}

size_t RigidBodyMaterialOffset(hkToolset version, bool x64) {
  return RigidBodyPropertiesOffset(version, x64) + ArrayStorageSize(x64);
}

size_t RigidBodyMotionOffset(hkToolset version) {
  return IsModernCollision(version) ? 224 : 208;
}

size_t CdBodySize(bool x64) { return x64 ? 32u : 16u; }

size_t BroadPhaseHandleOffset(bool x64) {
  return RigidBodyCollidableOffset() + CdBodySize(x64) + 4u;
}

size_t ConvexRadiusOffset() { return 16; }

size_t ConvexVerticesArrayOffset() { return 64; }

size_t ConvexVerticesNumVerticesOffset(bool x64) {
  return ConvexVerticesArrayOffset() + ArrayStorageSize(x64);
}

size_t ConvexVerticesPlaneEquationsOffset(bool x64) {
  return AlignOffset(ConvexVerticesNumVerticesOffset(x64) + sizeof(int32),
                     PointerSize(x64));
}

size_t MoppCodePointerOffset(hkToolset version) {
  return IsModernCollision(version) ? 20 : 16;
}

size_t StorageMeshArrayOffset(hkToolset version) {
  return IsModernCollision(version) ? 240 : 192;
}

size_t StorageShapeArrayOffset(hkToolset version) {
  return IsModernCollision(version) ? 252 : 204;
}

size_t ShapeSubpartShapeArrayOffset(hkToolset version) {
  return IsModernCollision(version) ? static_cast<size_t>(-1) : 8;
}

size_t MeshSubpartIndices8Offset(hkToolset version) {
  return IsModernCollision(version) ? 20 : static_cast<size_t>(-1);
}

size_t MeshSubpartIndices16Offset(hkToolset version) {
  return IsModernCollision(version) ? 32 : 20;
}

size_t MeshSubpartIndices32Offset(hkToolset version) {
  return IsModernCollision(version) ? 44 : 32;
}
} // namespace

template <class C> struct hkpMidBase : C {
  char *data = nullptr;

  explicit hkpMidBase(CRule) {}

  void SetDataPointer(void *ptr) override { data = static_cast<char *>(ptr); }
  const void *GetPointer() const override { return data; }
  void SwapEndian() override {}

  bool IsBigEndianData() const { return RequiresEndianSwap(this->header); }
};

struct hkpPhysicsDataMidInterface : hkpMidBase<hkpPhysicsDataInternalInterface> {
  using Base = hkpMidBase<hkpPhysicsDataInternalInterface>;
  using Base::Base;

  static constexpr uint32 kMaxSystems = 1 << 14;

  ArrayViewWithMode GetSystemsArray() const {
    ArrayViewWithMode systems = ReadArrayWithFallback(
        data, 12, this->rule.x64, this->IsBigEndianData(), kMaxSystems);
    const size_t pointerSize = systems.x64 ? 8u : 4u;
    if (!IsArrayViewInHeader(systems, pointerSize, this->header)) {
      return {};
    }
    return systems;
  }

  size_t GetNumSystems() const override {
    if (!IsPointerInHeader(this->header, data, 16)) {
      return 0;
    }
    return GetSystemsArray().view.count;
  }

  const hkpPhysicsSystem *GetSystem(size_t id) const override {
    const ArrayViewWithMode systems = GetSystemsArray();
    const void *item = ReadPointerArrayItem(systems.view, id, systems.x64);
    if (!IsPointerInHeader(this->header, item, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpPhysicsSystem>(this->header->GetClass(item));
  }
};

struct hkpPhysicsSystemMidInterface
    : hkpMidBase<hkpPhysicsSystemInternalInterface> {
  using Base = hkpMidBase<hkpPhysicsSystemInternalInterface>;
  using Base::Base;

  static constexpr uint32 kMaxRigidBodies = 1 << 16;

  ArrayViewWithMode GetRigidBodiesArray() const {
    ArrayViewWithMode bodies = ReadArrayWithFallback(
        data, 8, this->rule.x64, this->IsBigEndianData(), kMaxRigidBodies);
    const size_t pointerSize = bodies.x64 ? 8u : 4u;
    if (!IsArrayViewInHeader(bodies, pointerSize, this->header)) {
      return {};
    }
    return bodies;
  }

  std::string_view GetName() const override {
    const char *name = ReadPointer<char>(data, 56, this->rule.x64);
    if (!IsPointerInHeader(this->header, name, 1)) {
      return {};
    }

    return name;
  }

  bool IsActive() const override { return ReadValue<bool>(data, 64); }

  size_t GetNumRigidBodies() const override {
    if (!IsPointerInHeader(this->header, data, 16)) {
      return 0;
    }
    return GetRigidBodiesArray().view.count;
  }

  const hkpRigidBody *GetRigidBody(size_t id) const override {
    const ArrayViewWithMode rigidBodies = GetRigidBodiesArray();
    const void *item = ReadPointerArrayItem(rigidBodies.view, id, rigidBodies.x64);
    if (!IsPointerInHeader(this->header, item, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpRigidBody>(this->header->GetClass(item));
  }
};

struct hkpRigidBodyMidInterface : hkpMidBase<hkpRigidBodyInternalInterface> {
  using Base = hkpMidBase<hkpRigidBodyInternalInterface>;
  using Base::Base;

  static constexpr uint32 kMaxProperties = 1 << 16;

  ArrayViewWithMode GetPropertiesArray() const {
    ArrayViewWithMode properties = ReadArrayWithFallback(
        data, RigidBodyPropertiesOffset(this->rule.version, this->rule.x64),
        this->rule.x64, this->IsBigEndianData(), kMaxProperties);
    if (!IsArrayViewInHeader(properties, sizeof(hkpRigidBodyProperty),
                             this->header)) {
      return {};
    }
    return properties;
  }

  std::string_view GetName() const override {
    const char *name =
        ReadPointer<char>(data, RigidBodyNameOffset(this->rule.version), this->rule.x64);
    if (!IsPointerInHeader(this->header, name, 1)) {
      return {};
    }

    return name;
  }

  uint32 GetShapeKey() const override {
    const size_t offset = RigidBodyCollidableOffset() + PointerSize(this->rule.x64);
    return ReadValue<uint32>(data, offset, this->IsBigEndianData());
  }

  uint32 GetCollisionFilterInfo() const override {
    const size_t offset = BroadPhaseHandleOffset(this->rule.x64) + 8;
    return ReadValue<uint32>(data, offset, this->IsBigEndianData());
  }

  uint8 GetObjectQualityType() const override {
    return ReadValue<uint8>(data, BroadPhaseHandleOffset(this->rule.x64) + 6);
  }

  uint8 GetMotionType() const override {
    const size_t motionOffset = RigidBodyMotionOffset(this->rule.version);
    return ReadValue<uint8>(data, motionOffset + 8);
  }

  uint8 GetMaterialResponseType() const override {
    return ReadValue<uint8>(
        data, RigidBodyMaterialOffset(this->rule.version, this->rule.x64));
  }

  float GetMaterialFriction() const override {
    return ReadValue<float>(
        data, RigidBodyMaterialOffset(this->rule.version, this->rule.x64) + 4,
        this->IsBigEndianData());
  }

  float GetMaterialRestitution() const override {
    return ReadValue<float>(
        data, RigidBodyMaterialOffset(this->rule.version, this->rule.x64) + 8,
        this->IsBigEndianData());
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
    property.data = ReadValue<uint64>(propertyData, 8, properties.swapEndian);
    return property;
  }

  es::Matrix44 GetTransform() const override {
    const size_t motionOffset = RigidBodyMotionOffset(this->rule.version);
    return ReadValue<es::Matrix44>(data, motionOffset + 16, this->IsBigEndianData());
  }

  const hkpShape *GetShape() const override {
    const void *shapePtr = ReadPointer<char>(data, 16, this->rule.x64);
    if (!IsPointerInHeader(this->header, shapePtr, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpShape>(this->header->GetClass(shapePtr));
  }
};

struct hkpShapeMidInterface : hkpMidBase<hkpShapeInternalInterface> {
  using Base = hkpMidBase<hkpShapeInternalInterface>;
  using Base::Base;

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->IsBigEndianData());
  }
};

struct hkpMoppCodeMidInterface : hkpMidBase<hkpMoppCodeInternalInterface> {
  using Base = hkpMidBase<hkpMoppCodeInternalInterface>;
  using Base::Base;

  static constexpr uint32 kMaxCodeBytes = 1 << 27;
  mutable Vector4A16 offsetStorage{};

  ArrayViewWithMode GetDataArray() const {
    return ReadArrayWithFallback(data, 32, this->rule.x64, this->IsBigEndianData(),
                                 kMaxCodeBytes);
  }

  const Vector4A16 &GetOffset() const override {
    static const Vector4A16 defaultValue{};

    if (!data) {
      return defaultValue;
    }

    offsetStorage = ReadValue<Vector4A16>(data, 16, this->IsBigEndianData());
    return offsetStorage;
  }

  size_t GetDataSize() const override {
    return GetDataArray().view.count;
  }

  const uint8 *GetData() const override {
    return reinterpret_cast<const uint8 *>(GetDataArray().view.data);
  }
};

struct hkpMoppBvTreeShapeMidInterface
    : hkpMidBase<hkpMoppBvTreeShapeInternalInterface> {
  using Base = hkpMidBase<hkpMoppBvTreeShapeInternalInterface>;
  using Base::Base;

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->IsBigEndianData());
  }

  const hkpMoppCode *GetCode() const override {
    const void *codePtr = ReadPointer<char>(
        data, MoppCodePointerOffset(this->rule.version), this->rule.x64);
    if (!IsPointerInHeader(this->header, codePtr, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpMoppCode>(this->header->GetClass(codePtr));
  }

  const hkpShape *GetChildShape() const override {
    const size_t childPointerOffset = 48 + (this->rule.x64 ? 8 : 4);
    const void *shapePtr = ReadPointer<char>(data, childPointerOffset, this->rule.x64);
    if (!IsPointerInHeader(this->header, shapePtr, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpShape>(this->header->GetClass(shapePtr));
  }
};

struct hkpStorageExtendedMeshShapeMidInterface
    : hkpMidBase<hkpStorageExtendedMeshShapeInternalInterface> {
  using Base = hkpMidBase<hkpStorageExtendedMeshShapeInternalInterface>;
  using Base::Base;

  static constexpr uint32 kMaxSubparts = 1 << 16;

  ArrayViewWithMode GetMeshStorage() const {
    ArrayViewWithMode mesh = ReadArrayWithFallback(
        data, StorageMeshArrayOffset(this->rule.version), this->rule.x64,
        this->IsBigEndianData(), kMaxSubparts);
    const size_t pointerSize = mesh.x64 ? 8u : 4u;
    if (!IsArrayViewInHeader(mesh, pointerSize, this->header)) {
      return {};
    }
    return mesh;
  }

  ArrayViewWithMode GetShapeStorage() const {
    ArrayViewWithMode shapes = ReadArrayWithFallback(
        data, StorageShapeArrayOffset(this->rule.version), this->rule.x64,
        this->IsBigEndianData(), kMaxSubparts);
    const size_t pointerSize = shapes.x64 ? 8u : 4u;
    if (!IsArrayViewInHeader(shapes, pointerSize, this->header)) {
      return {};
    }
    return shapes;
  }

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->IsBigEndianData());
  }

  size_t GetNumMeshSubparts() const override {
    return GetMeshStorage().view.count;
  }

  const hkpStorageExtendedMeshShapeMeshSubpartStorage *
  GetMeshSubpart(size_t id) const override {
    const ArrayViewWithMode array = GetMeshStorage();
    const void *item = ReadPointerArrayItem(array.view, id, array.x64);
    if (!IsPointerInHeader(this->header, item, 1)) {
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
    if (!IsPointerInHeader(this->header, item, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpStorageExtendedMeshShapeShapeSubpartStorage>(
        this->header->GetClass(item));
  }
};

struct hkpStorageExtendedMeshShapeMeshSubpartStorageMidInterface
    : hkpMidBase<hkpStorageExtendedMeshShapeMeshSubpartStorageInternalInterface> {
  using Base =
      hkpMidBase<hkpStorageExtendedMeshShapeMeshSubpartStorageInternalInterface>;
  using Base::Base;

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
        data, 8, this->rule.x64, this->IsBigEndianData(), kMaxVertices);
    if (!IsArrayViewInHeader(vertices, sizeof(Vector4A16), this->header)) {
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
        data, offset, this->rule.x64, this->IsBigEndianData(), kMaxIndices);
    if (!IsArrayViewInHeader(indices, sizeof(uint8), this->header)) {
      return {};
    }
    return indices;
  }

  ArrayViewWithMode GetIndices16Array() const {
    ArrayViewWithMode indices = ReadArrayWithFallback(
        data, MeshSubpartIndices16Offset(this->rule.version), this->rule.x64,
        this->IsBigEndianData(), kMaxIndices);
    if (!IsArrayViewInHeader(indices, sizeof(uint16), this->header)) {
      return {};
    }
    return indices;
  }

  ArrayViewWithMode GetIndices32Array() const {
    ArrayViewWithMode indices = ReadArrayWithFallback(
        data, MeshSubpartIndices32Offset(this->rule.version), this->rule.x64,
        this->IsBigEndianData(), kMaxIndices);
    if (!IsArrayViewInHeader(indices, sizeof(uint32), this->header)) {
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

  bool GetTriangleIndices(size_t id, uint32 &a, uint32 &b,
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
};

struct hkpStorageExtendedMeshShapeShapeSubpartStorageMidInterface
    : hkpMidBase<hkpStorageExtendedMeshShapeShapeSubpartStorageInternalInterface> {
  using Base =
      hkpMidBase<hkpStorageExtendedMeshShapeShapeSubpartStorageInternalInterface>;
  using Base::Base;

  static constexpr uint32 kMaxShapes = 1 << 16;

  ArrayViewWithMode GetShapesArray() const {
    const size_t offset = ShapeSubpartShapeArrayOffset(this->rule.version);
    if (offset == static_cast<size_t>(-1)) {
      return {};
    }

    ArrayViewWithMode shapes = ReadArrayWithFallback(
        data, offset, this->rule.x64, this->IsBigEndianData(), kMaxShapes);
    const size_t pointerSize = shapes.x64 ? 8u : 4u;
    if (!IsArrayViewInHeader(shapes, pointerSize, this->header)) {
      return {};
    }
    return shapes;
  }

  size_t GetNumShapes() const override {
    const size_t offset = ShapeSubpartShapeArrayOffset(this->rule.version);
    if (offset == static_cast<size_t>(-1)) {
      return 0;
    }

    return GetShapesArray().view.count;
  }

  const hkpShape *GetShape(size_t id) const override {
    const size_t offset = ShapeSubpartShapeArrayOffset(this->rule.version);
    if (offset == static_cast<size_t>(-1)) {
      return nullptr;
    }

    const ArrayViewWithMode shapes = GetShapesArray();
    const void *item = ReadPointerArrayItem(shapes.view, id, shapes.x64);
    if (!IsPointerInHeader(this->header, item, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpShape>(this->header->GetClass(item));
  }
};

struct hkpListShapeMidInterface : hkpMidBase<hkpListShapeInternalInterface> {
  using Base = hkpMidBase<hkpListShapeInternalInterface>;
  using Base::Base;

  static constexpr uint32 kMaxChildren = 1 << 16;

  ArrayViewWithMode GetChildrenArray() const {
    ArrayViewWithMode children = ReadArrayWithFallback(
        data, 24, this->rule.x64, this->IsBigEndianData(), kMaxChildren);
    const size_t entrySize = children.x64 ? 24u : 16u;
    if (!IsArrayViewInHeader(children, entrySize, this->header)) {
      return {};
    }
    return children;
  }

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->IsBigEndianData());
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
    if (!IsPointerInHeader(this->header, shapePtr, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpShape>(this->header->GetClass(shapePtr));
  }
};

struct hkpConvexTransformShapeMidInterface
    : hkpMidBase<hkpConvexTransformShapeInternalInterface> {
  using Base = hkpMidBase<hkpConvexTransformShapeInternalInterface>;
  using Base::Base;

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->IsBigEndianData());
  }

  const hkpShape *GetChildShape() const override {
    const size_t childPointerOffset = 20 + (this->rule.x64 ? 8 : 4);
    const void *shapePtr = ReadPointer<char>(data, childPointerOffset, this->rule.x64);
    if (!IsPointerInHeader(this->header, shapePtr, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpShape>(this->header->GetClass(shapePtr));
  }

  es::Matrix44 GetTransform() const override {
    return ReadValue<es::Matrix44>(data, 32, this->IsBigEndianData());
  }
};

struct hkpConvexTranslateShapeMidInterface
    : hkpMidBase<hkpConvexTranslateShapeInternalInterface> {
  using Base = hkpMidBase<hkpConvexTranslateShapeInternalInterface>;
  using Base::Base;

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->IsBigEndianData());
  }

  const hkpShape *GetChildShape() const override {
    const size_t childPointerOffset = 20 + (this->rule.x64 ? 8 : 4);
    const void *shapePtr = ReadPointer<char>(data, childPointerOffset, this->rule.x64);
    if (!IsPointerInHeader(this->header, shapePtr, 1)) {
      return nullptr;
    }
    return safe_deref_cast<const hkpShape>(this->header->GetClass(shapePtr));
  }

  Vector4A16 GetTranslation() const override {
    return ReadValue<Vector4A16>(data, 32, this->IsBigEndianData());
  }
};

struct hkpBoxShapeMidInterface : hkpMidBase<hkpBoxShapeInternalInterface> {
  using Base = hkpMidBase<hkpBoxShapeInternalInterface>;
  using Base::Base;

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->IsBigEndianData());
  }
  float GetRadius() const override {
    return ReadValue<float>(data, ConvexRadiusOffset(), this->IsBigEndianData());
  }
  Vector4A16 GetHalfExtents() const override {
    return ReadValue<Vector4A16>(data, 32, this->IsBigEndianData());
  }
};

struct hkpCylinderShapeMidInterface : hkpMidBase<hkpCylinderShapeInternalInterface> {
  using Base = hkpMidBase<hkpCylinderShapeInternalInterface>;
  using Base::Base;

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->IsBigEndianData());
  }
  float GetRadius() const override {
    return ReadValue<float>(data, 20, this->IsBigEndianData());
  }
  Vector4A16 GetVertexA() const override {
    return ReadValue<Vector4A16>(data, 32, this->IsBigEndianData());
  }
  Vector4A16 GetVertexB() const override {
    return ReadValue<Vector4A16>(data, 48, this->IsBigEndianData());
  }
};

struct hkpConvexVerticesShapeMidInterface
    : hkpMidBase<hkpConvexVerticesShapeInternalInterface> {
  using Base = hkpMidBase<hkpConvexVerticesShapeInternalInterface>;
  using Base::Base;

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
        this->IsBigEndianData(), kMaxPackedVertices);
    if (!IsArrayViewInHeader(packed, sizeof(FourVectors), this->header)) {
      return {};
    }
    return packed;
  }

  ArrayViewWithMode GetPlaneEquationsArray() const {
    const ArrayViewWithMode packedVertices = GetPackedVertices();
    const bool x64 = packedVertices.view.data ? packedVertices.x64 : this->rule.x64;
    ArrayViewWithMode planes = ReadArrayWithFallback(
        data, ConvexVerticesPlaneEquationsOffset(x64), x64,
        this->IsBigEndianData(), kMaxPlaneEquations);
    if (!IsArrayViewInHeader(planes, sizeof(Vector4A16), this->header)) {
      return {};
    }
    return planes;
  }

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->IsBigEndianData());
  }

  float GetRadius() const override {
    return ReadValue<float>(data, ConvexRadiusOffset(), this->IsBigEndianData());
  }

  Vector4A16 GetAabbHalfExtents() const override {
    return ReadValue<Vector4A16>(data, 32, this->IsBigEndianData());
  }

  Vector4A16 GetAabbCenter() const override {
    return ReadValue<Vector4A16>(data, 48, this->IsBigEndianData());
  }

  size_t GetNumVertices() const override {
    const ArrayViewWithMode values = GetPackedVertices();
    const bool x64 = values.view.data ? values.x64 : this->rule.x64;
    const int32 count = ReadValue<int32>(
        data, ConvexVerticesNumVerticesOffset(x64), this->IsBigEndianData());
    if (count <= 0) {
      return 0;
    }

    return static_cast<size_t>(count);
  }

  bool GetVertex(size_t id, Vector4A16 &out) const override {
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
