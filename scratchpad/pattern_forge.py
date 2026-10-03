#!/usr/bin/env python3
"""pattern_forge.py - offline CS2 pattern/vtable toolkit (no game needed).

Every CS2 update, BEFORE injecting anything (debug-first workflow):
  1. python3 pattern_forge.py validate        # all maintained patterns still match exactly-once?
  2. python3 pattern_forge.py manifest        # record resolved target VAs (cross-build record)
  3. python3 pattern_forge.py vtables --classes CClientInput,CGameEventManager,CViewRender,...
                                             # vtable extents + slot stubs + fingerprints
  4. python3 pattern_forge.py forge --module client --target 0x... --name FooPointer
                                             # regenerate a broken pattern from a known target VA

Targets for `forge`: a global/function VA you trust (cs2-dumper output, the manifest's old
value shifted by a verified delta, or a fresh manual derivation). The forge scans the module
for rip-relative references to the target, picks a site, wildcards every position-dependent
displacement (rip disp32 + call rel32, imm8-aware), extends forward/backward at instruction
boundaries until the masked byte run is EXACTLY-ONCE unique across the module, enforces the
255-byte CodePattern cap, and emits the C++ line.
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys

try:
    import capstone
except ImportError:
    sys.exit("pip install capstone")

GAME_ROOT = os.environ.get(
    "CS2_GAME_ROOT",
    "/mnt/disk2/SteamLibrary/steamapps/common/Counter-Strike Global Offensive/game",
)
SRC_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cs2", "Source", "MemoryPatterns")
MANIFEST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pattern_manifest.json")

MODULES = {
    "client": f"{GAME_ROOT}/csgo/bin/linuxsteamrt64/libclient.so",
    "tier0": f"{GAME_ROOT}/bin/linuxsteamrt64/libtier0.so",
    "sound": f"{GAME_ROOT}/bin/linuxsteamrt64/libsoundsystem.so",
    "fs": f"{GAME_ROOT}/bin/linuxsteamrt64/libfilesystem_stdio.so",
    "panorama": f"{GAME_ROOT}/bin/linuxsteamrt64/libpanorama.so",
    "scene": f"{GAME_ROOT}/bin/linuxsteamrt64/libscenesystem.so",
    "schema": f"{GAME_ROOT}/bin/linuxsteamrt64/libschemasystem.so",
    "engine2": f"{GAME_ROOT}/bin/linuxsteamrt64/libengine2.so",
    "server": f"{GAME_ROOT}/csgo/bin/linuxsteamrt64/libserver.so",
    "networksystem": f"{GAME_ROOT}/bin/linuxsteamrt64/libnetworksystem.so",
    "particles": f"{GAME_ROOT}/bin/linuxsteamrt64/libparticles.so",
}

# pattern pool -> (module, function name in the Linux headers)
POOLS = {
    "kClientPatterns": ("client", "addClientPatterns"),
    "kSceneSystemPatterns": ("scene", "addSceneSystemPatterns"),
    "kTier0Patterns": ("tier0", "addTier0Patterns"),
    "kFileSystemPatterns": ("fs", "addFileSystemPatterns"),
    "kSoundSystemPatterns": ("sound", "addSoundSystemPatterns"),
    "kSchemaSystemPatterns": ("schema", "addSchemaSystemPatterns"),
    "kPanoramaPatterns": ("panorama", "addPanoramaPatterns"),
}

MAX_PATTERN = 255  # TempPatternPool stores patternLengths in uint8_t


class Module:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        d = self.data
        e_phoff, = struct.unpack_from("<Q", d, 0x20)
        e_phentsize, e_phnum = struct.unpack_from("<HH", d, 0x36)
        self.loads = []
        for i in range(e_phnum):
            t, fl, off, va, pa, fsz, msz, al = struct.unpack_from("<IIQQQQQQ", d, e_phoff + i * e_phentsize)
            if t == 1:
                self.loads.append((va, fsz, off))
        e_shoff, = struct.unpack_from("<Q", d, 0x28)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", d, 0x3A)
        strtab_off, = struct.unpack_from("<Q", d, e_shoff + e_shstrndx * e_shentsize + 0x18)
        strtab_size, = struct.unpack_from("<Q", d, e_shoff + e_shstrndx * e_shentsize + 0x20)
        shstr = d[strtab_off:strtab_off + strtab_size]
        self.sections = {}
        for i in range(e_shnum):
            sh = struct.unpack_from("<IIQQQQIIQQ", d, e_shoff + i * e_shentsize)
            end = shstr.index(b"\0", sh[0])
            name = shstr[sh[0]:end].decode()
            self.sections[name] = {"addr": sh[3], "off": sh[4], "size": sh[5], "flags": sh[2]}

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
        return self.sections[".text"]

    def is_code(self, va):
        t = self.text()
        return t["addr"] <= va < t["addr"] + t["size"]


_md = None


def md():
    global _md
    if _md is None:
        _md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        _md.detail = True
    return _md


def disasm_range(mod, va, size):
    """Fully-resolved instruction list over [va, va+size); stops cleanly at data."""
    fo = mod.v2f(va)
    if fo is None:
        return []
    out = []
    for insn in md().disasm(mod.data[fo:fo + size], va):
        out.append(insn)
    return out


def wild_positions(insns, va_start):
    """Byte offsets (relative to va_start) that are position-dependent and must be wildcarded:
    rip-relative disp32s and call/jmp rel32s. imm8-aware (the 09-25 lesson: for insns with a
    trailing immediate the disp32 ends before it)."""
    import capstone.x86 as X
    wild = set()
    for insn in insns:
        off = insn.address - va_start
        is_branch = insn.group(capstone.x86.X86_GRP_JUMP) or insn.group(capstone.x86.X86_GRP_CALL)
        imm_extra = sum(op.size for op in insn.operands
                        if op.type == X.X86_OP_IMM and not is_branch)
        for op in insn.operands:
            if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                disp_end = off + insn.size - imm_extra
                wild.update(range(disp_end - 4, disp_end))
            if is_branch and insn.size >= 5 and insn.mnemonic in ("call", "jmp", "ljmp"):
                wild.update(range(off + 1, off + 5))
    return wild


def mask_to_pattern(mod, va_start, length, wild):
    d = mod.data
    fo = mod.v2f(va_start)
    pat = [None if i in wild else d[fo + i] for i in range(length)]
    return pat


def pattern_regex(pat):
    return re.compile(b"".join(b"." if p is None else re.escape(bytes([p])) for p in pat), re.S)


def count_matches(mod, pat):
    return [m.start() for m in pattern_regex(pat).finditer(mod.data)]


def find_function_start(mod, va):
    """Walk back over 0xCC padding / ret to the function's first byte."""
    fo = mod.v2f(va)
    d = mod.data
    i = fo
    ccs = 0
    while i > 0:
        i -= 1
        if d[i] == 0xCC:
            ccs += 1
        elif d[i] == 0xC3 and ccs >= 1:
            return mod.f2v(i + 1)
        elif ccs >= 2:
            return mod.f2v(i + 1)
        else:
            ccs = 0
    return None


