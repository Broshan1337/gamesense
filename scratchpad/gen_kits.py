import re, sys
from collections import defaultdict

IG = '/tmp/opencode/items_game.txt'
EN = '/tmp/opencode/csgo_english.txt'
OUT = '/path/to/gamesense/Source/CS2/Econ/PaintKitDatabase.h'

d = open(IG, encoding='utf-8', errors='replace').read()
t = open(EN, 'rb').read().decode('utf-8-sig', errors='replace')

tok_cache = {}
def loc(tok):
    if tok is None: return None
    if tok in tok_cache: return tok_cache[tok]
    tok2 = tok[1:] if tok.startswith('#') else tok
    v = None
    m = re.search('"%s"[ \t]+"([^"]*)"' % re.escape(tok2), t)
    if m: v = m.group(1)
    else:
        m = re.search('"%s"[ \t]+"([^"]*)"' % re.escape(tok2), t, re.I)
        if m: v = m.group(1)
    tok_cache[tok] = v
    return v

def match_brace(text, open_pos):
    depth = 1; p = open_pos + 1
    while depth and p < len(text):
        c = text.find('{', p); q = text.find('}', p)
        if q == -1: return -1
        if c != -1 and c < q: depth += 1; p = c + 1
        else: depth -= 1; p = q + 1
    return p

# ---------- ALL paint_kits sections (top-level + crate-scoped) ----------
kits = {}  # id -> dict  (later sections override)
for hm in re.finditer(r'"paint_kits"\s*\n\s*\{', d):
    end = match_brace(d, hm.end() - 1)
    if end < 0: continue
    block = d[hm.end():end-1]
    for m in re.finditer(r'\n\t\t"(\d+)"\n\t\t\{(.*?)\n\t\t\}', block, re.S):
        idx = int(m.group(1)); body = m.group(2)
        n = re.search(r'"name"\t\t"([^"]+)"', body)
        if not n: continue
        wmin = re.search(r'"wear_remap_min"\t\t"([\d.eE+-]+)"', body)
        wmax = re.search(r'"wear_remap_max"\t\t"([\d.eE+-]+)"', body)
        kits[idx] = {
            'name': n.group(1),
            'tag': re.search(r'"description_tag"\t\t"([^"]+)"', body),
            'wmin': float(wmin.group(1)) if wmin else 0.0,
            'wmax': float(wmax.group(1)) if wmax else 1.0,
        }
print("total paint kits:", len(kits))
assert kits.get(568, {}).get('name') == 'am_emerald_marbleized', kits.get(568)
assert kits.get(255, {}).get('name') == 'cu_m4_asimov'
assert kits.get(38, {}).get('name') == 'aa_fade'

# ---------- glove finishes (vmt_path contains paints_gloves) ----------
GLOVE_KIT_PREFIX_TO_DEF = {
    'bloodhound': 5027, 'bloodhound_hydra': 5035, 'sporty': 5030, 'slick': 5031,
    'handwrap': 5032, 'motorcycle': 5033, 'specialist': 5034, 'operation10': 4725,
}
glove_kits = defaultdict(set)
for idx, k in kits.items():
    if idx < 10000: continue
    best = None
    for p, didx in GLOVE_KIT_PREFIX_TO_DEF.items():
        if k['name'] == p or k['name'].startswith(p + '_'):
            if best is None or len(p) > len(best): best = p
    if best is None:
        print("UNMAPPED GLOVE KIT", idx, k['name']); continue
    glove_kits[GLOVE_KIT_PREFIX_TO_DEF[best]].add(idx)
print("glove kits per glove:", {k: len(v) for k, v in sorted(glove_kits.items())})

# ---------- item defs (top-level items section only) ----------
name2def, def2itemname, def2prefab = {}, {}, {}
SEG_START, SEG_END = 118552, 597341
pos = SEG_START
while True:
    m = re.compile(r'\n\t\t"(\d+)"\n\t\t\{').search(d, pos)
    if not m or m.start() >= SEG_END: break
    end = match_brace(d, m.end() - 1)
    body = d[m.end():end-1]
    nm = re.search(r'"name"\t\t"([^"]+)"', body)
    if nm:
        name2def[nm.group(1)] = int(m.group(1))
        inn = re.search(r'"item_name"\t\t"([^"]+)"', body)
        if inn: def2itemname[int(m.group(1))] = inn.group(1)
        pb = re.search(r'"prefab"\t\t"([^"]+)"', body)
        if pb: def2prefab[int(m.group(1))] = pb.group(1)
    pos = m.end()
