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
#include "types.inl"

const TypeEntry *FindTypes(hkToolset toolset, uint8 profile) {
  for (const TypeEntry &entry : typeEntries) {
    if (entry.toolset == toolset && entry.profile == profile) {
      return &entry;
    }
  }
  return nullptr;
}

uint32 Havok300MetadataValue(const unsigned char *data, size_t offset = 0) {
  return uint32(data[offset]) | (uint32(data[offset + 1]) << 8) |
         (uint32(data[offset + 2]) << 16) |
         (uint32(data[offset + 3]) << 24);
}

struct Havok300MetadataView {
  const unsigned char *metadata;
  uint32 classSize;
  uint32 typeSize;
  uint32 localCount;
  uint32 globalCount;
  uint32 virtualCount;
  uint32 rootClassNameOffset;
  const unsigned char *classData;
  const unsigned char *typeData;

  explicit Havok300MetadataView(uint32 profile,
                                bool littleEndian = false) {
    metadata = havok300Metadata + 8 * sizeof(uint32);
    for (uint32 index = 0; index < profile; index++) {
      metadata += Havok300MetadataValue(havok300Metadata,
                                        index * sizeof(uint32));
    }
    classSize = Havok300MetadataValue(metadata);
    typeSize = Havok300MetadataValue(metadata, 4);
    localCount = Havok300MetadataValue(metadata, 8);
    globalCount = Havok300MetadataValue(metadata, 12);
    virtualCount = Havok300MetadataValue(metadata, 16);
    rootClassNameOffset = Havok300MetadataValue(metadata, 20);
    const unsigned char *bigEndianClassData = metadata + 24;
    if (littleEndian) {
      classData = bigEndianClassData + classSize + typeSize;
    } else {
      classData = bigEndianClassData;
    }
    typeData = classData + classSize;
  }

  const unsigned char *TypeData() const { return typeData; }
  const unsigned char *LocalFixups() const {
    return metadata + 24 + (classSize + typeSize) * 2;
  }
  const unsigned char *GlobalFixups() const {
    return LocalFixups() + localCount * 8;
  }
  const unsigned char *VirtualFixups() const {
    return GlobalFixups() + globalCount * 12;
  }
};

uint32 MetadataFixupValue(const unsigned char *data, size_t offset) {
  return uint32(data[offset]) | (uint32(data[offset + 1]) << 8) |
         (uint32(data[offset + 2]) << 16) |
         (uint32(data[offset + 3]) << 24);
}

void SwapType16(std::vector<unsigned char> &data, size_t offset) {
  std::swap(data[offset], data[offset + 1]);
}

void SwapType32(std::vector<unsigned char> &data, size_t offset) {
  std::swap(data[offset], data[offset + 3]);
  std::swap(data[offset + 1], data[offset + 2]);
}

void WriteClassNames(BinWritterRef_e wr, const TypeEntry &types,
                     bool swapEndian) {
  if (!swapEndian) {
    wr.WriteBuffer(reinterpret_cast<const char *>(types.ClassData()),
                   types.classSize);
    return;
  }

  const unsigned char *source = types.ClassData();
  std::vector<unsigned char> data(source, source + types.classSize);
  size_t offset = 0;
  while (offset + 5 <= data.size() && data[offset] != 0xff &&
         data[offset + 4] == '\t') {
    SwapType32(data, offset);
    offset += 5 + std::string_view(reinterpret_cast<const char *>(
                                      source + offset + 5))
                      .size() +
              1;
  }
  wr.WriteBuffer(reinterpret_cast<const char *>(data.data()), data.size());
}

