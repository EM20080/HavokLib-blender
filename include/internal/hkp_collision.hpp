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
#include "hk_internal_api.hpp"
#include "hklib/hkp_collision.hpp"

struct hkpPhysicsDataInternalInterface : hkpPhysicsData, hkVirtualClass {
  operator hkpPhysicsData const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpPhysicsSystemInternalInterface : hkpPhysicsSystem, hkVirtualClass {
  operator hkpPhysicsSystem const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpRigidBodyInternalInterface : hkpRigidBody, hkVirtualClass {
  operator hkpRigidBody const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpShapeInternalInterface : hkpShape, hkVirtualClass {
  operator hkpShape const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpMoppCodeInternalInterface : hkpMoppCode, hkVirtualClass {
  operator hkpMoppCode const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpMoppBvTreeShapeInternalInterface : hkpMoppBvTreeShape, hkVirtualClass {
  hkpMoppBvTreeShapeInternalInterface() { AddHash(hkpShape::GetHash()); }
  operator hkpMoppBvTreeShape const *() const override { return this; }
  operator hkpShape const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpStorageExtendedMeshShapeInternalInterface
    : hkpStorageExtendedMeshShape, hkVirtualClass {
  hkpStorageExtendedMeshShapeInternalInterface() { AddHash(hkpShape::GetHash()); }
  operator hkpStorageExtendedMeshShape const *() const override { return this; }
  operator hkpShape const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpStorageExtendedMeshShapeMeshSubpartStorageInternalInterface
    : hkpStorageExtendedMeshShapeMeshSubpartStorage, hkVirtualClass {
  operator hkpStorageExtendedMeshShapeMeshSubpartStorage const *() const override {
    return this;
  }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpStorageExtendedMeshShapeShapeSubpartStorageInternalInterface
    : hkpStorageExtendedMeshShapeShapeSubpartStorage, hkVirtualClass {
  operator hkpStorageExtendedMeshShapeShapeSubpartStorage const *() const override {
    return this;
  }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpListShapeInternalInterface : hkpListShape, hkVirtualClass {
  hkpListShapeInternalInterface() { AddHash(hkpShape::GetHash()); }
  operator hkpListShape const *() const override { return this; }
  operator hkpShape const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpConvexTransformShapeInternalInterface
    : hkpConvexTransformShape, hkVirtualClass {
  hkpConvexTransformShapeInternalInterface() { AddHash(hkpShape::GetHash()); }
  operator hkpConvexTransformShape const *() const override { return this; }
  operator hkpShape const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpConvexTranslateShapeInternalInterface
    : hkpConvexTranslateShape, hkVirtualClass {
  hkpConvexTranslateShapeInternalInterface() { AddHash(hkpShape::GetHash()); }
  operator hkpConvexTranslateShape const *() const override { return this; }
  operator hkpShape const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpBoxShapeInternalInterface : hkpBoxShape, hkVirtualClass {
  hkpBoxShapeInternalInterface() { AddHash(hkpShape::GetHash()); }
  operator hkpBoxShape const *() const override { return this; }
  operator hkpShape const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpCylinderShapeInternalInterface : hkpCylinderShape, hkVirtualClass {
  hkpCylinderShapeInternalInterface() { AddHash(hkpShape::GetHash()); }
  operator hkpCylinderShape const *() const override { return this; }
  operator hkpShape const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};

struct hkpConvexVerticesShapeInternalInterface
    : hkpConvexVerticesShape, hkVirtualClass {
  hkpConvexVerticesShapeInternalInterface() { AddHash(hkpShape::GetHash()); }
  operator hkpConvexVerticesShape const *() const override { return this; }
  operator hkpShape const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};
