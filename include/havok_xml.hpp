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

#pragma once
#include "hklib/hk_packfile.hpp"
#include "internal/hk_rootlevelcontainer.hpp"
#include "internal/hka_animationbinding.hpp"
#include "internal/hka_animationcontainer.hpp"
#include "internal/hka_annotationtrack.hpp"
#include "internal/hka_defaultanimrefframe.hpp"
#include "internal/hka_interleavedanimation.hpp"
#include "internal/hka_spline_compressor.hpp"
#include "internal/hka_splineanimation.hpp"
#include "internal/hka_skeleton.hpp"
#include "internal/hkx_environment.hpp"
#include "spike/uni/list_vector.hpp"

#define DECLARE_XMLCLASS(classname, parent)                                    \
  DECLARE_HKCLASS(classname)                                                   \
  void SwapEndian() override {}                                                \
  const void *GetPointer() const override { return this; };                    \
  void Process() override {}                                                   \
  void SetDataPointer(void *) override {}                                      \
                                                                               \
public:                                                                        \
  classname() {                                                                \
    this->AddHash(classname::GetHash());                                       \
    this->AddHash(parent::GetHash());                                          \
    this->className = #parent;                                                 \
  }

class xmlHavokFile : public IhkPackFile {
  VirtualClasses classes;
  hkToolset GetToolset() const override { return HKUNKVER; }

public:
  VirtualClasses &GetAllClasses() override { return classes; }
  template <class C> C *NewClass() {
    C *cls = new C();
    classes.emplace_back(cls);
    return cls;
  }
};

class xmlRootLevelContainer : public hkRootLevelContainerInternalInterface {
  DECLARE_XMLCLASS(xmlRootLevelContainer, hkRootLevelContainer)

  size_t Size() const override { return variants.size(); }
  const hkNamedVariant At(size_t id) const override { return variants.at(id); }
  void AddVariant(IhkVirtualClass *input, std::string_view name = {}) {
    auto className = checked_deref_cast<const hkVirtualClass>(input)->className;
    if (name.empty()) {
      name = className;
    }
    variants.push_back({name, className, input});
  }

private:
  std::vector<hkNamedVariant> variants;
};

class xmlAnimationContainer : public hkaAnimationContainerInternalInterface {
  DECLARE_XMLCLASS(xmlAnimationContainer, hkaAnimationContainer)

  size_t GetNumSkeletons() const override { return skeletons.size(); }
  const hkaSkeleton *GetSkeleton(size_t id) const override {
    return skeletons.at(id);
  }
  size_t GetNumAnimations() const override { return animations.size(); }
  const hkaAnimation *GetAnimation(size_t id) const override {
    return animations.at(id);
  }
  size_t GetNumBindings() const override { return bindings.size(); }
  const hkaAnimationBinding *GetBinding(size_t id) const override {
    return bindings.at(id);
  }
  size_t GetNumAttachments() const override { return attachments.size(); }
  const hkaBoneAttachment *GetAttachment(size_t id) const override {
    return attachments.at(id);
  }
  size_t GetNumSkins() const override { return skins.size(); }
  const hkaMeshBinding *GetSkin(size_t id) const override {
    return skins.at(id);
  }

  // private:
  std::vector<hkaSkeleton *> skeletons;
  std::vector<hkaAnimation *> animations;
  std::vector<hkaAnimationBinding *> bindings;
  std::vector<hkaBoneAttachment *> attachments;
  std::vector<hkaMeshBinding *> skins;
};

struct xmlBone : uni::Bone {
  int16 ID;
  std::string name;
  xmlBone *parent = nullptr;
  hkQTransform transform;

  uni::TransformType TMType() const override {
    return uni::TransformType::TMTYPE_RTS;
  }
  void GetTM(uni::RTSValue &out) const override { out = transform; }
  const Bone *Parent() const override { return parent; }
  size_t Index() const override { return ID; }
  std::string Name() const override { return name; }
  operator uni::Element<const uni::Bone>() const {
    return {static_cast<const uni::Bone *>(this), false};
  }
};

struct xmlRefFloat {
  std::string name;
  float value;

  xmlRefFloat() = default;
  xmlRefFloat(std::string_view _name, float _value)
      : name(_name), value(_value) {}
};

class xmlSkeleton : public hkaSkeletonInternalInterface, uni::List<uni::Bone> {
  DECLARE_XMLCLASS(xmlSkeleton, hkaSkeleton)

