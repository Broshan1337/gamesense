#pragma once

#include <CS2/Classes/Entities/C_Inferno.h>
#include <MemoryPatterns/PatternTypes/InfernoPatternTypes.h>

#include "BaseEntity.h"

template <typename HookContext>
class Inferno {
public:
    using RawType = cs2::C_Inferno;

    Inferno(HookContext& hookContext, cs2::C_Inferno* inferno) noexcept
        : hookContext{hookContext}
        , inferno{inferno}
    {
    }

    [[nodiscard]] decltype(auto) baseEntity() const noexcept
    {
        return hookContext.template make<BaseEntity>(inferno);
    }

    [[nodiscard]] auto fireEffectTickBegin() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToFireEffectTickBegin>().of(inferno).toOptional();
    }

    [[nodiscard]] auto fireLifetime() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToFireLifetime>().of(inferno).toOptional();
    }

private:
    HookContext& hookContext;
    cs2::C_Inferno* inferno;
};
