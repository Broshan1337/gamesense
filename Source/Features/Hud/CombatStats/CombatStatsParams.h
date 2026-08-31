#pragma once

#include <CS2/Classes/Color.h>
#include <CS2/Panorama/CUILength.h>
#include <CS2/Panorama/StyleEnums.h>

#include <GameClient/Panorama/PanelAlignmentParams.h>
#include <GameClient/Panorama/PanelFontParams.h>
#include <GameClient/Panorama/PanelMarginParams.h>

// Combat stats HUD: a hits/misses box stacked right above the status chips, and the event feed
// under the radar at the top-left, both in the watermark green.
namespace combat_stats_params
{
    static constexpr cs2::Color kTextColor{0xA3, 0xD4, 0x1F};
    static constexpr cs2::Color kMissColor{235, 95, 80};
    static constexpr cs2::Color kBoxColor{16, 20, 12, 165};
    static constexpr auto kBoxBorderRadius = cs2::CUILength::pixels(4);

    static constexpr auto kFont = PanelFontParams{
        .fontFamily = "Stratum2 Bold TF, 'Arial Unicode MS'",
        .fontSize = 14};
    static constexpr auto kBottomAlignment = PanelAlignmentParams{
        .horizontalAlignment = cs2::k_EHorizontalAlignmentLeft,
        .verticalAlignment = cs2::k_EVerticalAlignmentBottom};
    static constexpr auto kTopAlignment = PanelAlignmentParams{
        .horizontalAlignment = cs2::k_EHorizontalAlignmentLeft,
        .verticalAlignment = cs2::k_EVerticalAlignmentTop};

    // Sits directly above the StatusPanel box (its 330px bottom margin + box height + gap).
    static constexpr auto kCounterBoxMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(10),
        .marginBottom = cs2::CUILength::pixels(364)};
    // Feed sits under the radar (radar is the big square top-left); nudge if it overlaps on
    // your resolution.
    static constexpr auto kFeedBoxMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(10),
        .marginTop = cs2::CUILength::pixels(330)};

    static constexpr auto kRowMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(10),
        .marginTop = cs2::CUILength::pixels(3),
        .marginRight = cs2::CUILength::pixels(10),
        .marginBottom = cs2::CUILength::pixels(3)};
    static constexpr auto kFeedLineMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(10),
        .marginTop = cs2::CUILength::pixels(1),
        .marginRight = cs2::CUILength::pixels(10),
        .marginBottom = cs2::CUILength::pixels(1)};
}
