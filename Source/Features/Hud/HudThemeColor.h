#pragma once

#include <ctime>

#include <CS2/Classes/Color.h>
#include <Features/Hud/HudThemeColorConfigVariables.h>
#include <Features/Hud/ThemeAccent.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/Panorama/PanoramaUiPanel.h>
#include <HookContext/HookContextMacros.h>

// Tints the in-game HUD elements (health/ammo/money wash - the panels carrying the
// "hud-colorize-wash" class that the game's own cl_hud_color setting drives through CSS) with
// the live menu accent color. Walks the HUD panel tree a couple of times a second and re-applies
// the inline wash-color, which overrides the game's CSS class per panel. Toggle off = restore
// the palette color of the current cl_hud_color setting (the CSS palette was extracted from the
// game's compiled stylesheets).
template <typename HookContext>
class HudThemeColor {
public:
    explicit HudThemeColor(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() noexcept
    {
        const double now = monotonicSeconds();
        if (now - lastRun < 0.5)
            return;
        lastRun = now;

        const bool enabled = GET_CONFIG_VAR(hud_theme_vars::Enabled);
        const cs2::Color color = enabled ? accentColor() : paletteColor(readHudColorIndex());

        auto&& root = hookContext.hud().rootPanel();
        if (!root)
            return;
        walkAndTint(root, color, 0);
    }

private:
    [[nodiscard]] static double monotonicSeconds() noexcept
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
    }

    [[nodiscard]] static cs2::Color accentColor() noexcept
    {
        const auto rgb = theme_accent::rgb();
        return cs2::Color{static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8), static_cast<std::uint8_t>(rgb)};
    }

    [[nodiscard]] int readHudColorIndex() const noexcept
    {
        return hookContext.cvarSystem().readIntConVar("cl_hud_color").value_or(0);
    }

    // The game's own cl_hud_color palette (css @define color-hud-0..11; index 0 = default =
    // the CT light-blue, whose css var is ct-color rgb(150, 200, 250)).
    [[nodiscard]] static cs2::Color paletteColor(int index) noexcept
    {
        static constexpr cs2::Color palette[12] = {
            {150, 200, 250}, // 0 - default (ct-color)
            {232, 232, 232}, // 1
            {255, 255, 255}, // 2
            {150, 200, 255}, // 3
            {36, 120, 255},  // 4
            {200, 100, 255}, // 5
            {255, 41, 36},   // 6
            {255, 113, 36},  // 7
            {255, 247, 36},  // 8
            {62, 255, 36},   // 9
            {112, 255, 219}, // 10
            {255, 156, 205}, // 11
        };
        if (index < 0 || index > 11)
            index = 0;
        return palette[index];
    }

    void walkAndTint(auto&& panel, const cs2::Color& color, int depth) noexcept
    {
        if (visited >= kMaxVisited || depth > kMaxDepth)
            return;
        ++visited;

        if (panel.hasClass(washClassSymbol()))
            panel.setWashColor(color);

        for (auto&& child : panel.children())
            walkAndTint(child, color, depth + 1);
    }

    [[nodiscard]] cs2::CPanoramaSymbol washClassSymbol() noexcept
    {
        // class symbols are stable per process; one lookup then reuse
        if (cachedWashSymbol == 0)
            cachedWashSymbol = hookContext.template make<PanoramaUiEngine>().makeSymbol(0, "hud-colorize-wash");
        return cachedWashSymbol;
    }

    static constexpr int kMaxDepth = 14;
    static constexpr int kMaxVisited = 600;

    inline static double lastRun = -10.0;
    inline static std::uint64_t cachedWashSymbol = 0;
    inline static int visited = 0;

    HookContext& hookContext;
};