#pragma once

#include <Features/Combat/Autowall/Penetration.h>
#include <Features/Combat/Autowall/HealthDamage.h>
#include <GameClient/Tracing/Tracing.h>
#include <GameClient/Tracing/BulletSimulation.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>

class Autowall {
public:
    // Existing Lua API supplies damage-at-point and power, without weapon range
    // metadata. Preserve that contract using the same bounded neutral model.
    static Optional<float> penetratedDamage(const cs2::Vector& start, const cs2::Vector& end,
        void* skip, void* target, float damage, float power) noexcept
    {
        return penetration::estimate(start, end, skip, target, damage, power,
            [](const auto& from, const auto& to, void* entity) {
                return Tracing::traceLine(from, to, entity, kBulletMask);
            });
    }

    template <typename HookContext>
    static Optional<penetration::Impact> evaluate(const cs2::Vector& start, const cs2::Vector& end,
        void* skip, void* target, penetration::Bullet bullet, penetration::Limits limits,
        penetration::TraceBudget& budget, HookContext& context) noexcept
    {
        const int team = static_cast<int>(context.activeLocalPlayerPawn().teamNumber());
        if (team != 2 && team != 3) return {};
        return bullet_simulation::simulate(start, end, skip, target, bullet, limits, budget, team,
            [&](std::uint32_t handle) -> void* {
                return context.template make<EntitySystem>().getEntityFromHandle(cs2::CEntityHandle{handle});
            }, [&](void* entity) {
                auto* raw = static_cast<cs2::C_BaseEntity*>(entity);
                return raw && raw->identity && raw->identity->entityClass
                    && context.entityClassifier().initialized()
                    && !context.template make<BaseEntity>(raw).template is<PlayerPawn>();
            });
    }

    static constexpr std::uint64_t kBulletMask = 0x1C300B;

    template <typename HookContext>
    static Optional<float> healthDamage(HookContext& context, void* target, float damage,
        int hitgroup, float armorRatio, float headMultiplier) noexcept
    {
        if (!target) return {};
        auto&& schema = context.schemaSystem();
        const auto armorOffset = schema.getFieldOffset("C_CSPlayerPawn", "m_ArmorValue");
        if (!armorOffset.has_value() || *armorOffset <= 0) return {};
        int armor{};
        if (!LinuxPlatformApi::safeRead(static_cast<const std::byte*>(target) + *armorOffset, &armor, sizeof(armor))
            || armor < 0) return {};
        bool helmet = false;
        if (armor > 0 && hitgroup == 1) {
            const auto servicesOffset = schema.getFieldOffset("C_BasePlayerPawn", "m_pItemServices");
            const auto helmetOffset = schema.getFieldOffset("CCSPlayer_ItemServices", "m_bHasHelmet");
            if (!servicesOffset.has_value() || *servicesOffset <= 0 || !helmetOffset.has_value() || *helmetOffset <= 0) return {};
            void* services{};
            if (!LinuxPlatformApi::safeRead(static_cast<const std::byte*>(target) + *servicesOffset, &services, sizeof(services))
                || !services || !LinuxPlatformApi::safeRead(static_cast<const std::byte*>(services) + *helmetOffset, &helmet, sizeof(helmet))) return {};
        }
        return penetration::healthDamage(damage, hitgroup, armor, helmet, armorRatio, headMultiplier);
    }
};
