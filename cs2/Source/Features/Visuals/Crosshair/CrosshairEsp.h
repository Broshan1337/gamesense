#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Vector.h>
#include <Features/Combat/Aimbot/AimbotConfigVariables.h>
#include <Features/Combat/Autowall/Autowall.h>
#include <Features/Combat/Autowall/Penetration.h>
#include <Features/Visuals/Crosshair/CrosshairConfigVariables.h>
#include <GameClient/Entities/BaseWeapon.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <Features/Combat/ShotGeometry.h>
#include <GameClient/Tracing/Tracing.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/OverlayLayer.h>
#include <Utils/VerifyConsole.h>
#include <Utils/ColorUtils.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>
#include <Utils/Optional.h>

// Custom crosshair (vesta crosshair.cpp structure) + penetration crosshair
// (velocity pen crosshair mindset): the game thread evaluates the aim ray
// once per render frame and publishes the crosshair's screen-space pieces +
// resolved color through the OverlayLayer publish/snapshot; the present
// thread draws. Penetration mode: trace eye->forward*range; wall in the way =
// Autowall::evaluate the shot through it and color green when the post-wall
// damage clears PenetrationMinDamage, red otherwise; clear line = green.
template <typename HookContext>
class CrosshairEsp {
public:
    explicit CrosshairEsp(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(crosshair_vars::Enabled)) {
            if (wasActive) {
                overlay_layer::publishCrosshair({});
                wasActive = false;
            }
            return;
        }

        auto&& pawn = hookContext.activeLocalPlayerPawn();
        if (!pawn || pawn.isAlive() != true) {
            overlay_layer::publishCrosshair({});
            wasActive = false;
            return;
        }
        wasActive = true;

        auto color = static_cast<std::uint32_t>(GET_CONFIG_VAR(crosshair_vars::Color));
        const auto eye = pawn.eyePosition();
        const auto view = pawnValue<cs2::Vector>(pawn.baseEntity(), "C_CSPlayerPawn", "m_angEyeAngles");

        char damageText[16]{};
        bool hasDamageText = false;
        if (GET_CONFIG_VAR(crosshair_vars::PenetrationEnabled) && eye.hasValue() && view.hasValue()) {
            if (const auto resolved = penetrationColor(pawn, eye.value(), view.value(), damageText); resolved.hasValue()) {
                color = resolved.value();
                hasDamageText = damageText[0] != '\0';
            }
        }

        // Temporary breadcrumb (~ console): the in-game "crosshair renders
        // nothing" report needs the gate state from a live round.
        VerifyConsole::write(3.0f, "cross", "pieces published, pen=%d dmgText=%d",
            GET_CONFIG_VAR(crosshair_vars::PenetrationEnabled) ? 1 : 0, hasDamageText ? 1 : 0);

        publish(eye, view, color, damageText, hasDamageText);
    }

    void onUnload() const noexcept
    {
        overlay_layer::publishCrosshair({});
        wasActive = false;
    }

