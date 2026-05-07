/*  Havok Format Library
    Copyright(C) 2016-2022 Lukas Cone

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

#include "hka_animated_reference_frame.hpp"
#include "internal/hka_defaultanimrefframe.hpp"
#include <span>

#include "hka_animated_reference_frame_default.inl"

namespace {

struct hkaDefaultAnimatedReferenceFrameSaver {
  const hkaAnimatedReferenceFrameInternalInterface *in;
  const clgen::hkaDefaultAnimatedReferenceFrame::Interface *out;

  void Save(BinWritterRef_e wr, hkFixups &fixups) {
    const size_t sBegin = wr.Tell();
    auto &locals = fixups.locals;
    auto &lay = *out->layout;
    using mm = clgen::hkaDefaultAnimatedReferenceFrame::Members;

    wr.WriteBuffer(out->data, lay.totalSize);

    if (const auto numFrames = in->GetNumFrames(); numFrames) {
      wr.ApplyPadding();
      locals.emplace_back(
          sBegin + out->m(mm::referenceFrameSamples), wr.Tell());

      for (size_t i = 0; i < numFrames; i++) {
        wr.Write(in->GetRefFrame(i));
      }
    }
  }
};

} // namespace

struct hkaDefaultAnimatedReferenceFrameMidInterface
    : hkaAnimatedReferenceFrameMidInterface,
      hkaDefaultAnimatedReferenceFrameInternalInterface {

  clgen::hkaDefaultAnimatedReferenceFrame::Interface interface;
  std::unique_ptr<hkaDefaultAnimatedReferenceFrameSaver> saver;

  hkaDefaultAnimatedReferenceFrameMidInterface(clgen::LayoutLookup rules,
                                               char *data)
      : interface {
    data, rules
  } {
  }

  clgen::hkaAnimatedReferenceFrame::Interface Base() const override {
    return interface.BasehkaAnimatedReferenceFrame();
  }

  void SetDataPointer(void *ptr) override {
    interface.data = static_cast<char *>(ptr);
  }

  const void *GetPointer() const override { return interface.data; }

  void Process() override {
    const size_t numFrames = GetNumFrames();
    frameRate = numFrames > 0 ? (numFrames - 1) / GetDuration() : 30;
  }

  const Vector4A16 GetUp() const override { return interface.Up(); }
  const Vector4A16 GetForward() const override { return interface.Forward(); }
  float GetDuration() const override { return interface.Duration(); }
  size_t GetNumFrames() const override {
    return interface.NumReferenceFrameSamples();
  }

  const Vector4A16 &GetRefFrame(size_t id) const override {
    auto item = interface.ReferenceFrameSamples();
    return item[id];
  }

  void SwapEndian() override {
    clgen::EndianSwap(interface);
    Vector4A16 tmp = GetUp();
    FByteswapper(tmp);
    interface.Up(tmp);
    tmp = GetForward();
    FByteswapper(tmp);
    interface.Forward(tmp);

    if (auto refsData = interface.ReferenceFrameSamples()) {
      for (std::span<Vector4A16> refs(refsData,
                                      interface.NumReferenceFrameSamples());
           auto &i : refs) {
        FByteswapper(i);
      }
    }
  }

  void Reflect(const IhkVirtualClass *other) override {
    auto source = dynamic_cast<const hkaAnimatedReferenceFrameInternalInterface *>(
        other);

    if (!source) {
      throw std::bad_cast{};
    }

    interface.data =
        static_cast<char *>(calloc(1, interface.layout->totalSize));
    saver = std::make_unique<hkaDefaultAnimatedReferenceFrameSaver>();
    saver->in = source;
    saver->out = &interface;

    interface.Up(source->GetUp());
    interface.Forward(source->GetForward());
    interface.Duration(source->GetDuration());
    interface.NumReferenceFrameSamples(
        static_cast<uint32>(source->GetNumFrames()));

    auto base = interface.BasehkaAnimatedReferenceFrame();
    if (base.m(clgen::hkaAnimatedReferenceFrame::Members::frameType) >= 0) {
      base.FrameType(source->GetType());
    }
  }

  void Save(BinWritterRef_e wr, hkFixups &fixups) const override {
    saver->Save(wr, fixups);
  }

  ~hkaDefaultAnimatedReferenceFrameMidInterface() {
    if (saver) {
      free(interface.data);
    }
  }
};

IhkVirtualClass *
hkaDefaultAnimatedReferenceFrameInternalInterface::Create(CRule rule) {
  return new hkaDefaultAnimatedReferenceFrameMidInterface{
      clgen::LayoutLookup{rule.version, rule.x64, rule.reusePadding}, nullptr};
}
