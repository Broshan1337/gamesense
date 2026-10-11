#pragma once

#include <cmath>
#include <utility>
#include <cstdint>
#include <cstring>
#include <ctime>

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Vector.h>
#include <Features/Visuals/Esp/EspConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerController.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/GameSceneNode.h>
#include <GameClient/Hitboxes.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <GameClient/WorldToScreen/WorldToClipSpaceConverter.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/OverlayLayer.h>
#include <Utils/ColorUtils.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

// Core ESP suite (FrameworkCS2 ESP.cpp/Player.cpp/Skeleton.hpp structure, our
// idioms). Game-thread producer runs from the render-start chain (fresh view
// matrix), walks players once and publishes draw primitives (percents-of-display)
// through the OverlayLayer publish/snapshot; the present thread draws them in
// renderGameOverlay. Box = the OBB-corner bounding rect (FrameworkCS2: project
// the 8 collision-hull corners, pixel-round). Skeleton = the bone-pair list.
// Bars = health/armor with the hzqst-style backup-health damage echo.

// Pure helpers (no game state) - free functions so tests can exercise the
// projection and the damage-echo state machine without a full hook context.
namespace esp_detail
{

[[nodiscard]] inline bool finite(const cs2::Vector& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Percent-of-display projection; ok=false when behind the camera / degenerate w.
// kMaxNdc gates near-camera-plane blowups: a bone with w barely > 0 projects to
// |ndc| in the hundreds -> skeleton lines shooting across the monitor (the
// 2026-10-12 in-game report). Points past +/-160% of the half-screen are junk.
inline constexpr float kMaxNdc = 1.6f;

template <typename Converter>
[[nodiscard]] inline bool project(Converter&& converter, const cs2::Vector& world,
                                  float& xPercent, float& yPercent) noexcept
{
    if (!finite(world))
        return false;
    const auto clip = converter.toClipSpace(world);
    if (!(clip.w > 0.001f))
        return false;
    const float inverseW = 1.0f / clip.w;
    const float ndcX = clip.x * inverseW;
    const float ndcY = clip.y * inverseW;
    if (ndcX < -kMaxNdc || ndcX > kMaxNdc || ndcY < -kMaxNdc || ndcY > kMaxNdc)
        return false;
    xPercent = (ndcX + 1.0f) * 50.0f;
    yPercent = (1.0f - ndcY) * 50.0f;
    return std::isfinite(xPercent) && std::isfinite(yPercent);
}

// The hzqst-style backup-health damage echo: returns (backupFill, backupRgba)
// for this frame given the ring entry's state; rgba 0 = none. White bar fades
// over 500ms with a smoothstep ease.
inline std::pair<float, std::uint32_t> advanceBackupHealth(int& lastHealth, int& backupHealth,
                                                           std::int64_t& lastChangeMs, int healthValue,
                                                           std::int64_t nowMs, bool animOn) noexcept
{
    constexpr std::int64_t kBackupFadeMs = 500;
    if (lastHealth == 100 && backupHealth == 100 && lastChangeMs == 0) {
        // First sighting: adopt the current health WITHOUT treating it as a
        // drop (also stamps lastChangeMs so this branch never re-fires - a
        // fresh slot otherwise swallows the first real damage drop).
        lastHealth = healthValue;
        lastChangeMs = nowMs;
        return {0.0f, 0u};
    }
    if (healthValue < lastHealth) {
        backupHealth = lastHealth;
        lastChangeMs = nowMs;
    }
    lastHealth = healthValue;
    if (!animOn || backupHealth <= healthValue)
        return {0.0f, 0u};
    const std::int64_t age = nowMs - lastChangeMs;
    if (age >= 0 && age < kBackupFadeMs) {
        const float alpha = 1.0f - static_cast<float>(age) / static_cast<float>(kBackupFadeMs);
        const float eased = alpha * alpha * (3.0f - 2.0f * alpha);
        return {static_cast<float>(backupHealth) / 100.0f,
                static_cast<std::uint32_t>(color::Rgba{255, 255, 255,
                    static_cast<std::uint8_t>(200.0f * eased + 0.5f)})};
    }
    backupHealth = healthValue;
    return {0.0f, 0u};
}

}

template <typename HookContext>
class Esp {
public:
    explicit Esp(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(esp_vars::Enabled)) {
            if (wasActive) {
                overlay_layer::publishEsp(overlay_layer::EspScene{});
                wasActive = false;
            }
            return;
        }

