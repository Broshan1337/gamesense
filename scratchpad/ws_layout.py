import struct
pid = 280956
CLIENT = 0x7fb2a4800000
mem = open(f'/proc/{pid}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]
def u32(a): return struct.unpack('<I', rd(a, 4))[0]

es = u64(CLIENT + 0x4918570)
ws = 0x33f48493800

def resolve(h):
    idx = h & 0x7FFF
    chunk = u64(es + 0x10 + (idx // 512) * 8)
    if not chunk: return None
    ident = chunk + (idx % 512) * 0x70
    e = u64(ident)
    if not e or u32(ident + 0x10) != h: return None
    return e, u64(ident + 8)

print('handles at ws+0x60 (as u32 array):')
for i in range(8):
    h = u32(ws + 0x60 + i * 4)
    r = resolve(h) if h else None
    print(f'  [{i}] {h:#010x} idx={h & 0x7FFF:5d} -> ' + (f'entity {r[0]:#x} class={r[1]:#x}' if r else 'UNRESOLVED'))

print()
p40, c48 = u64(ws + 0x40), u32(ws + 0x48)
p50, c58 = u64(ws + 0x50), u32(ws + 0x58)
print(f'+0x40 ptr={p40:#x} count(+0x48)={c48}')
print(f'+0x50 ptr={p50:#x} count(+0x58)={c58}')
for label, p, c in (('+0x40 array', p40, c48), ('+0x50 array', p50, c58)):
    if p and c:
        try:
            first = u32(p)
            r = resolve(first)
            print(f'  {label}: first={first:#x} -> ' + (f'{r[0]:#x} class={r[1]:#x}' if r else 'unresolved'))
        except Exception as e:
            print(f'  {label}: unreadable ({e})')