# --------------------------------------------------------------------------- validate/manifest

def extract_patterns():
    """[(pool, file, typeName, patternString, add, absK)] from the Linux headers.
    Handles both forms: `.add(N)[.abs(K)]` resolvers and plain scan-only patterns."""
    out = []
    for pool, (module, func) in POOLS.items():
        for path in glob_linux_headers():
            txt = open(path).read()
            decls = list(re.finditer(r"add\w*Patterns\(auto", txt))
            for i, dn in enumerate(decls):
                if dn.group(0) != f"{func}(auto":
                    continue
                end = decls[i + 1].start() if i + 1 < len(decls) else len(txt)
                body = txt[dn.start():end]
                for m in re.finditer(r"addPattern<(\w+), CodePattern\{\"([^\"]+)\"\}([^>]*)>", body):
                    ops = m.group(3)
                    add = 0
                    k = 4
                    ma = re.search(r"\.add\((\d+)\)", ops)
                    if ma:
                        add = int(ma.group(1))
                    mk = re.search(r"\.abs\((\d+)\)", ops)
                    if mk:
                        k = int(mk.group(1))
                    out.append({
                        "pool": pool,
                        "file": os.path.basename(path),
                        "type": m.group(1),
                        "pattern": m.group(2),
                        "add": add,
                        "k": k,
                        "hasOp": bool(ma),
                    })
    return out


