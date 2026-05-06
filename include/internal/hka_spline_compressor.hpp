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
#include "hklib/hk_base.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct hkaSplineCompressedData {
  uint32 numFrames = 0;
  uint32 numBlocks = 0;
  uint32 maxFramesPerBlock = 0;
  uint32 maskAndQuantizationSize = 0;
  float blockDuration = 0.0f;
  float blockInverseDuration = 0.0f;
  float frameDuration = 0.0f;
  std::vector<uint32> blockOffsets;
  std::vector<uint32> floatBlockOffsets;
  std::vector<uint32> transformOffsets;
  std::vector<uint32> floatOffsets;
  std::vector<char> dataBuffer;
};

struct hkaSplineCompressionSettings {
  uint32 maxFramesPerBlock = 255;
  uint32 keyStep = 1;
  bool use16BitFloats = true;
  bool forceIdentityScale = false;
};

struct hkaSplineCompressionInput {
  const hkQTransform *transforms = nullptr;
  const float *floats = nullptr;
  uint32 numFrames = 0;
  uint32 numTransformTracks = 0;
  uint32 numFloatTracks = 0;
  float duration = 0.0f;
  hkaSplineCompressionSettings settings;
};

bool hkaCompressSplineAnimation(const hkaSplineCompressionInput &input,
                                hkaSplineCompressedData &output,
                                std::string *error = nullptr);
