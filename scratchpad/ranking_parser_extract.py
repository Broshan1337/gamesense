#!/usr/bin/env python3
"""Extract field stores + flag-bit manipulations from the ranking-block parser sub_1ED8280."""
import sys
sys.path.insert(0, 'scratchpad')
import capstone
from disasm import C, text_blob, md

blob, base = text_blob(C)

def disasm_range(va, count):
    off = C.va_to_off(va)
    code = blob[off - base: off - base + count * 16]
    return list(md.disasm(code, va))

insns = disasm_range(0x1ED8280, 4000)

# function length: stop at a ret that ends a long run without intervening calls (crude: print
# stats instead)
stores = {}   # (base_reg, disp) -> set of sizes/ops
flagops = []  # (addr, mnemonic, imm)
for insn in insns:
    ops = insn.op_str
    if '[rbx' in ops or '[r14' in ops or '[r15' in ops or '[r12' in ops:
        if insn.mnemonic in ('mov', 'or', 'and', 'test') and ('ptr [rbx' in ops or 'ptr [r14' in ops or 'ptr [r15' in ops or 'ptr [r12' in ops):
            print(f"0x{insn.address:X}: {insn.mnemonic} {ops}")
