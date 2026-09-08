#pragma once

#include <CS2/Classes/Color.h>
#include <CS2/Classes/Entities/GrenadeProjectiles.h>
#include <GameClient/Entities/EntityClassifier.h>
#include <Features/Visuals/ModelGlow/ModelGlowConfigVariables.h>
#include <Features/Visuals/ModelGlow/ModelGlowParams.h>
#include <Features/Visuals/ModelGlow/ModelGlowState.h>
#include <HookContext/HookContextMacros.h>

template <typename HookContext>
class GrenadeProjectileModelGlow {
public:
    explicit GrenadeProjectileModelGlow(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] bool enabled() const
    {
        return GET_CONFIG_VAR(model_glow_vars::GlowGrenadeProjectiles);
    }

    [[nodiscard]] auto deactivationFlag() const noexcept
    {
        return ModelGlowDeactivationFlags::GrenadeProjectileModelGlowDeactivating;
    }

    [[nodiscard]] Optional<color::HueInteger> hue(EntityTypeInfo entityTypeInfo) const
    {
        switch (entityTypeInfo.typeIndex) {
        case EntityTypeInfo::indexOf<cs2::C_FlashbangProjectile>(): return GET_CONFIG_VAR(model_glow_vars::FlashbangHue);
        case EntityTypeInfo::indexOf<cs2::C_HEGrenadeProjectile>(): return GET_CONFIG_VAR(model_glow_vars::HEGrenadeHue);
        case EntityTypeInfo::indexOf<cs2::C_SmokeGrenadeProjectile>(): return GET_CONFIG_VAR(model_glow_vars::SmokeGrenadeHue);
        case EntityTypeInfo::indexOf<cs2::C_MolotovProjectile>(): return GET_CONFIG_VAR(model_glow_vars::MolotovHue);
        default: return {};
        }
    }

    [[nodiscard]] cs2::Color color(EntityTypeInfo entityTypeInfo, auto&& /* entity */) const noexcept
    {
        switch (entityTypeInfo.typeIndex) {
        case EntityTypeInfo::indexOf<cs2::C_FlashbangProjectile>(): { const auto c = GET_CONFIG_VAR(model_glow_vars::FlashbangColor); return cs2::Color{c.r(), c.g(), c.b(), c.a()}; }
        case EntityTypeInfo::indexOf<cs2::C_HEGrenadeProjectile>(): { const auto c = GET_CONFIG_VAR(model_glow_vars::HEGrenadeColor); return cs2::Color{c.r(), c.g(), c.b(), c.a()}; }
        case EntityTypeInfo::indexOf<cs2::C_MolotovProjectile>(): { const auto c = GET_CONFIG_VAR(model_glow_vars::MolotovColor); return cs2::Color{c.r(), c.g(), c.b(), c.a()}; }
        case EntityTypeInfo::indexOf<cs2::C_SmokeGrenadeProjectile>(): { const auto c = GET_CONFIG_VAR(model_glow_vars::SmokeGrenadeColor); return cs2::Color{c.r(), c.g(), c.b(), c.a()}; }
        }
        return cs2::Color{150, 150, 150, 255};
    }

private:
    [[nodiscard]] auto& state() const
    {
        return hookContext.featuresStates().visualFeaturesStates.modelGlowState;
    }

    HookContext& hookContext;
};
