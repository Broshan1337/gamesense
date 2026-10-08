import struct

pid = 280956
CLIENT = 0x7fb2a4800000
mem = open(f'/proc/{pid}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]
def u16(a): return struct.unpack('<H', rd(a, 2))[0]

es = u64(CLIENT + 0x4918570)
MAP = es + 0xAA0
node_base = u64(MAP)          # EntityClasses.memory (node array)
num = u16(MAP + 10)           # numElements
print(f'entity class map: nodes={node_base:#x} numElements={num}')

def cstr(p, n=64):
    d = rd(p, n)
    end = d.find(b'\0')
    return d[:end].decode('ascii', 'replace') if end > 0 else ''

# node layout (24B): u16 left, right, parent, type; char* key; CEntityClass* value
entries = {}
for i in range(min(num, 4096)):
    n = node_base + i * 24
    keyp, valp = struct.unpack('<QQ', rd(n + 8, 16))
    if not keyp or not valp: continue
    name = cstr(keyp)
    if name:
        entries[name] = valp
print(f'read {len(entries)} named entries')

weapon_cls = {0x7fb2a8fc4660: 'weapon0', 0x7fb2a8fbe380: 'weapon1'}
for cls, tag in weapon_cls.items():
    names = [k for k, v in entries.items() if v == cls]
    print(f'{tag} classptr {cls:#x} -> map name(s): {names}')

# sample the weapon-ish names present
wnames = sorted(k for k in entries if 'Weapon' in k or 'Knife' in k)[:12]
print('\nweapon-class names in the map (sample):')
for k in wnames:
    print(f'  {k} -> {entries[k]:#x}')
