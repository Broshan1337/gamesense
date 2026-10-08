import struct

PID = 280956
mem = open(f'/proc/{PID}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]

CLASSES = {
    0x7fb2a8f3fa60: 'x343 (unrecognized)',
    0x7fb2a90f9e60: 'x8 (classifier slot 44)',
    0x7fb2a914a2c0: 'x1 (pawn, slot 45)',
    0x7fb2a9124c60: 'x1 (unrecognized)',
    0x7fb2a914eea0: 'x4 (unrecognized)',
    0x7fb2a9156a00: 'x3 (unrecognized)',
    0x7fb2a8edeee0: 'x1 (slot 1)',
    0x7fb2a90fa620: 'x1',
    0x7fb2a8fa8560: 'x1',
    0x7fb2a8edcba0: 'x1',
    0x7fb2a911da60: 'x10',
}
for cp, label in CLASSES.items():
    raw = rd(cp, 64)
    # try to find a plausible name pointer: scan qwords, follow to readable ascii
    name = '?'
    for off in range(0, 64, 8):
        p = struct.unpack_from('<Q', raw, off)[0]
        if 0x10000 < p < 0x7fffffffffff:
            try:
                s = rd(p, 48)
                end = s.find(b'\x00')
                cand = s[:end if end > 0 else 48]
                if 3 < len(cand) < 40 and all(32 <= b < 127 for b in cand):
                    name = cand.decode()
                    break
            except Exception:
                pass
    print(f'{cp:#x} {label:28s} first8={struct.unpack_from("<Q", raw, 0)[0]:#x} name?={name}')