print("item defs:", len(name2def))

# prefab item_names (baseitem weapons inherit display name from prefab)
prefab_itemname = {}
for hm in re.finditer(r'"prefabs"\s*\n\s*\{', d[:1764888]):
    end = match_brace(d, hm.end() - 1)
    if end < 0: continue
    block = d[hm.end():end-1]
    for m in re.finditer(r'\n\t\t"([a-z0-9_]+)"\n\t\t\{', block):
        end2 = match_brace(block, m.end() - 1)
        if end2 < 0: continue
        body = block[m.end():end2-1]
        inn = re.search(r'"item_name"\t\t"([^"]+)"', body)
        if inn: prefab_itemname[m.group(1)] = inn.group(1)

def weapon_display(didx, wname):
    v = loc(def2itemname.get(didx)) or loc(prefab_itemname.get(wname)) or loc(prefab_itemname.get(def2prefab.get(didx, ''))) or loc(prefab_itemname.get(wname + '_prefab'))
    return v or wname

# ---------- [kit]weapon loot lists (whole file) ----------
weapon_kits = defaultdict(set)
for m in re.finditer(r'\[([a-z0-9_\-\.]+)\]([a-z0-9_]+)"\s+"', d):
    weapon_kits[m.group(2)].add(m.group(1))
print("weapons with kits:", len(weapon_kits))

# ---------- knife finishes ----------
GENERIC_KNIFE_KITS = [
    12,   # Crimson Web
    38,   # Fade
    59,   # Slaughter
    409,  # Tiger Tooth
    410,  # Damascus Steel
    411,  # Damascus Steel (alternate)
    413,  # Marble Fade
    414,  # Rust Coat
    415, 416, 417,  # Doppler Ruby / Sapphire / Black Pearl
    418, 419, 420, 421,  # Doppler Phase 1-4
    568,  # Gamma Doppler (Emerald)
    569, 570, 571, 572,  # Gamma Doppler Phase 1-4
    617, 618, 619,  # rare gem variants (Black Pearl b / Phase 2 b / Sapphire b)
]
# per-model kits extracted & verified from crate-scoped paint_kits sections:
PER_MODEL_KNIFE_KITS = {
    500: [558, 563, 573],                 # Bayonet: Lore, Black Laminate, Autotronic
    505: [559, 564, 574],                 # Flip
    506: [560, 565, 575],                 # Gut
    507: [561, 566, 576],                 # Karambit
    508: [562, 567, 577],                 # M9 Bayonet
    512: [1106, 1111, 1116, 621],         # Falchion (+ Ultraviolet)
    509: [1107, 1112, 1117, 620],         # Huntsman (+ Ultraviolet)
    514: [1104, 1109, 1114],              # Bowie
    515: [1105, 1110, 1115],              # Butterfly
    516: [1108, 1113, 1118],              # Shadow Daggers
}
def display_name(idx, k):
    nm = loc(k['tag'].group(1) if k['tag'] else None)
    if nm is None:
        nm = k['name'].replace('_', ' ').title()
    schema = k['name']
    if idx == 411: extra = ' (Variant)'  # aq_damascus_90 - same display name as 410
    if 'ruby_marbleized' in schema: extra = ' (Ruby)'
    elif 'sapphire_marbleized' in schema: extra = ' (Sapphire)'
    elif 'blackpearl_marbleized' in schema: extra = ' (Black Pearl)'
    elif 'emerald_marbleized' in schema: extra = ' (Emerald)'
    elif 'phase1' in schema: extra = ' (Phase 1)'
    elif 'phase2' in schema: extra = ' (Phase 2)'
    elif 'phase3' in schema: extra = ' (Phase 3)'
    elif 'phase4' in schema: extra = ' (Phase 4)'
    else: extra = ''
    if extra and schema.endswith('_b'):
        extra = extra[:-1] + ', Rare Special)'
    nm += extra
    return nm