        // Session gate (map-transition rule): never walk entities mid-load.
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto curtime = hookContext.globalVars().curtime();
        if (!localPawn || !curtime.hasValue() || curtime.value() < schema_readiness::kMinMapTime) {
            overlay_layer::publishEsp(overlay_layer::EspScene{});
            wasActive = false;
            return;
        }

        const auto localEye = localPawn.eyePosition();
        if (!localEye.hasValue()) {
            overlay_layer::publishEsp(overlay_layer::EspScene{});
            wasActive = false;
            return;
        }

        wasActive = true;
        const auto nowMs = monotonicMs();
        overlay_layer::EspScene scene;

        auto&& converter = hookContext.template make<WorldToClipSpaceConverter>();
        const bool teammates = GET_CONFIG_VAR(esp_vars::TeammatesEnabled);
        const float maxDistance = static_cast<float>(GET_CONFIG_VAR(esp_vars::MaxDistance));

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
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
            if (!enemy.has_value())
                return;
            if (*enemy) {
                // enemy
            } else if (!teammates) {
                return;
            }

            const auto origin = pawn.absOrigin();
            if (!origin.hasValue() || !finite(origin.value()))
                return;
            const float dx = origin.value().x - localEye.value().x;
            const float dy = origin.value().y - localEye.value().y;
            const float dz = origin.value().z - localEye.value().z;
            const float distance = trig::squareRoot(dx * dx + dy * dy + dz * dz);
            if (maxDistance > 0.0f && distance > maxDistance)
                return;

            drawPlayer(scene, converter, pawn, origin.value(), distance, nowMs);
        });

        overlay_layer::publishEsp(scene);
    }

    void onUnload() const noexcept
    {
        overlay_layer::publishEsp(overlay_layer::EspScene{});
        wasActive = false;
        for (auto& entry : backupHealth)
            entry = BackupHealth{};
    }

