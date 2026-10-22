#pragma once
#include "core/gb.h"

// The BG palettes and OAM the sampled picture was drawn from, for ModsFrame. Not the live copies:
// the sample is taken mid-LCD-frame and the game rewrites both in vblank, so the live ones run a
// frame ahead of what is on screen during fades and movement. Line 143 is always from the last
// LCD frame the picture finished.
static inline const uint8_t *mods_picture_bg_pal(const GBSample *s) {
  return s->drawn_bg_pal[s->line_drawn[FB_H - 1]];
}

static inline const uint8_t *mods_picture_oam(const GBSample *s) {
  return s->drawn_oam[s->line_drawn[FB_H - 1]];
}
