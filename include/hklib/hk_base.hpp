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

#pragma once
#include "spike/crypto/jenkinshash.hpp"
#include "spike/reflect/reflector.hpp"
#include "spike/uni/common.hpp"
#include "spike/uni/rts.hpp"
#include "spike/util/pugi_fwd.hpp"

#define DECLARE_HKCLASS(classname)                                             \
  static constexpr JenHash GetHash() { return JenHash(#classname); }

MAKE_ENUM(ENUMSCOPE(hkToolset : uint8, hkToolset), EMEMBER(HKUNKVER),
          EMEMBER(HK500), EMEMBER(HK510), EMEMBER(HK550), EMEMBER(HK600),
          EMEMBER(HK610), EMEMBER(HK650), EMEMBER(HK660), EMEMBER(HK700),
          EMEMBER(HK710), EMEMBER(HK2010_1), EMEMBER(HK2010_2),
          EMEMBER(HK2011_1), EMEMBER(HK2011_2), EMEMBER(HK2011_3),
          EMEMBER(HK2012_1), EMEMBER(HK2012_2), EMEMBER(HK2013),
          EMEMBER(HK2014), EMEMBER(HK2015), EMEMBER(HK2016), EMEMBER(HK2017),
          EMEMBER(HK2018), EMEMBER(HK2019));

using XMLnode = pugi::xml_node;

struct XMLHandle {
  XMLnode *node;
  hkToolset toolset;
};

struct hkVirtualClass;
struct hkRootLevelContainer;
struct hkaAnimatedReferenceFrame;
struct hkaAnimation;
struct hkaAnimationBinding;
struct hkaAnimationContainer;
struct hkaSkeleton;
struct hkxEnvironment;
struct hkaBoneAttachment;
struct hkaMeshBinding;
struct hkaAnnotationTrack;
struct hkpPhysicsData;
struct hkpPhysicsSystem;
struct hkpRigidBody;
struct hkpShape;
struct hkpMoppCode;
struct hkpMoppBvTreeShape;
struct hkpStorageExtendedMeshShape;
struct hkpStorageExtendedMeshShapeMeshSubpartStorage;
struct hkpStorageExtendedMeshShapeShapeSubpartStorage;
struct hkpListShape;
struct hkpConvexTransformShape;
struct hkpConvexTranslateShape;
struct hkpBoxShape;
struct hkpCylinderShape;
struct hkpConvexVerticesShape;

struct IhkVirtualClass {
  virtual const void *GetPointer() const = 0;
  virtual operator hkVirtualClass const *() const { return nullptr; }
  virtual operator hkRootLevelContainer const *() const { return nullptr; }
  virtual operator hkaAnimatedReferenceFrame const *() const { return nullptr; }
  virtual operator hkaAnimation const *() const { return nullptr; }
  virtual operator hkaAnimationBinding const *() const { return nullptr; }
  virtual operator hkaAnimationContainer const *() const { return nullptr; }
  virtual operator hkaSkeleton const *() const { return nullptr; }
  virtual operator hkxEnvironment const *() const { return nullptr; }
  virtual operator hkaBoneAttachment const *() const { return nullptr; }
  virtual operator hkaMeshBinding const *() const { return nullptr; }
  virtual operator hkaAnnotationTrack const *() const { return nullptr; }
  virtual operator hkpPhysicsData const *() const { return nullptr; }
  virtual operator hkpPhysicsSystem const *() const { return nullptr; }
  virtual operator hkpRigidBody const *() const { return nullptr; }
  virtual operator hkpShape const *() const { return nullptr; }
  virtual operator hkpMoppCode const *() const { return nullptr; }
  virtual operator hkpMoppBvTreeShape const *() const { return nullptr; }
  virtual operator hkpStorageExtendedMeshShape const *() const {
    return nullptr;
  }
  virtual operator hkpStorageExtendedMeshShapeMeshSubpartStorage const *()
      const {
    return nullptr;
  }
  virtual operator hkpStorageExtendedMeshShapeShapeSubpartStorage const *()
      const {
    return nullptr;
  }
  virtual operator hkpListShape const *() const { return nullptr; }
  virtual operator hkpConvexTransformShape const *() const { return nullptr; }
  virtual operator hkpConvexTranslateShape const *() const { return nullptr; }
  virtual operator hkpBoxShape const *() const { return nullptr; }
  virtual operator hkpCylinderShape const *() const { return nullptr; }
  virtual operator hkpConvexVerticesShape const *() const { return nullptr; }
  virtual ~IhkVirtualClass() = default;
};

template <class C, class D> C *safe_deref_cast(D *val) {
  if (!val)
    return nullptr;
  return static_cast<C *>(*val);
}

template <class C, class D> C *checked_deref_cast(D *val) {
  if (!val)
    throw std::bad_cast{};
  return static_cast<C *>(*val);
}

using hkQTransform = uni::RTSValue;
