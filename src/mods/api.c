#include "mods/api.h"
#include "mods/mods.h"
#include "mods/overlay.h"
#include "lua.h"
#include "lauxlib.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

// Large enough for any ordinary log line (and mods_escape_into's worst case, where a byte of
// input can expand to a 4-byte \xHH escape) without making the buffer itself a place a mod can
// grow memory usage unboundedly -- it can't, since mods_escape_into is already bounded by
// dst_size and simply truncates past it. A log line this long is already not something a status
// line could usefully show in full.
static char last_log[512];

const char *mods_last_log(void) { return last_log[0] ? last_log : NULL; }
void mods_api_clear_log(void) { last_log[0] = 0; }

static int l_log(lua_State *L) {
  // luaL_checklstring, not luaL_checkstring: a Lua string can contain an embedded NUL, and
  // checkstring's bare `const char *` would silently truncate at it with nobody having decided
  // that was fine. The decision made here is not to truncate at all: mods_escape_into renders
  // every byte outside printable ASCII -- NUL included -- as a literal \xHH escape, so the whole
  // message survives (readably) in last_log and in the stderr line below. That's the same
  // escaper mods.c already uses for mod.ini values, reused here rather than duplicated, and it
  // closes off the same terminal-injection hole an unescaped CR/ANSI-escape log string would
  // otherwise open (a mod could otherwise print "\r\x1b[2K" followed by a forged status line).
  size_t len;
  const char *s = luaL_checklstring(L, 1, &len);
  mods_escape_into(last_log, sizeof last_log, s, len);
  fprintf(stderr, "mod: %s\n", last_log);
  return 0;
}

// lua_Integer is 64-bit; a bare `(int)luaL_checkinteger(...)` therefore truncates rather than
// clamps -- draw_rect(0, 0, 4294967296 + 5, 1, c) would arrive at overlay_rect as w == 5, not an
// error and not INT_MAX, simply because 4294967301 mod 2^32 happens to be 5. The overlay's own
// guards (overlay_rect/overlay_text/overlay_icon) already cope with any int, so this is never a
// crash either way -- but silently wrapping is still baffling behaviour no mod author watching
// their rectangle draw five pixels wide instead of refusing outright could ever predict from
// their own source. Every binding below that takes a coordinate or size from Lua goes through
// this instead of a bare cast, so an out-of-int-range argument saturates to INT_MIN/INT_MAX,
// which is at least a value a mod author could reason about (and, for an absurd width like this,
// indistinguishable in effect from "the whole overlay", which overlay_rect already clips to).
static int clamp_int(lua_Integer v) {
  if (v < INT_MIN) return INT_MIN;
  if (v > INT_MAX) return INT_MAX;
  return (int)v;
}

static int check_int(lua_State *L, int arg) { return clamp_int(luaL_checkinteger(L, arg)); }

static int l_on_frame(lua_State *L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  // luaL_ref pops the function off L's stack and stores it in the registry (shared across every
  // thread of this mod's Lua state, not just L), returning an int handle -- mods_set_on_frame_ref
  // is what actually remembers it, against whichever mod is currently running this call (mods.c's
  // active_mod), not a Lua global: see mods_set_on_frame_ref's own comment in mods.h for why a
  // global would let a mod bypass this binding entirely.
  int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  mods_set_on_frame_ref(L, ref);
  return 0;
}

static int l_on_room_change(lua_State *L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  mods_set_on_room_change_ref(L, ref);
  return 0;
}

static int l_game(lua_State *L) {
  lua_pushstring(L, mods_current_frame()->seasons ? "seasons" : "ages");
  return 1;
}

static int l_screen(lua_State *L) {
  lua_pushinteger(L, mods_current_frame()->width);
  return 1;
}

// $c000..$cfff and $ff80..$fffe, plus $d000..$dfff as WRAM bank 1 for an api=2 mod (so it can
// read what oracles.wr writes there; api=1 promised zeroes in that range). Anything else reads
// back 0 rather than erroring, as MODDING.md says under oracles.rd.
static int l_rd(lua_State *L) {
  int addr = check_int(L, 1);
  const ModsFrame *f = mods_current_frame();
  int v = 0;
  if (addr >= 0xc000 && addr <= 0xcfff && f->wram) v = f->wram[addr - 0xc000];
  else if (addr >= 0xff80 && addr <= 0xfffe && f->hram) v = f->hram[addr - 0xff80];
  else if (addr >= 0xd000 && addr <= 0xdfff && f->wram1 && mods_active_api() >= 2) v = f->wram1[addr - 0xd000];
  lua_pushinteger(L, v);
  return 1;
}

