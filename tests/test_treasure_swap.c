#include "unit.h"
#include "core/gb.h"
#include "platform/tas.h"
#include "platform/setup.h"
#include "game/game.h"
#include "game/features.h"
#include "assets/assets.h"
#include "mods/treasures.h"
#include <stdlib.h>

#define AGES GAME_ROM_DIR "/Legend of Zelda, The - Oracle of Ages (USA, Australia).gbc"
#define SEASONS GAME_ROM_DIR "/Legend of Zelda, The - Oracle of Seasons (USA, Australia).gbc"

static int skipped;
#define SKIP_TEST(msg) do { printf("skip: %s\n", msg); return false; } while (0)
#define RUN_SKIPPABLE(fn) do { if (fn()) printf("ok %s\n", #fn); else skipped = 1; } while (0)

static uint8_t tas_cb(void *ctx, uint64_t frame) { return tas_input_at((const Tas *)ctx, frame); }

typedef struct { GB *gb; uint8_t *rom; Tas tas; } Movie;

static bool movie_start(Movie *m) {
  size_t n;
  m->rom = oracles_read_file(AGES, &n);
  if (!m->rom || !tas_load(&m->tas, TAS_DIR "/ages-consoleverified.inputs")) return false;
  m->gb = calloc(1, sizeof *m->gb);
  gb_init(m->gb);
  ASSERT(gb_load_rom(m->gb, m->rom, n));
  m->gb->cyctab = cyctab_alloc(m->rom, n);
  assets_zero_code(m->rom, n);
  gb_reset(m->gb);
  if (!oracles_load_boot_state(m->gb, TAS_DIR "/ages-boot.state")) return false;
  m->gb->input_at = tas_cb;
  m->gb->input_ctx = &m->tas;
  return true;
}

static void movie_end(Movie *m) { free(m->gb); free(m->rom); tas_free(&m->tas); }

static bool treasure_obtained(GB *gb, int id) {
  return gb->wram[wObtainedTreasureFlags_BANK][(wObtainedTreasureFlags + id / 8) & 0xfff] >> (id % 8) & 1;
}

static uint8_t obj_byte(GB *gb, uint16_t obj, int field) { return gb->wram[1][(obj + field) & 0xfff]; }

static int calls;
static bool never(int group, int room, uint8_t id, uint8_t subid, uint8_t *new_id, uint8_t *new_subid) {
  (void)group; (void)room; (void)id; (void)subid; (void)new_id; (void)new_subid;
  calls++;
  return false;
}

typedef struct { FILE *ref; unsigned long long f, want; bool have; uint64_t mismatch; } RefCheck;
static void ref_cb(GB *gb, const GBSample *sm, void *ctx) {
  RefCheck *r = ctx;
  if (r->mismatch) return;
  while (r->have && r->f < sm->frame) r->have = fscanf(r->ref, "%llu %llx", &r->f, &r->want) == 2;
  if (r->have && r->f == sm->frame) {
    if (r->want != gb_state_hash(gb)) r->mismatch = sm->frame;
    r->have = fscanf(r->ref, "%llu %llx", &r->f, &r->want) == 2;
  }
}

// A callback that matches nothing leaves the game exactly as the ROM runs it.
static bool a_swap_that_matches_nothing_changes_nothing(void) {
  Movie m;
  if (!movie_start(&m)) SKIP_TEST("Ages ROM, movie or boot state missing");
  FILE *ref = fopen(TAS_DIR "/ages.ref", "r");
  if (!ref) SKIP_TEST("ages.ref missing");
  RefCheck rc = {ref, 0, 0, false, 0};
  rc.have = fscanf(ref, "%llu %llx", &rc.f, &rc.want) == 2;
  m.gb->frame_cb = ref_cb;
  m.gb->frame_ctx = &rc;
  calls = 0;
  features.treasure_swap = never;
  for (int i = 0; i < 14500 && !rc.mismatch && !m.gb->hung; i++) gb_run_frame(m.gb);
  features.treasure_swap = NULL;
  ASSERT(!m.gb->hung);
  ASSERT_EQ(rc.mismatch, 0);
  ASSERT(calls >= 1);
  fclose(ref);
  movie_end(&m);
  return true;
}

