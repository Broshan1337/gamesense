#!/usr/bin/env python3
"""Find the live PlayerRankingDataPointer pattern site + all flag tests around it."""
import sys
sys.path.insert(0, 'scratchpad')
import capstone
from disasm import C, text_blob, md, disas, func_start

blob, base = text_blob(C)
pat = bytes.fromhex('488d1d')  # lea r64, [rip+x] candidates followed by F6 43 10 04 0F 85

# find exact pattern: 48 8D 1D disp32 | F6 43 10 04 | 0F 85
sites = []
i = 0
n = len(blob) - 12
while i < n:
    if blob[i] == 0x48 and blob[i+1] == 0x8D and blob[i+2] == 0x1D \
       and blob[i+7] == 0xF6 and blob[i+8] == 0x43 and blob[i+9] == 0x10 and blob[i+10] == 0x04 \
       and blob[i+11] == 0x0F and blob[i+12] == 0x85:
        d = int.from_bytes(blob[i+3:i+7], 'little', signed=True)
        target = base + i + 7 + d
        sites.append((base + i, target))
    i += 1
print(f"pattern sites: {[(hex(s), hex(t)) for s, t in sites]}")

for s, t in sites:
    fs = func_start(C, s)
    print(f"\n===== site 0x{s:X} -> block 0x{t:X}, func 0x{fs:X} =====")
    if fs:
        print(disas(C, fs, 120))
