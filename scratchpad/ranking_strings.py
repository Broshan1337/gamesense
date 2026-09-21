#!/usr/bin/env python3
"""Resolve the KeyValues key strings + dump the skipped branch targets of the card publisher."""
import sys
sys.path.insert(0, 'scratchpad')
from disasm import C, text_blob, md

blob, base = text_blob(C)
md2 = md

def cstr(va, maxlen=48):
    try:
        off = C.va_to_off(va)
    except Exception:
        return None
    raw = C.data[off:off+maxlen]
    end = raw.find(b'\0')
    if end < 0:
        return None
    return raw[:end].decode('utf-8', 'replace')

def lea_target(addr, disp):
    # length of the lea instruction = 7
    return addr + 7 + disp

SITES = [
    (0x1EFF0F3, -4),  # skip
    (0x1EFF10D, -0x13bb04a),
    (0x1EFF16B, -0x13d1d52),
    (0x1EFF27E, -0x13bef1b),
    (0x1EFF366, -0x1366b2e),
    (0x1EFF37E, -0x13bbd06),
    (0x1EFF393, -0x138a646),
    (0x1EFF3A9, -0x13efc9c),
    (0x1EFF3BE, -0x13d441c),
    (0x1EFF3D3, -0x139fd32),
    (0x1EFF3EC, -0x139ca16),
    (0x1EFF3FB, -0x13afb6e),
    (0x1EFF413, -0x1367e41),
    (0x1EFF42C, -0x13a6023),
    (0x1EFF441, -0x137f8a0),
    (0x1EFF455, -0x13eb48e),
    (0x1EFF48D, -0x13c5a46),
    (0x1EFF4B1, -0x136eff2),
]
for addr, disp in SITES:
    t = lea_target(addr, disp)
    s = cstr(t)
    print(f"0x{addr:X} -> 0x{t:X} : {s!r}")

# the skill-group array base
t = 0x1EFF41A + 7 + 0x29cca7f
print(f"\nskill-group array base: 0x{t:X}")

# dump branch regions 0x1eff548..0x1eff720
print("\n===== branch targets =====")
off = C.va_to_off(0x1EFF540)
code = blob[off:off+0x200]
for insn in md2.disasm(code, 0x1EFF540):
    print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
