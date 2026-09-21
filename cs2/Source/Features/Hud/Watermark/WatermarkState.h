#pragma once

#include <CS2/Panorama/PanelHandle.h>
#include <cstdint>

// Panel handles for one segment chip of the watermark (box + tinted icon + value label).
struct WatermarkSegmentPanels {
    cs2::PanelHandle boxHandle;
    cs2::PanelHandle iconHandle;
    cs2::PanelHandle labelHandle;
};

struct WatermarkState {
    cs2::PanelHandle boxPanelHandle;        // transparent flow container of the chip row
    cs2::PanelHandle brandBoxHandle;        // "Neversnooze" chip (accent text)
    cs2::PanelHandle brandLabelHandle;
    WatermarkSegmentPanels segments[5];     // fps / speed / ping / team damage / clock
    cs2::PanelHandle chipBoxHandles[3];     // BT / EXP / COMP feature chips - see Watermark.h
    cs2::PanelHandle chipPanelHandles[3];
    std::uint32_t lastAccentColor{0};       // 0xRRGGBBAA; used to skip restyling when unchanged
    int lastMarginX{-1};                     // last box offset applied to the panel (Hud px)
    int lastMarginY{-1};
};
