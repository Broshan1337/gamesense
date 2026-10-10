#!/usr/bin/env python3
"""Scene-object owner-handle census (world-modulation RE): scan the heap for
CSceneObject instances (vtable ap 0xa198c0 on libscenesystem 11106093) and
dump the +0xC0 field distribution - the world-vs-entity-owner discriminator
the WorldColors recolorWorld filter relies on. Also dumps weapon entity
handles for correlation. Run with the game IN A MATCH."""
import struct, sys, collections

PID = int(sys.argv[1]) if len(sys.argv) > 1 else None
if not PID:
    import subprocess
    PID = int(subprocess.run(['pgrep', '-f', r'linuxsteamrt64/cs2$'], capture_output=True, text=True).stdout.split()[0])

mem = open(f'/proc/{PID}/mem', 'rb', 0)
def rd(a, n):
    if not (0 < a < 0x7fffffffffff): return b''
    try:
        mem.seek(a); return mem.read(n)
    except OSError: return b''
def u64(a):
    b = rd(a, 8); return struct.unpack('<Q', b)[0] if len(b) == 8 else 0
def u32(a):
    b = rd(a, 4); return struct.unpack('<I', b)[0] if len(b) == 4 else 0
def u16(a):
    b = rd(a, 2); return struct.unpack('<H', b)[0] if len(b) == 2 else 0
def cstr(a, n=96):
    s = rd(a, n)
    if not s: return None
    e = s.find(b'\0')
    if e < 1: return None
    t = s[:e].decode('latin1')
    return t if t.isprintable() and len(t) < 80 else None

maps = open(f'/proc/{PID}/maps').readlines()
def base_of(soname):
    for l in maps:
        if soname in l and 'r--p' in l and l.split()[2] == '00000000':
            return int(l.split('-')[0], 16)
    return None

CLIENT = base_of('libclient.so')
SCENE = base_of('libscenesystem.so')
print(f'client={CLIENT:#x} scene={SCENE:#x}')
if not CLIENT or not SCENE:
    sys.exit('module bases not found')

# 1) entity class map + weapon/pawn handles
es = u64(CLIENT + 0x4910af0)
memory = u64(es + 0xAA0); num = u16(es + 0xAA0 + 10)
classes = {}
for i in range(min(num, 4096)):
    node = memory + i*24
    k = cstr(u64(node + 8)); v = u64(node + 16)
    if k: classes[k] = v
rev = {v: k for k, v in classes.items()}
weapon_handles = {}
entity_handles = collections.Counter()
for k in range(32):
    chunk = u64(es + 0x10 + k*8)
    if not chunk: continue
    for i in range(64):
        ident = chunk + i*0x70
        e = u64(ident)
        if not e: continue
        n = rev.get(u64(ident + 8), '?')
        # entity handle: identity carries it - read the u32 near the start
        for hoff in (0x10, 0x14, 0x18):
            h = u32(ident + hoff)
            if h and (h & 0x3FFF) == (k*64 + i):
                entity_handles[(n, h)] += 1
                if 'Weapon' in n or 'AK47' in n or 'Knife' in n:
                    weapon_handles.setdefault((n, h), e)
                break
print('sample weapon handles:', [f'{n}={h:#x}' for (n, h) in list(weapon_handles)[:10]])

# 2) heap census: CSceneObject vptr = SCENE + 0xa198c0 (11106093)
VT = SCENE + 0xa198c0
vtb = struct.pack('<Q', VT)
regions = []
for l in maps:
    parts = l.split()
    if len(parts) < 5 or 'rw' not in parts[1]: continue
    if '/memfd:' in l or '[heap]' in l or '/libclient' in l or '/libscenesystem' in l or '/libengine' in l or '/libpanorama' in l or 'anon' in l or len(parts) < 6:
        lo, hi = parts[0].split('-')
        path = parts[-1]
        if path.startswith('/') or '[' in path: continue
        regions.append((int(lo, 16), int(hi, 16)))
owners = collections.Counter()
total = 0
samples = []
for lo, hi in regions:
    if hi - lo > 1 << 30: continue
    blob = rd(lo, min(hi - lo, 64 << 20))
    if not blob: continue
    i = 0
    while True:
        i = blob.find(vtb, i)
        if i < 0: break
        obj = lo + i
        total += 1
        own = u32(obj + 0xC0)
        owners[own] += 1
        if len(samples) < 30: samples.append((obj, own))
        i += 8
print(f'CSceneObject instances: {total}')
print('owner (+0xC0) distribution:')
for own, c in owners.most_common(20):
    print(f'  {own:#010x}: {c}')
world = owners.get(0xFFFFFFFF, 0)
print(f'world (0xFFFFFFFF): {world}  owned-ish: {total - world}')
# correlate: weapon scene objects?
wh = set(h for (n, h) in weapon_handles)
hits = [(obj, own) for obj, own in samples if own in wh]
print(f'weapon-handle matches in first {len(samples)} samples: {len(hits)}')