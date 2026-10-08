#pragma once

#include <CS2/Panorama/CUIPanel.h>
#include <Features/Game/MatchAutoAcceptConfigVariables.h>
#include <GameClient/Panorama/PanoramaUiEngine.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <Utils/NsStr.h>













template <typename HookContext>
class MatchAutoAccept {
public:
    explicit MatchAutoAccept(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        const auto enabled = GET_CONFIG_VAR(MatchAutoAcceptEnabled);

        
        
        
        if (enabled == subscribed && ++framesSinceRefresh < kRefreshIntervalFrames)
            return;

        framesSinceRefresh = 0;
        subscribed = enabled;
        if (enabled) {
            NS_DEC(subScript, kSubscribeScriptEnc);
            runScript(subScript);
        } else {
            NS_DEC(unsubScript, kUnsubscribeScriptEnc);
            runScript(unsubScript);
        }
    }

    void onUnload() const noexcept
    {
        if (subscribed) {
            NS_DEC(unsubScript, kUnsubscribeScriptEnc);
            runScript(unsubScript);
            subscribed = false;
        }
    }

private:
    void runScript(const char* script) const noexcept
    {
        const auto mainMenu = hookContext.patternSearchResults().template get<MainMenuPanelPointer>();
        if (!mainMenu || !*mainMenu)
            return;

        hookContext.template make<PanoramaUiEngine>().runScript((*mainMenu)->uiPanel, script);
    }

    
    
    
    
    
    
    
    
    
    
    static constexpr ns_str::Encrypted<sizeof(
        "\n(function() {\n"
        "  if (typeof $.readyUpAutoAcceptHook !== 'undefined' && $.readyUpAutoAcceptHook !== null)\n"
        "    return;\n"
        "  $.readyUpAutoAcceptHook = $.RegisterForUnhandledEvent('PanoramaComponent_Lobby_ReadyUpForMatch', function (shouldShow) {\n"
        "    if (shouldShow)\n"
        "      $.DispatchEvent('MatchAssistedAccept');\n"
        "  });\n"
        "})();\n")> kSubscribeScriptEnc{
        "\n(function() {\n"
        "  if (typeof $.readyUpAutoAcceptHook !== 'undefined' && $.readyUpAutoAcceptHook !== null)\n"
        "    return;\n"
        "  $.readyUpAutoAcceptHook = $.RegisterForUnhandledEvent('PanoramaComponent_Lobby_ReadyUpForMatch', function (shouldShow) {\n"
        "    if (shouldShow)\n"
        "      $.DispatchEvent('MatchAssistedAccept');\n"
        "  });\n"
        "})();\n"};

    static constexpr ns_str::Encrypted<sizeof(
        "\n(function() {\n"
        "  if (typeof $.readyUpAutoAcceptHook === 'undefined' || $.readyUpAutoAcceptHook === null)\n"
        "    return;\n"
        "  $.UnregisterForUnhandledEvent('PanoramaComponent_Lobby_ReadyUpForMatch', $.readyUpAutoAcceptHook);\n"
        "  $.readyUpAutoAcceptHook = null;\n"
        "})();\n")> kUnsubscribeScriptEnc{
        "\n(function() {\n"
        "  if (typeof $.readyUpAutoAcceptHook === 'undefined' || $.readyUpAutoAcceptHook === null)\n"
        "    return;\n"
        "  $.UnregisterForUnhandledEvent('PanoramaComponent_Lobby_ReadyUpForMatch', $.readyUpAutoAcceptHook);\n"
        "  $.readyUpAutoAcceptHook = null;\n"
        "})();\n"};

    
    
    static constexpr int kRefreshIntervalFrames = 128;

    
    inline static bool subscribed{false};
    inline static int framesSinceRefresh{0};

    HookContext& hookContext;
};
