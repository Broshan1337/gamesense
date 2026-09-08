#pragma once

#include <CS2/Panorama/CUIPanel.h>
#include <Features/Game/MatchAutoAcceptConfigVariables.h>
#include <GameClient/Panorama/PanoramaUiEngine.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>

// Accepts a found match automatically.
//
// Implemented entirely through the game's OWN event system rather than by hooking anything.
// CUiComponent_Lobby::MatchReady (sub_1D34320) fires the Panorama event
// `PanoramaComponent_Lobby_ReadyUpForMatch` with (shouldShow, playersReadyCount, numTotal), and the
// accept button dispatches `MatchAssistedAccept`. Subscribing to the first and dispatching the
// second is the whole feature - no detour, no vtable patch, no polling of game state.
//
// That route was chosen over hooking MatchReady for a concrete reason: its entry in the third
// CUiComponent_Lobby vtable is a THUNK that calls the implementation directly, so a vtable swap
// would miss every dispatch through that interface, and the class vtable's address never appears in
// code (it is reached through a data table), so it cannot be resolved by a code pattern either.
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

        // Re-run periodically rather than only on change. The handler lives in the Panorama
        // context, which is torn down and rebuilt as the UI navigates, taking our subscription with
        // it - the script below is written to be idempotent precisely so this is safe.
        if (enabled == subscribed && ++framesSinceRefresh < kRefreshIntervalFrames)
            return;

        framesSinceRefresh = 0;
        subscribed = enabled;
        runScript(enabled ? kSubscribeScript : kUnsubscribeScript);
    }

    void onUnload() const noexcept
    {
        if (subscribed) {
            runScript(kUnsubscribeScript);
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

    // Guarded on a handle stashed in the Panorama context so that running it twice does not stack a
    // second subscription - which matters because it IS run repeatedly, to survive UI reloads.
    //
    // Only `shouldShow` is tested. The event also carries the ready counts, but their order in the
    // callback is not something this could verify from the disassembly alone, and gating on a
    // argument whose position is uncertain would be a silent no-op rather than a visible bug.
    // Accepting on show is correct regardless: the event only fires when there is something to
    // accept.
    static constexpr auto kSubscribeScript = R"(
(function() {
  if (typeof $.NeversnoozeMatchAutoAccept !== 'undefined' && $.NeversnoozeMatchAutoAccept !== null)
    return;
  $.NeversnoozeMatchAutoAccept = $.RegisterForUnhandledEvent('PanoramaComponent_Lobby_ReadyUpForMatch', function (shouldShow) {
    if (shouldShow)
      $.DispatchEvent('MatchAssistedAccept');
  });
})();
)";

    static constexpr auto kUnsubscribeScript = R"(
(function() {
  if (typeof $.NeversnoozeMatchAutoAccept === 'undefined' || $.NeversnoozeMatchAutoAccept === null)
    return;
  $.UnregisterForUnhandledEvent('PanoramaComponent_Lobby_ReadyUpForMatch', $.NeversnoozeMatchAutoAccept);
  $.NeversnoozeMatchAutoAccept = null;
})();
)";

    // About two seconds at 60fps - often enough to recover from a UI reload quickly, rare enough
    // that re-running a guarded script costs nothing.
    static constexpr int kRefreshIntervalFrames = 128;

    // Constant-initialised and trivially destructible, so no __cxa_guard under -nostdlib.
    inline static bool subscribed{false};
    inline static int framesSinceRefresh{0};

    HookContext& hookContext;
};
