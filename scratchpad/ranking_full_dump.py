#!/usr/bin/env python3
"""Full dump of the profile-card publisher reading the ranking block (pattern site 0x1eff144,
block 0x4843720). Dumps every [rbx+disp] access in order with surrounding flag tests."""
import sys
sys.path.insert(0, 'scratchpad')
from disasm import C, text_blob, md

blob, base = text_blob(C)
site = 0x1EFF144

# function start: walk back over prologue
o = site - base
start = None
for back in range(0x40, 0x400):
    i = o - back
    if blob[i:i+3] == b'\x55\x48\x89' and blob[i+3] in (0xE5,):  # push rbp; mov rbp,rsp
        start = base + i
        break
    if blob[i:i+4] == b'\xf3\x0f\x1e\xfa':
        start = base + i
        break
print(f"start: {hex(start) if start else 'not found, using site-0x60'}")
if not start:
    start = site - 0x60

off = start - base
code = blob[off: off + 0x3000]
out = []
for insn in md.disasm(code, start):
    out.append(insn)
    if insn.mnemonic == 'ret' and len(out) > 100:
        break
    if len(out) > 1500:
        break

# print the whole thing, highlighting rbx accesses
for insn in out:
    mark = '  <== BLOCK' if 'rbx' in insn.op_str and '[' in insn.op_str else ''
    print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}{mark}")
