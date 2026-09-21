#pragma once

#include <CS2/Classes/Color.h>
#include <CS2/Panorama/CUIPanel.h>
#include <CS2/Panorama/CUILength.h>
#include <CS2/Panorama/StyleEnums.h>

#include <GameClient/Panorama/PanelAlignmentParams.h>
#include <GameClient/Panorama/PanelFontParams.h>
#include <GameClient/Panorama/PanelMarginParams.h>
#include <GameClient/Panorama/PanoramaLabel.h>

// Shared "pill chip" chrome for the HUD overlays (watermark row). One panel per chip:
// translucent dark fill with a GENEROUS border radius and a 1px hairline outline - the crisp,
// polished look. Deliberately NO nested glow panels and NO text shadows: Panorama's non-flow
// outer panels do not shrink-wrap their children (the glow ring rendered as a huge full-accent
// box behind the pill) and text shadows render far too heavy at HUD scale. Colors: fill
// near-black translucent; the accent lives in the text.
namespace chip_panel
{
    static constexpr cs2::Color kFillColor{13, 13, 16, 215};
    static constexpr std::uint8_t kBorderAlpha = 16;

    // Creates the chip panel as a child of `parent` with the caller's outer margin (the gap
    // between chips, or the feed's row margin). `radiusPx` overrides the corner rounding -
    // pass something like 100 for a FULL pill (CSS-style radius clamping rounds it to half
    // the panel height). Labels attach directly to the returned panel.
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
