#!/usr/bin/env python3
# Makes room for VMProtect's new program header: inserts a page of padding right after
# the phdr table and shifts EVERYTHING that follows - file offsets AND virtual
# addresses, uniformly - with complete fixups:
#   - segment 1 (the LOAD at file 0): filesz/memsz grow, vaddr mapping stays offset==vaddr
#   - segments 2+: file offset AND vaddr += pad (congruence preserved)
#   - e_entry += pad (it lives in the shifted range)
#   - all section sh_addr/sh_offset += pad (in-range ones)
#   - .dynamic: every address-valued tag += pad
#   - .rela.dyn/.rela.plt: r_offset += pad, nonzero in-range addends += pad
#   - dynsym: defined symbols' st_value += pad
# Result: a self-consistent image with free phdr-table space for VMProtect.
import struct, sys

PAD = 0x1000
VADDR_DTAGS = {3, 4, 5, 6, 7, 12, 13, 23, 25, 26, 0x6ffffef5, 0x6ffffff0, 0x6ffffffe}

def main(path):
    with open(path, 'rb') as f:
        data = bytearray(f.read())
    assert data[:4] == b'\x7fELF' and data[4] == 2

    e_entry, = struct.unpack_from('<Q', data, 0x18)
    e_phoff, = struct.unpack_from('<Q', data, 0x20)
    e_shoff, = struct.unpack_from('<Q', data, 0x28)
    e_phentsize, e_phnum = struct.unpack_from('<HH', data, 0x36)
    e_shentsize, e_shnum = struct.unpack_from('<HH', data, 0x3a)
    phdr_end = e_phoff + e_phnum * e_phentsize

    phdrs = [list(struct.unpack_from('<IIQQQQQQ', data, e_phoff + i * e_phentsize))
             for i in range(e_phnum)]

    # insert the padding
    new = bytearray(data[:phdr_end]) + bytearray(PAD) + bytearray(data[phdr_end:])

    def in_content(off):  # file offset that must shift
        return off >= phdr_end

    # program headers
    for i, p in enumerate(phdrs):
        p_type, flags, off, va, pa, fsz, msz, al = p
        if p_type == 1 and off == 0:          # segment 1: keep offset/vaddr, grow sizes
            fsz += PAD
            msz += PAD
        else:
            if in_content(off):
                off += PAD
            if va >= phdr_end:
                va += PAD
                pa += PAD
        struct.pack_into('<IIQQQQQQ', new, e_phoff + i * e_phentsize,
                         p_type, flags, off, va, pa, fsz, msz, al)

    # entry point
    if e_entry >= phdr_end:
        e_entry += PAD
        struct.pack_into('<Q', new, 0x18, e_entry)

    # section headers
    new_e_shoff = e_shoff + PAD
    struct.pack_into('<Q', new, 0x28, new_e_shoff)
    for i in range(e_shnum):
        # the table's CONTENT moved +PAD with everything else - read it at the new spot
        so = e_shoff + PAD + i * e_shentsize
        name, typ, flags, addr, off, size = struct.unpack_from('<IIQQQQ', new, so)
        shifted = False
        if addr >= phdr_end:
            addr += PAD
            shifted = True
        if in_content(off):
            off += PAD
            shifted = True
        if shifted:
            struct.pack_into('<IIQQQQ', new, so, name, typ, flags, addr, off, size)

    # locate the (already shifted) DYNAMIC segment in the new file
    dyn_off = dyn_sz = None
    for i, p in enumerate(phdrs):
        if p[0] == 2:  # PT_DYNAMIC
            dyn_off = p[2] if not in_content(p[2]) else p[2] + PAD
            dyn_sz = p[5]
            break
    assert dyn_off is not None
    # re-read the shifted phdr to get the true new offset
    for i in range(e_phnum):
        t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', new, e_phoff + i * e_phentsize)
        if t == 2:
            dyn_off, dyn_sz = off, fsz
            break

    # .dynamic: shift address-valued tags
    for off in range(dyn_off, dyn_off + dyn_sz, 16):
        tag, val = struct.unpack_from('<qQ', new, off)
        if tag == 0:
            break
        if tag in VADDR_DTAGS and val >= phdr_end:
            struct.pack_into('<Q', new, off + 8, val + PAD)

    # vaddr -> file offset via the shifted PT_LOADs
    loads = []
    for i in range(e_phnum):
        t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', new, e_phoff + i * e_phentsize)
        if t == 1:
            loads.append((off, va, fsz))
    def v2f(v):
        for loff, lva, lsz in loads:
            if lva <= v < lva + lsz:
                return v - lva + loff
        return None

    # .rela.dyn + .rela.plt: shift r_offset and nonzero in-range addends
    rela_ranges = []
    for i in range(e_phnum):
        t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', new, e_phoff + i * e_phentsize)
        if t == 2:
            d = new[off:off + fsz]
            pairs = {}
            for do in range(0, len(d), 16):
                tag, val = struct.unpack_from('<qQ', d, do)
                if tag == 0:
                    break
                pairs[tag] = val
            if 7 in pairs and 8 in pairs:      # DT_RELA / DT_RELASZ
                rela_ranges.append((pairs[7], pairs[8]))
            if 23 in pairs and 2 in pairs:     # DT_JMPREL / DT_PLTRELSZ
                rela_ranges.append((pairs[23], pairs[2]))
    for base, total in rela_ranges:
        base = v2f(base)
        if base is None:
            continue
        for ro in range(base, base + total, 24):
            r_offset, r_info, r_addend = struct.unpack_from('<QQq', new, ro)
            new_offset = r_offset + PAD if r_offset >= phdr_end else r_offset
            new_addend = r_addend + PAD if (r_addend != 0 and r_addend >= phdr_end) else r_addend
            if new_offset != r_offset or new_addend != r_addend:
                if 0x1736 <= ro < 0x49f00 and ro != base + (ro - base):
                    pass
                if 0x1736 <= ro < 0x4a000:
                    print(f"RELA write at {ro:#x} (base {base:#x}) r_offset {r_offset:#x}->{new_offset:#x}")
                struct.pack_into('<QQq', new, ro, new_offset, r_info, new_addend)

    # dynsym st_value for defined symbols
    for i in range(e_phnum):
        t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', new, e_phoff + i * e_phentsize)
        if t == 2:
            d = new[off:off + fsz]
            symtab = strsz = None
            for do in range(0, len(d), 16):
                tag, val = struct.unpack_from('<qQ', d, do)
                if tag == 0:
                    break
                if tag == 6:
                    symtab = val
                elif tag == 10:  # DT_STRSZ - not the symtab size; skip sizing via hash instead
                    pass
            if symtab is None:
                break
            symtab = v2f(symtab)
            if symtab is None:
                break
            # exact symbol bounds from the SHT_DYNSYM/SHT_SYMTAB section headers
            for i in range(e_shnum):
                so = new_e_shoff + i * e_shentsize
                sh_typ, = struct.unpack_from('<I', new, so + 4)
                if sh_typ in (11, 2):  # SHT_DYNSYM + SHT_SYMTAB (VMP reads .symtab!)
                    dsym_off, = struct.unpack_from('<Q', new, so + 0x18)
                    dsym_size, = struct.unpack_from('<Q', new, so + 0x20)
                    entsize, = struct.unpack_from('<Q', new, so + 0x38)
                    entsize = entsize or 24
                    for so2 in range(dsym_off, dsym_off + dsym_size, entsize):
                        fields = struct.unpack_from('<IBBHQQ', new, so2)
                        st_value, st_shndx = fields[4], fields[3]
                        if st_value == 0 or st_shndx == 0:
                            continue
                        if st_value >= phdr_end:
                            struct.pack_into('<Q', new, so2 + 8, st_value + PAD)
            break

    out = path + '.fixed'
    open(out, 'wb').write(bytes(new))
    print(f"{path} -> {out} (pad {PAD:#x}, entry {e_entry:#x})")

if __name__ == '__main__':
    main(sys.argv[1])