// Stage 3 Task 2: the current room, as a table { group, room, width, height }.
//
// width/height are in **metatiles**, not the 8x8 tiles room_w/room_h hold -- a metatile (one
// bush, one wall, one stair) is the game's own unit of meaning and is what Task 3's
// id-to-metatile mapping table will be keyed on, so this converts here rather than making every
// caller divide by two itself and risk forgetting. The conversion is an exact halving
// (room_w/room_h are always even -- the plan's verified-facts table observed 20x16 and 30x22,
// both even, and wRoomWidth/wRoomHeight are themselves twice a metatile count by construction),
// so plain integer division never silently drops an odd half-tile.
//
// Returns nil, not a table of zeroes, when no game is running. The three fields this reads --
// room_w, room_h, room_group -- are only meaningful while f->wram is non-NULL (see ModsFrame's
// own comment in mods.h: they default to plain 0 otherwise, the same convention `room` already
// follows); a table of zeroes would repeat exactly the lie oracles.screen() told before Stage 1
// fixed it, except that fix doesn't apply here. screen() could default sanely to 160 because 160
// is never a *real* in-game width the launcher could confuse with; 0 is a perfectly plausible
// room, group, width or height, so returning a table of zeroes here would be indistinguishable
// from a real room 0 of width 0 -- an honest nil is the only answer that isn't a lie. f->wram is
// the same guard l_rd uses to decide whether the bank-0 window is live, not an invented
// condition of this binding's own.
//
// Takes no arguments; extra ones are silently ignored (ordinary Lua semantics for a C function
// that never inspects lua_gettop/looks past the stack it needs) rather than rejected -- a
// deliberate choice, not an oversight, since there is nothing hostile an extra argument to a
// zero-argument accessor could do. See oracles_room_ignores_extra_arguments in test_mods.c for
// the pinned behaviour.
static int l_room(lua_State *L) {
  const ModsFrame *f = mods_current_frame();
  if (!f->wram) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, f->room_group);
  lua_setfield(L, -2, "group");
  lua_pushinteger(L, f->room);
  lua_setfield(L, -2, "room");
  lua_pushinteger(L, f->room_w / 2);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, f->room_h / 2);
  lua_setfield(L, -2, "height");
  return 1;
}

// Stage 3 Task 3: the metatile id at metatile coordinates (x, y) in the current room.
//
// wRoomLayout ($cf00, 0xc0 bytes, bank 0) is 16 bytes a row -- a fixed stride that does not
// track the room's own width -- of which only the first room_w/2 columns of any given row are
// meaningful; the rest is padding (`blank_data_around_collisions`, src/game/bank0.c:3467, writes
// 0x00 there for columns past the room, which is exactly the ambiguity this binding exists to
// avoid: a 0x00 id is a real, legal metatile, so handing back the padding byte for an
// out-of-room query would be a value a mod cannot distinguish from a real tile with id 0).
//
// `x`/`y` come straight from Lua and are therefore hostile: check_int, not a bare
// (int)luaL_checkinteger cast, so a 64-bit argument like 4294967296 (2^32) clamps to INT_MAX
// instead of wrapping back around to 0 and silently addressing the wrong tile -- see check_int's
// own comment for the draw_rect case this already fixed once.
//
// Returns nil, not the padding byte and not 0, for:
//   - no game running (f->wram NULL, the same guard l_room and l_rd both use);
//   - (x, y) outside the room's own room_w/2 by room_h/2 metatiles -- the documented, deliberate
//     choice (see MODDING.md's oracles.metatile entry) over returning the ambiguous padding byte;
//   - defensively, any (x, y) that would still land outside wRoomLayout's own 0xc0-byte region
//     even after the room-bounds check above -- which cannot happen for any room_w/room_h the
//     real game has ever produced (room_h/2 tops out at 11, so y*16+x tops out at 175, inside
//     0xc0 == 192), but costs nothing to check and turns "a future room geometry no one has seen
//     yet" into another nil rather than a read past the 0xc0 bytes this table actually occupies;
//   - x of 16 or more, for the same reason: past the stride it would alias the next row.
static int l_metatile(lua_State *L) {
  int x = check_int(L, 1);
  int y = check_int(L, 2);
  const ModsFrame *f = mods_current_frame();
  if (!f->wram) {
    lua_pushnil(L);
    return 1;
  }
  int mw = f->room_w / 2, mh = f->room_h / 2;
  if (x < 0 || y < 0 || x >= mw || y >= mh || x >= 16) {
    lua_pushnil(L);
    return 1;
  }
  int idx = y * 16 + x;
  if (idx < 0 || idx >= 0xc0) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushinteger(L, f->wram[0xf00 + idx]);
  return 1;
}

