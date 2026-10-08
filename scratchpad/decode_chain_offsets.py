import sys, os, struct
sys.path.insert(0, '/home/computer/Desktop/Gamesense-master/scratchpad')
import pattern_forge as pf

CUR = '/home/computer/Desktop/Gamesense-master/scratchpad/binaries/2026-10-06-11087116'
client = pf.Module(os.path.join(CUR, 'libclient.so'))

# decode the runtime-read offset VALUE for each weapon-chain pattern from the game binary
WANT = {'OffsetToWeaponServices', 'OffsetToWeapons', 'OffsetToActiveWeapon',
        'OffsetToOwnerEntity', 'OffsetToGameSceneNode', 'OffsetToHealth',
        'OffsetToLifeState', 'OffsetToBasePawnHandle', 'OffsetToPlayerController'}
for p in pf.extract_patterns():
    if p['type'] not in WANT:
        continue
    toks = p['pattern'].split()
    rx = pf.pattern_regex([None if t == '?' else int(t, 16) for t in toks])
    hits = [m.start() for m in rx.finditer(client.data)]
    if len(hits) != 1:
        print(f"{p['type']}: {len(hits)} hits ?!")
        continue
    fo = hits[0] + p['add']
    raw = client.data[fo:fo + 4]
    val = struct.unpack('<I', raw)[0]
    print(f"{p['type']:28s} = {val} (0x{val:x})  raw={raw.hex()}")
