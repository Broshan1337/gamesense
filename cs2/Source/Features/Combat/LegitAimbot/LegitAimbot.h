#pragma once

#include <cstdint>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Vector.h>
#include <Features/Combat/AimTarget.h>
#include <Features/Combat/LegitAimbot/LegitAimbotConfigVariables.h>
#include <Features/Combat/MovementFix.h>
#include <GameClient/Bind.h>
#include <GameClient/Tracing/Tracing.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <SDL/SdlFunctions.h>
#include <Utils/Optional.h>
#include <Utils/Random.h>
#include <Utils/Trig.h>

// Legit aim assist (the Legit tab's aimbot). While the aim key (MOUSE4 / X1) is held, smoothly steps the
// REAL view angles toward the nearest enemy hitbox within a small FOV. Unlike the Rage-tab silent aimbot
// - which redirects the shot inside input_history and never moves the camera - this writes the command's
// own view angles, so the rendered view actually turns, reading like the player aimed. Each tick it
// closes a fraction (1/smooth) of the remaining angle, an exponential ease that looks human rather than a
// snap. Fire is left entirely to the player; this only steers the aim.
template <typename HookContext>
class LegitAimbot {
public:
    explicit LegitAimbot(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(legit_aimbot_vars::Enabled)) {
            lastTargetHandleValue = 0;
            return;
        }


        if (!Bind::isDown(GET_CONFIG_VAR(legit_aimbot_vars::AimKey))) {
            lastTargetHandleValue = 0;
            return;
        }

        UserCmd userCmd{cmd};
        if (!userCmd) {
            lastTargetHandleValue = 0;
            return;
        }

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn || localPawn.isAlive() != true) {
            lastTargetHandleValue = 0;
            return;
        }

        auto aimTarget = hookContext.template make<AimTarget>();
        const auto eye = aimTarget.eyePosition(localPawn);
        const auto currentPitch = userCmd.viewPitch();
        const auto currentYaw = userCmd.viewYaw();
        if (!eye.hasValue() || !currentPitch.hasValue() || !currentYaw.hasValue()) {
            lastTargetHandleValue = 0;
            return;
        }




        float maxFov = static_cast<float>(GET_CONFIG_VAR(legit_aimbot_vars::Fov));
        if (GET_CONFIG_VAR(legit_aimbot_vars::SpreadCircleFov)) {
            if (const auto cone = hookContext.localPlayerBulletInaccuracy(); cone.hasValue() && cone.value() > 0.0f)
                maxFov = trig::arcTangent2(cone.value(), 1.0f) * trig::kRadiansToDegrees;
        }
        cs2::C_BaseEntity* preferredTarget = nullptr;
        if (GET_CONFIG_VAR(legit_aimbot_vars::TargetLock) && lastTargetHandleValue != 0) {
            if (auto* const instance = hookContext.template make<EntitySystem>().getEntityFromHandle(cs2::CEntityHandle{lastTargetHandleValue}))
                preferredTarget = static_cast<cs2::C_BaseEntity*>(instance);
        }
        const auto aim = aimTarget.acquire(eye.value(), currentPitch.value(), currentYaw.value(), maxFov, hitboxFlags(),
            [&](const auto& candidate) {
                return !GET_CONFIG_VAR(legit_aimbot_vars::WallCheck)
                    || Tracing::isVisible(eye.value(), candidate.aimPoint, localPawn.baseEntity(), candidate.entity);
            }, preferredTarget, static_cast<target_selection::Mode>(static_cast<std::uint8_t>(GET_CONFIG_VAR(legit_aimbot_vars::TargetSelection))));
        if (!aim.hasValue()) {
            lastTargetHandleValue = 0;
            return;
        }
        lastTargetHandleValue = hookContext.template make<BaseEntity>(aim.value().entity).handle().value;

        const auto step = humanizedStep(aim.value().angles.pitch - currentPitch.value(), trig::normalizeDegrees(aim.value().angles.yaw - currentYaw.value()));


        movement_fix::setViewAngles(userCmd, currentPitch.value() + step.pitch, currentYaw.value() + step.yaw);
    }

private:
    struct Step {
        float pitch;
        float yaw;
    };

    // The per-tick view step toward the target, ported from velocity-cs2's legit apply_aimbot. Instead of
    // closing a fixed 1/smooth of the remaining angle (a dead-giveaway constant ease), it shapes the step
    // like a hand:
    //   - a distance-based ease (fast far, tightening as it closes) scaled by the Smooth setting,
    //   - a small per-tick speed jitter (~6%) and a slight per-axis bias so pitch and yaw don't track in
    //     lockstep,
    //   - and an occasional tiny overshoot on the final approach.
    // deltaPitch/deltaYaw are the remaining angle to the target (yaw already wrapped to the short way).
    [[nodiscard]] Step humanizedStep(float deltaPitch, float deltaYaw) const noexcept
    {
        const float deltaLength = trig::squareRoot(deltaPitch * deltaPitch + deltaYaw * deltaYaw);
        if (deltaLength < 0.001f)
            return {0.0f, 0.0f};

        const float baseSmooth = static_cast<float>(GET_CONFIG_VAR(legit_aimbot_vars::Smooth));

        // Ease: distance_factor is 1 when far (>=10deg) and 0 when nearly on target; ease = 1 -
        // distance_factor^2 makes the fraction moved small at range and grow as the gap closes.
        float distanceFactor = deltaLength / 10.0f;
        if (distanceFactor > 1.0f)
            distanceFactor = 1.0f;
        const float ease = 1.0f - distanceFactor * distanceFactor;

        float smoothFactor = (0.3f + ease * 0.7f) / baseSmooth;
        smoothFactor *= Random::normalClamped(1.0f, 0.06f, 0.85f, 1.15f);

        const float xBias = Random::normalClamped(1.0f, 0.02f, 0.95f, 1.05f);
        const float yBias = Random::normalClamped(0.97f, 0.03f, 0.90f, 1.04f);

        float movePitch = deltaPitch * smoothFactor * xBias;
        float moveYaw = deltaYaw * smoothFactor * yBias;

        // Occasional small overshoot on the final approach - a human rarely lands a flick dead-on.
        if (deltaLength < 2.0f && deltaLength > 0.3f && Random::floating(0.0f, 1.0f) < 0.15f) {
            const float overshoot = Random::normalClamped(1.2f, 0.08f, 1.05f, 1.4f);
            movePitch *= overshoot;
            moveYaw *= overshoot;
        }

        return {movePitch, moveYaw};
    }

    [[nodiscard]] typename AimTarget<HookContext>::HitboxFlags hitboxFlags() const noexcept
    {
        return {
            .head = GET_CONFIG_VAR(legit_aimbot_vars::HitHead),
            .chest = GET_CONFIG_VAR(legit_aimbot_vars::HitChest),
            .stomach = GET_CONFIG_VAR(legit_aimbot_vars::HitStomach),
            .arms = GET_CONFIG_VAR(legit_aimbot_vars::HitArms),
            .legs = GET_CONFIG_VAR(legit_aimbot_vars::HitLegs),
        };
    }

    // The aim-assist hold key is configurable (legit_aimbot_vars::AimKey). Default MOUSE5 - the
    // same thumb button the triggerbot defaults to, so holding it engages both (aim onto the
    // target and fire when the crosshair lands) - a deliberate "aim and shoot" combo. Bind both
    // features to different keys in the menu if independent control is wanted. The silent aimbot
    // stays on left-click.

    inline static std::uint32_t lastTargetHandleValue{0};
    HookContext& hookContext;
};
