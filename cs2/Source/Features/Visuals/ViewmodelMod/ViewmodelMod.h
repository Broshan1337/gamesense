#pragma once

#include <optional>

#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <HookContext/HookContextMacros.h>
#include "ViewmodelModConfigVariables.h"





template <typename HookContext>
class ViewmodelMod {
public:
    explicit ViewmodelMod(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    void run() const noexcept
    {
        const bool enabled = GET_CONFIG_VAR(viewmodel_mod_vars::ModifyPosition);

        if (!enabled) {
            onUnload();
            return;
        }

        if (!originalsValid) {
            bool allRead = true;
            for (int i = 0; i < 3; ++i) {
                if (const auto original = cvarSystem().readFloatConVar(kOffsetCvarNames[i]); original.has_value())
                    originals[i] = original.value();
                else
                    allRead = false;
            }
            
            
            
            originalsValid = allRead;
            if (!allRead)
                return;
        }

        static_cast<void>(cvarSystem().forceFloatConVar(kOffsetCvarNames[0], static_cast<float>(GET_CONFIG_VAR(viewmodel_mod_vars::OffsetX))));
        static_cast<void>(cvarSystem().forceFloatConVar(kOffsetCvarNames[1], static_cast<float>(GET_CONFIG_VAR(viewmodel_mod_vars::OffsetY))));
        static_cast<void>(cvarSystem().forceFloatConVar(kOffsetCvarNames[2], static_cast<float>(GET_CONFIG_VAR(viewmodel_mod_vars::OffsetZ))));
        positionWasEnabled = true;
    }

    void onUnload() const noexcept
    {
        if (positionWasEnabled && originalsValid) {
            for (int i = 0; i < 3; ++i)
                static_cast<void>(cvarSystem().forceFloatConVar(kOffsetCvarNames[i], originals[i]));
        }
        originalsValid = false;
        positionWasEnabled = false;
    }

    [[nodiscard]] bool shouldModifyViewmodelFov() const
    {
        auto&& localPlayerPawn = hookContext.localPlayerController().pawn().template cast<PlayerPawn>();
        return localPlayerPawn && !localPlayerPawn.isScoped().valueOr(false);
    }

    [[nodiscard]] float viewmodelFov() const
    {
        return GET_CONFIG_VAR(viewmodel_mod_vars::Fov);
    }

private:
    [[nodiscard]] CvarSystem<HookContext> cvarSystem() const noexcept
    {
        return hookContext.template make<CvarSystem>();
    }

    
    static constexpr const char* kOffsetCvarNames[3] = {"viewmodel_offset_x", "viewmodel_offset_y", "viewmodel_offset_z"};

    inline static bool positionWasEnabled{false};
    inline static bool originalsValid{false};
    inline static float originals[3]{};

    HookContext& hookContext;
};
