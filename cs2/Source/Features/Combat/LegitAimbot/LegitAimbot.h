#pragma once

#include <cstdint>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Vector.h>
#include <Features/Combat/AimTarget.h>
#include <Features/Combat/LegitAimbot/LegitAimbotConfigVariables.h>
#include <Features/Combat/MovementFix.h>
#include <GameClient/Tracing/Tracing.h>
#include <GameClient/Bind.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <SDL/SdlFunctions.h>
#include <Utils/Optional.h>
#include <Utils/Random.h>
#include <Utils/Trig.h>







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

    
    
    
    
    
    
    
    
    [[nodiscard]] Step humanizedStep(float deltaPitch, float deltaYaw) const noexcept
    {
        const float deltaLength = trig::squareRoot(deltaPitch * deltaPitch + deltaYaw * deltaYaw);
        if (deltaLength < 0.001f)
            return {0.0f, 0.0f};

        const float baseSmooth = static_cast<float>(GET_CONFIG_VAR(legit_aimbot_vars::Smooth));

        
        
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

    
    
    
    
    

    inline static std::uint32_t lastTargetHandleValue{0};
    HookContext& hookContext;
};
