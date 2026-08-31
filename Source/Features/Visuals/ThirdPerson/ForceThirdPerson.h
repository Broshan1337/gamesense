#pragma once

#include <CS2/Classes/ViewSetup.h>
#include <Features/Visuals/ThirdPerson/ThirdPersonConfigVariables.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Trig.h>

// FrameworkCS2 port (Source/Features/Visuals/ForceThirdPerson): repositions the camera behind
// the eye position along the inverse view direction by overriding the view setup from the
// ClientModeCSNormal::OverrideView hook (called AFTER the original, which fills the setup).
//
// Differences from the reference: the eye position comes from the pawn's own eyePosition()
// (absOrigin + m_vecViewOffset) instead of a hardcoded 64/46 eye-height lerp, and observer
// follow-cam support is not ported (local-only scope; alive-only v1).
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

        // The ORIGINAL OverrideView just filled the view setup from the live camera - its angles
        // ARE the first-person view, so the third-person camera orbits exactly with it. (The
        // CSGOInput +0x7C0 angles the FrameworkCS2 reference reads turned out to NOT track the
        // live view on this build - the camera stayed put while the player turned.)
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
        // angles stay as the original wrote them - the camera looks where the first-person view looks
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
