#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>

#include <Features/Game/BunnyhopConfigVariables.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/VerifyConsole.h>

// The legacy TestStrafer setting now enables the normal solver with diagnostics.
// A second movement writer used to add synthetic camera yaw subticks and compete
// with the real mouse input. Only Bunnyhop owns the strafe command now.
template <typename HookContext>
class TestStrafer {
public:
    explicit TestStrafer(HookContext& hookContext) noexcept : hookContext{hookContext} {}

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(TestStraferEnabled) || !UserCmd{cmd})
            return onUnload();
        auto&& pawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!pawn || pawn.health().valueOr(0) <= 0)
            return onUnload();
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_vecAbsVelocity");
        const auto onGround = pawn.isOnGround();
        if (!offset.has_value() || *offset <= 0 || !onGround.hasValue())
            return;
        cs2::Vector velocity{};
        std::memcpy(&velocity, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity()))
            + *offset, sizeof(velocity));
        const float speed = std::hypot(velocity.x, velocity.y);
        if (!std::isfinite(speed))
            return;
        if (onGround.value()) {
            if (airborne)
                VerifyConsole::write(0.5f, "[strafehop]", "peak=%.1f delta=%+.1f", peak, peak - lastPeak);
            if (airborne)
                lastPeak = peak;
            peak = 0.0f;
            airborne = false;
        } else {
            airborne = true;
            peak = std::max(peak, speed);
            VerifyConsole::write(1.0f, "[strafe]", "speed=%.1f peak=%.1f", speed, peak);
        }
    }

    void onWriteMoveCrc(cs2::CUserCmd*) const noexcept {}

    void onUnload() const noexcept
    {
        airborne = false;
        peak = 0.0f;
        lastPeak = 0.0f;
    }

private:
    inline static bool airborne{};
    inline static float peak{};
    inline static float lastPeak{};
    HookContext& hookContext;
};
