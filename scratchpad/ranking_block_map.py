#!/usr/bin/env python3
"""Map the full PlayerRankingData block: every lea-xref to unk_4817720 + per-function
flag-gate -> field-offset mapping. Block base = libclient VA 0x4817720 (RVA convention)."""
import sys
sys.path.insert(0, 'scratchpad')
from disasm import C, disas, func_start
from rederive import lea_xrefs

BLOCK = 0x4817720

sites = lea_xrefs(C, BLOCK)
print(f"lea-xrefs to ranking block 0x{BLOCK:X}: {[hex(s) for s in sites]}")
for s in sites:
    fs = func_start(C, s)
    print(f"\n===== xref site 0x{s:X} in func 0x{fs:X} =====" if fs else f"\n===== xref site 0x{s:X} (no prologue) =====")
    if fs:
        print(disas(C, fs, 200))
