#!/usr/bin/env python3
"""Callers of the ranking-block accessors + full disasm of the known reader."""
import sys
sys.path.insert(0, 'scratchpad')
import capstone
from disasm import C, disas, func_start, text_blob, md

ACCESSORS = [0x178E4A0, 0x178E550]

blob, base = text_blob(C)
# scan E8/E9 rel32 call sites
def call_sites(target):
    hits = []
    i = 0
    n = len(blob) - 5
    while i < n:
        if blob[i] == 0xE8:
            d = int.from_bytes(blob[i+1:i+5], 'little', signed=True)
            if base + i + 5 + d == target:
                hits.append(base + i)
        i += 1
    return hits

for acc in ACCESSORS:
    sites = call_sites(acc)
    print(f"accessor 0x{acc:X}: {len(sites)} call sites")
    callers = {}
    for s in sites:
        fs = func_start(C, s)
        callers.setdefault(fs if fs else 0, []).append(s)
    for fs, ss in sorted(callers.items()):
        print(f"  caller 0x{fs:X} sites {[hex(x) for x in ss]}")

print("\n===== reader sub_1ED8280 full =====")
print(disas(C, 0x1ED8280, 300))
