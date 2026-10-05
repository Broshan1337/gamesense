#!/usr/bin/env python3
"""Scan libpanorama.so .text for the panel-children collector shape and re-forge
ChildPanelsCount/Array patterns for today's build."""
import struct, re, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

PANO = '/home/d/.local/share/Steam/steamapps/common/Counter-Strike Global Offensive/game/bin/linuxsteamrt64/libpanorama.so'
data = open(PANO, 'rb').read()
e_shoff = struct.unpack_from('<Q', data, 0x28)[0]
e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', data, 0x3a)
def sh(i):
    o = e_shoff + i * e_shentsize
    return struct.unpack_from('<IIQQQQ', data, o)
strtab_off = sh(e_shstrndx)[4]
def secname(i):
    o = strtab_off + sh(i)[0]; end = data.index(b'\0', o)
    return data[o:end].decode()
text_addr = text_size = text_off = 0
for i in range(e_shnum):
    nm, typ, flags, addr, off, size = sh(i)
    if secname(i) == '.text':
        text_addr, text_size, text_off = addr, size, off
        break
blob = data[text_off:text_off+text_size]
md = Cs(CS_ARCH_X86, CS_MODE_64)
print(f"panorama .text 0x{text_addr:x} size 0x{text_size:x}")

def scan(p):
    toks = p.split()
    rx = b''.join(b'.' if t == '?' else re.escape(bytes([int(t,16)])) for t in toks)
    return [m.start() for m in re.finditer(rx, blob, re.DOTALL)]

def dis(va, back=0, count=26):
    off = text_off + (va - text_addr) - back
    code = blob[off:off + count * 8]
    for insn in md.disasm(code, va - back):
        print(f"   0x{text_addr + (insn.address - text_addr):x}: {insn.mnemonic} {insn.op_str}")

# shapes: mov edi,[reg+disp32] / mov edx,[reg+disp32] etc. followed by test/jle + array load
shapes = {
 'mov edi,[r+di]; test edi,edi; jle': "8B B8 ? ? ? ? 85 FF 0F 8E",
 'mov edi,[r+di]; test edi,edi; jbe': "8B B8 ? ? ? ? 85 FF 0F 87",
 'mov edx,[r+di]; test edx,edx; jle': "8B 90 ? ? ? ? 85 D2 0F 8E",
 'mov edi,[r+di]; test edi,edi; jbe(j82)': "8B B8 ? ? ? ? 85 FF 0F 82",
}
all_sites = []
for name, p in shapes.items():
    hits = scan(p)
    print(f"\n{name}: {len(hits)}")
    for h in hits[:14]:
        va = text_addr + h
        disp = int.from_bytes(blob[h+2:h+6], 'little', signed=True)
        nxt = blob[h+8:h+22].hex(' ').upper()
        print(f"   0x{va:x} countDisp={disp:#x} next={nxt}")
        all_sites.append((va, disp, name))
EOF_MARKER = None
