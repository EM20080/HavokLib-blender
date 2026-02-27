// This file has been automatically generated. Do not modify.
#include "hka_animation.inl"
namespace clgen::hkaDeltaCompressedAnimation {
enum Members {
  basehkaAnimation,
  blockSize,
  dataBuffer,
  lastBlockSize,
  maskDOFsIdx,
  maskDOFsSize,
  numDataBuffer,
  numDynamicTransformDOFs,
  numStaticTransformDOFs,
  numberOfPoses,
  qFormat,
  quantizedDataIdx,
  quantizedDataSize,
  staticMaskIdx,
  staticMaskSize,
  totalBlockSize,
  _count_,
};
static const std::set<ClassData<_count_>> LAYOUTS {
  {{{{HK500, HK550, 8, 0}}, 136}, {0, 60, 120, 112, 100, 104, 128, -1, -1, 56, 64, 84, 88, 92, 96, 108}, {0x2a88, 0xaa88}},
  {{{{HK600, HK2010_2, 8, 0}}, 144}, {0, 60, 128, 120, 100, 104, 136, 112, 108, 56, 64, 84, 88, 92, 96, 116}, {0xaa88, 0xaa8a}},
  {{{{HK500, HK510, 4, 0}}, 100}, {0, 36, 92, 88, 76, 80, 96, -1, -1, 32, 40, 60, 64, 68, 72, 84}, {0x2a88, 0xaa88}},
  {{{{HK550, HK550, 4, 0}}, 104}, {0, 40, 96, 92, 80, 84, 100, -1, -1, 36, 44, 64, 68, 72, 76, 88}, {0x2a88, 0xaa88}},
  {{{{HK600, HK660, 4, 0}}, 112}, {0, 40, 104, 100, 80, 84, 108, 92, 88, 36, 44, 64, 68, 72, 76, 96}, {0xaa88, 0xaa8a}},
  {{{{HK700, HK2010_2, 4, 0}}, 120}, {0, 44, 108, 104, 84, 88, 112, 96, 92, 40, 48, 68, 72, 76, 80, 100}, {0xaa88, 0xaa8a}},
  {{{{HK500, HK510, 8, 1}}, 120}, {0, 48, 104, 100, 88, 92, 112, -1, -1, 44, 52, 72, 76, 80, 84, 96}, {0x2a88, 0xaa88}},
  {{{{HK550, HK550, 8, 1}}, 128}, {0, 56, 112, 108, 96, 100, 120, -1, -1, 52, 60, 80, 84, 88, 92, 104}, {0x2a88, 0xaa88}},
  {{{{HK600, HK660, 8, 1}}, 136}, {0, 56, 120, 116, 96, 100, 128, 108, 104, 52, 60, 80, 84, 88, 92, 112}, {0xaa88, 0xaa8a}},
  {{{{HK700, HK2010_2, 8, 1}}, 144}, {0, 60, 128, 120, 100, 104, 136, 112, 108, 56, 64, 84, 88, 92, 96, 116}, {0xaa88, 0xaa8a}},
  {{{{HK500, HK510, 4, 1}}, 100}, {0, 36, 92, 88, 76, 80, 96, -1, -1, 32, 40, 60, 64, 68, 72, 84}, {0x2a88, 0xaa88}},
  {{{{HK550, HK550, 4, 1}}, 104}, {0, 40, 96, 92, 80, 84, 100, -1, -1, 36, 44, 64, 68, 72, 76, 88}, {0x2a88, 0xaa88}},
  {{{{HK600, HK660, 4, 1}}, 112}, {0, 40, 104, 100, 80, 84, 108, 92, 88, 36, 44, 64, 68, 72, 76, 96}, {0xaa88, 0xaa8a}},
  {{{{HK700, HK2010_2, 4, 1}}, 120}, {0, 44, 108, 104, 84, 88, 112, 96, 92, 40, 48, 68, 72, 76, 80, 100}, {0xaa88, 0xaa8a}}
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
  uint32 QuantizedDataIdx() const { return m(Members::quantizedDataIdx) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::quantizedDataIdx)); }
  uint32 QuantizedDataSize() const { return m(Members::quantizedDataSize) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::quantizedDataSize)); }
  uint32 StaticMaskIdx() const { return m(Members::staticMaskIdx) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::staticMaskIdx)); }
  uint32 StaticMaskSize() const { return m(Members::staticMaskSize) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::staticMaskSize)); }
  uint32 MaskDOFsIdx() const { return m(Members::maskDOFsIdx) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::maskDOFsIdx)); }
  uint32 MaskDOFsSize() const { return m(Members::maskDOFsSize) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::maskDOFsSize)); }
  uint32 TotalBlockSize() const { return m(Members::totalBlockSize) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::totalBlockSize)); }
  uint32 LastBlockSize() const { return m(Members::lastBlockSize) == -1 ? uint32{} : *reinterpret_cast<uint32*>(data + m(Members::lastBlockSize)); }
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
  void QuantizedDataIdx(uint32 value) { if (m(Members::quantizedDataIdx) >= 0) *reinterpret_cast<uint32*>(data + m(Members::quantizedDataIdx)) = value; }
  void QuantizedDataSize(uint32 value) { if (m(Members::quantizedDataSize) >= 0) *reinterpret_cast<uint32*>(data + m(Members::quantizedDataSize)) = value; }
  void StaticMaskIdx(uint32 value) { if (m(Members::staticMaskIdx) >= 0) *reinterpret_cast<uint32*>(data + m(Members::staticMaskIdx)) = value; }
  void StaticMaskSize(uint32 value) { if (m(Members::staticMaskSize) >= 0) *reinterpret_cast<uint32*>(data + m(Members::staticMaskSize)) = value; }
  void MaskDOFsIdx(uint32 value) { if (m(Members::maskDOFsIdx) >= 0) *reinterpret_cast<uint32*>(data + m(Members::maskDOFsIdx)) = value; }
  void MaskDOFsSize(uint32 value) { if (m(Members::maskDOFsSize) >= 0) *reinterpret_cast<uint32*>(data + m(Members::maskDOFsSize)) = value; }
  void TotalBlockSize(uint32 value) { if (m(Members::totalBlockSize) >= 0) *reinterpret_cast<uint32*>(data + m(Members::totalBlockSize)) = value; }
  void LastBlockSize(uint32 value) { if (m(Members::lastBlockSize) >= 0) *reinterpret_cast<uint32*>(data + m(Members::lastBlockSize)) = value; }
  void NumDataBuffer(uint32 value) { if (m(Members::numDataBuffer) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numDataBuffer)) = value; }
  void NumStaticTransformDOFs(uint32 value) { if (m(Members::numStaticTransformDOFs) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numStaticTransformDOFs)) = value; }
  void NumDynamicTransformDOFs(uint32 value) { if (m(Members::numDynamicTransformDOFs) >= 0) *reinterpret_cast<uint32*>(data + m(Members::numDynamicTransformDOFs)) = value; }


  int16 m(uint32 id) const { return layout->vtable[id]; }
  char *data;
  const ClassData<_count_> *layout;
  LayoutLookup lookup;
};
}
