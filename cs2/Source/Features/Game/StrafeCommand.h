#pragma once

#include <cmath>
#include <cstring>

#include <CS2/Classes/CCSGOInput.h>
#include <Features/Game/AirStrafe.h>
#include <GameClient/SubtickMoves.h>
#include <GameClient/UserCmd.h>

// Staged intent belongs to one command, including its original view basis.
// CreateMove and CRC hooks may run on different commands or more than once.
class StrafeCommand {
public:
    void reset() noexcept { command = nullptr; }

    [[nodiscard]] bool stage(cs2::CUserCmd* cmd, air_strafe::Move move) noexcept
    {
        reset();
        const UserCmd userCmd{cmd};
        const auto yaw = userCmd.viewYaw();
        const float length = std::hypot(move.forward, move.left);
        if (!userCmd || !yaw.hasValue() || !std::isfinite(yaw.value())
            || !std::isfinite(length) || length <= 0.0f)
            return false;
        command = cmd;
        base = userCmd.baseMessage();
        sequence = commandNumber(cmd);
        sourceYaw = yaw.value();
        magnitude = std::min(length, 1.0f);
        movement = {move.forward / length, move.left / length};
        return true;
    }

    [[nodiscard]] bool active() const noexcept { return command != nullptr; }
    [[nodiscard]] air_strafe::Move move() const noexcept { return movement; }

    [[nodiscard]] bool matches(cs2::CUserCmd* cmd) const noexcept
    {
        return active() && cmd == command && UserCmd{cmd}.baseMessage() == base
            && commandNumber(cmd) == sequence;
    }

    template <typename HookContext>
    bool commit(cs2::CUserCmd* cmd) noexcept
    {
        const bool matching = matches(cmd);
        reset(); // Do not duplicate edits on a repeated serialization callback.
        if (!matching)
            return false;
        const UserCmd userCmd{cmd};
        const auto yaw = userCmd.viewYaw();
        if (!yaw.hasValue() || !std::isfinite(yaw.value()))
            return false;
        // Another feature may have aimed after capture. Preserve world movement.
        const float relativeYaw = std::atan2(movement.left, movement.forward);
        const auto corrected = air_strafe::moveAtAngle(
            static_cast<float>(std::remainder(double(sourceYaw) * trig::kDegreesToRadians + relativeYaw,
                double(trig::kTwoPi))),
            static_cast<float>(std::remainder(double(yaw.value()) * trig::kDegreesToRadians,
                double(trig::kTwoPi))), 0.0f, true);
        userCmd.setForwardMove(corrected.forward * magnitude);
        userCmd.setLeftMove(corrected.left * magnitude);
        using Buttons = cs2::CCSGOInput::Buttons;
        constexpr auto mask = Buttons::kForward | Buttons::kBack | Buttons::kMoveLeft | Buttons::kMoveRight;
        std::uint64_t buttons{};
        if (corrected.forward > 0.0f) buttons |= Buttons::kForward;
        if (corrected.forward < 0.0f) buttons |= Buttons::kBack;
        if (corrected.left > 0.0f) buttons |= Buttons::kMoveLeft;
        if (corrected.left < 0.0f) buttons |= Buttons::kMoveRight;
        userCmd.replaceButtons(mask, buttons);
        SubtickMoves<HookContext>::stripMovement(userCmd.baseMessage(), mask);
        return true;
    }

    [[nodiscard]] static int commandNumber(cs2::CUserCmd* cmd) noexcept
    {
        int value{};
        if (cmd)
            std::memcpy(&value, reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::kCommandNumberOffset,
                sizeof(value));
        return value;
    }

private:
    cs2::CUserCmd* command{};
    std::byte* base{};
    int sequence{};
    float sourceYaw{}, magnitude{1.0f};
    air_strafe::Move movement{};
};
