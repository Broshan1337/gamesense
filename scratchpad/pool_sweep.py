#!/usr/bin/env python3
"""Full pattern-pool sweep (offline runtime-replica).

NOTE: infers the module per FILE by the builder variable found in the header
(clientPatterns -> libclient, panoramaPatterns -> libpanorama, ...). Files with
MULTIPLE chains (e.g. PanoramaUiPanelPatternsLinux.h has addClientPatterns AND
addPanoramaPatterns) get all their patterns scanned against the FIRST builder's
module - wrong for the second chain. The authoritative per-pool check is the
compiled scratchpad pattern_scan tool (pattern types carry the module)."""
"""Full sweep v3: parse EVERY addPattern from every Linux pattern header, infer module from
the builder the file uses (grep for the builder variable), wildcard-scan against that
module's .text, report MISSING/MULTI and resolved values."""
import json, re, struct, os, glob

GAME = '/home/d/.local/share/Steam/steamapps/common/Counter-Strike Global Offensive/game'
MODULES = {
    'client': f'{GAME}/csgo/bin/linuxsteamrt64/libclient.so',
    'scene': f'{GAME}/bin/linuxsteamrt64/libscenesystem.so',
    'tier0': f'{GAME}/bin/linuxsteamrt64/libtier0.so',
    'fs': f'{GAME}/bin/linuxsteamrt64/libfilesystem_stdio.so',
    'sound': f'{GAME}/bin/linuxsteamrt64/libsoundsystem.so',
    'schema': f'{GAME}/bin/linuxsteamrt64/libschemasystem.so',
    'panorama': f'{GAME}/bin/linuxsteamrt64/libpanorama.so',
    'sdl': f'{GAME}/bin/linuxsteamrt64/libSDL3.so.0',
    'inputsystem': f'{GAME}/bin/linuxsteamrt64/libinputsystem.so',
    'matchmaking': f'{GAME}/bin/linuxsteamrt64/libmatchmaking.so',
    'networksystem': f'{GAME}/bin/linuxsteamrt64/libnetworksystem.so',
    'particles': f'{GAME}/bin/linuxsteamrt64/libparticles.so',
    'materialsystem2': f'{GAME}/bin/linuxsteamrt64/libmaterialsystem2.so',
    'vphysics2': f'{GAME}/bin/linuxsteamrt64/libvphysics2.so',
    'localize': f'{GAME}/bin/linuxsteamrt64/liblocalize.so',
}
hdr_dir = '/home/d/dev/neversnooze/gamesense/cs2/Source/MemoryPatterns/Linux'
rx_loose = re.compile(r'addPattern<(\w+),\s*CodePattern\{"([^"]+)"\}([^;]*?)>\(\)', re.S)
ops_rx = re.compile(r'\.(add|abs|read8|read)\((\d+)?\)')
# module inference: the builder var used in the file (clientPatterns / panoramaPatterns / ...)
builder_rx = re.compile(r'(clientPatterns|sceneSystemPatterns|tier0Patterns|soundPatterns|fileSystemPatterns|panoramaPatterns|schemaSystemPatterns|sdlPatterns)\b')

def text_of(mod):
    data = open(MODULES[mod], 'rb').read()
    e_shoff, = struct.unpack_from('<Q', data, 0x28)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', data, 0x3a)
    def sh(i):
        o = e_shoff + i * e_shentsize
        return struct.unpack_from('<IIQQQQ', data, o)
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        name_off = sh(i)[0]
        # skip name check - just match by index order is unsafe; do names properly
        pass
    # do it with names:
    strtab_off = sh(e_shstrndx)[4]
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        name_off, typ, flags, addr, off, size = struct.unpack_from('<IIQQQQ', data, o)
        end = data.index(b'\0', strtab_off + name_off)
        if data[strtab_off + name_off:end].decode() == '.text':
            return addr, data[off:off+size]
    raise KeyError(mod)

broken, ok, checked = {}, 0, 0
for path in sorted(glob.glob(hdr_dir + '/*.h')):
    src = open(path).read()
    bm = builder_rx.search(src)
    if not bm: continue
    mod_key = bm.group(1).replace('Patterns', '').replace('client', 'client').replace('sceneSystem','scene').replace('tier0','tier0').replace('sound','sound').replace('fileSystem','fs').replace('panorama','panorama').replace('schemaSystem','schema').replace('sdl','sdl')
    mod = mod_key
    if mod not in MODULES: continue
    va, blob = text_of(mod)
    for m in rx_loose.finditer(src):
        name, pat, opchain = m.group(1), m.group(2), m.group(3)
        offset, op = 0, 'None'
        for om in ops_rx.finditer(opchain):
            kind, arg = om.group(1), om.group(2)
            if kind == 'add': offset += int(arg or 0)
            elif kind == 'read': op = 'Read'
            elif kind == 'read8': op = 'Read8'
            elif kind == 'abs': op = 'Abs4' if (arg or '4') == '4' else 'Abs5'
        width = 1 if op == 'Read8' else 4
        checked += 1
        toks = pat.split()
        rxp = b''.join(b'.' if t == '?' else re.escape(bytes([int(t,16)])) for t in toks)
        hits = [mm.start() for mm in re.finditer(rxp, blob, re.DOTALL)]
        if len(hits) == 1: ok += 1; continue
        broken.setdefault((mod, len(hits)), []).append(name)

print(f"checked {checked}, exactly-once {ok}")
print("=== BROKEN ===")
for (mod, n), names in sorted(broken.items()):
    status = 'MISSING' if n == 0 else 'MULTI'
    print(f"{mod:12} x{n:<3} {status}: {', '.join(sorted(names))}")
print(f"total broken: {sum(len(v) for v in broken.values())}")
