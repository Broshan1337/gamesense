#!/usr/bin/env python3
"""Fast lea rip-relative xref scan + the three re-derivations."""
import struct
from disasm import C, P, text_blob, disas, func_start

def lea_xrefs(elf, target_va):
    """all `lea r64,[rip+disp32]` sites (REX 48/4C/49/4D + 8D + modrm&0xC7==5) hitting target"""
    blob, base = text_blob(elf)
    hits = []
    n = len(blob) - 7
    i = blob.find(b'\x8D')
    while 0 <= i < n:
        if i >= 1 and blob[i-1] in (0x48, 0x4C, 0x49, 0x4D) and (blob[i+1] & 0xC7) == 0x05:
            d = struct.unpack_from('<i', blob, i+2)[0]
            site = base + i - 1
            if site + 7 + d == target_va:
                hits.append(site)
        i = blob.find(b'\x8D', i + 1)
    return hits

def show(elf, va, title):
    print(f"--- {title} @ 0x{va:X} ---")
    fs = func_start(elf, va)
    print(f"func_start guess: 0x{fs:X}" if fs else "no prologue found")
    print(disas(elf, va, 25))

if __name__ == '__main__':
    print("=== GameEventManagerGlobalPointer ===")
    rs_va = 0xB7203B  # "round_start"
    x = lea_xrefs(C, rs_va)
    print(f"'round_start' lea-xrefs: {[hex(h) for h in x]}")
    for h in x:
        fs = func_start(C, h)
        print(f"\nxref site 0x{h:X}, func_start 0x{fs:X}" if fs else f"\nxref site 0x{h:X}, no func_start")
        if fs:
            print(disas(C, fs, 30))

    print("\n=== RunScriptFunctionPointer ===")
    for needle, sva in [('compile+run', 0x1C5828), ('pre-compiled', 0x1BF898)]:
        x = lea_xrefs(P, sva)
        print(f"{needle!r} lea-xrefs: {[hex(h) for h in x]}")
        for h in x:
            fs = func_start(P, h)
            print(f"  site 0x{h:X} -> func_start 0x{fs:X}" if fs else f"  site 0x{h:X} -> ?")
