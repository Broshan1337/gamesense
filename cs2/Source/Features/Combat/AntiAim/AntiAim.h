#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <Features/Combat/AntiAim/AntiAimConfigVariables.h>
#include <Features/Combat/VisibilityCheck.h>
#include <Features/Game/FvaEmulator.h>
#include <Features/Combat/MovementFix.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <GameClient/UserCmd.h>
#include <GameClient/WorldToScreen/WorldToClipSpaceConverter.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/GuiLog.h>
#include <UI/ImGui/OverlayLayer.h>
#include <Utils/ColorUtils.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

// Anti-aim (velocity features/combat/impl/misc.cpp antiaim). CS2 semantics: the
// server clamps CS:GO-style desync, so what works is real pitch/yaw on the
// server-side view reconstruction - pitch down/up, base yaw 180 off view,
// at-targets/autoyaw/manual-side/avoid-backstab, and the +33 model-roll
// compensation.
//
// ANGLE DELIVERY (2026-10-15, owner request "just do it in the feature
// itself"): the AA writes its angles DIRECTLY on the command's base
// viewangles - no FVA feature dependency. The input-history channel turned
// out to only affect SHOT reconstruction: the server's animated model kept
// the real angles ("the anti aim was not looking down at all"), so the FVA
// delivery was invisible AA. Base writes are what the server (and everyone
// spectating us) actually animates from. Tradeoff, accepted deliberately:
// first-person camera follows the AA pitch when pitch AA is on - that is the
// same view every CS:GO AA has; YAW-only AA does not touch the camera pitch.
// On ticks where the aimbot staged a shot, the SHOT's target wins (the AA
// stays quiet - that is hide-shots; with HideShots on, raw attack-held ticks
// also stay quiet).
template <typename HookContext>
class AntiAim {
public:
    explicit AntiAim(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd, bool aimbotShotStaged) const noexcept
    {
        aaActive = false;

        if (!GET_CONFIG_VAR(antiaim_vars::Enabled))
            return;

        const UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn || localPawn.isAlive() != true)
            return;
        if (const auto mapTime = hookContext.globalVars().curtime();
            !mapTime.hasValue() || mapTime.value() < schema_readiness::kMinMapTime)
            return;

        // velocity gates: using something, mid-throw, ladder/noclip.
        if (userCmd.isButtonDown(kInUse))
            return;
        if (grenadeThrowing(localPawn))
            return;
        const auto moveType = pawnInt(localPawn, "C_BaseEntity", "m_nActualMoveType");
        // MoveType_t (schema libserver dump): MOVETYPE_NOCLIP = 7, MOVETYPE_LADDER = 9.
        if (moveType == 7 || moveType == 9)
            return;

        const auto viewPitch = userCmd.viewPitch();
        const auto viewYaw = userCmd.viewYaw();
        const auto origin = localPawn.absOrigin();
        if (!viewPitch.hasValue() || !viewYaw.hasValue() || !origin.hasValue()
            || !std::isfinite(viewPitch.value()) || !std::isfinite(viewYaw.value())
            || !hitboxOriginFinite(origin.value()))
            return;

        int yawSide = 0;  // -1 manual left, +1 manual right
        const bool leftHeld = Bind::isDown(static_cast<int>(GET_CONFIG_VAR(antiaim_vars::ManualLeftKey)));
        const bool rightHeld = Bind::isDown(static_cast<int>(GET_CONFIG_VAR(antiaim_vars::ManualRightKey)));
        if (leftHeld)  // both held: left wins (velocity clears right)
            yawSide = -1;
        else if (rightHeld)
            yawSide = 1;

        stagedPitch = pitchFor(viewPitch.value());
        stagedYaw = yawFor(userCmd, localPawn, origin.value(), viewYaw.value(), yawSide);
        indicatorYaw = viewYaw.value() + 180.0f;
        if (yawSide == -1)
            indicatorYaw -= 90.0f;
        else if (yawSide == 1)
            indicatorYaw += 90.0f;

        // Base-angle write in the feature itself (2026-10-15, owner request):
        // the old FVA-channel arming is gone - the server's model animation
        // never consumed the history entries, so the AA was invisible.

        // Fire ticks: the aimbot's own FVA target (the silent shot redirect)
        // wins - arming ours would clobber it. That IS hide-shots behavior; with
        // HideShots on, raw attack-held ticks also stay quiet (velocity's
        // "hide onshot" default).
        const bool attackHeld = userCmd.isButtonDown(kInAttack);
        if (aimbotShotStaged || (GET_CONFIG_VAR(antiaim_vars::HideShots) && attackHeld))
            return;

