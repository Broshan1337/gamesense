import struct

pid = 280956
CLIENT = 0x7fb2a4800000
mem = open(f'/proc/{pid}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]
def u32(a): return struct.unpack('<I', rd(a, 4))[0]
def f32(a): return struct.unpack('<f', rd(a, 4))[0]

# offsets decoded from the current game binary (pattern read values)
OFF_GSN, OFF_HEALTH, OFF_LIFESTATE = 0x4a0, 0x4bc, 0x4c4
OFF_PAWN_HANDLE, OFF_WEAPONSERVICES = 0x83c, 0x1278
OFF_WEAPONS, OFF_ACTIVEWEAPON = 0x48, 0x60
OFF_ROUNDWINSTATUS = 2468

es = u64(CLIENT + 0x4918570)
ctrl = u64(CLIENT + 0x4903f18)
gr = u64(CLIENT + 0x493abf0)
print(f'entitySystem={es:#x} controller={ctrl:#x} gameRules={gr:#x}')

h = u32(ctrl + OFF_PAWN_HANDLE)
idx, serial = h & 0x7FFF, h >> 15
chunk = u64(es + 0x10 + (idx // 512) * 8)
pawn = 0
if chunk:
    ident = chunk + (idx % 512) * 0x70
    if u32(ident + 0x10) == h:
        pawn = u64(ident)
print(f'pawn handle={h:#x} -> pawn={pawn:#x}')

if pawn:
    health = u32(pawn + OFF_HEALTH)
    lifestate = u8 = rd(pawn + OFF_LIFESTATE, 1)[0]
    gsn = u64(pawn + OFF_GSN)
    print(f'  health={health} lifestate={lifestate} gameSceneNode={gsn:#x}')
    ws = u64(pawn + OFF_WEAPONSERVICES)
    print(f'  weaponServices = {ws:#x}')
    if ws:
        # CUtlVector<CEntityHandle> inline at +0x48: {int size; T* memory; ...}
        wsize, wmem = struct.unpack('<iQ', rd(ws + OFF_WEAPONS, 12))
        print(f'  weapons: size={wsize} memory={wmem:#x}')
        ok = 0
        for i in range(min(wsize, 64)):
            hdl = u32(wmem + i * 4)
            widx = hdl & 0x7FFF
            wchunk = u64(es + 0x10 + (widx // 512) * 8)
            if not wchunk: continue
            wident = wchunk + (widx % 512) * 0x70
            we = u64(wident)
            if we and u32(wident + 0x10) == hdl:
                ok += 1
                if i < 3 or ok <= 3:
                    print(f'    weapon[{i}]: handle={hdl:#x} idx={widx} entity={we:#x} class={u64(wident + 8):#x}')
        print(f'  weapons resolving cleanly: {ok}/{min(wsize, 64)}')
        aw = u32(ws + OFF_ACTIVEWEAPON)
        print(f'  activeWeapon handle = {aw:#x} (idx {aw & 0x7FFF})')

if gr:
    rws = u32(gr + OFF_ROUNDWINSTATUS)
    print(f'\ngameRules roundWinStatus(+{OFF_ROUNDWINSTATUS:#x}) = {rws} (0 = None = round active/playing)')
