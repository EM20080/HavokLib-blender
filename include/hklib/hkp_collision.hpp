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

#pragma once
#include "hk_base.hpp"
#include "spike/type/matrix44.hpp"
#include "spike/uni/virtual_iterator.hpp"

struct hkpPhysicsSystem;
struct hkpRigidBody;
struct hkpShape;
struct hkpMoppCode;
struct hkpStorageExtendedMeshShapeMeshSubpartStorage;
struct hkpStorageExtendedMeshShapeShapeSubpartStorage;

struct hkpPhysicsData : IhkVirtualClass {
  DECLARE_HKCLASS(hkpPhysicsData)

  virtual size_t GetNumSystems() const = 0;
  virtual const hkpPhysicsSystem *GetSystem(size_t id) const = 0;

  typedef uni::VirtualIteratorProxy<hkpPhysicsData,
                                    &hkpPhysicsData::GetNumSystems,
                                    const hkpPhysicsSystem *,
                                    &hkpPhysicsData::GetSystem>
      iteratorSystems;
  const iteratorSystems Systems() const { return iteratorSystems(this); }
};

struct hkpPhysicsSystem : IhkVirtualClass {
  DECLARE_HKCLASS(hkpPhysicsSystem)

  virtual std::string_view GetName() const = 0;
  virtual bool IsActive() const = 0;
  virtual size_t GetNumRigidBodies() const = 0;
  virtual const hkpRigidBody *GetRigidBody(size_t id) const = 0;

  typedef uni::VirtualIteratorProxy<hkpPhysicsSystem,
                                    &hkpPhysicsSystem::GetNumRigidBodies,
                                    const hkpRigidBody *,
                                    &hkpPhysicsSystem::GetRigidBody>
      iteratorRigidBodies;
  const iteratorRigidBodies RigidBodies() const {
    return iteratorRigidBodies(this);
  }
};

struct hkpRigidBody : IhkVirtualClass {
  DECLARE_HKCLASS(hkpRigidBody)

  virtual std::string_view GetName() const = 0;
  virtual es::Matrix44 GetTransform() const = 0;
  virtual const hkpShape *GetShape() const = 0;
};

struct hkpShape : IhkVirtualClass {
  DECLARE_HKCLASS(hkpShape)
  virtual uint32 GetShapeType() const = 0;
};

struct hkpMoppCode : IhkVirtualClass {
  DECLARE_HKCLASS(hkpMoppCode)

  virtual const Vector4A16 &GetOffset() const = 0;
  virtual size_t GetDataSize() const = 0;
  virtual const uint8 *GetData() const = 0;
};

struct hkpMoppBvTreeShape : hkpShape {
  DECLARE_HKCLASS(hkpMoppBvTreeShape)

  virtual const hkpMoppCode *GetCode() const = 0;
  virtual const hkpShape *GetChildShape() const = 0;
};

struct hkpStorageExtendedMeshShape : hkpShape {
  DECLARE_HKCLASS(hkpStorageExtendedMeshShape)

  virtual size_t GetNumMeshSubparts() const = 0;
  virtual const hkpStorageExtendedMeshShapeMeshSubpartStorage *
  GetMeshSubpart(size_t id) const = 0;
  virtual size_t GetNumShapeSubparts() const = 0;
  virtual const hkpStorageExtendedMeshShapeShapeSubpartStorage *
  GetShapeSubpart(size_t id) const = 0;

  typedef uni::VirtualIteratorProxy<
      hkpStorageExtendedMeshShape, &hkpStorageExtendedMeshShape::GetNumMeshSubparts,
      const hkpStorageExtendedMeshShapeMeshSubpartStorage *,
      &hkpStorageExtendedMeshShape::GetMeshSubpart>
      iteratorMeshSubparts;
  typedef uni::VirtualIteratorProxy<
      hkpStorageExtendedMeshShape,
      &hkpStorageExtendedMeshShape::GetNumShapeSubparts,
      const hkpStorageExtendedMeshShapeShapeSubpartStorage *,
      &hkpStorageExtendedMeshShape::GetShapeSubpart>
      iteratorShapeSubparts;

  const iteratorMeshSubparts MeshSubparts() const {
    return iteratorMeshSubparts(this);
  }
  const iteratorShapeSubparts ShapeSubparts() const {
    return iteratorShapeSubparts(this);
  }
};

struct hkpStorageExtendedMeshShapeMeshSubpartStorage : IhkVirtualClass {
  DECLARE_HKCLASS(hkpStorageExtendedMeshShapeMeshSubpartStorage)

  virtual size_t GetNumVertices() const = 0;
  virtual const Vector4A16 *GetVertices() const = 0;
  virtual size_t GetNumIndices8() const = 0;
  virtual const uint8 *GetIndices8() const = 0;
  virtual size_t GetNumIndices16() const = 0;
  virtual const uint16 *GetIndices16() const = 0;
  virtual size_t GetNumIndices32() const = 0;
  virtual const uint32 *GetIndices32() const = 0;
};

struct hkpStorageExtendedMeshShapeShapeSubpartStorage : IhkVirtualClass {
  DECLARE_HKCLASS(hkpStorageExtendedMeshShapeShapeSubpartStorage)

  virtual size_t GetNumShapes() const = 0;
  virtual const hkpShape *GetShape(size_t id) const = 0;

  typedef uni::VirtualIteratorProxy<
      hkpStorageExtendedMeshShapeShapeSubpartStorage,
      &hkpStorageExtendedMeshShapeShapeSubpartStorage::GetNumShapes,
      const hkpShape *,
      &hkpStorageExtendedMeshShapeShapeSubpartStorage::GetShape>
      iteratorShapes;
  const iteratorShapes Shapes() const { return iteratorShapes(this); }
};

struct hkpListShape : hkpShape {
  DECLARE_HKCLASS(hkpListShape)

  virtual size_t GetNumChildren() const = 0;
  virtual const hkpShape *GetChild(size_t id) const = 0;

  typedef uni::VirtualIteratorProxy<hkpListShape, &hkpListShape::GetNumChildren,
                                    const hkpShape *, &hkpListShape::GetChild>
      iteratorChildren;
  const iteratorChildren Children() const { return iteratorChildren(this); }
};

struct hkpConvexTransformShape : hkpShape {
  DECLARE_HKCLASS(hkpConvexTransformShape)

  virtual const hkpShape *GetChildShape() const = 0;
  virtual es::Matrix44 GetTransform() const = 0;
};

struct hkpConvexTranslateShape : hkpShape {
  DECLARE_HKCLASS(hkpConvexTranslateShape)

  virtual const hkpShape *GetChildShape() const = 0;
  virtual Vector4A16 GetTranslation() const = 0;
};

struct hkpBoxShape : hkpShape {
  DECLARE_HKCLASS(hkpBoxShape)
  virtual Vector4A16 GetHalfExtents() const = 0;
};

struct hkpCylinderShape : hkpShape {
  DECLARE_HKCLASS(hkpCylinderShape)

  virtual float GetRadius() const = 0;
  virtual Vector4A16 GetVertexA() const = 0;
  virtual Vector4A16 GetVertexB() const = 0;
};

struct hkpConvexVerticesShape : hkpShape {
  DECLARE_HKCLASS(hkpConvexVerticesShape)

  virtual size_t GetNumVertices() const = 0;
  virtual bool GetVertex(size_t id, Vector4A16 &out) const = 0;
};
