#pragma once

#include <cstdint>

#include <CS2/Classes/IGameEventManager2.h>
#include <CS2/Classes/Vector.h>
#include <Features/Visuals/KillEffects/KillEffectsConfigVariables.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <GameClient/PlayerSlotLookup.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <GameClient/WorldToScreen/WorldToClipSpaceConverter.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/OverlayLayer.h>
#include <Utils/ColorUtils.h>
#include <Utils/Trig.h>

// Kill effect (lightning strike): when the LOCAL player kills someone, the
// victim's death position is captured from player_death and pushed into a small
// ring with a seed. run() re-projects the (world-fixed) death spot every frame
// and publishes it with the fade baked in; renderGameOverlay draws a jagged
// screen-space polyline from the spot upward (seeded zigzag, white flash tint
// during the first moments).
template <typename HookContext>
class KillEffects {
public:
    explicit KillEffects(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event || !game_events::is(event, "player_death"))
            return;
        if (!game_events::localPlayerIsAttacker(hookContext, event))
            return;
        const auto victimSlot = game_events::entityForKey(event, "userid");
        if (!PlayerSlotLookup<HookContext>::isValidSlot(victimSlot))
            return;
        const auto curtime = hookContext.globalVars().curtime();
        if (!curtime.hasValue())
            return;

        auto&& victimPawn = hookContext.template make<PlayerSlotLookup<HookContext>>().pawnBySlot(victimSlot);
        if (!victimPawn)
            return;
        const auto origin = victimPawn.absOrigin();
        if (!origin.hasValue())
            return;

        auto& slot = bolts[writeIndex];
        slot.x = origin.value().x;
        slot.y = origin.value().y;
        slot.z = origin.value().z;
        slot.bornTime = curtime.value();
        slot.seed = static_cast<std::uint32_t>(curtime.value() * 1000.0f) * 1664525u + 1013904223u + ++seedCounter;
        slot.active = true;
        writeIndex = (writeIndex + 1) % kMaxBolts;
    }

    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(KillEffectsEnabled)) {
            if (wasActive) {
                overlay_layer::publishBolts(nullptr, 0);
                wasActive = false;
            }
            return;
        }

        // Session gate (map-transition rule).
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto curtime = hookContext.globalVars().curtime();
        if (!localPawn || !curtime.hasValue() || curtime.value() < schema_readiness::kMinMapTime) {
            overlay_layer::publishBolts(nullptr, 0);
            wasActive = false;
            return;
        }

        wasActive = true;
        auto&& converter = hookContext.template make<WorldToClipSpaceConverter>();
        const auto color = GET_CONFIG_VAR(KillEffectsColor);

        overlay_layer::Bolt published[overlay_layer::kMaxBolts];
        int count = 0;
        for (int i = 0; i < kMaxBolts; ++i) {
            auto& bolt = bolts[(writeIndex + i) % kMaxBolts]; // oldest first
            if (!bolt.active)
                continue;
            const float age = curtime.value() - bolt.bornTime;
            if (age < 0.0f || age >= kLifetime) {
                bolt.active = false;
                continue;
            }
            const auto baseClip = converter.toClipSpace(cs2::Vector{bolt.x, bolt.y, bolt.z});
            if (baseClip.w < 0.001f)
                continue;
            // Strike point: the victim's death spot.
            const float xPercent = (baseClip.x / baseClip.w + 1.0f) * 50.0f;
            const float yPercent = (1.0f - baseClip.y / baseClip.w) * 50.0f;
            // Sky end: the world point kBoltWorldHeight straight above the
            // kill, projected. Behind-camera or off-screen values are fine -
            // the draw side uses the base->sky DIRECTION (capped, clamped
            // never-sideways/below), not the raw point, so degenerate
            // projections cannot draw floor-crossing lines.
            float upXPercent = xPercent;
            float upYPercent = yPercent - 5.0f;  // sane fallback: above the spot
            const auto upClip = converter.toClipSpace(cs2::Vector{bolt.x, bolt.y, bolt.z + kBoltWorldHeight});
            if (upClip.w > 0.001f) {
                upXPercent = (upClip.x / upClip.w + 1.0f) * 50.0f;
                upYPercent = (1.0f - upClip.y / upClip.w) * 50.0f;
            }

            const float ageScale = age / kLifetime;
            const float fade = 1.0f - ageScale;
            // Flash tint: the first part of the life lerps the stroke toward white.
            const float flash = age < kFlashTime ? (1.0f - age / kFlashTime) : 0.0f;
            const auto flashR = static_cast<std::uint8_t>(static_cast<float>(color.r()) + (255.0f - static_cast<float>(color.r())) * flash + 0.5f);
            const auto flashG = static_cast<std::uint8_t>(static_cast<float>(color.g()) + (255.0f - static_cast<float>(color.g())) * flash + 0.5f);
            const auto flashB = static_cast<std::uint8_t>(static_cast<float>(color.b()) + (255.0f - static_cast<float>(color.b())) * flash + 0.5f);
            const auto faded = color::Rgba{
                flashR, flashG, flashB,
                static_cast<std::uint8_t>(static_cast<float>(color.a()) * fade + 0.5f),
            };
            published[count++] = overlay_layer::Bolt{xPercent, yPercent, upXPercent, upYPercent, ageScale, bolt.seed, static_cast<std::uint32_t>(faded)};
        }

        overlay_layer::publishBolts(published, count);
    }

private:
    static constexpr int kMaxBolts = overlay_layer::kMaxBolts;
    static constexpr float kLifetime = 1.4f;
    static constexpr float kFlashTime = 0.25f;
    // World height of the sky end above the kill - the strike's origin.
    static constexpr float kBoltWorldHeight = 2500.0f;

    struct TrackedBolt {
        float x{}, y{}, z{};
        float bornTime{};
        std::uint32_t seed{};
        bool active{false};
    };

    HookContext& hookContext;
    inline static TrackedBolt bolts[kMaxBolts]{};
    inline static int writeIndex{0};
    inline static std::uint32_t seedCounter{0};
    inline static bool wasActive{false};
};