#pragma once

#include <cstddef>
#include <utility>

#include <CS2/Panorama/CUIPanel.h>

#include "PanoramaUiPanelIterator.h"






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
