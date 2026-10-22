#!/usr/bin/env python3
"""Pins app.c's rd_room through the real frame path, which nothing else does: app.c is not linked
into test_mods, and the TAS replays that exercise the same WRAM address (test_wide.c, test_room.c)
never call through app.c at all. Installs a mod whose on_room_change logs the room number, drives
the real app (SDL dummy drivers, ORACLES_TEST_KEYS) through the start of the known-good Seasons
movie far enough to walk out of the starting room, and asserts the handler actually fired with a
changed, non-starting room -- matching the rooms test_room.c's record_cb already validates for the
same address (0xc4c, i.e. $cc4c) from the engine side.

Stage 3 Task 2 extends the same mod to also call oracles.room() on every on_room_change and
separately re-read the backing bytes with oracles.rd() at the addresses the plan's verified-facts
table and app.c's own rd_room_group comment confirm (0xcc49 for Seasons' room group,
0xcd0a/0xcd0b for wRoomWidth/wRoomHeight -- the width/height addresses are the same in both
games, so no seasons ternary is needed for those). This is a useful regression check on its own
(it would catch a wrong width/height address, or wram3-shaped plumbing mistakes carried into
room/group/width/height), but it does NOT catch app.c's rd_room_group ternary being swapped:
checked by mutation, swapping it leaves this test green, because room group is 0 for this test's
whole frame range (the opening of the game, confirmed by a WRAM dump at movie frame 9000: both
0xcc49, the correct Seasons address, and 0xcc2d, what a swapped ternary would read instead, are
0 there) -- the two addresses agree by coincidence at every frame this test reaches, so no
byte-content comparison here can tell a correct read from a swapped one.

Stage 3 Task 3 extends the same mod once more to walk oracles.metatile(x, y) over every (x, y)
inside the room's own width/height (in metatiles, i.e. the halved values oracles.room() already
returns) on every on_room_change, comparing each one against oracles.rd(0xcf00 + y*16 + x) --
the same $cf00 wRoomLayout address the plan's verified-facts table gives, read independently
through the already-pinned oracles.rd rather than hardcoding any id, so this does not pin the
test to one movie frame's specific layout. It also calls oracles.metatile(width, 0) -- one column
past the room's own width, inside the 16-byte stride for any room up to 16 metatiles wide -- and
checks that comes back nil rather than the padding byte wRoomLayout actually holds there, which
is the live engine-loaded version of the same ambiguity test_mods.c's synthetic
metatile_is_nil_past_the_rooms_width_not_just_the_stride already pins against a fabricated
ModsFrame.

KNOWN, DOCUMENTED GAP (not closed by this file): catching that specific mutation needs a frame
where room group is actually non-zero. Driving this same movie's replay, by two different
input-encoding strategies (OR-aggregating each FAST-mode tick's 4 real frames, and sampling only
the last of each 4), tens of thousands of frames further than this test does -- to movie frame
~33,440, about 8.5 real minutes of test time -- never left the small room cluster {0, 167, 151,
152} this test already sees by frame ~9500, and so never observed a non-zero group either. A
frame-exact replay (FAST mode off, one real GB frame per ORACLES_TEST_KEYS tick) was also tried,
to rule out an artifact of either aggregation strategy, and reached a *different* room sequence
within a comparable frame budget -- consistent with the opening sequence (naming, which save
file gets created, the first several ticks' input timing) being more timing-sensitive than this
harness's tick granularity, not with any of the three replays being "more correct" than another.
None of the three reached a confirmed non-zero room group within a few-minutes test budget.
Reaching the first dungeon (where a separate WRAM dump recorded group=4, room=4) evidently needs
either many more frames than 33,440, a different TAS movie/recording, or input delivered with a
fidelity this harness's tick-based ORACLES_TEST_KEYS mechanism does not have for anything beyond
the held-button "opening sequence" its own encoding comment already scopes it to. Closing this
gap for real is therefore left to a future task with either a faster or finer-grained way to
drive app.c's present_game (the only thing that exercises rd_room_group at all, app.c being
deliberately excluded from test_mods) through enough real gameplay to reach a non-zero group.

The movie's inputs are frame-exact, but ORACLES_TEST_KEYS ticks are not frames: in normal play each
tick is paced to real wall-clock time against the (dummy) audio clock, so tick:frame drifts all over
the place early on. Holding the FAST action for the whole run sidesteps that pacing (fast mode runs
a fixed 4 real GB frames per tick, unconditionally -- see run_game's `fast` branch in app.c) and
makes tick:frame a known, constant 4:1, so every 4 consecutive movie frames are OR'd into one tick's
held buttons. That is coarser than the original frame-exact movie, but every button in the opening
sequence is held for several frames at a time, so the OR'd version still drives the same menus,
naming screen and short walk, just not frame-perfect.

usage: test_mods_room.py ORACLES_NATIVE ROM TAS_INPUTS   (exit 77 when either is missing)"""
import os, subprocess, struct, sys, tempfile

