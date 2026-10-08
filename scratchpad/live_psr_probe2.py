import struct

import os
PID = int(os.environ.get("PROBE_PID", "280956"))
import os
BASE = int(os.environ.get("PROBE_BASE", "0x7fb25f90c000"), 16)

mem = open(f'/proc/{PID}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]

gc = u64(BASE + 0xebb460)          # ManuallyDestructible.object
print(f'GlobalContext instance = {gc:#x}')
if not gc:
    raise SystemExit('not initialized')
hasComplete = rd(gc + 24 + 7784, 1)[0]
print(f'hasCompleteObject = {hasComplete}')
FULL = gc + 24                       # complete object base (union at 0)
PSR = FULL + 1616
one = rd(PSR, 19)
four = rd(PSR + 19, 49 * 4)
eight = rd(PSR + 19 + 49 * 4, 58 * 8)

KNOWN = dict((int(k,16), v) for k,v in {
    '0x53eff9f6800': 'old-session EntitySystemPointer',
}.items())  # knowns are session-specific; probe prints all instead
KNOWN_UNUSED = {
    0x53eff9f6800: 'EntitySystemPointer (live value)',
    0x7f8d2404e160: 'GameEventManagerGlobalPointer (live)',
    0x7f8bb56830f0: 'CSGOInputPointer (live)',
    0x53f7f4bf000: 'GameRulesPointer (live)',
    0x53f78099400: 'LocalPlayerControllerPointer (live)',
}
print('\n8-byte results, nonzero:')
vals = struct.unpack('<58Q', eight)
for i, v in enumerate(vals):
    mark = f'  <<== {KNOWN[v]}' if v in KNOWN else ''
    if v: print(f'  [{i:2d}] {v:#x}{mark}')
print(f'\nnonzero count: {sum(1 for v in vals if v)}/58')
print('all-zero eightByteResults:', all(v == 0 for v in vals))
print('all-zero fourByteResults:', all(v == 0 for v in struct.unpack("<49I", four)))
print('all-zero oneByteResults :', all(v == 0 for v in one))
