#pragma once
// The status bar's button letters in other glyphs: the game's own B and A (BG tiles $05 and $08,
// each a letter and the opening bracket) are repainted in the finished picture, never in VRAM, so
// the emulation, its hashes and save states stay the game's. Glyph codes are ui/labels.h's.
#include "core/gb.h"
#include <stdint.h>

#define HUD_GLYPH_H 7
// Seven rows of a 4x7 cell glyph, bit 0x80 >> x for column x (letters in x 1-4, as the bar draws
// them; shapes may use x 0-5). NULL for a code with no glyph.
const uint8_t *hud_glyph(uint8_t code);
// Repaints the game's B and A letters in rgb (FB_W x FB_H RGB24, the sample's own picture) with
// glyphs b and a. A code of 0, or the letter itself, leaves that one alone.
void hud_relabel(const GBSample *s, uint8_t b, uint8_t a, uint8_t *rgb);