  std::string Name() const override { return name; }
  size_t GetNumLocalFrames() const override { return localFrames.size(); }
  hkLocalFrameOnBone GetLocalFrame(size_t id) const override {
    return localFrames.at(id);
  }
  size_t GetNumPartitions() const override { return partitions.size(); }
  hkaPartition GetPartition(size_t id) const override {
    return partitions.at(id);
  }
  size_t GetNumBones() const override { return bones.size(); }
  std::string_view GetBoneName(size_t id) const override {
    return bones.at(id)->name;
  }
  const hkQTransform *GetBoneTM(size_t id) const override {
    return &bones.at(id)->transform;
  }
  int16 GetBoneParentID(size_t id) const override {
    xmlBone *parent = bones.at(id)->parent;
    return parent ? parent->ID : -1;
  }

  size_t GetNumReferenceFloats() const override { return floats.size(); };
  float GetReferenceFloat(size_t id) const override {
    return floats.at(id).value;
  }
  size_t GetNumFloatSlots() const override { return GetNumReferenceFloats(); }
  std::string_view GetFloatSlot(size_t id) const override {
    return floats.at(id).name;
  }

  uni::SkeletonBonesConst Bones() const override {
    return uni::SkeletonBonesConst(
        dynamic_cast<const uni::List<uni::Bone> *>(this), false);
  }

private:
  size_t Size() const override { return bones.size(); }
  uni::Element<const uni::Bone> At(size_t id) const override {
    return {bones.at(id).get(), false};
  }

public:
  std::string name;
  std::vector<std::unique_ptr<xmlBone>> bones;
  std::vector<xmlRefFloat> floats;
  std::vector<hkLocalFrameOnBone> localFrames;
  std::vector<hkaPartition> partitions;
};

class xmlAnnotationTrack : public hkaAnnotationTrackInternalInterface {
  DECLARE_XMLCLASS(xmlAnnotationTrack, hkaAnnotationTrack);

  size_t Size() const override { return frames.size(); }
  const hkaAnnotationFrame At(size_t id) const override { return frames.at(id); }
  std::string_view GetName() const override { return name; }
  std::string name;
  std::vector<hkaAnnotationFrame> frames;
};

template <class _parent> class xmlAnimation : public virtual _parent {
  DECLARE_XMLCLASS(xmlAnimation, hkaAnimation);

  std::string_view GetAnimationTypeName() const override {
    return GetReflectedEnum<hkaAnimationType>()->names[animType];
  }
  hkaAnimationType GetAnimationType() const override { return animType; }
  float Duration() const override { return duration; }
  const hkaAnimatedReferenceFrame *GetExtractedMotion() const override {
    return extractedMotion;
  }
  size_t GetNumAnnotations() const override { return annotations.size(); }
  hkaAnnotationTrackPtr GetAnnotation(size_t id) const override {
    return hkaAnnotationTrackPtr(&annotations[id], false);
  }

  hkaAnimationType animType;
  float duration;
  const hkaAnimatedReferenceFrame *extractedMotion = nullptr;
  mutable std::vector<xmlAnnotationTrack> annotations;
};

class xmlInterleavedAnimation
    : public xmlAnimation<hkaAnimationLerpSampler>,
      public hkaInterleavedAnimationInternalInterface {
  DECLARE_HKCLASS(xmlInterleavedAnimation)
  void SwapEndian() override {}
  const void *GetPointer() const override { return sourcePtr ? sourcePtr : this; };
  void Process() override {}
  void SetDataPointer(void *) override {}

public:
  using transform_container = std::vector<hkQTransform>;
  using float_container = std::vector<float>;
  using transform_ptr = uni::Element<transform_container>;
  using float_ptr = uni::Element<float_container>;
  const void *sourcePtr = nullptr;
  xmlInterleavedAnimation() {
    AddHash(JenHash("hkaInterleavedSkeletalAnimation"));
    AddHash(JenHash("hkaInterleavedUncompressedAnimation"));
    className = "hkaInterleavedAnimation";
  }

  std::vector<transform_ptr> transforms;
  std::vector<float_ptr> floats;

  size_t GetNumOfTransformTracks() const override { return transforms.size(); }
  size_t GetNumOfFloatTracks() const override { return floats.size(); }

  std::string_view GetClassName(hkToolset toolset) const override {
    if (toolset > HK550)
      return "hkaInterleavedUncompressedAnimation";
    else
      return "hkaInterleavedSkeletalAnimation";
  }

  void GetFrame(size_t trackID, int32 frame, hkQTransform &out) const override {
    out = transforms[trackID]->at(frame);
  }

  size_t GetNumTransforms() const override {
    if (transforms.size())
      return transforms[0]->size() * transforms.size();

    return 0;
  }

  size_t GetNumFloats() const override {
    if (floats.size())
      return floats[0]->size() * floats.size();

    return 0;
  }

  const hkQTransform *GetTransform(size_t id) const override {
    const size_t numTracks = transforms.size();
    return &transforms[id % numTracks]->at(id / numTracks);
  }

  float GetFloat(size_t id) const override {
    const int numTracks = static_cast<int>(floats.size());
    return floats[id % numTracks]->at(id / numTracks);
  }
};

