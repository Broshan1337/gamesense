import struct

pid = 280956
CLIENT = 0x7fb2a4800000
mem = open(f'/proc/{pid}/mem', 'rb', 0)
def rd(a, n):
    mem.seek(a); return mem.read(n)
def u64(a): return struct.unpack('<Q', rd(a, 8))[0]
def u32(a): return struct.unpack('<I', rd(a, 4))[0]
def i32(a): return struct.unpack('<i', rd(a, 4))[0]

es = u64(CLIENT + 0x4918570)
OFF_ATTR = 4632        # C_EconEntity::m_AttributeManager (schema)
OFF_ITEM = 80          # C_AttributeContainer::m_Item (schema) - pointer? or inline?
OFF_DEFIDX = 4290      # C_EconItemView::m_iItemDefinitionIndex (schema)

for label, we in (('knife(C_Knife)', 0x33f278c7000), ('p2000(C_WeaponHKP2000)', 0x33f278e5000)):
    attr = u64(we + OFF_ATTR) if False else we + OFF_ATTR
    # m_AttributeManager is an inline C_AttributeContainer at +4632; m_Item at attr+80
    item_ptr = u64(attr + OFF_ITEM)
    print(f'{label}: weapon={we:#x}')
    print(f'  attrContainer @ +{OFF_ATTR:#x} = {attr:#x}')
    print(f'  m_Item (ptr@+80) = {item_ptr:#x}')
    # m_Item is a CEconItemView* (pointer to an item view, often a shared/static item)
    try:
        defidx = i32(item_ptr + OFF_DEFIDX)
        print(f'  m_iItemDefinitionIndex @ item+{OFF_DEFIDX:#x} = {defidx}')
    except Exception as e:
        print(f'  defidx read failed: {e}')
    # also try treating m_Item as an inline offset (not pointer)
    try:
        defidx2 = i32(attr + OFF_ITEM + OFF_DEFIDX)
        print(f'  (inline-interpretation) defidx = {defidx2}')
    except Exception as e:
        pass
