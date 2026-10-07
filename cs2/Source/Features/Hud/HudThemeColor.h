#pragma once

#include <ctime>

#include <CS2/Classes/Color.h>
#include <Features/Hud/HudThemeColorConfigVariables.h>
#include <Features/Hud/ThemeAccent.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/Panorama/PanoramaUiPanel.h>
#include <HookContext/HookContextMacros.h>







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

    
    
    [[nodiscard]] static cs2::Color paletteColor(int index) noexcept
    {
        static constexpr cs2::Color palette[12] = {
            {150, 200, 250}, 
            {232, 232, 232}, 
            {255, 255, 255}, 
            {150, 200, 255}, 
            {36, 120, 255},  
            {200, 100, 255}, 
            {255, 41, 36},   
            {255, 113, 36},  
            {255, 247, 36},  
            {62, 255, 36},   
            {112, 255, 219}, 
            {255, 156, 205}, 
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