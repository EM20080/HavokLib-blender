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
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct IhkVirtualClass;

struct hkFixup {
  const IhkVirtualClass *destClass{nullptr};
  size_t strOffset{0};
  size_t destination{0};

  hkFixup(size_t offset, size_t dest) : strOffset(offset), destination(dest) {}
  hkFixup(size_t offset, const IhkVirtualClass *dest)
      : destClass(dest), strOffset(offset) {}
  hkFixup(size_t offset) : strOffset(offset) {}
};

struct hkRawLocalFixup {
  std::int32_t pointer{0};
  std::int32_t destination{0};
};

struct hkLegacySceneBlob {
  std::string name;
  std::string className;
  std::string data;
  std::vector<hkRawLocalFixup> localFixups;
  size_t variantPtrOff{static_cast<size_t>(-1)};
};

struct hkFixups {
  std::vector<hkFixup> locals;
  std::vector<hkFixup> finals;
  std::vector<hkFixup> globals;
  std::optional<hkLegacySceneBlob> legacyScene;
};