app, rom, tas_path = sys.argv[1], sys.argv[2], sys.argv[3]
if not os.path.exists(rom) or not os.path.exists(tas_path):
    print('skip: ROM or TAS inputs not present')
    sys.exit(77)

with open(tas_path, 'rb') as f:
    data = f.read()
if data[:4] != b'ORIN':
    print('skip: unrecognised TAS inputs format')
    sys.exit(77)
count = struct.unpack('<I', data[4:8])[0]
inputs = data[8:8 + count]

# 9500, not just enough to leave the starting room (~1881): confirmed empirically, wRoomWidth/
# wRoomHeight read back 0 (not yet populated) for the whole movie until somewhere before frame
# 9500, where they become the plan's own observed 20x16 (10x8 metatiles) -- a mutation-tested
# fact, not a guess: at 2200 frames, swapping rd_room_w and rd_room_h's addresses (or deleting
# l_room's /2 halving entirely) left this test green, because width/height's independent-byte
# check was comparing 0 against 0 regardless of which address is read. 9500 frames (~75 real
# seconds under fast mode) is long enough that this check is no longer vacuous for width/height;
# room group, separately, is NOT observed to become non-zero anywhere in this movie's first
# ~33,400 frames (see this file's own "KNOWN, DOCUMENTED GAP" paragraph above), so extending
# NFRAMES further would cost real minutes without closing that particular gap.
NFRAMES = min(9500, count)
GROUP = 4  # matches fast mode's 4-real-frames-per-tick

# GB joypad bit -> SDL key name, via this app's default bindings (src/ui/controls.c): A=X, B=Z,
# Right/Left/Up/Down = arrow keys, Start=Return. Select is never used by the opening sequence.
NAMES = ['X', 'Z', None, 'Return', 'Right', 'Left', 'Up', 'Down']

entries = []
nticks = (NFRAMES + GROUP - 1) // GROUP
for t in range(1, nticks + 2):
    entries.append(f'{t}:Tab')               # hold FAST the whole time
for tk in range(nticks):
    agg = 0
    for f in range(tk * GROUP, min((tk + 1) * GROUP, NFRAMES)):
        agg |= inputs[f]
    tick = tk + 1
    for k in range(8):
        if (agg >> k) & 1 and NAMES[k]:
            entries.append(f'{tick}:{NAMES[k]}')
keys = ','.join(entries)

