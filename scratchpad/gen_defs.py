import os
import re

IG = '/tmp/opencode/items_game.txt'
EN = '/tmp/opencode/csgo_english.txt'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'cs2', 'Source', 'CS2', 'Econ', 'ItemDefDatabase.h')

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

def cpp_str(s):
    return '"%s"' % s.replace('\\', '\\\\').replace('"', '\\"')

# ---------- exact per-def blocks (segment ends at the next def) ----------
starts = [(m.group(1), m.start()) for m in re.finditer(r'"(\d{3,5})"\s*\r?\n\s*\{', d)]

# prefab table: name -> item_name token (defs inherit the display token from prefabs)
prefab_tokens = {}
pm = re.search(r'"prefabs"\s*\r?\n\s*\{', d)
if pm:
    pend = d.index('\n\t}', pm.end())
    for m in re.finditer(r'"([a-z0-9_]+)"\s*\r?\n\s*\{(.*?)\n\t\t\}', d[pm.end():pend], re.S):
        inn = re.search(r'"item_name"\s+"([^"]+)"', m.group(2))
        if inn:
            prefab_tokens[m.group(1)] = inn.group(1)

cases, keys, agents = {}, {}, {}
for idx, (defid, p) in enumerate(starts):
    end = starts[idx + 1][1] if idx + 1 < len(starts) else len(d)
    seg = d[p:end]
    pref = re.search(r'"prefab"\s+"([^"]+)"', seg)
    words = (pref.group(1) if pref else '').split()
    name = re.search(r'"name"\s+"([^"]+)"', seg)
    n = name.group(1) if name else '?'
    inn = re.search(r'"item_name"\s+"([^"]+)"', seg)
    model = re.search(r'"model_player"\s+"([^"]+)"', seg)
    model_path = model.group(1) if model else ''

    def display():
        # def item_name -> prefab chain item_name -> raw name
        if inn:
            v = loc(inn.group(1))
            if v: return v
        for w in words:
            v = loc(prefab_tokens.get(w))
            if v: return v
        return n

    if 'weapon_case_key' in words:
        # skins bizarrely share this prefab; real keys have a Key item_name token or none
        if not inn or 'key' in inn.group(1).lower():
            keys.setdefault(int(defid), (display(), ''))
    elif 'weapon_case' in words:
        cases.setdefault(int(defid), (display(), ''))
    elif any(w.startswith('customplayer') for w in words):
        agents.setdefault(int(defid), (display(), model_path))

def emit(name, table):
    rows = sorted(table.items(), key=lambda kv: kv[1][0].lower())
    lines = ['inline constexpr ItemDefEntry %s[] = {' % name]
    for defid, (disp, model) in rows:
        lines.append('    {%d, %s, %s},' % (defid, cpp_str(disp), cpp_str(model)))
    lines.append('};')
    return rows, lines

case_rows, case_lines = emit('kCaseItems', cases)
key_rows, key_lines = emit('kKeyItems', keys)
agent_rows, agent_lines = emit('kAgentItems', agents)

hdr = []
hdr.append('// GENERATED from the game\'s own shipped item schema')
hdr.append('// (csgo/pak01_dir.vpk -> scripts/items/items_game.txt, joined against')
hdr.append('// resource/csgo_english.txt). Do not edit by hand - regenerate instead')
hdr.append('// (scratchpad/gen_defs.py).')
hdr.append('//')
hdr.append('// Cases = defs with prefab "weapon_case"; keys = defs with prefab')
hdr.append('// "weapon_case_key" that are actual keys (several skins share that prefab).')
hdr.append('// NOTE: CS2\'s def space is reshuffled vs CSGO - 5001 is a Premier medal here,')
hdr.append('// the classic key is 1203. Always verify defs against items_game.txt.')
hdr.append('#pragma once')
hdr.append('')
hdr.append('#include <cstdint>')
hdr.append('')
hdr.append('namespace cs2')
hdr.append('{')
hdr.append('')
hdr.append('struct ItemDefEntry {')
hdr.append('    std::uint16_t defIndex;')
hdr.append('    const char* name;')
hdr.append('    const char* model; // "model_player" (agents only; empty elsewhere)')
hdr.append('};')
hdr.append('')
hdr += case_lines
hdr.append('')
hdr += key_lines
hdr.append('')
hdr += agent_lines
hdr.append('')
hdr.append('[[nodiscard]] inline const ItemDefEntry* itemDefById(const ItemDefEntry* list, int count, std::uint16_t defIndex) noexcept')
hdr.append('{')
hdr.append('    for (int i = 0; i < count; ++i) {')
hdr.append('        if (list[i].defIndex == defIndex)')
hdr.append('            return &list[i];')
hdr.append('    }')
hdr.append('    return nullptr;')
hdr.append('}')
hdr.append('')
hdr.append('}')
hdr.append('')

open(OUT, 'w').write('\n'.join(hdr) + '\n')
print(f"wrote {OUT}: {len(case_rows)} cases, {len(key_rows)} keys, {len(agent_rows)} agents")
for k in sorted(cases, key=int)[:8]: print("  case", k, cases[k])
print("  ...")
for k in sorted(keys, key=int)[:6]: print("  key", k, keys[k])
