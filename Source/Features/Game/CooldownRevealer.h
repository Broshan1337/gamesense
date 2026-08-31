#pragma once

#include <cstdint>

#include <Features/Game/CooldownRevealerConfigVariables.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <Utils/BytePatch.h>

// Shows the matchmaking-cooldown notice for ANY player, not just yourself.
//
// The client already receives these notices about other players and simply declines to display
// them: the handler compares the SteamID in the message against your own and only shows it on a
// match. This forces that comparison's branch, so a notice the client already holds gets rendered.
// Nothing extra is requested and nothing is transmitted.
//
// This is the project's only patch to game CODE (everything else hooks or reads), so it is applied
// only while the toggle is on, and is always restored - both when toggled off and on unload.
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
    // `jz <display>` -> `nop; jmp <display>`.
    //
    // Only the two opcode bytes are replaced; the original rel32 that follows is deliberately left
    // untouched. A jz's displacement is relative to the end of its own 6-byte instruction, and
    // after the patch the 5-byte jmp ends at that same address, so the destination is unchanged.
    // Recomputing it would be unnecessary and easy to get wrong.
    //
    // This is the OPPOSITE of the Windows equivalent, which NOPs a `jnz`. The Linux compiler
    // emitted the inverted branch - taken when the notice IS about you - so NOPping it here would
    // hide every notice rather than reveal them.
    void applyPatch() const noexcept
    {
        const auto gate = hookContext.patternSearchResults().template get<AbandonCooldownGate>();
        if (!gate)
            return;

        static constexpr std::uint8_t kForceJump[]{0x90, 0xE9};
        if (!patch.apply(const_cast<void*>(gate), kForceJump))
            return; // retried by run() while the feature stays enabled
    }

    // Process-lifetime: the patch outlives any single frame and must be restorable from onUnload.
    inline static BytePatch<2> patch{};

    HookContext& hookContext;
};
