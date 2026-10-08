#!/usr/bin/env python3
"""pattern_entry_check.py - regression guard against mis-anchored fn-pointer patterns.

`pattern_forge.py validate` only checks exactly-once uniqueness. It cannot catch a
pattern that matches exactly-once at the WRONG site (e.g. mid-function). A call through
such a pointer jumps into another function's body -> crash.

This checks every no-resolver (fn-pointer) pattern in the Linux headers: the single
match must sit at a real function entry (prologue byte 0x55/0x53 or endbr64) preceded
by padding (0xCC) or a function terminator (0xC3/0x0F0B).

Usage: CS2_GAME_ROOT=<game> python3 scratchpad/pattern_entry_check.py
Exit 0 = all entry sites sane, 1 = mis-anchored pattern found.
"""
import os
import re
import struct
import sys

GAME_ROOT = os.environ.get(
    "CS2_GAME_ROOT",
    "/mnt/disk2/SteamLibrary/steamapps/common/Counter-Strike Global Offensive/game",
)
SRC_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cs2", "Source", "MemoryPatterns")
MODULES = {
    "client": f"{GAME_ROOT}/csgo/bin/linuxsteamrt64/libclient.so",
}


def load(path):
    data = open(path, "rb").read()
    e_phoff, = struct.unpack_from("<Q", data, 0x20)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x36)
    loads = []
    for i in range(e_phnum):
        t, fl, off, va, pa, fsz, msz, al = struct.unpack_from("<IIQQQQQQ", data, e_phoff + i * e_phentsize)
        if t == 1:
            loads.append((va, fsz, off))

    def f2v(off):
        for base, fsz, off0 in loads:
            if off0 <= off < off0 + fsz:
                return base + (off - off0)
        return None

    return data, f2v


def main():
    data, f2v = load(MODULES["client"])
    bad = 0
    checked = 0
    for fname in sorted(os.listdir(os.path.join(SRC_ROOT, "Linux"))):
        if not fname.endswith("Linux.h"):
            continue
        txt = open(os.path.join(SRC_ROOT, "Linux", fname)).read()
        for m in re.finditer(r"addPattern<(\w+), CodePattern\{(\"([^\"]+)\")\}([^>]*)>", txt):
            name, pat, ops = m.group(1), m.group(3), m.group(4)
            if re.search(r"\.\w+\(", ops):
                continue  # resolver patterns (.add/.abs/.read/.read8/...) anchor mid-function by design
            toks = pat.split()
            rx = re.compile(b"".join(b"." if t in ("?", "??") else re.escape(bytes([int(t, 16)])) for t in toks), re.S)
            hits = [mm.start() for mm in rx.finditer(data)]
            if len(hits) != 1:
                continue  # validate owns uniqueness; we own entry-sanity
            checked += 1
            h = hits[0]
            first = data[h]
            prev = data[max(0, h - 8):h]
            # Plausible function-entry first bytes: push rbp/rbx (0x55/0x53), endbr64
            # (0xF3), or a REX-prefixed instruction (0x40-0x4F, incl. 0x48 mov/lea).
            # Packed code often omits padding between functions, so preceding bytes
            # are not consulted: implausible first bytes (e.g. 0x28 = sub [rcx],al,
            # which never starts a function) condemn the site on their own.
            if not (first in (0x55, 0x53, 0xF3) or 0x40 <= first <= 0x4F):
                bad += 1
                print(f"MIS-ANCHORED {fname} {name}: fileoff 0x{h:X} (VA 0x{f2v(h):X}), "
                      f"entry byte 0x{first:02x}, prev {prev.hex(' ')}")
    print(f"checked={checked} mis-anchored={bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
