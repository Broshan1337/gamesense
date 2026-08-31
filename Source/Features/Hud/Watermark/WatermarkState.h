#pragma once

#include <CS2/Panorama/PanelHandle.h>
#include <cstdint>

struct WatermarkState {
    cs2::PanelHandle boxPanelHandle;
    cs2::PanelHandle textPanelHandle;
    cs2::PanelHandle chipPanelHandles[3];   // BT / EXP / COMP - see Watermark.h
    std::uint32_t lastAccentColor{0};       // 0xRRGGBBAA; used to skip restyling when unchanged
    int lastMarginX{-1};                     // last box offset applied to the panel (Hud px)
    int lastMarginY{-1};
};
