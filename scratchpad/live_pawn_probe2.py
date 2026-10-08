import struct

PID = 206011
CLIENT = 0x7f8c24800000
CTRL = 0x53f78099400
ES = 0x53eff9f6800

mem = open(f'/proc/{PID}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]
def u32(a): return struct.unpack('<I', rd(a, 4))[0]

print('all 64 chunk pointers:')
allocated = {}
for k in range(64):
    c = u64(ES + 0x10 + k * 8)
    if c:
        allocated[k] = c
        print(f'  chunk{k:02d} = {c:#x}')
print(f'allocated chunks: {sorted(allocated)}')

# walk every allocated chunk, dump identities with their first 0x18 bytes
print('\nidentity details (chunk0 + any chunk holding the pawn idx 649):')
for k in sorted(allocated):
    if k not in (0, 649 // 64) and k > 1: continue
    c = allocated[k]
    for i in range(64):
        ident = c + i * 0x70
        e = u64(ident)
        if not e: continue
        raw = rd(ident, 0x20)
        print(f'  [{k*64+i:3d}] entity={e:#x} raw: {raw.hex(" ")}')

# the pawn handle from the controller
val = u32(CTRL + 0x83C)
print(f'\nm_hPawn = {val:#x} idx={val & 0x3FFF} serial={val >> 14:#x}')
idx = val & 0x3FFF
ck = allocated.get(idx // 64)
if ck:
    ident = ck + (idx % 64) * 0x70
    print(f'identity@{ident:#x}: entity={u64(ident):#x} raw={rd(ident, 0x20).hex(" ")}')
else:
    print(f'chunk {idx//64} NOT ALLOCATED -> pawn() = null -> every pawn-gated feature dead')
