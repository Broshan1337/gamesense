# Neversnooze

<img width="3440" height="1440" alt="image" src="https://github.com/user-attachments/assets/c64839e7-4ade-4028-98e5-50fb6b77af79" />

An in-process internal trainer for Counter-Strike 2 on Linux. Originally started as a Linux
fork of [Osiris](https://github.com/danielkrupinski/Osiris), the codebase has since been heavily
refactored: the legacy Source 2 Panorama UI was replaced with Dear ImGui rendered directly into
the game's Vulkan swapchain, LuaJIT 2.1 is embedded for user scripting, and all offsets are
resolved at load time through pattern-based signature scanning — no hardcoded addresses.

Built for personal use and local testing. Expect breakage after every CS2 update; fixes and
pattern re-anchors are the main maintenance work and pull requests are very welcome.

## Project status (October 2026)

- **CS2 module** — builds clean and is in active use. All patterns are currently re-validated
  against the live build (161 signatures, exactly-once checked). Two features remain
  build-gated: viewmodel rotation is keyed to one inspected client build (GNU build ID
  `ea57d883…`) and stays inert on newer builds, and engine-exact wall penetration
  ("Shoot Walls") needs a per-build ABI table (present for `ea57d883…` and `866bdc00…`);
  everywhere else the ragebot falls back to a trace estimate.
- **TF2 module** — experimental stub: constructor diagnostics, minimal Vulkan present hooks and
  logging only (10 files). No aimbot, visuals, menu or config system yet.
- **Windows** — unsupported and none planned. The old Windows tree is unmaintained legacy code.

## Architecture

| Piece | What it is |
|---|---|
| `cs2/` → `libMangoHud.so` | 64-bit module injected into the CS2 process. Hooks `vkQueuePresentKHR`/`vkAcquireNextImageKHR` to render the ImGui overlay through the game's own Vulkan, hooks the input/net paths for movement and combat features, and embeds a LuaJIT 2.1 runtime for user scripts. |
| `cs2/` → `libSteamModule.so` | 32-bit module injected into the Steam client process. Hooks libc filesystem calls (`open`, `openat`, `stat`, `lstat`) to conceal the game's injected memory descriptors from Steam-side sweeps. |
| [neversneeze-loader](https://github.com/Broshan1337/neversneeze-loader) | External ELF loader. Packs the payloads, signs them, injects via anonymous memfds and stamps the cryptographic session trailer the modules verify. |
| `cs2/inject.sh` | Scripted fallback (`inject_memfd` + GDB). Does **not** stamp a session trailer — see below. |

## Prerequisites

- x86_64 Linux, GCC 13+ or Clang 18+ (C++20), CMake 3.24+, Python 3.10+ for the tooling.
- **Only the Steam module needs 32-bit multilib** support (Steam is a 32-bit process).
- The loader links OpenSSL. The CS2/TF2 modules link nothing but `libdl` — SDL, Vulkan and
  ImGui are all resolved from the game process or vendored in-tree, so no `-dev` packages for
  those are needed.

Debian / Ubuntu:

```sh
sudo apt install build-essential cmake gdb git \
    gcc-multilib g++-multilib libssl-dev python3-pip
pip install capstone        # pattern tooling (scratchpad/pattern_forge.py)
```

Fedora:

```sh
sudo dnf install gcc-c++ cmake make gdb git openssl-devel python3-pip
# for the Steam module only (32-bit multilib):
sudo dnf install glibc-devel.i686 libstdc++-devel.i686
pip install capstone
```

Arch:

```sh
sudo pacman -S base-devel cmake gdb git openssl python-pip
# for the Steam module only (enable the [multilib] repo first):
sudo pacman -S multilib-devel
```

## Key material (one-time, per machine)

Modules verify a session trailer against a machine-local key — a fresh clone does **not**
build without it. Mint your own keydir and generate the module-side header (the loader must
be built from the same keydir):

```sh
git clone https://github.com/Broshan1337/neversneeze-loader
cmake -B neversneeze-loader/build -S neversneeze-loader
cmake --build neversneeze-loader/build --target payload_packer
neversneeze-loader/build/payload_packer keygen ~/.config/neversnooze-keys
chmod 700 ~/.config/neversnooze-keys
python3 nix/gen_session_bind_key.py \
    ~/.config/neversnooze-keys/heartbeat.key \
    cs2/Source/Utils/SessionBindKey.h   # plain python3, despite the path
# building the TF2 module too? same key, second copy: tf2/Source/SessionBindKey.h
```

Never commit or publish `SessionBindKey.h` or the keydir.

## Building

> **Note on obfuscation:** the tree defaults to the Arkari (obfuscator) toolchain
> (`-DNEVERSNOOZE_OBFUSCATE` defaults to ON and adds Arkari-specific compiler flags). With
> standard GCC or Clang you **must** pass `-DNEVERSNOOZE_OBFUSCATE=OFF`.

CS2 module, release:

```sh
cmake -B cs2/build -S cs2 -DNEVERSNOOZE_OBFUSCATE=OFF
cmake --build cs2/build --target Neversnooze
# -> cs2/build/Source/libMangoHud.so
```

CS2 module, debug — build this first when re-deriving patterns or diagnosing crashes; a debug
build makes pattern-uniqueness assertion failures fatal and gives full logs:

```sh
cmake -B cs2/build-dbg -S cs2 -DCMAKE_BUILD_TYPE=Debug -DNEVERSNOOZE_OBFUSCATE=OFF
cmake --build cs2/build-dbg --target Neversnooze
```

Steam module (32-bit) — needs its **own** build tree (the 64- and 32-bit targets write the
same output name and cannot share a directory), and the option spelled exactly as below:

```sh
cmake -B cs2/build-steam -S cs2 \
    -DNEVERSNOOZE_BUILD_STEAM_MODULE32:BOOL=ON -DNEVERSNOOZE_OBFUSCATE=OFF
cmake --build cs2/build-steam --target SteamModule32
```

TF2 module:

```sh
cmake -B tf2/build -S tf2
cmake --build tf2/build --target NeversnoozeTF2
```

## Testing

Unit tests and the VAC decoy harness run on GoogleTest, fetched automatically at configure
time (needs network the first time). The functional suite is stale upstream — enable `unit`
only:

```sh
cmake -B cs2/build-tests -S cs2 -DENABLE_TESTS=unit -DNEVERSNOOZE_OBFUSCATE=OFF
cmake --build cs2/build-tests
ctest --test-dir cs2/build-tests           # 1,300+ tests, currently all green
```

## Injection

**Recommended: the loader.** `run.sh` in [neversneeze-loader](https://github.com/Broshan1337/neversneeze-loader)
self-elevates, injects the Steam module and the CS2 module through memfds, and stamps the
session trailer. A local build always wins over the embedded payloads on dev machines.

**Fallback: `sudo cs2/inject.sh`** drives `cs2/inject_memfd` (built on demand) and GDB. It
performs a raw injection **without** a session trailer — by design the modules fail closed
then: the CS2 module detects the missing/invalid trailer and releases itself, and the Steam
module goes inert. Use the loader for normal operation; `inject.sh` is for debugging the
injection path itself.

## Configuration & logs

Everything lives under `~/OsirisCS2/`:

- `~/OsirisCS2/configs/` — saved configurations (JSON) and feature sidecars
- `~/OsirisCS2/logs/` — rotating logs
- `/tmp/gamesense_gui.log` — in-game anomaly log; a healthy session writes nothing

## Helping update it after a CS2 update

Every update breaks some patterns/offsets — that's most of the maintenance work, and
contributions here are very welcome:

1. Dump the new build with [cs2-dumper (Linux fork)](https://github.com/Broshan1337/cs2dumper-for-linux)
   (`tools/verify_signatures.py` reports which dumper signatures broke) and diff the new
   `output/` against the previous dump.
2. Validate the in-repo patterns: `python3 scratchpad/pattern_forge.py validate` reports every
   pattern that no longer matches exactly-once (needs `pip install capstone` and
   `CS2_GAME_ROOT` pointing at your `…/Counter-Strike Global Offensive/game` directory).
3. Re-derive broken ones with `pattern_forge.py forge --module client --target 0x… --name FooPointer`
   from an address you trust (dumper output, or the old value from `pattern_manifest.json`
   shifted by a verified delta), then drop the emitted line into the pattern headers under
   `cs2/Source/MemoryPatterns/`.
4. Build the **debug** tree first, inject that, and only move to release once it's stable.

## Known limitations

- Don't publish Nix derivations built with `nix/key-material.nix` — private key material is
  copied into the world-readable Nix store.
- The Lua runtime preloads the LuaJIT FFI. Scripts have unrestricted access to native
  functions; do not run untrusted community scripts.
- Viewmodel rotation and engine-exact wall penetration are keyed to inspected client builds
  (see status above); they stay disabled on unrecognized builds instead of misbehaving.

## Credits

- [Osiris](https://github.com/danielkrupinski/Osiris) by Daniel Krupiński — the starting point
  (MIT, license file kept): config system and core feature architecture.
- [FrameworkCS2](https://github.com/Sumandora/FrameworkCS2) by Sumandora — reference for many
  features and the entity/render approach.
- [Dear ImGui](https://github.com/ocornut/imgui) by Omar Cornut — the UI layer.
- [LuaJIT](https://luajit.org/) by Mike Pall — the scripting runtime.
