#pragma once

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <Features/Visuals/OffScreenArrows/OffScreenArrowsConfigVariables.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <GameClient/WorldToScreen/WorldToClipSpaceConverter.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/OverlayLayer.h>

// Off-screen enemy arrows (velocity-style): game-thread producer walks the
// entity list once per frame, projects every alive enemy's head into clip
// space, and publishes only the OFF-SCREEN ones (NDC outside [-1,1] or behind
// the camera). renderGameOverlay turns each into a filled triangle sitting on
// a circle around screen center, pointing toward the enemy.
template <typename HookContext>
class OffScreenArrows {
public:
    explicit OffScreenArrows(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(OffScreenArrowsEnabled)) {
            if (wasActive) {
                overlay_layer::publish(nullptr, 0);
                wasActive = false;
            }
            return;
        }

        // Session gate (map-transition rule): the entity list is gutted mid-map-load,
        // never walk it until a live session exists.
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto curtime = hookContext.globalVars().curtime();
        if (!localPawn || !curtime.hasValue() || curtime.value() < schema_readiness::kMinMapTime) {
            overlay_layer::publish(nullptr, 0);
            wasActive = false;
            return;
        }

        wasActive = true;
        overlay_layer::Arrow arrows[overlay_layer::kMaxArrows];
        int count = 0;

        auto&& converter = hookContext.template make<WorldToClipSpaceConverter>();
        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            if (count >= overlay_layer::kMaxArrows)
                return;
            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            if (!entityTypeInfo.template is<cs2::C_CSPlayerPawn>())
                return;

            auto&& pawn = hookContext.template make<PlayerPawn>(static_cast<cs2::C_CSPlayerPawn*>(entityIdentity.entity));
            if (pawn.isControlledByLocalPlayer())
                return;
            const auto alive = pawn.isAlive();
            if (!alive.has_value() || !*alive)
                return;
            const auto enemy = pawn.isEnemy();
            if (!enemy.has_value() || !*enemy)
                return;

            const auto eye = pawn.eyePosition();
            if (!eye.hasValue())
                return;

            const auto clip = converter.toClipSpace(eye.value());
            // w ~= 0: direction is undefined, skip rather than feed inf into the draw side.
            if (!(clip.w > 0.001f) && !(clip.w < -0.001f))
                return;
            const bool behind = !(clip.w > 0.001f);
            // Behind the camera: 1/w mirrors the point through screen center,
            // publish raw percents and flag it so the draw side negates the direction.
            const auto inverseW = 1.0f / clip.w;
            const auto xPercent = (clip.x * inverseW + 1.0f) * 50.0f;
            const auto yPercent = (1.0f - clip.y * inverseW) * 50.0f;
            if (!behind && xPercent >= 0.0f && xPercent <= 100.0f && yPercent >= 0.0f && yPercent <= 100.0f)
                return;
            arrows[count++] = overlay_layer::Arrow{xPercent, yPercent, behind};
        });

        overlay_layer::publish(arrows, count);
    }

private:
    HookContext& hookContext;
    inline static bool wasActive{false};
};