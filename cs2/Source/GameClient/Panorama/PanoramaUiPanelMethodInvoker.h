#pragma once

#include <utility>

#include <CS2/Panorama/CUIPanel.h>
#include <Utils/RetAddrSpoofer.h>

template <typename OffsetType>
struct PanoramaUiPanelMethodInvoker {
    PanoramaUiPanelMethodInvoker(cs2::CUIPanel* panel, OffsetType offsetToFunction)
        : panel{panel}
        , function{panel ? offsetToFunction.of(panel->vmt).get() : nullptr}
    {
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return panel != nullptr && function != nullptr;
    }

    template <typename... Args>
    decltype(auto) operator()(Args&&... args) const noexcept
    {
        // VMT-slot dispatch: `function` points into the panel's vtable, so dereference it to get
        // the game function, then spoof its return address.
        return RetAddrSpoofer::spoof(*function)(panel, std::forward<Args>(args)...);
    }

    cs2::CUIPanel* panel;
    typename OffsetType::FieldType* function;
};
