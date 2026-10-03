#!/usr/bin/env python3
# patfix.py - near-match search for broken CodePatterns after a CS2 update.
#
# For a pattern that no longer byte-matches, this finds the closest candidate sites in
# the new .text: seeds on the longest contiguous non-wildcard run, scores every
# neighbourhood against the full pattern (non-wildcard byte equality), reports the best
# few with a capstone disasm.
#
#   REMOD=client python3 - <<'EOF'
#   import patfix as P; P.report("0F B6 97 ?? ?? ?? ?? 39 F2", top=6)
#   EOF
import os, struct, importlib, rehelp

def seed_of(hexpat):
    toks = hexpat.split()
    best = (0, 0, 0)  # length, start index of run in tokens
    cur_len = 0
    cur_start = 0
    for i, t in enumerate(toks):
        if t not in ('??', '?'):
            if cur_len == 0:
                cur_start = i
            cur_len += 1
            if cur_len > best[0]:
                best = (cur_len, cur_start, i)
        else:
            cur_len = 0
    return best  # (len, start_tok, end_tok_inclusive)

def _to_bytes(hexpat):
    pat = bytearray()
    for t in hexpat.split():
        pat.append(0 if t in ('??', '?') else int(t, 16))
    return bytes(pat)

def candidates(hexpat, mod=None):
    m = mod or rehelp._ensure()
    toks = hexpat.split()
    seed_len, seed_start, seed_end = seed_of(hexpat)
    if seed_len < 4:
        print(f"!! pattern has no >=4-byte non-wildcard run: {hexpat}")
        return []
    seed = _to_bytes(' '.join(toks[seed_start:seed_end + 1]))
    seed_first = toks[seed_start]
    d = m.data
    # locate the seed occurrences in FILE space, map to .text offsets
    out = []
    off = 0
    while True:
        i = d.find(seed, off)
        if i < 0:
            break
        off = i + 1
        # file offset -> vaddr
        va = None
        for base, filesz, off0 in m.loads:
            if off0 <= i < off0 + filesz:
                va = base + (i - off0)
                break
        if va is None:
            continue
        out.append(va)
        if len(out) > 4000:
            break
    return out

def score_at(hexpat, va, mod=None):
    m = mod or rehelp._ensure()
    toks = hexpat.split()
    pat = _to_bytes(hexpat)
    off = m.v2f(va)
    if off is None:
        return 0.0, 0
    need = len(toks)
    got = 0
    match = 0
    for k, t in enumerate(toks):
        if off + k >= len(m.data):
            break
        got += 1
        if t in ('??', '?'):
            match += 1
        elif m.data[off + k] == pat[k]:
            match += 1
    if got == 0:
        return 0.0, 0
    return match / got, got

def report(hexpat, top=6, min_score=0.80, mod=None, ctx=12):
    m = mod or rehelp._ensure()
    toks = hexpat.split()
    len_before = seed_of(hexpat)[1]  # tokens before seed start
    vas = candidates(hexpat, m)
    scored = []
    for va in vas:
        # score the window starting at va - len_before (the pattern may start before the seed)
        start_va = va - len_before
        s, n = score_at(hexpat, start_va, m)
        if s >= min_score:
            scored.append((s, start_va, n))
    scored.sort(reverse=True)
    seen = set()
    shown = 0
    for s, va, n in scored:
        if shown >= top:
            break
        # dedupe overlapping candidates (within 16 bytes)
        if any(abs(va - v) < 16 for v in seen):
            continue
        seen.add(va)
        shown += 1
        print(f"--- candidate @ {va:#x} score {s:.2f}")
        rehelp.pdis(va, min(n, 40))
        print()
    return scored

import rehelp  # noqa: E402