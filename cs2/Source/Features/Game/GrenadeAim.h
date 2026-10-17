#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Vector.h>
#include <Features/Game/GrenadeThrows.h>
#include <Features/Game/GrenadeAimConfigVariables.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <Features/Combat/MovementFix.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

// Saved-throw grenade aim (deadlocked's design, 2026-10-15 port): while
// holding a live grenade, aim at the recorded view angles of the CLOSEST
// saved throw of the SAME weapon on the current map. The throw physics is
// untouched - SuperToss still corrects the release; this only steers.
template <typename HookContext>
class GrenadeAim {
public:
    explicit GrenadeAim(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        pendingPitch = 0.0f;
        pendingYaw = 0.0f;
        hasPending = false;

        if (!GET_CONFIG_VAR(grenade_aim_vars::Enabled) || !UserCmd{cmd})
            return;
        if (!grenade_throws::currentMap[0] || grenade_throws::throwCount <= 0)
            return;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn || localPawn.isAlive() != true)
            return;
        auto&& weapon = localPawn.getActiveWeapon();
        auto* const weaponEntity = static_cast<cs2::C_BaseEntity*>(weapon.baseEntity());
        if (!weaponEntity)
            return;

        // In-hand grenade test: the throw-strength field exists only on
        // C_BaseCSGrenade, and m_fThrowTime > 0 only while the pin is pulled
        // / throwing - never in the idle hand, so the aim must not fight
        // normal play (deadlocked checks WeaponClass == Grenade).
        const auto throwStrength = grenadeSchemaFloat(weaponEntity, "m_flThrowStrength");
        if (!throwStrength.hasValue() || throwStrength.value() < 0.0f || throwStrength.value() > 1.0f)
            return;
        const auto throwTime = grenadeSchemaFloat(weaponEntity, "m_fThrowTime");
        if (!throwTime.hasValue() || throwTime.value() <= 0.0f)
            return;

        const auto position = localPawn.baseEntity().absOrigin();
        const auto pitch = UserCmd{cmd}.viewPitch();
        const auto yaw = UserCmd{cmd}.viewYaw();
        if (!position.hasValue() || !pitch.hasValue() || !yaw.hasValue())
            return;

        const int defIndex = weapon.itemDefinitionIndex().valueOr(0);
        constexpr float kMaxDistance = 150.0f;
        constexpr float kMaxFov = 30.0f;

        float bestFov = kMaxFov;
        bool found = false;
        for (int i = 0; i < grenade_throws::throwCount; ++i) {
            const auto& entry = grenade_throws::throws[i];
            if (!entry.valid || entry.weaponDefIndex != defIndex)
                continue;
            if (std::strcmp(entry.map, grenade_throws::currentMap) != 0)
                continue;
            const auto dx = entry.position.x - position.value().x;
            const auto dy = entry.position.y - position.value().y;
            const auto dz = entry.position.z - position.value().z;
            if (dx * dx + dy * dy + dz * dz > kMaxDistance * kMaxDistance)
                continue;
            const auto angles = shotAngles(position.value(), entry.position);
            const float dPitch = trig::normalizeDegrees(angles.pitch - pitch.value());
            const float dYaw = trig::normalizeDegrees(angles.yaw - yaw.value());
            const float fov = trig::squareRoot(dPitch * dPitch + dYaw * dYaw);
            if (fov < bestFov) {
                bestFov = fov;
                pendingPitch = entry.pitch;
                pendingYaw = entry.yaw;
                found = true;
            }
        }
        hasPending = found;
    }

    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        if (!cmd || !hasPending)
            return;
        movement_fix::setViewAngles(UserCmd{cmd}, pendingPitch, pendingYaw);
    }

    void onUnload() const noexcept
    {
        hasPending = false;
    }

private:
    struct Angles {
        float pitch;
        float yaw;
    };

    [[nodiscard]] Angles shotAngles(const cs2::Vector& from, const cs2::Vector& to) const noexcept
    {
        const cs2::Vector delta{to.x - from.x, to.y - from.y, to.z - from.z};
        const float horizontal = trig::squareRoot(delta.x * delta.x + delta.y * delta.y);
        return Angles{-trig::arcTangent2(delta.z, horizontal) * trig::kRadiansToDegrees,
            trig::arcTangent2(delta.y, delta.x) * trig::kRadiansToDegrees};
    }

    [[nodiscard]] Optional<float> grenadeSchemaFloat(cs2::C_BaseEntity* entity, const char* field) const noexcept
    {
        // Both m_flThrowStrength and m_fThrowTime are C_BaseCSGrenade fields
        // (schema-resolved; the field only exists on grenade weapons, so a
        // non-grenade resolves empty and the caller stands down).
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseCSGrenade", field);
        if (!offset.has_value() || *offset <= 0)
            return {};
        float value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(entity) + *offset, sizeof(value));
        return value;
    }

    mutable float pendingPitch{0.0f};
    mutable float pendingYaw{0.0f};
    mutable bool hasPending{false};

    HookContext& hookContext;
};