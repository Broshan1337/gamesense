#pragma once

#include <CS2/Classes/ViewSetup.h>
#include <Features/Visuals/Removals/RemovalsConfigVariables.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/Panorama/PanoramaUiEngine.h>
#include <HookContext/HookContextMacros.h>
#include <Hooks/SceneRenderHooks.h>

// FrameworkCS2 port (Source/Features/Visuals/Removals), adapted to this project's mechanisms:
//   - RemoveViewPunch: OverrideView pass - writing the RAW input angles over the view setup
//     implicitly removes view punch, aim punch and screen shake (reference behavior).
//   - RemoveLegs: the first-person legs renderer is skipped with a single 0xC3 hot patch
//     (scene_render_hooks::setLegsRenderRemoved).
//   - RemoveFlashOverlay: the whole PostHud render pipeline layer is skipped by patching the
//     RenderingPipelineCsgoPostHud vtable slot to a stub (reference does the same early return;
//     today that pipeline only carries the flash effect).
//   - RemoveMenuAds: the game's own Panorama menu is still Panorama even though OUR menu is
//     ImGui - the reference hides the "JsLeftColumn" panel with SetHasClass, re-issued on a
//     cadence to survive UI reloads, and restored on disable/unload.
//   - RemoveScope: NOT ported - the reference modifies DrawHudOverlay's argument struct, which
//     needs a trampoline detour this project has no safe mechanism for yet.
template <typename HookContext>
class Removals {
public:
    explicit Removals(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // OverrideView hook pass, runs AFTER the original filled the view setup.
    //
    // RemoveViewPunch: the original's view angles = aim angles + camera punch, and the local
    // pawn's aim punch (the same value the RCS uses) is the punch the camera adds - subtracting
    // it restores the punch-free view. (The FrameworkCS2 reference overwrites with the CSGOInput
    // +0x7C0 angles, but that offset does not track the live view on this build.)
    void overrideView(ViewSetup* viewSetup) const noexcept
    {
        if (!viewSetup || !GET_CONFIG_VAR(RemoveViewPunch))
            return;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn)
            return;

        const auto punch = localPawn.aimPunchAngle();
        if (!punch.hasValue())
            return;
        if (!looksLikeAngles(ViewSetup::viewAngles(viewSetup)) || !looksLikeAngles(punch.value()))
            return;

        auto& viewAngles = ViewSetup::viewAngles(viewSetup);
        viewAngles.x -= punch.value().x;
        viewAngles.y -= punch.value().y;
    }

    // Per-frame tick from ViewRenderHook_onRenderStart: patch bookkeeping + the ads panel script.
    void run() const noexcept
    {
        scene_render_hooks::setLegsRenderRemoved(GET_CONFIG_VAR(RemoveLegs));
        scene_render_hooks::setFlashOverlayRemoved(GET_CONFIG_VAR(RemoveFlashOverlay));
        updateMenuAds();
    }

    void onUnload() const noexcept
    {
        scene_render_hooks::setLegsRenderRemoved(false);
        scene_render_hooks::setFlashOverlayRemoved(false);
        if (adsHidden)
            (void)hideMenuAds(false);
    }

private:
    void updateMenuAds() const noexcept
    {
        const bool wantHidden = GET_CONFIG_VAR(RemoveMenuAds);
        // Re-issue periodically (the reference re-runs on a 1s interval) so a Panorama UI reload
        // cannot resurrect the panel while the feature is on.
        if (wantHidden == adsHidden && !wantHidden)
            return;
        ++frameCounter;
        if (wantHidden == adsHidden && frameCounter % kReissueIntervalFrames != 0)
            return;

        if (hideMenuAds(wantHidden))
            adsHidden = wantHidden;
    }

    [[nodiscard]] bool hideMenuAds(bool hidden) const noexcept
    {
        const auto mainMenu = hookContext.patternSearchResults().template get<MainMenuPanelPointer>();
        if (!mainMenu || !*mainMenu)
            return false;

        const char* const script = hidden
            ? "(function(){ var p = $.GetContextPanel().FindChildTraverse('JsLeftColumn'); if (p) p.SetHasClass('hidden', true); })();"
            : "(function(){ var p = $.GetContextPanel().FindChildTraverse('JsLeftColumn'); if (p) p.SetHasClass('hidden', false); })();";
        hookContext.template make<PanoramaUiEngine>().runScript((*mainMenu)->uiPanel, script);
        return true;
    }

    [[nodiscard]] static bool looksLikeAngles(const cs2::Vector& angles) noexcept
    {
        constexpr float kMaxPitch = 90.1f;
        constexpr float kMaxYaw = 180.1f;
        return angles.x >= -kMaxPitch && angles.x <= kMaxPitch
            && angles.y >= -kMaxYaw && angles.y <= kMaxYaw
            && angles.z >= -kMaxPitch && angles.z <= kMaxPitch;
    }

    static constexpr int kReissueIntervalFrames = 300; // ~5s at 60fps

    // Toggle liveness across per-frame invocations (the feature object is value-constructed
    // every frame, so this lives on the hook thread's static storage like SpectatorList's).
    inline static bool adsHidden{false};
    inline static int frameCounter{0};

    HookContext& hookContext;
};
