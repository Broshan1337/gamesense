#!/usr/bin/env python3
# Maps the pattern POOL index (as printed by pattern_scan.cpp) back to its source
# pattern name + literal, by replaying the MemoryPatterns.h ADD_PATTERNS order over
# the Linux pattern headers. Usage: python3 pool_map.py > /tmp/opencode/pool_map.txt
import re, glob, os

ROOT = os.path.expanduser('~/Desktop/Gamesense-master/cs2/Source/MemoryPatterns')
mem = open(f'{ROOT}/MemoryPatterns.h').read()

# each consteval block: #define ADD_PATTERNS(patterns) addPatterns([]... patterns::addXxx(patternPool) ...)
# followed by .ADD_PATTERNS(GroupName) lines
pools = []  # (poolname, [groupnames])
cur = None
for line in mem.splitlines():
    m = re.search(r'patterns::add(\w+)\(patternPool\)', line)
    if m and 'define' in line:
        method = 'add' + re.search(r'patterns::add(\w+)\(patternPool\)', line).group(1)
        cur = {'method': method, 'groups': []}
        continue
    if cur is not None and '.ADD_PATTERNS(' in line:
        cur['groups'].append(re.search(r'\.ADD_PATTERNS\((\w+)\)', line).group(1))
    if cur is not None and 'return PatternPool' in line:
        pools.append(cur)
        cur = None

# group name -> header file + ordered pattern literals
def group_patterns(group):
    header = glob.glob(f'{ROOT}/Linux/{group}sLinux.h') or glob.glob(f'{ROOT}/Linux/{group}Linux.h')
    if not header:
        return None
    src = open(header[0]).read()
    out = []
    for m in re.finditer(r'template addPattern<(\w+), CodePattern\{"([^"]+)"\}([^>]*)>', src):
        out.append((m.group(1), m.group(2)))
    return out

index = 0
for pool in pools:
    method = pool['method']
    print(f"### pool {pool['groups'][0] if False else method.replace('add','').replace('Patterns','')} groups={pool['groups']}")
    for g in pool['groups']:
        pats = group_patterns(g)
        if pats is None:
            print(f'  !! no patterns parsed for {g}')
            continue
        for (name, pat) in pats:
            print(f'{index:4d} {g:28s} {name:42s} {pat}')
            index += 1