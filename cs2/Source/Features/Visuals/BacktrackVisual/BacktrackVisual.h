#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Entities/GrenadeProjectiles.h>
#include <CS2/Classes/Vector.h>
#include <Features/Visuals/BacktrackVisual/BacktrackVisualConfigVariables.h>
#include <Features/Visuals/Esp/Esp.h> // esp_detail::project
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/GameSceneNode.h>
#include <GameClient/Hitboxes.h>
#include <GameClient/Lagcomp.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <GameClient/Tracing/Tracing.h>
#include <GameClient/WorldToScreen/WorldToClipSpaceConverter.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/OverlayLayer.h>
#include <Utils/ColorUtils.h>
#include <Utils/Trig.h>

// Backtrack record visualization (2026-10-26, friend request after the legit
// backtrack landed): draws what the lag-compensation window actually holds as
// enemies move - a soft model-ghost SILHOUETTE per stored record (the
// record's own captured bones drawn as fat translucent body segments + a
// head blob, so it reads as a chunky humanoid ghost rather than a skeleton).
// The marker at index 0 is the NEWEST record (what a backtracked shot at this
// instant resolves against), fading toward the oldest.
//
// Legit constraints (friend requirements): the ghosts only show while the
// enemy is actually MOVING (live 2D velocity gate - no clutter while they
// hold an angle), every record is wall-checked (a record whose chest point
// is not directly visible from the local eye is never drawn - the trail
// breaks across it too), and with HideInSmoke on, a record whose sight line
// crosses an active smoke volume is hidden as well.
//
// x88-style persistence (2026-10-28 refinement): records do NOT pop off the
// screen when they expire out of the unlag window (or the motion gate
// closes) - the last drawn state is mirrored into a per-pawn linger cache
// and keeps rendering at the oldest-record color, fading to nothing over
// Ghost Linger ms. Wall/smoke checks are re-run every frame while fading, so
// a lingering ghost can never show through geometry.
//
// Distance scaling (same refinement): stroke widths used to be fixed px, so
// on a far player the 6.5px head blob swallowed the whole body - "only a
// circle remains". The projected body length (feet -> head) now scales the
// strokes down proportionally (clamped so the close-up look is unchanged),
// and the overlay channel reserves a full silhouette per record so a record
// is dropped WHOLE when the budget runs out - never half-drawn.
//
// Same producer/consumer shape as the Esp: this runs on the render-start
// chain (fresh view matrix, same game thread that writes the Lagcomp slots),
// re-projects the stored world positions each frame and publishes draw
// primitives through the dedicated overlay_layer::BacktrackScene channel;
// the present thread draws them in renderGameOverlay.
template <typename HookContext>
class BacktrackVisual {
public:
    explicit BacktrackVisual(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(backtrack_visual_vars::Enabled)) {
            if (wasActive)
                overlay_layer::publishBacktrackScene(overlay_layer::BacktrackScene{});
            clearCache();
            wasActive = false;
            return;
        }

        // Session gate (map-transition rule): never walk entities mid-load.
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto curtime = hookContext.globalVars().curtime();
        if (!localPawn || !curtime.hasValue() || curtime.value() < schema_readiness::kMinMapTime) {
            overlay_layer::publishBacktrackScene(overlay_layer::BacktrackScene{});
            clearCache();
            wasActive = false;
            return;
        }
        const auto eye = localPawn.eyePosition();
        if (!eye.hasValue()) {
            overlay_layer::publishBacktrackScene(overlay_layer::BacktrackScene{});
            clearCache();
            wasActive = false;
            return;
        }

        wasActive = true;
        ++frameSerial;
        overlay_layer::BacktrackScene scene{};

        auto&& converter = hookContext.template make<WorldToClipSpaceConverter>();
        auto&& lagcomp = hookContext.template make<Lagcomp>();

