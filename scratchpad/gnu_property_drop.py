#!/usr/bin/env python3
# Removes the PT_GNU_PROPERTY program header from an ELF64 binary.
#
# Part of the VMProtect ship flow (see project_anti_reversing_hardening memory):
#   1. rebuild Loader/build-vmp (stock clang, NS_LOADER_SHIP=OFF = unstripped input)
#   2. THIS SCRIPT: drop PT_GNU_PROPERTY (VMP's phdr handling trips over it / the Sep 21
#      verified baseline carried no GNU_PROPERTY - the 2026-09-22 evening round ran VMP on
#      an unshifted input with GNU_PROPERTY still present and produced a 5-LOAD layout
#      that differs from the proven 6-LOAD baseline)
#   3. elf_shift.py: make room for VMP's new program header
#   4. VMP-Ultra (manual step - the only one that is not scripted)
#
# Mechanics: remove the GNU_PROPERTY entry from the phdr table (shift later entries up,
# decrement e_phnum, zero the freed tail slot). Sections/relocations reference phdr
# CONTENT, not indices, so removing one entry is safe. Writes in place (backup kept
# next to the file).
import struct, sys

PT_GNU_PROPERTY = 0x6474e553

def main(path):
    with open(path, 'rb') as f:
        data = bytearray(f.read())
    assert data[:4] == b'\x7fELF' and data[4] == 2, "ELF64 only"
    e_phoff, = struct.unpack_from('<Q', data, 0x20)
    e_phentsize, e_phnum = struct.unpack_from('<HH', data, 0x36)

    # locate PT_GNU_PROPERTY
    idx = None
    for i in range(e_phnum):
        p_type, = struct.unpack_from('<I', data, e_phoff + i * e_phentsize)
        if p_type == PT_GNU_PROPERTY:
            idx = i
            break
    if idx is None:
        print(f"{path}: no PT_GNU_PROPERTY - nothing to do")
        return

    entsz = e_phentsize
    table_end = e_phoff + e_phnum * entsz
    # shift everything after the GNU_PROPERTY entry up by one slot
    data[e_phoff + idx * entsz: table_end - entsz] = data[e_phoff + (idx + 1) * entsz: table_end]
    # zero the freed last slot
    data[table_end - entsz: table_end] = bytes(entsz)
    struct.pack_into('<H', data, 0x38, e_phnum - 1)

    with open(path + '.bak', 'wb') as f:
        f.write(open(path, 'rb').read())
    with open(path, 'wb') as f:
        f.write(bytes(data))
    print(f"{path}: dropped PT_GNU_PROPERTY (was index {idx}), e_phnum {e_phnum} -> {e_phnum - 1} (backup: {path}.bak)")

if __name__ == '__main__':
    main(sys.argv[1])