// Stage 3 Task 4: a collision value, raw and decoded. Below $10 it is four quarter-tile bits,
// verified by running the ROM's own _simpleCollision in the interpreter for every quarter in both
// games: bit 3 top-left, bit 2 top-right, bit 1 bottom-left, bit 0 bottom-right. $10 and above
// indexes a ROM table of octant masks (holes, ledges, bridges) that also depends on which check
// the game is making, so `solid` and `quarters` are left nil rather than guessed.
static void push_collision(lua_State *L, int v) {
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, v);
  lua_setfield(L, -2, "raw");
  bool special = v >= 0x10;
  lua_pushboolean(L, special);
  lua_setfield(L, -2, "special");
  if (special) return;
  lua_pushboolean(L, v == 0x0f);
  lua_setfield(L, -2, "solid");
  lua_createtable(L, 0, 4);
  lua_pushboolean(L, v & 8);
  lua_setfield(L, -2, "tl");
  lua_pushboolean(L, v & 4);
  lua_setfield(L, -2, "tr");
  lua_pushboolean(L, v & 2);
  lua_setfield(L, -2, "bl");
  lua_pushboolean(L, v & 1);
  lua_setfield(L, -2, "br");
  lua_setfield(L, -2, "quarters");
}

// The tileset's default collision for metatile `id`, from w3TileCollisions ($db00, WRAM bank 3).
// An id is a byte, so anything outside 0..255 is the mod's mistake and raises rather than being
// masked into some other tile's answer. nil when bank 3 is not there (no game running).
static int l_solidity(lua_State *L) {
  int id = check_int(L, 1);
  luaL_argcheck(L, id >= 0 && id <= 0xff, 1, "metatile id must be 0-255");
  const ModsFrame *f = mods_current_frame();
  if (!f->wram3) {
    lua_pushnil(L);
    return 1;
  }
  push_collision(L, f->wram3[0xb00 + id]);
  return 1;
}

// The live collision at metatile (x, y), from wRoomCollisions ($ce00, bank 0): the tileset default
// plus whatever the room's objects set on load, so it can differ from solidity(metatile(x, y)) --
// 13 of 80 tiles do in Seasons overworld room $98. Same bounds and nil rules as l_metatile: the
// 16-byte stride's padding (the game writes $ff there) is not a tile.
static int l_collision(lua_State *L) {
  int x = check_int(L, 1);
  int y = check_int(L, 2);
  const ModsFrame *f = mods_current_frame();
  if (!f->wram) {
    lua_pushnil(L);
    return 1;
  }
  int mw = f->room_w / 2, mh = f->room_h / 2;
  if (x < 0 || y < 0 || x >= mw || y >= mh || x >= 16 || y * 16 + x >= 0xc0) {
    lua_pushnil(L);
    return 1;
  }
  push_collision(L, f->wram[0xe00 + y * 16 + x]);
  return 1;
}

// Stage 3 Task 5: BG palette `i` as four RGB555 colours, the format draw_rect takes. A palette
// index is 0..7 on the hardware, so anything else raises. Each colour is two little-endian bytes
// whose bit 15 the hardware ignores. nil with no picture yet.
static int l_tile_palette(lua_State *L) {
  int i = check_int(L, 1);
  luaL_argcheck(L, i >= 0 && i <= 7, 1, "palette must be 0-7");
  const ModsFrame *f = mods_current_frame();
  if (!f->bg_pal) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 4, 0);
  for (int c = 0; c < 4; c++) {
    const uint8_t *p = &f->bg_pal[i * 8 + c * 2];
    lua_pushinteger(L, (p[0] | p[1] << 8) & 0x7fff);
    lua_rawseti(L, -2, c + 1);
  }
  return 1;
}

