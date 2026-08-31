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

// The watermark: a boxed label pinned to the TOP-RIGHT corner of the HUD root (full-screen panel -
// the old version hung off HudReticle, which is a mid-screen strip, hence it floated at the right
// edge of the middle). Green text on a translucent dark rounded box.
namespace watermark_panel_params
{
    // User-picked green (hex A3D41F), readable on both bright and dark scenes.
    static constexpr cs2::Color kTextColor{0xA3, 0xD4, 0x1F};
    static constexpr cs2::Color kBoxColor{16, 20, 12, 165};
    static constexpr auto kBoxBorderRadius = cs2::CUILength::pixels(4);

    static constexpr auto kFont = PanelFontParams{
        .fontFamily = "Stratum2 Bold TF, 'Arial Unicode MS'",
        .fontSize = 14};
    static constexpr auto kAlignment = PanelAlignmentParams{
        .horizontalAlignment = cs2::k_EHorizontalAlignmentRight,
        .verticalAlignment = cs2::k_EVerticalAlignmentTop};
    static constexpr auto kBoxMargin = PanelMarginParams{
        .marginTop = cs2::CUILength::pixels(10),
        .marginRight = cs2::CUILength::pixels(10)};
    // The inner labels' margins are the box's padding (Panorama boxes have no padding property).
    static constexpr auto kTextMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(10),
        .marginTop = cs2::CUILength::pixels(3),
        .marginRight = cs2::CUILength::pixels(4),
        .marginBottom = cs2::CUILength::pixels(3)};
    // Feature chips ("BT EXP COMP") sit right of the main text.
    static constexpr auto kChipMargin = PanelMarginParams{
        .marginLeft = cs2::CUILength::pixels(0),
        .marginTop = cs2::CUILength::pixels(3),
        .marginRight = cs2::CUILength::pixels(10),
        .marginBottom = cs2::CUILength::pixels(3)};
    static constexpr auto kChipFont = PanelFontParams{
        .fontFamily = "Stratum2 Bold TF, 'Arial Unicode MS'",
        .fontSize = 11};
    // A chip is bright when its feature is on, dim gray when off.
    static constexpr cs2::Color kChipOffColor{110, 110, 110};
}
