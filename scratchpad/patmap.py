#!/usr/bin/env python3
# Maps the pattern POOL hex (as printed by pattern_scan.cpp BAD lines) back to its source
# pattern by normalizing the source literals. Handles leading-wildcard trimming: a source
# pattern whose normalized bytes CONTAIN the pool hex (with 3f wildcard bytes aligning)
# matches. Prints: pool_hex -> group/name/offset/literal.
import re, glob, os, sys

ROOT = os.path.expanduser('~/Desktop/Gamesense-master/cs2/Source/MemoryPatterns')

def normalize(literal):
    toks = literal.split()
    out = []
    for t in toks:
        if t in ('?', '??'):
            out.append('3f')
        else:
            out.append(f'{int(t, 16):02x}')
    return ''.join(out)

rows = []
for header in sorted(glob.glob(f'{ROOT}/Linux/*Linux.h')):
    src = open(header).read()
    group = os.path.basename(header).replace('Linux.h', '')
    for m in re.finditer(r'template addPattern<(\w+), CodePattern\{"([^"]+)"\}([^>]*)>', src):
        name, lit, ops = m.group(1), m.group(2), m.group(3)
        rows.append((group, name, normalize(lit), lit, ops.strip()))

print(f"{len(rows)} source patterns loaded")
for arg in sys.argv[1:]:
    target = arg.lower()
    matches = [(g, n, h, lit, ops) for (g, n, h, lit, ops) in rows
               if target in h or h in target]
    print(f"--- {arg}")
    if not matches:
        print("  NO SOURCE MATCH")
    for (g, n, h, lit, ops) in matches[:6]:
        print(f"  {g}/{n:44s} {lit}  {ops}  (norm {h})")