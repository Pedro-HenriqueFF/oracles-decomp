#pragma once
// Internal to src/wide: framebuffer_to_rgb's RGB555 to RGB24 conversion, so a pixel drawn here is
// the game's own of that colour (dim: three quarters as bright).
#include <stdbool.h>
#include <stdint.h>

static inline void put555(uint8_t *out, int c, bool dim) {
  int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
  uint8_t rgb[3] = {(uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 3) | (g >> 2)), (uint8_t)((b << 3) | (b >> 2))};
  for (int k = 0; k < 3; k++) out[k] = dim ? (uint8_t)(rgb[k] * 3 / 4) : rgb[k];
}
