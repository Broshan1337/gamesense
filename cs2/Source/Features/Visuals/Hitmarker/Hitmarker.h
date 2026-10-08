#pragma once

#include <cstring>

#include <CS2/Classes/IGameEventManager2.h>
#include <Features/Visuals/Hitmarker/HitmarkerConfigVariables.h>
#include <Features/Visuals/Hitmarker/HitmarkerState.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/OverlayLayer.h>
#include <Utils/ColorUtils.h>

template <typename HookContext>
class Hitmarker {
public:
    explicit Hitmarker(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event)
            return;
        if (!game_events::is(event, "player_hurt"))
            return;
        if (!game_events::localPlayerIsAttacker(hookContext, event))
            return;

        
        if (game_events::localPlayerIsSlot(hookContext, game_events::entityForKey(event, "userid")))
            return;

        if (const auto curtime = hookContext.globalVars().curtime(); curtime.hasValue())
            hookContext.featuresStates().visualFeaturesStates.hitmarkerState.lastHurtTime = curtime.value();
    }

    
    
    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(HitmarkerEnabled)) {
            overlay_layer::publishHitmarker({});
            return;
        }

        const auto curtime = hookContext.globalVars().curtime();
        if (!curtime.hasValue()) {
            overlay_layer::publishHitmarker({});
            return;
        }

        const auto& state = hookContext.featuresStates().visualFeaturesStates.hitmarkerState;
        const float timeout = GET_CONFIG_VAR(HitmarkerTimeout); 
        const float elapsed = curtime.value() - state.lastHurtTime;
        if (state.lastHurtTime <= -1.0e8f || elapsed < 0.0f || elapsed >= timeout) {
            overlay_layer::publishHitmarker({});
            return;
        }

        const auto color = GET_CONFIG_VAR(HitmarkerColor);
        
        const float progress = elapsed / timeout;
        const auto fadedAlpha = static_cast<std::uint8_t>(static_cast<float>(color.a()) * (1.0f - progress) + 0.5f);
        overlay_layer::publishHitmarker(overlay_layer::Hitmarker{
            .gap = GET_CONFIG_VAR(HitmarkerGap),
            .length = GET_CONFIG_VAR(HitmarkerLength),
            .rgba = color::Rgba{color.r(), color.g(), color.b(), fadedAlpha},
        });
    }

private:
    HookContext& hookContext;
};
