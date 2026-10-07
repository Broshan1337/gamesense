#pragma once

#include <CS2/Classes/ViewSetup.h>
#include <Features/Visuals/Removals/RemovalsConfigVariables.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/Panorama/PanoramaUiEngine.h>
#include <HookContext/HookContextMacros.h>
#include <Hooks/SceneRenderHooks.h>














template <typename HookContext>
class Removals {
public:
    explicit Removals(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    
    
    
    
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

    static constexpr int kReissueIntervalFrames = 300; 

    
    
    inline static bool adsHidden{false};
    inline static int frameCounter{0};

    HookContext& hookContext;
};
