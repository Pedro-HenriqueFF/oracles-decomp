#!/usr/bin/env python3
"""Runs mods api=2 through the real app: the start of the Seasons movie drives oracles-native into
play, where a mod with api=2 and changes_game=yes swaps treasure $28/0 (rupees) for $0d/0
(bombchus) in the current room and writes an INTERAC_TREASURE object holding the rupees into a
free interaction slot with oracles.wr. The game loads that object on the next frame, through the
engine's treasure loader, so reading the slot back with oracles.rd shows the whole chain: the writes
landed between frames, the app handed the engine the mod's swap table, and the object carries the
bombchus (treasure $0d, grab mode 2, parameter $10, graphics $24) with the rupees' spawn mode (3,
out of a chest).

usage: test_mods_api2.py ORACLES_NATIVE ROM TAS_INPUTS   (exit 77 when either is missing)"""
import os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from movie_keys import movie_keys

app, rom, tas_path = sys.argv[1], sys.argv[2], sys.argv[3]
if not os.path.exists(rom) or not os.path.exists(tas_path):
    print('skip: ROM or TAS inputs not present')
    sys.exit(77)
got = movie_keys(tas_path, 9500)
if got is None:
    print('skip: unrecognised TAS inputs format')
    sys.exit(77)
keys, nframes = got

MOD = """
local slot, waited, done = nil, 0, false
oracles.on_frame(function()
  if done then return end
  local info = oracles.room()
  if info == nil or info.width == 0 then return end
  if slot == nil then
    for page = 0xd2, 0xdf do
      if oracles.rd(page * 256 + 0x40) == 0 then slot = page * 256 + 0x40 break end
    end
    if slot == nil then return end
    oracles.swap_treasure(info.group, info.room, 0x28, 0, 0x0d, 0)
    oracles.wr(slot, 1)
    oracles.wr(slot + 1, 0x60)
    oracles.wr(slot + 2, 0x28)
    oracles.wr(slot + 3, 0)
    for o = 4, 0x3f do oracles.wr(slot + o, 0) end
    oracles.log('api2watch spawned slot=' .. slot)
    return
  end
  waited = waited + 1
  local v30 = oracles.rd(slot + 0x30)
  if v30 ~= 0 or waited > 30 then
    done = true
    oracles.log('api2watch var30=' .. v30 .. ' var31=' .. oracles.rd(slot + 0x31) ..
                ' var32=' .. oracles.rd(slot + 0x32) .. ' var34=' .. oracles.rd(slot + 0x34) ..
                ' gfx=' .. oracles.rd(slot + 2))
  end
end)
"""

env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy')
with tempfile.TemporaryDirectory() as cache:
    r = subprocess.run([app, rom, '--cache', cache, '--frames', '1'], env=env, capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stderr
    moddir = os.path.join(cache, 'mods', 'api2watch')
    os.makedirs(moddir, exist_ok=True)
    with open(os.path.join(moddir, 'mod.ini'), 'w') as f:
        f.write('id=api2watch\nname=Api2watch\napi=2\nchanges_game=yes\ngames=seasons\n')
    with open(os.path.join(moddir, 'main.lua'), 'w') as f:
        f.write(MOD)
    r = subprocess.run([app, '--game', 'seasons', '--cache', cache, '--frames', str(nframes + 40)],
                       env=dict(env, ORACLES_TEST_KEYS=keys), capture_output=True, text=True, timeout=200)
    assert r.returncode == 0, r.stderr
    assert 'api2watch' not in ''.join(l for l in r.stderr.splitlines(True) if 'disabling' in l or 'failed' in l), r.stderr
    assert 'api2watch spawned' in r.stderr, f'never reached play with a free interaction slot; stderr:\n{r.stderr}'
    lines = [l for l in r.stderr.splitlines() if 'api2watch var30=' in l]
    assert lines, f'the spawned slot was never read back; stderr:\n{r.stderr}'
    kv = dict(tok.split('=', 1) for tok in lines[-1].split('api2watch ', 1)[1].split())
    want = {'var30': '13', 'var31': '3', 'var32': '2', 'var34': '16', 'gfx': '36'}
    assert kv == want, f'slot read back {kv}, want {want}'
print('ok mods_api2')
