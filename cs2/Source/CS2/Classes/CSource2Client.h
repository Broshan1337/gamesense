#pragma once

#include <cstdint>

namespace cs2 {

// The client.dll-side interface the engine calls back into for per-frame lifecycle
// notifications. Found via IDA MCP this session: OnFrameStageNotify is vtable slot 36
// (sub_18CAB20 - confirmed real via the literal "./cdll_client_int.cpp" source-file string
// referenced inside it, plus per-stage debug strings "FrameNetUpdate(%.3f %d)" (stage 5) and
// "FramePostDataUpdate(%.3f %d)" (stage 6) matching classic Source engine's own naming for
// this exact function). Resolved live via CreateInterface("Source2Client002", ...) - see
// Source2ClientPointer.h.
struct CSource2Client {
    using OnFrameStageNotify = std::int64_t(CSource2Client* thisptr, int frameStage);

    // Returns the command NUMBER currently being built for a split-screen slot - not an index into
    // anything of ours, but the value the command ring is keyed by. Feed it to the command getter.
    //
    // Slot 18, taken from the internal reference base and verified here rather than trusted: the
    // vtable entry at 0x43C86D0 (= vtable base + 18*8, where the base is derived from
    // OnFrameStageNotify's known slot 36 at 0x43C8760) holds sub_18920D0, which is
    // `v2 = sub_15D97F0(slot); return v2 ? sub_15DAAE0(v2) : 0;` - a per-slot lookup returning an
    // int, exactly the shape this should be.
    using GetCommandIndex = int(CSource2Client* thisptr, int slot);
    static constexpr int kGetCommandIndexVtableSlot = 18;
};

}
