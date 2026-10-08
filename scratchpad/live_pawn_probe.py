import struct

PID = 206011
CLIENT = 0x7f8c24800000
CTRL = 0x53f78099400   # live local player controller (chunk0[1] + the LocalPlayerControllerPointer global agree)

mem = open(f'/proc/{PID}/mem', 'rb', 0)

def rd(addr, n):
    mem.seek(addr)
    return mem.read(n)

def u64(a): return struct.unpack('<Q', rd(a, 8))[0]
def u32(a): return struct.unpack('<I', rd(a, 4))[0]

print(f'controller = {CTRL:#x}')
# controller vtable sanity
vt = u64(CTRL)
print(f'  vtable = {vt:#x} (client image? {"YES" if CLIENT <= vt < CLIENT + 0x5000000 else "no"})')

# m_hPawn at +2108 (0x83C) per the schema-resolved pawn() chain; m_hPlayerPawn at +2732 (0xAAC)
for name, off in (('m_hPawn(+2108)', 0x83C), ('m_hPlayerPawn(+2732)', 0xAAC)):
    raw = rd(CTRL + off, 4)
    val = struct.unpack('<I', raw)[0]
    idx = val & 0x3FFF
    serial = val >> 14
    print(f'  {name}: raw={val:#x} index={idx} serial={serial:#x}')

# entity lookup for the pawn handle index
es = u64(CLIENT + 0x4918570)
def entity_at(idx):
    chunk = u64(es + 0x10 + (idx // 64) * 8)
    if not chunk: return None
    ident = chunk + (idx % 64) * 0x70
    e = u64(ident)
    if not e: return None
    h = u32(ident)  # the identity's stored handle (first 4 bytes)? verify against index
    cls = u64(ident + 8)
    return e, h, cls

for name, off in (('m_hPawn', 0x83C), ('m_hPlayerPawn', 0xAAC)):
    val = u32(CTRL + off)
    idx = val & 0x3FFF
    r = entity_at(idx)
    if r:
        e, h, cls = r
        print(f'  {name} -> idx {idx}: entity={e:#x} identFirst={h:#x} classptr={cls:#x}')
    else:
        print(f'  {name} -> idx {idx}: NO ENTITY (chunk missing or empty)')

# also dump some nearby controller fields to find the pawn handle empirically:
# scan the controller's first 0x1000 bytes for u32s whose low 14 bits = a populated entity index
es_idx_populated = set()
for k in range(2):
    chunk = u64(es + 0x10 + k * 8)
    if not chunk: continue
    for i in range(64):
        if u64(chunk + i * 0x70):
            es_idx_populated.add(k * 64 + i)
print(f'\npopulated entity indices: {sorted(es_idx_populated)}')
hits = []
for off in range(0, 0x1000, 4):
    val = u32(CTRL + off)
    idx = val & 0x3FFF
    if val and idx in es_idx_populated and (val >> 14) != 0 and (val >> 14) < 0x3FFF:
        hits.append((off, val))
print('controller u32s that look like handles to populated indices:')
for off, val in hits[:15]:
    print(f'  +{off:#x}: {val:#x} (idx {val & 0x3FFF}, serial {val >> 14:#x})')
