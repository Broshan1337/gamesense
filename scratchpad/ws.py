#!/usr/bin/env python3
"""Wildcard byte-pattern scanner over extracted .text blobs (post-update re-derivation).
Usage: python3 ws.py
"""
import sys

CLIENT_TEXT_VADDR = 0xC78F00   # readelf: [26] .text addr; blob = file bytes of section
PANORAMA_TEXT_VADDR = 0x1DEEC0

def load(path):
    with open(path, 'rb') as f:
        return f.read()

CLIENT = load('/tmp/patscan/libclient.text')
PANORAMA = load('/tmp/patscan/libpanorama.text')

def parse(pat):
    out = []
    for tok in pat.split():
        if tok == '??' or tok == '?':
            out.append(None)
        else:
            out.append(int(tok, 16))
    return bytes(b for b in out if b is not None), [b is None for b in out]

def find_all(blob, pat):
    import re
    toks = pat.split()
    rx = b''.join(b'.' if t in ('?', '??') else re.escape(bytes([int(t, 16)])) for t in toks)
    return [m.start() for m in re.finditer(rx, blob, re.DOTALL)]


def report(name, blob, vaddr_base, pat, ctx=48):
    hits = find_all(blob, pat)
    print(f"{name}: {len(hits)} match(es)")
    for h in hits:
        va = vaddr_base + h
        print(f"  func_start vaddr = 0x{va:X}  (.text+0x{h:X})")
        tail = blob[h:h+ctx]
        print("  next bytes: " + " ".join(f"{b:02X}" for b in tail))
    return hits

def rip_target(match_off, disp_off_in_pattern_is_none, blob, vaddr_base, add_n):
    """resolve .add(N).abs() style: disp32 at match+N, target = match+N+4+disp"""
    mva = vaddr_base + match_off
    p = match_off + add_n
    import struct
    disp = struct.unpack('<i', blob[p:p+4])[0]
    tgt = mva + add_n + 4 + disp
    return disp, tgt

if __name__ == '__main__':
    # --- 1. GameEventManagerGlobalPointer: drop the stale FC anchor ---
    p1 = "55 48 8D 15 ?? ?? ?? ?? 31 C9 48 89 E5 41 54 4C 8D 25 ?? ?? ?? ?? 53 48 89 FB C6 47 08 01 48 89 DE 49 8B 3C 24 48 8B 07 FF 50 20 49 8B 3C 24 C6 43 08 01 48 89 DE 48 8D 15"
    print("=" * 20, "GameEventManagerGlobalPointer (client)")
    h1 = report("p1", CLIENT, CLIENT_TEXT_VADDR, p1)
    if len(h1) == 1:
        d, t = rip_target(h1[0], None, CLIENT, CLIENT_TEXT_VADDR, 18)
        print(f"  add(18).abs(): disp=0x{d & 0xFFFFFFFF:X} -> global @ 0x{t:X}")

    # --- 2. PointerToUpdateSubclass: wildcard the literal E8 71 call tail ---
    p2 = "55 48 89 E5 41 57 41 56 41 55 41 54 49 89 F4 53 48 89 FB 48 81 EC A8 00 00 00"
    print("=" * 20, "PointerToUpdateSubclass (client)")
    h2 = report("p2", CLIENT, CLIENT_TEXT_VADDR, p2)

    # --- 3. RunScriptFunctionPointer (panorama): unchanged pattern ---
    p3 = "55 48 8D 05 ? ? ? ? 48 89 E5 41 57 49 89 ? 41 56 48 8D 3D ? ? ? ? 49 89 F6"
    print("=" * 20, "RunScriptFunctionPointer (panorama)")
    report("p3", PANORAMA, PANORAMA_TEXT_VADDR, p3)
