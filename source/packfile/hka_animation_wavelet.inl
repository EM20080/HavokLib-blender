// This file has been automatically generated. Do not modify.
#include "hka_animation.inl"
namespace clgen::hkaWaveletCompressedAnimation {
enum Members {
  basehkaAnimation,
  blockIndexIdx,
  blockIndexSize,
  blockSize,
  dataBuffer,
  numDataBuffer,
  numDynamicTransformDOFs,
  numStaticTransformDOFs,
  numberOfPoses,
  qFormat,
  quantizedDataIdx,
  quantizedDataSize,
  staticDOFsIdx,
  staticMaskIdx,
  _count_,
};
static const std::set<ClassData<_count_>> LAYOUTS {
  {{{{HK500, HK550, 8, 0}}, 128}, {0, 92, 96, 60, 112, 120, -1, -1, 56, 64, 100, 104, 88, 84}, {0x8a8, 0xaa2}},
  {{{{HK600, HK2010_2, 8, 0}}, 136}, {0, 100, 104, 60, 120, 128, 96, 92, 56, 64, 108, 112, 88, 84}, {0xa8a8, 0xaa2}},
  {{{{HK500, HK510, 4, 0}}, 92}, {0, 68, 72, 36, 84, 88, -1, -1, 32, 40, 76, 80, 64, 60}, {0x8a8, 0xaa2}},
  {{{{HK550, HK550, 4, 0}}, 96}, {0, 72, 76, 40, 88, 92, -1, -1, 36, 44, 80, 84, 68, 64}, {0x8a8, 0xaa2}},
  {{{{HK600, HK660, 4, 0}}, 104}, {0, 80, 84, 40, 96, 100, 76, 72, 36, 44, 88, 92, 68, 64}, {0xa8a8, 0xaa2}},
  {{{{HK700, HK2010_2, 4, 0}}, 112}, {0, 84, 88, 44, 100, 104, 80, 76, 40, 48, 92, 96, 72, 68}, {0xa8a8, 0xaa2}},
  {{{{HK500, HK510, 8, 1}}, 112}, {0, 80, 84, 48, 96, 104, -1, -1, 44, 52, 88, 92, 76, 72}, {0x8a8, 0xaa2}},
  {{{{HK550, HK550, 8, 1}}, 120}, {0, 88, 92, 56, 104, 112, -1, -1, 52, 60, 96, 100, 84, 80}, {0x8a8, 0xaa2}},
  {{{{HK600, HK660, 8, 1}}, 128}, {0, 96, 100, 56, 112, 120, 92, 88, 52, 60, 104, 108, 84, 80}, {0xa8a8, 0xaa2}},
  {{{{HK700, HK2010_2, 8, 1}}, 136}, {0, 100, 104, 60, 120, 128, 96, 92, 56, 64, 108, 112, 88, 84}, {0xa8a8, 0xaa2}},
  {{{{HK500, HK510, 4, 1}}, 92}, {0, 68, 72, 36, 84, 88, -1, -1, 32, 40, 76, 80, 64, 60}, {0x8a8, 0xaa2}},
  {{{{HK550, HK550, 4, 1}}, 96}, {0, 72, 76, 40, 88, 92, -1, -1, 36, 44, 80, 84, 68, 64}, {0x8a8, 0xaa2}},
  {{{{HK600, HK660, 4, 1}}, 104}, {0, 80, 84, 40, 96, 100, 76, 72, 36, 44, 88, 92, 68, 64}, {0xa8a8, 0xaa2}},
  {{{{HK700, HK2010_2, 4, 1}}, 112}, {0, 84, 88, 44, 100, 104, 80, 76, 40, 48, 92, 96, 72, 68}, {0xa8a8, 0xaa2}}
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
  uint32 NumberOfPoses() const { return m(Members::numberOfPoses) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numberOfPoses)); }
  uint32 BlockSize() const { return m(Members::blockSize) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::blockSize)); }
  QuantizationFormat QFormat() const { return m(Members::qFormat) == -1 ? QuantizationFormat{} : *reinterpret_cast<QuantizationFormat*>(data + m(Members::qFormat)); }
  uint32 StaticMaskIdx() const { return m(Members::staticMaskIdx) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::staticMaskIdx)); }
  uint32 StaticDOFsIdx() const { return m(Members::staticDOFsIdx) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::staticDOFsIdx)); }
  uint32 BlockIndexIdx() const { return m(Members::blockIndexIdx) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::blockIndexIdx)); }
  uint32 BlockIndexSize() const { return m(Members::blockIndexSize) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::blockIndexSize)); }
  uint32 QuantizedDataIdx() const { return m(Members::quantizedDataIdx) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::quantizedDataIdx)); }
  uint32 QuantizedDataSize() const { return m(Members::quantizedDataSize) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::quantizedDataSize)); }
  Pointer<char> DataBufferPtr() {
    int16 off = m(Members::dataBuffer); if (off == -1) return {nullptr, lookup};
    return {data + off, lookup};
  }
  char *DataBuffer() {
    int16 off = m(Members::dataBuffer); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<char**>(data + off);
    return *reinterpret_cast<es::PointerX86<char>*>(data + off);
  }
  const char *DataBuffer() const {
    int16 off = m(Members::dataBuffer); if (off == -1) return nullptr;
    if (layout->ptrSize == 8) return *reinterpret_cast<char**>(data + off);
    return *reinterpret_cast<es::PointerX86<char>*>(data + off);
  }
  uint32 NumDataBuffer() const { return m(Members::numDataBuffer) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numDataBuffer)); }
  uint32 NumStaticTransformDOFs() const { return m(Members::numStaticTransformDOFs) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numStaticTransformDOFs)); }
  uint32 NumDynamicTransformDOFs() const { return m(Members::numDynamicTransformDOFs) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::numDynamicTransformDOFs)); }
  void NumberOfPoses(uint32 value) { if (m(Members::numberOfPoses) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numberOfPoses)) = value; }
  void BlockSize(uint32 value) { if (m(Members::blockSize) >= 0) *reinterpret_cast<uint32*>(data + m(Members::blockSize)) = value; }
  void QFormat(QuantizationFormat value) { if (m(Members::qFormat) >= 0) *reinterpret_cast<QuantizationFormat*>(data + m(Members::qFormat)) = value; }
  void StaticMaskIdx(uint32 value) { if (m(Members::staticMaskIdx) >= 0) *reinterpret_cast<uint32*>(data + m(Members::staticMaskIdx)) = value; }
  void StaticDOFsIdx(uint32 value) { if (m(Members::staticDOFsIdx) >= 0) *reinterpret_cast<uint32*>(data + m(Members::staticDOFsIdx)) = value; }
  void BlockIndexIdx(uint32 value) { if (m(Members::blockIndexIdx) >= 0) *reinterpret_cast<uint32*>(data + m(Members::blockIndexIdx)) = value; }
  void BlockIndexSize(uint32 value) { if (m(Members::blockIndexSize) >= 0) *reinterpret_cast<uint32*>(data + m(Members::blockIndexSize)) = value; }
  void QuantizedDataIdx(uint32 value) { if (m(Members::quantizedDataIdx) >= 0) *reinterpret_cast<uint32*>(data + m(Members::quantizedDataIdx)) = value; }
  void QuantizedDataSize(uint32 value) { if (m(Members::quantizedDataSize) >= 0) *reinterpret_cast<uint32*>(data + m(Members::quantizedDataSize)) = value; }
  void NumDataBuffer(uint32 value) { if (m(Members::numDataBuffer) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numDataBuffer)) = value; }
  void NumStaticTransformDOFs(uint32 value) { if (m(Members::numStaticTransformDOFs) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numStaticTransformDOFs)) = value; }
  void NumDynamicTransformDOFs(uint32 value) { if (m(Members::numDynamicTransformDOFs) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numDynamicTransformDOFs)) = value; }


  int16 m(uint32 id) const { return layout->vtable[id]; }
  char *data;
  const ClassData<_count_> *layout;
  LayoutLookup lookup;
};
}
