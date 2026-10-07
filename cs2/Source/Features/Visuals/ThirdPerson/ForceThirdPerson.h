#pragma once

#include <CS2/Classes/ViewSetup.h>
#include <Features/Visuals/ThirdPerson/ThirdPersonConfigVariables.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Trig.h>








template <typename HookContext>
class ForceThirdPerson {
public:
    explicit ForceThirdPerson(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void overrideView(ViewSetup* viewSetup) const noexcept
    {
        if (!viewSetup || !GET_CONFIG_VAR(ForceThirdPersonEnabled))
            return;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn)
            return;

        const auto eye = localPawn.eyePosition();
        if (!eye.hasValue())
            return;

        
        
        
        
        const auto& angles = ViewSetup::viewAngles(viewSetup);
        if (!looksLikeAngles(angles))
            return;

        const float distance = GET_CONFIG_VAR(ForceThirdPersonDistance);
        if (distance <= 0.0f)
            return;

        const float pitchRadians = angles.x * trig::kDegreesToRadians;
        const float yawRadians = angles.y * trig::kDegreesToRadians;
        const float pitchCosine = trig::cosine(pitchRadians);
        const cs2::Vector viewDirection{
            trig::cosine(yawRadians) * pitchCosine,
            trig::sine(yawRadians) * pitchCosine,
            -trig::sine(pitchRadians),
        };

        auto& cameraPosition = ViewSetup::cameraPosition(viewSetup);
        const auto& eyePosition = eye.value();
        cameraPosition.x = eyePosition.x - viewDirection.x * distance;
        cameraPosition.y = eyePosition.y - viewDirection.y * distance;
        cameraPosition.z = eyePosition.z - viewDirection.z * distance;
        
    }

private:
    [[nodiscard]] static bool looksLikeAngles(const cs2::Vector& angles) noexcept
    {
        constexpr float kMaxPitch = 90.1f;
        constexpr float kMaxYaw = 180.1f;
        return angles.x >= -kMaxPitch && angles.x <= kMaxPitch
            && angles.y >= -kMaxYaw && angles.y <= kMaxYaw
            && angles.z >= -kMaxPitch && angles.z <= kMaxPitch;
    }

    HookContext& hookContext;
};
