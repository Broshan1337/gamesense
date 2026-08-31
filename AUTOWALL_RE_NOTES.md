# Autowall RE notes — Linux libclient.so bullet-penetration port

Goal: full velocity-parity autowall for the rage aimbot = call/simulate the game's real
bullet-penetration machinery instead of the triggerbot's two-trace thickness heuristic.
Reference implementation: `FORFUTURETESTS/velocity-main/cs2/velocity-cs2/project/core/features/combat/impl/shared.cpp`
(`penetration::prepare/run/can`) + gate shape in `impl/legit.cpp:275-300`
(`visible = !pen.penetrated; blocked requires cfg.autowall && pen.damage >= cfg.min_damage`).

## Toolchain that works (proven this session)

- IDA Pro 9.2 headless: `~/ida-pro-9.2/idat -A -S'/path/script.py' <lib>.so.i64` — runs IDAPython,
  exits cleanly with `idaapi.qexit(0)`. ~15-30 s per pass even on the 700 MB client DB.
- Existing IDA DBs (do NOT regenerate):
  - `game/csgo/bin/linuxsteamrt64/libclient.so.i64` (+ .id0/.id1/.id2/.nam/.til)
  - `game/bin/linuxsteamrt64/libengine2.so.*`, libscenesystem, libschemasystem, libSDL3
- Working-dir scratch dumps: `/tmp/re/*.asm` (regenerate via the scripts above if wiped).
- `strings -a` on libclient.so is a goldmine for convar/schema anchors.
- No radare2/rizin installed; objdump/readelf/nm/gdb available.

## Confirmed baseline (our own verified runtime offsets, cross-checked in the IDA DB)

From `Source/GameClient/Tracing/Tracing.h` — all present in the DB as functions/globals:
- `sub_169FBC0` = CGameTraceManager::TraceShape
- trace-manager pointer holder `off_4562230`; filter vtable `off_42C4338`
- `sub_169E3E0` = C_BaseEntity* -> packed handle
- GameTrace result layout: hitEntity +0x08, endPos +0x84, normal +0x90, fraction +0xAC, size 0x140

## CRITICAL FINDING (2026-08-25): Aug-25 update (build 14177) shifted everything again

Handled fully offline this time (no IDA) - see memory `reference_pattern_porting.md` for the full
trail and the new `scratchpad/*.py` toolkit. Summary of current-binary values:
- Tracing.h: kTraceShapeOffset **0x16BCCC0**, kTraceManagerOffset **0x45849F0** (.data),
  kFilterVtableOffset **0x42E48D8** (.data.rel.ro), kEntityToHandleOffset **0x16BB4E0**
  (trace-caller region moved ~+0x1CAC0; .data grew ~+0x22000 vs Aug-20).
- Autowall.h: ConVar-getter **0x150DE60**, ff_damage_bullet_penetration object **0x47E91F0**
  (reduction object = 0x47E9210). Combat region DID move this time (unlike Aug-20).
- AttackButton 0x47F8A50: still NOT statically re-derivable; live-check before trusting ForceShot
  (echo *(u32*)(base+0x47F8A50) while tapping LMB - expect 0x00010001 / 0x01000100).
- Patterns: 3 broken (GameEventManagerGlobalPointer, PointerToUpdateSubclass, RunScriptFunctionPointer),
  all fixed; pattern_scan clean across all pools.

## CRITICAL FINDING (2026-08-24): Aug-20 game update shifted addresses

The IDA libclient DB predates the 2026-08-20 Steam update -> STALE. Verified consequences:
- rodata strings shifted +0x20; cvar/data globals +0x600 (e.g. ff_damage_reduction_bullets
  object 0x47C4810 -> **0x47C4E10**, ff_damage_bullet_penetration 0x47C47F0 -> **0x47C4DF0**);
  some later .text regions +0x600..+0x640 (TraceShape etc.), but code in the 0x14Ex-0x153x
  combat region did NOT move (sub_14FDBA0, sub_14EF560, sub_1529E20 confirmed byte-identical
  at old addresses on the current binary).
