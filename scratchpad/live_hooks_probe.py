import struct

PID = 280956
BASE = 0x7fb25f90c000      # our module (libMangoHud.so)
CLIENT = 0x7fb2a4800000    # libclient.so

mem = open(f'/proc/{PID}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]

def in_module(a):
    return BASE <= a < BASE + 0x4000000
def in_client(a):
    return CLIENT <= a < CLIENT + 0x5000000

# 1) ViewRender hook check: global at client+0x4943710 -> object -> vptr -> slot 4
vr_global = u64(CLIENT + 0x4943710)
print(f'[client+0x4943710] ViewRenderPointer value = {vr_global:#x}')
vr = u64(vr_global) if vr_global else 0
print(f'  *global (the ViewRender object) = {vr:#x}')
if vr:
    vptr = u64(vr)
    print(f'  vptr = {vptr:#x} (in {"MODULE" if in_module(vptr) else "client" if in_client(vptr) else "other"})')
    for slot in (2, 3, 4, 5):
        fn = u64(vptr + slot * 8)
        loc = 'MODULE(hooked!)' if in_module(fn) else ('client' if in_client(fn) else 'other')
        print(f'  slot {slot}: {fn:#x} -> {loc}')

# 2) Source2Client FSN hook (slot 36) — PointerToClientMode not needed; Source2Client object:
# our hook per memory: CSource2Client slot 18/36. Get the client interface global:
# Source2Client global: pattern Source2ClientPointer? not in pool. Skip; check CSGOInput instead.
inp_global = u64(CLIENT + 0x4951ac0)
inp = u64(inp_global) if inp_global else 0
if inp:
    vptr = u64(inp)
    print(f'\nCSGOInput object {inp:#x}, vptr {vptr:#x} ({"MODULE" if in_module(vptr) else "client" if in_client(vptr) else "other"})')
    # hooked slots per memory: input 26/6/7
    for slot in (6, 7, 26):
        fn = u64(vptr + slot * 8)
        loc = 'MODULE(hooked!)' if in_module(fn) else ('client' if in_client(fn) else 'other')
        print(f'  slot {slot}: {fn:#x} -> {loc}')
