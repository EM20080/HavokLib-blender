// This file has been automatically generated. Do not modify.
#include "../hka_animation.inl"
namespace clgen::hkaLosslessCompressedAnimation {
enum Members {
  basehkaAnimation,
  dynamicRotations,
  dynamicScales,
  dynamicTranslations,
  floats,
  numDynamicRotations,
  numDynamicScales,
  numDynamicTranslations,
  numFloats,
  numFrames,
  numRotationTypeAndOffsets,
  numScaleTypeAndOffsets,
  numStaticRotations,
  numStaticScales,
  numStaticTranslations,
  numTranslationTypeAndOffsets,
  rotationTypeAndOffsets,
  scaleTypeAndOffsets,
  staticRotations,
  staticScales,
  staticTranslations,
  translationTypeAndOffsets,
  _count_,
};
static const std::set<ClassData<_count_>> LAYOUTS {
  {{{{HK2014, HK2014, 8, 0}}, 224}, {0, 104, 152, 56, 200, 112, 160, 64, 208, 216, 144, 192, 128, 176, 80, 96, 136, 184, 120, 168, 72, 88}, {0xa800, 0xaaaa, 0x0}},
  {{{{HK2014, HK2014, 4, 0}}, 156}, {0, 68, 104, 32, 140, 72, 108, 36, 144, 152, 96, 132, 84, 120, 48, 60, 92, 128, 80, 116, 44, 56}, {0xa800, 0xaaaa, 0x0}},
  {{{{HK2014, HK2014, 8, 1}}, 216}, {0, 96, 144, 48, 192, 104, 152, 56, 200, 208, 136, 184, 120, 168, 72, 88, 128, 176, 112, 160, 64, 80}, {0xa800, 0xaaaa, 0x0}},
  {{{{HK2014, HK2014, 4, 1}}, 156}, {0, 68, 104, 32, 140, 72, 108, 36, 144, 152, 96, 132, 84, 120, 48, 60, 92, 128, 80, 116, 44, 56}, {0xa800, 0xaaaa, 0x0}}
};
struct Interface {
  Interface(char *data_, LayoutLookup layout_): data{data_}, layout{GetLayout(LAYOUTS, {layout_, {LookupFlag::Ptr ,LookupFlag::Padding}})}, lookup{layout_} {}
  Interface(const Interface&) = default;
  Interface(Interface&&) = default;
  Interface &operator=(const Interface&) = default;
  Interface &operator=(Interface&&) = default;
  uint16 LayoutVersion() const { return lookup.version; }
  hkaAnimation::Interface BasehkaAnimation() const {
    int16 off = m(Members::basehkaAnimation); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  Pointer<float> DynamicTranslationsPtr() {
    int16 off = m(Members::dynamicTranslations); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  float *DynamicTranslations() {
    int16 off = m(Members::dynamicTranslations); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<float**>(data + off);
    return *reinterpret_cast<es::PointerX86<float>*>(data + off);
  }
  const float *DynamicTranslations() const {
    int16 off = m(Members::dynamicTranslations); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<float**>(data + off);
    return *reinterpret_cast<es::PointerX86<float>*>(data + off);
  }
  uint32 NumDynamicTranslations() const { return m(Members::numDynamicTranslations) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numDynamicTranslations)); }
  Pointer<float> StaticTranslationsPtr() {
    int16 off = m(Members::staticTranslations); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  float *StaticTranslations() {
    int16 off = m(Members::staticTranslations); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<float**>(data + off);
    return *reinterpret_cast<es::PointerX86<float>*>(data + off);
  }
  const float *StaticTranslations() const {
    int16 off = m(Members::staticTranslations); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<float**>(data + off);
    return *reinterpret_cast<es::PointerX86<float>*>(data + off);
  }
  uint32 NumStaticTranslations() const { return m(Members::numStaticTranslations) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numStaticTranslations)); }
  Pointer<USVector4> TranslationTypeAndOffsetsPtr() {
    int16 off = m(Members::translationTypeAndOffsets); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  USVector4 *TranslationTypeAndOffsets() {
    int16 off = m(Members::translationTypeAndOffsets); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<USVector4**>(data + off);
    return *reinterpret_cast<es::PointerX86<USVector4>*>(data + off);
  }
  const USVector4 *TranslationTypeAndOffsets() const {
    int16 off = m(Members::translationTypeAndOffsets); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<USVector4**>(data + off);
    return *reinterpret_cast<es::PointerX86<USVector4>*>(data + off);
  }
  uint32 NumTranslationTypeAndOffsets() const { return m(Members::numTranslationTypeAndOffsets) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numTranslationTypeAndOffsets)); }
  Pointer<Vector4A16> DynamicRotationsPtr() {
    int16 off = m(Members::dynamicRotations); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  Vector4A16 *DynamicRotations() {
    int16 off = m(Members::dynamicRotations); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<Vector4A16**>(data + off);
    return *reinterpret_cast<es::PointerX86<Vector4A16>*>(data + off);
  }
  const Vector4A16 *DynamicRotations() const {
    int16 off = m(Members::dynamicRotations); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<Vector4A16**>(data + off);
    return *reinterpret_cast<es::PointerX86<Vector4A16>*>(data + off);
  }
  uint32 NumDynamicRotations() const { return m(Members::numDynamicRotations) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numDynamicRotations)); }
  Pointer<Vector4A16> StaticRotationsPtr() {
    int16 off = m(Members::staticRotations); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  Vector4A16 *StaticRotations() {
    int16 off = m(Members::staticRotations); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<Vector4A16**>(data + off);
    return *reinterpret_cast<es::PointerX86<Vector4A16>*>(data + off);
  }
  const Vector4A16 *StaticRotations() const {
    int16 off = m(Members::staticRotations); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<Vector4A16**>(data + off);
    return *reinterpret_cast<es::PointerX86<Vector4A16>*>(data + off);
  }
  uint32 NumStaticRotations() const { return m(Members::numStaticRotations) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numStaticRotations)); }
  Pointer<uint16> RotationTypeAndOffsetsPtr() {
    int16 off = m(Members::rotationTypeAndOffsets); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  uint16 *RotationTypeAndOffsets() {
    int16 off = m(Members::rotationTypeAndOffsets); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<uint16**>(data + off);
    return *reinterpret_cast<es::PointerX86<uint16>*>(data + off);
  }
  const uint16 *RotationTypeAndOffsets() const {
    int16 off = m(Members::rotationTypeAndOffsets); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<uint16**>(data + off);
    return *reinterpret_cast<es::PointerX86<uint16>*>(data + off);
  }
  uint32 NumRotationTypeAndOffsets() const { return m(Members::numRotationTypeAndOffsets) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numRotationTypeAndOffsets)); }
  Pointer<float> DynamicScalesPtr() {
    int16 off = m(Members::dynamicScales); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  float *DynamicScales() {
    int16 off = m(Members::dynamicScales); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<float**>(data + off);
    return *reinterpret_cast<es::PointerX86<float>*>(data + off);
  }
  const float *DynamicScales() const {
    int16 off = m(Members::dynamicScales); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<float**>(data + off);
    return *reinterpret_cast<es::PointerX86<float>*>(data + off);
  }
  uint32 NumDynamicScales() const { return m(Members::numDynamicScales) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numDynamicScales)); }
  Pointer<float> StaticScalesPtr() {
    int16 off = m(Members::staticScales); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  float *StaticScales() {
    int16 off = m(Members::staticScales); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<float**>(data + off);
    return *reinterpret_cast<es::PointerX86<float>*>(data + off);
  }
  const float *StaticScales() const {
    int16 off = m(Members::staticScales); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<float**>(data + off);
    return *reinterpret_cast<es::PointerX86<float>*>(data + off);
  }
  uint32 NumStaticScales() const { return m(Members::numStaticScales) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numStaticScales)); }
  Pointer<USVector4> ScaleTypeAndOffsetsPtr() {
    int16 off = m(Members::scaleTypeAndOffsets); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  USVector4 *ScaleTypeAndOffsets() {
    int16 off = m(Members::scaleTypeAndOffsets); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<USVector4**>(data + off);
    return *reinterpret_cast<es::PointerX86<USVector4>*>(data + off);
  }
  const USVector4 *ScaleTypeAndOffsets() const {
    int16 off = m(Members::scaleTypeAndOffsets); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<USVector4**>(data + off);
    return *reinterpret_cast<es::PointerX86<USVector4>*>(data + off);
  }
  uint32 NumScaleTypeAndOffsets() const { return m(Members::numScaleTypeAndOffsets) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numScaleTypeAndOffsets)); }
  Pointer<float> FloatsPtr() {
    int16 off = m(Members::floats); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  float *Floats() {
    int16 off = m(Members::floats); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<float**>(data + off);
    return *reinterpret_cast<es::PointerX86<float>*>(data + off);
  }
  const float *Floats() const {
    int16 off = m(Members::floats); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<float**>(data + off);
    return *reinterpret_cast<es::PointerX86<float>*>(data + off);
  }
  uint32 NumFloats() const { return m(Members::numFloats) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numFloats)); }
  uint32 NumFrames() const { return m(Members::numFrames) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numFrames)); }
  void NumDynamicTranslations(uint32 value) { if (m(Members::numDynamicTranslations) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numDynamicTranslations)) = value; }
  void NumStaticTranslations(uint32 value) { if (m(Members::numStaticTranslations) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numStaticTranslations)) = value; }
  void NumTranslationTypeAndOffsets(uint32 value) { if (m(Members::numTranslationTypeAndOffsets) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numTranslationTypeAndOffsets)) = value; }
  void NumDynamicRotations(uint32 value) { if (m(Members::numDynamicRotations) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numDynamicRotations)) = value; }
  void NumStaticRotations(uint32 value) { if (m(Members::numStaticRotations) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numStaticRotations)) = value; }
  void NumRotationTypeAndOffsets(uint32 value) { if (m(Members::numRotationTypeAndOffsets) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numRotationTypeAndOffsets)) = value; }
  void NumDynamicScales(uint32 value) { if (m(Members::numDynamicScales) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numDynamicScales)) = value; }
  void NumStaticScales(uint32 value) { if (m(Members::numStaticScales) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numStaticScales)) = value; }
  void NumScaleTypeAndOffsets(uint32 value) { if (m(Members::numScaleTypeAndOffsets) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numScaleTypeAndOffsets)) = value; }
  void NumFloats(uint32 value) { if (m(Members::numFloats) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numFloats)) = value; }
  void NumFrames(uint32 value) { if (m(Members::numFrames) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numFrames)) = value; }


  int16 m(uint32 id) const { return layout->vtable[id]; }
  char *data;
  const ClassData<_count_> *layout;
  LayoutLookup lookup;
};
}