// Stage 3 Task 5: the sprites on screen, as { x, y, tile, attr } in OAM order, with x/y the
// sprite's top-left in the 160x144 picture (OAM stores them +8 and +16). An entry the hardware
// does not show -- OAM y of 0 or 160 and up, x of 0 or 168 and up -- is left out, so the count is
// what is visible, not 40. nil with no picture yet.
static int l_sprites(lua_State *L) {
  const ModsFrame *f = mods_current_frame();
  if (!f->oam) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 40, 0);
  int n = 0;
  for (int i = 0; i < 40; i++) {
    const uint8_t *o = &f->oam[i * 4];
    if (o[0] == 0 || o[0] >= 160 || o[1] == 0 || o[1] >= 168) continue;
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, o[1] - 8);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, o[0] - 16);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, o[2]);
    lua_setfield(L, -2, "tile");
    lua_pushinteger(L, o[3]);
    lua_setfield(L, -2, "attr");
    lua_rawseti(L, -2, ++n);
  }
  return 1;
}

// Stage 3 Task 6: { x, y, room_x, room_y }. x/y are the hardware scroll (SCX/SCY, 0-255) the play
// area's first line was drawn with; room_x/room_y the room pixel at the play area's top-left, the
// same frame sprites() describes, so room_x + sprite.x is the sprite's place in the room. room_x
// and room_y are left out outside normal play rather than given a position the picture does not
// show. nil with no picture.
static int l_camera(lua_State *L) {
  const ModsFrame *f = mods_current_frame();
  if (!f->scroll) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, f->scroll_x);
  lua_setfield(L, -2, "x");
  lua_pushinteger(L, f->scroll_y);
  lua_setfield(L, -2, "y");
  if (f->origin) {
    lua_pushinteger(L, f->origin_x);
    lua_setfield(L, -2, "room_x");
    lua_pushinteger(L, f->origin_y);
    lua_setfield(L, -2, "room_y");
  }
  return 1;
}

static int l_draw_rect(lua_State *L) {
  overlay_rect(mods_overlay_mut(), check_int(L, 1), check_int(L, 2), check_int(L, 3),
               check_int(L, 4), check_int(L, 5));
  return 0;
}

static int l_draw_text(lua_State *L) {
  const ModsFrame *f = mods_current_frame();
  if (f->font) {
    // luaL_checklstring, not luaL_checkstring: a Lua string may legally contain an embedded
    // NUL, and that decision is made explicit here on purpose -- overlay_text (Task 3, not
    // touched by this task) takes a plain NUL-terminated C string with no length parameter of
    // its own, so "a\0b" still only draws "a"'s glyph no matter which checker is used to fetch
    // the argument. Using checklstring instead of checkstring is this binding saying that
    // truncation out loud -- choosing it deliberately, as the task requires, rather than
    // silently inheriting checkstring's behaviour and leaving it undecided. `len` is kept (not
    // discarded) so the decision is visible at the call site, and so a future change here, or in
    // overlay_text, has it on hand rather than needing to recompute it. See
    // draw_text_truncates_at_an_embedded_nul in test_mods.c for the pinned behaviour, and
    // MODDING.md's own oracles.draw_text entry for where a mod author is actually told.
    size_t len;
    const char *s = luaL_checklstring(L, 3, &len);
    (void)len;
    overlay_text(mods_overlay_mut(), f->font, check_int(L, 1), check_int(L, 2), s, check_int(L, 4));
  }
  return 0;
}

