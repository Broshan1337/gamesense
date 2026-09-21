#!/usr/bin/env python3
# Pads the ELF program-header table with spare PT_NULL slots so VMProtect can insert
# its new segment ("Not enough space for the new segment in the file header").
#
# Mechanics: the first PT_LOAD starts at file offset 0 and covers the ELF header +
# phdr table. We insert page-aligned padding right after the phdr table (inside the
# first LOAD's file range, so vaddrs stay untouched), extend the first LOAD's
# p_filesz, and shift every later segment's/section's FILE offset by the pad.
# (p_vaddr - p_offset) congruence mod p_align holds because the pad is a page
# multiple. Virtual addresses never change, so relocations/dynamic stay valid.
import struct, sys

def main(path):
    with open(path, 'rb') as f:
        data = bytearray(f.read())
    assert data[:4] == b'\x7fELF' and data[4] == 2, "ELF64 only"
    e_phoff, = struct.unpack_from('<Q', data, 0x20)
    e_shoff, = struct.unpack_from('<Q', data, 0x28)
    e_phentsize, e_phnum = struct.unpack_from('<HH', data, 0x36)
    e_shentsize, e_shnum = struct.unpack_from('<HH', data, 0x3a)
    assert e_phoff == 64, f"unexpected phoff {e_phoff}"

    phdr_end = e_phoff + e_phnum * e_phentsize
    pad = 0x1000 * ((4 * e_phentsize + 0xfff) // 0x1000)  # room for 4 spare phdrs, page-aligned

    phdrs = []
    for i in range(e_phnum):
        phdrs.append(struct.unpack_from('<IIQQQQQQ', data, e_phoff + i * e_phentsize))

    # the PT_LOAD that starts at file offset 0 contains the ELF header + phdr table
    zero_load_idx = next(i for i, p in enumerate(phdrs) if p[0] == 1 and p[2] == 0)
    first = phdrs[zero_load_idx]

    new = bytearray()
    new += data[:phdr_end]
    new += b'\x00' * pad
    new += data[phdr_end:]

    # zero LOAD: grow filesz/memsz by pad (its content region now includes the padding)
    new_first = (1, first[1], first[2], first[3], first[4], first[5] + pad, first[6] + pad, first[7])
    struct.pack_into('<IIQQQQQQ', new, e_phoff + zero_load_idx * e_phentsize, *new_first)
    for i in range(e_phnum):
        if i == zero_load_idx:
            continue
        t, flags, off, va, pa, fsz, msz, al = phdrs[i]
        if off >= phdr_end:
            off += pad
        struct.pack_into('<IIQQQQQQ', new, e_phoff + i * e_phentsize, t, flags, off, va, pa, fsz, msz, al)
    # spare slots: PT_NULL already zeroed by the padding

    # shift section headers + entries
    new_e_shoff = e_shoff + pad
    struct.pack_into('<Q', new, 0x28, new_e_shoff)
    for i in range(e_shnum):
        so = e_shoff + i * e_shentsize
        name, typ, flags, addr, off, size = struct.unpack_from('<IIQQQQ', new, so + 0x0)
        if off >= phdr_end:
            struct.pack_into('<Q', new, so + 0x18, off + pad)

    open(path + '.padded', 'wb').write(bytes(new))
    print(f"padded {path}: +{pad:#x} bytes, {e_phnum} -> {e_phnum + 4} phdr slots -> {path}.padded")

if __name__ == '__main__':
    main(sys.argv[1])
