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

```sh
cmake -B cs2/build -S cs2
cmake --build cs2/build --target Neversnooze
```

Load it with `sudo cs2/inject.sh`. This is a hobby project — read the code before running
anything, and don't expect hand-holding.

## Credits

[Osiris](https://github.com/danielkrupinski/Osiris) by Daniel Krupiński — the starting point
(MIT, license file kept).
