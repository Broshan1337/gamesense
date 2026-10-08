# cs2/ — the CS2 module

The CS2 side of Neversnooze: a Dear ImGui overlay rendered through the game's own Vulkan
(the old Panorama UI is gone), a Lua scripting API, and the usual feature set. **Linux
only.** The built artifact is `libMangoHud.so` — the name is deliberate, it blends in with
the actual MangoHud overlay people run anyway.

## Building (stock toolchain)

```sh
cmake -B cs2/build-dbg -S cs2 -DCMAKE_BUILD_TYPE=Debug
cmake --build cs2/build-dbg --target Neversnooze
```

Inject with `sudo cs2/inject.sh` (it prompts: release from `cs2/build/`, debug from
`cs2/build-dbg/`; it refuses stale builds). Work in a debug build first — crashes give full
logs there, and the in-game log lives at `$HOME/OsirisCS2/logs/gamesense_gui.log` (anomaly-only:
a healthy session prints nothing). All module diagnostics and the loader/module exchange files
(unload request, RPC/persona/radio helper scripts) live under that exchange root — `/tmp`
stopped being shared between the Steam container and the host with the 2026-10-04 Steam client
update, so `/tmp` paths no longer work for anything the module writes (see
`Source/Utils/NsPaths.h`).

Tests: `cmake -B cs2/build-tests -S cs2 -DENABLE_TESTS=unit` then
`cmake --build cs2/build-tests && ctest --test-dir cs2/build-tests`.

Settings are stored in `$HOME/OsirisCS2/configs`.

## Slider binds and viewmodel rotation

Right-click a numeric slider row to choose a key, Hold/Toggle mode, and an override
value. Hold restores the normal value when released; Toggle restores it on the next
press. Unbinding, changing keys/modes, and unloading also restore active overrides.
Auto-save preserves the normal values while overrides are active; loading/resetting
a config clears active overrides first. Bindings and their values are saved in `configs/feature_binds.txt`; existing boolean
bind files remain compatible. Percentage sliders and minimum damage support this
alongside the other ranged sliders.

In Visuals → Viewmodel, enable **Modify Position** to use X/Y/Z offsets and
**Pitch/Roll** (−180° to 180°). Rotation changes the private first-person HUD pose
used by the arms and weapon. The Linux hook currently supports client build ID
`ea57d8833ab297b622699925dbc83bf6aa1aa615`; other builds leave rotation inactive and
report that status. Position offsets are restored when disabled or unloaded.

Insert toggles on physical presses. Captured SDL input is drained without hiding
window/quit events, and menu dismissal has no closing fade.

## Building with OLLVM obfuscation (Arkari)

The tree supports per-function obfuscation through [Arkari](https://github.com/Arkari/obfuscator)
(a goron-derived LLVM fork). Configure with its clang as the compiler:

```sh
cmake -B cs2/build-obf-test -S cs2 \
  -DNEVERSNOOZE_OBFUSCATE=ON \
  -DCMAKE_C_COMPILER=$HOME/obfuscator/Arkari/llvm/build/bin/clang \
  -DCMAKE_CXX_COMPILER=$HOME/obfuscator/Arkari/llvm/build/bin/clang++
```

- `NEVERSNOOZE_OBFUSCATE` is ON by default; with a stock compiler it compiles clean (the
  annotations compile away), so the same tree builds with GCC or Arkari clang.
- `-mllvm -arkari-cfg=<file>` carries the random seed (`NEVERSNOOZE_ARKARI_CFG` cache var).
- Per-function policy lives in `cs2/Source/Utils/ObfAnnotations.h` — `NS_OBF_FLATTEN`
  (`+fla`), `NS_OBF_ICALL` (`+icall`), `NS_OBF_INDGV` (`+indgv`), `NS_OBF_CIE` (`+cie`), etc.
  Annotate **cold, secret-carrying code only — never hot paths** (flattening a per-frame
  function is a straight FPS tax).
- Obfuscated builds also lift clang's constexpr/template limits (`-fconstexpr-depth=4096`
  etc.) because the pattern TypeLists instantiate ~460-argument fold expressions.

## Ship hardening

- `-DNEVERSNOOZE_SHIP:BOOL=ON` (default): the release link is fully stripped and the symbol
  table is preserved only in a local `<build>/Source/ship-debug/<name>.debug` copy +
  `.gnu_debuglink`, so you can still symbolize your own crashes.
- `-DNEVERSNOOZE_BUILD_STEAM_MODULE32:BOOL=ON` builds the Steam-side module. Spell it
  exactly — an untyped `-D` with a typo'd name silently leaves the target out of the build.

## Session binding / heartbeats

The loader stamps a 64-byte trailer after the ELF image inside the memfd it injects from
(format in the [loader repo](https://github.com/Broshan1337/neversneeze-loader),
`src/SessionTrailer.h`): magic `"NSHB02"`, the loader's pid + comm, and a proof
(`comm XOR key`). Module side (`cs2/Source/Utils/SessionBind.h`):

- `verifySession()` runs once, early on the first present thread: it finds its own memfd via
  `/proc/self/fd`, reads the trailer, verifies magic + proof. **Fail-closed** — no trailer or
  bad proof means the module wasn't injected by a loader holding the key (e.g. a dumped
  module re-injected standalone) and the module releases itself.
- `tickLoaderLiveness()` polls the stamped loader pid afterwards; if the loader died, the
  module is an orphan and releases itself after a grace window. The check compares against
  the comm the loader stamped at injection time, so the loader binary can be renamed freely.
- The key itself lives in `cs2/Source/Utils/SessionBindKey.h` — **machine-local and
  gitignored**, so the repo doesn't build as-is until you make one. Format: two 32-byte
  arrays, `kKeyMasked` (key XOR mask) and `kKeyMask`; unmask transiently at verification
  time. Generate your own pair, keep it in sync with the loader's `heartbeat.key`, and keep
  the `cs2/` and `tf2/` copies byte-identical.

## VMProtect

VMP applies to the **loader** only (see the loader repo for that flow) — and only on stock
codegen: VMP's parser cannot read Arkari-flattened code, so the two are never stacked in one
binary. Module-side protection is Arkari annotations plus the layers below.

## Other hardening in the module

- **String vault** (`Utils/NsStr.h`): identity strings are XOR-obfuscated constants, not
  contiguous rodata literals (a plain string is one `movabs` immediate away from leaking).
- **Pattern vault** (`MemorySearch/PatternVault.h`): byte patterns are encrypted at rest;
  the pools unseal only after the session verify passes.
- **Honeypots** (`Security/Honeypots.h`): decoy "license/key" strings that are deliberately
  visible in `strings` output, plus heavily flattened dead-by-construction functions that
  waste an analyst's time. Pure computation, never executed.
- **Self-unload** (`Utils/LinuxSelfUnload.h`): the unmap worker needs
  `[[clang::musttail]]` under clang (GCC uses `[[gnu::musttail]]`) — dropping it makes the
  unload crash with a distinctive signature.
