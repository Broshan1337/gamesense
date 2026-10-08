#pragma once

#include <utility>

#include <CS2/Classes/Color.h>
#include <Features/Visuals/OutlineGlow/OutlineGlowConfigVariables.h>
#include <GameClient/Entities/EntityClassifier.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/ColorUtils.h>

template <typename HookContext>
class PlayerOutlineGlow {
public:
    explicit PlayerOutlineGlow(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] bool enabled() const
    {
        return GET_CONFIG_VAR(outline_glow_vars::GlowPlayers);
    }

    [[nodiscard]] bool shouldApplyGlow(EntityTypeInfo , auto&& playerPawn) const noexcept
    {
        return playerPawn.isAlive().value_or(true)
            && playerPawn.health().greaterThan(0).valueOr(true)
            && !playerPawn.isControlledByLocalPlayer()
            && playerPawn.isTTorCT()
            && (!GET_CONFIG_VAR(outline_glow_vars::GlowOnlyEnemies) || playerPawn.isEnemy().value_or(true));
    }

    [[nodiscard]] cs2::Color color(EntityTypeInfo , auto&& playerPawn) const noexcept
    {
        if (const auto isEnemy = playerPawn.isEnemy(); isEnemy.has_value()) {
            const auto configuredColor = *isEnemy ? GET_CONFIG_VAR(outline_glow_vars::EnemyColor) : GET_CONFIG_VAR(outline_glow_vars::AllyColor);
            
            
            const auto alpha = static_cast<std::uint8_t>(playerPawn.hasImmunity().valueOr(false)
                ? outline_glow_params::kImmunePlayerGlowAlpha
                : configuredColor.a());
            return cs2::Color{configuredColor.r(), configuredColor.g(), configuredColor.b(), alpha};
        }
        return fallbackColor(playerPawn);
    }

    [[nodiscard]] std::uint8_t getGlowColorAlpha(auto&& playerPawn) const noexcept
    {
        using namespace outline_glow_params;
        return playerPawn.hasImmunity().valueOr(false) ? kImmunePlayerGlowAlpha : kGlowAlpha;
    }

private:
    [[nodiscard]] static cs2::Color fallbackColor(auto&& playerPawn) noexcept
    {
        using namespace outline_glow_params;
        const auto alpha = playerPawn.hasImmunity().valueOr(false) ? kImmunePlayerGlowAlpha : kGlowAlpha;
        return kFallbackColor.setAlpha(alpha);
    }

    HookContext& hookContext;
};
