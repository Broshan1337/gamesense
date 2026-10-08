import struct, os

PID = 280956
BASE = 0x7fb256fe7000      # current injection exec base
CLIENT = 0x7fb2a4800000

mem = open(f'/proc/{PID}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]
def u32(a): return struct.unpack('<I', rd(a, 4))[0]

gc = u64(BASE + 0xebc460)
print(f'GlobalContext = {gc:#x}')
FULL = gc + 24
eight = rd(FULL + 1616 + 19 + 49 * 4, 58 * 8)
vals = struct.unpack('<58Q', eight)
def find(client_off, label):
    want = CLIENT + client_off
    for i, v in enumerate(vals):
        if v == want:
            print(f'  {label}: eightByteResults[{i}] = {v:#x} (client+{client_off:#x}) OK')
            return want
    print(f'  {label}: NOT FOUND in results (want client+{client_off:#x})')
    return None

es_g = find(0x4918570, 'EntitySystemPointer global')
lpc_g = find(0x4903f18, 'LocalPlayerControllerPointer global')
gr_g = find(0x493abf0, 'GameRulesPointer global')

es = u64(es_g) if es_g else 0
ctrl = u64(lpc_g) if lpc_g else 0
gr = u64(gr_g) if gr_g else 0
print(f'\nlive: entitySystem={es:#x} controller={ctrl:#x} gameRules={gr:#x}')
print('in-match?' , 'YES' if gr and es else 'no')

if ctrl:
    # m_hPawn at +0x83C (schema, dec 2108)
    h = u32(ctrl + 0x83C)
    idx, serial = h & 0x7FFF, h >> 15
    print(f'controller m_hPawn(+0x83C) = {h:#x} idx={idx} serial={serial:#x}')
    if es:
        chunk = u64(es + 0x10 + (idx // 512) * 8)
        print(f'  chunk[{idx//512}] = {chunk:#x}')
        if chunk:
            ident = chunk + (idx % 512) * 0x70
            e = u64(ident)
            ih = u32(ident + 0x10)
            cls = u64(ident + 8)
            print(f'  identity: entity={e:#x} handle={ih:#x} ({ "MATCH" if ih==h else "MISMATCH" }) classptr={cls:#x}')
            if cls:
                vt = u64(cls)
                print(f'  class vtable={vt:#x} (client+{vt-CLIENT:#x})' if CLIENT <= vt < CLIENT+0x5000000 else f'  class vtable={vt:#x}')
    # where is the pawn in the classifier? compare classptr with classifier slots
    classifier = FULL + 7224
    classes = struct.unpack('<52Q', rd(classifier, 52 * 8))
    nz = [(i, v) for i, v in enumerate(classes) if v]
    print(f'\nclassifier now: {len(nz)}/52 filled')
    if cls:
        matches = [i for i, v in nz if v == cls]
        print(f'pawn classptr matches classifier slot: {matches}')
    # dump a few classptrs of populated identities and see if classifier can name them
    if es:
        seen = {}
        for k in range(3):
            c = u64(es + 0x10 + k * 8)
            if not c: continue
            for i in range(512):
                ident = c + i * 0x70
                e = u64(ident)
                if not e: continue
                cp = u64(ident + 8)
                seen[cp] = seen.get(cp, 0) + 1
        known = {v: i for i, v in nz}
        print(f'\ndistinct live classptrs: {len(seen)}; recognized by classifier: {sum(1 for cp in seen if cp in known)}')
        for cp, n in list(seen.items())[:10]:
            print(f'  {cp:#x} x{n} -> classifier slot {known.get(cp, "NOT-IN-CLASSIFIER")}')