- **Tracing.h had gone stale**: old kTraceShapeOffset 0x169FBC0 now sits between int3 padding.
  FIXED in Tracing.h (fail-open design had masked it):
  - kTraceShapeOffset      0x169FBC0 -> **0x16A0200**
  - kTraceManagerOffset    0x4562230 -> **0x4562830**
  - kFilterVtableOffset    0x42C4338 -> **0x42C4938**
  - kEntityToHandleOffset  0x169E3E0 -> **0x169EA20**
  - (trace-result ctor 0x245E840 -> 0x245EE40, for reference)
  GameTrace layout itself UNCHANGED (fraction still +0xAC, flag bytes +0xBA/+0xBB seen live).
- ELF gotcha: libclient.so maps .text at vaddr = file offset + 0x1000 (data +0x2000/+0x3000).
  Raw file reads must convert; objdump prints vaddrs. Filter layout re-confirmed byte-exact at
  f108e1-f10995 (vtable@+0, mask@+8 e.g. 0xC1001, skip handles@+0x20 = 0xFF..FF,
  flags qword@+0x30 = 0x100FFFF00000000 pattern).
- IDA headless now CRASHES on this DB ("free(): invalid size") even after deleting derived
  files. Ground truth = objdump on current .so: full dump kept at
  /mnt/disk2/scratch/re/client.asm.gz (87 MB gz). Regenerate:
  objdump -d --no-show-raw-insn libclient.so | gzip > client.asm.gz

## Current-binary anchor chain (all verified on today's .so)

- ConVar registration blob @0x1514C40 unchanged; name strings b49875/b0eda4.
- ConVar objects: reduction=0x47C4E10, bullet_penetration=0x47C4DF0.
- Readers (code addrs unchanged): 0x14EF8E0 (reduction), **0x14FE42C + 0x14FE486 (both,
  inside pen-math fn sub_14FDBA0)**.
- sub_1529E20 = FireBullets-style entry (huge frame 0x28A8, float-heavy ABI, uses mask
  0x1C300B @0x152A0ED); its callee old-sub_2317350 moved to ~0x2317950.


## Key structural finding — velocity's struct IS this game struct

In `sub_14FDBA0` (rdi = big trace-data struct, rdx = per-hit record):
- `[rdi+8]` = contact array, stride **0x38**, entry fields +0x2C (serial int), +0x30 (word), +0x33 (byte)
- record fields used: `[rdx+10h]` word contact idx (`& 0x7FFF`), `[rdx+12h]` word second idx,
  `[rdx+14h]` bit0 flag — **exactly** velocity's `bullet_trace_record`
  (enter_contact_ix@0x10, exit_contact_ix@0x12, can_penetrate@0x14 bit0, damage_applied@0x08,
  float@0x04 == 1.0 exit check).
⇒ velocity's magic offsets (`trace_ptr + 0x1820` num_hits, `+0x1828` hit_array, `+0x8`
surface/contact array) are fields of THIS SAME game struct. The Linux port mostly needs Linux
addresses of the two engine functions velocity patterns on Windows:
- their `trace_bullet_data_init` (ctor/init of the big struct)
- their `trace_bullet` — called `(trace*, float damage, float penetration, float range_modifier, int 4, int local_team, 0)`
- helper around both: `make_filter(local_pawn, 0x1c300b, 3, 15)`

## `sub_14FDBA0` internals mapped so far

- Entity-handle resolution identical to ours: `qword_45AEE00` entity system, `handle >> 9 & 0x3F`,
  `imul 0x70` node array, serial compare at +0x10 (`sub_149D4D0` does the same 4×).
- Virtual calls: `[ent_vtable+0x50]`; global `unk_48B0BF8` -> `[vtbl+0xC0]` -> `+0x20` -> feeds
  **`sub_169F590`** (sibling of TraceShape @169FBC0 — likely specialized bullet/surface trace;
  returns ptr with +0x14 word, +0x08 float).
- Paired enter/exit: two buffers initialized by **`sub_245E840`** (known trace-result ctor) then
  **`sub_14EAAD0`(trace_struct, result_buf, xmm0=float, edx=contact_idx*0x38)** ×2 =
  compute enter/exit positions for a contact pair.
