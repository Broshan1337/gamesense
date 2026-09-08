#pragma once

#include <Features/Visuals/OutlineGlow/OutlineGlowConfigVariables.h>
#include <Features/Visuals/OutlineGlow/OutlineGlowParams.h>
#include <GameClient/Entities/EntityClassifier.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/ColorUtils.h>

template <typename HookContext>
class TickingBombOutlineGlow {
public:
    TickingBombOutlineGlow(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] bool enabled() const
    {
        return GET_CONFIG_VAR(outline_glow_vars::GlowTickingBomb);
    }

    [[nodiscard]] bool shouldApplyGlow(EntityTypeInfo /* entityTypeInfo */, auto&& plantedBomb) const
    {
        return plantedBomb.isTicking().valueOr(true);
    }

    [[nodiscard]] color::HueInteger hue() const
    {
        return GET_CONFIG_VAR(outline_glow_vars::TickingBombHue);
    }
    [[nodiscard]] cs2::Color color(EntityTypeInfo /* entityTypeInfo */, auto&& /* entity */) const noexcept
    {
        const auto picked = GET_CONFIG_VAR(outline_glow_vars::TickingBombColor);
        return cs2::Color{picked.r(), picked.g(), picked.b(), picked.a()};
    }

private:
    HookContext& hookContext;
};
