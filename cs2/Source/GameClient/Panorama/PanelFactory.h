#pragma once

#include <CS2/Panorama/CImagePanel.h>
#include <CS2/Panorama/CLabel.h>
#include <CS2/Panorama/CUIPanel.h>
#include <GameClient/MemAlloc.h>
#include <MemoryPatterns/PatternTypes/PanelPatternTypes.h>
#include <MemoryPatterns/PatternTypes/PanoramaImagePanelPatternTypes.h>
#include <MemoryPatterns/PatternTypes/PanoramaLabelPatternTypes.h>
#include <GameClient/Panorama/ClientPanel.h>
#include <GameClient/Panorama/ImagePanel.h>
#include <Utils/CrashLogger.h>

template <typename HookContext>
struct PanelFactory {
    explicit PanelFactory(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] decltype(auto) createPanel(cs2::CUIPanel* parentPanel, const char* id = "") noexcept
    {
        
        
        
        CrashLogger::trace(0x378);
        if (!parentPanel) {
            
            
            
            
            CrashLogger::trace(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
            CrashLogger::trace(0x37D);
            return hookContext.template make<ClientPanel>(nullptr);
        }
        if (!panelConstructor()) {
            CrashLogger::trace(0x37E);
            return hookContext.template make<ClientPanel>(nullptr);
        }
        auto created = hookContext.template make<ClientPanel>(panelConstructor()(id, parentPanel->clientPanel));
        CrashLogger::trace(0x379);
        return created;
    }

    [[nodiscard]] decltype(auto) createLabelPanel(cs2::CUIPanel* parentPanel, const char* id = "") const noexcept
    {
        if (!parentPanel || !labelPanelConstructor() || !labelPanelSize())
            return hookContext.template make<ClientPanel>(nullptr);

        const auto memory{static_cast<cs2::CLabel*>(hookContext.template make<MemAlloc>().allocate(labelPanelSize()))};
        if (memory)
            labelPanelConstructor()(memory, parentPanel->clientPanel, id);
        return hookContext.template make<ClientPanel>(memory);
    }

    [[nodiscard]] decltype(auto) createImagePanel(cs2::CUIPanel* parentPanel, const char* id = "") noexcept
    {
        if (!parentPanel || !imagePanelConstructor() || !imagePanelSize())
            return hookContext.template make<ClientPanel>(nullptr).template as<ImagePanel>();

        const auto memory{static_cast<cs2::CImagePanel*>(hookContext.template make<MemAlloc>().allocate(imagePanelSize()))};
        if (memory)
            imagePanelConstructor()(memory, parentPanel->clientPanel, id);
        return hookContext.template make<ClientPanel>(memory).template as<ImagePanel>();
    }

private:
    [[nodiscard]] auto panelConstructor() const noexcept
    {
        return hookContext.patternSearchResults().template get<PanelConstructorPointer>();
    }

    [[nodiscard]] auto imagePanelConstructor() const noexcept
    {
        return hookContext.patternSearchResults().template get<ImagePanelConstructorPointer>();
    }

    [[nodiscard]] auto imagePanelSize() const noexcept
    {
        return hookContext.patternSearchResults().template get<ImagePanelClassSize>();
    }

    [[nodiscard]] auto labelPanelConstructor() const noexcept
    {
        return hookContext.patternSearchResults().template get<LabelPanelConstructorPointer>();
    }

    [[nodiscard]] auto labelPanelSize() const noexcept
    {
        return hookContext.patternSearchResults().template get<LabelPanelObjectSize>();
    }

    HookContext& hookContext;
};