static int l_draw_icon(lua_State *L) {
  const ModsFrame *f = mods_current_frame();
  bool ok = false;
  // Both guarded, not just f->rom: overlay_icon's c600 argument is f->wram + 0x600, which is
  // NULL + 0x600 (undefined behaviour, not a null pointer) if f->wram is NULL -- true in the
  // launcher before any game is running, exactly the state f->rom is also NULL in, but a
  // distinct field a mod-author-visible guard on f->rom alone would not catch. wide_item_icon
  // null-checks rom but not c600, so this is the only guard standing between a NULL wram and a
  // read through it.
  if (f->rom && f->wram)
    ok = overlay_icon(mods_overlay_mut(), f->rom, f->rom_size, f->seasons,
                      (uint8_t)luaL_checkinteger(L, 1), f->wram + 0x600,
                      check_int(L, 2), check_int(L, 3));
  lua_pushboolean(L, ok);
  return 1;
}

// ---------------------------------------------------------------------------------------------
// Task 9: settings.
// ---------------------------------------------------------------------------------------------

static int l_setting_get_bool(lua_State *L) {
  lua_pushboolean(L, mods_setting_value((int)lua_tointeger(L, lua_upvalueindex(1))));
  return 1;
}

static int l_setting_get_number(lua_State *L) {
  lua_pushinteger(L, mods_setting_number((int)lua_tointeger(L, lua_upvalueindex(1))));
  return 1;
}

// Reads element `n` of the table at stack index `tbl_idx` as an integer, clamped exactly the way
// check_int clamps an ordinary argument -- lua_rawgeti (not lua_geti) deliberately, so a hostile
// __index metamethod on a mod-supplied table cannot run arbitrary Lua from inside what looks like
// a plain {min, max, default} literal.
static int table_int_at(lua_State *L, int tbl_idx, int n) {
  lua_rawgeti(L, tbl_idx, n);
  lua_Integer v = luaL_checkinteger(L, -1);
  lua_pop(L, 1);
  return clamp_int(v);
}

// oracles.setting(name, true/false) makes an ON/OFF row; oracles.setting(name, {min, max,
// default}) makes a numeric row -- "the two shapes the settings menu already draws", per the
// spec. A pre-dispatch audit flagged that the original sketch implemented only the boolean shape
// and would have let oracles.setting(name, {1, 10, 5}) silently behave as a (truthy) boolean
// instead of either implementing or deliberately refusing the numeric one; this implements it,
// branching on lua_type(L, 2) rather than on anything about the default value itself, since both
// shapes can start at a value that would look sensible as either kind.
static int l_setting(lua_State *L) {
  size_t len;
  const char *name = luaL_checklstring(L, 1, &len);
  if (lua_type(L, 2) == LUA_TTABLE) {
    int lo = table_int_at(L, 2, 1), hi = table_int_at(L, 2, 2), def = table_int_at(L, 2, 3);
    int idx = mods_setting_add_number(name, len, lo, hi, def);
    if (idx < 0)
      return luaL_error(L, "oracles.setting: too many settings, or an invalid/too-long name");
    lua_pushinteger(L, idx);
    lua_pushcclosure(L, l_setting_get_number, 1);
    return 1;
  }
  bool initial = lua_isnone(L, 2) ? true : lua_toboolean(L, 2);
  int idx = mods_setting_add(name, len, initial);
  if (idx < 0)
    return luaL_error(L, "oracles.setting: too many settings, or an invalid/too-long name");
  lua_pushinteger(L, idx);
  // A getter closure, not the current value itself, so a mod that stashes the return value in a
  // local never caches a stale read: the settings menu (a future task) can change the row at any
  // time, and the mod only ever sees that change the next time it calls show().
  lua_pushcclosure(L, l_setting_get_bool, 1);
  return 1;
}

// ---------------------------------------------------------------------------------------------
// Task 9: the per-mod store.
// ---------------------------------------------------------------------------------------------

