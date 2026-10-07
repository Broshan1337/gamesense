#pragma once

#include <CS2/Classes/Color.h>
#include <CS2/Panorama/CUIPanel.h>
#include <CS2/Panorama/CUILength.h>
#include <CS2/Panorama/StyleEnums.h>

#include <GameClient/Panorama/PanelAlignmentParams.h>
#include <GameClient/Panorama/PanelFontParams.h>
#include <GameClient/Panorama/PanelMarginParams.h>
#include <GameClient/Panorama/PanoramaLabel.h>







namespace chip_panel
{
    static constexpr cs2::Color kFillColor{13, 13, 16, 215};
    static constexpr std::uint8_t kBorderAlpha = 16;

    
    
    
    
    template <typename HookContext>
    [[nodiscard]] auto createChipPanel(HookContext& hookContext, cs2::CUIPanel* parent,
        const PanelMarginParams& outerMargin, float radiusPx = 10.0f) noexcept
    {
        auto&& pill = hookContext.panelFactory().createPanel(parent).uiPanel();
        pill.setFlowChildren(cs2::k_EFlowRight);
        pill.setBackgroundColor(kFillColor);
        pill.setBorderRadius(cs2::CUILength::pixels(radiusPx));
        pill.setBorder(cs2::CUILength::pixels(1), cs2::Color{255, 255, 255, kBorderAlpha});
        pill.setMargin(outerMargin);
        return pill;
    }
}
