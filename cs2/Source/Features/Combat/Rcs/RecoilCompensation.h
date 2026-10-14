#pragma once

#include <algorithm>
#include <cstring>

#include <Features/Combat/MovementFix.h>
#include <GameClient/InputHistory.h>
#include <GameClient/UserCmd.h>
#include <Utils/Trig.h>

namespace recoil_compensation
{
// RCS (2026-10-13 rewrite, friend report "screen moves down, bullets not in one
// point"): shots are reconstructed from the input-history ENTRY angles (the
// FVA/silent-aim channel - SubtickShotWriter::writeShotIntoEntry is the idiom),
// so the compensation goes THERE, subtracting the FULL current aim punch from
// every entry's angles. The camera never comes into it.
//
// The old version wrote the cmd BASE angles (which pin the camera - the
// 2026-10-10 anti-aim lesson: base writes are the camera) with the per-command
// punch DELTA, which sank the visible view by the accumulated punch ("moves
// the screen down") and left the shots one punch-step off the crosshair; its
// entry writes reused the same delta, so the reconstructed shot landed at
// view + punch[N-1] - a scattered arc, not one point.
//
// Entry angles are absolute real-view samples rebuilt from the mouse every
// frame, so a per-frame FULL punch subtraction is idempotent (no accumulation):
// reconstructed eye = view - punch, server shot = (view - punch) + punch =
// view - every bullet lands at the crosshair, camera stays at the real view.
inline void apply(cs2::CUserCmd* cmd, float punchPitch, float punchYaw) noexcept
{
    if (!__builtin_isfinite(punchPitch) || !__builtin_isfinite(punchYaw))
        return;
    const InputHistory history{cmd};
    if (!history.looksValid())
        return;
    for (int i = 0; i < history.currentSize(); ++i) {
        auto* entry = history.entryAt(i);
        std::byte* angles = nullptr;
        std::memcpy(&angles, entry + cs2::CUserCmd::InputHistory::kEntryViewAnglesOffset, sizeof(angles));
        if (!angles)
            continue;
        float samplePitch{}, sampleYaw{};
        using Angles = cs2::CUserCmd::BaseMessage::ViewAngles;
        std::memcpy(&samplePitch, angles + Angles::kPitchOffset, sizeof(samplePitch));
        std::memcpy(&sampleYaw, angles + Angles::kYawOffset, sizeof(sampleYaw));
        if (!__builtin_isfinite(samplePitch) || !__builtin_isfinite(sampleYaw))
            continue;
        samplePitch = std::clamp(samplePitch - punchPitch, -89.0f, 89.0f);
        sampleYaw = trig::normalizeDegrees(sampleYaw - punchYaw);
        std::memcpy(angles + Angles::kPitchOffset, &samplePitch, sizeof(samplePitch));
        std::memcpy(angles + Angles::kYawOffset, &sampleYaw, sizeof(sampleYaw));
        std::uint32_t bits{};
        std::memcpy(&bits, entry + cs2::CUserCmd::InputHistory::kEntryHasBitsOffset, sizeof(bits));
        bits |= cs2::CUserCmd::InputHistory::kEntryViewAnglesHasBit;
        std::memcpy(entry + cs2::CUserCmd::InputHistory::kEntryHasBitsOffset, &bits, sizeof(bits));
    }
}
}