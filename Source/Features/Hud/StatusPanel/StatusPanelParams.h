#pragma once

#include <CS2/Classes/Color.h>
#include <CS2/Constants/ColorConstants.h>
#include <CS2/Panorama/CUILength.h>
#include <CS2/Panorama/StyleEnums.h>

#include <GameClient/Panorama/PanelAlignmentParams.h>
#include <GameClient/Panorama/PanelFontParams.h>
#include <GameClient/Panorama/PanelMarginParams.h>

// The bottom-left status box (AIM / TRIG / BLOCK chips), parked just above the money/chat HUD.
// Chip semantics - three states, color-coded:
//   bright green = feature enabled AND its hold key is down (actively working right now)
//   dim green    = feature enabled, key up (idle, waiting for the hold)
//   gray         = feature disabled
namespace status_panel_params
{
    static constexpr cs2::Color kHeldColor{0xA3, 0xD4, 0x1F};     // same green as the watermark
    static constexpr cs2::Color kIdleColor{0x51, 0x69, 0x14};     // ~half-brightness of the above
    static constexpr cs2::Color kOffColor{110, 110, 110};
    static constexpr cs2::Color kBoxColor{16, 20, 12, 165};
    static constexpr auto kBoxBorderRadius = cs2::CUILength::pixels(4);

    static constexpr auto kFont = PanelFontParams{
        .fontFamily = "Stratum2 Bold TF, 'Arial Unicode MS'",
        .fontSize = 14};
    static constexpr auto kAlignment = PanelAlignmentParams{
        .horizontalAlignment = cs2::k_EHorizontalAlignmentLeft,
        .verticalAlignment = cs2::k_EVerticalAlignmentBottom};
    // Bottom margin clears the vanilla money HUD + chat feed (chat render sits on top of
    // lower-placed panels - verified in-game); nudge if it collides on your resolution.
    static constexpr auto kBoxMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(10),
        .marginBottom = cs2::CUILength::pixels(330)};
    static constexpr auto kChipMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(10),
        .marginTop = cs2::CUILength::pixels(3),
        .marginRight = cs2::CUILength::pixels(10),
        .marginBottom = cs2::CUILength::pixels(3)};
}
