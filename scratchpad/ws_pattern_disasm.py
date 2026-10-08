import sys, os, struct
sys.path.insert(0, '/home/computer/Desktop/Gamesense-master/scratchpad')
import pattern_forge as pf
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

CUR = '/home/computer/Desktop/Gamesense-master/scratchpad/binaries/2026-10-06-11087116'
client = pf.Module(os.path.join(CUR, 'libclient.so'))
md = Cs(CS_ARCH_X86, CS_MODE_64)

for name, pat in (('Weapons', "8B 57 ? 48 8B 4F ? 8D"),
                  ('ActiveWeapon', "09 D0 89 43 ? 48 85")):
    rx = pf.pattern_regex([None if t == '?' else int(t, 16) for t in pat.split()])
    hits = [client.f2v(m.start()) for m in rx.finditer(client.data)]
    site = hits[0]
    fo = client.v2f(site - 48)
    print(f'===== {name} site {site:#x} =====')
    for ins in md.disasm(client.data[fo:fo + 120], site - 48):
        mark = '  << pattern' if ins.address == site else ''
        print(f'  {ins.address:#x} {ins.mnemonic} {ins.op_str}{mark}')
    print()
