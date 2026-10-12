#pragma once

#include <cstdint>

#include <Features/Game/CooldownRevealerConfigVariables.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <Utils/BytePatch.h>










template <typename HookContext>
class CooldownRevealer {
public:
    explicit CooldownRevealer(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        const auto shouldBeApplied = GET_CONFIG_VAR(CooldownRevealerEnabled);
        if (shouldBeApplied == patch.isApplied())
            return;

        if (shouldBeApplied)
            applyPatch();
        else
            patch.restore();
    }

    void onUnload() const noexcept
    {
        patch.restore();
    }

private:
    
    
    
    
    
    
    
    
    
    
    void applyPatch() const noexcept
    {
        const auto gate = hookContext.patternSearchResults().template get<AbandonCooldownGate>();
        if (!gate)
            return;

        static constexpr std::uint8_t kForceJump[]{0xEB};
        if (!patch.apply(const_cast<void*>(gate), kForceJump))
            return; 
    }

    
    inline static BytePatch<1> patch{};

    HookContext& hookContext;
};
