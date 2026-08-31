#!/usr/bin/env python3
"""Crash diagnosis for SIGSEGV in Aimbot::hitchanceFraction -> spreadFn call.
Checks where the spread-related patterns match in the CURRENT libclient.so and
disassembles the crash frame libclient+0x1af9b2d."""
import re
import sys
sys.path.insert(0, '/path/to/gamesense/scratchpad')
from elfmap import Elf
import capstone

CLIENT_SO = '/mnt/disk2/SteamLibrary/steamapps/common/Counter-Strike Global Offensive/game/csgo/bin/linuxsteamrt64/libclient.so'
C = Elf(CLIENT_SO)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

s = C.by_name['.text']
blob = C.data[s['off']:s['off'] + s['size']]
base = s['addr']
print(f".text va=0x{base:X} size=0x{s['size']:X}")

def find_all(pat_str):
    toks = pat_str.split()
    rx = b''.join(b'.' if t == '?' else re.escape(bytes([int(t, 16)])) for t in toks)
    return [m.start() for m in re.finditer(rx, blob, re.DOTALL)]

patterns = {
    'CalculateSpread': "55 48 89 E5 41 57 41 56 41 89 CE 41 55 41 54 41 89 FC 53 4C 89 C3 48 83 EC 58 89 75 A8 BE FF FF FF FF 66 89 7D A0 48 8D 3D ? ? ? ?",
    'SpreadSeed':      "55 89 D0 48 89 E5 53 48 81 EC D8 00 00 00 F3 0F 10 06 0F 2F 05 ? ? ? ? F3 0F 10 1D ? ? ? ? 72 ? 0F 2F D8",
    'GetInaccuracy':   "55 48 89 E5 41 57 41 56 49 89 ? 41 55 49 89 ? 41 54 53 48 89 FB 48 83 EC ? E8",
    'GetSpread':       "55 48 89 E5 48 83 EC ? 48 63",
    'UpdateAccPenalty':"55 48 89 E5 41 55 41 54 53 48 89 FB 48 83 EC 18 E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 89 C7 49 89 C4 E8",
}
for name, pat in patterns.items():
    hits = find_all(pat)
    vas = ', '.join(f"0x{base + h:X} (.text+0x{h:X})" for h in hits) or 'NONE'
    print(f"{name}: {len(hits)} match(es) at {vas}")

CRASH_VA = 0x1AF9B2D
FUNC_VA = 0x1AF96C0
off = C.va_to_off(FUNC_VA)
code = C.data[off:off + 0x600]
print(f"--- disassembly of CalculateSpread func 0x{FUNC_VA:X} (crash at +0x{CRASH_VA-FUNC_VA:X}) ---")
for insn in md.disasm(code, FUNC_VA):
    marker = '   <<<< CRASH PC' if insn.address == CRASH_VA else ''
    print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}{marker}")
    if insn.address > CRASH_VA:
        break
