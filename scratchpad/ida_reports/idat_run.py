import ida_auto, idaapi, idautils, ida_funcs, ida_name, idc
print("[run] waiting for autoanalysis...", flush=True)
ida_auto.auto_wait()
print("[run] analysis done", flush=True)

try:
    import ida_hexrays
    HAVE_HX = ida_hexrays.init_hexrays_plugin()
except Exception:
    HAVE_HX = False
print("[run] hexrays:", HAVE_HX, flush=True)

out = open('/tmp/scenesystem_report.txt', 'w')
def w(*a):
    print(*a, file=out, flush=True)

def fof(ea):
    return ida_funcs.get_func(ea)

def fname(ea):
    n = ida_name.get_ea_name(ea)
    return n if n else f"sub_{ea:X}"

warn_fn = None
for s in idautils.Strings():
    st = str(s)
    if 'Cannot generate primitives' in st:
        w("WARNSTR", hex(s.ea), st[:80])
        for xr in idautils.XrefsTo(s.ea):
            f = fof(xr.frm)
            if f:
                w("  xref", hex(xr.frm), "-> func", hex(f.start_ea), "-", hex(f.end_ea))
                warn_fn = warn_fn or f.start_ea
            else:
                w("  xref", hex(xr.frm), "NO FUNC")

known = {
    0x3C2C80: 'CAgg_DrawArray_guess',
    0x3E2460: 'CInst_DrawArray_guess',
    0x3A9CA0: 'PrimitiveSort',
    0x3BD680: 'SubmitDriver',
}
for ea, nm in known.items():
    f = fof(ea)
    if f:
        w("KNOWN", nm, hex(ea), "range:", hex(f.start_ea), "-", hex(f.end_ea))
        # callers via xrefs
        seen = set()
        for xr in idautils.XrefsTo(f.start_ea):
            cf = fof(xr.frm)
            if cf and cf.start_ea not in seen:
                seen.add(cf.start_ea)
                w("   caller", hex(cf.start_ea), "site", hex(xr.frm))

def decompile_func(ea, tag):
    if not HAVE_HX:
        return
    f = fof(ea)
    if not f:
        w("[dec] no func", hex(ea)); return
    try:
        cf = ida_hexrays.decompile(f.start_ea)
        w(f"===== DECOMPILE {tag} {hex(f.start_ea)} =====")
        w(str(cf))
    except Exception as e:
        w("[dec] FAILED", hex(ea), repr(e))

decompile_func(warn_fn, 'WARN-FN') if warn_fn else w("[dec] no warn fn")
decompile_func(0x3BD680, 'SubmitDriver')
decompile_func(0x3C2C80, 'CAgg_DrawArray')

# indirect calls through slot +8 in core region
import collections
byf = collections.defaultdict(list)
seg = idaapi.get_segm_by_name('.text')
ea2 = seg.start_ea
end = seg.end_ea
while ea2 < end:
    m = idaapi.print_insn_mnemonic(ea2)
    if m.startswith('call'):
        op = idaapi.print_operand(ea2, 0)
        if '+ 8]' in op or '+8]' in op:
            f = fof(ea2)
            byf[f.start_ea if f else 0].append(ea2)
    ea2 = idc.next_head(ea2, end)
for fst in sorted(byf):
    if fst and 0x280000 <= fst < 0x430000:
        w(f"CALL+8 {hex(fst)} ({fname(fst)}): {[hex(a) for a in byf[fst]][:8]}")

out.close()
print("[run] report written", flush=True)
idc.qexit(0)
