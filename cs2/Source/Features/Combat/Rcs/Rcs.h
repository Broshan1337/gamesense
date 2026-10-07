#pragma once

#include <CS2/Classes/CUserCmd.h>
#include <Features/Combat/Rcs/RcsConfigVariables.h>
#include <Features/Combat/Rcs/RecoilCompensation.h>
#include <HookContext/HookContextMacros.h>

template <typename HookContext>
class Rcs {
public:
    explicit Rcs(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(rcs_vars::Enabled) || !UserCmd{cmd}) {
            state.reset();
            return;
        }
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn || localPawn.isAlive() != true) {
            state.reset();
            activePawn = nullptr;
            return;
        }
        auto* pawn = static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity());
        if (pawn != activePawn) {
            state.reset();
            activePawn = pawn;
        }
        const auto shots = localPawn.shotsFired();
        if (!shots.hasValue() || (shots.value() < 1 && !state.active()))
            return;
        const auto punch = localPawn.aimPunchAngle();
        if (!punch.hasValue() || !__builtin_isfinite(punch.value().x) || !__builtin_isfinite(punch.value().y))
            return;

        int sequence{};
        std::memcpy(&sequence, reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::kCommandNumberOffset, sizeof(sequence));
        const float scale = static_cast<float>(GET_CONFIG_VAR(rcs_vars::Strength)) / 100.0f;
        const auto delta = state.correction(sequence, punch.value().x * scale, punch.value().y * scale);
        // The camera carries earlier commands' compensation. Only the change in kick
        // belongs in this command; adding the full kick again drives the pitch down.
        recoil_compensation::apply(cmd, delta.pitch, delta.yaw);
        // Keep tracking through recoil recovery so the last correction is unwound.
        if (shots.value() < 1 && punch.value().x == 0.0f && punch.value().y == 0.0f)
            state.reset();
    }

private:
    inline static recoil_compensation::CommandState state{};
    inline static cs2::C_BaseEntity* activePawn{};
    HookContext& hookContext;
};
