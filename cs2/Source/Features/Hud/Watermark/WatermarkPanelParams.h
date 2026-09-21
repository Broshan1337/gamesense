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

// The watermark: a row of rounded "pill" chips pinned to the TOP-RIGHT corner of the HUD root
// (full-screen parent - see Hud::rootPanel()). Each datum (fps / speed / ping / team damage /
// clock) lives in its own translucent dark chip with a leading tinted icon; the brand chip leads
// the row in the menu accent color. Chip chrome (accent glow ring + fill + hairline border) is
// shared with the other HUD chips - see ChipPanel.h.
namespace watermark_panel_params
{
    // User-picked green (hex A3D41F), readable on both bright and dark scenes. Kept as the
    // compile-time source of the default menu accent (Neverlose.cpp accentFromWatermark()).
    static constexpr cs2::Color kTextColor{0xA3, 0xD4, 0x1F};

    // Value text is near-white; the accent is spent on the brand text + icons + chip glow only.
    static constexpr cs2::Color kValueColor{232, 233, 238};
    // A feature chip is bright (accent) when its feature is on, dim gray when off.
    static constexpr cs2::Color kChipOffColor{110, 110, 110};

    static constexpr auto kAlignment = PanelAlignmentParams{
        .horizontalAlignment = cs2::k_EHorizontalAlignmentRight,
        .verticalAlignment = cs2::k_EVerticalAlignmentTop};

    // Gap BETWEEN chips (every chip's outer margin). Panorama boxes have no padding property -
    // the inner spacing is the child panels' margins.
    static constexpr auto kChipGap = PanelMarginParams{
        .marginRight = cs2::CUILength::pixels(5)};
    static constexpr auto kBrandTextMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(10),
        .marginTop = cs2::CUILength::pixels(4),
        .marginRight = cs2::CUILength::pixels(10),
        .marginBottom = cs2::CUILength::pixels(4)};
    // Icon hugs the chip's left edge, vertically centered; the value label follows it.
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

    // Leading icons, all tinted with the menu accent. Paths verified against the shipped
    // csgo pak01 VPK (panorama/images/icons/ui/...).
    static constexpr auto kFpsIconUrl = "s2r://panorama/images/icons/ui/graph.vsvg";
    static constexpr auto kSpeedIconUrl = "s2r://panorama/images/icons/ui/fast.vsvg";
    static constexpr auto kPingIconUrl = "s2r://panorama/images/icons/ui/globe.vsvg";
    static constexpr auto kTeamDamageIconUrl = "s2r://panorama/images/icons/ui/bullet.vsvg";
    static constexpr auto kClockIconUrl = "s2r://panorama/images/icons/ui/clock.vsvg";
    static constexpr int kIconTextureHeight = 13;

    // Brand text is encrypted (NsStr.h) and decrypted at its use site in Watermark.h.
}