# ---------- target weapons ----------
GUNS = [
    (1, 'weapon_deagle'), (2, 'weapon_elite'), (3, 'weapon_fiveseven'), (4, 'weapon_glock'),
    (7, 'weapon_ak47'), (8, 'weapon_aug'), (9, 'weapon_awp'), (10, 'weapon_famas'),
    (11, 'weapon_g3sg1'), (13, 'weapon_galilar'), (14, 'weapon_m249'), (16, 'weapon_m4a1'),
    (17, 'weapon_mac10'), (19, 'weapon_p90'), (23, 'weapon_mp5sd'), (24, 'weapon_ump45'),
    (25, 'weapon_xm1014'), (26, 'weapon_bizon'), (27, 'weapon_mag7'), (28, 'weapon_negev'),
    (29, 'weapon_sawedoff'), (30, 'weapon_tec9'), (32, 'weapon_hkp2000'), (33, 'weapon_mp7'),
    (34, 'weapon_mp9'), (35, 'weapon_nova'), (36, 'weapon_p250'), (38, 'weapon_scar20'),
    (39, 'weapon_sg556'), (40, 'weapon_ssg08'), (60, 'weapon_m4a1_silencer'),
    (61, 'weapon_usp_silencer'), (63, 'weapon_cz75a'), (64, 'weapon_revolver'),
]
KNIVES = [
    (500, 'weapon_bayonet'), (503, 'weapon_knife_css'), (505, 'weapon_knife_flip'),
    (506, 'weapon_knife_gut'), (507, 'weapon_knife_karambit'), (508, 'weapon_knife_m9_bayonet'),
    (509, 'weapon_knife_tactical'), (512, 'weapon_knife_falchion'), (514, 'weapon_knife_survival_bowie'),
    (515, 'weapon_knife_butterfly'), (516, 'weapon_knife_push'), (517, 'weapon_knife_cord'),
    (518, 'weapon_knife_canis'), (519, 'weapon_knife_ursus'), (520, 'weapon_knife_gypsy_jackknife'),
    (521, 'weapon_knife_outdoor'), (522, 'weapon_knife_stiletto'), (523, 'weapon_knife_widowmaker'),
    (525, 'weapon_knife_skeleton'), (526, 'weapon_knife_kukri'),
]
GLOVES = [
    (5027, 'studded_bloodhound_gloves'), (5030, 'sporty_gloves'), (5031, 'slick_gloves'),
    (5032, 'leather_handwraps'), (5033, 'motorcycle_gloves'), (5034, 'specialist_gloves'),
    (5035, 'studded_hydra_gloves'), (4725, 'studded_brokenfang_gloves'),
]
for didx, wname in GUNS + KNIVES + GLOVES:
    if wname not in name2def:
        print("WARN missing def name in schema:", didx, wname)
    elif name2def[wname] != didx:
        print("WARN defindex mismatch:", wname, "schema:", name2def[wname], "expected:", didx)

name_to_id = defaultdict(list)
for i in kits: name_to_id[kits[i]['name']].append(i)
def ids_for_names(names):
    out = []
    for n in sorted(names):
        cands = name_to_id.get(n)
        if cands is None:
            print("WARN kit name missing from paint_kits sections:", n)
            continue
        out.append(cands[0])
    return out

# ---------- build lists ----------
gun_lists = {}
for didx, wname in GUNS:
    ids = ids_for_names(weapon_kits.get(wname, set()))
    if not ids: print("WARN no kits for gun", wname)
    gun_lists[didx] = ids
knife_generic_ids = ids_for_names({kits[i]['name'] for i in GENERIC_KNIFE_KITS if i in kits})
print("generic knife kits:", len(knife_generic_ids))
knife_lists = {}
for didx, wname in KNIVES:
    knife_lists[didx] = list(knife_generic_ids) + sorted(PER_MODEL_KNIFE_KITS.get(didx, []))
glove_lists = {didx: sorted(glove_kits.get(didx, set())) for didx, _ in GLOVES}

# display names for every referenced kit id
referenced = set()
for ids in list(gun_lists.values()) + list(knife_lists.values()) + list(glove_lists.values()):
    referenced |= set(ids)
names = {i: display_name(i, kits[i]) for i in sorted(referenced)}

# duplicate-name check within each list (would be ambiguous in UI)
for didx, ids in list(gun_lists.items()) + list(knife_lists.items()) + list(glove_lists.items()):
    seen = {}
    for i in ids:
        if names[i] in seen:
            print("DUP", didx, i, names[i], "vs kit", seen[names[i]])
        seen[names[i]] = i

def cpp_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

kit_lines = ['    {%d, %s, %.2ff, %.2ff},' % (i, cpp_str(names[i]), kits[i]['wmin'], kits[i]['wmax']) for i in sorted(referenced)]
kit_index_in_table = {i: n for n, i in enumerate(sorted(referenced))}

