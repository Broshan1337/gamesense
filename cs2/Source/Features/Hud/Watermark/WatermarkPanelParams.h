#pragma once

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Constants/ColorConstants.h>
#include <CS2/Classes/Color.h>
#include <CS2/Classes/Vector.h>
#include <CS2/Panorama/CUILength.h>
#include <CS2/Panorama/StyleEnums.h>

#include <GameClient/Panorama/PanelAlignmentParams.h>
#include <GameClient/Panorama/PanelFontParams.h>
#include <GameClient/Panorama/PanelMarginParams.h>






namespace watermark_panel_params
{
    
    
    static constexpr cs2::Color kTextColor{0xA3, 0xD4, 0x1F};

    
    static constexpr cs2::Color kValueColor{232, 233, 238};
    
    static constexpr cs2::Color kChipOffColor{110, 110, 110};

    static constexpr auto kAlignment = PanelAlignmentParams{
        .horizontalAlignment = cs2::k_EHorizontalAlignmentRight,
        .verticalAlignment = cs2::k_EVerticalAlignmentTop};

    
    
    static constexpr auto kChipGap = PanelMarginParams{
        .marginRight = cs2::CUILength::pixels(5)};
    static constexpr auto kBrandTextMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(10),
        .marginTop = cs2::CUILength::pixels(4),
        .marginRight = cs2::CUILength::pixels(10),
        .marginBottom = cs2::CUILength::pixels(4)};
    
    static constexpr auto kIconMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(8),
        .marginRight = cs2::CUILength::pixels(1)};
    static constexpr auto kSegmentTextMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(1),
        .marginTop = cs2::CUILength::pixels(4),
        .marginRight = cs2::CUILength::pixels(10),
        .marginBottom = cs2::CUILength::pixels(4)};
    static constexpr auto kFeatureChipTextMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(8),
        .marginTop = cs2::CUILength::pixels(4),
        .marginRight = cs2::CUILength::pixels(8),
        .marginBottom = cs2::CUILength::pixels(4)};

    static constexpr auto kBrandFont = PanelFontParams{
        .fontFamily = "Stratum2 Bold TF, 'Arial Unicode MS'",
        .fontSize = 13};
    static constexpr auto kValueFont = PanelFontParams{
        .fontFamily = "Stratum2 Bold TF, 'Arial Unicode MS'",
        .fontSize = 13};
    static constexpr auto kChipFont = PanelFontParams{
        .fontFamily = "Stratum2 Bold TF, 'Arial Unicode MS'",
        .fontSize = 12};

    
    
    static constexpr auto kFpsIconUrl = "s2r://panorama/images/icons/ui/graph.vsvg";
    static constexpr auto kSpeedIconUrl = "s2r://panorama/images/icons/ui/fast.vsvg";
    static constexpr auto kPingIconUrl = "s2r://panorama/images/icons/ui/globe.vsvg";
    static constexpr auto kTeamDamageIconUrl = "s2r://panorama/images/icons/ui/bullet.vsvg";
    static constexpr auto kClockIconUrl = "s2r://panorama/images/icons/ui/clock.vsvg";
    static constexpr int kIconTextureHeight = 13;

    
}
