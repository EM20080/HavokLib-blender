#pragma once
#include <cstddef>
#include <cstdint>
#include "hklib/hk_base.hpp"

struct EmbeddedTypes {
  const uint8_t *data;
  size_t size;
  bool isBigEndian = false;
};

EmbeddedTypes GetEmbeddedTypes(hkToolset toolset, bool bigEndian = false);
