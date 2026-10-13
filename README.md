# Neversnooze
<img width="3440" height="1440" alt="image" src="https://github.com/user-attachments/assets/c64839e7-4ade-4028-98e5-50fb6b77af79" />

A local trainer project for CS2 (and more recently TF2) on Linux. Started as a fork of
[Osiris](https://github.com/danielkrupinski/Osiris) — most of that original code has since been
torn out and replaced with my own work. Built for personal use and local testing.

> Screenshots will be added later.

## Current state

The late-September 2026 CS2 update broke a chunk of things — **ESP and the other visuals are
currently broken** while the patterns/offsets are being re-validated. Everything else (Lua
scripting, skin/agent changer, sound features, etc.) is in various states of working. This
project is under active fixing, expect breakage after game updates.

TF2 support is new and rough around the edges.

**I don't have as much time for this anymore.** If a game update lands before I get to it,
pull requests fixing the broken patterns/offsets are very welcome — see the
[update workflow](#helping-update-it-after-a-game-update) below for how.

## What this is

- Dear ImGui overlay rendered through the game's own Vulkan — the old Panorama UI is gone
- A Lua API so you can build your own menu items and features without touching C++
- Pattern-based offsets, no hardcoded addresses; offsets come from my updated Linux fork of
  [cs2-dumper](https://github.com/Broshan1337/cs2dumper-for-linux)
- Injection is handled by a separate loader:
  [neversneeze-loader](https://github.com/Broshan1337/neversneeze-loader)
- Linux only, no Windows support and none planned

## AI usage

AI tools were partially used in this project — reverse engineering the game binaries, writing
and fixing code, and documentation. Nothing ships without being tested in-game first.

## Helping update it after a game update

Every CS2 update breaks some patterns/offsets — that's most of the maintenance work, and
contributions here are very welcome. The workflow:

1. Dump the new build with [cs2-dumper](https://github.com/Broshan1337/cs2dumper-for-linux)
   (its `tools/verify_signatures.py` tells you which dumper signatures broke), and diff the new
   `output/` against the previous dump to see what moved.
2. Check the in-repo patterns: `python3 scratchpad/pattern_forge.py validate` reports every
   pattern that no longer matches exactly-once (needs `pip install capstone`, and
   `CS2_GAME_ROOT` pointing at your game directory).
3. Re-derive the broken ones with `pattern_forge.py forge --module client --target 0x... --name
   FooPointer` from a target address you trust (dumper output, or the old value from
   `pattern_manifest.json` shifted by a verified delta), then drop the emitted line into the
   pattern headers under `cs2/Source/MemoryPatterns/`.
4. Build a **debug** build first (`cmake -B cs2/build-dbg -S cs2`), inject that, and only move
   to a release build once it's stable — crashes give full logs that way.

## Building

First time on a new machine: the modules verify a session trailer against a machine-local
key, so a fresh clone does not build as-is. Mint your own keydir and generate the
module-side header (the loader must be built from the same keydir):

```sh
git clone https://github.com/Broshan1337/neversneeze-loader
cmake -B neversneeze-loader/build -S neversneeze-loader
cmake --build neversneeze-loader/build --target payload_packer
neversneeze-loader/build/payload_packer keygen ~/.config/neversnooze-keys
python3 nix/gen_session_bind_key.py \
    ~/.config/neversnooze-keys/heartbeat.key \
    cs2/Source/Utils/SessionBindKey.h   # plain python3, despite the path
# building TF2 too? same key, second copy: tf2/Source/SessionBindKey.h
```

CS2 module (libMangoHud.so):

```sh
cmake -B cs2/build -S cs2
cmake --build cs2/build --target Neversnooze
```

Steam module (libSteamModule.so, 32-bit) — required by the loader to hide the session
from Steam. Steam runs as a 32-bit process, so this needs `g++-multilib` and its **own**
build tree with the option spelled exactly as below (the 64- and 32-bit targets write the
same output name, so they cannot share a build directory):

```sh
cmake -B cs2/build-steam -S cs2 -DNEVERSNOOZE_BUILD_STEAM_MODULE32:BOOL=ON
cmake --build cs2/build-steam --target SteamModule32
```

Load it with the loader (neversneeze-loader) - `run.sh` there self-elevates and injects the
module with the session trailer it verifies. Injecting any other way (e.g. `cs2/inject.sh`,
which drives `inject_memfd` directly) fails closed by design: without the loader-stamped
trailer the pattern vault stays sealed and the module releases itself.

## Nix

Reproducible plain-GCC builds of everything in the repo: `nix build .#cs2-module`,
`.#steam-module`, `.#tf2-module`, `.#injector`, `.#key-material` (keydir +
`SessionBindKey.h` — a Nix-built module only pairs with a loader built from the same
key material), or `nix develop` for a cmake/ninja/vulkan shell. The Arkari/VMProtect
ship artifacts stay outside Nix — that toolchain can't run in a build sandbox. Details
in `flake.nix`.

## Credits

[Osiris](https://github.com/danielkrupinski/Osiris) by Daniel Krupiński — the starting point
(MIT, license file kept).

[FrameworkCS2](https://github.com/Sumandora/FrameworkCS2) by [Sumandora](https://github.com/Sumandora/) - many features ported/pasted from here
