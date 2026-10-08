#!/usr/bin/env python3
import struct, re
PANO = '/home/d/.local/share/Steam/steamapps/common/Counter-Strike Global Offensive/game/bin/linuxsteamrt64/libpanorama.so'
data = open(PANO, 'rb').read()
e_shoff = struct.unpack_from('<Q', data, 0x28)[0]
e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', data, 0x3a)
def sh(i):
    o = e_shoff + i * e_shentsize
    return struct.unpack_from('<IIQQQQ', data, o)
strtab_off = sh(e_shstrndx)[4]
def secname(i):
    o = strtab_off + sh(i)[0]; end = data.index(b'\0', o)
    return data[o:end].decode()
for i in range(e_shnum):
    nm, typ, flags, addr, off, size = sh(i)
    if secname(i) == '.text':
        base, blob = addr, data[off:off+size]
        break
def count(p):
    toks = p.split()
    rx = b''
    for t in toks:
        if '?' in t:
            fixed = t.replace('?', '')
            if fixed:
                rx += re.escape(bytes([int(fixed.zfill(2), 16)]))
            else:
                rx += b'.'
        else:
            rx += re.escape(bytes([int(t, 16)]))
    return [m.start() for m in re.finditer(rx, blob, re.DOTALL)]
tests = {
 'count (1-byte wc at +2)':  "8B B8 ? 02 00 00 85 FF 7E 44 48 8B 88 D0 02 00 00",
 'array (4-byte wc at +13)': "8B B8 ? 02 00 00 85 FF 7E 44 48 8B 88 ? ? ? ?",
}
for name, p in tests.items():
    hits = count(p)
    print(f"{name}: {len(hits)} at {[hex(base + h) for h in hits[:4]]}")
    print("   PASS" if len(hits) == 1 else "   FAIL")
