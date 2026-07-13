/*  Havok Format Library
    Copyright(C) 2016-2023 Lukas Cone

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

#include "fixups.hpp"
#include "format_old.hpp"
#include "hklib/hka_animation.hpp"
#include "hklib/hka_skeleton.hpp"
#include "hklib/hk_rootlevelcontainer.hpp"
#include "hklib/hkp_collision.hpp"
#include "internal/hk_internal_api.hpp"
#include "spike/crypto/jenkinshash.hpp"
#include "spike/io/binwritter.hpp"

void hkxHeader::Save(BinWritterRef_e wr, const VirtualClasses &classes) const {
  std::unordered_map<std::string, size_t> cnOffsetMap;
  const int32 dataSectionId = 2;
  constexpr size_t npos = -1;

  wr.SwapEndian((layout.littleEndian != 0) != LittleEndian());

  bool hasCollisionClasses = false;
  for (auto &c : classes) {
    auto dc = checked_deref_cast<const hkVirtualClass>(c.get());
    if (dc->GetClassName(toolset).starts_with("hkp")) {
      hasCollisionClasses = true;
      break;
    }
  }

  hkxHeaderData hdr = *this;
  hdr.numSections = 3;
  hdr.contentsSectionIndex = dataSectionId;
  if (hasCollisionClasses) {
    if (toolset == HK550) {
      hdr.contentsClassNameSectionOffset = 902;
    } else if (toolset == HK2010_2 || toolset == HK2012_2) {
      hdr.contentsClassNameSectionOffset = 75;
    }
  }
  if (toolset == HK550)
    hdr.flags = 0xFFFFFFFF;
  hdr.maxpredicate = -1;
  hdr.predicateArraySizePlusPadding = -1;
  hdr.contentsVersion[sizeof(hdr.contentsVersion) - 1] = char(0xFF);
  wr.Write<hkxHeaderData>(hdr);

  hkxSectionHeader classSection{};
  classSection.sectionTag[sizeof(classSection.sectionTag) - 1] = char(0xFF);
  memcpy(classSection.sectionTag, "__classnames__", sizeof("__classnames__"));

  wr.Push();
  wr.Write<hkxSectionHeaderData>(classSection);

  if (version == 11) {
    wr.Skip(16);
  }

  hkxSectionHeader typeSection{};
  typeSection.sectionTag[sizeof(typeSection.sectionTag) - 1] = char(0xFF);
  memcpy(typeSection.sectionTag, "__types__", sizeof("__types__"));

  wr.Write<hkxSectionHeaderData>(typeSection);

  if (version == 11) {
    wr.Skip(16);
  }

  hkxSectionHeader mainSection{};
  mainSection.sectionTag[sizeof(mainSection.sectionTag) - 1] = char(0xFF);
  memcpy(mainSection.sectionTag, "__data__", sizeof("__data__"));

  wr.Write<hkxSectionHeaderData>(mainSection);

  if (version == 11) {
    wr.Skip(16);
  }

  classSection.absoluteDataStart = uint32(wr.Tell());
  wr.SetRelativeOrigin(wr.Tell(), false);

  VirtualClasses refClasses;
  hkFixups fixups;
  std::unordered_map<const IhkVirtualClass *, IhkVirtualClass *> clsRemap;

  auto getClassSignature = [&](std::string_view name) {
    if (toolset == HK550) {
      static const std::pair<std::string_view, uint32> signatures[] = {
          {"hkClass", 0x38771F8E},
          {"hkClassMember", 0xA5240F57},
          {"hkClassEnum", 0x8A3609CF},
          {"hkClassEnumItem", 0xCE6F8A6C},
          {"hkRootLevelContainer", 0xF598A34E},
          {"hkaAnimatedReferenceFrame", 0xDA8C7D7D},
          {"hkxMeshUserChannelInfo", 0x64E9A03C},
          {"hkxAttribute", 0x914DA6C1},
          {"hkaAnnotationTrackAnnotation", 0x731888CA},
          {"hkaSkeletalAnimation", 0x98F9313D},
          {"hkBaseObject", 0xE0708A00},
          {"hkxAttributeHolder", 0x445A443A},
          {"hkxMaterial", 0xF2EC0C9C},
          {"hkxIndexBuffer", 0x1C8A8C37},
          {"hkxTextureInplace", 0xF64B134C},
          {"hkxMaterialTextureStage", 0xE085BA9F},
          {"hkxTextureFile", 0x0217EF77},
          {"hkaInterleavedSkeletalAnimation", 0x62B02E7B},
          {"hkaSplineSkeletalAnimation", 0x30A67D2A},
          {"hkaBone", 0xA74011F0},
          {"hkxMesh", 0x72E8E849},
          {"hkxMeshSection", 0x912C8863},
          {"hkaBoneAttachment", 0x8BDD3E9A},
          {"hkaDefaultAnimatedReferenceFrame", 0x122F506B},
          {"hkaSkeleton", 0x334DBE6C},
          {"hkRootLevelContainerNamedVariant", 0x853A899C},
          {"hkxVertexFormat", 0x379FD194},
          {"hkxSkinBinding", 0xC532C710},
          {"hkaMeshBindingMapping", 0x4DA6A6F4},
          {"hkxScene", 0x1FB22361},
          {"hkxLight", 0x8E92A993},
          {"hkxNodeSelectionSet", 0x158BEA87},
          {"hkxNodeAnnotationData", 0x521E9517},
          {"hkxCamera", 0xD5C65FAE},
          {"hkxNode", 0x06AF1B5A},
          {"hkxAttributeGroup", 0x1667C01C},
          {"hkxVertexBuffer", 0x57061454},
          {"hkaMeshBinding", 0xCDB31E0C},
          {"hkReferencedObject", 0x3B1C1113},
          {"hkaAnimationContainer", 0xF456626D},
          {"hkaAnnotationTrack", 0x846FC690},
          {"hkaAnimationBinding", 0xFB496074},
          {"hkAabb", 0x4A948B16},
          {"hkpShapeContainer", 0xE0708A00},
          {"hkpEntity", 0xFA798537},
          {"hkpMaterial", 0x0485A264},
          {"hkpMoppCode", 0x72EE59F8},
          {"hkpSphereRepShape", 0xE7ECA7EB},
          {"hkAabbUint32", 0xAC084729},
          {"hkpRigidBody", 0x3224DE76},
          {"hkpStorageExtendedMeshShapeMeshSubpartStorage", 0x3B2F0B51},
          {"hkpExtendedMeshShapeSubpart", 0x7C9435C7},
          {"hkpStorageExtendedMeshShape", 0xC22CA89E},
          {"hkpExtendedMeshShape", 0x260C3908},
          {"hkpConstraintInstance", 0x2033B565},
          {"hkMultiThreadCheck", 0x1C3C8820},
          {"hkpExtendedMeshShapeShapesSubpart", 0xFCE016A3},
          {"hkpConstraintData", 0xC18F0D87},
          {"hkpStorageExtendedMeshShapeShapeSubpartStorage", 0x502939ED},
          {"hkpTypedBroadPhaseHandle", 0xA539881B},
          {"hkpAction", 0xB6966E59},
          {"hkpWorldObject", 0x50F6EE9F},
          {"hkpMoppCodeCodeInfo", 0xD8FDBB08},
          {"hkMotionState", 0x064E7CE4},
          {"hkpPhantom", 0xAA0ADE1A},
          {"hkpBoxShape", 0x3444D2D5},
          {"hkpEntitySmallArraySerializeOverrideType", 0xEB2364C1},
          {"hkpPhysicsData", 0xC2A461E4},
          {"hkWorldMemoryAvailableWatchDog", 0x0D13F6B4},
          {"hkpPhysicsSystem", 0x1B58F0EF},
          {"hkSweptTransform", 0x0B4E5770},
          {"hkpCollidable", 0x5879A2C3},
          {"hkpBroadPhaseHandle", 0x940569DC},
          {"hkpShapeCollection", 0x9B1A3265},
          {"hkpExtendedMeshShapeTrianglesSubpart", 0x3375309C},
          {"hkpKeyframedRigidMotion", 0x8B0A2DBF},
          {"hkMoppBvTreeShapeBase", 0x4117D60E},
          {"hkpMoppBvTreeShape", 0xDBBA3C29},
          {"hkpBvTreeShape", 0xE7ECA7EB},
          {"hkpModifierConstraintAtom", 0xBB7A348B},
          {"hkpSingleShapeContainer", 0x73AA1D38},
          {"hkpCollisionFilter", 0x60960336},
          {"hkpProperty", 0x9CE308E9},
          {"hkpLinkedCollidable", 0x4E89E0F0},
          {"hkpWorldCinfo", 0x3FCD7295},
          {"hkpMaxSizeMotion", 0xABE8F4F8},
          {"hkpPropertyValue", 0xC75925AA},
          {"hkpCdBody", 0xE94D2688},
          {"hkpShape", 0x666490A1},
          {"hkpStorageSampledHeightFieldShape", 0x5AA633E0},
          {"hkpTriSampledHeightFieldBvTreeShape", 0x8C608221},
          {"hkpTriSampledHeightFieldCollection", 0xCB2ECF39},
          {"hkpCollidableBoundingVolumeData", 0x97B45527},
          {"hkpMotion", 0x141ABDC9},
          {"hkpConvexListFilter", 0x81D074A4},
          {"hkpConvexShape", 0xF8F74F85},
          {"hkpEntitySpuCollisionCallback", 0x81147F05},
          {"hkpConstraintAtom", 0xED982D04},
      };

      for (auto &sig : signatures) {
        if (sig.first == name) {
          return sig.second;
        }
      }
    } else if (toolset == HK2010_1 || toolset == HK2010_2) {
      static const std::pair<std::string_view, uint32> signatures[] = {
          {"hkClass", 0x75585EF6},
          {"hkClassMember", 0x5C7EA4C2},
          {"hkClassEnum", 0x8A3609CF},
          {"hkClassEnumItem", 0xCE6F8A6C},
          {"hkRootLevelContainer", 0x2772C11E},
          {"hkxScene", 0x5F673DDD},
          {"hkaAnimationContainer", 0x8DC20333},
          {"hkaInterleavedUncompressedAnimation", 0x930AF031},
          {"hkaSplineCompressedAnimation", 0x792EE0BB},
          {"hkaQuantizedAnimation", 0x3920F053},
          {"hkaDefaultAnimatedReferenceFrame", 0x6D85E445},
          {"hkaAnimationBinding", 0x66EAC971},
          {"hkpPhysicsData", 0xC2A461E4},
          {"hkpPhysicsSystem", 0xFF724C17},
          {"hkpRigidBody", 0x75F8D805},
          {"hkpMoppBvTreeShape", 0x90B29D39},
          {"hkpMoppCode", 0x924C2661},
          {"hkpStorageExtendedMeshShape", 0xB469EFBC},
          {"hkpStorageExtendedMeshShapeMeshSubpartStorage", 0x5AAD4DE6},
          {"hkpBoxShape", 0x3444D2D5},
          {"hkpCompressedSampledHeightFieldShape", 0x97B6E143},
      };

      for (auto &sig : signatures) {
        if (sig.first == name) {
          return sig.second;
        }
      }
    } else if (toolset == HK2012_1 || toolset == HK2012_2) {
      static const std::pair<std::string_view, uint32> signatures[] = {
          {"hkClass", 0x33D42383},
          {"hkClassMember", 0xB0EFA719},
          {"hkClassEnum", 0x8A3609CF},
          {"hkClassEnumItem", 0xCE6F8A6C},
          {"hkRootLevelContainer", 0x2772C11E},
          {"hkxScene", 0x3637A8EC},
          {"hkaAnimationContainer", 0x8DC20333},
          {"hkaSplineCompressedAnimation", 0xA57D6A61},
          {"hkaDefaultAnimatedReferenceFrame", 0xB287B5E8},
          {"hkaAnimationBinding", 0xA808529F},
          {"hkpPhysicsData", 0xC2A461E4},
          {"hkpPhysicsSystem", 0xFF724C17},
          {"hkpRigidBody", 0xD0313594},
          {"hkpStaticCompoundShape", 0xE198603F},
          {"hkpBvCompressedMeshShape", 0x7A192C95},
          {"hkpMoppBvTreeShape", 0x96CE87D2},
          {"hkpMoppCode", 0x924C2661},
          {"hkpStorageExtendedMeshShape", 0x30C05D40},
          {"hkpStorageExtendedMeshShapeMeshSubpartStorage", 0x5AAD4DE6},
          {"hkpBoxShape", 0x0C1112EA},
      };

      for (auto &sig : signatures) {
        if (sig.first == name) {
          return sig.second;
        }
      }
    }

    return JenHash(name).raw();
  };

  auto writeClassName = [&](std::string_view name) {
    wr.Write(getClassSignature(name));
    wr.Write('\t');
    cnOffsetMap[std::string(name)] = wr.Tell();
    wr.WriteContainer(name);
    wr.Skip(1);
  };

  static const std::string_view reqClassNames[] = {
      "hkClass", "hkClassMember", "hkClassEnum", "hkClassEnumItem"};
  for (auto &c : reqClassNames) {
    writeClassName(c);
  }

  auto writeOldClassNames = [&](const auto &classNames) {
    for (auto &c : classNames) {
      if (!cnOffsetMap.contains(std::string(c))) {
        writeClassName(c);
      }
    }
  };

  if (toolset == HK550) {
    static const std::string_view legacy550AnimationClassNames[] = {
        "hkRootLevelContainer",
        "hkaAnimatedReferenceFrame",
        "hkxMeshUserChannelInfo",
        "hkxAttribute",
        "hkaAnnotationTrackAnnotation",
        "hkaSkeletalAnimation",
        "hkBaseObject",
        "hkxAttributeHolder",
        "hkxMaterial",
        "hkxIndexBuffer",
        "hkxTextureInplace",
        "hkxMaterialTextureStage",
        "hkxTextureFile",
        "hkaInterleavedSkeletalAnimation",
        "hkaSplineSkeletalAnimation",
        "hkaBone",
        "hkxMesh",
        "hkxMeshSection",
        "hkaBoneAttachment",
        "hkaDefaultAnimatedReferenceFrame",
        "hkaSkeleton",
        "hkRootLevelContainerNamedVariant",
        "hkxVertexFormat",
        "hkxSkinBinding",
        "hkaMeshBindingMapping",
        "hkxScene",
        "hkxLight",
        "hkxNodeSelectionSet",
        "hkxNodeAnnotationData",
        "hkxCamera",
        "hkxNode",
        "hkxAttributeGroup",
        "hkxVertexBuffer",
        "hkaMeshBinding",
        "hkReferencedObject",
        "hkaAnimationContainer",
        "hkaAnnotationTrack",
        "hkaAnimationBinding",
    };

    static const std::string_view hk550CollisionClassNames[] = {
        "hkpPhantom",
        "hkpBoxShape",
        "hkAabb",
        "hkpEntitySmallArraySerializeOverrideType",
        "hkpPhysicsData",
        "hkWorldMemoryAvailableWatchDog",
        "hkpPhysicsSystem",
        "hkSweptTransform",
        "hkpShapeContainer",
        "hkpCollidable",
        "hkpBroadPhaseHandle",
        "hkpEntity",
        "hkpShapeCollection",
        "hkpMaterial",
        "hkpExtendedMeshShapeTrianglesSubpart",
        "hkpKeyframedRigidMotion",
        "hkMoppBvTreeShapeBase",
        "hkpMoppCode",
        "hkAabbUint32",
        "hkpSphereRepShape",
        "hkpMoppBvTreeShape",
        "hkpRigidBody",
        "hkpStorageExtendedMeshShapeMeshSubpartStorage",
        "hkpBvTreeShape",
        "hkpExtendedMeshShapeSubpart",
        "hkpStorageExtendedMeshShape",
        "hkpExtendedMeshShape",
        "hkpModifierConstraintAtom",
        "hkpSingleShapeContainer",
        "hkReferencedObject",
        "hkpCollisionFilter",
        "hkpConstraintInstance",
        "hkpProperty",
        "hkRootLevelContainer",
        "hkpLinkedCollidable",
        "hkMultiThreadCheck",
        "hkpWorldCinfo",
        "hkpMaxSizeMotion",
        "hkpPropertyValue",
        "hkpCdBody",
        "hkBaseObject",
        "hkpShape",
        "hkpStorageSampledHeightFieldShape",
        "hkpTriSampledHeightFieldBvTreeShape",
        "hkpTriSampledHeightFieldCollection",
        "hkpExtendedMeshShapeShapesSubpart",
        "hkpCollidableBoundingVolumeData",
        "hkpConstraintData",
        "hkpStorageExtendedMeshShapeShapeSubpartStorage",
        "hkpTypedBroadPhaseHandle",
        "hkpMotion",
        "hkpWorldObject",
        "hkpAction",
        "hkpConvexListFilter",
        "hkpConvexShape",
        "hkRootLevelContainerNamedVariant",
        "hkpEntitySpuCollisionCallback",
        "hkpMoppCodeCodeInfo",
        "hkMotionState",
        "hkpConstraintAtom",
    };

    if (hasCollisionClasses) {
      writeOldClassNames(hk550CollisionClassNames);
    } else {
      writeOldClassNames(legacy550AnimationClassNames);
    }
  }

  if (hasCollisionClasses &&
      (toolset == HK2010_2 || toolset == HK2012_2)) {
    static const std::string_view hk2010CollisionClassNames[] = {
        "hkRootLevelContainer",
        "hkxScene",
        "hkpPhysicsData",
        "hkpPhysicsSystem",
        "hkpRigidBody",
        "hkpStaticCompoundShape",
        "hkpBvCompressedMeshShape",
        "hkpMoppBvTreeShape",
        "hkpMoppCode",
        "hkpStorageExtendedMeshShape",
        "hkpStorageExtendedMeshShapeMeshSubpartStorage",
        "hkpBoxShape",
        "hkpCompressedSampledHeightFieldShape",
    };
    writeOldClassNames(hk2010CollisionClassNames);
  }

  CRule rule(toolset, layout.reusePaddingOptimization,
             layout.bytesInPointer > 4);

  for (auto &c : classes) {
    auto dc = checked_deref_cast<const hkVirtualClass>(c.get());
    auto clName = dc->GetClassName(toolset);
    auto nClass = hkVirtualClass::Create(clName, rule);

    if (!nClass) {
      continue;
    }

    auto cls = const_cast<hkVirtualClass *>(
        checked_deref_cast<const hkVirtualClass>(nClass));

    clsRemap[c.get()] = nClass;
    cls->Reflect(c.get());

    if (wr.SwappedEndian()) {
      cls->SwapEndian();
    }

    refClasses.emplace_back(nClass);

    auto mapIt = cnOffsetMap.find(std::string(clName));
    if (mapIt != cnOffsetMap.end()) {
      fixups.finals.emplace_back(mapIt->second);
    } else {
      const size_t entryOff = wr.Tell();
      writeClassName(clName);
      size_t nameOff = entryOff + 5;
      fixups.finals.emplace_back(nameOff);
    }

    if (toolset == HK550 && clName == "hkaSkeleton") {
      size_t boneOff;
      auto boneIt = cnOffsetMap.find("hkaBone");
      if (boneIt != cnOffsetMap.end()) {
        boneOff = boneIt->second;
      } else {
        const size_t entryOff = wr.Tell();
        writeClassName("hkaBone");
        boneOff = entryOff + 5;
      }

      const size_t numBones =
          checked_deref_cast<const hkaSkeleton>(c.get())->GetNumBones();

      for (size_t i = 0; i < numBones; i++) {
        fixups.finals.emplace_back(boneOff, c.get());
      }
    }

    if (toolset == HK550) {
      auto hkAnim = safe_deref_cast<const hkaAnimation>(c.get());

      if (hkAnim && hkAnim->GetNumAnnotations()) {
        size_t trackOff;
        auto trackIt = cnOffsetMap.find("hkaAnnotationTrack");
        if (trackIt != cnOffsetMap.end()) {
          trackOff = trackIt->second;
        } else {
          const size_t entryOff = wr.Tell();
          writeClassName("hkaAnnotationTrack");
          trackOff = entryOff + 5;
        }

        for (size_t i = 0; i < hkAnim->GetNumAnnotations(); i++) {
          fixups.finals.emplace_back(trackOff, c.get());
        }
      }
    }

    if (toolset < HK700) {
      if (auto root = safe_deref_cast<const hkRootLevelContainer>(c.get())) {
        for (auto &variant : *root) {
          if (variant.pointer ||
              std::string_view(variant.className) != "hkxScene") {
            continue;
          }

          if (cnOffsetMap.contains("hkxScene")) {
            break;
          }

          writeClassName("hkxScene");
          break;
        }
      }
    }
  }

  wr.ResetRelativeOrigin(false);
  if (hasCollisionClasses && toolset == HK550 &&
      fixups.finals.size() == refClasses.size()) {
    std::unordered_map<IhkVirtualClass *, size_t> refIndex;
    refIndex.reserve(refClasses.size());
    for (size_t i = 0; i < refClasses.size(); i++) {
      refIndex[refClasses[i].get()] = i;
    }

    std::vector<size_t> orderedIndices;
    std::vector<uint8> used(refClasses.size(), 0);
    orderedIndices.reserve(refClasses.size());

    auto appendSourceClass = [&](const IhkVirtualClass *source) {
      if (!source) {
        return;
      }

      auto mapped = clsRemap.find(source);
      if (mapped == clsRemap.end() || !mapped->second) {
        return;
      }

      auto index = refIndex.find(mapped->second);
      if (index == refIndex.end() || used[index->second]) {
        return;
      }

      used[index->second] = 1;
      orderedIndices.emplace_back(index->second);
    };

    auto appendShapeTree = [&](const hkpShape *shape, const auto &self) -> void {
      if (!shape) {
        return;
      }

      appendSourceClass(shape);

      if (const auto *mopp = safe_deref_cast<const hkpMoppBvTreeShape>(shape)) {
        appendSourceClass(mopp->GetCode());
        self(mopp->GetChildShape(), self);
        return;
      }

      if (const auto *tree =
              safe_deref_cast<const hkpTriSampledHeightFieldBvTreeShape>(
                  shape)) {
        const auto *collection = tree->GetChildShape();
        appendSourceClass(collection);
        if (collection) {
          self(collection->GetHeightField(), self);
        }
        return;
      }

      if (const auto *collection =
              safe_deref_cast<const hkpTriSampledHeightFieldCollection>(
                  shape)) {
        self(collection->GetHeightField(), self);
        return;
      }

      if (const auto *storage =
              safe_deref_cast<const hkpStorageExtendedMeshShape>(shape)) {
        for (size_t i = 0; i < storage->GetNumMeshSubparts(); i++) {
          appendSourceClass(storage->GetMeshSubpart(i));
        }
        for (size_t i = 0; i < storage->GetNumShapeSubparts(); i++) {
          const auto *subpart = storage->GetShapeSubpart(i);
          appendSourceClass(subpart);
          if (!subpart) {
            continue;
          }
          for (size_t s = 0; s < subpart->GetNumShapes(); s++) {
            self(subpart->GetShape(s), self);
          }
        }
        return;
      }

      if (const auto *list = safe_deref_cast<const hkpListShape>(shape)) {
        for (size_t i = 0; i < list->GetNumChildren(); i++) {
          self(list->GetChild(i), self);
        }
        return;
      }

      if (const auto *compound =
              safe_deref_cast<const hkpStaticCompoundShape>(shape)) {
        for (size_t i = 0; i < compound->GetNumInstances(); i++) {
          self(compound->GetInstance(i).shape, self);
        }
      }
    };

    auto appendPhysicsData = [&](const hkpPhysicsData *physicsData) {
      appendSourceClass(physicsData);
      if (!physicsData) {
        return;
      }

      for (size_t systemIndex = 0; systemIndex < physicsData->GetNumSystems();
           systemIndex++) {
        const auto *system = physicsData->GetSystem(systemIndex);
        appendSourceClass(system);
        if (!system) {
          continue;
        }

        for (size_t bodyIndex = 0; bodyIndex < system->GetNumRigidBodies();
             bodyIndex++) {
          const auto *body = system->GetRigidBody(bodyIndex);
          appendSourceClass(body);
          if (body) {
            appendShapeTree(body->GetShape(), appendShapeTree);
          }
        }
      }
    };

    for (const auto &sourceClass : classes) {
      if (const auto *root =
              safe_deref_cast<const hkRootLevelContainer>(sourceClass.get())) {
        appendSourceClass(root);
        for (const auto &variant : *root) {
          if (const auto *physicsData =
                  safe_deref_cast<const hkpPhysicsData>(variant.pointer)) {
            appendPhysicsData(physicsData);
          } else {
            appendSourceClass(variant.pointer);
          }
        }
      }
    }

    for (size_t i = 0; i < refClasses.size(); i++) {
      if (!used[i]) {
        orderedIndices.emplace_back(i);
      }
    }

    VirtualClasses orderedClasses;
    std::vector<hkFixup> orderedFinals;
    orderedClasses.reserve(refClasses.size());
    orderedFinals.reserve(fixups.finals.size());
    for (size_t index : orderedIndices) {
      orderedClasses.emplace_back(std::move(refClasses[index]));
      orderedFinals.emplace_back(std::move(fixups.finals[index]));
    }
    refClasses = std::move(orderedClasses);
    fixups.finals = std::move(orderedFinals);
  }

  if (hasCollisionClasses &&
      (toolset == HK550 || toolset == HK2010_2 || toolset == HK2012_2)) {
    while (GetPadding(wr.Tell(), 16)) {
      wr.Write<uint8>(0xFF);
    }
  } else {
    if (toolset == HK2012_1 || toolset == HK2012_2) {
      while (GetPadding(wr.Tell(), 16)) {
        wr.Write<uint8>(0xFF);
      }
    } else {
      wr.ApplyPadding();
    }
  }
  classSection.bufferSize = uint32(wr.Tell() - classSection.absoluteDataStart);
  classSection.exportsOffset = classSection.bufferSize;
  classSection.globalFixupsOffset = classSection.bufferSize;
  classSection.importsOffset = classSection.bufferSize;
  classSection.localFixupsOffset = classSection.bufferSize;
  classSection.virtualFixupsOffset = classSection.bufferSize;
  wr.ApplyPadding();

  typeSection.absoluteDataStart = uint32(wr.Tell());
  typeSection.bufferSize = 0;
  typeSection.exportsOffset = 0;
  typeSection.globalFixupsOffset = 0;
  typeSection.importsOffset = 0;
  typeSection.localFixupsOffset = 0;
  typeSection.virtualFixupsOffset = 0;
  wr.ApplyPadding();

  mainSection.absoluteDataStart = uint32(wr.Tell());
  wr.SetRelativeOrigin(wr.Tell(), false);

  if (hasCollisionClasses &&
      (toolset == HK550 || toolset == HK2010_2 || toolset == HK2012_2)) {
    size_t rootIndex = npos;
    size_t sceneIndex = npos;

    for (size_t i = 0; i < refClasses.size(); i++) {
      auto cls = checked_deref_cast<const hkVirtualClass>(refClasses[i].get());
      const auto clName = cls->GetClassName(toolset);
      if (clName == "hkRootLevelContainer") {
        rootIndex = i;
      } else if (clName == "hkxScene") {
        sceneIndex = i;
      }
    }

    if (rootIndex != npos && sceneIndex != npos &&
        sceneIndex != rootIndex + 1) {
      auto sceneClass = std::move(refClasses[sceneIndex]);
      auto sceneFinal = std::move(fixups.finals[sceneIndex]);
      refClasses.erase(refClasses.begin() + sceneIndex);
      fixups.finals.erase(fixups.finals.begin() + sceneIndex);
      if (sceneIndex < rootIndex) {
        rootIndex--;
      }
      refClasses.insert(refClasses.begin() + rootIndex + 1,
                        std::move(sceneClass));
      fixups.finals.insert(fixups.finals.begin() + rootIndex + 1,
                           std::move(sceneFinal));
    }
  }

  size_t curFixup = 0;
  std::unordered_map<IhkVirtualClass *, size_t> savedClasses;
  size_t legacySceneOffset = npos;

  auto writeLegacySceneData = [&]() {
    if (legacySceneOffset != npos ||
        !fixups.legacyScene || fixups.legacyScene->data.empty()) {
      return;
    }

    wr.ApplyPadding();
    legacySceneOffset = wr.Tell();
    wr.WriteBuffer(fixups.legacyScene->data.data(),
                   fixups.legacyScene->data.size());
  };

  size_t legacySceneLocalFixupsWritten = 0;
  auto writeLegacySceneLocalFixups = [&]() {
    if (legacySceneLocalFixupsWritten || !fixups.legacyScene ||
        legacySceneOffset == npos) {
      return;
    }

    for (const auto &lf : fixups.legacyScene->localFixups) {
      mainSection.localFixups.emplace_back(legacySceneOffset + lf.pointer,
                                           legacySceneOffset + lf.destination);
    }

    legacySceneLocalFixupsWritten = fixups.legacyScene->localFixups.size();
  };

  size_t legacySceneGlobalFixupsWritten = 0;
  auto writeLegacySceneGlobalFixup = [&]() {
    if (legacySceneGlobalFixupsWritten || !fixups.legacyScene ||
        legacySceneOffset == npos || fixups.legacyScene->variantPtrOff == npos) {
      return;
    }

    mainSection.globalFixups.emplace_back(
        fixups.legacyScene->variantPtrOff, dataSectionId, legacySceneOffset);
    legacySceneGlobalFixupsWritten = 1;
  };

  for (auto &c : refClasses) {
    wr.ApplyPadding();
    const auto clsOffset = wr.Tell();
    savedClasses[c.get()] = clsOffset;
    fixups.finals[curFixup++].destination = clsOffset;

    auto cls = checked_deref_cast<const hkVirtualClass>(c.get());
    const auto clName = cls->GetClassName(toolset);
    cls->Save(wr, fixups);
    if (clsOffset == 0) {
      writeLegacySceneData();
    }

    while (curFixup < fixups.finals.size() &&
           fixups.finals[curFixup].destClass) {
      curFixup++;
    }
  }

  writeLegacySceneData();

  for (auto &l : fixups.locals) {
    if (legacySceneOffset != npos &&
        l.strOffset >= legacySceneOffset) {
      writeLegacySceneLocalFixups();
    }

    if (fixups.legacyScene &&
        fixups.legacyScene->variantPtrOff != npos &&
        l.strOffset > fixups.legacyScene->variantPtrOff) {
      writeLegacySceneGlobalFixup();
    }

    if (l.destClass) {
      auto sClass = clsRemap[l.destClass];
      if (sClass) {
        mainSection.globalFixups.emplace_back(l.strOffset, dataSectionId,
                                              savedClasses[sClass]);
      }
    } else if (l.destination != npos) {
      mainSection.localFixups.emplace_back(l.strOffset, l.destination);
    }
  }
  writeLegacySceneLocalFixups();
  writeLegacySceneGlobalFixup();

  size_t legacySceneVirtualFixupsWritten = 0;
  for (auto &l : fixups.finals) {
    mainSection.virtualFixups.emplace_back(l.destination, 0, l.strOffset);

    const auto rootName = cnOffsetMap.find("hkRootLevelContainer");
    if (!legacySceneVirtualFixupsWritten &&
        legacySceneOffset != npos &&
        rootName != cnOffsetMap.end() && l.strOffset == rootName->second) {
      auto cnIt = cnOffsetMap.find(fixups.legacyScene->className);
      if (cnIt != cnOffsetMap.end()) {
        mainSection.virtualFixups.emplace_back(legacySceneOffset, 0,
                                               cnIt->second);
        legacySceneVirtualFixupsWritten = 1;
      }
    }
  }

  for (auto &l : fixups.globals) {
    mainSection.globalFixups.emplace_back(l.strOffset, dataSectionId,
                                          l.destination);
  }

  if (fixups.legacyScene && legacySceneOffset != npos) {
    if (!legacySceneVirtualFixupsWritten) {
      auto cnIt = cnOffsetMap.find(fixups.legacyScene->className);
      if (cnIt != cnOffsetMap.end()) {
        mainSection.virtualFixups.emplace_back(legacySceneOffset, 0,
                                               cnIt->second);
      }
    }
  }

  wr.ApplyPadding();
  mainSection.localFixupsOffset = int32(wr.Tell());
  wr.WriteContainer(mainSection.localFixups);
  if (mainSection.localFixups.size() & 1) {
    wr.Write<int64>(-1);
  }

  wr.ApplyPadding();
  mainSection.globalFixupsOffset = int32(wr.Tell());
  wr.WriteContainer(mainSection.globalFixups);
  if (hasCollisionClasses && toolset == HK550) {
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
  }
  const size_t preVirtualPad = GetPadding(wr.Tell(), 16) / 4;
  for (size_t p = 0; p < preVirtualPad; p++) {
    wr.Write<int32>(-1);
  }
  mainSection.virtualFixupsOffset = int32(wr.Tell());
  wr.WriteContainer(mainSection.virtualFixups);

  const size_t pad = GetPadding(wr.Tell(), 16) / 4;

  for (size_t p = 0; p < pad; p++) {
    wr.Write<int32>(-1);
  }

  mainSection.bufferSize = int32(wr.Tell());
  mainSection.exportsOffset = mainSection.bufferSize;
  mainSection.importsOffset = mainSection.bufferSize;

  wr.ResetRelativeOrigin(false);
  wr.Pop();
  wr.Write<hkxSectionHeaderData>(classSection);

  if (version == 11) {
    wr.Skip(16);
  }

  wr.Write<hkxSectionHeaderData>(typeSection);

  if (version == 11) {
    wr.Skip(16);
  }

  wr.Write<hkxSectionHeaderData>(mainSection);
}
