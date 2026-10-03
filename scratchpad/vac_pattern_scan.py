#!/usr/bin/env python3
"""Scan steamservice.so for VAC build_diagnostic_response and memory copy functions."""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from elfmap import Elf
import capstone

STEAMSERVICE = os.path.expanduser('~/.local/share/Steam/steamrt64/steamservice.so')

elf = Elf(STEAMSERVICE)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

text = elf.by_name.get('.text')
if not text:
    print("ERROR: .text section not found")
    sys.exit(1)

code = elf.data[text['off']:text['off']+text['size']]

print(f"steamservice.so .text: 0x{text['addr']:X} - 0x{text['addr']+text['size']:X} ({text['size']} bytes)")
print()

# Strategy 1: Look for functions with large stack frames (>= 0x500) that process arrays
# Similar to Windows build_diagnostic_response which has sub rsp, 0x9E0
print("=" * 60)
print("Strategy 1: Functions with large stack frames")
print("=" * 60)

large_frame_funcs = []
for insn in md.disasm(code, text['addr']):
    if insn.mnemonic == 'sub' and 'rsp' in insn.op_str:
        # Parse the immediate value
        try:
            if 'rsp' in insn.op_str:
                parts = insn.op_str.split(',')
                if len(parts) == 2:
                    imm_str = parts[1].strip()
                    if imm_str.startswith('0x'):
                        imm = int(imm_str, 16)
                    else:
                        imm = int(imm_str)
                    if imm >= 0x500:
                        # Find function start by looking for push rbp or endbr64
                        offset = elf.va_to_off(insn.address)
                        if offset:
                            func_start = None
                            # Look back up to 0x100 bytes for function prologue
                            back_start = max(0, offset - 0x100)
                            back_code = elf.data[back_start:offset]
                            for i in range(len(back_code) - 3, -1, -1):
                                # endbr64
                                if back_code[i:i+4] == b'\xf3\x0f\x1e\xfa':
                                    func_start = back_start + i
                                    break
                                # push rbp; mov rbp, rsp
                                if back_code[i:i+3] == b'\x55\x48\x89\xe5':
                                    func_start = back_start + i
                                    break
                            large_frame_funcs.append((insn.address, imm, func_start))
        except:
            pass

print(f"Found {len(large_frame_funcs)} functions with large stack frames:")
for addr, size, func_start in large_frame_funcs[:20]:
    if func_start:
        va = elf.off_to_va(func_start)
        print(f"  sub rsp, 0x{size:X} at 0x{insn.address:X} (func ~0x{va:X})")
    else:
        print(f"  sub rsp, 0x{size:X} at 0x{addr:X}")

# Strategy 2: Look for patterns similar to Windows build_diagnostic_response
# Windows: 89 54 24 10 53 56 57 41 54 41 55 41 56 41 57 48 81 EC E0 09 00 00
# This is: mov [rsp+10], edx; push rbx; push rsi; push rdi; push r12-r15; sub rsp, 0x9E0
print()
print("=" * 60)
print("Strategy 2: Direct pattern match (Windows build_diagnostic_response prologue)")
print("=" * 60)

# Try partial pattern - push sequence
push_seq = bytes([0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57])
idx = 0
while True:
    pos = code.find(push_seq, idx)
    if pos == -1:
        break
    va = text['addr'] + pos
    print(f"  Found push sequence at 0x{va:X}")
    # Disassemble first 30 bytes
    snippet = code[pos:pos+30]
    print("  ", end="")
    for insn in md.disasm(snippet, va):
        print(f"0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
        if insn.address > va + 20:
            break
    print()
    idx = pos + 1

# Strategy 3: Look for functions that access type==28 or type==29 (diagnostic types)
# These would have cmp dword ptr [reg+68], 28 or similar
print()
print("=" * 60)
print("Strategy 3: Functions with cmp [reg+68], 28/29 (diagnostic type checks)")
print("=" * 60)

type_check_candidates = set()
for insn in md.disasm(code, text['addr']):
    if insn.mnemonic == 'cmp' and ('28' in insn.op_str or '29' in insn.op_str):
        # Check if it's comparing a memory operand with offset around 68
        if 'ptr' in insn.op_str and ('+68' in insn.op_str or '+44' in insn.op_str):
            type_check_candidates.add(insn.address)

print(f"Found {len(type_check_candidates)} potential type check locations")

# Strategy 4: Find memcpy/memmove imports or internal copies
print()
print("=" * 60)
print("Strategy 4: memcpy/memmove references")
print("=" * 60)

plt = elf.by_name.get('.plt')
got = elf.by_name.get('.got.plt')
if plt and got:
    plt_code = elf.data[plt['off']:plt['off']+plt['size']]
    # Look for common patterns that indicate memory copying for VAC
    # Could be calls to memcpy via PLT
    memcpy_va = None
    reloc_section = elf.by_name.get('.rela.plt') or elf.by_name.get('.rela.dyn')
    if reloc_section:
        import struct
        reloc_data = elf.data[reloc_section['off']:reloc_section['off']+reloc_section['size']]
        # Each reloc is 24 bytes (Elf64_Rela)
        for i in range(0, len(reloc_data), 24):
            r_offset, r_info, r_addend = struct.unpack_from('<QQq', reloc_data, i)
            sym_idx = r_info >> 32
            type = r_info & 0xffffffff
            if type == 7:  # R_X86_64_JUMP_SLOT
                # Try to find symbol name
                pass  # Would need dynsym section
    
print()
print("=" * 60)
print("Strategy 5: Looking for analyze_module callers")
print("=" * 60)

# We know analyze_module pattern: 48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F9 0F B6 51
analyze_module_pattern = bytes([0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 
                                 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x0F, 0xB6, 0x51])
idx = code.find(analyze_module_pattern)
if idx != -1:
    analyze_module_va = text['addr'] + idx
    print(f"analyze_module at 0x{analyze_module_va:X}")
    
    # Find callers
    callers = elf.xrefs_to_va(analyze_module_va)
    print(f"Callers: {len(callers)}")
    for sec, off, end_va in callers[:10]:
        print(f"  Called from 0x{end_va:X}")
else:
    print("analyze_module pattern not found (pattern may have changed)")
