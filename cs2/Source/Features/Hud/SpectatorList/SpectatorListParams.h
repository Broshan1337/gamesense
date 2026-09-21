#pragma once

#include <Config/ConfigVariable.h>
#include <CS2/Constants/ColorConstants.h>
#include <CS2/Classes/Color.h>
#include <CS2/Panorama/CUILength.h>
#include <CS2/Panorama/StyleEnums.h>

#include <GameClient/Panorama/PanelAlignmentParams.h>
#include <GameClient/Panorama/PanelFontParams.h>
#include <GameClient/Panorama/PanelMarginParams.h>

// Spectator list: a box directly under the watermark (top-right). Visible only while someone is
// actually spectating the local player.
namespace spectator_list_params
{
    static constexpr cs2::Color kTextColor{0xA3, 0xD4, 0x1F};
    static constexpr cs2::Color kBoxColor{16, 20, 12, 165};
    static constexpr auto kBoxBorderRadius = cs2::CUILength::pixels(4);

    static constexpr auto kFont = PanelFontParams{
        .fontFamily = "Stratum2 Bold TF, 'Arial Unicode MS'",
        .fontSize = 12};
    static constexpr auto kAlignment = PanelAlignmentParams{
        .horizontalAlignment = cs2::k_EHorizontalAlignmentRight,
        .verticalAlignment = cs2::k_EVerticalAlignmentTop};
    // Under the watermark: its box starts at 10px top + ~26px height + gap.
    static constexpr auto kBoxMargin = PanelMarginParams{
        .marginTop = cs2::CUILength::pixels(44),
        .marginRight = cs2::CUILength::pixels(10)};
    static constexpr auto kLineMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(10),
        .marginTop = cs2::CUILength::pixels(1),
        .marginRight = cs2::CUILength::pixels(10),
        .marginBottom = cs2::CUILength::pixels(1)};
}

namespace spectator_list_params
{
// Hud > Spectators: the right-side box listing who is watching the POV (ours when alive,
// the spectated player's when we are dead and following someone).
CONFIG_VARIABLE(SpectatorListEnabled, bool, true);
}