static int seen_group = -1, seen_room = -1, seen_id = -1, seen_subid = -1;
static bool sword_to_bombchus(int group, int room, uint8_t id, uint8_t subid, uint8_t *new_id, uint8_t *new_subid) {
  if (seen_id < 0) { seen_group = group; seen_room = room; seen_id = id; seen_subid = subid; }
  if (group != 0 || room != 0x39 || id != 0x05 || subid != 0) return false;
  *new_id = 0x0d;
  *new_subid = 0;
  return true;
}

// The movie's first treasure is the sword in room $39. Swapped, the object carries the bombchus'
// grab mode, parameter, text and graphics but keeps the sword's spawn mode and room-flag bit, and
// Link ends up with bombchus instead of the sword.
static bool the_first_treasure_of_the_ages_movie_can_be_swapped(void) {
  Movie m;
  if (!movie_start(&m)) SKIP_TEST("Ages ROM, movie or boot state missing");
  features.treasure_swap = sword_to_bombchus;
  bool checked = false;
  for (int i = 0; i < 15000 && !m.gb->hung; i++) {
    gb_run_frame(m.gb);
    if (seen_id >= 0 && !checked) {
      checked = true;
      uint16_t obj = 0xd240;
      ASSERT_EQ(obj_byte(m.gb, obj, OBJ_ID), 0x60);
      ASSERT_EQ(obj_byte(m.gb, obj, OBJ_VAR30), 0x0d);
      ASSERT_EQ(obj_byte(m.gb, obj, OBJ_VAR31), 0x00);
      ASSERT_EQ(obj_byte(m.gb, obj, OBJ_VAR32), 0x02);
      ASSERT_EQ(obj_byte(m.gb, obj, OBJ_VAR33), 0x08);
      ASSERT_EQ(obj_byte(m.gb, obj, OBJ_VAR34), 0x10);
      ASSERT_EQ(obj_byte(m.gb, obj, OBJ_VAR35), 0x32);
      ASSERT_EQ(obj_byte(m.gb, obj, OBJ_SUBID), 0x24);
    }
  }
  features.treasure_swap = NULL;
  ASSERT(!m.gb->hung);
  ASSERT_EQ(seen_group, 0);
  ASSERT_EQ(seen_room, 0x39);
  ASSERT_EQ(seen_id, 0x05);
  ASSERT_EQ(seen_subid, 0);
  ASSERT(checked);
  ASSERT(treasure_obtained(m.gb, 0x0d));
  ASSERT(!treasure_obtained(m.gb, 0x05));
  movie_end(&m);
  return true;
}

// Every pair the mod host accepts decodes to a leaf entry of the ROM's own table.
static bool the_treasure_list_matches_both_roms(void) {
  const char *paths[2] = {AGES, SEASONS};
  for (int g = 0; g < 2; g++) {
    size_t n;
    uint8_t *rom = oracles_read_file(paths[g], &n);
    if (!rom) SKIP_TEST("ROM missing");
    GB *gb = calloc(1, sizeof *gb);
    gb_init(gb);
    ASSERT(gb_load_rom(gb, rom, n));
    syms_select(g, 0);
    uint8_t bank = SYMBANK(treasureObjectData);
    for (int id = 0; id < 0x100; id++) {
      int count = mods_treasure_subids(g, id);
      if (!count) continue;
      uint8_t top = rom[(size_t)bank * 0x4000 + ((SYM(treasureObjectData) + id * 4) & 0x3fff)];
      if (!(top & 0x80)) ASSERT_EQ(count, 1);
      for (int s = 0; s < count; s++) {
        uint8_t t[4];
        features_treasure_object(gb, (uint8_t)id, (uint8_t)s, t);
        ASSERT(!(t[0] & 0x80));
      }
    }
    uint8_t t[4];
    features_treasure_object(gb, g ? 0x28 : 0x05, 0, t);
    if (g) { ASSERT_EQ(t[0], 0x38); ASSERT_EQ(t[1], 0x01); ASSERT_EQ(t[2], 0x01); ASSERT_EQ(t[3], 0x28); }
    else { ASSERT_EQ(t[0], 0x09); ASSERT_EQ(t[1], 0x01); ASSERT_EQ(t[2], 0x1c); ASSERT_EQ(t[3], 0x10); }
    free(gb);
    free(rom);
  }
  syms_select(0, 0);
  return true;
}

int main(void) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  RUN_SKIPPABLE(the_treasure_list_matches_both_roms);
  RUN_SKIPPABLE(a_swap_that_matches_nothing_changes_nothing);
  RUN_SKIPPABLE(the_first_treasure_of_the_ages_movie_can_be_swapped);
  return skipped ? 77 : 0;
}