private:
    struct BackupHealth {
        std::uint32_t handle{};
        int lastHealth{100};
        int backupHealth{100};
        std::int64_t lastChangeMs{};
    };
    static constexpr int kBackupHealthSlots = 64;
    inline static BackupHealth backupHealth[kBackupHealthSlots]{};

    [[nodiscard]] static std::int64_t monotonicMs() noexcept
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1'000'000;
    }

    [[nodiscard]] static bool finite(const cs2::Vector& v) noexcept
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    // Percent-of-display projection; ok=false when behind the camera / degenerate w.
    [[nodiscard]] static bool project(auto&& converter, const cs2::Vector& world,
                                      float& xPercent, float& yPercent) noexcept
    {
        return esp_detail::project(converter, world, xPercent, yPercent);
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

    // Collision-hull OBB corners -> bounding rect (FrameworkCS2's box).
    [[nodiscard]] bool boundingRect(auto&& converter, auto&& pawn, const cs2::Vector& origin,
                                     float& x, float& y, float& w, float& h) const noexcept
    {
        auto&& node = hookContext.template make<BaseEntity>(pawn.baseEntity()).gameSceneNode();
        if (!node)
            return false;
        cs2::Vector mins{-16.0f, -16.0f, 0.0f};
        cs2::Vector maxs{16.0f, 16.0f, 72.0f};
        readCollisionBounds(pawn.baseEntity(), mins, maxs);

        float minX = 1.0e30f, minY = 1.0e30f, maxX = -1.0e30f, maxY = -1.0e30f;
        for (int corner = 0; corner < 8; ++corner) {
            const cs2::Vector world{origin.x + (corner & 1 ? maxs.x : mins.x),
                                    origin.y + (corner & 2 ? maxs.y : mins.y),
                                    origin.z + (corner & 4 ? maxs.z : mins.z)};
            float px, py;
            if (!project(converter, world, px, py))
                return false;
            minX = std::min(minX, px);
            minY = std::min(minY, py);
            maxX = std::max(maxX, px);
            maxY = std::max(maxY, py);
        }
        if (maxX <= minX || maxY <= minY)
            return false;
        x = minX;
        y = minY;
        w = maxX - minX;
        h = maxY - minY;
        return true;
    }

    void readCollisionBounds(cs2::C_BaseEntity* entity, cs2::Vector& mins, cs2::Vector& maxs) const noexcept
    {
        auto&& schema = hookContext.schemaSystem();
        const auto collisionOffset = schema.getFieldOffset("C_BaseEntity", "m_pCollision");
        if (!collisionOffset.has_value() || *collisionOffset <= 0)
            return;
        std::byte* collision{};
        std::memcpy(&collision, reinterpret_cast<const std::byte*>(entity) + *collisionOffset, sizeof(collision));
        if (!collision)
            return;
        const auto minsOffset = schema.getFieldOffset("CCollisionProperty", "m_vecMins");
        const auto maxsOffset = schema.getFieldOffset("CCollisionProperty", "m_vecMaxs");
        if (!minsOffset.has_value() || *minsOffset <= 0 || !maxsOffset.has_value() || *maxsOffset <= 0)
            return;
        std::memcpy(&mins, collision + *minsOffset, sizeof(mins));
        std::memcpy(&maxs, collision + *maxsOffset, sizeof(maxs));
        if (!finite(mins) || !finite(maxs)) {
            mins = {-16.0f, -16.0f, 0.0f};
            maxs = {16.0f, 16.0f, 72.0f};
        }
    }

    [[nodiscard]] BackupHealth& backupSlot(std::uint32_t handle) const noexcept
    {
        for (auto& entry : backupHealth)
            if (entry.handle == handle)
                return entry;
        for (auto& entry : backupHealth)
            if (entry.handle == 0) {
                entry = BackupHealth{handle, 100, 100, 0};
                return entry;
            }
        backupHealth[0] = BackupHealth{handle, 100, 100, 0};
        return backupHealth[0];
    }

    void drawPlayer(overlay_layer::EspScene& scene, auto&& converter, auto&& pawn,
                    const cs2::Vector& origin, float distance, std::int64_t nowMs) const noexcept
    {
        float bx, by, bw, bh;
        if (!boundingRect(converter, pawn, origin, bx, by, bw, bh))
            return;

        // Enemy/team color tint: enemies keep their configured colors.
        const auto health = pawn.health();
        const int healthValue = health.hasValue() ? health.value() : 100;

        // Backup-health echo state (hzqst's white damage-fade bar).
        auto& backup = backupSlot(hookContext.template make<BaseEntity>(pawn.baseEntity()).handle().value);
        const auto backupPair = esp_detail::advanceBackupHealth(
            backup.lastHealth, backup.backupHealth, backup.lastChangeMs, healthValue, nowMs,
            GET_CONFIG_VAR(esp_vars::BackupHealthAnim));
        const float backupFill = backupPair.first;
        const std::uint32_t backupRgba = backupPair.second;

        const float right = bx + bw;
        const float bottom = by + bh;

        // ---- Box ----
        if (GET_CONFIG_VAR(esp_vars::BoxEnabled) && scene.rectCount < overlay_layer::kMaxEspRects)
            scene.rects[scene.rectCount++] = overlay_layer::EspRect{
                bx, by, bw, bh,
                static_cast<std::uint32_t>(GET_CONFIG_VAR(esp_vars::BoxColor)),
                static_cast<std::uint8_t>(GET_CONFIG_VAR(esp_vars::BoxStyle)) != 0,
                GET_CONFIG_VAR(esp_vars::BoxFill),
                static_cast<float>(GET_CONFIG_VAR(esp_vars::CornerLength)) / 100.0f,  // corner arm % of the shorter side
                static_cast<float>(GET_CONFIG_VAR(esp_vars::BoxThickness)),
            };

        // ---- Skeleton ----
        // BONE-index pairs (vesta's live-tested CS2 list, NOT hitbox indices -
        // CS2's hitbox 13-18 ordering differs from the CS:GO SDK order my first
        // cut assumed, which drew shoulder-to-hand crossing lines):
        // spine 1-2-3-4-23-6, head 6-7, right arm 6-9-10-11, left arm 6-13-14-15,
        // right leg 1-17-18-19, left leg 1-20-21-22. Read straight from the
        // bone cache via GameSceneNode::boneTransform.
        if (GET_CONFIG_VAR(esp_vars::SkeletonEnabled)) {
            auto&& node = hookContext.template make<BaseEntity>(pawn.baseEntity()).gameSceneNode();
            if (node) {
                const auto skeletonRgba = static_cast<std::uint32_t>(GET_CONFIG_VAR(esp_vars::SkeletonColor));
                constexpr int kSkeletonBonePairs[][2]{
                    { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 23 }, { 23, 6 }, { 6, 7 },   // spine + head
                    { 6, 9 }, { 9, 10 }, { 10, 11 },                                 // right arm
                    { 6, 13 }, { 13, 14 }, { 14, 15 },                              // left arm
                    { 1, 17 }, { 17, 18 }, { 18, 19 },                              // right leg
                    { 1, 20 }, { 20, 21 }, { 21, 22 },                              // left leg
                };
                constexpr int kPairCount = static_cast<int>(sizeof(kSkeletonBonePairs) / sizeof(kSkeletonBonePairs[0]));
                constexpr int kMaxBoneIdx = 23;

                float projected[kMaxBoneIdx + 1][2]{};
                bool visible[kMaxBoneIdx + 1]{};
                for (int pair = 0; pair < kPairCount; ++pair) {
                    if (scene.lineCount + 2 > overlay_layer::kMaxEspLines)
                        break;
                    const int from = kSkeletonBonePairs[pair][0];
                    const int to = kSkeletonBonePairs[pair][1];
                    if (!visible[from]) {
                        const auto bone = node.boneTransform(from);
                        visible[from] = bone.hasValue()
                            && project(converter, bone.value().position, projected[from][0], projected[from][1]);
                    }
                    if (!visible[from])
                        continue;
                    if (!visible[to]) {
                        const auto bone = node.boneTransform(to);
                        visible[to] = bone.hasValue()
                            && project(converter, bone.value().position, projected[to][0], projected[to][1]);
                    }
                    if (!visible[to])
                        continue;
                    scene.lines[scene.lineCount++] = overlay_layer::EspLine{projected[from][0], projected[from][1],
                                                                            projected[to][0], projected[to][1], skeletonRgba, 1.3f};
                }
            }
        }

        // ---- Head circle ----
        // Centered on the head hitbox's real bone (the hitbox chain's live
        // head), radius in px from config.
        if (GET_CONFIG_VAR(esp_vars::HeadCircleEnabled) && scene.circleCount < overlay_layer::kMaxEspCircles) {
            auto&& node = hookContext.template make<BaseEntity>(pawn.baseEntity()).gameSceneNode();
            if (node) {
                const auto hitboxSet = Hitboxes::query(node.raw());
                if (const auto* head = hitboxSet.find(0);
                    head && head->bone >= 0) {
                    const auto bone = node.boneTransform(head->bone);
                    float hx, hy;
                    if (bone.hasValue() && project(converter, bone.value().position, hx, hy))
                        scene.circles[scene.circleCount++] = overlay_layer::EspCircle{
                            hx, hy, static_cast<float>(GET_CONFIG_VAR(esp_vars::HeadCircleRadius)),
                            static_cast<std::uint32_t>(GET_CONFIG_VAR(esp_vars::HeadCircleColor)),
                            static_cast<float>(GET_CONFIG_VAR(esp_vars::BoxThickness))};
                }
            }
        }

        // ---- View line ----
        if (GET_CONFIG_VAR(esp_vars::ViewLineEnabled)) {
            const auto eyeAngles = pawnValue<cs2::Vector>(pawn.baseEntity(), "C_CSPlayerPawn", "m_angEyeAngles");
            const auto eye = pawn.eyePosition();
            if (eyeAngles.hasValue() && eye.hasValue()) {
                const float pitch = eyeAngles.value().x;
                const float yaw = eyeAngles.value().y;
                const float length = static_cast<float>(GET_CONFIG_VAR(esp_vars::ViewLineLength));
                const float pitchRad = pitch * trig::kDegreesToRadians;
                const float yawRad = yaw * trig::kDegreesToRadians;
                const cs2::Vector end{eye.value().x + trig::cosine(pitchRad) * trig::cosine(yawRad) * length,
                                      eye.value().y + trig::cosine(pitchRad) * trig::sine(yawRad) * length,
                                      eye.value().z - trig::sine(pitchRad) * length};
                float ex, ey;
                if (finite(end) && project(converter, end, ex, ey)
                    && scene.lineCount < overlay_layer::kMaxEspLines) {
                    float hx, hy;
                    if (project(converter, eye.value(), hx, hy))
                        scene.lines[scene.lineCount++] = overlay_layer::EspLine{hx, hy, ex, ey, 0x64FFFFFF, 1.0f};
                }
            }
        }

        // ---- Snapline ----
        if (GET_CONFIG_VAR(esp_vars::SnaplineEnabled) && scene.lineCount < overlay_layer::kMaxEspLines)
            scene.lines[scene.lineCount++] = overlay_layer::EspLine{50.0f, 100.0f, bx + bw * 0.5f, bottom,
                                                                    static_cast<std::uint32_t>(GET_CONFIG_VAR(esp_vars::SnaplineColor)), 1.0f};

        // ---- Bars ----
        // Small constant-px-width bars tucked against the box sides (the old
        // percent-of-display widths rendered ~13px-wide bars far off the box).
        const float barWidth = static_cast<float>(GET_CONFIG_VAR(esp_vars::HealthBarWidth));
        if (GET_CONFIG_VAR(esp_vars::HealthBarEnabled) && scene.barCount < overlay_layer::kMaxEspBars) {
            const float fill = static_cast<float>(healthValue < 0 ? 0 : (healthValue > 100 ? 100 : healthValue)) / 100.0f;
            scene.bars[scene.barCount++] = overlay_layer::EspBar{bx - 0.12f, by, barWidth, bh,
                                                                 fill, backupFill,
                                                                 static_cast<std::uint32_t>(GET_CONFIG_VAR(esp_vars::HealthColor)),
                                                                 backupRgba};
        }
        if (GET_CONFIG_VAR(esp_vars::ArmorBarEnabled)) {
            const auto armor = pawnValue<std::uint8_t>(pawn.baseEntity(), "C_CSPlayerPawn", "m_ArmorValue");
            if (armor.hasValue() && armor.value() > 0 && scene.barCount < overlay_layer::kMaxEspBars) {
                const float fill = static_cast<float>(armor.value()) / 100.0f;
                scene.bars[scene.barCount++] = overlay_layer::EspBar{right + 0.12f, by, barWidth - 1.0f, bh,
                                                                     fill, 0.0f,
                                                                     static_cast<std::uint32_t>(GET_CONFIG_VAR(esp_vars::ArmorColor)),
                                                                     0};
            }
        }

        // ---- Damage echo number (the visible half of the damage echo) ----
        // A floating "-N" at the box's right edge, rising and fading over the
        // same 500ms window as the white bar band (the band alone was easy to
        // miss in-game - 2026-10-12 report).
        if (GET_CONFIG_VAR(esp_vars::BackupHealthAnim) && backupRgba != 0) {
            constexpr std::int64_t kEchoLifeMs = 500;
            const std::int64_t age = nowMs - backup.lastChangeMs;
            if (age >= 0 && age < kEchoLifeMs && scene.textCount < overlay_layer::kMaxEspTexts) {
                const float t = static_cast<float>(age) / static_cast<float>(kEchoLifeMs);
                const float alpha = 1.0f - t;
                const int magnitude = backup.backupHealth - healthValue;
                char text[32]{"-0"};
                int len = 1;
                if (magnitude >= 100)
                    text[len++] = static_cast<char>('0' + (magnitude / 100) % 10);
                if (magnitude >= 10)
                    text[len++] = static_cast<char>('0' + (magnitude / 10) % 10);
                text[len++] = static_cast<char>('0' + magnitude % 10);
                text[len] = '\0';
                const auto faded = color::Rgba{255, 120, 120,
                    static_cast<std::uint8_t>(255.0f * alpha + 0.5f)};
                scene.texts[scene.textCount++] = overlay_layer::EspText{
                    right + 0.4f, by + bh * 0.35f - t * 2.2f, false,
                    static_cast<std::uint32_t>(faded), static_cast<float>(GET_CONFIG_VAR(esp_vars::EchoSize)), {}};
                copyText(scene.texts[scene.textCount - 1].text, text);
            }
        }

        // ---- Text ----
        // Name sits just above the box (by - 1.0; the old by - 2.6 floated
        // far over the head), font size from the NameSize config.
        if (GET_CONFIG_VAR(esp_vars::NameEnabled)) {
            const auto name = controllerName(pawn);
            if (name[0] && scene.textCount < overlay_layer::kMaxEspTexts) {
                scene.texts[scene.textCount++] = overlay_layer::EspText{bx + bw * 0.5f, by - 1.0f, true, 0xE6E6ECFF,
                                                                        static_cast<float>(GET_CONFIG_VAR(esp_vars::NameSize)), {}};
                copyText(scene.texts[scene.textCount - 1].text, name);
            }
        }
        if (GET_CONFIG_VAR(esp_vars::WeaponNameEnabled)) {
            const auto* weaponName = pawn.getActiveWeapon().getName();
            if (weaponName && weaponName[0] && scene.textCount < overlay_layer::kMaxEspTexts) {
                scene.texts[scene.textCount++] = overlay_layer::EspText{bx + bw * 0.5f, bottom + 0.6f, true, 0xC8CBD2FF, 12.0f, {}};
                copyText(scene.texts[scene.textCount - 1].text, weaponName);
            }
        }
        // Distance sits just right of the box (right + 0.35, small font) - the
        // old right + 0.8 parked it far from the enemy (2026-10-12 report).
        if (GET_CONFIG_VAR(esp_vars::DistanceEnabled) && scene.textCount < overlay_layer::kMaxEspTexts) {
            // 52.49 hammer units per meter.
            const int meters = static_cast<int>(distance * (1.0f / 52.49f) + 0.5f);
            char text[32]{};
            text[0] = static_cast<char>('0' + (meters / 100) % 10);
            int len = meters >= 100 ? 1 : 0;
            if (meters >= 10)
                text[len++] = static_cast<char>('0' + (meters / 10) % 10);
            text[len++] = static_cast<char>('0' + meters % 10);
            text[len++] = 'm';
            scene.texts[scene.textCount++] = overlay_layer::EspText{right + 0.35f, by + bh * 0.5f, false, 0xBFC3CCFF, 11.0f, {}};
            copyText(scene.texts[scene.textCount - 1].text, text);
        }

        // ---- Info flags (right of the box, past the armor bar, stacked downward) ----
        if (GET_CONFIG_VAR(esp_vars::FlagsEnabled)) {
            float flagY = by;
            const auto pushFlag = [&](const char* label, std::uint32_t rgba) {
                if (scene.textCount >= overlay_layer::kMaxEspTexts)
                    return;
                scene.texts[scene.textCount++] = overlay_layer::EspText{right + 0.4f, flagY, false, rgba, 11.0f, {}};
                copyText(scene.texts[scene.textCount - 1].text, label);
                flagY += 1.6f;
            };
            if (GET_CONFIG_VAR(esp_vars::FlagFlashed)) {
                const auto remaining = pawn.getRemainingFlashBangTime();
                if (remaining > 0.05f)
                    pushFlag("FLASH", 0xE6FFFFFF);
            }
            if (GET_CONFIG_VAR(esp_vars::FlagScoped) && pawn.isScoped().valueOr(false))
                pushFlag("SCOP", 0xE6FFFFFF);
            if (GET_CONFIG_VAR(esp_vars::FlagReloading)) {
                const auto inReload = pawnValue<bool>(pawn.getActiveWeapon().baseEntity(), "C_CSWeaponBase", "m_bInReload");
                if (inReload.valueOr(false))
                    pushFlag("RLD", 0xE6FFFFFF);
            }
            if (GET_CONFIG_VAR(esp_vars::FlagDefusing) && pawn.isDefusing().valueOr(false))
                pushFlag("DEFUSE", 0x9BE8B0FF);
            if (GET_CONFIG_VAR(esp_vars::FlagBomb) && pawn.isCarryingC4())
                pushFlag("BOMB", 0xFF9CB0FF);
            if (GET_CONFIG_VAR(esp_vars::FlagKit)) {
                const auto kit = itemServicesValue<bool>(pawn, "m_bHasDefuser");
                if (kit.valueOr(false))
                    pushFlag("KIT", 0x9BC9E8FF);
            }
        }
    }

    // m_iszPlayerName is an INLINE char array on the controller (PlayerList's
    // proven read): copy the bytes out - never reinterpret the field as a
    // pointer (the name's own bytes as a pointer was the 2026-10-11 crash).
    // Single static scratch buffer: game thread only, consumed immediately.
    [[nodiscard]] const char* controllerName(auto&& pawn) const noexcept
    {
        inlineNameBuffer[0] = '\0';
        const auto controllerHandle = pawnValue<std::uint32_t>(pawn.baseEntity(), "C_CSPlayerPawn", "m_hController");
        if (!controllerHandle.hasValue())
            return inlineNameBuffer;
        auto* const controller = static_cast<cs2::C_BaseEntity*>(hookContext.template make<EntitySystem>()
            .getEntityFromHandle(cs2::CEntityHandle{controllerHandle.value()}));
        if (!controller)
            return inlineNameBuffer;
        const auto nameOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iszPlayerName");
        if (!nameOffset.has_value() || *nameOffset <= 0)
            return inlineNameBuffer;
        const auto* const source = reinterpret_cast<const char*>(
            reinterpret_cast<const std::byte*>(controller) + *nameOffset);
        if (!looksLikeName(source))
            return inlineNameBuffer;
        std::strncpy(inlineNameBuffer, source, sizeof(inlineNameBuffer) - 1);
        inlineNameBuffer[sizeof(inlineNameBuffer) - 1] = '\0';
        return inlineNameBuffer;
    }

    [[nodiscard]] static bool looksLikeName(const char* name) noexcept
    {
        if (!name)
            return false;
        const unsigned char first = static_cast<unsigned char>(name[0]);
        return first >= 0x20 && first != 0x7F;
    }

    template <typename T>
    [[nodiscard]] Optional<T> itemServicesValue(auto&& pawn, const char* fieldName) const noexcept
    {
        const auto servicesOffset = hookContext.schemaSystem().getFieldOffset("C_CSPlayerPawn", "m_pItemServices");
        if (!servicesOffset.has_value() || *servicesOffset <= 0)
            return {};
        std::byte* services{};
        std::memcpy(&services, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity())) + *servicesOffset, sizeof(services));
        if (!services)
            return {};
        const auto fieldOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayer_ItemServices", fieldName);
        if (!fieldOffset.has_value() || *fieldOffset <= 0)
            return {};
        T value{};
        std::memcpy(&value, services + *fieldOffset, sizeof(value));
        return value;
    }

    static void copyText(char (&destination)[32], const char* source) noexcept
    {
        int i = 0;
        for (; i < 31 && source[i]; ++i)
            destination[i] = source[i];
        destination[i] = '\0';
    }

    HookContext& hookContext;
    inline static bool wasActive{false};
    inline static char inlineNameBuffer[40]{};
};