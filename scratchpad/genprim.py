#!/usr/bin/env python3
"""Hunt the Linux generate_primitives dispatcher in libscenesystem.so."""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import struct
import capstone
from elfmap import Elf

SO = '/mnt/disk2/SteamLibrary/steamapps/common/Counter-Strike Global Offensive/game/bin/linuxsteamrt64/libscenesystem.so'
S = Elf(SO)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = False

def text_blob(elf):
    s = elf.by_name['.text']
    return elf.data[s['off']:s['off']+s['size']], s['addr']

def disas_range(elf, va_start, va_end):
    off = elf.va_to_off(va_start)
    code = elf.data[off:off+(va_end-va_start)]
    out = []
    for insn in md.disasm(code, va_start):
        out.append(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
    return "\n".join(out)

def disas_n(elf, va, count=80):
    off = elf.va_to_off(va)
    code = elf.data[off:off+count*16]
    out = []
    for insn in md.disasm(code, va):
        out.append(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
        if len(out) >= count:
            break
    return "\n".join(out)

def func_start(elf, va, maxback=0x20000):
    blob, base = text_blob(elf)
    off = va - base
    # common linux prologues: push rbp / endbr64-ish; try several
    pats = [b'\x55\x48\x89\xE5', b'\xF3\x0F\x1E\xFA', b'\x48\x83\xEC']
    best = None
    for p in pats:
        i = blob.rfind(p, max(0, off - maxback), off)
        while i >= 0:
            seg = blob[i:off]
            if b'\xC3' not in seg[:-5]:
                cand = base + i
                if best is None or cand > best:
                    best = cand
                break
            i = blob.rfind(p, max(0, i - maxback), i)
    return best

def lea_xrefs(elf, target_va):
    blob, base = text_blob(elf)
    hits = []
    n = len(blob) - 7
    i = blob.find(b'\x8D')
    while 0 <= i < n:
        if blob[i-1] in (0x48, 0x4C, 0x49, 0x4D) and (blob[i+1] & 0xC7) == 0x05:
            d = struct.unpack_from('<i', blob, i+2)[0]
            site = base + i - 1
            if site + 7 + d == target_va:
                hits.append(site)
        i = blob.find(b'\x8D', i + 1)
    return hits

if __name__ == '__main__':
    print("=== string anchor ===")
    offs = S.find_string_offsets('Cannot generate primitives')
    for o in offs:
        va = S.off_to_va(o)
        print(f"str fileoff 0x{o:X} -> VA 0x{va:X} : {S.cstr_at_off(o)!r}")
        x = lea_xrefs(S, va)
        print(f"lea-xrefs: {[hex(h) for h in x]}")
        for h in x:
            fs = func_start(S, h)
            print(f"site 0x{h:X} func_start guess: 0x{fs:X}" if fs else f"site 0x{h:X}: no prologue")
