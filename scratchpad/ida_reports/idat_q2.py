import ida_auto, idaapi, idautils, ida_funcs, ida_name, idc
ida_auto.auto_wait()
import ida_hexrays
HAVE_HX = ida_hexrays.init_hexrays_plugin()
out = open('/tmp/scenesystem_report2.txt', 'w')
def w(*a): print(*a, file=out, flush=True)
def fof(ea): return ida_funcs.get_func(ea)
def fname(ea):
    n = ida_name.get_ea_name(ea); return n if n else f"sub_{ea:X}"

# all indirect calls with disp 0x20 through reg
seg = idaapi.get_segm_by_name('.text')
ea = seg.start_ea; end = seg.end_ea
import collections
byf = collections.defaultdict(list)
while ea < end:
    dis = idc.GetDisasm(ea)
    if dis.lower().startswith('call') and '[' in dis:
        if '+20h]' in dis:
            f = fof(ea)
            byf[f.start_ea if f else 0].append((ea, dis))
    ea = idc.next_head(ea, end)
w("=== call[+0x20] sites by function ===")
for fst in sorted(byf):
    w(hex(fst), fname(fst), "sites:", [(hex(a), op) for a, op in byf[fst]][:6])

def dec(ea, tag):
    if not HAVE_HX: return
    f = fof(ea)
    if not f: return
    try:
        w(f"===== DECOMPILE {tag} {hex(f.start_ea)} =====")
        w(str(ida_hexrays.decompile(f.start_ea)))
    except Exception as e:
        w("[dec fail]", hex(ea), repr(e))
dec(0x3BFAE0, 'CAgg_slot20')
out.close()
print("[q2] done", flush=True)
idc.qexit(0)
