// This file has been automatically generated. Do not modify.
#include "hka_animation.inl"
namespace clgen::hkaQuantizedAnimation {
enum Members {
  basehkaAnimation,
  data,
  endian,
  numData,
  skeleton,
  _count_,
};
static const std::set<ClassData<_count_>> LAYOUTS {
  {{{{HK500, HK2015, 8, 0}}, 88}, {0, 56, 72, 64, 80}, {0xa0}},
  {{{{HK2016, HK2019, 8, 0}}, 96}, {0, 64, 80, 72, 88}, {0xa0}},
  {{{{HK500, HK510, 4, 0}}, 52}, {0, 32, 44, 36, 48}, {0xa0}},
  {{{{HK550, HK660, 4, 0}}, 56}, {0, 36, 48, 40, 52}, {0xa0}},
  {{{{HK700, HK2015, 4, 0}}, 60}, {0, 40, 52, 44, 56}, {0xa0}},
  {{{{HK2016, HK2019, 4, 0}}, 64}, {0, 44, 56, 48, 60}, {0xa0}},
  {{{{HK500, HK510, 8, 1}}, 80}, {0, 48, 64, 56, 72}, {0xa0}},
  {{{{HK550, HK2015, 8, 1}}, 88}, {0, 56, 72, 64, 80}, {0xa0}},
  {{{{HK2016, HK2019, 8, 1}}, 96}, {0, 64, 80, 72, 88}, {0xa0}},
  {{{{HK500, HK510, 4, 1}}, 52}, {0, 32, 44, 36, 48}, {0xa0}},
  {{{{HK550, HK660, 4, 1}}, 56}, {0, 36, 48, 40, 52}, {0xa0}},
  {{{{HK700, HK2015, 4, 1}}, 60}, {0, 40, 52, 44, 56}, {0xa0}},
  {{{{HK2016, HK2019, 4, 1}}, 64}, {0, 44, 56, 48, 60}, {0xa0}}
};
struct Interface {
  Interface(char *data_, LayoutLookup layout_): data{data_}, layout{GetLayout(LAYOUTS, {layout_, {LookupFlag::Ptr ,LookupFlag::Padding}})}, lookup{layout_} {}
  Interface(const Interface&) = default;
  Interface(Interface&&) = default;
  Interface &operator=(const Interface&) = default;
  Interface &operator=(Interface&&) = default;
  uint16 LayoutVersion() const { return lookup.version; }
  hkaAnimation::Interface BasehkaAnimation() const {
    int16 off = m(basehkaAnimation); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  Pointer<char> DataPtr() {
    int16 off = m(data); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  char *Data() {
    int16 off = m(data); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<char**>(data + off);
    return *reinterpret_cast<es::PointerX86<char>*>(data + off);
  }
  const char *Data() const {
    int16 off = m(data); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<char**>(data + off);
    return *reinterpret_cast<es::PointerX86<char>*>(data + off);
  }
  uint32 NumData() const { return m(numData) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(numData)); }
  uint32 Endian() const { return m(endian) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(endian)); }
  Pointer<char> SkeletonPtr() {
    int16 off = m(skeleton); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  char *Skeleton() {
    int16 off = m(skeleton); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<char**>(data + off);
    return *reinterpret_cast<es::PointerX86<char>*>(data + off);
  }
  const char *Skeleton() const {
    int16 off = m(skeleton); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<char**>(data + off);
    return *reinterpret_cast<es::PointerX86<char>*>(data + off);
  }
  void NumData(uint32 value) { if (m(numData) >= 0) *reinterpret_cast<uint32*>(data + m(numData)) = value; }
  void Endian(uint32 value) { if (m(endian) >= 0) *reinterpret_cast<uint32*>(data + m(endian)) = value; }


  int16 m(uint32 id) const { return layout->vtable[id]; }
  char *data;
  const ClassData<_count_> *layout;
  LayoutLookup lookup;
};
}