        const bool fixedColor = GET_CONFIG_VAR(backtrack_visual_vars::ColorMode) != 0;
        const auto fixedRgba = static_cast<std::uint32_t>(GET_CONFIG_VAR(backtrack_visual_vars::Color));
        const bool hideInSmoke = GET_CONFIG_VAR(backtrack_visual_vars::HideInSmoke);
        const float lingerSeconds = static_cast<float>(GET_CONFIG_VAR(backtrack_visual_vars::LingerMs)) * 0.001f;
        const bool lingerEnabled = lingerSeconds > 0.0f;
        const float now = curtime.value();
        // Smoke spheres are enumerated ONCE per frame (not per record) - the
        // entity walk is too heavy to repeat 80 times a frame.
        cs2::Vector smokeCenters[kMaxSmokeSpheres];
        int smokeCount = 0;
        if (hideInSmoke)
            collectSmokeSpheres(smokeCenters, smokeCount);

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            if (!entityTypeInfo.template is<cs2::C_CSPlayerPawn>())
                return;
            auto&& pawn = hookContext.template make<PlayerPawn>(static_cast<cs2::C_CSPlayerPawn*>(entityIdentity.entity));
            if (pawn.isControlledByLocalPlayer() || pawn.isEnemy() != true || pawn.isAlive() != true)
                return;
            auto* const entity = static_cast<cs2::C_BaseEntity*>(entityIdentity.entity);

            // Every record the server would still credit (the unlag-budget
            // filter lives in validRecords), newest first. count == 0 is the
            // normal end-of-window state - the linger pass below still runs so
            // the last drawn ghosts can fade out instead of popping.
            const typename Lagcomp<HookContext>::Record* records[Lagcomp<HookContext>::kMaxRecords];
            const int count = lagcomp.validRecords(entity, Lagcomp<HookContext>::kMaxRecords, records,
                                                   Lagcomp<HookContext>::kMaxRecords);
            if (count <= 0 && !lingerEnabled)
                return;

            auto&& node = hookContext.template make<BaseEntity>(entity).gameSceneNode();
            if (!node)
                return; // half-gone entity: its cache slot stays untouched and the
                        // orphan sweep below drops it (a ghost we cannot head-mark
                        // or age is never drawn stale)
            const auto hitboxSet = Hitboxes::query(node.raw());
            const auto* const headHitbox = hitboxSet.find(0);
            const int headBone = (headHitbox && headHitbox->bone >= 0) ? headHitbox->bone : -1;

            // Linger cache slot for this pawn (only allocated while persistence
            // is on). Touched every frame this pawn is visited.
            GhostSlot* slot = lingerEnabled ? slotFor(entityIdentity.handle.value) : nullptr;
            if (slot)
                slot->touchedFrame = frameSerial;

            // Live ghosts: only while the character is in motion (a stationary
            // player's records are clutter), every record wall- and
            // smoke-checked at chest height. Records shown here are mirrored
            // into the linger cache so they can fade rather than pop when any
            // gate stops them (expiry, motion, death of the trail...).
            if (count > 0 && isMoving(entity)) {
                for (int r = 0; r < count; ++r) {
                    const auto& record = *records[r];
                    if (record.boneCount <= 0)
                        continue;
                    const cs2::Vector chest{record.origin.x, record.origin.y, record.origin.z + kChestHeight};

                    // No wall-bang ghosts: a record the local eye cannot see
                    // directly (traced at chest height) is never drawn.
                    if (!Tracing::isVisible(eye.value(), chest, localPawn.baseEntity(), entity))
                        continue;
                    // Same rule for smoke: a record inside an active smoke volume
                    // is hidden (the aimbot's geometric smoke-sphere test).
                    if (hideInSmoke && segmentCrossesSmoke(eye.value(), chest, smokeCenters, smokeCount))
                        continue;

                    const float age01 = age01Of(r, count);
                    if (slot)
                        upsertRecord(*slot, record, now, frameSerial, age01);
                    emitSilhouette(scene, converter, record,
                                   recordColor(age01, fixedColor, fixedRgba), headBone);
                }
            }