env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy')
with tempfile.TemporaryDirectory() as cache:
    r = subprocess.run([app, rom, '--cache', cache, '--frames', '1'], env=env, capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr

    moddir = os.path.join(cache, 'mods', 'roomwatch')
    os.makedirs(moddir, exist_ok=True)
    with open(os.path.join(moddir, 'mod.ini'), 'w') as f:
        f.write('id=roomwatch\nname=Roomwatch\napi=1\ngames=ages,seasons\n')
    with open(os.path.join(moddir, 'main.lua'), 'w') as f:
        # Seasons-only addresses, matching this harness always running '--game seasons': 0xcc49
        # is rd_room_group's seasons branch ($c000 + 0xc49), 0xcd0a/0xcd0b are wRoomWidth/
        # wRoomHeight (same address in both games, per the plan's verified-facts table). info's
        # width/height are already halved into metatiles by oracles.room() itself, so they are
        # compared against raw_w/raw_h floor-divided by 2 here, not against raw_w/raw_h directly.
        f.write(
            "oracles.on_room_change(function(r)\n"
            "  local info = oracles.room()\n"
            "  local raw_group = oracles.rd(0xcc49)\n"
            "  local raw_w = oracles.rd(0xcd0a)\n"
            "  local raw_h = oracles.rd(0xcd0b)\n"
            "  local ok = info ~= nil and info.room == r and info.group == raw_group\n"
            "             and info.width == raw_w // 2 and info.height == raw_h // 2\n"
            "  -- Task 3: oracles.metatile(x, y) against the same $cf00 bytes read\n"
            "  -- independently via oracles.rd, over every in-room (x, y) -- not a hardcoded id.\n"
            "  local meta_ok = true\n"
            "  local meta_nonzero = false\n"
            "  if info ~= nil then\n"
            "    for y = 0, info.height - 1 do\n"
            "      for x = 0, info.width - 1 do\n"
            "        local id = oracles.metatile(x, y)\n"
            "        local raw = oracles.rd(0xcf00 + y * 16 + x)\n"
            "        if id ~= raw then meta_ok = false end\n"
            "        if raw ~= 0 then meta_nonzero = true end\n"
            "      end\n"
            "    end\n"
            "  end\n"
            "  -- One column past the room's own width, still inside the 16-byte stride: must be\n"
            "  -- nil, not whatever padding byte wRoomLayout actually holds there.\n"
            "  local oob_nil = info == nil or oracles.metatile(info.width, 0) == nil\n"
            "  -- Task 4: oracles.collision(x, y).raw against $ce00 read through oracles.rd.\n"
            "  local col_ok = true\n"
            "  local col_nonzero = false\n"
            "  if info ~= nil then\n"
            "    for y = 0, info.height - 1 do\n"
            "      for x = 0, info.width - 1 do\n"
            "        local c = oracles.collision(x, y)\n"
            "        local raw = oracles.rd(0xce00 + y * 16 + x)\n"
            "        if c == nil or c.raw ~= raw then col_ok = false end\n"
            "        if raw ~= 0 then col_nonzero = true end\n"
            "      end\n"
            "    end\n"
            "  end\n"
            "  oracles.log('roomwatch room=' .. r .. ' ok=' .. tostring(ok) ..\n"
            "              ' group=' .. (info and info.group or -1) ..\n"
            "              ' rawgroup=' .. raw_group ..\n"
            "              ' width=' .. (info and info.width or -1) ..\n"
            "              ' raww=' .. raw_w ..\n"
            "              ' height=' .. (info and info.height or -1) ..\n"
            "              ' rawh=' .. raw_h ..\n"
            "              ' metaok=' .. tostring(meta_ok) ..\n"
            "              ' metanonzero=' .. tostring(meta_nonzero) ..\n"
            "              ' oobnil=' .. tostring(oob_nil) ..\n"
            "              ' colok=' .. tostring(col_ok) ..\n"
            "              ' colnonzero=' .. tostring(col_nonzero))\n"
            "end)\n"
            # Task 4: bank 3 has no oracles.rd window, so solidity is checked against what the
            # game builds from it: loadRoomCollisions fills $ce00 with w3TileCollisions[layout id],
            # then a room's objects may change some tiles (13 of 80 in Seasons room $98, none in
            # room $a7). So at least one frame must show a whole room where
            # solidity(metatile(x, y)) equals the $ce00 byte everywhere, with a non-zero one
            # among them. A NULL or wrong wram3 in app.c cannot produce that frame.
            "local sol_exact = false\n"
            "local sol_nil = false\n"
            # Task 5: sprites() against the game's own shadow OAM, wOam at $cb00 in bank 0, with
            # the same visibility filter. The picture's OAM is wOam as DMA'd in the last vblank,
            # so they agree whenever nothing moved for a frame; one such frame with a sprite on
            # screen is required. tile_palette has no bank-0 copy to compare with (the game keeps
            # its palettes in bank 2), so it must be non-nil in a game and show a palette with at
            # least three distinct colours.
            "local spr_exact, pal_nil, pal_varied = false, false, false\n"
            "local function check_sprites()\n"
            "  local s = oracles.sprites()\n"
            "  if s == nil then return end\n"
            "  local want = {}\n"
            "  for i = 0, 39 do\n"
            "    local y, x = oracles.rd(0xcb00 + i * 4), oracles.rd(0xcb01 + i * 4)\n"
            "    if y ~= 0 and y < 160 and x ~= 0 and x < 168 then\n"
            "      want[#want + 1] = (x - 8) .. '/' .. (y - 16) .. '/' .. oracles.rd(0xcb02 + i * 4) ..\n"
            "                        '/' .. oracles.rd(0xcb03 + i * 4)\n"
            "    end\n"
            "  end\n"
            "  if #want == 0 or #want ~= #s then return end\n"
            "  for k, sp in ipairs(s) do\n"
            "    if want[k] ~= sp.x .. '/' .. sp.y .. '/' .. sp.tile .. '/' .. sp.attr then return end\n"
            "  end\n"
            "  spr_exact = true\n"
            "  oracles.log('sprwatch exact n=' .. #s)\n"
            "end\n"
            "local function check_palettes()\n"
            "  for i = 0, 7 do\n"
            "    local p = oracles.tile_palette(i)\n"
            "    if p == nil then\n"
            "      if not pal_nil then pal_nil = true oracles.log('palwatch nil') end\n"
            "      return\n"
            "    end\n"
            "    local seen, n = {}, 0\n"
            "    for _, c in ipairs(p) do if not seen[c] then seen[c] = true n = n + 1 end end\n"
            "    if n >= 3 and not pal_varied then pal_varied = true oracles.log('palwatch varied p=' .. i) end\n"
            "  end\n"
            "end\n"
            # Task 6: camera() against Seasons' own camera, hCameraX ($ffaa) / hCameraY ($ffa8),
            # and its scroll against wScreenOffsetX/Y ($cd09/$cd08): room_x is SCX minus the
            # offset, room_y SCY minus it plus the 16 status-bar lines. The camera is the drawn
            # picture's, so off the exact frame it may trail the game's camera by one frame or sit
            # up to 3 pixels off in a shake; anything else is logged as bad.
            "local cam_exact, cam_bad, cam_room = false, false, false\n"
            "local prev_cx, prev_cy = -1, -1\n"
            "local function check_camera()\n"
            "  local c = oracles.camera()\n"
            "  local cx = oracles.rd(0xffaa) + oracles.rd(0xffab) * 256\n"
            "  local cy = oracles.rd(0xffa8) + oracles.rd(0xffa9) * 256\n"
            "  if c ~= nil and c.room_x ~= nil then\n"
            "    cam_room = true\n"
            "    local sx = (c.x - oracles.rd(0xcd09)) % 256 == c.room_x % 256\n"
            "    local sy = (c.y - oracles.rd(0xcd08) + 16) % 256 == c.room_y % 256\n"
            "    if c.room_x == cx and c.room_y == cy and sx and sy then\n"
            "      if not cam_exact then cam_exact = true oracles.log('camwatch exact x=' .. c.room_x .. ' y=' .. c.room_y) end\n"
            "    elseif not (c.room_x == prev_cx and c.room_y == prev_cy)\n"
            "           and not (math.abs(c.room_x - cx) <= 3 and math.abs(c.room_y - cy) <= 3) then\n"
            "      if not cam_bad then cam_bad = true oracles.log('camwatch bad ' .. c.room_x .. ',' .. c.room_y .. ' camera ' .. cx .. ',' .. cy) end\n"
            "    end\n"
            "  end\n"
            "  prev_cx, prev_cy = cx, cy\n"
            "end\n"
            "oracles.on_frame(function()\n"
            "  check_camera()\n"
            "  if oracles.room() ~= nil and oracles.room().width > 0 then\n"
            "    if not spr_exact then check_sprites() end\n"
            "    if not pal_varied and not pal_nil then check_palettes() end\n"
            "  end\n"
            "  if sol_exact then return end\n"
            "  local info = oracles.room()\n"
            "  if info == nil or info.width == 0 then return end\n"
            "  local all, nonzero = true, false\n"
            "  for y = 0, info.height - 1 do\n"
            "    for x = 0, info.width - 1 do\n"
            "      local s = oracles.solidity(oracles.metatile(x, y))\n"
            "      local raw = oracles.rd(0xce00 + y * 16 + x)\n"
            "      if s == nil then\n"
            "        if not sol_nil then sol_nil = true oracles.log('solwatch nil room=' .. info.room) end\n"
            "        return\n"
            "      end\n"
            "      if s.raw ~= raw then all = false end\n"
            "      if raw ~= 0 then nonzero = true end\n"
            "    end\n"
            "  end\n"
            "  if all and nonzero then\n"
            "    sol_exact = true\n"
            "    oracles.log('solwatch exact room=' .. info.room)\n"
            "  end\n"
            "end)\n")

    max_frames = NFRAMES + 40
    r = subprocess.run([app, '--game', 'seasons', '--cache', cache, '--frames', str(max_frames)],
                        env=dict(env, ORACLES_TEST_KEYS=keys), capture_output=True, text=True, timeout=200)
    assert r.returncode == 0, r.stderr

    lines = [l for l in r.stderr.splitlines() if 'roomwatch room=' in l]
    parsed = []
    for l in lines:
        fields = l.split('roomwatch ', 1)[1]
        kv = dict(tok.split('=', 1) for tok in fields.split())
        parsed.append(kv)
    rooms = [int(d['room']) for d in parsed]
    assert len(rooms) >= 2, f'on_room_change fired {len(rooms)} time(s), expected at least 2; stderr:\n{r.stderr}'
    assert rooms[0] == 0, f'first room should be the pre-game default (0), got {rooms[0]}'
    assert any(x != 0 for x in rooms[1:]), f'never left room 0: {rooms}'

    # Self-check, asserted before the correctness check below: width/height's independent-byte
    # comparison is vacuous (0 against 0, no matter which address is read) until wRoomWidth/
    # wRoomHeight are actually populated -- exactly the failure mode that let a real mutation
    # (swapping rd_room_w/rd_room_h) through when NFRAMES was 2200. If this ever stops seeing a
    # non-zero width here, NFRAMES needs raising again, not a silently-weaker check.
    widths_seen = sorted(set(int(d['width']) for d in parsed))
    assert any(w != 0 for w in widths_seen), (
        f'never observed a non-zero room width (widths seen: {widths_seen}); the width/height '
        f'independent-byte check below would be vacuous -- raise NFRAMES. stderr:\n{r.stderr}')

    # oracles.room()'s group/room/width/height must match the same bytes read independently via
    # oracles.rd() at every single on_room_change call, not merely "eventually" or "on average".
    # A real regression check (e.g. a wrong width/height address), but -- see this file's own
    # docstring's "KNOWN, DOCUMENTED GAP" paragraph -- not what pins app.c's rd_room_group
    # ternary specifically, since group is 0 (so unable to disagree with anything) at every frame
    # this test reaches.
    mismatches = [d for d in parsed if d['ok'] != 'true']
    assert not mismatches, f'oracles.room() disagreed with independently-read raw bytes: {mismatches}'

    # Task 3's own non-vacuity guard, the same discipline as the width/height check above: at
    # least one oracles.rd(0xcf00 + y*16 + x) read during this run must actually be non-zero, or
    # the metaok comparison below could be trivially satisfied by both sides reading 0 for the
    # whole room (e.g. a layout that is never populated at these frames, or an l_metatile that
    # always returns 0/nil regardless of x/y).
    assert any(d['metanonzero'] == 'true' for d in parsed), (
        f'never observed a non-zero metatile id via oracles.rd(0xcf00+...); the '
        f'oracles.metatile() comparison below would be vacuous. stderr:\n{r.stderr}')

    # oracles.metatile(x, y) must agree with the same bytes read independently via oracles.rd,
    # for every (x, y) in the room, at every on_room_change call.
    meta_mismatches = [d for d in parsed if d['metaok'] != 'true']
    assert not meta_mismatches, (
        f'oracles.metatile() disagreed with independently-read raw bytes: {meta_mismatches}')

    # One column past the room's own width must come back nil, not the padding byte wRoomLayout
    # actually holds there -- the live-engine version of the ambiguity test_mods.c's synthetic
    # metatile_is_nil_past_the_rooms_width_not_just_the_stride test already pins.
    oob_failures = [d for d in parsed if d['oobnil'] != 'true']
    assert not oob_failures, (
        f'oracles.metatile(width, 0) was not nil for a real room: {oob_failures}')
    # Task 4: collision(x, y) against the same $ce00 bytes read through oracles.rd, with its own
    # non-vacuity guard (a grid of zeroes would agree with an always-zero binding).
    assert any(d['colnonzero'] == 'true' for d in parsed), (
        f'never observed a non-zero collision byte via oracles.rd(0xce00+...); the '
        f'oracles.collision() comparison would be vacuous. stderr:\n{r.stderr}')
    col_mismatches = [d for d in parsed if d['colok'] != 'true']
    assert not col_mismatches, (
        f'oracles.collision() disagreed with independently-read raw bytes: {col_mismatches}')

    # Task 4: solidity reads WRAM bank 3 through app.c's .wram3. Pinned by the frame above where a
    # whole room's solidity(metatile) matched $ce00 with a non-zero value in it.
    sol = [l for l in r.stderr.splitlines() if 'solwatch ' in l]
    assert not any('solwatch nil' in l for l in sol), (
        f'oracles.solidity() returned nil during a game, so app.c gave it no bank 3: {sol}')
    assert any('solwatch exact' in l for l in sol), (
        f'no frame had every tile\'s oracles.solidity(oracles.metatile(x, y)).raw equal $ce00; '
        f'stderr:\n{r.stderr}')
    # Task 5: sprites through app.c's .oam, palettes through its .bg_pal.
    spr = [l for l in r.stderr.splitlines() if 'sprwatch ' in l]
    assert spr, f'no frame had oracles.sprites() equal wOam\'s visible entries; stderr:\n{r.stderr}'
    pal = [l for l in r.stderr.splitlines() if 'palwatch ' in l]
    assert not any('palwatch nil' in l for l in pal), (
        f'oracles.tile_palette() returned nil during a game, so app.c gave it no palettes: {pal}')
    assert any('palwatch varied' in l for l in pal), (
        f'no BG palette had three distinct colours; stderr:\n{r.stderr}')
    # Task 6: camera() through app.c's scroll and origin fields.
    cam = [l for l in r.stderr.splitlines() if 'camwatch ' in l]
    assert not any('camwatch bad' in l for l in cam), f'oracles.camera() strayed from hCamera: {cam}'
    assert any('camwatch exact' in l for l in cam), (
        f'no frame had oracles.camera() equal hCamera with a matching scroll; stderr:\n{r.stderr}')
print('ok mods_room')
