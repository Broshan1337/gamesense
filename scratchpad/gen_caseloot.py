import re

IG = '/tmp/opencode/items_game.txt'
OUT = '/path/to/gamesense/Source/CS2/Econ/CaseLootDatabase.h'

d = open(IG, encoding='utf-8', errors='replace').read()

# weapon name -> def index (same tables as gen_kits.py)
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
    (521, 'weapon_knife_outback'), (522, 'weapon_knife_butt_plug'), (523, 'weapon_knife_floss'),
    (524, 'weapon_knife_kukri'), (525, 'weapon_knife_widowmaker'),
]
weapon_def = {name: didx for didx, name in GUNS + KNIVES}

# all paint_kits sections (top-level + crate-scoped): kit name -> id
kit_id = {}
for hm in re.finditer(r'"paint_kits"\s*\r?\n\s*\{', d):
    depth = 1; p = hm.end()
    while depth and p < len(d):
        c = d.find('{', p); q = d.find('}', p)
        if q == -1: break
        if c != -1 and c < q: depth += 1; p = c + 1
        else: depth -= 1; p = q + 1
    block = d[hm.end():p-1]
    for m in re.finditer(r'\n\t\t"(\d+)"\n\t\t\{(.*?)\n\t\t\}', block, re.S):
        nm = re.search(r'"name"\s+"([^"]+)"', m.group(2))
        if nm:
            kit_id.setdefault(nm.group(1), int(m.group(1)))

TIER_RANK = {'common': 0, 'uncommon': 1, 'rare': 2, 'mythical': 3, 'legendary': 4, 'ancient': 5}

# series -> loot list name (all revolving_loot_lists blocks, top-level + nested)
series_to_list = {}
for hm in re.finditer(r'"revolving_loot_lists"\s*\r?\n\s*\{', d):
    depth = 1; p = hm.end()
    while depth and p < len(d):
        c = d.find('{', p); q = d.find('}', p)
        if q == -1: break
        if c != -1 and c < q: depth += 1; p = c + 1
        else: depth -= 1; p = q + 1
    block = d[hm.end():p-1]
    for m in re.finditer(r'"(\d+)"\s+"([a-z0-9_]+)"', block):
        series_to_list.setdefault(int(m.group(1)), m.group(2))

# all client_loot_lists blocks: list name -> group -> raw entries
all_lists = {}
for hm in re.finditer(r'"client_loot_lists"\s*\r?\n\s*\{', d):
    depth = 1; p = hm.end()
    while depth and p < len(d):
        c = d.find('{', p); q = d.find('}', p)
        if q == -1: break
        if c != -1 and c < q: depth += 1; p = c + 1
        else: depth -= 1; p = q + 1
    block = d[hm.end():p-1]
    for gm in re.finditer(r'"([a-z0-9_]+)"\s*\r?\n\s*\{([^{}]*)\}', block):
        list_name, body = gm.group(1), gm.group(2)
        entries = re.findall(r'"(\[[^\]]+\]([a-z0-9_]+))"', body)
        all_lists.setdefault(list_name, {})  # keep first (nested dupes)
        if list_name in all_lists and not all_lists[list_name]:
            all_lists[list_name] = entries

# case defs (prefab weapon_case) -> supply crate series
starts = [(m.group(1), m.start()) for m in re.finditer(r'"(\d{3,5})"\s*\r?\n\s*\{', d)]
case_series = {}
for idx, (defid, p) in enumerate(starts):
    end = starts[idx + 1][1] if idx + 1 < len(starts) else len(d)
    seg = d[p:end]
    if not re.search(r'"prefab"\s+"([^"]*weapon_case)', seg):
        continue
    sm = re.search(r'"set supply crate series"\s*\r?\n\s*\{\s*\r?\n\s*"attribute_class"\s+"supply_crate_series"\s*\r?\n\s*"value"\s+"(\d+)"', seg)
    if sm:
        case_series.setdefault(int(defid), int(sm.group(1)))

def resolve_list(name, depth=0):
    """list name -> [(tierRank, weaponDef, kitId)], following sub-list indirection."""
    if depth > 4 or name not in all_lists:
        return []
    out = []
    for full, wname in all_lists[name]:
        kitm = re.match(r'\[([^\]]+)\]', full)
        if not kitm or wname not in weapon_def or kitm.group(1) not in kit_id:
            continue
        if wname in ('sticker', 'music_kit', 'musickit'):
            continue
        out.append((weapon_def[wname], kit_id[kitm.group(1)]))
    if not out:
        # indirection: entries are other list names
        raw = all_lists[name]
        pass
    return out

