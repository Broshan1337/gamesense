import struct, sys, re

PID = 206011
CLIENT = 0x7f8c24800000  # libclient.so base from the Integrity log line

mem = open(f'/proc/{PID}/mem', 'rb', 0)

def rd(addr, n):
    mem.seek(addr)
    return mem.read(n)

def u64(addr):
    return struct.unpack('<Q', rd(addr, 8))[0]

def u32(addr):
    return struct.unpack('<I', rd(addr, 4))[0]

# 1) the globals our patterns resolve (values from pattern_manifest.json, build 11087116)
GLOBALS = {
    'EntitySystemPointer': 0x4918570,
    'GameEventManagerGlobalPointer': 0x4935258,
    'CSGOInputPointer': 0x4951ac0,
    'GameRulesPointer': 0x493abf0,
    'LocalPlayerControllerPointer': 0x4903f18,
    'PointerToClientMode': 0x49162a2 - 0x3777 + 0x3780 if False else None,
}
for name, off in GLOBALS.items():
    if off is None: continue
    v = u64(CLIENT + off)
    print(f'{name:32s} [{CLIENT+off:#x}] = {v:#x}')

es = u64(CLIENT + 0x4918570)
print(f'\nentitySystem = {es:#x}')
if es:
    # chunk array inline at +0x10 (10-03 FIX #2): chunks[k] = es+0x10+k*8
    for k in range(4):
        chunk = u64(es + 0x10 + k * 8)
        print(f'  chunk{k} = {chunk:#x}')
        if chunk:
            # CEntityIdentity stride 0x70; class ptr lives on the IDENTITY at +8
            for i in range(6):
                ident = chunk + i * 0x70
                entity = u64(ident)
                if not entity: continue
                cls = u64(ident + 8)
                handle = u32(ident + 0)  # handle/index guess
                print(f'    [{k*64+i}] entity={entity:#x} classptr={cls:#x} first8={handle:#x}')
                if cls:
                    # vtable of the class -> typeinfo -> name
                    vt = u64(cls)
                    if vt > 0x10000:
                        ti = struct.unpack('<q', rd(vt - 8, 8))[0]  # typeinfo ptr is usually vt-8 as absolute or offset
                        print(f'      class vtbl={vt:#x} (ti disp={ti})')
    # also the OLD-style member deref for comparison: *(es+16) if the member is the array itself
    print(f'  [es+0x10] first qword = {u64(es + 0x10):#x}')
