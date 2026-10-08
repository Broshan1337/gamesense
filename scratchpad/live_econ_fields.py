import struct

pid = 280956
mem = open(f'/proc/{pid}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u32(a): return struct.unpack('<I', rd(a, 4))[0]
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]

# fallback fields (schema, current build)
OFF_FALLBACK_PK, OFF_FALLBACK_SEED, OFF_FALLBACK_WEAR, OFF_FALLBACK_ST = 10040, 10044, 10048, 10052
OFF_ORIG_LOW, OFF_ORIG_HIGH = 10032, 10036

es = 0x33ecda55000
def weapons_now():
    ctrl = u64(CLIENT + 0x4903f18) if False else None

CLIENT = 0x7fb2a4800000
ctrl = u64(CLIENT + 0x4903f18)
es = u64(CLIENT + 0x4918570)
h = u32(ctrl + 0x83c)
chunk = u64(es + 0x10 + ((h & 0x7FFF) // 512) * 8)
pawn = u64(chunk + ((h & 0x7FFF) % 512) * 0x70)
ws = u64(pawn + 0x1278)
wsize, = struct.unpack('<i', rd(ws + 0x48, 4))
wmem = u64(ws + 0x50)
print(f'pawn={pawn:#x} weapons size={wsize}')
for i in range(min(wsize, 8)):
    hdl = u32(wmem + i * 4)
    widx = hdl & 0x7FFF
    wchunk = u64(es + 0x10 + (widx // 512) * 8)
    if not wchunk: continue
    wident = wchunk + (widx % 512) * 0x70
    we = u64(wident)
    if not we or u32(wident + 0x10) != hdl: continue
    pk = u32(we + OFF_FALLBACK_PK)
    seed = u32(we + OFF_FALLBACK_SEED)
    wear = struct.unpack('<f', rd(we + OFF_FALLBACK_WEAR, 4))[0]
    st = u32(we + OFF_FALLBACK_ST)
    origlow = u32(we + OFF_ORIG_LOW)
    item = we + 0x1218 + 80
    defidx = u32(item + 4290)
    print(f'  weapon[{i}] entity={we:#x}: fallbackPK={pk} seed={seed} wear={wear:.4f} statTrak={st} origLow={origlow} defidx@+4290={defidx}')