def glob_linux_headers():
    return [os.path.join(SRC_ROOT, "Linux", f) for f in sorted(os.listdir(os.path.join(SRC_ROOT, "Linux")))
            if f.endswith("Linux.h")]


def cmd_validate(args):
    total = missing = multi = 0
    mods = {}
    report = []
    for p in extract_patterns():
        pool, module, func = p["pool"], *POOLS[p["pool"]]
        if module not in mods:
            mods[module] = Module(MODULES[module])
        mod = mods[module]
        toks = p["pattern"].split()
        rx = pattern_regex([None if t == "?" else int(t, 16) for t in toks])
        hits = [m.start() for m in rx.finditer(mod.data)]
        total += 1
        tag = "OK" if len(hits) == 1 else ("MULTI" if len(hits) > 1 else "MISSING")
        if len(hits) != 1:
            if len(hits) == 0:
                missing += 1
            else:
                multi += 1
            resolved = None
            for h in hits[:2]:
                va = mod.f2v(h)
                if va is None:
                    continue
                fo = mod.v2f(va + p["add"])
                disp, = struct.unpack_from("<i", mod.data, fo)
                resolved = hex(va + p["add"] + p["k"] + disp)
            report.append(f"{tag:7s} {p['file'][:34]:34s} {p['type']:40s} hits={len(hits)} resolved={resolved}")
    for line in report:
        print(line)
    print(f"\nTOTAL={total} MISSING={missing} MULTI={multi}")
    return 0 if missing == 0 and multi == 0 else 1


def cmd_manifest(args):
    entries = {}
    mods = {}
    for p in extract_patterns():
        module = POOLS[p["pool"]][0]
        if module not in mods:
            mods[module] = Module(MODULES[module])
        mod = mods[module]
        toks = p["pattern"].split()
        rx = pattern_regex([None if t == "?" else int(t, 16) for t in toks])
        hits = [m.start() for m in rx.finditer(mod.data)]
        if len(hits) != 1:
            entries[p["type"]] = {"module": module, "status": "BROKEN" if len(hits) == 0 else f"MULTI{len(hits)}"}
            continue
        va = mod.f2v(hits[0])
        fo = mod.v2f(va + p["add"])
        disp, = struct.unpack_from("<i", mod.data, fo)
        entries[p["type"]] = {
            "module": module,
            "status": "ok",
            "site": hex(va),
            "resolved": hex(va + p["add"] + p["k"] + disp),
        }
    data = {
        "binaries": {k: os.path.getmtime(v) for k, v in MODULES.items() if os.path.exists(v)},
        "patterns": entries,
    }
    with open(MANIFEST, "w") as f:
        json.dump(data, f, indent=1)
    ok = sum(1 for v in entries.values() if v["status"] == "ok")
    print(f"manifest written: {MANIFEST} ({ok}/{len(entries)} resolved)")
    for name, v in sorted(entries.items()):
        if v["status"] != "ok":
            print(f"  BROKEN: {name} ({v['status']})")


# --------------------------------------------------------------------------- vtables

def rela_relative_map(mod):
    """addend -> [offsets] and offset -> addend for R_X86_64_RELATIVE in .rela.dyn."""
    sec = mod.sections.get(".rela.dyn")
    if not sec:
        return {}, {}
    n = sec["size"] // 24
    fo = sec["off"]
    addend_map, off_map = {}, {}
    for i in range(n):
        r_offset, r_info, r_addend = struct.unpack_from("<QQq", mod.data, fo + i * 24)
        if (r_info & 0xffffffff) == 8:
            addend_map.setdefault(r_addend, []).append(r_offset)
            off_map[r_offset] = r_addend
    return addend_map, off_map


def typeinfo_name_for(mod, ti_va, addend_map):
    """The mangled name string of a typeinfo object (name ptr at +8, via reloc addend)."""
    fo = mod.v2f(ti_va + 8)
    if fo is None:
        return None
    raw, = struct.unpack_from("<Q", mod.data, fo)
    name_va = raw if raw else None
    if not name_va:
        # PIE: the pointer rides in a relocation
        for off in addend_map.get(ti_va + 8, []):
            pass
        # the addend AT offset ti+8 is the name pointer
        # off_map maps offset -> addend; find via addend list instead
        for a, offs in addend_map.items():
            if ti_va + 8 in offs:
                name_va = a
                break
    if not name_va:
        return None
    fo2 = mod.v2f(name_va)
    if fo2 is None:
        return None
    return mod.data[fo2:fo2 + 160].split(b"\0")[0].decode("ascii", "replace")


