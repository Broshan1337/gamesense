import ida_auto, idaapi, idautils, ida_funcs, idc
ida_auto.auto_wait()
import ida_hexrays
HAVE_HX = ida_hexrays.init_hexrays_plugin()
out = open('/tmp/scenesystem_report5.txt', 'w')
def w(*a): print(*a, file=out, flush=True)
def fof(ea): return ida_funcs.get_func(ea)

# find call [reg+20h] sites; keep small funcs; decompile and grep for 4-arg dispatch
cand_funcs = set()
seg = idaapi.get_segm_by_name('.text')
ea = seg.start_ea; end = seg.end_ea
while ea < end:
    dis = idc.GetDisasm(ea)
    if dis.lower().startswith('call') and '+20h]' in dis:
        f = fof(ea)
        if f and (f.end_ea - f.start_ea) < 0x1200:
            cand_funcs.add(f.start_ea)
    ea = idc.next_head(ea, end)
w('candidate funcs:', len(cand_funcs))
for fst in sorted(cand_funcs):
    if not HAVE_HX: break
    try:
        cf = str(ida_hexrays.decompile(fst))
    except Exception:
        continue
    # dispatcher shape: virtual call via +32 with 4+ hexrays args on the same line
    lines = cf.splitlines()
    hits = [l.strip() for l in lines if '+ 32LL' in l and '(' in l]
    if not hits:
        continue
    sig = lines[0] if lines else ''
    w('=== FUNC', hex(fst), sig[:150])
    for h in hits[:6]:
        w('   ', h[:180])
out.close()
print('[q5] done', flush=True)
idc.qexit(0)