            // Linger pass: records drawn live in an EARLIER frame (expired out
            // of the unlag window, motion gate closed, ...) keep showing at
            // their last age color and fade to nothing over lingerSeconds.
            // Wall/smoke checks are re-run every frame - a fading ghost never
            // shows through geometry.
            if (slot && lingerEnabled) {
                for (auto& ghost : slot->records) {
                    if (!ghost.used || ghost.lastLiveFrame == frameSerial)
                        continue;
                    const float age = now - ghost.lastSeen;
                    if (age < 0.0f || age > lingerSeconds) {
                        ghost.used = false;
                        continue;
                    }
                    const cs2::Vector chest{ghost.origin.x, ghost.origin.y, ghost.origin.z + kChestHeight};
                    if (!Tracing::isVisible(eye.value(), chest, localPawn.baseEntity(), entity))
                        continue;
                    if (hideInSmoke && segmentCrossesSmoke(eye.value(), chest, smokeCenters, smokeCount))
                        continue;
                    emitSilhouette(scene, converter, ghost,
                                   recordColor(ghost.age01, fixedColor, fixedRgba,
                                               1.0f - age / lingerSeconds),
                                   headBone);
                }
            }
        });

        // Slots nobody visited this frame belong to an enemy that died or
        // left: there is no live entity to wall-check against anymore, so the
        // cache is dropped instead of drawn (the wall rule outranks the fade).
        for (auto& slot : ghostSlots)
            if (slot.used && slot.touchedFrame != frameSerial)
                resetSlot(slot);

        overlay_layer::publishBacktrackScene(scene);
    }

    void onUnload() const noexcept
    {
        wasActive = false;
        clearCache();
        overlay_layer::publishBacktrackScene(overlay_layer::BacktrackScene{});
    }

