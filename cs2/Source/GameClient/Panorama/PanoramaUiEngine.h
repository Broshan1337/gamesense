#pragma once

#include <Platform/Macros/FunctionAttributes.h>
#include <Platform/Macros/IsPlatform.h>
#include <MemoryPatterns/PatternTypes/UiEnginePatternTypes.h>

template <typename HookContext>
class PanoramaUiEngine {
public:
    explicit PanoramaUiEngine(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void runScript(cs2::CUIPanel* contextPanel, const char* scriptSource) noexcept
    {
        
        
        constexpr auto originFile{'\0'};
        constexpr auto line{1};
        if (hookContext.patternSearchResults().template get<RunScriptFunctionPointer>() && thisptr())
            hookContext.patternSearchResults().template get<RunScriptFunctionPointer>()(*thisptr(), contextPanel, scriptSource, &originFile, line);
    }

    [[nodiscard]] decltype(auto) getPanelFromHandle(cs2::PanelHandle handle) noexcept
    {
        if (hookContext.patternSearchResults().template get<GetPanelPointerFunctionPointer>() && thisptr())
            return hookContext.uiPanel(hookContext.patternSearchResults().template get<GetPanelPointerFunctionPointer>()(*thisptr(), &handle));
        return hookContext.uiPanel(nullptr);
    }

    [[NOINLINE]] void deletePanelByHandle(cs2::PanelHandle handle) noexcept
    {
        if (handle.isValid())
            onDeletePanel(handle);
    }

    [[nodiscard]] cs2::CPanoramaSymbol makeSymbol(int type, const char* text) noexcept
    {
        if (hookContext.patternSearchResults().template get<MakeSymbolFunctionPointer>() && thisptr())
            return hookContext.patternSearchResults().template get<MakeSymbolFunctionPointer>()(WIN64_ONLY(*thisptr(),) type, text);
        return -1;
    }

    void registerEventHandler(cs2::CPanoramaSymbol symbol, cs2::CUIPanel* panel, cs2::CUtlAbstractDelegate handler) noexcept
    {
        if (hookContext.patternSearchResults().template get<RegisterEventHandlerFunctionPointer>() && thisptr())
            hookContext.patternSearchResults().template get<RegisterEventHandlerFunctionPointer>()(*thisptr(), symbol, panel, &handler);
    }

private:
    [[nodiscard]] auto thisptr() const noexcept
    {
        return hookContext.patternSearchResults().template get<UiEnginePointer>();
    }

    void onDeletePanel(cs2::PanelHandle panelHandle) noexcept
    {
        cs2::CPanel2D* clientPanel = getPanelFromHandle(panelHandle).clientPanel();
        if (clientPanel && hookContext.patternSearchResults().template get<OnDeletePanelFunctionPointer>() && thisptr())
            hookContext.patternSearchResults().template get<OnDeletePanelFunctionPointer>()(*thisptr(), clientPanel);
    }

    HookContext& hookContext;
};
