#!/usr/bin/env python3
# rehelp.py - capstone-based RE toolkit for the CS2 Linux binaries (no IDA needed).
# Recreated 2026-09-23 for the 1.41.8.2 update.
#
#   import rehelp as R            # REMOD env selects the module (client/tier0/sound/fs/
#   R.pdis(va, n)                 #   panorama/scene/schema/engine2/server)
#   R.findv("48 8B 05 ?? ?? ?? ??")
#   R.strings_addr("ChangeTeammateColor")
#   R.xrefs_to(va)                # rip-rel disp32 + E8/E9 rel32 refs, .text scope
#   R.func_start(va)
#   R.dis(va, n)                  # list of (addr, bytes, mnemonic, ops)
import struct, os, importlib

GAME = "/mnt/disk2/SteamLibrary/steamapps/common/Counter-Strike Global Offensive"
MODULES = {
    "client":   GAME + "/game/csgo/bin/linuxsteamrt64/libclient.so",
    "tier0":    GAME + "/game/bin/linuxsteamrt64/libtier0.so",
    "sound":    GAME + "/game/bin/linuxsteamrt64/libsoundsystem.so",
    "fs":       GAME + "/game/bin/linuxsteamrt64/libfilesystem_stdio.so",
    "panorama": GAME + "/game/bin/linuxsteamrt64/libpanorama.so",
    "scene":    GAME + "/game/bin/linuxsteamrt64/libscenesystem.so",
    "schema":   GAME + "/game/bin/linuxsteamrt64/libschemasystem.so",
    "engine2":  GAME + "/game/bin/linuxsteamrt64/libengine2.so",
    "server":   GAME + "/game/csgo/bin/linuxsteamrt64/libserver.so",
}

class Mod:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        d = self.data
        e_phoff, = struct.unpack_from('<Q', d, 0x20)
        e_phentsize, e_phnum = struct.unpack_from('<HH', d, 0x36)
        self.loads = []
        for i in range(e_phnum):
            t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', d, e_phoff + i * e_phentsize)
            if t == 1:
                self.loads.append((va, fsz, off))
        e_shoff, = struct.unpack_from('<Q', d, 0x28)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', d, 0x3a)
        strtab_hdr_off = e_shoff + e_shstrndx * e_shentsize
        strtab_off, strtab_size = struct.unpack_from('<QQ', d, strtab_hdr_off + 0x18)
        self.shstrdata = d[strtab_off:strtab_off + strtab_size]
        self.sections = []
        for i in range(e_shnum):
            sh = struct.unpack_from('<IIQQQQIIQQ', d, e_shoff + i * e_shentsize)
            end = self.shstrdata.index(b"\0", sh[0])
            name = self.shstrdata[sh[0]:end].decode("ascii", "replace")
            self.sections.append({'name': name, 'addr': sh[3], 'off': sh[4], 'size': sh[5],
                                  'type': sh[1], 'flags': sh[2]})


    def f2v(self, off):
        for base, filesz, off0 in self.loads:
            if off0 <= off < off0 + filesz:
                return base + (off - off0)
        return None

    def v2f(self, va):
        for base, filesz, off0 in self.loads:
            if base <= va < base + filesz:
                return off0 + (va - base)
        return None

    def text(self):
        for s in self.sections:
            if s['name'] == '.text':
                return s
        return None

_M = None

def load(name=None):
    global _M
    _M = Mod(MODULES[name or os.environ.get('REMOD', 'client')])
    return _M

def _ensure():
    global _M
    if _M is None:
        load(os.environ.get('REMOD', 'client'))
    return _M

def v2f(va, mod=None):
    m = mod or _ensure()
    for base, filesz, off in m.loads:
        if base <= va < base + filesz:
            return off + (va - base)
    return None

def file_read(va, n, mod=None):
    m = mod or _ensure()
    for base, filesz, off0 in m.loads:
        if base <= va < base + filesz:
            return m.data[off0 + (va - base):off0 + (va - base) + n]
    return b''

def pdis(va, n=24, mod=None):
    md = import_capstone()
    code = file_read(va, n * 15 + 16)
    out = []
    for insn in md.disasm(code, va):
        out.append(f"0x{insn.address:x}:  {insn.bytes.hex():<20} {insn.mnemonic} {insn.op_str}")
        print(out[-1])
        if len(out) >= n:
            break
    return out

def dis(va, n=24, mod=None):
    md = import_capstone()
    code = file_read(va, n * 15 + 16)
    out = []
    for insn in md.disasm(code, va):
        out.append((insn.address, insn.bytes.hex(), insn.mnemonic, insn.op_str))
        if len(out) >= n:
            break
    return out