private:
    // Age color: record 0 (newest) is fully lit, the oldest keeps a third of
    // its alpha so the history reads as a comet tail instead of uniform noise.
    // Gradient mode runs green -> red across the window; fixed mode keeps the
    // user's color and only fades the alpha. `fade` is the linger multiplier
    // (1 for live records, decaying to 0 as an expired ghost disappears).
    [[nodiscard]] static std::uint32_t recordColor(float age01, bool fixedColor, std::uint32_t fixedRgba,
                                                   float fade = 1.0f) noexcept
    {
        age01 = std::clamp(age01, 0.0f, 1.0f);
        std::uint32_t r, g, b, baseAlpha;
        if (fixedColor) {
            r = fixedRgba >> 24 & 0xFF;
            g = fixedRgba >> 16 & 0xFF;
            b = fixedRgba >> 8 & 0xFF;
            baseAlpha = fixedRgba & 0xFF;
        } else {
            r = static_cast<std::uint32_t>(255.0f * age01 + 0.5f);
            g = static_cast<std::uint32_t>(255.0f * (1.0f - age01) + 0.5f);
            b = 40;
            baseAlpha = 230;
        }
        const float alphaScale = (1.0f - 0.7f * age01) * std::clamp(fade, 0.0f, 1.0f);
        const auto alpha = static_cast<std::uint32_t>(static_cast<float>(baseAlpha) * alphaScale + 0.5f);
        return (r << 24) | (g << 16) | (b << 8) | alpha;
    }

    [[nodiscard]] static float age01Of(int recordIndex, int count) noexcept
    {
        return count > 1 ? static_cast<float>(recordIndex) / static_cast<float>(count - 1) : 0.0f;
    }

    // The model-ghost silhouette: the record's captured bones drawn as FAT
    // translucent body segments (thick spine, arms, legs) plus a head blob -
    // the overlapping strokes blend into a soft humanoid ghost, unlike the
    // skeleton's hairlines. Thickness is scaled by the projected body length
    // (kReferenceBodyPx and below shrink proportionally, never grow) so a
    // far player reads as a small ghost instead of one dominating circle.
    // Works on both Lagcomp::Record and the linger cache's GhostRecord - the
    // shared access shape is {origin, boneCount, bones[i].position}.
    template <typename Converter, typename RecordT>
    static void emitSilhouette(overlay_layer::BacktrackScene& scene, Converter& converter,
                               const RecordT& record, std::uint32_t rgba,
                               int headBone) noexcept
    {
        struct Segment { int from; int to; float thickness; };
        // Spine as the fat core, limbs medium, neck thin. The bone list is
        // the Esp's verified map (spine 1-2-3-4-23-6, head 6-7, right arm
        // 6-9-10-11, left arm 6-13-14-15, right leg 1-17-18-19, left leg
        // 1-20-21-22).
        constexpr Segment kSegments[]{
            { 1, 2, kTorsoThickness }, { 2, 3, kTorsoThickness }, { 3, 4, kTorsoThickness },
            { 4, 23, kTorsoThickness }, { 23, 6, kTorsoThickness },   // spine
            { 6, 7, kNeckThickness },                                 // neck
            { 6, 9, kLimbThickness }, { 9, 10, kLimbThickness }, { 10, 11, kLimbThickness },
            { 6, 13, kLimbThickness }, { 13, 14, kLimbThickness }, { 14, 15, kLimbThickness },
            { 1, 17, kLimbThickness }, { 17, 18, kLimbThickness }, { 18, 19, kLimbThickness },
            { 1, 20, kLimbThickness }, { 20, 21, kLimbThickness }, { 21, 22, kLimbThickness },
        };
        constexpr int kSegmentCount = static_cast<int>(sizeof(kSegments) / sizeof(kSegments[0]));
        constexpr int kMaxBoneIdx = 23;

        // All-or-nothing budget: reserve room for the WHOLE silhouette before
        // drawing any of it. The old code drew segments until the channel ran
        // out mid-record (spine + arms visible, legs cut - "the bottom
        // disappears") and leaned on the never-exhausted dot channel for the
        // head, leaving a lone circle. With the raised budget this trips only
        // past 10 fully visible enemies, and then whole records drop instead.
        if (scene.lineCount + kSegmentCount > overlay_layer::kMaxBacktrackLines)
            return;

        const int probeBone = headBone >= 0 && headBone < record.boneCount ? headBone : 7;
        float scale = 1.0f;
        if (probeBone < record.boneCount) {
            float footX, footY, headX, headY;
            if (esp_detail::project(converter, record.origin, footX, footY)
                && esp_detail::project(converter, record.bones[probeBone].position, headX, headY)) {
                const float bodyPx = trig::squareRoot((headX - footX) * (headX - footX)
                                                    + (headY - footY) * (headY - footY));
                scale = std::clamp(bodyPx / kReferenceBodyPx, kMinScale, 1.0f);
            }
        }

        float projected[kMaxBoneIdx + 1][2]{};
        bool ok[kMaxBoneIdx + 1]{};
        for (const auto& segment : kSegments) {
            const int from = segment.from;
            const int to = segment.to;
            if (!ok[from] && from < record.boneCount)
                ok[from] = esp_detail::project(converter, record.bones[from].position,
                                               projected[from][0], projected[from][1]);
            if (!ok[from])
                continue;
            if (!ok[to] && to < record.boneCount)
                ok[to] = esp_detail::project(converter, record.bones[to].position,
                                             projected[to][0], projected[to][1]);
            if (!ok[to])
                continue;
            scene.lines[scene.lineCount++] = overlay_layer::EspLine{projected[from][0], projected[from][1],
                                                                    projected[to][0], projected[to][1],
                                                                    rgba,
                                                                    std::max(1.0f, segment.thickness * scale)};
        }

        // The head blob (slightly bigger than a trail dot) rounds out the
        // silhouette; the head bone doubles as its center. Scaled with the
        // body like the strokes, with a visibility floor.
        const int blobBone = probeBone;
        if (blobBone < record.boneCount && scene.dotCount < overlay_layer::kMaxBacktrackDots) {
            float hx, hy;
            if (esp_detail::project(converter, record.bones[blobBone].position, hx, hy))
                scene.dots[scene.dotCount++] = overlay_layer::BacktrackDot{
                    hx, hy, rgba, std::max(kMinBlobRadiusPx, kHeadBlobRadiusPx * scale)};
        }
    }

    // The motion gate: live horizontal speed of the pawn. Below the walk
    // threshold the enemy is holding an angle - no ghosts. When the offset
    // can't be read, assume moving rather than never showing anything.
    [[nodiscard]] bool isMoving(cs2::C_BaseEntity* entity) const noexcept
    {
        const auto velocityOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_vecAbsVelocity");
        if (!velocityOffset.has_value() || *velocityOffset <= 0)
            return true;
        cs2::Vector velocity{};
        std::memcpy(&velocity, reinterpret_cast<const std::byte*>(entity) + *velocityOffset, sizeof(velocity));
        return trig::squareRoot(velocity.x * velocity.x + velocity.y * velocity.y) >= kMinMotionSpeed;
    }

    // Geometric smoke test - the aimbot's proven smoke-sphere approximation
    // (its IgnoreSmoke check): does the from->to segment cross an active
    // (popped) smoke volume? Smoke grenades enumerate as
    // C_SmokeGrenadeProjectile with m_bDidSmokeEffect set once popped.
    void collectSmokeSpheres(cs2::Vector* out, int& count) const noexcept
    {
        const auto didOffset = hookContext.schemaSystem().getFieldOffset("C_SmokeGrenadeProjectile", "m_bDidSmokeEffect");
        if (!didOffset.has_value() || *didOffset <= 0)
            return;
        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& identity) {
            if (count >= kMaxSmokeSpheres)
                return;
            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(identity.entity));
            if (!baseEntity.classify().template is<cs2::C_SmokeGrenadeProjectile>())
                return;
            bool didSmoke = false;
            std::memcpy(&didSmoke, reinterpret_cast<const std::byte*>(identity.entity) + *didOffset, sizeof(didSmoke));
            if (!didSmoke)
                return;
            const auto origin = baseEntity.absOrigin();
            if (origin.hasValue())
                out[count++] = origin.value();
        });
    }

    // Closest-point distance from the smoke centers to the from->to segment.
    [[nodiscard]] static bool segmentCrossesSmoke(const cs2::Vector& from, const cs2::Vector& to,
                                                  const cs2::Vector* smokeCenters, int smokeCount) noexcept
    {
        constexpr float kSmokeRadius = 144.0f;
        const cs2::Vector segment{to.x - from.x, to.y - from.y, to.z - from.z};
        const float segmentLengthSq = segment.x * segment.x + segment.y * segment.y + segment.z * segment.z;
        if (segmentLengthSq <= 0.0f)
            return false;

        for (int i = 0; i < smokeCount; ++i) {
            const auto& center = smokeCenters[i];
            const cs2::Vector delta{center.x - from.x, center.y - from.y, center.z - from.z};
            const float t = std::clamp((delta.x * segment.x + delta.y * segment.y + delta.z * segment.z) / segmentLengthSq, 0.0f, 1.0f);
            const cs2::Vector closest{from.x + segment.x * t, from.y + segment.y * t, from.z + segment.z * t};
            const float dx = center.x - closest.x;
            const float dy = center.y - closest.y;
            const float dz = center.z - closest.z;
            if (dx * dx + dy * dy + dz * dz <= kSmokeRadius * kSmokeRadius)
                return true;
        }
        return false;
    }

    // ---- Linger cache (x88-style persistence) ----------------------------
    // Mirrors of records that were drawn live, kept after they leave
    // validRecords so they can fade instead of popping. Keyed by pawn handle;
    // only the bone positions the silhouette actually uses are stored (the
    // overlay resamples at 24 bones), not the full 128-bone snapshot.
    static constexpr int kLingerBones = 24; // silhouette bone indices stop at 23 (head/limbs)
    static constexpr int kMaxRecords = Lagcomp<HookContext>::kMaxRecords;
    static constexpr int kGhostSlots = 16;  // enemy pawns (a 5v5 lobby uses 5)

    // .position keeps the access shape identical to Lagcomp::Bone so the
    // shared emitSilhouette works on live records and ghosts alike.
    struct GhostBone { cs2::Vector position; };

    struct GhostRecord {
        bool used{false};
        float simTime{};                    // identity: matches Lagcomp record simulationTime
        float lastSeen{};                   // curtime of the last frame drawn live
        std::uint32_t lastLiveFrame{};      // frame serial of the last frame drawn live
        float age01{};                      // age color it had while live (continuity)
        cs2::Vector origin{};               // feet position for the chest-height checks
        int boneCount{0};                   // clamped to kLingerBones
        GhostBone bones[kLingerBones]{};
    };

    struct GhostSlot {
        bool used{false};
        std::uint32_t handle{0};
        std::uint32_t touchedFrame{0};
        GhostRecord records[kMaxRecords]{};
    };

    [[nodiscard]] static GhostSlot* slotFor(std::uint32_t handle) noexcept
    {
        GhostSlot* spare = nullptr;
        for (auto& slot : ghostSlots) {
            if (slot.used) {
                if (slot.handle == handle)
                    return &slot;
                continue;
            }
            if (!spare)
                spare = &slot;
        }
        if (!spare) {
            // More live enemies than slots (never in practice): evict the
            // pawn seen longest ago.
            spare = &ghostSlots[0];
            for (auto& slot : ghostSlots)
                if (slot.touchedFrame < spare->touchedFrame)
                    spare = &slot;
        }
        resetSlot(*spare);
        spare->used = true;
        spare->handle = handle;
        spare->touchedFrame = frameSerial;
        return spare;
    }

    // Mirror a record just drawn live into the cache: refreshed in place when
    // the simulation time matches (the common per-frame path), oldest evicted
    // when the per-pawn record array is full.
    static void upsertRecord(GhostSlot& slot, const typename Lagcomp<HookContext>::Record& record,
                             float now, std::uint32_t frame, float age01) noexcept
    {
        GhostRecord* spare = nullptr;
        for (auto& ghost : slot.records) {
            if (ghost.used) {
                if (std::fabs(ghost.simTime - record.simulationTime) < 1e-4f) {
                    refreshGhost(ghost, record, now, frame, age01);
                    return;
                }
                continue;
            }
            if (!spare)
                spare = &ghost;
        }
        if (!spare) {
            spare = &slot.records[0];
            for (auto& ghost : slot.records)
                if (ghost.lastSeen < spare->lastSeen)
                    spare = &ghost;
        }
        refreshGhost(*spare, record, now, frame, age01);
    }

    static void refreshGhost(GhostRecord& ghost, const typename Lagcomp<HookContext>::Record& record,
                             float now, std::uint32_t frame, float age01) noexcept
    {
        ghost.used = true;
        ghost.simTime = record.simulationTime;
        ghost.lastSeen = now;
        ghost.lastLiveFrame = frame;
        ghost.age01 = age01;
        ghost.origin = record.origin;
        ghost.boneCount = record.boneCount < kLingerBones ? record.boneCount : kLingerBones;
        for (int i = 0; i < ghost.boneCount; ++i)
            ghost.bones[i].position = record.bones[i].position;
    }

    static void resetSlot(GhostSlot& slot) noexcept
    {
        slot = GhostSlot{};
    }

    static void clearCache() noexcept
    {
        for (auto& slot : ghostSlots)
            if (slot.used)
                resetSlot(slot);
    }

    static constexpr float kHeadBlobRadiusPx = 6.5f;
    static constexpr float kMinBlobRadiusPx = 2.0f;
    static constexpr float kTorsoThickness = 13.0f;
    static constexpr float kLimbThickness = 8.0f;
    static constexpr float kNeckThickness = 6.0f;
    // Player height (px) at which the fixed thicknesses above were tuned;
    // below it all strokes scale down proportionally (emitSilhouette).
    static constexpr float kReferenceBodyPx = 240.0f;
    static constexpr float kMinScale = 0.25f;
    // Chest height for the per-record wall/smoke checks - the middle of the
    // player hull, so a record behind cover is rejected even when its head
    // would peek.
    static constexpr float kChestHeight = 46.0f;
    // Units/s below which a pawn counts as holding an angle (a walk is ~250).
    static constexpr float kMinMotionSpeed = 10.0f;
    static constexpr int kMaxSmokeSpheres = 16;

    inline static bool wasActive = false;
    inline static std::uint32_t frameSerial = 0;
    inline static GhostSlot ghostSlots[kGhostSlots]{};

    HookContext& hookContext;
};
