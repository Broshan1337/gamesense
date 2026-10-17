#pragma once

#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/CUserCmd.h>
#include <GameClient/UserCmd.h>

template <typename HookContext>
class AttackCommand {
public:
    explicit AttackCommand(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // Retained for feature lifecycle callers; presses no longer share a global toggle.
    static void reset() noexcept {}

    bool press(cs2::CUserCmd* cmd) const noexcept
    {
        const UserCmd userCmd{cmd};
        if (!userCmd || !userCmd.pressButtonsBothBanks(cs2::CCSGOInput::Buttons::kAttack))
            return false;
        // The attack START HISTORY INDEX is mandatory (2026-10-15, the
        // "sv: Firing first shot, but no attack information present...
        // attack1_start_history_index: -1" server warnings in the friend's
        // log): a sent attack without it degrades lag comp to the fallback
        // path. The in-progress command's input history is rebuilt by the
        // game every frame and the size read can miss on the exact frame
        // that ends up being sent - refuse the press then; every caller
        // runs per frame and simply fires a frame later with a complete
        // attack record.
        const auto historySize = userCmd.inputHistorySize();
        if (!historySize.hasValue())
            return false;
        static_cast<void>(userCmd.setAttack1StartHistoryIndex(historySize.value() - 1));
        return true;
    }

private:
    HookContext& hookContext;
};