        aaActive = true;
        movement_fix::setViewAngles(userCmd, stagedPitch, stagedYaw);
    }

    // Feet arc publish (producer side): run from the render-start chain where the
    // view matrix is fresh - draws the direction indicator when AA is active.
    void onRenderUpdate() const noexcept
    {
        if (!aaActive || !GET_CONFIG_VAR(antiaim_vars::Enabled) || !GET_CONFIG_VAR(antiaim_vars::DirectionIndicator)) {
            if (indicatorPublished) {
                overlay_layer::publishAaArc(nullptr, 0);
                indicatorPublished = false;
            }
            return;
        }
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto origin = localPawn ? localPawn.absOrigin() : Optional<cs2::Vector>{};
        if (!origin.hasValue()) {
            overlay_layer::publishAaArc(nullptr, 0);
            indicatorPublished = false;
            return;
        }

        auto&& converter = hookContext.template make<WorldToClipSpaceConverter>();
        overlay_layer::AaArcPoint points[overlay_layer::kMaxAaArcPoints];
        int count = 0;
        constexpr float kRadius = 28.0f;
        constexpr float kFeetOffset = -2.0f;
        constexpr int kSegments = 48;
        constexpr float kSweep = 60.0f;
        const float yawRad = indicatorYaw * trig::kDegreesToRadians;
        const float halfSweep = kSweep * 0.5f * trig::kDegreesToRadians;
        const float step = (halfSweep * 2.0f) / kSegments;
        const auto color = GET_CONFIG_VAR(antiaim_vars::DirectionIndicatorColor);
        for (int i = 0; i <= kSegments && count < overlay_layer::kMaxAaArcPoints; ++i) {
            const float angle = yawRad - halfSweep + step * static_cast<float>(i);
            const cs2::Vector world{origin.value().x + trig::cosine(angle) * kRadius,
                                    origin.value().y + trig::sine(angle) * kRadius,
                                    origin.value().z + kFeetOffset};
            const auto clip = converter.toClipSpace(world);
            if (!(clip.w > 0.001f)) {
                // Behind camera kills the whole arc (projection is not monotonic).
                overlay_layer::publishAaArc(nullptr, 0);
                indicatorPublished = false;
                return;
            }
            const float inverseW = 1.0f / clip.w;
            // Cubic edge fade (velocity's smoothstep taper).
            const float frac = static_cast<float>(i) / kSegments;
            const float edge = 1.0f - trig::absolute(frac - 0.5f) * 2.0f;
            const float fade = edge * edge * edge * (edge * (edge * 6.0f - 15.0f) + 10.0f);
            points[count++] = overlay_layer::AaArcPoint{
                (clip.x * inverseW + 1.0f) * 50.0f,
                (1.0f - clip.y * inverseW) * 50.0f,
                static_cast<std::uint32_t>(color::Rgba{color.r(), color.g(), color.b(),
                    static_cast<std::uint8_t>(static_cast<float>(color.a()) * (fade < 0.05f ? 0.05f : fade) + 0.5f)}),
            };
        }
        overlay_layer::publishAaArc(points, count);
        indicatorPublished = true;
    }

    void onUnload() const noexcept
    {
        aaActive = false;
        overlay_layer::publishAaArc(nullptr, 0);
        indicatorPublished = false;
    }

