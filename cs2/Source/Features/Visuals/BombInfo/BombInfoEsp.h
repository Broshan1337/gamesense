#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include <CS2/Constants/BombsiteIndex.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Vector.h>
#include <Features/Visuals/BombInfo/BombInfoConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlantedC4.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <GameClient/WorldToScreen/WorldToClipSpaceConverter.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/OverlayLayer.h>
#include <Utils/ColorUtils.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

// Bomb info panel + safe zone (batch C, vesta bomb.cpp structure): the game
// thread publishes the planted C4's world position + countdown; the safe zone
// is a world-space ring contour around the bomb with the radius where our
// FULL-HP estimate of the blast damage drops below our health - the lazy
// correct blast model (vesta's game::blast_damage needs a live RE session we
// deliberately skip): damage = 500 * exp(-distance/500) - the community's
// standard C4 falloff fit; invert for our health -> the survive radius.
// Draw side: panel top-right (BindsList chrome), ring projected per frame.
template <typename HookContext>
class BombInfoEsp {
public:
    explicit BombInfoEsp(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(bomb_info_vars::Enabled)) {
            if (wasActive) {
                overlay_layer::publishBombInfo({});
                wasActive = false;
            }
            return;
        }

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto curtime = hookContext.globalVars().curtime();
        if (!localPawn || !curtime.hasValue() || curtime.value() < schema_readiness::kMinMapTime) {
            overlay_layer::publishBombInfo({});
            wasActive = false;
            return;
        }

        overlay_layer::BombInfo info{};
        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            if (info.planted)
                return;
            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            if (!entityTypeInfo.template is<cs2::CPlantedC4>())
                return;
            auto&& planted = hookContext.template make<PlantedC4>(static_cast<cs2::CPlantedC4*>(entityIdentity.entity));
            const auto origin = planted.baseEntity().absOrigin();
            if (!origin.hasValue() || !std::isfinite(origin.value().x))
                return;
            const auto ticking = planted.isTicking();
            info.planted = true;
            info.ticking = ticking.valueOr(false);
            info.beingDefused = planted.isBeingDefused();
            info.timeLeft = planted.getTimeToExplosion().valueOr(-1.0f);
            info.canDefuse = planted.canBeDefused().valueOr(false);
            info.defuseTimeLeft = planted.getTimeToDefuseEnd().valueOr(-1.0f);
            const auto site = planted.bombsiteIndex();
            info.site = site.hasValue() ? static_cast<int>(site.value()) : -1;

            const auto localOrigin = localPawn.absOrigin();
            const auto health = localPawn.health();
            if (!localOrigin.hasValue() || !health.hasValue())
                return;
            info.localHealth = health.value();
            // The survive radius: 500*exp(-r/500) < health -> r > 500*ln(500/health).
            const float h = static_cast<float>(health.value() > 0 ? health.value() : 1);
            info.surviveRadius = 500.0f * std::log(500.0f / h) + 48.0f;  // + hull pad
            info.x = origin.value().x;
            info.y = origin.value().y;
            info.z = origin.value().z;
        });

        wasActive = true;
        overlay_layer::publishBombInfo(info);
    }

    void onUnload() const noexcept
    {
        overlay_layer::publishBombInfo({});
        wasActive = false;
    }

private:
    HookContext& hookContext;
    inline static bool wasActive{false};
};