- Damage-math tail uses `xmmword_AFA6F0[+8] / distance` and both ff_damage convars.
- Other helpers: `sub_14FDA80`, `sub_14EA6A0`, `sub_14F13A0` (×3), `sub_C70D10`.
- Multi-trace neighbor: `sub_149D4D0` (4× TraceShape, masks 0xC3001/0x4900).

## PEN FORMULA DECODED (from sub_14FDBA0 @0x14FE1A8-0x14FE382, current binary)

Per-shot context struct (r9 in fn): +0x00 float currentDamage, +0x04 float penetrationPower,
+0x08/+0x0C floats (range-ish), +0x10 int penetrationsLeft, +0x14 byte flag.
Per wall layer (generic branch; ff_pen = ff_damage_bullet_penetration GetFloat):

    thickness = |exitPos - enterPos|                      // sqrt of squared delta @14FE1EE
    loss = max(0, 3.75 / penetrationPower) * (3.0 / ff_pen)
         + 0.16 * currentDamage                           // 0.16 = rodata AFC100
         + thickness^2 / (24.0 * ff_pen)                  // 24.0 = AFC030
    currentDamage -= loss;
    if (currentDamage < 1.0) -> bullet dead, stop;
    penetrationsLeft -= 1;

Constants (rodata vaddr==fileoff): 3.0=AFC590, 1.25=AFC5AC, 0.16=AFC100, 24.0=AFC030,
1.0=AFA708. Branches: if ff_pen <= 0.1 (AFC578) variant at 14FE390 uses 1.0/ff_pen clamped.
Special materials dispatch on a WORD read from the trace-sibling's return (+0x14): compares
0x55/0x4C('L')/0x47('G') etc. at 14FE4E8-14FE51B (glass/metal/etc. modifiers differ).
Range check: 3000.0 (AFC4D0) compared early against a distance.
Helper: sub_14F13A0(cvarObj*, -1) = ConVar::GetFloat.

## SOLVED this session (2026-08-24 cont.)

- **Surface-prop getter 0x169FBD0** (4 instructions): `entry = global[0x47E1BE8]->entries@+0x28
  + index*0x20`; entry: float @+0x08 (pen modifier), word @+0x14 (material). Index comes from
  contact entry +0x30 (word), entity handle at contact +0x2C.
- **ConVar value read**: `sub_14F13A0(cvarObj, dontcare)` returns float: `p=accessor20d3f50(obj);
  p ? *(float*)p : *(float*)(*(float**)(*(void**)obj+8))`. Callable directly at runtime.
- **Generic loss branch needs NO material data**: only weaponPenPower, ff_pen cvar, thickness,
  currentDamage. Per-material special-casing (glass 'G'/0x47 etc.) can be added later.
- Deltas confirmed per-function (padding varies): TraceShape/sibling/enterexit region +0x640..0x660;
  combat-region functions unchanged in place but with rel32 call targets re-linked.
- Fresh dumps: /mnt/disk2/scratch/re/pen_full.asm (full pen-math body), enterexit.asm,
  fb_cur.asm (current FireBullets orchestrator), nb.asm (trace-manager neighborhood).

## V1 AUTOWALL BLUEPRINT (everything researched, ready to implement)

    float simulate(eye->aimPoint ray):
      cur = baseDamage(falloff via m_flRangeModifier, armor scaling already in estimatedDamage)
      pos = eye; dir normalized
      for (layers = 0; layers < 4; ++layers):
        t = Tracing::traceLine(pos, aimPoint)
        if (!t.didHit || t.hitEntity == target) return cur            // reached target
        if (t.hitEntity != nullptr) return 0                          // player-blocked: never
        enter = t.endPos
        exit  = stepToExit(enter, dir)                                // our own stepper
        if (!exit) return 0
        thickness = |exit - enter|
        ff_pen   = getFloat(cvar 0x47C4DF0)                           // call 14F13A0
        cur -= max(0, 3.75/penPower)*(3.0/ff_pen) + 0.16*cur + thickness^2/(24*ff_pen)
        if (cur < 1) return 0
        pos = exit + dir*epsilon
      return 0

    stepToExit: classic stepping - advance along dir in ~4u steps tracing backwards from beyond;
    refine last free/hit boundary with a bisect trace. (TraceShape primitive only.)