def is_trivial_stub(mod, fn_va):
    """trivial: xor eax,eax;ret / ret / mov eax,imm;ret / tiny (<=4 real bytes)."""
    fo = mod.v2f(fn_va)
    if fo is None:
        return True
    b = mod.data[fo:fo + 8]
    if b[:1] == b"\xC3":
        return True
    if b[:3] == b"\x31\xC0\xC3" or b[:3] == b"\x31\xC9\xC3":
        return True
    if b[:2] == b"\x89\xC8" and b[2:3] == b"\xC3":  # mov eax, ecx; ret? unlikely but tiny
        return True
    return False


def fingerprint(mod, fn_va, window=96):
    """Normalized code hash: disassemble up to `window` bytes, zero every wildcarded byte."""
    fo = mod.v2f(fn_va)
    if fo is None:
        return None
    code = mod.data[fo:fo + window]
    insns = list(md().disasm(code, fn_va))
    if not insns:
        return None
    wild = wild_positions(insns, fn_va)
    norm = bytes(0 if i in wild else b for i, b in enumerate(code[:insns[-1].address - fn_va + insns[-1].size]))
    import hashlib
    return hashlib.sha1(norm).hexdigest()[:16]


def scan_vtable(mod, vt0, addend_map, off_map, max_slots=512):
    """The same semantics as VmtLengthCalculator: skip NULL/header pairs, 3-consecutive
    non-code stop, length = last code entry + 1. Uses reloc addends first (PIE-correct),
    falls back to raw file bytes."""
    exec_lo = mod.text()["addr"]
    exec_hi = exec_lo + mod.text()["size"]
    last_code = -1
    run = 0
    trunc = None
    entries = []
    for s in range(max_slots):
        a = off_map.get(vt0 + s * 8)
        fo = mod.v2f(vt0 + s * 8)
        e = a if a is not None else (struct.unpack_from("<Q", mod.data, fo)[0] if fo is not None else 0)
        if mod.is_code(e):
            last_code = s
            run = 0
        else:
            run += 1
            if run >= 3 and last_code >= 0:
                trunc = s
                break
        if s < max_slots:
            entries.append(e)
    length = (last_code + 1) if last_code >= 0 else 0
    return length, trunc, entries[:length + 3]


def collect_classes(mod, addend_map, off_map):
    """Itanium typeinfo discovery: .rodata holds length-prefixed mangled type names
    ("12CClientInput"); a reloc whose addend = the name VA marks the typeinfo object's
    name field (typeinfo+8); relocs whose addend = the typeinfo VA are vtable[-1] entries.
    Verified chain from the 09-25 session (CClientInput/CGameEventManager/CViewRender)."""
    ro = mod.sections[".rodata"]
    rodata = mod.data[ro["off"]:ro["off"] + ro["size"]]
    classes = {}
    name_rx = re.compile(rb"[0-9]{1,3}[A-Z][A-Za-z0-9_]{2,}\x00")
    for m in name_rx.finditer(rodata):
        s = m.group(0)[:-1]
        digits = 0
        while digits < len(s) and 0x30 <= s[digits] <= 0x39:
            digits += 1
        try:
            ln = int(s[:digits])
        except ValueError:
            continue
        if digits == 0 or ln != len(s) - digits or ln < 4:
            continue
        name_va = ro["addr"] + m.start() + (0 if False else 0)
        name_va = ro["addr"] + m.start()
        for t in addend_map.get(name_va, []):
            ti = t - 8
            for v in addend_map.get(ti, []):
                vt0 = v + 8
                ott = off_map.get(vt0 - 16)
                if ott is None:
                    # offset-to-top = 0 has no relocation (file bytes are already zero)
                    fo2 = mod.v2f(vt0 - 16)
                    ott = struct.unpack_from("<Q", mod.data, fo2)[0] if fo2 is not None else None
                classes.setdefault(ti, {"names": set(), "vtables": set()})
                classes[ti]["names"].add(s[digits:].decode("ascii", "replace"))
                classes[ti]["vtables"].add((vt0, ott))
    return classes


