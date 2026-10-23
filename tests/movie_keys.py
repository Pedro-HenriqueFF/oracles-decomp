"""Turns the start of a TAS .inputs movie into ORACLES_TEST_KEYS for the real app.

The movie's inputs are frame-exact, but ORACLES_TEST_KEYS ticks are not frames: in normal play each
tick is paced to real wall-clock time against the (dummy) audio clock. Holding FAST (Tab) the whole
run makes the app run a fixed number of frames per tick, so every GROUP consecutive movie frames are
OR'd into one tick's held buttons. That is coarser than the movie, but every button in the opening
sequence is held for several frames at a time, so it still drives the same menus, naming screen and
short walk."""
import struct

GROUP = 4
NAMES = ['X', 'Z', None, 'Return', 'Right', 'Left', 'Up', 'Down']


def movie_keys(tas_path, nframes):
    """Returns (keys, nframes actually used), or None when the file is not an ORIN movie."""
    with open(tas_path, 'rb') as f:
        data = f.read()
    if data[:4] != b'ORIN':
        return None
    count = struct.unpack('<I', data[4:8])[0]
    inputs = data[8:8 + count]
    nframes = min(nframes, count)
    entries = []
    nticks = (nframes + GROUP - 1) // GROUP
    for t in range(1, nticks + 2):
        entries.append(f'{t}:Tab')
    for tk in range(nticks):
        agg = 0
        for f in range(tk * GROUP, min((tk + 1) * GROUP, nframes)):
            agg |= inputs[f]
        for k in range(8):
            if (agg >> k) & 1 and NAMES[k]:
                entries.append(f'{tk + 1}:{NAMES[k]}')
    return ','.join(entries), nframes
