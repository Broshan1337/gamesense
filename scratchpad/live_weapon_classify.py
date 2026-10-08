import struct

pid = 280956
CLIENT = 0x7fb2a4800000
mem = open(f'/proc/{pid}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]
def u32(a): return struct.unpack('<I', rd(a, 4))[0]

# module base (instrumented build, injected earlier - check maps)
BASE = None
for line in open(f'/proc/{pid}/maps'):
    if 'memfd:libMangoHud' in line and ' r-xp ' in line:
        BASE = int(line.split('-')[0], 16); break
print(f'module base = {BASE:#x}' if BASE else 'MODULE NOT INJECTED - vanilla game')

es = u64(CLIENT + 0x4918570)

# locate the local pawn via controller
ctrl = u64(CLIENT + 0x4903f18)
h = u32(ctrl + 0x83c)
idx = h & 0x7FFF
chunk = u64(es + 0x10 + (idx // 512) * 8)
pawn = u64(chunk + (idx % 512) * 0x70)
ws = u64(pawn + 0x1278)
wsize, = struct.unpack('<i', rd(ws + 0x48, 4))
wmem = u64(ws + 0x50)
print(f'pawn={pawn:#x} ws={ws:#x} weapons size={wsize}')

weapons = []
for i in range(max(0, min(wsize, 32))):
    hdl = u32(wmem + i * 4)
    widx = hdl & 0x7FFF
    wchunk = u64(es + 0x10 + (widx // 512) * 8)
    if not wchunk: continue
    wident = wchunk + (widx % 512) * 0x70
    we = u64(wident)
    if we and u32(wident + 0x10) == hdl:
        weapons.append((i, hdl, we, u64(wident + 8)))
for i, hdl, we, cls in weapons:
    print(f'  weapon[{i}] handle={hdl:#x} entity={we:#x} classptr={cls:#x}')

if BASE:
    # fresh GlobalContext offsets from the CURRENT build
    import subprocess
    out = subprocess.run(['nm', '/home/computer/Desktop/Gamesense-master/cs2/build-dbg/Source/libMangoHud.so'],
                         capture_output=True, text=True).stdout
    import re
    def sym(needle):
        for line in out.splitlines():
            if needle in line:
                return int(line.split()[0], 16)
        return None
    gc_off = sym('_ZN13GlobalContext13globalContextE')
    gc = u64(BASE + gc_off)
    FULL = gc + 24
    classifier = FULL + 7224
    classes = struct.unpack('<52Q', rd(classifier, 52 * 8))
    print(f'\nclassifier filled: {sum(1 for c in classes if c)}/52')
    for i, hdl, we, cls in weapons:
        slot = [j for j, c in enumerate(classes) if c == cls]
        print(f'  weapon[{i}] classptr {cls:#x} -> classifier slot {slot if slot else "NOT IN CLASSIFIER"}')
    # also the pawn
    pcls = u64(es and 0)  # placeholder
