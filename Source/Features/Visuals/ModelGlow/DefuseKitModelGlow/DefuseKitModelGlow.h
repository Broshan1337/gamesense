#pragma once

#include <Features/Visuals/ModelGlow/ModelGlowConfigVariables.h>
#include <Features/Visuals/ModelGlow/ModelGlowParams.h>
#include <Features/Visuals/ModelGlow/ModelGlowState.h>
#include <GameClient/Entities/EntityClassifier.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/ColorUtils.h>

template <typename HookContext>
class DefuseKitModelGlow {
public:
    explicit DefuseKitModelGlow(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] bool enabled() const
    {
        return GET_CONFIG_VAR(model_glow_vars::GlowDefuseKits);    
    }

    [[nodiscard]] auto deactivationFlag() const noexcept
    {
        return ModelGlowDeactivationFlags::DefuseKitModelGlowDeactivating;
    }

    [[nodiscard]] color::HueInteger hue() const
    {
        return GET_CONFIG_VAR(model_glow_vars::DefuseKitHue);
    }
    [[nodiscard]] cs2::Color color(EntityTypeInfo /* entityTypeInfo */, auto&& /* entity */) const noexcept
    {
        const auto picked = GET_CONFIG_VAR(model_glow_vars::DefuseKitColor);
        return cs2::Color{picked.r(), picked.g(), picked.b(), picked.a()};
    }

private:
    [[nodiscard]] auto& state() const
    {
        return hookContext.featuresStates().visualFeaturesStates.modelGlowState;
    }

    HookContext& hookContext;
};
