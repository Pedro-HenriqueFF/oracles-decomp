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
            "  oracles.log('roomwatch room=' .. r .. ' ok=' .. tostring(ok) ..\n"
            "              ' group=' .. (info and info.group or -1) ..\n"
            "              ' rawgroup=' .. raw_group ..\n"
            "              ' width=' .. (info and info.width or -1) ..\n"
            "              ' raww=' .. raw_w ..\n"
            "              ' height=' .. (info and info.height or -1) ..\n"
            "              ' rawh=' .. raw_h)\n"
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
print('ok mods_room')
