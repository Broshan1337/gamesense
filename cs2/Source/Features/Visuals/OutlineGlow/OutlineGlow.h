#pragma once

#include <utility>
#include <type_traits>
#include <Features/Visuals/Chams/ChamsConfigVariables.h>
#include <Features/Visuals/ModelGlow/ModelGlowConfigVariables.h>

#include <CS2/Classes/CPlantedC4.h>
#include <CS2/Classes/Entities/CBaseAnimGraph.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Entities/C_Hostage.h>
#include <CS2/Classes/Entities/WeaponEntities.h>
#include <GameClient/Entities/BaseModelEntity.h>
#include <GameClient/Entities/PlantedC4.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/Entities/EntityClassifier.h>

#include "DefuseKitOutlineGlow/DefuseKitOutlineGlow.h"
#include "DroppedBombOutlineGlow/DroppedBombOutlineGlow.h"
#include "GrenadeProjectileOutlineGlow/GrenadeProjectileOutlineGlow.h"
#include "HostageOutlineGlow/HostageOutlineGlow.h"
#include "PlayerOutlineGlow/PlayerOutlineGlow.h"
#include "TickingBombOutlineGlow/TickingBombOutlineGlow.h"
#include "WeaponOutlineGlow/WeaponOutlineGlow.h"

template <typename HookContext>
class OutlineGlow {
public:
    explicit OutlineGlow(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] auto applyGlow() const noexcept
    {
        return [this](auto&& glow, auto&& entity, EntityTypeInfo entityTypeInfo) {
            bool hiddenModelGlow = false;
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(glow)>, PlayerOutlineGlow<HookContext>>) {
                hiddenModelGlow = GET_CONFIG_VAR(chams_vars::HideEnemies)
                    && GET_CONFIG_VAR(model_glow_vars::Enabled) && GET_CONFIG_VAR(model_glow_vars::GlowPlayers)
                    && entity.isEnemy() == true && entity.isAlive() == true;
            }
            if ((!GET_CONFIG_VAR(outline_glow_vars::Enabled) || !glow.enabled()) && !hiddenModelGlow)
                return;

            if (!entityTypeInfo.isModelEntity() || entity.baseEntity().template as<BaseModelEntity>().glowProperty().isGlowing().valueOr(false))
                return;

            if (hiddenModelGlow) {
                const auto c = GET_CONFIG_VAR(model_glow_vars::EnemyColor);
                entity.baseEntity().applyGlowRecursively(cs2::Color{c.r(), c.g(), c.b(), c.a()}, getGlowRange(glow));
            } else if (shouldApplyGlow(glow, entityTypeInfo, entity))
                entity.baseEntity().applyGlowRecursively(getGlowColor(glow, entity, entityTypeInfo), getGlowRange(glow));
        };
    }

    void onUnload() const noexcept
    {
        hookContext.template make<GlowSceneObjects>().clearObjects();
    }

private:
    [[nodiscard]] static bool shouldApplyGlow(auto&& glow, [[maybe_unused]] EntityTypeInfo entityTypeInfo, auto&& entity)
    {
        if constexpr (requires { { glow.shouldApplyGlow(entityTypeInfo, entity) } -> std::same_as<bool>; })
            return glow.shouldApplyGlow(entityTypeInfo, entity);
        else
            return true;
    }

    template <typename Hue>
    [[nodiscard]] static cs2::Color colorFromHue(Optional<Hue> hue) noexcept
    {
        using namespace outline_glow_params;
        if (hue.hasValue())
            return color::HSBtoRGB(hue.value(), kSaturation, kBrightness);
        return kFallbackColor;
    }

    [[nodiscard]] static cs2::Color colorFromHue(auto hue) noexcept
    {
        using namespace outline_glow_params;
        return color::HSBtoRGB(hue, kSaturation, kBrightness);
    }

    [[nodiscard]] static auto getGlowHue(auto&& glow, auto&& entity, [[maybe_unused]] EntityTypeInfo entityTypeInfo)
    {
        if constexpr (requires { { glow.hue(entityTypeInfo, entity) }; })
            return glow.hue(entityTypeInfo, entity);
        else
            return glow.hue();
    }

    
    
    [[nodiscard]] static cs2::Color getGlowColor(auto&& glow, auto&& entity, EntityTypeInfo entityTypeInfo)
    {
        if constexpr (requires { { glow.color(entityTypeInfo, entity) }; })
            return glow.color(entityTypeInfo, entity);
        else
            return colorFromHue(getGlowHue(glow, entity, entityTypeInfo)).setAlpha(getGlowColorAlpha(glow, entity));
    }

    [[nodiscard]] static std::uint8_t getGlowColorAlpha(auto&& glow, auto&& entity) noexcept
    {
        if constexpr (requires { { glow.getGlowColorAlpha(entity) } -> std::same_as<std::uint8_t>; })
            return glow.getGlowColorAlpha(entity);
        else
            return outline_glow_params::kGlowAlpha;
    }

    [[nodiscard]] static int getGlowRange(auto&& glow) noexcept
    {
        if constexpr (requires { { glow.getGlowRange() } -> std::same_as<int>; })
            return glow.getGlowRange();
        else
            return 0;
    }

    HookContext& hookContext;
};
