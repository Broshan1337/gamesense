#pragma once

#include <Features/Visuals/OutlineGlow/OutlineGlowConfigVariables.h>
#include <Features/Visuals/OutlineGlow/OutlineGlowParams.h>
#include <GameClient/Entities/EntityClassifier.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/ColorUtils.h>

template <typename HookContext>
class HostageOutlineGlow {
public:
    explicit HostageOutlineGlow(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] bool enabled() const
    {
        return GET_CONFIG_VAR(outline_glow_vars::GlowHostages);
    }

    [[nodiscard]] color::HueInteger hue() const
    {
        return GET_CONFIG_VAR(outline_glow_vars::HostageHue);
    }
    [[nodiscard]] cs2::Color color(EntityTypeInfo , auto&& ) const noexcept
    {
        const auto picked = GET_CONFIG_VAR(outline_glow_vars::HostageColor);
        return cs2::Color{picked.r(), picked.g(), picked.b(), picked.a()};
    }

private:
    HookContext& hookContext;
};
