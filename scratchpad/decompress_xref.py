#!/usr/bin/env python3
"""Find GOT relocs for given symbol names in a DSO, then xref call sites via .text scan.

Usage: python3 scratchpad/decompress_xref.py <module.so> <symbol> [<symbol> ...]
1. Parses .dynsym/.dynstr/.rela.dyn/.rela.plt directly (no readelf truncation issues).
2. For each GOT slot found, scans .text for `call/jmp [rip+disp]` referencing the slot
   and for PLT stubs `bnd jmp [rip+disp]` referencing it, then re-scans for `call <stub>`.
3. Prints every reference site with the nearest preceding function prologue.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfmap import Elf  # noqa: E402


def dyn_symbols(elf):
    """-> {name: sym_index} from .dynsym/.dynstr."""
    dynsym = elf.by_name.get('.dynsym')
    dynstr = elf.by_name.get('.dynstr')
    if not dynsym or not dynstr:
        return {}, {}
    syms = {}
    names = []
    count = dynsym['size'] // 24  # Elf64_Sym
    for i in range(count):
        o = dynsym['off'] + i * 24
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from('<IBBHQQ', elf.data, o)
        if st_name:
            n_off = dynstr['off'] + st_name
            n_end = elf.data.index(b'\x00', n_off)
            name = elf.data[n_off:n_end].decode('utf-8', 'replace')
            syms[name] = i
            names.append((i, name))
    return syms, names


def relocs_for_symbols(elf, wanted_indexes):
    """-> list of (got_va, sym_index, type) for R_X86_64_JUMP_SLOT/GLOB_DAT of wanted symbols."""
    out = []
    for sec_name in ('.rela.dyn', '.rela.plt'):
        sec = elf.by_name.get(sec_name)
        if not sec:
            continue
        count = sec['size'] // 24
        for i in range(count):
            o = sec['off'] + i * 24
            r_offset, r_info, r_addend = struct.unpack_from('<QQq', elf.data, o)
            sym_idx = r_info >> 32
            rtype = r_info & 0xFFFFFFFF
            if sym_idx in wanted_indexes and rtype in (6, 7):  # GLOB_DAT, JUMP_SLOT
                out.append((r_offset, sym_idx, rtype))
    return out


def find_rip_refs(elf, target_va):
    """Scan .text for `call/jmp [rip+disp32]` and `lea disp32` hitting target_va."""
    text = elf.by_name['.text']
    blob = elf.data[text['off']:text['off'] + text['size']]
    hits = []
    n = len(blob)
    # FF 15 = call [rip+disp32], FF 25 = jmp [rip+disp32], 48 8B 05 = mov rax,[rip+disp32],
    # FF D3-ish register calls excluded. Scan every byte position for the three opcode shapes.
    for i in range(n - 6):
        if blob[i] == 0xFF and blob[i + 1] == 0x15:  # call [rip+d32]
            disp = struct.unpack_from('<i', blob, i + 2)[0]
            va = text['addr'] + i
            if va + 6 + disp == target_va:
                hits.append((va, 'call [rip]'))
        elif blob[i] == 0xFF and blob[i + 1] == 0x25:  # jmp [rip+d32]
            disp = struct.unpack_from('<i', blob, i + 2)[0]
            va = text['addr'] + i
            if va + 6 + disp == target_va:
                hits.append((va, 'jmp [rip]'))
        elif blob[i] == 0x48 and blob[i + 1] == 0x8B and (blob[i + 2] & 0xC7) == 0x05:  # mov r64,[rip+d32]
            disp = struct.unpack_from('<i', blob, i + 3)[0]
            va = text['addr'] + i
            if va + 7 + disp == target_va:
                hits.append((va, 'mov r,[rip]'))
    return hits


def find_plt_stub(elf, got_va):
    """Scan .plt/.plt.sec/.plt.got for `bnd jmp *got(%rip)` (FF 25 disp32) -> stub VA."""
    stubs = []
    for sec_name in ('.plt', '.plt.sec', '.plt.got'):
        sec = elf.by_name.get(sec_name)
        if not sec:
            continue
        blob = elf.data[sec['off']:sec['off'] + sec['size']]
        for i in range(len(blob) - 6):
            if blob[i] == 0xFF and blob[i + 1] == 0x25:
                disp = struct.unpack_from('<i', blob, i + 2)[0]
                va = sec['addr'] + i
                if va + 6 + disp == got_va:
                    stubs.append((sec_name, va))
    return stubs


def find_rel32_refs(elf, target_va, opcodes=(0xE8, 0xE9)):
    """Scan .text for E8/E9 rel32 call/jmp targeting target_va."""
    text = elf.by_name['.text']
    blob = elf.data[text['off']:text['off'] + text['size']]
    hits = []
    n = len(blob)
    for i in range(n - 5):
        if blob[i] in opcodes:
            rel = struct.unpack_from('<i', blob, i + 1)[0]
            va = text['addr'] + i
            if va + 5 + rel == target_va:
                hits.append((va, 'call rel32' if blob[i] == 0xE8 else 'jmp rel32'))
    return hits


def find_got_address_loads(elf, target_va):
    """Scan .text for mov/lea r64,[rip+disp32] loading the GOT slot's ADDRESS (indirect dispatch)."""
    text = elf.by_name['.text']
    blob = elf.data[text['off']:text['off'] + text['size']]
    hits = []
    n = len(blob)
    for i in range(n - 7):
        b0, b1, b2 = blob[i], blob[i + 1], blob[i + 2]
        is_lea = (b0 & 0xF8) == 0x48 and b1 == 0x8D and (b2 & 0xC7) == 0x05
        is_mov = (b0 & 0xF8) == 0x48 and b1 == 0x8B and (b2 & 0xC7) == 0x05
        if not (is_lea or is_mov):
            continue
        disp_len = 4
        disp = struct.unpack_from('<i', blob, i + 3)[0]
        va = text['addr'] + i
        if va + 3 + disp_len + disp == target_va:
            hits.append((va, 'lea r,[rip]' if is_lea else 'mov r,[rip]'))
    return hits


