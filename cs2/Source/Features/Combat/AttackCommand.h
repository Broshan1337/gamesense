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
        if (const auto historySize = userCmd.inputHistorySize(); historySize.hasValue())
            static_cast<void>(userCmd.setAttack1StartHistoryIndex(historySize.value() - 1));
        return true;
    }

private:
    HookContext& hookContext;
};
