import struct

PID = 206011
CTRL = 0x53f78099400
ES = 0x53eff9f6800

mem = open(f'/proc/{PID}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]
def u32(a): return struct.unpack('<I', rd(a, 4))[0]

chunks = {k: u64(ES + 0x10 + k * 8) for k in range(64)}
chunks = {k: v for k, v in chunks.items() if v}
print('allocated chunks:', {k: hex(v) for k, v in chunks.items()})

m_hPawn = u32(CTRL + 0x83C)
print(f'm_hPawn = {m_hPawn:#x}')

# scan EVERY allocated chunk for an identity whose +0x10 handle == m_hPawn
found = []
for k, c in chunks.items():
    for i in range(512):
        ident = c + i * 0x70
        e = u64(ident)
        if not e: continue
        h = u32(ident + 0x10)
        if h == m_hPawn:
            found.append((k, i, ident, e))
for k, i, ident, e in found:
    print(f'MATCH: chunk{k} slot{i} (global idx {k*512+i}) ident={ident:#x} entity={e:#x}')
    print(f'  raw: {rd(ident, 0x20).hex(" ")}')
    # entity class ptr via identity+8
    print(f'  classptr={u64(ident + 8):#x}')

# also list what chunk1 actually holds (first 20 populated)
c = chunks.get(1)
if c:
    print('\nchunk1 populated identities (first 20):')
    n = 0
    for i in range(512):
        ident = c + i * 0x70
        e = u64(ident)
        if not e: continue
        n += 1
        if n <= 20:
            print(f'  idx {i}: entity={e:#x} handle={u32(ident+0x10):#x} classptr={u64(ident+8):#x}')
    print(f'  chunk1 total populated: {n}')