def main():
    path = sys.argv[1]
    wanted_names = sys.argv[2:]
    elf = Elf(path)
    syms, _ = dyn_symbols(elf)
    wanted = {name: syms[name] for name in wanted_names if name in syms}
    for name in wanted_names:
        if name not in syms:
            print(f"[-] {name}: not in .dynsym of {path.split('/')[-1]}")
    if not wanted:
        return
    for name, idx in wanted.items():
        print(f"[+] {name}: dynsym index {idx}")

    got_slots = relocs_for_symbols(elf, set(wanted.values()))
    name_by_idx = {v: k for k, v in wanted.items()}
    for got_va, idx, rtype in got_slots:
        kind = 'GLOB_DAT' if rtype == 6 else 'JUMP_SLOT'
        print(f"[+] {name_by_idx[idx]} GOT slot @ 0x{got_va:X} ({kind})")
        refs = find_rip_refs(elf, got_va)
        for va, kind_ref in refs:
            print(f"      ref @ 0x{va:X} ({kind_ref})")
        stubs = find_plt_stub(elf, got_va)
        call_targets = set()
        for sec_name, ff25_va in stubs:
            # .plt entries here are `mov idx,%r11d (6) + jmp *GOT(%rip) (6) + int3 pad`; with IBT
            # layouts it is `endbr64 (4) + bnd jmp *GOT`. Callers target the entry START - try all
            # realistic back-offsets.
            call_targets.update({ff25_va, ff25_va - 4, ff25_va - 6})
            print(f"      PLT stub [{sec_name}] @ 0x{ff25_va:X} (entry candidates 0x{ff25_va:X}/0x{ff25_va - 4:X}/0x{ff25_va - 6:X})")
        for target in sorted(call_targets):
            for va, kind_ref in find_rel32_refs(elf, target):
                print(f"      call-site @ 0x{va:X} ({kind_ref}) -> 0x{target:X}")
        # Indirect dispatch: GOT slot address-loaded into a register (mov/lea r64,[rip+d32])
        got_addr_refs = find_got_address_loads(elf, got_va)
        for va, kind_ref in got_addr_refs:
            print(f"      GOT-loaded @ 0x{va:X} ({kind_ref})")
        if not refs and not stubs and not got_addr_refs:
            print("      (unreferenced: no rip-refs, no PLT stub, no GOT loads)")


if __name__ == '__main__':
    main()
