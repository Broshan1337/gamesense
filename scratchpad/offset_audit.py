#!/usr/bin/env python3
"""offset_audit.py - regression guard for hardcoded game-layout constants.

Most entity offsets in this tree resolve at runtime via the SchemaSystem
(update-proof). A few are hardcoded numbers in cs2/Source. When a game update
moves those fields, the code silently reads the wrong memory. This script parses
the hardcoded constants out of the headers and compares them against the
cs2-dumper schema snapshots (sept23/sept26/oct3 must ALL agree - a value stable
across three builds is safe to hardcode; anything else must go through schema).

Usage: python3 scratchpad/offset_audit.py
Exit 0 = all constants match stable dumper values, 1 = stale constant found.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "cs2", "Source")
DUMPER = "/home/d/dev/neversnooze/cs2dumper-for-linux"
SNAPSHOTS = [
    "output_sept23_backup/libclient_so.json",
    "output_sept26_68f386a6_backup/libclient_so.json",
    "output/libclient_so.json",
]

# (source file, constant name, class, field) - ground truth comes from the dumper.
CHECKS = [
    ("GameClient/Entities/BaseWeapon.h", "kNumBulletsOffset", "CCSWeaponBaseVData", "m_nNumBullets"),
    ("GameClient/Entities/BaseWeapon.h", "kRecoilIndexOffset", "C_CSWeaponBase", "m_flRecoilIndex"),
]


def dumper_values(cls, field):
    vals = set()
    for snap in SNAPSHOTS:
        d = json.load(open(os.path.join(DUMPER, snap)))["libclient.so"]["classes"]
        vals.add(d[cls]["fields"][field])
    return vals


def code_value(src_rel, const):
    txt = open(os.path.join(SRC, src_rel)).read()
    m = re.search(rf"{const}\s*=\s*(0x[0-9a-fA-F]+|\d+)", txt)
    if not m:
        print(f"NOT-FOUND {src_rel} {const}: constant missing from source")
        return None
    return int(m.group(1), 0)


def main():
    bad = 0
    for src_rel, const, cls, field in CHECKS:
        vals = dumper_values(cls, field)
        code = code_value(src_rel, const)
        if code is None:
            bad += 1
            continue
        if len(vals) != 1:
            print(f"UNSTABLE {cls}.{field}: dumper snapshots disagree {sorted(hex(v) for v in vals)} - "
                  f"do not hardcode, resolve via schema")
            bad += 1
            continue
        want = next(iter(vals))
        tag = "OK" if code == want else "STALE"
        if code != want:
            bad += 1
        print(f"{tag:6s} {const} = {hex(code)} (dumper {cls}.{field} = {hex(want)})")
    print(f"checked={len(CHECKS)} stale={bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
