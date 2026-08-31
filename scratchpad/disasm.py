#!/usr/bin/env python3
"""Disassembly helpers + targeted re-derivation of the 3 broken patterns."""
import struct
import capstone
from elfmap import Elf
from ws import find_all

CLIENT_SO = '/mnt/disk2/SteamLibrary/steamapps/common/Counter-Strike Global Offensive/game/csgo/bin/linuxsteamrt64/libclient.so'
PANORAMA_SO = '/mnt/disk2/SteamLibrary/steamapps/common/Counter-Strike Global Offensive/game/bin/linuxsteamrt64/libpanorama.so'

C = Elf(CLIENT_SO)
P = Elf(PANORAMA_SO)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = False

def text_blob(elf):
    s = elf.by_name['.text']
    return elf.data[s['off']:s['off']+s['size']], s['addr']

def disas(elf, va, count=60):
    off = elf.va_to_off(va)
    code = elf.data[off:off+count*16]
    out = []
    for insn in md.disasm(code, va):
        out.append(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
        if insn.mnemonic == 'ret' and len(out) > 4:
            break
        if len(out) >= count:
            break
    return "\n".join(out)

def func_start(elf, va, maxback=0x4000):
    """nearest preceding '55 48 89 E5' prologue in .text"""
    blob, base = text_blob(elf)
    off = va - base
    i = blob.rfind(b'\x55\x48\x89\xE5', max(0, off - maxback), off)
    if i < 0:
        return None
    # ensure no ret/jmp between prologue and va (crude)
    seg = blob[i:off]
    return base + i if b'\xC3' not in seg[:-5] else None

def rip_lea_xrefs(elf, target_va):
    """find `lea r64,[rip+disp]` sites whose target == target_va, via linear disasm of .text"""
    blob, base = text_blob(elf)
    hits = []
    for insn in md.disasm(blob, base):
        if insn.mnemonic == 'lea' and 'rip' in insn.op_str:
            try:
                t = int(insn.op_str.split('rip ')[1].split(']')[0].replace(' ', ''), 16) + insn.address + insn.size
            except Exception:
                continue
            if t == target_va:
                hits.append(insn.address)
    return hits