private:
    static constexpr std::uint64_t kInUse = 1ull << 5;
    static constexpr std::uint64_t kInAttack = 1ull << 0;

    [[nodiscard]] float pitchFor(float viewPitch) const noexcept
    {
        switch (static_cast<std::uint8_t>(GET_CONFIG_VAR(antiaim_vars::PitchMode))) {
        case 1: return 89.0f;   // down
        case 2: return -89.0f;  // up
        default: return viewPitch;
        }
    }

    // velocity's get_yaw: base 180 off view; avoid-backstab overrides toward the
    // nearest knife enemy; autoyaw/at-targets pick the threat; manual side offsets.
    [[nodiscard]] float yawFor(const UserCmd& userCmd, auto&& localPawn, const cs2::Vector& origin,
                               float viewYaw, int yawSide) const noexcept
    {
        auto&& entitySystem = hookContext.template make<EntitySystem>();
        auto&& localEye = eyeOf(hookContext.activeLocalPlayerPawn());
        float baseYaw = viewYaw - 180.0f;

        // Avoid backstab: aim the yaw at the nearest enemy holding a knife
        // within 350 units (velocity's backstab scan).
        if (GET_CONFIG_VAR(antiaim_vars::AvoidBackstab)) {
            float knifeDistSq = 1.0e30f;
            float knifeYaw = 0.0f;
            bool knifeFound = false;
            entitySystem.forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
                auto* const rawPawn = classifyPawn(entityIdentity);
                if (!rawPawn)
                    return;
                auto&& pawn = hookContext.template make<PlayerPawn>(rawPawn);
                const auto [alive, enemy] = pawnState(pawn);
                if (!alive || !enemy)
                    return;
                const auto pawnOrigin = pawn.absOrigin();
                if (!pawnOrigin.hasValue())
                    return;
                const float dx = pawnOrigin.value().x - origin.x;
                const float dy = pawnOrigin.value().y - origin.y;
                const float distSq = dx * dx + dy * dy;
                if (distSq > 350.0f * 350.0f)
                    return;
                const auto weaponType = pawn.getActiveWeapon().weaponType();
                if (!weaponType.hasValue() || weaponType.value() != 0)
                    return;  // 0 = knife
                if (distSq < knifeDistSq) {
                    knifeDistSq = distSq;
                    knifeYaw = trig::arcTangent2(dy, dx) * trig::kRadiansToDegrees;
                    knifeFound = true;
                }
            });
            if (knifeFound)
                return knifeYaw;
        }

        const auto mode = static_cast<std::uint8_t>(GET_CONFIG_VAR(antiaim_vars::AutoYaw));
        const bool atTargets = Bind::isDown(static_cast<int>(GET_CONFIG_VAR(antiaim_vars::AtTargetsKey)));
        if (mode != 0 || atTargets) {
            float bestScore = -1.0e30f;
            float bestDistanceSq = 1.0e30f;
            int bestHealth = 1 << 30;
            float bestYaw = baseYaw;
            float bestVisibleScore = -1.0e30f;
            float bestVisibleYaw = baseYaw;
            bool found = false;
            bool foundVisible = false;

            entitySystem.forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
                auto* const rawPawn = classifyPawn(entityIdentity);
                if (!rawPawn)
                    return;
                auto&& pawn = hookContext.template make<PlayerPawn>(rawPawn);
                const auto [alive, enemy] = pawnState(pawn);
                if (!alive || !enemy)
                    return;
                if (pawnImmune(pawn))
                    return;
                const auto pawnOrigin = pawn.absOrigin();
                if (!pawnOrigin.hasValue())
                    return;
                const float dx = pawnOrigin.value().x - origin.x;
                const float dy = pawnOrigin.value().y - origin.y;
                const float targetYaw = trig::arcTangent2(dy, dx) * trig::kRadiansToDegrees - 180.0f;

                if (!atTargets || mode == 0) {
                    // Pure autoyaw: score by mode, no visibility pass.
                    bool better = false;
                    if (mode == 1) {
                        const auto dot = crosshairDot(userCmd, localEye, pawn);
                        better = dot > bestScore;
                        if (better) bestScore = dot;
                    } else if (mode == 2) {
                        const float distSq = dx * dx + dy * dy;
                        better = distSq < bestDistanceSq;
                        if (better) bestDistanceSq = distSq;
                    } else if (mode == 3) {
                        const int health = pawnInt(pawn, "C_BaseEntity", "m_iHealth");
                        better = health > 0 && health < bestHealth;
                        if (better) bestHealth = health;
                    }
                    if (better) {
                        bestYaw = targetYaw;
                        found = true;
                    }
                    return;
                }

                // at-targets (hold bind): visible-preferred crosshair dot.
                const auto eye = eyeOf(pawn);
                bool visible = false;
                if (eye.hasValue() && localEye.hasValue()) {
                    const auto vis = VisibilityCheck::measure(localEye.value(), pawn.baseEntity(), eye.value(), nullptr);
                    visible = vis.state == VisibilityResult::State::Clear;
                }
                const auto dot = crosshairDot(userCmd, localEye, pawn);
                if (visible) {
                    if (dot > bestVisibleScore) {
                        bestVisibleScore = dot;
                        bestVisibleYaw = targetYaw;
                        foundVisible = true;
                    }
                } else if (dot > bestScore) {
                    bestScore = dot;
                    bestYaw = targetYaw;
                    found = true;
                }
            });

            if (mode != 0) {
                if (found)
                    baseYaw = bestYaw;
            } else if (foundVisible) {
                baseYaw = bestVisibleYaw;
            } else if (found) {
                baseYaw = bestYaw;
            }
        }

        float yaw = baseYaw;
        if (yawSide == -1)
            yaw -= 90.0f;
        else if (yawSide == 1)
            yaw += 90.0f;

        // Model sideways-roll compensation ("thx saphy").
        if (GET_CONFIG_VAR(antiaim_vars::AutoYawAdjust))
            yaw += 33.0f;
        return yaw;
    }

    [[nodiscard]] cs2::C_CSPlayerPawn* classifyPawn(const auto& entityIdentity) const noexcept
    {
        const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
        if (!entityTypeInfo.template is<cs2::C_CSPlayerPawn>())
            return nullptr;
        return static_cast<cs2::C_CSPlayerPawn*>(entityIdentity.entity);
    }

    // PlayerPawn does not implement isControlledByLocalPlayer for enemy walks;
    // the local pawn is filtered by team via isEnemy (same as OffScreenArrows).
    [[nodiscard]] std::pair<bool, bool> pawnState(auto&& pawn) const noexcept
    {
        const auto alive = pawn.isAlive();
        const auto enemy = pawn.isEnemy();
        return {(alive.has_value() && *alive), (enemy.has_value() && *enemy)};
    }

    [[nodiscard]] bool pawnImmune(auto&& pawn) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_CSPlayerPawn", "m_bGunGameImmunity");
        if (!offset.has_value() || *offset <= 0)
            return false;
        bool immune{};
        std::memcpy(&immune, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity())) + *offset, sizeof(immune));
        return immune;
    }

    [[nodiscard]] int pawnInt(auto&& pawn, const char* className, const char* fieldName) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset(className, fieldName);
        if (!offset.has_value() || *offset <= 0)
            return -1;
        int value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity())) + *offset, sizeof(value));
        return value;
    }

    [[nodiscard]] bool grenadeThrowing(auto&& localPawn) const noexcept
    {
        const auto weaponType = localPawn.getActiveWeapon().weaponType();
        if (!weaponType.hasValue() || weaponType.value() != 9)
            return false;  // 9 = grenade
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseCSGrenade", "m_fThrowTime");
        if (!offset.has_value() || *offset <= 0)
            return false;
        auto&& weapon = localPawn.getActiveWeapon();
        float throwTime{};
        std::memcpy(&throwTime, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(weapon.baseEntity())) + *offset, sizeof(throwTime));
        return throwTime > 0.0f;
    }

    [[nodiscard]] Optional<cs2::Vector> eyeOf(auto&& pawn) const noexcept
    {
        if (!pawn)
            return {};
        return pawn.eyePosition();
    }

    [[nodiscard]] float crosshairDot(const UserCmd& userCmd, const Optional<cs2::Vector>& localEye, auto&& pawn) const noexcept
    {
        const auto pitch = userCmd.viewPitch();
        const auto yaw = userCmd.viewYaw();
        const auto eye = eyeOf(pawn);
        if (!pitch.hasValue() || !yaw.hasValue() || !localEye.hasValue() || !eye.hasValue())
            return -2.0f;
        const cs2::Vector toTarget{eye.value().x - localEye.value().x,
                                   eye.value().y - localEye.value().y,
                                   eye.value().z - localEye.value().z};
        const float lengthSq = toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z;
        if (lengthSq < 1.0f)
            return -2.0f;
        const float pitchRad = pitch.value() * trig::kDegreesToRadians;
        const float yawRad = yaw.value() * trig::kDegreesToRadians;
        const float forward[3] = {trig::cosine(pitchRad) * trig::cosine(yawRad),
                                  trig::cosine(pitchRad) * trig::sine(yawRad),
                                  -trig::sine(pitchRad)};
        const float inverseLength = 1.0f / trig::squareRoot(lengthSq);
        return forward[0] * toTarget.x * inverseLength + forward[1] * toTarget.y * inverseLength + forward[2] * toTarget.z * inverseLength;
    }

    [[nodiscard]] static bool hitboxOriginFinite(const cs2::Vector& origin) noexcept
    {
        return std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z);
    }

    HookContext& hookContext;

    inline static bool aaActive{false};
    inline static bool indicatorPublished{false};
    inline static float stagedPitch{0.0f};
    inline static float stagedYaw{0.0f};
    inline static float indicatorYaw{0.0f};
};