def cmd_vtables(args):
    mod = Module(MODULES[args.module])
    addend_map, off_map = rela_relative_map(mod)
    classes = collect_classes(mod, addend_map, off_map)
    want = set(args.classes.split(",")) if args.classes else None
    out = {}
    for ti, info in sorted(classes.items()):
        name = sorted(info["names"])[0]
        if want and not any(w in name for w in want):
            continue
        # the vtable whose offset-to-top == 0 is the primary
        primary = next((vt0 for vt0, ott in sorted(info["vtables"]) if ott == 0), None)
        if primary is None:
            continue
        length, trunc, entries = scan_vtable(mod, primary, addend_map, off_map)
        if length == 0:
            continue
        slots = []
        for s, e in enumerate(entries[:length]):
            fp = fingerprint(mod, e) if mod.is_code(e) else None
            slots.append({"slot": s, "fn": hex(e), "stub": is_trivial_stub(mod, e) if mod.is_code(e) else None, "fp": fp})
        out[name] = {"typeinfo": hex(ti), "primary": hex(primary), "slots": length,
                     "truncAt": trunc, "entries": slots}
    for name, v in sorted(out.items()):
        real = sum(1 for s in v["entries"] if not s["stub"])
        print(f"{name}: primary {v['primary']} slots={v['slots']} real={real} stubs={v['slots'] - real}")
        if args.detail:
            for s in v["entries"]:
                mark = "STUB" if s["stub"] else "    "
                print(f"  {mark} [{s['slot']:3d}] {s['fn']} fp={s['fp']}")
    with open(os.path.join(os.path.dirname(MANIFEST), f"vtables_{args.module}.json"), "w") as f:
        json.dump(out, f, indent=1)
    print(f"written: vtables_{args.module}.json ({len(out)} classes)")


# --------------------------------------------------------------------------- forge

def rip_refs_to(mod, target):
    """All rip-relative disp32 references to `target` anywhere in the mapped image."""
    refs = []
    d = mod.data
    for m in re.finditer(rb"[\x48\x4c\x49\x4d](?:\x8b|\x8d|\x89)[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....", d):
        fo = m.start()
        disp, = struct.unpack_from("<i", d, fo + 3)
        va = mod.f2v(fo)
        if va is None:
            continue
        if va + 7 + disp == target:
            refs.append(va)
    return refs


def forge(mod, target, name, verbose=True):
    refs = rip_refs_to(mod, target)
    if not refs:
        print(f"[forge] no rip-relative reference to 0x{target:x} in {os.path.basename(mod.path)}")
        return None
    if verbose:
        print(f"[forge] {len(refs)} referencing site(s): {[hex(r) for r in refs]}")
    candidates = []
    for ref in refs:
        # the ref must sit inside a real function with a clean instruction boundary
        fn = find_function_start(mod, ref)
        if fn is None:
            continue
        # candidate A: forward from the ref's instruction start
        for start, back in ((ref, 0), (fn, ref - fn)):
            if back > 150:
                continue
            va_start = start
            # disasm [start, start+255) fully-resolved; bail if we hit the fn end early (rare)
            insns = disasm_range(mod, va_start, MAX_PATTERN)
            if not insns or insns[0].address > va_start:
                continue
            # the resolved disp32 we want must be INSIDE the window at a known offset
            disp_off = (ref + 3) - va_start if start == ref else None
            if start == ref:
                pass
            else:
                disp_off = None
                for insn in insns:
                    if insn.address == ref:
                        disp_off = insn.address - va_start + 3
                if disp_off is None:
                    continue
            # try increasing tails until exactly-once
            for length in range(8, MAX_PATTERN + 1):
                if disp_off is None or disp_off + 4 > length:
                    # keep the resolved disp inside the window
                    if length <= disp_off + 4:
                        continue
                insns_w = disasm_range(mod, va_start, length)
                wild = wild_positions(insns_w, va_start)
                pat = mask_to_pattern(mod, va_start, length, wild)
                # never end mid-instruction
                end_ok = any(i.address + i.size == va_start + length for i in insns_w)
                if not end_ok:
                    continue
                hits = count_matches(mod, pat)
                if len(hits) == 1:
                    candidates.append((va_start, length, disp_off, pat, back))
                    break
                if len(hits) == 0:
                    break
        if len(candidates) >= 6:
            break
    if not candidates:
        print("[forge] no exactly-once pattern found within the 255-byte cap")
        return None
    candidates.sort(key=lambda c: c[1])
    va_start, length, disp_off, pat, back = candidates[0]
    s = " ".join("??" if p is None else f"{p:02X}" for p in pat)
    resolved = va_start + disp_off + 4 + struct.unpack_from("<i", mod.data, mod.v2f(va_start + disp_off))[0]
    print(f"\n[forge] pattern for {name}: start 0x{va_start:x} ({length} bytes, ref disp at +{disp_off})")
    print(f"[forge] resolves 0x{resolved:x} via add({disp_off}).abs()")
    print(f'CodePattern{{"{s}"}}.add({disp_off}).abs()')
    return s, disp_off


