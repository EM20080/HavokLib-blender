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

#pragma once
#include "hk_internal_api.hpp"
#include "hklib/hk_rootlevelcontainer.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct hkPreservedLocalFixup {
  std::int32_t pointer{0};
  std::int32_t destination{0};
};

struct hkPreservedSceneBlob {
  std::string name;
  std::string className;
  std::string data;
  std::vector<hkPreservedLocalFixup> localFixups;
  std::uint8_t sourceLittleEndian{1};
};

struct hkRootLevelContainerInternalInterface : hkRootLevelContainer,
                                               hkVirtualClass {
  operator hkRootLevelContainer const *() const override { return this; }
  operator hkVirtualClass const *() const override { return this; }
  void ToXML(XMLHandle hdl) const override;
  virtual const hkPreservedSceneBlob *
  GetPreservedSceneBlob(std::string_view) const {
    return nullptr;
  }
  static IhkVirtualClass *Create(CRule rule);
};