## IMPLEMENTED (2026-08-24, build green - pending in-game verification)

- `Source/Features/Combat/Autowall/Autowall.h` - v1 sim per the blueprint: iterated forward/reversed
  TraceShape pairing (conservative on multi-layer walls), decoded loss formula, live ff_damage
  cvar read via game getter call (fallback 1.0), 4-layer cap.
- Bullet mask: sim traces use **0x1C300B** (FireBullets @0x152A0ED) - includes CONTENTS_GRATE so
  grates get traced and their near-zero thickness handled by the loss formula like the server. The
  Aimbot LOS pre-check keeps the default Tracing mask (0x1C3003) so grates count as VISIBLE for
  WallCheck-only mode (bullets pass grates; a blocked trace there would wrongly reject).
- `BaseWeapon::penetrationPower()` added (m_flPenetration via schema).
- Aimbot gate `passesVisibility()` after multipoint refine: WallCheck = clear LOS; Autowall =
  surviving damage (falloff -> wall losses -> armor/hitgroup scale) >= MinDamage. Player-blocked
  rays always reject; unreadable weapon inputs fail closed. Neither option set = old behavior.
- Config: aimbot_vars::WallCheck / Autowall registered in all six places
  (AimbotConfigVariables/ConfigVariableTypes/ConfigSchema appended LAST/CombatTab x2/CreateGUI.js);
  UI: "Only Shoot Visible Targets" + "Shoot Through Thin Walls" under Auto Shoot Min Damage.
- TEMPORARY VerifyConsole logs ([autowall]): player-blocked / world-blocked / bullet-absorbed /
  "surviving=%.1f min=%d -> FIRE|reject". Strip once verified.

## IN-GAME VERIFICATION MATRIX (next session)

1. Sanity: traces work at all post-offset-fix (wall_check on, shoot visible enemy through nothing).
2. Grate spot-check: shoot through a metal grate - expect tiny loss (not "absorbed"), shot fires.
2. Thin wall (door): autowall on, min_damage ~20 -> expect surviving>min -> FIRE; server hitmarker
   through wall confirms the server agrees.
3. Thick wall: expect "bullet absorbed" or low surviving -> reject; no shot fired.
4. Player-blocked: teammate between -> "player-blocked", never fires.
5. Compare surviving-damage log vs actual applied damage numbers shown by the server (dmg indicator)
   to validate the formula constants empirically.
6. Then: ForceShot ground->air, MaxAccuracyOnly flip test, and strip all TEMPORARY instrumentation.




## AttackButton status (RETIRED 2026-08-25 evening - subtick-only firing now)

- **The kbutton write path is GONE.** After fixing the moved offset (0x47F8A50 -> 0x481CE50) same-day,
  the user chose to drop the real-kbutton approach entirely: triggerbot + rage force-shot now fire
  through `AttackCommand` (`Source/Features/Combat/AttackCommand.h`) - IN_ATTACK in BOTH banks of
  buttons_pb + raw words, a CSubtickMoveStep press(0.0)/release(1.0) pair, and attack1_start_history_index
  = newest input_history entry - spliced in the WriteMoveCrc hook (slot 7 pre-original), where slot 6 can
  no longer overwrite it. AttackButton.h was deleted; nothing writes kbutton memory any more, so the
  offset below is irrelevant unless that path is ever revived.
- For the record (build 14177): in_attack kbutton sat at **0x481CE50** (+0x13E00 vs Aug-20's 0x47F8A50),
  per cs2-dumper's live KeyButton-list walk (buttons.json). Still NOT statically re-derivable: KeyButton
  structs are runtime-initialized into .bss; the live-process list walk IS the derivation.

## Interim code already merged (unrelated to RE)

- `Source/Features/Combat/VisibilityCheck.h` — shared two-trace measurement + decision rule
  (Clear/WorldBlocked/EntityBlocked/Unknown), created BEFORE deciding on full parity. Triggerbot can
  adopt it later; rage will use the real autowall once this port lands.
- `Source/GameClient/Tracing/Tracing.h` — the four module-relative offsets revalidated/fixed for the
  Aug-20 update (see CRITICAL FINDING above). Build green after the change.


