#pragma once

#include <CS2/Classes/Entities/GrenadeProjectiles.h>
#include <MemoryPatterns/PatternTypes/SmokeGrenadeProjectilePatternTypes.h>

#include "BaseEntity.h"

template <typename HookContext>
class SmokeGrenadeProjectile {
public:
    SmokeGrenadeProjectile(HookContext& hookContext, cs2::C_SmokeGrenadeProjectile* smokeGrenadeProjectile) noexcept
        : hookContext{hookContext}
        , smokeGrenadeProjectile{smokeGrenadeProjectile}
    {
    }

    using RawType = cs2::C_SmokeGrenadeProjectile;

    [[nodiscard]] decltype(auto) baseEntity() const noexcept
    {
        return hookContext.template make<BaseEntity>(smokeGrenadeProjectile);
    }

    [[nodiscard]] auto didSmokeEffect() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToDidSmokeEffect>().of(smokeGrenadeProjectile).toOptional();
    }

    [[nodiscard]] auto smokeEffectTickBegin() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToSmokeEffectTickBegin>().of(smokeGrenadeProjectile).toOptional();
    }

private:
    HookContext& hookContext;
    cs2::C_SmokeGrenadeProjectile* smokeGrenadeProjectile;
};
