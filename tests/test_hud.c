// The status bar's B and A repainted in other glyphs: on a made-up sample with the game's letter
// tiles where the bar puts them, plain and mirrored, and with other art in those tiles.
#include "unit.h"
#include "core/gb.h"
#include "hw/render.h"
#include "wide/hud.h"
#include "ui/labels.h"

static const uint8_t tile_b[16] = {0x00, 0xff, 0x73, 0xff, 0x4a, 0xff, 0x4a, 0xff, 0x72, 0xff, 0x4a, 0xff, 0x4a, 0xff, 0x72, 0xff};
static const uint8_t tile_a[16] = {0x00, 0xff, 0x33, 0xff, 0x4a, 0xff, 0x4a, 0xff, 0x7a, 0xff, 0x4a, 0xff, 0x4a, 0xff, 0x4a, 0xff};
static GBSample s;
static uint8_t rgb[FB_W * FB_H * 3], before[FB_W * FB_H * 3];
// palette 0 colour 2 (paper) is 0x4210, colour 3 (ink) black; the picture itself is white, so a
// pixel repainted reads as one or the other and one left alone stays white
enum { PAPER = (16 << 3) | (16 >> 2), INK = 0, WHITE = 0xff };

// The bar as the game draws it: map $9C00 rows 30-31, SCY 240, B at column 0, A at column col_a
static void make_bar(int col_a, uint8_t attr_a) {
  memset(&s, 0, sizeof s);
  for (int y = 0; y < 16; y++) { s.line_lcdc[y] = 0xef; s.line_scy[y] = 240; s.line_wy[y] = 144; s.line_wx[y] = 7; }
  uint8_t *v0 = s.drawn_vram[0][0], *v1 = s.drawn_vram[0][1];
  memcpy(v0 + 0x1050, tile_b, 16);
  memcpy(v0 + 0x1080, tile_a, 16);
  v0[0x1c00 + 30 * 32 + 0] = 0x05; v1[0x1c00 + 30 * 32 + 0] = 0x80;
  v0[0x1c00 + 30 * 32 + col_a] = 0x08; v1[0x1c00 + 30 * 32 + col_a] = attr_a;
  s.drawn_bg_pal[0][4] = 0x10; s.drawn_bg_pal[0][5] = 0x42;
  for (int i = 0; i < FB_W * FB_H; i++) s.framebuffer[i] = 0x7fff;
  framebuffer_to_rgb(s.framebuffer, rgb);
}

static int px(int x, int y) { return rgb[((size_t)y * FB_W + x) * 3]; }
static bool same(int x, int y) { size_t i = ((size_t)y * FB_W + x) * 3; return !memcmp(rgb + i, before + i, 3); }

// cell x0..x0+5 on lines 0-7 holds glyph g: row 0 paper, rows 1-7 the glyph
static void assert_glyph(int x0, const uint8_t *g) {
  for (int y = 0; y < 8; y++)
    for (int c = 0; c < 6; c++) ASSERT_EQ(px(x0 + c, y), y && (g[y - 1] >> (7 - c)) & 1 ? INK : PAPER);
}

static void glyphs_exist_for_every_code(void) {
  for (int c = 'A'; c <= 'Z'; c++) ASSERT(hud_glyph((uint8_t)c));
  for (int c = '0'; c <= '9'; c++) ASSERT(hud_glyph((uint8_t)c));
  for (int c = HUD_GLYPH_CROSS; c <= HUD_GLYPH_TRIANGLE; c++) ASSERT(hud_glyph((uint8_t)c));
  ASSERT(!hud_glyph(0));
  ASSERT(!hud_glyph('?'));
}

// Every glyph keeps out of x 6-7, where the bracket shares the cell
static void glyphs_leave_the_bracket_alone(void) {
  for (int c = 1; c < 128; c++) {
    const uint8_t *g = hud_glyph((uint8_t)c);
    for (int r = 0; g && r < HUD_GLYPH_H; r++) ASSERT_EQ(g[r] & 0x03, 0);
  }
}

