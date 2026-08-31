#!/usr/bin/env python3
"""ELF section map + rip-relative xref scanner for post-update pattern re-derivation."""
import struct

class Elf:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        assert self.data[:4] == b'\x7fELF'
        e_shoff = struct.unpack_from('<Q', self.data, 0x28)[0]
        e_shentsize = struct.unpack_from('<H', self.data, 0x3A)[0]
        e_shnum = struct.unpack_from('<H', self.data, 0x3C)[0]
        e_shstrndx = struct.unpack_from('<H', self.data, 0x3E)[0]
        def sh(i):
            o = e_shoff + i * e_shentsize
            name, typ, flags, addr, off, size = struct.unpack_from('<IIQQQQ', self.data, o)
            return dict(name=name, type=typ, flags=flags, addr=addr, off=off, size=size)
        strtab = sh(e_shstrndx)
        self.sections = []
        for i in range(e_shnum):
            s = sh(i)
            n_start = strtab['off'] + s['name']
            n_end = self.data.index(b'\x00', n_start)
            s['sname'] = self.data[n_start:n_end].decode()
            self.sections.append(s)
        self.by_name = {s['sname']: s for s in self.sections}

    def off_to_va(self, off):
        for s in self.sections:
            if s['type'] != 8 and s['size'] and s['off'] <= off < s['off'] + s['size']:
                return s['addr'] + (off - s['off'])
        return None

    def va_to_off(self, va):
        for s in self.sections:
            if s['type'] != 8 and s['addr'] and s['addr'] <= va < s['addr'] + s['size']:
                return s['off'] + (va - s['addr'])
        return None

    def sec_of_va(self, va):
        for s in self.sections:
            if s['addr'] and s['addr'] <= va < s['addr'] + s['size']:
                return s['sname']
        return '?'

    def cstr_at_off(self, off, maxlen=80):
        end = self.data.index(b'\x00', off)
        return self.data[off:min(end, off+maxlen)].decode('utf-8', 'replace')

    def find_string_offsets(self, needle):
        out, start = [], 0
        nb = needle.encode() if isinstance(needle, str) else needle
        while True:
            i = self.data.find(nb, start)
            if i < 0:
                return out
            # require NUL-terminated-ish context
            out.append(i)
            start = i + 1

    def xrefs_to_va(self, va):
        """Scan every section's raw bytes for disp32 whose rip-target == va.
        Returns list of (section_name, disp_end_off, insn_end_va)."""
        hits = []
        target = struct.pack('<i', 0)  # placeholder; we compute per position instead
        import struct as st
        dv = st.pack('<i', -1)
        for s in self.sections:
            if not s['size'] or s['type'] == 8 or not s['addr']:
                continue
            blob = self.data[s['off']:s['off']+s['size']]
            base = s['addr']
            i = 0
            n = len(blob) - 4
            while i < n:
                d = st.unpack_from('<i', blob, i)[0]
                if d > -0x10000000 and d < 0x10000000:
                    end_va = base + i + 4
                    if end_va + d == va:
                        hits.append((s['sname'], i, end_va))
                i += 1
        return hits
