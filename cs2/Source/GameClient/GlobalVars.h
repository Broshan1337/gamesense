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

    // The true tick duration (interval_per_tick), read straight out of the struct rather than from
    // the ambiguous frametime pattern, and sanity-bounded to the range a real CS2 tickrate can
    // produce (CS2 is 64-tick; 1/128..1/32 covers any conceivable server). The window matters:
    // when the Aug-29 build moved interval_per_tick from 0x40 to 0x1C, the dead field at 0x40 fed
    // denormal junk through the old 0..0.1 window and froze every sim dividing the tick by it -
    // a value outside the real range means the offset is wrong, and 1/64 (velocity-cs2's own
    // hardcoded constant) is the correct degradation on any current server.
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
