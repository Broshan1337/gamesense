#pragma once

#include <Features/Combat/Autowall/Penetration.h>
#include <Features/Combat/Autowall/HealthDamage.h>
#include <GameClient/Tracing/Tracing.h>
#include <GameClient/Tracing/BulletSimulation.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <Utils/VerifyConsole.h>

#include <atomic>

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
            }, [&](void* entity) { return isPenetrable(context, entity); });
    }

    // True when evaluateAimPoint() is answered by the game's own bullet
    // simulation (exact), false when it uses the trace estimate.
    [[nodiscard]] static bool usesEngine() noexcept
    {
        return static_cast<bool>(bullet_simulation::binding());
    }

    // Damage of a shot aimed at a point on the target (the ragebot's question).
    //  - Engine (primary): the game's own FireBullet simulation - exact, with
    //    the hitgroup actually struck. Only on inspected client builds.
    //  - Fallback: the build-independent trace estimate, used when the loaded
    //    client build is not recognised. It has no material data, so damage
    //    through walls is approximate, and it cannot see which hitbox is
    //    struck: the aimed hitgroup is reported instead.
    // The mode in use is on the menu toggle ("Shoot Walls (Engine)" or
    // "Shoot Walls (Fallback: estimate)") and announced once in the console.
    template <typename HookContext>
    static Optional<penetration::Impact> evaluateAimPoint(const cs2::Vector& start, const cs2::Vector& aimPoint,
        void* skip, void* target, penetration::Bullet bullet, penetration::Limits limits,
        penetration::TraceBudget& budget, int aimedHitgroup, HookContext& context) noexcept
    {
        const auto& native = bullet_simulation::binding();
        if (!native.resolved) return {}; // another thread is still validating the engine ABI
        if (!announced.exchange(true, std::memory_order_acq_rel))
            VerifyConsole::write(0.0f, "autowall", native ? "shoot walls: ENGINE simulation (exact)"
                : "shoot walls: FALLBACK estimate - client build not recognised, wall damage is approximate");
        if (native)
            return evaluate(start, aimPoint, skip, target, bullet, limits, budget, context);
        const auto estimate = penetration::simulate(start, aimPoint, skip, target, bullet, limits, budget,
            [](const auto& from, const auto& to, void* entity) {
                return Tracing::traceLine(from, to, entity, kBulletMask);
            }, [&](void* entity) { return isPenetrable(context, entity); });
        if (!estimate.hasValue()) return {};
        penetration::Impact impact = estimate.value();
        impact.hitgroup = aimedHitgroup;
        return impact;
    }

    // World and props can be shot through; another player blocks the shot.
    template <typename HookContext>
    [[nodiscard]] static bool isPenetrable(HookContext& context, void* entity) noexcept
    {
        auto* raw = static_cast<cs2::C_BaseEntity*>(entity);
        return raw && raw->identity && raw->identity->entityClass
            && context.entityClassifier().initialized()
            && !context.template make<BaseEntity>(raw).template is<PlayerPawn>();
    }

    inline static std::atomic<bool> announced{false};

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
