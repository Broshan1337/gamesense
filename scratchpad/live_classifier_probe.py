import struct

PID = 280956
BASE = 0x7fb25f90c000
CLIENT = 0x7fb2a4800000

mem = open(f'/proc/{PID}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]

gc = u64(BASE + 0xebb460)
print(f'GlobalContext = {gc:#x}')
hasComplete = rd(gc + 24 + 7784, 1)[0]
print(f'hasCompleteObject = {hasComplete}')
FULL = gc + 24
CLASSIFIER = FULL + 7224
classes = struct.unpack('<52Q', rd(CLASSIFIER, 52 * 8))
nonzero = [(i, v) for i, v in enumerate(classes) if v]
print(f'classifier: {len(nonzero)}/52 filled')
for i, v in nonzero[:8]:
    print(f'  [{i:2d}] {v:#x}')
if len(nonzero) == 52:
    print('CLASSIFIER FULLY POPULATED')

# PSR re-check for this session (was verified live earlier; keep here for the record)
eight = rd(FULL + 1616 + 19 + 49 * 4, 58 * 8)
vals = struct.unpack('<58Q', eight)
print(f'client pool 8B results nonzero: {sum(1 for v in vals if v)}/58')
print(f'  EntitySystemPointer slot expected at client+0x4918570: found={any(v == CLIENT + 0x4918570 for v in vals)}')