class xmlSplineCompressedAnimation
    : public xmlAnimation<hkaAnimationInternalInterface>,
      public hkaSplineCompressedAnimationInternalInterface {
  DECLARE_HKCLASS(xmlSplineCompressedAnimation)
  void SwapEndian() override {}
  const void *GetPointer() const override { return this; };
  void Process() override {}
  void SetDataPointer(void *) override {}

public:
  using transform_container = std::vector<hkQTransform>;
  using float_container = std::vector<float>;

  xmlSplineCompressedAnimation() {
    AddHash(JenHash("hkaSplineSkeletalAnimation"));
    AddHash(JenHash("hkaSplineCompressedAnimation"));
    className = "hkaSplineCompressedAnimation";
    animType = HK_SPLINE_COMPRESSED_ANIMATION;
  }

  std::string_view GetClassName(hkToolset toolset) const override {
    if (toolset > HK550)
      return "hkaSplineCompressedAnimation";
    else
      return "hkaSplineSkeletalAnimation";
  }

  bool CompressFromInterleaved(
      const xmlInterleavedAnimation &source,
      const hkaSplineCompressionSettings &settings = {},
      std::string *error = nullptr) {
    sourceTransforms.clear();
    sourceFloats.clear();

    const size_t frames = source.transforms.empty()
                              ? 0
                              : source.transforms.front()->size();
    const size_t numTracks = source.transforms.size();
    sourceTransforms.reserve(frames * numTracks);

    for (size_t frame = 0; frame < frames; frame++) {
      for (size_t track = 0; track < numTracks; track++) {
        sourceTransforms.push_back(source.transforms[track]->at(frame));
      }
    }

    const size_t numFloatTracks = source.floats.size();
    sourceFloats.reserve(frames * numFloatTracks);
    for (size_t frame = 0; frame < frames; frame++) {
      for (size_t track = 0; track < numFloatTracks; track++) {
        sourceFloats.push_back(source.floats[track]->at(frame));
      }
    }

    hkaSplineCompressionInput input;
    input.transforms = sourceTransforms.data();
    input.floats = sourceFloats.empty() ? nullptr : sourceFloats.data();
    input.numFrames = static_cast<uint32>(frames);
    input.numTransformTracks = static_cast<uint32>(numTracks);
    input.numFloatTracks = static_cast<uint32>(numFloatTracks);
    input.duration = source.Duration();
    input.settings = settings;

    compressed = {};
    if (!hkaCompressSplineAnimation(input, compressed, error)) {
      return false;
    }

    animType = HK_SPLINE_COMPRESSED_ANIMATION;
    duration = source.duration;
    frameRate = source.frameRate;
    this->numTransformTracks = static_cast<uint32>(numTracks);
    this->numFloatTracks = static_cast<uint32>(numFloatTracks);
    extractedMotion = source.extractedMotion;
    annotations = source.annotations;
    return true;
  }

  void SetCompressedData(hkaSplineCompressedData data, uint32 transforms,
                         uint32 floats, uint32 fps) {
    compressed = std::move(data);
    numTransformTracks = transforms;
    numFloatTracks = floats;
    frameRate = fps;
  }

  size_t GetNumOfTransformTracks() const override {
    return numTransformTracks;
  }

  size_t GetNumOfFloatTracks() const override { return numFloatTracks; }

  char *GetData() const override {
    if (compressed.dataBuffer.empty()) {
      return nullptr;
    }

    return const_cast<char *>(compressed.dataBuffer.data());
  }

  std::span<const uint32> GetBlockOffsets() const override {
    return compressed.blockOffsets;
  }

  std::span<const uint32> GetFloatBlockOffsets() const override {
    return compressed.floatBlockOffsets;
  }

  std::span<const uint32> GetTransformOffsets() const override {
    return compressed.transformOffsets;
  }

  std::span<const uint32> GetFloatOffsets() const override {
    return compressed.floatOffsets;
  }

  uint32 GetNumFrames() const override { return compressed.numFrames; }
  uint32 GetNumBlocks() const override { return compressed.numBlocks; }
  uint32 GetMaxFramesPerBlock() const override {
    return compressed.maxFramesPerBlock;
  }
  uint32 GetMaskAndQuantizationSize() const override {
    return compressed.maskAndQuantizationSize;
  }
  uint32 GetNumDataBuffer() const override {
    return static_cast<uint32>(compressed.dataBuffer.size());
  }
  float GetBlockDuration() const override {
    return compressed.blockDuration;
  }
  float GetBlockInverseDuration() const override {
    return compressed.blockInverseDuration;
  }
  float GetFrameDuration() const override {
    return compressed.frameDuration;
  }

  void GetValue(uni::RTSValue &output, float time,
                size_t trackID) const override {
    if (sourceTransforms.empty() || !numTransformTracks || !GetNumFrames() ||
        trackID >= numTransformTracks) {
      output = {};
      return;
    }

    const float frameFull = std::max(0.0f, time * static_cast<float>(frameRate));
    const size_t frame =
        std::min<size_t>(static_cast<size_t>(frameFull), GetNumFrames() - 1);
    output = sourceTransforms[frame * numTransformTracks + trackID];
  }

  hkaSplineCompressedData compressed;
  transform_container sourceTransforms;
  float_container sourceFloats;
  uint32 numTransformTracks = 0;
  uint32 numFloatTracks = 0;
};