# note: all_lists stores entries; indirection needs names - re-scan with names kept
def resolve_list2(name, depth=0):
    if depth > 4:
        return []
    found = None
    for hm in re.finditer(r'"client_loot_lists"\s*\r?\n\s*\{', d):
        blk = d[hm.end():hm.end()+200000]
        gm = re.search('"%s"\s*\r?\n\s*\{([^{}]*)\}' % re.escape(name), blk)
        if gm:
            found = gm.group(1)
            break
    if found is None:
        return []
    out = []
    for m in re.finditer(r'"([^"]+)"\s+"[^"]*"', found):
        key = m.group(1)
        if key.startswith('['):
            kitm = re.match(r'\[([^\]]+)\]([a-z0-9_]+)', key)
            if kitm and kitm.group(2) in weapon_def and kitm.group(1) in kit_id:
                out.append((weapon_def[kitm.group(2)], kit_id[kitm.group(1)]))
        else:
            out.extend(resolve_list2(key, depth + 1))
    return out

loot = {}
missing_series, empty_lists = [], []
for caseDef, series in sorted(case_series.items()):
    list_name = series_to_list.get(series)
    if not list_name:
        missing_series.append((caseDef, series))
        continue
    groups = {}
    for tier in TIER_RANK:
        nm = f"{list_name}_{tier}" if not list_name.endswith('_lootlist') else f"{list_name}_{tier}"
        entries = resolve_list2(nm)
        if entries:
            groups[TIER_RANK[tier]] = sorted(set(entries))
    if groups:
        loot[caseDef] = groups
    else:
        empty_lists.append((caseDef, series, list_name))

print(f"cases with loot: {len(loot)} / {len(case_series)}; no-series: {missing_series}; empty: {empty_lists}")

# ---------- emit ----------
def cpp_str(s):
    return '"%s"' % s.replace('\\', '\\\\').replace('"', '\\"')

hdr = []
hdr.append('// GENERATED from the game\'s own shipped item schema')
hdr.append('// (csgo/pak01_dir.vpk -> scripts/items/items_game.txt: crate "supply crate series"')
hdr.append('// attribute -> revolving_loot_lists -> client_loot_lists, entries "[paintkit]weapon"')
hdr.append('// resolved against the paint_kits + weapon def tables). Do not edit by hand -')
hdr.append('// regenerate with scratchpad/gen_caseloot.py.')
hdr.append('//')
hdr.append('// Tier ranks: 0=common 1=uncommon 2=rare 3=mythical 4=legendary 5=ancient. The runtime')
hdr.append('// maps Valve\'s published odds onto the ranks a crate actually has.')
hdr.append('#pragma once')
hdr.append('')
hdr.append('#include <cstdint>')
hdr.append('')
hdr.append('namespace cs2')
hdr.append('{')
hdr.append('')
hdr.append('struct CaseLootEntry {')
hdr.append('    std::uint16_t weaponDefIndex;')
hdr.append('    std::uint16_t paintKitId;')
hdr.append('};')
hdr.append('')
hdr.append('struct CaseLootGroup {')
hdr.append('    const CaseLootEntry* entries;')
hdr.append('    std::uint16_t entryCount;')
hdr.append('    std::uint8_t tierRank;')
hdr.append('};')
hdr.append('')
hdr.append('struct CaseLoot {')
hdr.append('    std::uint16_t caseDefIndex;')
hdr.append('    const CaseLootGroup* groups;')
hdr.append('    std::uint8_t groupCount;')
hdr.append('};')
hdr.append('')

emitted_groups = []
emitted_entries = []
kase_rows = []
for caseDef, groups in sorted(loot.items()):
    group_rows = []
    for rank in sorted(groups):
        entries = groups[rank]
        arr_name = f"kCaseLootEntries{caseDef}_{rank}"
        hdr.append(f'inline constexpr CaseLootEntry {arr_name}[] = {{')
        for wdef, kit in entries:
            hdr.append(f'    {{{wdef}, {kit}}},')
        hdr.append('};')
        group_rows.append((arr_name, len(entries), rank))
        emitted_entries.extend(entries)
    garr = f"kCaseLootGroups{caseDef}"
    hdr.append(f'inline constexpr CaseLootGroup {garr}[] = {{')
    for arr_name, count, rank in group_rows:
        hdr.append(f'    {{{arr_name}, {count}, {rank}}},')
    hdr.append('};')
    emitted_groups.append(garr)
    kase_rows.append((caseDef, garr, len(group_rows)))

hdr.append('')
hdr.append('inline constexpr CaseLoot kCaseLoot[] = {')
for caseDef, garr, gcount in kase_rows:
    hdr.append(f'    {{{caseDef}, {garr}, {gcount}}},')
hdr.append('};')
hdr.append('')
hdr.append('[[nodiscard]] inline const CaseLoot* caseLootFor(std::uint16_t caseDefIndex) noexcept')
hdr.append('{')
hdr.append('    for (const auto& loot : kCaseLoot) {')
hdr.append('        if (loot.caseDefIndex == caseDefIndex)')
hdr.append('            return &loot;')
hdr.append('    }')
hdr.append('    return nullptr;')
hdr.append('}')
hdr.append('')
hdr.append('}')
hdr.append('')

open(OUT, 'w').write('\n'.join(hdr) + '\n')
total = sum(len(e) for e in [emitted_entries])
print(f"wrote {OUT}: {len(kase_rows)} crates, {len(emitted_entries)} loot entries")