// Converts the Lua value at `idx` into a ModValue, enforcing the spec's "strings, numbers and
// booleans" at the boundary rather than downstream: a table, function, nil, userdata or thread
// all fail here, as does a string that fails mods_store_string_valid (too long, or containing
// any byte outside printable ASCII -- a newline or NUL could otherwise inject a second key=value
// row or corrupt store.ini's one-line-per-key shape; see mods_store_string_valid's own comment).
// lua_tolstring/lua_isinteger, not luaL_checkstring/a bare lua_tointeger: a Lua string may
// legally contain an embedded NUL (caught below, not silently truncated at it), and Lua 5.4
// itself distinguishes an integer-subtype number from a float-subtype one, which is preserved
// here (ModValueKind splits them) rather than collapsed into one "number" case the way a single
// lua_tonumber() call would.
static bool lua_to_mod_value(lua_State *L, int idx, ModValue *out) {
  switch (lua_type(L, idx)) {
  case LUA_TBOOLEAN:
    out->kind = MOD_VALUE_BOOL;
    out->b = lua_toboolean(L, idx);
    return true;
  case LUA_TNUMBER:
    if (lua_isinteger(L, idx)) {
      out->kind = MOD_VALUE_INT;
      out->i = (long long)lua_tointeger(L, idx);
    } else {
      out->kind = MOD_VALUE_FLOAT;
      out->f = (double)lua_tonumber(L, idx);
    }
    return true;
  case LUA_TSTRING: {
    size_t len;
    const char *s = lua_tolstring(L, idx, &len);
    if (!mods_store_string_valid(s, len)) return false;
    out->kind = MOD_VALUE_STRING;
    memcpy(out->s, s, len);
    out->s[len] = 0;
    return true;
  }
  default:
    return false;
  }
}

static int l_store(lua_State *L) {
  size_t klen;
  const char *key = luaL_checklstring(L, 1, &klen);
  luaL_checkany(L, 2);
  ModValue v;
  if (!lua_to_mod_value(L, 2, &v))
    return luaL_error(L, "oracles.store: value must be a string, number or boolean, within "
                         "this build's size and character limits");
  if (!mods_store_set(key, klen, &v))
    return luaL_error(L, "oracles.store: could not save (invalid key, or the store could not "
                         "be written)");
  return 0;
}

static int l_load(lua_State *L) {
  size_t klen;
  const char *key = luaL_checklstring(L, 1, &klen);
  ModValue v;
  if (!mods_store_get(key, klen, &v)) {
    lua_pushnil(L);
    return 1;
  }
  switch (v.kind) {
  case MOD_VALUE_BOOL: lua_pushboolean(L, v.b); break;
  case MOD_VALUE_INT: lua_pushinteger(L, (lua_Integer)v.i); break;
  case MOD_VALUE_FLOAT: lua_pushnumber(L, v.f); break;
  case MOD_VALUE_STRING: lua_pushstring(L, v.s); break;
  default: lua_pushnil(L); break;
  }
  return 1;
}

static int l_swap_treasure(lua_State *L) {
  int a[6];
  for (int i = 0; i < 6; i++) a[i] = check_int(L, i + 1);
  const char *why = mods_add_swap(a[0], a[1], a[2], a[3], a[4], a[5]);
  if (why) return luaL_error(L, "oracles.swap_treasure: %s", why);
  return 0;
}

static int l_wr(lua_State *L) {
  int addr = check_int(L, 1), value = check_int(L, 2);
  const char *why = mods_queue_write(addr, value);
  if (why) return luaL_error(L, "oracles.wr: %s", why);
  return 0;
}

// The one list of what the API is. MODDING.md must document exactly these. Kept alphabetical so
// that cross-check can simply walk both lists in order.
static const luaL_Reg api[] = {
  {"camera", l_camera},
  {"collision", l_collision},
  {"draw_icon", l_draw_icon},
  {"draw_rect", l_draw_rect},
  {"draw_text", l_draw_text},
  {"game", l_game},
  {"load", l_load},
  {"log", l_log},
  {"metatile", l_metatile},
  {"on_frame", l_on_frame},
  {"on_room_change", l_on_room_change},
  {"rd", l_rd},
  {"room", l_room},
  {"screen", l_screen},
  {"setting", l_setting},
  {"solidity", l_solidity},
  {"sprites", l_sprites},
  {"store", l_store},
  {"swap_treasure", l_swap_treasure},
  {"tile_palette", l_tile_palette},
  {"wr", l_wr},
  {NULL, NULL},
};

void mods_api_open(lua_State *L) {
  luaL_newlib(L, api);
  lua_setglobal(L, "oracles");
}

int mods_api_count(void) { return (int)(sizeof api / sizeof api[0]) - 1; }
const char *mods_api_name(int i) { return i >= 0 && i < mods_api_count() ? api[i].name : ""; }
