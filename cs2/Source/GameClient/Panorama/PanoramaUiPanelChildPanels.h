#pragma once

#include <cstddef>
#include <utility>

#include <CS2/Panorama/CUIPanel.h>

#include "PanoramaUiPanelIterator.h"

// 2026-09-26 5GB update: the CUIPanel children storage split into two separate fields -
// the COUNT at +0x2C8 (uint32) and the ARRAY POINTER at +0x2D0 (CUIPanel*[]), verified
// live: the root 'CSGOHud' panel's count = its child count and the array = the children
// CUIPanels ('Hud', 'HudTeamCounter', ...). The old single-embedded-CUtlVector shape is
// gone. This struct now carries the two fields separately.
template <typename HookContext>
struct PanoramaUiPanelChildPanels {
    PanoramaUiPanelChildPanels(HookContext& hookContext, cs2::CUIPanel** memory, std::uint32_t count) noexcept
        : hookContext{hookContext}
        , memory{memory}
        , count{count}
    {
    }

    [[nodiscard]] decltype(auto) begin() noexcept
    {
        if (memory)
            return hookContext.template make<PanoramaUiPanelIterator>(memory);
        return hookContext.template make<PanoramaUiPanelIterator>(nullptr);
    }

    [[nodiscard]] decltype(auto) end() noexcept
    {
        if (memory)
            return hookContext.template make<PanoramaUiPanelIterator>(memory + count);
        return hookContext.template make<PanoramaUiPanelIterator>(nullptr);
    }

    [[nodiscard]] decltype(auto) operator[](std::size_t index) noexcept
    {
        if (memory && index < count)
            return hookContext.uiPanel(memory[index]);
        return hookContext.uiPanel(nullptr);
    }

    template <typename F>
    void forEach(F&& f) noexcept
    {
        if (!memory)
            return;

        for (std::uint32_t i = 0; i < count; ++i)
            f(hookContext.uiPanel(memory[i]));
    }

    HookContext& hookContext;
    cs2::CUIPanel** memory;
    std::uint32_t count;
};
