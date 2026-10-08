#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Vector.h>
#include <Features/Combat/MovementFix.h>
#include <Features/Game/MovementConfigVariables.h>
#include <GameClient/Entities/BaseWeapon.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

















template <typename HookContext>
class SuperToss {
public:
    explicit SuperToss(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        reset();

        if (!GET_CONFIG_VAR(supertoss_vars::Enabled))
            return;

        const UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return;
        const auto health = localPawn.health();
        if (!health.hasValue() || health.value() <= 0)
            return;

        auto&& activeWeapon = localPawn.getActiveWeapon();
        auto* const weaponEntity = static_cast<cs2::C_BaseEntity*>(activeWeapon.baseEntity());
        if (!weaponEntity)
            return;

        
        
        const auto throwTime = schemaFloat("C_BaseCSGrenade", "m_fThrowTime", weaponEntity);
        if (!throwTime.has_value() || throwTime.value() <= 0.0f)
            return;

        const auto throwStrength = schemaFloat("C_BaseCSGrenade", "m_flThrowStrength", weaponEntity);
        const auto vdataThrowVelocity = activeWeapon.vDataFloat("m_flThrowVelocity");
        const auto viewPitch = userCmd.viewPitch();
        const auto viewYaw = userCmd.viewYaw();
        const auto velocity = readVelocity(weaponEntity);
        if (!throwStrength.has_value() || !vdataThrowVelocity.hasValue() || vdataThrowVelocity.value() <= 0.0f
            || !viewPitch.hasValue() || !viewYaw.hasValue() || !velocity.hasValue())
            return;

        
        
        float strength = std::clamp(throwStrength.value(), 0.0f, 1.0f);
        if (strength >= 0.4f && strength <= 0.6f)
            strength = 0.5f;

        const float baseThrowVelocity = std::clamp(vdataThrowVelocity.value() * 0.9f, 15.0f, 750.0f);
        const float throwSpeed = (strength * 0.7f + 0.3f) * baseThrowVelocity;

        
        
        float pitch = viewPitch.value();
        float yaw = viewYaw.value();
        while (pitch > 180.0f)
            pitch -= 360.0f;
        while (pitch < -180.0f)
            pitch += 360.0f;
        pitch -= (90.0f - std::abs(pitch)) * 10.0f / 90.0f;

        const float pitchRadians = pitch * trig::kDegreesToRadians;
        const float yawRadians = yaw * trig::kDegreesToRadians;
        const cs2::Vector desiredForward{
            trig::cosine(pitchRadians) * trig::cosine(yawRadians),
            trig::cosine(pitchRadians) * trig::sine(yawRadians),
            -trig::sine(pitchRadians)};

        const cs2::Vector& playerVelocity = velocity.value();
        if (playerVelocity.x * playerVelocity.x + playerVelocity.y * playerVelocity.y + playerVelocity.z * playerVelocity.z < 1.0f)
            return;

        constexpr float kVelocityInheritance = 1.25f;
        const float velocityContribution[3] = {
            playerVelocity.x * kVelocityInheritance,
            playerVelocity.y * kVelocityInheritance,
            playerVelocity.z * kVelocityInheritance};

        const float velocityAlongDesired = velocityContribution[0] * desiredForward.x
            + velocityContribution[1] * desiredForward.y
            + velocityContribution[2] * desiredForward.z;
        const float perpendicular[3] = {
            velocityContribution[0] - desiredForward.x * velocityAlongDesired,
            velocityContribution[1] - desiredForward.y * velocityAlongDesired,
            velocityContribution[2] - desiredForward.z * velocityAlongDesired};
        const float perpendicularLengthSquared = perpendicular[0] * perpendicular[0]
            + perpendicular[1] * perpendicular[1]
            + perpendicular[2] * perpendicular[2];

        
        
        if (perpendicularLengthSquared >= throwSpeed * throwSpeed)
            return;

        const float forwardComponent = trig::squareRoot(throwSpeed * throwSpeed - perpendicularLengthSquared);
        const float totalAlongDesired = velocityAlongDesired + forwardComponent;
        if (totalAlongDesired <= 0.0f)
            return;

        
        
        float corrected[3] = {
            (desiredForward.x * totalAlongDesired - velocityContribution[0]) / throwSpeed,
            (desiredForward.y * totalAlongDesired - velocityContribution[1]) / throwSpeed,
            (desiredForward.z * totalAlongDesired - velocityContribution[2]) / throwSpeed};
        const float correctedLength = trig::squareRoot(corrected[0] * corrected[0]
            + corrected[1] * corrected[1] + corrected[2] * corrected[2]);
        if (correctedLength < 0.001f)
            return;
        corrected[0] /= correctedLength;
        corrected[1] /= correctedLength;
        corrected[2] /= correctedLength;

        
        const float correctedPitch = -trig::arcSine(std::clamp(corrected[2], -1.0f, 1.0f)) * trig::kRadiansToDegrees;
        const float correctedYaw = trig::arcTangent2(corrected[1], corrected[0]) * trig::kRadiansToDegrees;

        
        
        
        float inputPitch = correctedPitch;
        for (int i = 0; i < 16; ++i)
            inputPitch = correctedPitch + (90.0f - std::abs(inputPitch)) * 10.0f / 90.0f;

        pendingPitch = std::clamp(inputPitch, -89.0f, 89.0f);
        pendingYaw = correctedYaw;
        hasPendingAngles = true;
    }

    
    
    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        if (!cmd || !hasPendingAngles)
            return;

        const UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        
        
        movement_fix::setViewAngles(userCmd, pendingPitch, pendingYaw);
    }

    void onUnload() const noexcept
    {
        reset();
    }

private:
    void reset() const noexcept
    {
        hasPendingAngles = false;
        pendingPitch = 0.0f;
        pendingYaw = 0.0f;
    }

    [[nodiscard]] std::optional<float> schemaFloat(const char* className, const char* fieldName, const void* object) const noexcept
    {
        if (!object)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset(className, fieldName);
        if (!offset.has_value() || *offset <= 0)
            return {};
        float value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(object) + *offset, sizeof(value));
        return value;
    }

    [[nodiscard]] Optional<cs2::Vector> readVelocity(const void* pawnEntity) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_vecAbsVelocity");
        if (!offset.has_value() || *offset <= 0)
            return {};
        cs2::Vector velocity{};
        std::memcpy(&velocity, reinterpret_cast<const std::byte*>(pawnEntity) + *offset, sizeof(velocity));
        return velocity;
    }

    inline static bool hasPendingAngles{false};
    inline static float pendingPitch{0.0f};
    inline static float pendingYaw{0.0f};

    HookContext& hookContext;
};