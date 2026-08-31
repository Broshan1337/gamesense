#pragma once

#include <CS2/Panorama/PanelHandle.h>

struct StatusPanelState {
    cs2::PanelHandle boxPanelHandle;
    cs2::PanelHandle chipPanelHandles[3];   // AIM / TRIG / BLOCK - see StatusPanel.h
};
