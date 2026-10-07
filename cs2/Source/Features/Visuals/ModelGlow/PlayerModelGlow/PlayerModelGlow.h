#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include <CS2/Classes/Color.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <Features/Visuals/ModelGlow/ModelGlowConfigVariables.h>
#include <Features/Visuals/ModelGlow/ModelGlowState.h>
#include <HookContext/HookContextMacros.h>

std::uint64_t PlayerPawn_sceneObjectUpdater(cs2::C_CSPlayerPawn* playerPawn, void* unknown, bool unknownBool) noexcept;

template <typename HookContext>
class PlayerModelGlow {
public:
    explicit PlayerModelGlow(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] bool enabled() const
    {
        return GET_CONFIG_VAR(model_glow_vars::GlowPlayers);
    }

    [[nodiscard]] bool shouldApplyGlow(auto&& playerPawn) const
    {
        return playerPawn.isAlive().value_or(true)
            && playerPawn.health().greaterThan(0).valueOr(true)
            && !playerPawn.isControlledByLocalPlayer()
            && playerPawn.isTTorCT()
            && (!GET_CONFIG_VAR(model_glow_vars::GlowOnlyEnemies) || playerPawn.isEnemy().value_or(true));
    }

    [[nodiscard]] auto deactivationFlag() const noexcept
    {
        return ModelGlowDeactivationFlags::PlayerModelGlowDeactivating;
    }

    [[nodiscard]] auto& originalSceneObjectUpdater() const
    {
        return state().originalPlayerPawnSceneObjectUpdater;
    }

    [[nodiscard]] auto replacementSceneObjectUpdater() const
    {
        return &PlayerPawn_sceneObjectUpdater;
    }

    [[nodiscard]] cs2::Color color(auto&& playerPawn) const
    {
        if (const auto isEnemy = playerPawn.isEnemy(); isEnemy.has_value()) {
            
            
            const auto configuredColor = *isEnemy ? GET_CONFIG_VAR(model_glow_vars::EnemyColor) : GET_CONFIG_VAR(model_glow_vars::AllyColor);
            return cs2::Color{configuredColor.r(), configuredColor.g(), configuredColor.b(), configuredColor.a()};
        }
        return fallbackColor();
    }

private:
    [[nodiscard]] auto& state() const noexcept
    {
        return hookContext.featuresStates().visualFeaturesStates.modelGlowState;
    }

    [[nodiscard]] static cs2::Color fallbackColor() noexcept
    {
        return model_glow_params::kFallbackColor;
    }

    HookContext& hookContext;
};
