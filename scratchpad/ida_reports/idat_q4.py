import ida_auto, idaapi, idautils, ida_funcs, ida_funcs, idc
ida_auto.auto_wait()
out = open('/tmp/scenesystem_report4.txt', 'w')
def w(*a): print(*a, file=out, flush=True)
def fof(ea): return ida_funcs.get_func(ea)

classes = {
 '16CSceneObjectDesc': 0xa0d030,
 '20CBaseSceneObjectDesc': 0xa0d048,
 '25CAggregateSceneObjectDesc': 0xa0c408,
 '29CInstancedMeshSceneObjectDesc': 0xa0cb90,
 '27CMeshBuilderSceneObjectDesc': 0xa0cdb0,
 '30CProjectedDecalSceneObjectDesc': 0xa0cf18,
 '23CClutterSceneObjectDesc': 0xa0ca48,
 '32CAggregateRTProxySceneObjectDesc': 0xa0c680,
}
# scan data segments for qword == (nameslot-8)
ti_ptrs = {cls: addr - 8 for cls, addr in classes.items()}
seg = idaapi.get_segm_by_name('.data.rel.ro')
ea = seg.start_ea; end = seg.end_ea
hits = {c: [] for c in classes}
while ea < end - 8:
    v = idc.get_qword(ea)
    for cls, tp in ti_ptrs.items():
        if v == tp:
            hits[cls].append(ea)
    ea += 8
for cls, hs in hits.items():
    w('==', cls)
    for h in hs:
        apoint = h + 8
        slots = []
        for i in range(14):
            v = idc.get_qword(apoint + i*8)
            if not (0x1e0000 <= v < 0x600000):
                break
            slots.append((i*8, hex(v)))
        w('   vtbl', hex(h), 'addrpoint', hex(apoint), 'slot+0x20:', [s for s in slots if s[0] == 32], 'all:', slots)
out.close()
print('[q4] done', flush=True)
idc.qexit(0)