private:
    [[nodiscard]] Optional<std::uint32_t> penetrationColor(auto&& pawn, const cs2::Vector& eye,
                                                            const cs2::Vector& view,
                                                            char (&damageText)[16]) const noexcept
    {
        auto&& weapon = pawn.getActiveWeapon();
        const auto base = weapon.baseDamage();
        const auto power = weapon.penetrationPower();
        const auto rangeModifier = weapon.rangeModifier();
        const auto maxRange = weapon.maxRange();
        if (!base.hasValue() || !power.hasValue() || !rangeModifier.hasValue() || !maxRange.hasValue())
            return {};

        const auto basis = shot_geometry::angleVectors(view.x, view.y);
        const cs2::Vector end{eye.x + basis.forward.x * maxRange.value(),
                              eye.y + basis.forward.y * maxRange.value(),
                              eye.z + basis.forward.z * maxRange.value()};

        const auto trace = Tracing::traceLine(eye, end, pawn.baseEntity(), Autowall::kBulletMask);
        if (!trace.valid)
            return {};

        const auto yes = static_cast<std::uint32_t>(GET_CONFIG_VAR(crosshair_vars::PenetrationColorYes));
        const auto no = static_cast<std::uint32_t>(GET_CONFIG_VAR(crosshair_vars::PenetrationColorNo));

        if (!trace.didHit)
            return yes;  // clear line of sight - a normal shot lands

        // Wall in the way: full bullet simulation through it. The text shows
        // the damage we'd do shooting through it (the request: "what the
        // highest damage would be").
        const penetration::Bullet bullet{base.value(), power.value(), rangeModifier.value(), maxRange.value()};
        const penetration::Limits limits{};
        penetration::TraceBudget budget{};
        const auto impact = Autowall::evaluate(eye, end, pawn.baseEntity(), trace.hitEntity,
                                               bullet, limits, budget, hookContext);
        if (!impact.hasValue()) {
            copyText(damageText, "0 dmg");
            return no;
        }
        const int damage = static_cast<int>(impact.value().damage + 0.5f);
        damageText[0] = static_cast<char>('0' + (damage / 100) % 10);
        int len = damage >= 100 ? 1 : 0;
        if (damage >= 10)
            damageText[len++] = static_cast<char>('0' + (damage / 10) % 10);
        damageText[len++] = static_cast<char>('0' + damage % 10);
        damageText[len++] = ' ';
        damageText[len++] = 'd';
        damageText[len++] = 'm';
        damageText[len++] = 'g';
        damageText[len] = '\0';
        return impact.value().damage >= static_cast<float>(GET_CONFIG_VAR(crosshair_vars::PenetrationMinDamage)) ? yes : no;
    }

    static void copyText(char (&destination)[16], const char* source) noexcept
    {
        int i = 0;
        for (; i < 15 && source[i]; ++i)
            destination[i] = source[i];
        destination[i] = '\0';
    }

    // The pieces are display-independent (gap/length/thickness in px), so the
    // producer computes them once against a virtual 1920-wide display; the
    // draw side scales by display.width / 1920.
    void publish(const Optional<cs2::Vector>& eye, const Optional<cs2::Vector>& view, std::uint32_t color,
                  const char (&damageText)[16], bool hasDamageText) const noexcept
    {
        constexpr float kReferenceWidth = 1920.0f;
        const float scale = kReferenceWidth / 1920.0f;
        const float thickness = static_cast<float>(GET_CONFIG_VAR(crosshair_vars::Thickness)) * scale;
        const float gap = static_cast<float>(GET_CONFIG_VAR(crosshair_vars::Gap)) * scale;
        const float length = static_cast<float>(GET_CONFIG_VAR(crosshair_vars::Length)) * scale;

        overlay_layer::Crosshair crosshair{};
        crosshair.rgba = color;
        crosshair.thicknessPx = thickness;
        crosshair.outlinePx = GET_CONFIG_VAR(crosshair_vars::Outline) ? thickness + 2.0f : 0.0f;
        crosshair.hasDamageText = hasDamageText;
        for (int i = 0; i < 16; ++i)
            crosshair.damageText[i] = damageText[i];

        const auto push = [&](float xPercent, float yPercent, float wPercent, float hPercent) {
            if (crosshair.pieceCount >= overlay_layer::kMaxCrosshairPieces)
                return;
            crosshair.pieces[crosshair.pieceCount++] = {xPercent, yPercent, wPercent, hPercent};
        };

        const float half = thickness * 0.5f;
        const auto toPercentX = [&](float px) { return 50.0f + px / kReferenceWidth * 100.0f; };
        const auto toPercentY = [&](float px) { return 50.0f + px / 1080.0f * 100.0f; };

        if (GET_CONFIG_VAR(crosshair_vars::Lines) && length > 0.0f) {
            push(toPercentX(-gap - length - half), toPercentY(-half), length / kReferenceWidth * 100.0f, thickness / 1080.0f * 100.0f);
            push(toPercentX(gap + half), toPercentY(-half), length / kReferenceWidth * 100.0f, thickness / 1080.0f * 100.0f);
            push(toPercentX(-half), toPercentY(gap + half), thickness / kReferenceWidth * 100.0f, length / 1080.0f * 100.0f);
            if (!GET_CONFIG_VAR(crosshair_vars::TStyle))
                push(toPercentX(-half), toPercentY(-gap - length - half), thickness / kReferenceWidth * 100.0f, length / 1080.0f * 100.0f);
        }
        if (GET_CONFIG_VAR(crosshair_vars::Dot))
            push(toPercentX(-half), toPercentY(-half), thickness / kReferenceWidth * 100.0f, thickness / 1080.0f * 100.0f);

        overlay_layer::publishCrosshair(crosshair);
    }

    [[nodiscard]] int pawnInt(cs2::C_BaseEntity* entity, const char* className, const char* fieldName) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset(className, fieldName);
        if (!offset.has_value() || *offset <= 0)
            return -1;
        int value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(entity) + *offset, sizeof(value));
        return value;
    }

    template <typename T>
    [[nodiscard]] Optional<T> pawnValue(cs2::C_BaseEntity* entity, const char* className, const char* fieldName) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset(className, fieldName);
        if (!offset.has_value() || *offset <= 0)
            return {};
        T value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(entity) + *offset, sizeof(value));
        return value;
    }

    HookContext& hookContext;
    inline static bool wasActive{false};
};