// The game's own B and A are its tile rows 1-7 without the bracket
static void own_letters_match_the_tiles(void) {
  for (int r = 0; r < HUD_GLYPH_H; r++) {
    ASSERT_EQ(hud_glyph('B')[r], tile_b[2 + r * 2] & 0xf8);
    ASSERT_EQ(hud_glyph('A')[r], tile_a[2 + r * 2] & 0xf8);
  }
}

static void letters_repainted_in_place(void) {
  make_bar(5, 0x80);
  memcpy(before, rgb, sizeof rgb);
  hud_relabel(&s, 'B', 'A', rgb);                      // the game's own: nothing to do
  hud_relabel(&s, 0, 0, rgb);
  ASSERT(!memcmp(before, rgb, sizeof rgb));
  hud_relabel(&s, 'X', HUD_GLYPH_CROSS, rgb);
  assert_glyph(0, hud_glyph('X'));
  assert_glyph(40, hud_glyph(HUD_GLYPH_CROSS));
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < FB_W; x++)
      if (y >= 8 || (x >= 6 && x < 40) || x >= 46) ASSERT(same(x, y));  // brackets and the rest kept
}

static void one_letter_alone(void) {
  make_bar(5, 0x80);
  memcpy(before, rgb, sizeof rgb);
  hud_relabel(&s, 'Y', 'A', rgb);
  assert_glyph(0, hud_glyph('Y'));
  for (int y = 0; y < 8; y++) ASSERT(same(41, y));
}

static void mirrored_a_keeps_its_glyph_the_right_way_round(void) {
  make_bar(8, 0xa0);                                   // > 13 hearts: A at column 8, flipped
  memcpy(before, rgb, sizeof rgb);
  hud_relabel(&s, 0, 'Y', rgb);
  assert_glyph(64 + 2, hud_glyph('Y'));
  for (int y = 0; y < 8; y++) ASSERT(same(64, y) && same(65, y));  // the bracket, at x 0-1 now
  for (int y = 0; y < 8; y++) ASSERT(same(1, y));                  // B left as it was
}

static void other_art_in_the_tiles_is_left_alone(void) {
  make_bar(5, 0x80);
  s.drawn_vram[0][0][0x1052] ^= 0xff;                  // a title screen's art, not the letter
  memcpy(before, rgb, sizeof rgb);
  hud_relabel(&s, 'X', 'Y', rgb);
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) ASSERT(same(x, y));    // B cell unchanged
  assert_glyph(40, hud_glyph('Y'));                    // A's tile is still the letter
}

static void off_the_bar_is_left_alone(void) {
  make_bar(5, 0x80);
  for (int y = 0; y < 16; y++) s.line_lcdc[y] = 0xff;  // unsigned tile data: not the bar
  memcpy(before, rgb, sizeof rgb);
  hud_relabel(&s, 'X', 'Y', rgb);
  ASSERT(!memcmp(before, rgb, sizeof rgb));
  make_bar(5, 0x80);
  for (int y = 0; y < 16; y++) { s.line_wy[y] = 0; s.line_wx[y] = 7; }  // the window over it
  memcpy(before, rgb, sizeof rgb);
  hud_relabel(&s, 'X', 'Y', rgb);
  ASSERT(!memcmp(before, rgb, sizeof rgb));
}

int main(void) {
  RUN(glyphs_exist_for_every_code);
  RUN(glyphs_leave_the_bracket_alone);
  RUN(own_letters_match_the_tiles);
  RUN(letters_repainted_in_place);
  RUN(one_letter_alone);
  RUN(mirrored_a_keeps_its_glyph_the_right_way_round);
  RUN(other_art_in_the_tiles_is_left_alone);
  RUN(off_the_bar_is_left_alone);
  return 0;
}
