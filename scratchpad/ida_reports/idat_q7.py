import ida_auto, ida_hexrays, idc
ida_auto.auto_wait()
ida_hexrays.init_hexrays_plugin()
out = open('/tmp/scenesystem_report7.txt', 'w')
for ea, tag in ((0x40c600,'BASE'),(0x3f5610,'MESHBUILDER')):
    cf = str(ida_hexrays.decompile(ea))
    out.write(f'===== {tag} {ea:#x} len={len(cf)} =====\n')
    out.write(cf)
    out.write('\n')
out.close()
print('[q7] done', flush=True)
idc.qexit(0)