# per-weapon sorted (by display name) kit index arrays, deduped
sig_map, uniq_arrays, list_rows = {}, [], []
for didx, wname in GUNS + KNIVES + GLOVES:
    ids = (gun_lists | knife_lists | glove_lists)[didx]
    ids = sorted(ids, key=lambda i: (names[i].lower(), i))
    sig = tuple(ids)
    if sig not in sig_map:
        sig_map[sig] = len(uniq_arrays)
        uniq_arrays.append([kit_index_in_table[i] for i in ids])
    list_rows.append((didx, weapon_display(didx, wname), sig_map[sig], len(ids)))
list_rows.sort(key=lambda x: x[0])

hdr = []
hdr.append('// GENERATED from the game\'s own shipped item schema')
hdr.append('// (csgo/pak01_dir.vpk -> scripts/items/items_game.txt, joined against')
hdr.append('// resource/csgo_english.txt). Do not edit by hand - regenerate instead.')
hdr.append('//')
hdr.append('// Kit ids and per-weapon compatibility come from the [kit]weapon loot-list entries;')
hdr.append('// knife finishes from the paint kits referenced only by rare-item loot lists plus')
hdr.append('// per-model finish kits (Lore / Black Laminate / Autotronic / Ultraviolet); glove')
hdr.append('// finishes from the paints_gloves vmt_path kit blocks (prefix -> glove model).')
hdr.append('#pragma once')
hdr.append('')
hdr.append('#include <cstdint>')
hdr.append('')
hdr.append('namespace cs2')
hdr.append('{')
hdr.append('')
hdr.append('struct PaintKitEntry {')
hdr.append('    int id;')
hdr.append('    const char* name;')
hdr.append('    float wearRemapMin;')
hdr.append('    float wearRemapMax;')
hdr.append('};')
hdr.append('')
hdr.append('inline constexpr PaintKitEntry kPaintKits[] = {')
hdr += kit_lines
hdr.append('};')
hdr.append('')
hdr.append('// Per-weapon lists of indices into kPaintKits, each sorted alphabetically by name.')
hdr.append('struct WeaponPaintKitList {')
hdr.append('    std::uint16_t defIndex;')
hdr.append('    const char* weaponName;')
hdr.append('    const std::uint16_t* kitIndices;')
hdr.append('    std::uint16_t kitCount;')
hdr.append('};')
hdr.append('')
for n, arr in enumerate(uniq_arrays):
    hdr.append('inline constexpr std::uint16_t kKitIndices%d[] = {' % n)
    for i in range(0, len(arr), 12):
        hdr.append('    ' + ', '.join(str(x) for x in arr[i:i+12]) + ',')
    hdr.append('};')
hdr.append('')
hdr.append('inline constexpr WeaponPaintKitList kWeaponPaintKitLists[] = {')
for didx, wdisplay, ai, cnt in list_rows:
    hdr.append('    {%d, %s, kKitIndices%d, %d},' % (didx, cpp_str(wdisplay), ai, cnt))
hdr.append('};')
hdr.append('')
hdr.append('[[nodiscard]] inline const WeaponPaintKitList* paintKitListFor(std::uint16_t defIndex)')
hdr.append('{')
hdr.append('    for (const auto& list : kWeaponPaintKitLists)')
hdr.append('        if (list.defIndex == defIndex)')
hdr.append('            return &list;')
hdr.append('    return nullptr;')
hdr.append('}')
hdr.append('')
hdr.append('[[nodiscard]] inline const PaintKitEntry* paintKitFor(std::uint16_t defIndex, int kitId)')
hdr.append('{')
hdr.append('    const auto* list = paintKitListFor(defIndex);')
hdr.append('    if (!list)')
hdr.append('        return nullptr;')
hdr.append('    for (std::uint16_t i = 0; i < list->kitCount; ++i) {')
hdr.append('        const auto& kit = kPaintKits[list->kitIndices[i]];')
hdr.append('        if (kit.id == kitId)')
hdr.append('            return &kit;')
hdr.append('    }')
hdr.append('    return nullptr;')
hdr.append('}')
hdr.append('')
hdr.append('[[nodiscard]] inline const PaintKitEntry* paintKitById(int kitId)')
hdr.append('{')
hdr.append('    for (const auto& kit : kPaintKits)')
hdr.append('        if (kit.id == kitId)')
hdr.append('            return &kit;')
hdr.append('    return nullptr;')
hdr.append('}')
hdr.append('')
hdr.append('}')
hdr.append('')

open(OUT, 'w').write('\n'.join(hdr) + '\n')
print("wrote", OUT, "| kits:", len(kit_lines), "| lists:", len(list_rows), "| unique arrays:", len(uniq_arrays))
for didx, wdisplay, ai, cnt in list_rows:
    print(" ", didx, wdisplay, cnt)
