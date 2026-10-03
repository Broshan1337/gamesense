# Neversnooze

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
