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
#include "hklib/hk_base.hpp"
#include <vector>

struct hkaQuantizedEncoderSettings {
  float translationTolerance = 0.0001f;
  float rotationTolerance = 0.0001f;
  float scaleTolerance = 0.0001f;
  float floatTolerance = 0.0001f;
};

struct hkaQuantizedEncoderInput {
  const hkQTransform *transforms = nullptr;
  const hkQTransform *referencePose = nullptr;
  const float *floats = nullptr;
  const float *referenceFloats = nullptr;
  const uint16 *transformTrackToBoneIndices = nullptr;
  const uint16 *floatTrackToFloatSlotIndices = nullptr;
  uint32 numFrames = 0;
  uint32 numTransformTracks = 0;
  uint32 numFloatTracks = 0;
  uint32 numBones = 0;
  uint32 numFloats = 0;
  float duration = 0.0f;
  hkaQuantizedEncoderSettings settings;
};

bool hkaEncodeQuantizedAnimation(const hkaQuantizedEncoderInput &input,
                                 std::vector<char> &output);