class xmlDefaultAnimatedReferenceFrame
    : public hkaDefaultAnimatedReferenceFrameInternalInterface {
  DECLARE_HKCLASS(xmlDefaultAnimatedReferenceFrame)
  void SwapEndian() override {}
  const void *GetPointer() const override { return this; }
  void Process() override {}
  void SetDataPointer(void *) override {}

public:
  xmlDefaultAnimatedReferenceFrame() {
    AddHash(GetHash());
    AddHash(JenHash("hkaDefaultAnimatedReferenceFrame"));
    className = "hkaDefaultAnimatedReferenceFrame";
  }

  hkaAnimatedReferenceFrameType GetType() const override {
    return hkaAnimatedReferenceFrameType::DEFAULT;
  }
  const Vector4A16 GetUp() const override { return up; }
  const Vector4A16 GetForward() const override { return forward; }
  float GetDuration() const override { return duration; }
  size_t GetNumFrames() const override { return referenceFrames.size(); }
  const Vector4A16 &GetRefFrame(size_t id) const override {
    return referenceFrames.at(id);
  }

  Vector4A16 up;
  Vector4A16 forward;
  float duration = 0.f;
  std::vector<Vector4A16> referenceFrames;
};

class xmlAnimationBinding : public hkaAnimationBindingInternalInterface {
  DECLARE_XMLCLASS(xmlAnimationBinding, hkaAnimationBinding);
  hkaAnimation *animation = nullptr;
  std::string skeletonName;
  std::vector<int16> transformTrackToBoneIndices;
  std::vector<int16> floatTrackToFloatSlotIndices;
  std::vector<int16> partitionIndices;
  BlendHint blendHint = NORMAL;

  std::string_view GetSkeletonName() const override { return skeletonName; }
  const hkaAnimation *GetAnimation() const override { return animation; }
  BlendHint GetBlendHint() const override { return blendHint; }
  size_t GetNumTransformTrackToBoneIndices() const override {
    return transformTrackToBoneIndices.size();
  }
  int16 GetTransformTrackToBoneIndex(size_t id) const override {
    return transformTrackToBoneIndices[id];
  }
  size_t GetNumFloatTrackToFloatSlotIndices() const override {
    return floatTrackToFloatSlotIndices.size();
  }
  int16 GetFloatTrackToFloatSlotIndex(size_t id) const override {
    return floatTrackToFloatSlotIndices[id];
  }
  size_t GetNumPartitionIndices() const override {
    return partitionIndices.size();
  }
  int16 GetPartitionIndex(size_t id) const override {
    return partitionIndices[id];
  }
};

struct xmlEnvironmentVariable {
  std::string name, value;

  xmlEnvironmentVariable() = default;
  xmlEnvironmentVariable(const std::string &iname, const std::string &ivalue)
      : name(iname), value(ivalue) {}

  operator hkxEnvironmentVariable() const { return {name, value}; }
};

class xmlEnvironment
    : public hkxEnvironmentInternalInterface,
      public uni::VectorList<hkxEnvironmentVariable, xmlEnvironmentVariable,
                             uni::Vector> {
  DECLARE_XMLCLASS(xmlEnvironment, hkxEnvironment);
};
