#pragma once

// FrameworkCS2 port (Source/Features/Misc/Hitmarker): a fading four-line marker around the
// crosshair after the local player damages someone. The timestamp is game curtime (std::chrono
// is unavailable - the library links -nostdlib), the drawing happens on the present thread via
// the overlay layer, exactly like the off-screen arrows.
struct HitmarkerState {
    // Game time of the last player_hurt caused by the local player; -1e9 = nothing recorded.
    float lastHurtTime{-1.0e9f};
};