def forge_direct(mod, target, name, verbose=True):
    """Forge from the target's OWN entry bytes (fn-pointer patterns: the fn is referenced
    only by call rel32s, which rip_refs_to does not see). Wildcards every position-dependent
    byte (rip disp32 / branch rel32, imm8-aware), grows from the minimum clean-insn-boundary
    length until the masked run matches EXACTLY-ONCE, then re-verifies the single match is
    the target itself. No .abs() - fn-pointer patterns have no global to resolve."""
    insns_all = disasm_range(mod, target, MAX_PATTERN)
    if not insns_all or insns_all[0].address != target:
        print(f"[forge] cannot disassemble 0x{target:x} at an instruction boundary")
        return None
    best = None
    for length in range(8, MAX_PATTERN + 1):
        insns = disasm_range(mod, target, length)
        if not insns or insns[-1].address + insns[-1].size < target + length:
            continue  # landed mid-instruction / ran past disassembly
        if not any(i.address + i.size == target + length for i in insns):
            continue
        wild = wild_positions(insns, target)
        pat = mask_to_pattern(mod, target, length, wild)
        hits = count_matches(mod, pat)
        if len(hits) == 1 and mod.f2v(hits[0]) == target:
            best = (length, pat)
            break
        if len(hits) == 0:
            break
    if best is None:
        print("[forge] no exactly-once direct pattern within the 255-byte cap")
        return None
    length, pat = best
    s = " ".join("??" if p is None else f"{p:02X}" for p in pat)
    if verbose:
        print(f"\n[forge] DIRECT pattern for {name}: entry 0x{target:x}, {length} bytes, exactly-once (match == target)")
        print(f"[forge] prologue:")
        for insn in disasm_range(mod, target, length):
            print(f"    0x{insn.address:x}  {insn.mnemonic} {insn.op_str}")
        print(f'CodePattern{{"{s}"}}')
    return s


def cmd_forge(args):
    mod = Module(MODULES[args.module])
    target = int(args.target, 16)
    if args.direct:
        forge_direct(mod, target, args.name)
    else:
        forge(mod, target, args.name or args.target)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("validate", help="check every maintained pattern against the on-disk binaries")
    sub.add_parser("manifest", help="record resolved target VAs into pattern_manifest.json")
    v = sub.add_parser("vtables", help="dump class vtables (extents, stubs, fingerprints)")
    v.add_argument("--module", default="client")
    v.add_argument("--classes", help="comma-separated substrings of class names to include")
    v.add_argument("--detail", action="store_true")
    f = sub.add_parser("forge", help="generate an exactly-once pattern resolving a target VA")
    f.add_argument("--module", default="client")
    f.add_argument("--target", required=True, help="hex VA of the global/function to resolve")
    f.add_argument("--name", default="Unnamed")
    f.add_argument("--direct", action="store_true",
                   help="anchor at the target's own entry bytes (fn-pointer patterns; "
                        "target referenced only by call rel32s). Emits a pattern with no .abs()")
    args = ap.parse_args()
    {"validate": cmd_validate, "manifest": cmd_manifest, "vtables": cmd_vtables, "forge": cmd_forge}[args.cmd](args)


if __name__ == "__main__":
    main()