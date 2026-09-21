#pragma once

#include <atomic>

// The Neverlose-style menu (design: FORFUTURETESTS/neverlose-last, DESIGN.md + the reference
// implementation in its example_win32_directx11). Hand-drawn ImGui draw-list UI: translucent
// shell, left navigation rail with FontAwesome icons, setting cards with animated
// toggle/select/slider rows and a global select-popover layer. All rows bind 1:1 onto the
// ConfigSchema variables through ui_config (same semantics the stock-ImGui tabs had).
//
// Freestanding notes: no std::string/std::vector anywhere - const char* option tables and
// stack buffers only; animation easing uses expf/powf from the explicitly linked libm.
namespace neverlose
{

// Loads the menu fonts (Inter-Medium body, Inter-SemiBold strong, FontAwesome icons) into the
// ImGui atlas. Call once after ImGui::CreateContext(); the atlas builds at the first NewFrame.
void loadFonts() noexcept;

// The menu's UI scale (user-adjustable 0.75-2.0 in the profile popover; 1.0 = design size).
// Lua HUD scripts multiply their metrics by this to match the native panels' text size.
// Inline atomic + inline accessor: header-only, so TUs that link Lua.cpp without the menu
// (the UnitTests) still resolve - Neverlose.cpp publishes via applyMetrics.
inline std::atomic<float> g_menuUiScale{1.0f};

[[nodiscard]] inline float uiScale() noexcept
{
    return g_menuUiScale.load(std::memory_order_relaxed);
}

[[nodiscard]] bool fontsLoaded() noexcept;

// Present-thread menu rendering (called from GUI::render while the menu is visible).
void render() noexcept;

// Present-thread GAME-anchored overlay rendering: runs EVERY frame regardless of menu
// visibility (the ImGui frame itself always runs - only the menu shell is alpha-gated).
// Draws the overlay layer (off-screen arrows, hitmarker) on the foreground draw list and the
// player list window. Called from GUI::render between NewFrame and Render.
void renderGameOverlay() noexcept;

// Outer menu glow band around the shell, drawn on the FOREGROUND draw list (the shell window
// clips its own draw list to the shell rect, which would hide the band entirely). `menuAlpha`
// is the shell's open/close fade - the glow fades with it. Call from GUI::render.
void drawMenuGlow(float menuAlpha) noexcept;

// Deferred actions that must run OUTSIDE an ImGui frame (font reloads on menu-scale changes).
// Call once per frame from GUI::render, right before ImGui_ImplVulkan_NewFrame().
void processDeferred() noexcept;

// Keybind capture may be active when the menu closes - callers use this to abort it.
void cancelKeybindCapture() noexcept;

// Arms the cinematic open animation (call on the menu-open rising edge).
void beginReveal() noexcept;

// Arms the cinematic close animation - the mirror of beginReveal: the shell scales back down,
// slides and fades (call on the menu-close falling edge). Reverses from the current reveal
// progress, so closing mid-open-animation is smooth.
void beginDismiss() noexcept;

// True while the close animation is still running: the caller keeps the render path alive
// past the alpha gate so the transform can land.
[[nodiscard]] bool isDismissing() noexcept;

}
