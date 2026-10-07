#pragma once

#include <CS2/Classes/GlobalVars.h>
#include <MemoryPatterns/PatternTypes/GlobalVarsPatternTypes.h>
#include <Utils/Optional.h>

template <typename HookContext>
struct GlobalVars {
    [[nodiscard]] Optional<float> curtime() const noexcept
    {
        if (globalVars)
            return globalVars->curtime;
        return {};
    }

    [[nodiscard]] Optional<float> frametime() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToFrametime>().of(globalVars).toOptional();
    }

    [[nodiscard]] Optional<std::int32_t> tickCount() const noexcept
    {
        if (globalVars && globalVars->tickCount > 0)
            return globalVars->tickCount;
        return {};
    }

    
    
    
    
    
    
    
    [[nodiscard]] Optional<float> tickInterval() const noexcept
    {
        if (!globalVars)
            return {};
        if (globalVars->intervalPerTick >= 1.0f / 128.0f && globalVars->intervalPerTick <= 1.0f / 32.0f)
            return globalVars->intervalPerTick;
        return 0.015625f;
    }

    HookContext& hookContext;
    cs2::GlobalVars* globalVars;
};
