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
#include "hklib/hk_packfile.hpp"
#include "spike/type/pointer.hpp"
#include <cstring>
#include <type_traits>
#include <vector>

namespace {
uint16 ByteSwap16(uint16 value) {
  return static_cast<uint16>(((value & 0x00FFu) << 8) |
                             ((value & 0xFF00u) >> 8));
}

uint32 ByteSwap32(uint32 value) {
  return ((value & 0x000000FFu) << 24) | ((value & 0x0000FF00u) << 8) |
         ((value & 0x00FF0000u) >> 8) | ((value & 0xFF000000u) >> 24);
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

size_t RigidBodyNameOffset(hkToolset version) {
  return IsModernCollision(version) ? 120 : 112;
}

size_t RigidBodyMotionOffset(hkToolset version) {
  return IsModernCollision(version) ? 224 : 208;
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
    return ReadArrayWithFallback(data, 12, this->rule.x64, this->IsBigEndianData(),
                                 kMaxSystems);
  }

  size_t GetNumSystems() const override {
    return GetSystemsArray().view.count;
  }

  const hkpPhysicsSystem *GetSystem(size_t id) const override {
    const ArrayViewWithMode systems = GetSystemsArray();
    const void *item = ReadPointerArrayItem(systems.view, id, systems.x64);
    return safe_deref_cast<const hkpPhysicsSystem>(this->header->GetClass(item));
  }
};

struct hkpPhysicsSystemMidInterface
    : hkpMidBase<hkpPhysicsSystemInternalInterface> {
  using Base = hkpMidBase<hkpPhysicsSystemInternalInterface>;
  using Base::Base;

  static constexpr uint32 kMaxRigidBodies = 1 << 16;

  ArrayViewWithMode GetRigidBodiesArray() const {
    return ReadArrayWithFallback(data, 8, this->rule.x64, this->IsBigEndianData(),
                                 kMaxRigidBodies);
  }

  std::string_view GetName() const override {
    const char *name = ReadPointer<char>(data, 56, this->rule.x64);
    if (!name) {
      return {};
    }

    return name;
  }

  bool IsActive() const override { return ReadValue<bool>(data, 64); }

  size_t GetNumRigidBodies() const override {
    return GetRigidBodiesArray().view.count;
  }

  const hkpRigidBody *GetRigidBody(size_t id) const override {
    const ArrayViewWithMode rigidBodies = GetRigidBodiesArray();
    const void *item = ReadPointerArrayItem(rigidBodies.view, id, rigidBodies.x64);
    return safe_deref_cast<const hkpRigidBody>(this->header->GetClass(item));
  }
};

struct hkpRigidBodyMidInterface : hkpMidBase<hkpRigidBodyInternalInterface> {
  using Base = hkpMidBase<hkpRigidBodyInternalInterface>;
  using Base::Base;

  std::string_view GetName() const override {
    const char *name =
        ReadPointer<char>(data, RigidBodyNameOffset(this->rule.version), this->rule.x64);
    if (!name) {
      return {};
    }

    return name;
  }

  es::Matrix44 GetTransform() const override {
    const size_t motionOffset = RigidBodyMotionOffset(this->rule.version);
    return ReadValue<es::Matrix44>(data, motionOffset + 16, this->IsBigEndianData());
  }

  const hkpShape *GetShape() const override {
    const void *shapePtr = ReadPointer<char>(data, 16, this->rule.x64);
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
    return safe_deref_cast<const hkpMoppCode>(this->header->GetClass(codePtr));
  }

  const hkpShape *GetChildShape() const override {
    const size_t childPointerOffset = 48 + (this->rule.x64 ? 8 : 4);
    const void *shapePtr = ReadPointer<char>(data, childPointerOffset, this->rule.x64);
    return safe_deref_cast<const hkpShape>(this->header->GetClass(shapePtr));
  }
};

struct hkpStorageExtendedMeshShapeMidInterface
    : hkpMidBase<hkpStorageExtendedMeshShapeInternalInterface> {
  using Base = hkpMidBase<hkpStorageExtendedMeshShapeInternalInterface>;
  using Base::Base;

  static constexpr uint32 kMaxSubparts = 1 << 16;

  ArrayViewWithMode GetMeshStorage() const {
    return ReadArrayWithFallback(
        data, StorageMeshArrayOffset(this->rule.version), this->rule.x64,
        this->IsBigEndianData(), kMaxSubparts);
  }

  ArrayViewWithMode GetShapeStorage() const {
    return ReadArrayWithFallback(
        data, StorageShapeArrayOffset(this->rule.version), this->rule.x64,
        this->IsBigEndianData(), kMaxSubparts);
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
    return ReadArrayWithFallback(data, 8, this->rule.x64, this->IsBigEndianData(),
                                 kMaxVertices);
  }

  ArrayViewWithMode GetIndices8Array() const {
    const size_t offset = MeshSubpartIndices8Offset(this->rule.version);
    if (offset == static_cast<size_t>(-1)) {
      return {};
    }

    return ReadArrayWithFallback(data, offset, this->rule.x64, this->IsBigEndianData(),
                                 kMaxIndices);
  }

  ArrayViewWithMode GetIndices16Array() const {
    return ReadArrayWithFallback(data, MeshSubpartIndices16Offset(this->rule.version),
                                 this->rule.x64, this->IsBigEndianData(), kMaxIndices);
  }

  ArrayViewWithMode GetIndices32Array() const {
    return ReadArrayWithFallback(data, MeshSubpartIndices32Offset(this->rule.version),
                                 this->rule.x64, this->IsBigEndianData(), kMaxIndices);
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

    return ReadArrayWithFallback(data, offset, this->rule.x64, this->IsBigEndianData(),
                                 kMaxShapes);
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
    return safe_deref_cast<const hkpShape>(this->header->GetClass(item));
  }
};

struct hkpListShapeMidInterface : hkpMidBase<hkpListShapeInternalInterface> {
  using Base = hkpMidBase<hkpListShapeInternalInterface>;
  using Base::Base;

  static constexpr uint32 kMaxChildren = 1 << 16;

  ArrayViewWithMode GetChildrenArray() const {
    return ReadArrayWithFallback(data, 24, this->rule.x64, this->IsBigEndianData(),
                                 kMaxChildren);
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

  ArrayViewWithMode GetPackedVertices() const {
    return ReadArrayWithFallback(data, 64, this->rule.x64, this->IsBigEndianData(),
                                 kMaxPackedVertices);
  }

  uint32 GetShapeType() const override {
    return ReadValue<uint32>(data, 12, this->IsBigEndianData());
  }

  size_t GetNumVertices() const override {
    const int32 count = ReadValue<int32>(data, 76, this->IsBigEndianData());
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
