#pragma once

#include <utility>

#include <CS2/Classes/Color.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <Features/Game/PlayerAnalyzer/CheatOMeterState.h>
#include <Features/Game/PlayerAnalyzer/PlayerAnalyzerConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Panorama/PanoramaLabel.h>
#include <Utils/StringBuilder.h>

#include "PlayerAnalyzerTagPanelContext.h"

// In-world "CHEAT O METER" tag: the analyzer's current verdict for THIS pawn, shown as one
// colored text line under the player info container. Only scanned players get a tag (selected
// in the menu AND sampled at least once). Runs on the game thread; reads the analyzer's
// per-slot stats directly (same thread writes them - no locking needed beyond the selection
// set, which is spinlock-guarded in cheat_ometer).
template <typename HookContext, typename Context = PlayerAnalyzerTagPanelContext<HookContext>>
class PlayerAnalyzerTagPanel {
public:
    template <typename... Args>
    explicit PlayerAnalyzerTagPanel(Args&&... args) noexcept
        : context{std::forward<Args>(args)...}
    {
    }

    void update(auto&& playerPawn) const noexcept
    {
        const auto tag = tagForPawn(playerPawn);
        if (!tag.active) {
            context.panel().setVisible(false);
            return;
        }

        context.panel().setVisible(true);

        auto&& labelPanel = context.panel().children()[0];

        if (context.cache().analyzerTagScore(tag.score))
            labelPanel.clientPanel().template as<PanoramaLabel>().setText(verdictText(tag.score));

        if (const auto color = verdictColor(tag.score); context.cache().analyzerTagColor(color))
            labelPanel.setColor(color);
    }

private:
    [[nodiscard]] auto tagForPawn(auto&& playerPawn) const noexcept
    {
        if (!context.config().template getVariable<analyzer_vars::Enabled>() || !context.config().template getVariable<analyzer_vars::EspTag>())
            return cheat_ometer::Tag{};

        if (!playerPawn.isTTorCT() || playerPawn.isControlledByLocalPlayer())
            return cheat_ometer::Tag{};

        auto* const controllerEntity = static_cast<cs2::C_BaseEntity*>(playerPawn.playerController().baseEntity());
        if (!controllerEntity)
            return cheat_ometer::Tag{};

        // controller slot (picker/event numbering) - never the pawn's entity index
        const auto slot = static_cast<int>(context.hookContext().template make<BaseEntity>(controllerEntity).handle().index().value) - 1;
        return cheat_ometer::tagForSlot(slot);
    }

    [[nodiscard]] static const char* verdictText(int score) noexcept
    {
        if (score >= 70)
            return "CHEAT?";
        if (score >= 40)
            return "SUSPECT";
        return "OK";
    }

    [[nodiscard]] static cs2::Color verdictColor(int score) noexcept
    {
        if (score >= 70)
            return cs2::Color{255, 71, 71};   // red
        if (score >= 40)
            return cs2::Color{255, 180, 60};  // amber
        return cs2::Color{120, 243, 79};      // green
    }

    Context context;
};