def import_capstone():
    import capstone
    return capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

def _hex_to_bytes(hexpat):
    pat = bytearray()
    wild = []
    for t in hexpat.split():
        if t in ('??', '?'):
            pat.append(0)
            wild.append(True)
        else:
            pat.append(int(t, 16))
            wild.append(False)
    return bytes(pat), wild

def findv(hexpat, mod=None):
    m = mod or _ensure()
    pat, wild = _hex_to_bytes(hexpat)
    d = m.data
    out = []
    start = 0
    while len(out) < 400:
        i = d.find(pat, start)
        if i < 0:
            break
        # a zeroed pattern byte may stand for any byte: verify non-wildcard bytes only
        ok = True
        for k, t in enumerate(hexpat.split()):
            if not wild[k] and d[i + k] != pat[k]:
                ok = False
                break
        if ok:
            va = None
            for base, filesz, off0 in m.loads:
                if off0 <= i < off0 + filesz:
                    va = base + (i - off0)
                    break
            if va is not None:
                out.append(va)
        start = i + 1
        if len(out) >= 400:
            break
    return out

def strings_addr(s, mod=None):
    m = mod or _ensure()
    d = m.data
    b = s.encode()
    out = []
    start = 0
    while len(out) < 64:
        j = d.find(b, start)
        if j < 0:
            break
        a = j
        while a > 0 and 0x20 <= d[a - 1] < 0x7f:
            a -= 1
        k = j + len(b)
        while k < len(d) and 0x20 <= d[k] < 0x7f:
            k += 1
        # map file offset j -> vaddr via loads (covers .rodata/.data.rel.ro)
        va = None
        for base, filesz, off0 in m.loads:
            if off0 <= j < off0 + filesz:
                va = base + (j - off0)
                break
        if va is None:
            start = j + 1
            continue
        sa = va - (j - a)
        out.append((sa, d[a:k].decode('ascii', 'replace')))
        start = j + 1
    return out

def xrefs_to(va, max_hits=64):
    m = _ensure()
    t = m.text()
    if not t:
        return []
    foff, size, base = t['off'], t['size'], t['addr']
    d = m.data
    hits = []
    for i in range(0, size - 4):
        o = foff + i
        va_here = base + i
        # rip-relative: rip after the disp32 = va_here+4
        disp = struct.unpack_from('<i', d, o)[0]
        if va_here + 4 + disp == va:
            hits.append((va_here - 4, 'ripdisp'))
            if len(hits) >= max_hits:
                break
            continue
        # rel32 call/jmp: instruction starts at va_here (E8/E9 at the PREVIOUS byte)
        rel = disp
        prev = d[o - 1] if i >= 1 else 0
        if prev in (0xE8, 0xE9) and va_here + rel == va:
            hits.append((va_here - 1, 'calljmp'))
            if len(hits) >= max_hits:
                break
    return hits

def func_start(va):
    m = _ensure()
    off = None
    for base, filesz, off0 in m.loads:
        if base <= va < base + filesz:
            off = off0 + (va - base)
    if off is None:
        return None
    d = m.data
    i = off
    ccs = 0
    while i > 0:
        i -= 1
        if d[i] == 0xCC:
            ccs += 1
        elif d[i] == 0xC3 and ccs >= 1:
            return f2v(m, i + 1)
        elif ccs >= 2:
            return f2v(m, i + 1)
        else:
            ccs = 0
    return None

def f2v(mod, off):
    for base, filesz, off0 in mod.loads:
        if off0 <= off < off0 + filesz:
            return base + (off - off0)
    return None

def pfunc(va, n=80):
    s = func_start(va)
    print(f"func_start({va:#x}) = {s and hex(s)}")
    if s:
        pdis(s, n)
def wscan(hexpat, limit=20, mod=None, text_only=True):
    """Wildcard-aware regex scan over .text (or whole module). Returns vaddrs."""
    import re as _re
    m = mod or _ensure()
    toks = hexpat.split()
    rx = b''
    for t in toks:
        rx += b'.' if t in ('??', '?') else _re.escape(bytes([int(t, 16)]))
    out = []
    t = m.text()
    lo, hi = (t['off'], t['off'] + t['size']) if (t and text_only) else (0, len(m.data))
    for mo in _re.finditer(rx, m.data[lo:hi], _re.DOTALL):
        va = m.f2v(lo + mo.start())
        if va is not None:
            out.append(va)
            if len(out) >= limit:
                break
    return out
