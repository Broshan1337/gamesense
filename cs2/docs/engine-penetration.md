# CS2 Linux penetration binding

The aimbot and triggerbot use the client's world trace, entity trace, update
builder, and penetration handler. The older neutral-material implementation is
retained only for the existing Lua `penetratedDamage` API, whose arguments do
not contain the weapon's full range metadata.

Supported client ELF build ID: `ea57d8833ab297b622699925dbc83bf6aa1aa615`.
An unsupported build or changed ABI anchors disables this path. The menu then
shows **Shoot Walls (Unavailable)**. There is no automatic estimated fallback.

## Recovered Linux call flow

RVAs are virtual addresses relative to the loaded client, not file offsets.

- `0x155a700`: create trace, returning a Vector in SysV `xmm0/xmm1`.
  Parameters are trace data, start pointer, displacement pointer, filter pointer,
  and penetration count in `rdi/rsi/rdx/rcx/r8`.
- `0x16f4f00`: trace manager wrapper; the second entity pass receives the query
  attributes at filter + 8 and a float of -1.0. Rebuild updates at `0x1538760`
  when this pass adds trace elements.
- `0x1542f80`: penetration handler, receiving trace data, bullet state, update,
  shooter team, and an optional diagnostic buffer. A true return stops the shot.
  The diagnostic argument is null in this implementation.
- `0x156dc40`: original FireBullet caller, inspected only; never invoked.
  Its calls at `0x156e060` and `0x156e205` independently anchor the bindings.

This build uses 0x38-byte elements, 0x18-byte updates, and the update list at
trace-data offset 0x1c20. The original caller's two-pass filter setup and
IMemAlloc cleanup are reproduced. Compile-time offset assertions and runtime
checks protect against the older 0x30-byte Windows layout being used here.

The engine handles material-specific penetration loss and air-segment range
decay. The caller applies normal player armor and the actual traced hitgroup
once afterward. Custom server damage multipliers and heavy armor are not
modeled by that final health-damage calculation. Other players block the shot.
Per-command budgets bound queries; missing targets, invalid data, exhausted
budgets, and failed penetration return unknown damage.

Triggerbot checks the command angles with the same recoil correction as its
shot writer, along the weapon's range, rather than an unrelated line to a bone.
This check describes the central shot
ray; random spread remains the responsibility of the existing hitchance gates.
It does not establish an exact server spread seed.

## Reference sources

- [UC: CS2 AutoWall (2023)](https://www.unknowncheats.me/forum/counter-strike-2-a/608159-cs2-autowall-penetration-system.html)
  describes trace updates and the penetration handler, including decompiled
  material handling. Its Windows signatures and buffer sizes are not used.
- [UC: Autowall (2025)](https://www.unknowncheats.me/forum/counter-strike-2-a/726328-autowall.html)
  documents a changed Windows trace calling convention.
- [SlowlyRIP CS2](https://github.com/SlowlyRIP/CS2/blob/main/CS2/Core/Features/AutoWall/AutoWall.hpp)
  illustrates the engine-helper architecture; it is an older Windows reference.
- [Axion Linux port](https://github.com/louislusted-collab/Axion-NeverloseUI-Linux)
  supplies Linux trace context, but explicitly disables its penetration helper
  calls. The binding here was recovered from the installed client instead.

## Validation boundaries

Tests use a fake penetration handler to verify integration invariants, actual
hitgroup propagation, armor, thickness/range limits, obstruction, invalid
indices/fractions, engine failure, and budget exhaustion. They do not establish
the client's runtime ABI or compare client predictions with server damage.

Debug/Release compilation and static checks against the installed ELF are
separate from in-game validation. Before treating predictions as verified,
compare direct hits and shots through glass, wood, concrete, multiple walls,
and a blocking teammate against a local server's observed damage. Repeat after
any supported-profile update. A game update requires recovering and validating
a new profile, not merely changing the build ID.
