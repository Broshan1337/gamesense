#pragma once

#include <algorithm>
#include <cstring>

#include <Features/Combat/MovementFix.h>
#include <GameClient/InputHistory.h>
#include <GameClient/UserCmd.h>
#include <Utils/Trig.h>

namespace recoil_compensation
{
struct Kick { float pitch{}, yaw{}; };

class CommandState {
public:
    void reset() noexcept { *this = {}; }
    [[nodiscard]] bool active() const noexcept { return haveCommand; }
    [[nodiscard]] Kick correction(int sequence, float pitchKick, float yawKick) noexcept
    {
        if (!haveCommand || sequence < lastSequence)
            reset();
        if (!haveCommand || sequence != lastSequence) {
            previous = current;
            lastSequence = sequence;
            haveCommand = true;
        }
        // A repeated CreateMove rebuilds this command's base angles. Reuse its
        // previous-command baseline instead of consuming the correction twice.
        current = {pitchKick, yawKick};
        return {current.pitch - previous.pitch, current.yaw - previous.yaw};
    }
private:
    bool haveCommand{};
    int lastSequence{};
    Kick previous{}, current{};
};

inline void apply(cs2::CUserCmd* cmd, float pitchKick, float yawKick) noexcept
{
    const UserCmd userCmd{cmd};
    const auto pitch = userCmd.viewPitch();
    const auto yaw = userCmd.viewYaw();
    if (!pitch.hasValue() || !yaw.hasValue() || !__builtin_isfinite(pitch.value())
        || !__builtin_isfinite(yaw.value()) || !__builtin_isfinite(pitchKick) || !__builtin_isfinite(yawKick))
        return;

    movement_fix::setViewAngles(userCmd, std::clamp(pitch.value() - pitchKick, -89.0f, 89.0f),
                               trig::normalizeDegrees(yaw.value() - yawKick));

    // Firing consumes input_history, not just the base command's angles. Preserve each
    // sample's mouse movement while applying the same correction to its shot angle.
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
        samplePitch = std::clamp(samplePitch - pitchKick, -89.0f, 89.0f);
        sampleYaw = trig::normalizeDegrees(sampleYaw - yawKick);
        std::memcpy(angles + Angles::kPitchOffset, &samplePitch, sizeof(samplePitch));
        std::memcpy(angles + Angles::kYawOffset, &sampleYaw, sizeof(sampleYaw));
        std::uint32_t bits{};
        std::memcpy(&bits, entry + cs2::CUserCmd::InputHistory::kEntryHasBitsOffset, sizeof(bits));
        bits |= cs2::CUserCmd::InputHistory::kEntryViewAnglesHasBit;
        std::memcpy(entry + cs2::CUserCmd::InputHistory::kEntryHasBitsOffset, &bits, sizeof(bits));
    }
}
}
