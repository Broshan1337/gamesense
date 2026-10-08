#pragma once

#include <CS2/Panorama/PanelHandle.h>
#include <cstdint>


struct WatermarkSegmentPanels {
    cs2::PanelHandle boxHandle;
    cs2::PanelHandle iconHandle;
    cs2::PanelHandle labelHandle;
};

struct WatermarkState {
    cs2::PanelHandle boxPanelHandle;        
    cs2::PanelHandle brandBoxHandle;        
    cs2::PanelHandle brandLabelHandle;
    WatermarkSegmentPanels segments[5];     
    cs2::PanelHandle chipBoxHandles[3];     
    cs2::PanelHandle chipPanelHandles[3];
    std::uint32_t lastAccentColor{0};       
    int lastMarginX{-1};                     
    int lastMarginY{-1};
};
