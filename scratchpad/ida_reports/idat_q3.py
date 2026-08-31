import ida_auto, idaapi, idautils, ida_funcs, ida_name, idc
ida_auto.auto_wait()
import ida_hexrays
HAVE_HX = ida_hexrays.init_hexrays_plugin()
out = open('/tmp/scenesystem_report3.txt', 'w')
def w(*a): print(*a, file=out, flush=True)
def fof(ea): return ida_funcs.get_func(ea)

# 1. find typeinfo-name strings and their typeinfo objs -> vtable headers -> slot +0x20
classes = ['16CSceneObjectDesc','20CBaseSceneObjectDesc','25CAggregateSceneObjectDesc',
           '29CInstancedMeshSceneObjectDesc','27CMeshBuilderSceneObjectDesc',
           '30CProjectedDecalSceneObjectDesc','23CClutterSceneObjectDesc',
           '29CAggregateRTProxySceneObjectDesc','32CAggregateRTProxySceneObjectDesc']

for cls in classes:
    # find string
    sea = None
    for s in idautils.Strings():
        if str(s) == cls:
            sea = s.ea; break
    if sea is None:
        w(cls, "STRING NOT FOUND"); continue
    # typeinfo obj: whoever points AT this string (data xref)
    ti_obj = None
    for xr in idautils.XrefsTo(sea):
        ti_obj = xr.frm; break
    if ti_obj is None:
        w(cls, "no typeinfo obj found"); continue
    w(f"== {cls}: str {sea:#x} typeinfo {ti_obj:#x}")
    # vtable headers point at typeinfo obj (at vtbl_hdr+8)
    hdrs = set()
    for xr in idautils.XrefsTo(ti_obj):
        hdrs.add(xr.frm - 8)
        hdrs.add(xr.frm)
    for h in sorted(hdrs):
        # read slots
        slots = []
        for i in range(12):
            v = idc.get_qword(h + 8 + i*8)
            f = fof(v)
            if v == 0 or (f is None and v < 0x100000):
                break
            slots.append((i*8, v))
        w("  vtable-hdr", hex(h), "slots:", [(hex(o), hex(v)) for o, v in slots[:12]])
out.close()
print('[q3] done', flush=True)
idc.qexit(0)