void WriteTypes(BinWritterRef_e wr, const TypeEntry &types, bool swapEndian) {
  if (!swapEndian) {
    wr.WriteBuffer(reinterpret_cast<const char *>(types.TypeData()),
                   types.typeSize);
    return;
  }

  const unsigned char *source = types.TypeData();
  std::vector<unsigned char> data(source, source + types.typeSize);

  auto localDestination = [&](size_t pointer) {
    for (size_t offset = types.typeLocal;
         offset + 8 <= types.typeGlobal; offset += 8) {
      if (MetadataFixupValue(source, offset) == pointer) {
        return size_t(MetadataFixupValue(source, offset + 4));
      }
    }
    return ~size_t{};
  };

  auto swapMember = [&](size_t offset) {
    SwapType16(data, offset + 14);
    SwapType16(data, offset + 16);
    SwapType16(data, offset + 18);
  };

  auto swapEnum = [&](size_t offset) {
    const uint32 numItems = MetadataFixupValue(source, offset + 8);
    const size_t items = localDestination(offset + 4);
    SwapType32(data, offset + 8);
    SwapType32(data, offset + 16);
    if (items != ~size_t{}) {
      for (uint32 index = 0; index < numItems; index++) {
        SwapType32(data, items + index * 8);
      }
    }
  };

  auto swapClass = [&](size_t offset) {
    const uint32 numEnums = MetadataFixupValue(source, offset + 20);
    const uint32 numMembers = MetadataFixupValue(source, offset + 28);
    const size_t enums = localDestination(offset + 16);
    const size_t members = localDestination(offset + 24);
    if (enums != ~size_t{}) {
      for (uint32 index = 0; index < numEnums; index++) {
        swapEnum(enums + index * 20);
      }
    }
    if (members != ~size_t{}) {
      for (uint32 index = 0; index < numMembers; index++) {
        swapMember(members + index * 24);
      }
    }
    SwapType32(data, offset + 8);
    SwapType32(data, offset + 12);
    SwapType32(data, offset + 20);
    SwapType32(data, offset + 28);
    SwapType32(data, offset + 40);
    SwapType32(data, offset + 44);
  };

  for (size_t offset = types.typeVirtual;
       offset + 12 <= types.typeExports; offset += 12) {
    const uint32 objectOffset = MetadataFixupValue(source, offset);
    if (objectOffset == uint32(-1)) {
      break;
    }
    const uint32 nameOffset = MetadataFixupValue(source, offset + 8);
    const std::string_view name(reinterpret_cast<const char *>(
        types.ClassData() + nameOffset));
    if (name == "hkClass") {
      swapClass(objectOffset);
    } else if (name == "hkClassEnum") {
      swapEnum(objectOffset);
    }
  }

  for (size_t offset = types.typeLocal;
       offset + 8 <= types.typeGlobal; offset += 8) {
    SwapType32(data, offset);
    SwapType32(data, offset + 4);
  }
  for (size_t offset = types.typeGlobal;
       offset + 12 <= types.typeVirtual; offset += 12) {
    SwapType32(data, offset);
    SwapType32(data, offset + 4);
    SwapType32(data, offset + 8);
  }
  for (size_t offset = types.typeVirtual;
       offset + 12 <= types.typeExports; offset += 12) {
    SwapType32(data, offset);
    SwapType32(data, offset + 4);
    SwapType32(data, offset + 8);
  }

  wr.WriteBuffer(reinterpret_cast<const char *>(data.data()), data.size());
}

size_t TypeClassOffset(const TypeEntry &types, std::string_view className) {
  const unsigned char *data = types.TypeData();
  for (size_t v = types.typeVirtual; v + 12 <= types.typeExports; v += 12) {
    const uint32 objectOffset = MetadataFixupValue(data, v);
    if (objectOffset == uint32(-1)) {
      break;
    }
    for (size_t l = types.typeLocal; l + 8 <= types.typeGlobal; l += 8) {
      if (MetadataFixupValue(data, l) != objectOffset) {
        continue;
      }
      const uint32 nameOffset = MetadataFixupValue(data, l + 4);
      if (nameOffset < types.typeLocal &&
          className == reinterpret_cast<const char *>(data + nameOffset)) {
        return objectOffset;
      }
      break;
    }
  }
  return ~size_t{};
}

