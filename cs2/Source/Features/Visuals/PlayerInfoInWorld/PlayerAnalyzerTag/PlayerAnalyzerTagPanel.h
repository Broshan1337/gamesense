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
            return cs2::Color{255, 71, 71};   
        if (score >= 40)
            return cs2::Color{255, 180, 60};  
        return cs2::Color{120, 243, 79};      
    }

    Context context;
};
