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
#include "hka_animation.hpp"

struct hkaQuantizedAnimationInternalInterface : virtual hkaAnimationLerpSampler {
  virtual const char *GetData() const = 0;
  virtual size_t GetDataSize() const = 0;

  operator hkaQuantizedAnimationInternalInterface const *() const override {
    return this;
  }
  operator hkVirtualClass const *() const override { return this; }
  static IhkVirtualClass *Create(CRule rule);
};
