#pragma once

#include <CS2/Panorama/PanelHandle.h>

struct CombatStatsState {
    // Bottom-left hits/misses box
    cs2::PanelHandle counterBoxPanelHandle;
    cs2::PanelHandle hitsPanelHandle;
    cs2::PanelHandle missesPanelHandle;
    cs2::PanelHandle ratioPanelHandle;

    // Top-left event feed (kFeedLines stacked labels, newest first)
    cs2::PanelHandle feedBoxPanelHandle;
    cs2::PanelHandle feedLineHandles[5];
};
