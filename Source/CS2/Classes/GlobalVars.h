#pragma once

#include <cstdint>

#include <Utils/Pad.h>

namespace cs2
{

struct GlobalVars {
    using frametime = float; // type tag for OffsetToFrametime (pattern-resolved, not an offset here)

    PAD(16); // realtime @ 0x00, framecount @ 0x04, absoluteframetime @ 0x08, frametime @ 0x0C
    std::int32_t maxClients; // 0x10 (read as 64 in the live 2026-08-29 build)
    PAD(8); // 0x14 / 0x18
    // VERIFIED against the live 2026-08-29 build by reading the struct out of process memory:
    // float 0.015625 (1/64) sits here, right after maxClients. The old 0x40 assumption died with
    // that update - the dead field at 0x40 transiently holds denormal floats that passed the old
    // sanity window and froze every sim that divided the tick with it.
    float intervalPerTick; // 0x1C
    PAD(16); // 0x20 .. 0x2C
    float curtime; // 0x30 (unchanged, matches velocity-cs2's globalvars+0x30)
    PAD(12); // 0x34 .. 0x3C: frametime pair / map-related floats, nothing needs them
    // The classic `float interval_per_tick; int tickcount;` pair's second half: tickcount verified
    // live at 0x44 (382219 = curtime 5972.17 / 0.015625), but interval_per_tick itself is at 0x1C
    // on this build - do NOT assume the two are adjacent again.
    float intervalPerTickLegacy; // 0x40 (reads 0.0 on the live build - kept only as a warning anchor)
    std::int32_t tickCount; // 0x44
};

}