size_t Havok300TypeClassOffset(std::string_view className,
                               uint32 profile) {
  const Havok300MetadataView metadata(profile);
  for (size_t v = 0; v < metadata.virtualCount; v++) {
    const unsigned char *virtualFixup = metadata.VirtualFixups() + v * 12;
    const uint32 objectOffset = MetadataFixupValue(virtualFixup, 0);
    for (size_t l = 0; l < metadata.localCount; l++) {
      const unsigned char *localFixup = metadata.LocalFixups() + l * 8;
      if (MetadataFixupValue(localFixup, 0) != objectOffset) {
        continue;
      }
      const uint32 nameOffset = MetadataFixupValue(localFixup, 4);
      const char *name = reinterpret_cast<const char *>(metadata.TypeData() +
                                                        nameOffset);
      if (className == name) {
        return objectOffset;
      }
    }
  }
  return ~size_t{};
}

void hkxHeader::Save(BinWritterRef_e wr, const VirtualClasses &classes) const {
  std::unordered_map<std::string, size_t> cnOffsetMap;
  const int32 dataSectionId = 2;
  constexpr size_t npos = ~size_t{};

  // The STH2006 files use big-endian 0x4001, but 3.3 AssetCc accepts the
  // little-endian 0x4101 layout. Preserve the caller's requested endianness;
  // unlike Havok 5.5 collision, 3.3 is not tied to one of those two rules.
  wr.SwapEndian((layout.littleEndian != 0) != LittleEndian());

  bool hasCollisionClasses = false;
  bool hasSkeletonClasses = false;
  uint32 havok300MetadataProfile = 0;
  for (auto &c : classes) {
    if (safe_deref_cast<const hkpPhysicsData>(c.get()) ||
        safe_deref_cast<const hkpPhysicsSystem>(c.get()) ||
        safe_deref_cast<const hkpRigidBody>(c.get()) ||
        safe_deref_cast<const hkpShape>(c.get())) {
      hasCollisionClasses = true;
    }
    if (safe_deref_cast<const hkpConvexVerticesShape>(c.get())) {
      havok300MetadataProfile |= 1;
    }
    if (safe_deref_cast<const hkpCylinderShape>(c.get())) {
      havok300MetadataProfile |= 2;
    }
    if (safe_deref_cast<const hkpListShape>(c.get())) {
      havok300MetadataProfile |= 4;
    }
    if (safe_deref_cast<const hkaSkeleton>(c.get())) {
      hasSkeletonClasses = true;
    }
  }

  const uint8 typeProfile = hasCollisionClasses ? 2
                            : hasSkeletonClasses ? 1
                                                 : 0;
  const TypeEntry *types =
      writeMetadata && toolset != HK330B2
          ? FindTypes(toolset, typeProfile)
          : nullptr;

  hkxHeaderData hdr = *this;
  hdr.numSections = 3;
  hdr.contentsSectionIndex = dataSectionId;
  if (types) {
    hdr.contentsClassNameSectionOffset =
        static_cast<int32>(types->contentsClassNameOffset);
  } else if (hasCollisionClasses) {
    if (toolset == HK330B2) {
      hdr.contentsClassNameSectionOffset =
          Havok300MetadataView(havok300MetadataProfile).rootClassNameOffset;
    } else if (toolset == HK510) {
      hdr.contentsClassNameSectionOffset = 733;
    } else if (toolset == HK550) {
      hdr.contentsClassNameSectionOffset = 902;
    } else if (toolset == HK2010_2 || toolset == HK2012_2 ||
               toolset == HK2014 || toolset == HK2014_2) {
      hdr.contentsClassNameSectionOffset = 75;
    }
  }
  if (toolset == HK510 || toolset == HK550)
    hdr.flags = 0xFFFFFFFF;
  if (toolset == HK2014 || toolset == HK2014_2) {
    hdr.maxpredicate = 21;
    hdr.predicateArraySizePlusPadding = 0;
  } else {
    hdr.maxpredicate = -1;
    hdr.predicateArraySizePlusPadding = -1;
  }
  hdr.contentsVersion[sizeof(hdr.contentsVersion) - 1] = char(0xFF);
  wr.Write<hkxHeaderData>(hdr);

  hkxSectionHeader classSection{};
  classSection.sectionTag[sizeof(classSection.sectionTag) - 1] = char(0xFF);
  memcpy(classSection.sectionTag, "__classnames__", sizeof("__classnames__"));

  wr.Push();
  wr.Write<hkxSectionHeaderData>(classSection);

  if (version == 11) {
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
  }

  hkxSectionHeader typeSection{};
  typeSection.sectionTag[sizeof(typeSection.sectionTag) - 1] = char(0xFF);
  memcpy(typeSection.sectionTag, "__types__", sizeof("__types__"));

  wr.Write<hkxSectionHeaderData>(typeSection);

  if (version == 11) {
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
  }

  hkxSectionHeader mainSection{};
  mainSection.sectionTag[sizeof(mainSection.sectionTag) - 1] = char(0xFF);
  memcpy(mainSection.sectionTag, "__data__", sizeof("__data__"));

  wr.Write<hkxSectionHeaderData>(mainSection);

  if (version == 11) {
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
  }

  classSection.absoluteDataStart = uint32(wr.Tell());
  wr.SetRelativeOrigin(wr.Tell(), false);

  VirtualClasses refClasses;
  hkFixups fixups;
  std::unordered_map<const IhkVirtualClass *, IhkVirtualClass *> clsRemap;

  auto getClassSignature = [&](std::string_view name) {
    if (toolset == HK330B2) {
      static const std::pair<std::string_view, uint32> signatures[] = {
          {"hkClass", 0x14425E51},
          {"hkClassMember", 0x4A986551},
          {"hkClassEnum", 0x8A3609CF},
          {"hkClassEnumItem", 0xCE6F8A6C},
          {"hkRootLevelContainer", 0x12A4E063},
          {"hkPhysicsData", 0xC2A461E4},
          {"hkPhysicsSystem", 0xE15F41A4},
          {"hkRigidBody", 0x33B07A1E},
          {"hkSpatialRigidBodyDeactivator", 0x7AA84797},
          {"hkSphereMotion", 0xE6F30C22},
          {"hkBoxShape", 0x9D45AD25},
          {"hkConvexTranslateShape", 0xD5CCC442},
          {"hkConvexVerticesShape", 0x03363466},
          {"hkCylinderShape", 0x6C787842},
          {"hkListShape", 0x39C84054},
      };

      for (auto &sig : signatures) {
        if (sig.first == name) {
          return sig.second;
        }
      }
    } else if (toolset == HK510) {
      static const std::pair<std::string_view, uint32> signatures[] = {
          {"hkClass", 0xFC41DC67},
          {"hkClassMember", 0x258A78EE},
          {"hkClassEnum", 0x528CE1E5},
          {"hkClassEnumItem", 0xCE6F8A6C},
          {"hkpPhantom", 0xAA0ADE1A},
          {"hkAabb", 0x4A948B16},
          {"hkpEntitySmallArraySerializeOverrideType", 0xEB2364C1},
          {"hkpPhysicsData", 0xC2A461E4},
          {"hkWorldMemoryAvailableWatchDog", 0x0D13F6B4},
          {"hkpPhysicsSystem", 0x1B58F0EF},
          {"hkSweptTransform", 0x0B4E5770},
          {"hkpShapeContainer", 0xE0708A00},
          {"hkpCollidable", 0x5879A2C3},
          {"hkpBroadPhaseHandle", 0x940569DC},
          {"hkpEntity", 0xFA798537},
          {"hkpShapeCollection", 0x9B1A3265},
          {"hkpMaterial", 0x0485A264},
          {"hkpSimpleMeshShape", 0x9040D7BA},
          {"hkpKeyframedRigidMotion", 0x8B0A2DBF},
          {"hkMoppBvTreeShapeBase", 0x4117D60E},
          {"hkpMoppCode", 0xBD097996},
          {"hkAabbUint32", 0xAC084729},
          {"hkpMoppBvTreeShape", 0xDBBA3C29},
          {"hkpRigidBody", 0x3224DE76},
          {"hkpBvTreeShape", 0xE7ECA7EB},
          {"hkpModifierConstraintAtom", 0x83C56F2E},
          {"hkpSingleShapeContainer", 0x73AA1D38},
          {"hkReferencedObject", 0x3B1C1113},
          {"hkpCollisionFilter", 0x250EE68F},
          {"hkpSimpleMeshShapeTriangle", 0xD38738C1},
          {"hkpConstraintInstance", 0x2033B565},
          {"hkpProperty", 0x9CE308E9},
          {"hkRootLevelContainer", 0xF598A34E},
          {"hkpLinkedCollidable", 0x4E89E0F0},
          {"hkMultiThreadCheck", 0x1C3C8820},
          {"hkpWorldCinfo", 0xFB086D6B},
          {"hkpMaxSizeMotion", 0xABE8F4F8},
          {"hkpPropertyValue", 0xC75925AA},
          {"hkpCdBody", 0xE94D2688},
          {"hkBaseObject", 0xE0708A00},
          {"hkpShape", 0x666490A1},
          {"hkpConstraintData", 0xF9515B8A},
          {"hkpCollidableBoundingVolumeData", 0x97B45527},
          {"hkpTypedBroadPhaseHandle", 0xA539881B},
          {"hkpMotion", 0x141ABDC9},
          {"hkpWorldObject", 0x50F6EE9F},
          {"hkpAction", 0xB6966E59},
          {"hkpConvexListFilter", 0x81D074A4},
          {"hkRootLevelContainerNamedVariant", 0x853A899C},
          {"hkpEntitySpuCollisionCallback", 0x81147F05},
          {"hkpConstrainedSystemFilter", 0x9F20D23D},
          {"hkpMoppCodeCodeInfo", 0xD8FDBB08},
          {"hkMotionState", 0x064E7CE4},
          {"hkpConstraintAtom", 0x357781EE},
      };

      for (auto &sig : signatures) {
        if (sig.first == name) {
          return sig.second;
        }
      }
    } else if (toolset == HK550) {
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
    } else if (toolset == HK2014 || toolset == HK2014_2) {
      static const std::pair<std::string_view, uint32> signatures[] = {
          {"hkClass", 0x33D42383},
          {"hkClassMember", 0xB0EFA719},
          {"hkClassEnum", 0x8A3609CF},
          {"hkClassEnumItem", 0xCE6F8A6C},
          {"hkRootLevelContainer", 0x2772C11E},
          {"hkpPhysicsData", 0x47A8CA83},
          {"hkpPhysicsSystem", 0xB3CC6E64},
          {"hkpRigidBody", 0xCD2E69E5},
          {"hkpConvexVerticesShape", 0xC21C8B5A},
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

  if (toolset == HK330B2) {
    const Havok300MetadataView metadata(havok300MetadataProfile,
                                        layout.littleEndian != 0);
    wr.WriteBuffer(reinterpret_cast<const char *>(metadata.classData),
                   metadata.classSize);
    size_t offset = 0;
    while (offset + 5 <= metadata.classSize) {
      if (metadata.classData[offset] == 0xff ||
          metadata.classData[offset + 4] != '\t') {
        break;
      }
      const char *name = reinterpret_cast<const char *>(metadata.classData +
                                                        offset + 5);
      cnOffsetMap[name] = offset + 5;
      offset += 5 + std::string_view(name).size() + 1;
    }
  } else if (types) {
    WriteClassNames(wr, *types, toolset == HK550 && !layout.littleEndian);
    size_t offset = 0;
    const unsigned char *classData = types->ClassData();
    while (offset + 5 <= types->classSize) {
      if (classData[offset] == 0xff || classData[offset + 4] != '\t') {
        break;
      }
      const char *name = reinterpret_cast<const char *>(
          classData + offset + 5);
      cnOffsetMap[name] = offset + 5;
      offset += 5 + std::string_view(name).size() + 1;
    }
  } else {
    static const std::string_view reqClassNames[] = {
        "hkClass", "hkClassMember", "hkClassEnum", "hkClassEnumItem"};
    for (auto &c : reqClassNames) {
      writeClassName(c);
    }
  }

  auto writeOldClassNames = [&](const auto &classNames) {
    if (types) {
      return;
    }
    for (auto &c : classNames) {
      if (!cnOffsetMap.contains(std::string(c))) {
        writeClassName(c);
      }
    }
  };

  if (toolset == HK510 && hasCollisionClasses) {
    static const std::string_view hk510CollisionClassNames[] = {
        "hkpPhantom",
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
        "hkpSimpleMeshShape",
        "hkpKeyframedRigidMotion",
        "hkMoppBvTreeShapeBase",
        "hkpMoppCode",
        "hkAabbUint32",
        "hkpMoppBvTreeShape",
        "hkpRigidBody",
        "hkpBvTreeShape",
        "hkpModifierConstraintAtom",
        "hkpSingleShapeContainer",
        "hkReferencedObject",
        "hkpCollisionFilter",
        "hkpSimpleMeshShapeTriangle",
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
        "hkpConstraintData",
        "hkpCollidableBoundingVolumeData",
        "hkpTypedBroadPhaseHandle",
        "hkpMotion",
        "hkpWorldObject",
        "hkpAction",
        "hkpConvexListFilter",
        "hkRootLevelContainerNamedVariant",
        "hkpEntitySpuCollisionCallback",
        "hkpConstrainedSystemFilter",
        "hkpMoppCodeCodeInfo",
        "hkMotionState",
        "hkpConstraintAtom",
    };
    writeOldClassNames(hk510CollisionClassNames);
  }

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
        "hkpMoppBvTreeShape",
        "hkpMoppCode",
        "hkpStorageExtendedMeshShape",
        "hkpStorageExtendedMeshShapeMeshSubpartStorage",
        "hkpBoxShape",
        "hkpCompressedSampledHeightFieldShape",
    };
    writeOldClassNames(hk2010CollisionClassNames);

    if (toolset == HK2012_2) {
      static const std::string_view hk2012CollisionClassNames[] = {
          "hkpStaticCompoundShape",
          "hkpBvCompressedMeshShape",
      };
      writeOldClassNames(hk2012CollisionClassNames);
    }
  }

  CRule rule(toolset, layout.reusePaddingOptimization,
             layout.bytesInPointer > 4);

  for (auto &c : classes) {
    auto dc = checked_deref_cast<const hkVirtualClass>(c.get());
    auto clName = dc->GetClassName(toolset);
    if (toolset <= HK550) {
      if (clName == "hkaSplineCompressedAnimation") {
        clName = "hkaSplineSkeletalAnimation";
      } else if (clName == "hkaInterleavedUncompressedAnimation") {
        clName = "hkaInterleavedSkeletalAnimation";
      }
    }
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
  if (hasCollisionClasses && (toolset == HK510 || toolset == HK550) &&
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
      (toolset == HK510 || toolset == HK550 || toolset == HK2010_2 ||
       toolset == HK2012_2 || toolset == HK2014 || toolset == HK2014_2)) {
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
  if (toolset == HK330B2) {
    const Havok300MetadataView metadata(havok300MetadataProfile,
                                        layout.littleEndian != 0);
    wr.SetRelativeOrigin(wr.Tell(), false);
    wr.WriteBuffer(reinterpret_cast<const char *>(metadata.TypeData()),
                   metadata.typeSize);
    typeSection.localFixupsOffset = int32(wr.Tell());
    for (size_t i = 0; i < metadata.localCount; i++) {
      const unsigned char *fixup = metadata.LocalFixups() + i * 8;
      wr.Write<int32>(MetadataFixupValue(fixup, 0));
      wr.Write<int32>(MetadataFixupValue(fixup, 4));
    }
    while (GetPadding(wr.Tell(), 16)) {
      wr.Write<int32>(-1);
    }
    typeSection.globalFixupsOffset = int32(wr.Tell());
    for (size_t i = 0; i < metadata.globalCount; i++) {
      const unsigned char *fixup = metadata.GlobalFixups() + i * 12;
      wr.Write<int32>(MetadataFixupValue(fixup, 0));
      wr.Write<int32>(MetadataFixupValue(fixup, 4));
      wr.Write<int32>(MetadataFixupValue(fixup, 8));
    }
    while (GetPadding(wr.Tell(), 16)) {
      wr.Write<int32>(-1);
    }
    typeSection.virtualFixupsOffset = int32(wr.Tell());
    for (size_t i = 0; i < metadata.virtualCount; i++) {
      const unsigned char *fixup = metadata.VirtualFixups() + i * 12;
      wr.Write<int32>(MetadataFixupValue(fixup, 0));
      wr.Write<int32>(MetadataFixupValue(fixup, 4));
      wr.Write<int32>(MetadataFixupValue(fixup, 8));
    }
    while (GetPadding(wr.Tell(), 16)) {
      wr.Write<int32>(-1);
    }
    typeSection.exportsOffset = int32(wr.Tell());
    typeSection.importsOffset = typeSection.exportsOffset;
    typeSection.bufferSize = typeSection.exportsOffset;
    wr.ResetRelativeOrigin(false);
  } else if (types) {
    WriteTypes(wr, *types, toolset == HK550 && !layout.littleEndian);
    typeSection.localFixupsOffset = types->typeLocal;
    typeSection.globalFixupsOffset = types->typeGlobal;
    typeSection.virtualFixupsOffset = types->typeVirtual;
    typeSection.exportsOffset = types->typeExports;
    typeSection.importsOffset = types->typeImports;
    typeSection.bufferSize = types->typeSize;
  } else {
    typeSection.bufferSize = 0;
    typeSection.exportsOffset = 0;
    typeSection.globalFixupsOffset = 0;
    typeSection.importsOffset = 0;
    typeSection.localFixupsOffset = 0;
    typeSection.virtualFixupsOffset = 0;
  }
  wr.ApplyPadding();

  mainSection.absoluteDataStart = uint32(wr.Tell());
  wr.SetRelativeOrigin(wr.Tell(), false);

  if (hasCollisionClasses &&
      (toolset == HK510 || toolset == HK550 || toolset == HK2010_2 ||
       toolset == HK2012_2)) {
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

  for (auto &object : fixups.virtualObjects) {
    auto name = cnOffsetMap.find(object.className);
    if (name != cnOffsetMap.end()) {
      mainSection.virtualFixups.emplace_back(object.offset, 0, name->second);
    }
  }

  for (auto &l : fixups.globals) {
    mainSection.globalFixups.emplace_back(l.strOffset, dataSectionId,
                                          l.destination);
  }

  for (auto &typeClass : fixups.typeClasses) {
    size_t destination = npos;
    if (toolset == HK330B2) {
      destination = Havok300TypeClassOffset(typeClass.className,
                                            havok300MetadataProfile);
    } else if (types) {
      destination = TypeClassOffset(*types, typeClass.className);
    }
    if (destination != npos) {
      mainSection.globalFixups.emplace_back(typeClass.offset, 1, destination);
    }
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
  if (hasCollisionClasses && (toolset == HK510 || toolset == HK550)) {
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
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
  }

  wr.Write<hkxSectionHeaderData>(typeSection);

  if (version == 11) {
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
    wr.Write<int32>(-1);
  }

  wr.Write<hkxSectionHeaderData>(mainSection);
}
