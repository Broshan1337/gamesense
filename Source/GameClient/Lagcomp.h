#pragma once

#include <cstddef>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/GlobalVars.h>
#include <CS2/Classes/Vector.h>
#include <Features/Combat/ShotGeometry.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/GameSceneNode.h>
#include <MemoryPatterns/PatternTypes/GameSceneNodePatternTypes.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>
#include <Utils/VerifyConsole.h>

// Port of velocity-cs2's shared::lagcomp (features/combat/combat.hpp + impl/shared.cpp) - the FULL
// bone-history subsystem the exact ragebot is built on. Every frame it snapshots each alive enemy's
// ENTIRE bone cache (up to 128 bones, position+scale+rotation per 0x20-byte entry) into per-pawn
// records; when the ragebot fires it can apply() a past record over the live bone cache, aim at real
// hitbox capsules from that moment, and stamp the record's tick so the server rewinds there.
//
// The delicate part is setup(): a remote pawn's bone cache only holds its latest interpolated state,
// so to snapshot DISTINCT ticks velocity forces a rebuild by writing the entity's networked
// origin/rotation into the absolute transforms, pinning globalvars tick/simtime to "now", and calling
// the game's own CSkeletonInstance mask setters (setMeshGroupMask(0xFFFFF) + setSkeleton(0x100),
// resolved by pattern - see GameSceneNodePatternTypes.h for the Linux identification), then restores
// everything and copies the regenerated cache out.
//
// Deviation from velocity, documented: record VALIDITY uses the local controller's m_iPing/2 as the
// one-way latency source instead of INetChannel::GetLatency(FLOW_OUTGOING) (net-channel access not
// wired yet); sv_maxunlag_player does not exist on this Linux build. See unlagBudgetSeconds().
template <typename HookContext>
class Lagcomp {
public:
    static constexpr int kMaxRecords = 16;
    static constexpr int kMaxBoneCount = 128;
    static constexpr std::size_t kBoneStride = 0x20; // pos(12) + scale(4) + rotation quat(16)
    static constexpr float kTickInterval = 0.015625f;
    // Fallback only - the real value is read from the sv_maxunlag cvar (see unlagBudget()).
    static constexpr float kDefaultMaxUnlag = 1.0f;

    struct Bone {
        cs2::Vector position;
        float scale;
        float rotation[4];
    };

    struct Record {
        bool valid{false};
        bool extrapolated{false};
        float simulationTime{};
        int tick{-1};
        int boneCount{0};
        cs2::Vector origin{};
        cs2::CGameSceneNode* node{}; // needed by apply()/restore() to find the live bone cache
        Bone bones[kMaxBoneCount]{};
    };

    struct Slot {
        cs2::C_BaseEntity* pawn{};
        // Full CEntityHandle value (index | serial << 15) captured from the entity identity at
        // record time. Pointers get recycled across respawns and map changes - matching by pointer
        // alone let a fresh pawn inherit another life/world's bone history (memcpy'd against freed
        // nodes by apply()). A handle mismatch on the same address now wipes the slot instead.
        std::uint32_t handleValue{0};
        int head{-1};   // most recent record
        int count{0};
        Record records[kMaxRecords]{};
    };


    struct Result {
        cs2::Vector aimPoint;
        int hitgroup;
        float simulationTime;
    };

    explicit Lagcomp(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // velocity's is_valid budget: sv_maxunlag minus one-way latency. Source substitutions documented:
    //   * sv_maxunlag_player does not exist on this Linux build (strings-verified), so velocity's
    //     min(v, player) collapses to just sv_maxunlag.
    //   * one-way latency = local controller m_iPing / 2 (round-trip ms, schema-resolved) instead of
    //     INetChannel::GetLatency(FLOW_OUTGOING) - same quantity without net-channel access; swap in
    //     the net-channel read when that RE lands.
    [[nodiscard]] float unlagBudgetSeconds() const noexcept
    {
        const auto maxUnlag = hookContext.template make<CvarSystem>().readFloatConVar("sv_maxunlag").value_or(kDefaultMaxUnlag);
        const auto pingMs = hookContext.localPlayerController().ping().valueOr(0);
        return maxUnlag - static_cast<float>(pingMs) / 2000.0f;
    }

    // Per-frame update: capture a new record for every alive enemy whose simulation tick advanced.
    void run() const noexcept
    {
        const auto simOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_flSimulationTime");
        if (!simOffset.has_value() || *simOffset <= 0)
            return;

        int enemiesSeen = 0;
        int recordsAdded = 0;

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& identity) {
            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(identity.entity));
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;
            auto&& target = baseEntity.template as<PlayerPawn>();
            if (!target || target.isControlledByLocalPlayer() || target.isEnemy() != true || target.isAlive() != true)
                return;

            auto* const entity = static_cast<cs2::C_BaseEntity*>(target.baseEntity());
            auto& slot = slotFor(entity, identity.handle.value);
            ++enemiesSeen;

            float simTime{};
            std::memcpy(&simTime, reinterpret_cast<const std::byte*>(entity) + *simOffset, sizeof(simTime));
            const auto simTick = static_cast<int>(simTime / kTickInterval);

            if (slot.count > 0 && slot.records[slot.head].tick >= simTick)
                return; // no new simulation tick yet

            auto&& node = target.baseEntity().gameSceneNode();
            if (!node)
                return;

            slot.head = (slot.head + 1) % kMaxRecords;
            auto& record = slot.records[slot.head];
            record = Record{};
            record.valid = setupRecord(entity, node.raw(), simTime, simTick, record);
            if (record.valid)
                record.origin = readVector(node.raw(), kOriginOffset);
            if (slot.count < kMaxRecords)
                ++slot.count;
            ++recordsAdded;
        });

    }

private:
    // velocity's record::setup: force the game to regenerate this pawn's skeleton for "now", then
    // snapshot the regenerated bone cache. Offsets from the 2026-08-20 cs2-dumper output:
    // CSkeletonInstance::m_modelState (schema), CGameSceneNode m_vecOrigin=0x80 / m_angRotation=0xB8 /
    // m_vecAbsOrigin=0xC8 / m_angAbsRotation=0xD4; bone array ptr = modelState+0x80, count =
    // modelState+0x5C (our in-game-verified Linux layout - NOT velocity's Windows +0x80/+0x8C).
    [[nodiscard]] bool setupRecord(cs2::C_BaseEntity* entity, cs2::CGameSceneNode* node, float simTime, int simTick, Record& record) const noexcept
    {
        if (!node)
            return false;

        const auto modelStateOffset = hookContext.schemaSystem().getFieldOffset("CSkeletonInstance", "m_modelState");
        if (!modelStateOffset.has_value() || *modelStateOffset <= 0)
            return false;

        auto* const nodeBytes = reinterpret_cast<std::byte*>(node);
        auto* const modelState = nodeBytes + *modelStateOffset;

        std::uint32_t boneCount{};
        std::memcpy(&boneCount, modelState + kModelStateBoneCountOffset, sizeof(boneCount));
        if (boneCount == 0 || boneCount > kMaxSaneBoneCount)
            return false;
        if (boneCount > kMaxBoneCount)
            boneCount = kMaxBoneCount;

        const auto setMeshGroup = hookContext.patternSearchResults().template get<GameSceneNodeSetMeshGroupMaskFunction>();
        const auto setSkeleton = hookContext.patternSearchResults().template get<GameSceneNodeSetSkeletonFunction>();
        if (!setMeshGroup || !setSkeleton)
            return false;

        auto&& globalVars = hookContext.globalVars();
        if (!globalVars.globalVars)
            return false;
        auto* gv = globalVars.globalVars;
        const float currentTime = gv->curtime;
        const auto backupTickCount = gv->tickCount;

        const auto simOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_flSimulationTime");
        if (!simOffset.has_value() || *simOffset <= 0)
            return false;
        float backupSimTime{};
        std::memcpy(&backupSimTime, reinterpret_cast<const std::byte*>(entity) + *simOffset, sizeof(backupSimTime));

        const auto backupAbsOrigin = readVector(node, kAbsOriginOffset);
        const auto backupAbsRotation = readVector(node, kAbsRotationOffset);

        // Pin absolute transforms to the networked ones and the clock to "now", then let the game
        // regenerate the skeleton through its own mask setters (velocity's exact sequence).
        writeVector(node, kAbsOriginOffset, readVector(node, kOriginOffset));
        writeVector(node, kAbsRotationOffset, readVector(node, kRotationOffset));
        gv->tickCount = static_cast<int>(currentTime / kTickInterval);
        std::memcpy(reinterpret_cast<std::byte*>(entity) + *simOffset, &currentTime, sizeof(currentTime));

        // RE-ENABLED 2026-08-24 after root-causing the 2026-08-23 23:56 SEGV: the crash was NOT these
        // functions (both patterns verified unique-in-binary) but their CALL CONTEXT - we invoked them
        // from inside CreateMove, mid-prediction, while velocity runs record capture from frame-stage
        // 6. Capture has moved to Source2ClientHook_onFrameStageNotify (stage 6) to match. If a similar
        // crash recurs even from the frame-stage context, re-disable and gdb the coredump.
        setMeshGroup(node, kMeshGroupAllMask);
        setSkeleton(node, kSkeletonRebuildMask);

        gv->tickCount = backupTickCount;
        std::memcpy(reinterpret_cast<std::byte*>(entity) + *simOffset, &backupSimTime, sizeof(backupSimTime));
        writeVector(node, kAbsOriginOffset, backupAbsOrigin);
        writeVector(node, kAbsRotationOffset, backupAbsRotation);

        std::byte* boneCache{};
        std::memcpy(&boneCache, modelState + kModelStateBonesOffset, sizeof(boneCache));
        if (!boneCache)
            return false;

        std::memcpy(record.bones, boneCache, static_cast<std::size_t>(boneCount) * kBoneStride);
        record.boneCount = static_cast<int>(boneCount);
        record.simulationTime = simTime;
        record.tick = simTick;
        record.node = node;
        return true;
    }

public:
    // velocity's record::apply/restore: overwrite the LIVE bone cache with a past record (and back).
    // While applied, every bone/hitbox read sees the pawn exactly as it stood on that tick.
    bool apply(const Record& record) const noexcept
    {
        if (!record.valid || applied.record != nullptr)
            return false;

        auto* cache = liveBoneCache(record);
        if (!cache)
            return false;
        const auto copyCount = safeBoneCopyCount(record);
        if (copyCount == 0)
            return false;

        std::memcpy(applied.backup, cache, sizeof(Bone) * copyCount);
        std::memcpy(cache, record.bones, sizeof(Bone) * copyCount);
        applied.record = &record;
        return true;
    }

    void restore() const noexcept
    {
        if (applied.record == nullptr)
            return;

        if (auto* cache = liveBoneCache(*applied.record))
            std::memcpy(cache, applied.backup, sizeof(Bone) * safeBoneCopyCount(*applied.record));
        applied.record = nullptr;
    }

    // Unload-time wipe of the per-pawn record pools (unload() calls it so a re-injection cannot
    // inherit bone history from another world).
    static void clearSlots() noexcept
    {
        for (auto& slot : slots) {
            slot.pawn = nullptr;
            slot.handleValue = 0;
            slot.head = -1;
            slot.count = 0;
        }
    }

    // Records of `entity` newer than maxTicks back, oldest last. Mirrors velocity's
    // get_valid_records (validity budget + tick window).
    [[nodiscard]] int validRecords(cs2::C_BaseEntity* entity, int maxTicks, const Record** out, int outCapacity) const noexcept
    {
        const auto* slot = findSlot(entity);
        if (!slot || slot->count == 0 || maxTicks <= 0)
            return 0;

        const auto curtime = hookContext.globalVars().curtime().valueOr(0.0f);
        const auto budget = unlagBudgetSeconds();
        if (budget <= 0.0f)
            return 0; // latency already eats the whole unlag window
        const float oldestAllowedTime = curtime - budget;
        const int newestTick = slot->records[slot->head].tick;

        int picked = 0;
        for (int back = 0; back < slot->count && picked < outCapacity; ++back) {
            const int index = ((slot->head - back) % kMaxRecords + kMaxRecords) % kMaxRecords;
            const auto& record = slot->records[index];
            if (!record.valid || record.simulationTime < oldestAllowedTime)
                continue;
            // Future-absolute guard (map change / reconnect): a record stamped AFTER the current
            // local time belongs to another world - never a valid rewind target.
            if (record.simulationTime > curtime + 0.1f)
                continue;
            if ((newestTick - record.tick) > maxTicks)
                break;
            out[picked++] = &record;
        }
        return picked;
    }
    // Backtrack-compatible pick: the lowest-FOV past target point for `entity` within maxTicks,
    // honoring the aimbot's hitbox priority over the stored skeletons. The multipoint port (#4)
    // replaces this with full hitbox scanning; kept so the backtrack feature keeps its shape.
    [[nodiscard]] Optional<Result> bestRecord(cs2::C_BaseEntity* entity, const cs2::Vector& eye, float pitch, float yaw, bool head, bool chest, bool stomach, bool arms, bool legs, int maxTicks) const noexcept
    {
        constexpr int kBones = 5;
        constexpr int kBoneIndices[kBones] = {6, 4, 2, 9, 25};   // head, chest, stomach, arms, legs
        constexpr int kHitgroups[kBones] = {1, 2, 3, 4, 6};
        const bool wants[kBones] = {head, chest, stomach, arms, legs};

        const Record* records[kMaxRecords];
        const int count = validRecords(entity, maxTicks, records, kMaxRecords);

        Optional<Result> best;
        float bestFov = 1e9f;
        for (int r = 0; r < count; ++r) {
            const auto& record = *records[r];
            for (int b = 0; b < kBones; ++b) {
                if (!wants[b] || kBoneIndices[b] >= record.boneCount)
                    continue;
                const auto& position = bonePosition(record, kBoneIndices[b]);
                const auto angles = shot_geometry::anglesTo(eye, position);
                const float dPitch = angles.pitch - pitch;
                const float dYaw = trig::normalizeDegrees(angles.yaw - yaw);
                const float fov = trig::squareRoot(dPitch * dPitch + dYaw * dYaw);
                if (fov < bestFov) {
                    bestFov = fov;
                    best = Result{position, kHitgroups[b], record.simulationTime};
                }
                break; // highest-priority resolvable bone for this record only
            }
        }
        return best;
    }

    [[nodiscard]] int pickRecords(cs2::C_BaseEntity* entity, int maxTicks, const Record** out, int outCapacity) const noexcept
    {
        return validRecords(entity, maxTicks, out, outCapacity);
    }

    // velocity's lagcomp::extrapolate (impl/extrapolation.cpp), ported onto our records. Builds a
    // FUTURE record for a pawn by stepping its latest snapshot forward: server-tick delta decides how
    // far, turn-rate prediction bends the heading, and each tick runs gravity + a hull sweep with wall
    // slides (predictMovement). All bones are shifted by the resulting delta. Used as the aimbot's
    // FALLBACK target source when no valid past record exists - exactly velocity's gather_candidates.
    //
    // Substitutions, documented: server_tick comes from the local controller's m_nTickBase (velocity
    // reads netclient->vfunc23()+892; tickBase is the same predicted server tick, schema-resolved),
    // and gravity comes from the sv_gravity cvar instead of a hardcoded 800.
    [[nodiscard]] Optional<Record> extrapolate(cs2::C_BaseEntity* entity, int maxTicks) const noexcept
    {
        const auto* slot = findSlot(entity);
        if (!slot || slot->count == 0 || maxTicks <= 0)
            return {};

        const auto& latest = slot->records[slot->head];
        if (!latest.valid)
            return {};

        const auto serverTick = hookContext.localPlayerController().tickBase();
        if (!serverTick.hasValue())
            return {};
        const int deltaTicks = serverTick.value() - latest.tick;
        if (deltaTicks <= 0)
            return {};
        const int ticksToExtrapolate = deltaTicks < maxTicks ? deltaTicks : maxTicks;

        auto* const entityBytes = reinterpret_cast<const std::byte*>(entity);
        const auto velocityOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_vecVelocity");
        const auto flagsOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_fFlags");
        if (!velocityOffset.has_value() || *velocityOffset <= 0 || !flagsOffset.has_value() || *flagsOffset <= 0)
            return {};

        cs2::Vector velocity{};
        std::memcpy(&velocity, entityBytes + *velocityOffset, sizeof(velocity));
        const auto speed = trig::squareRoot(velocity.x * velocity.x + velocity.y * velocity.y);
        if (speed < 0.1f)
            return {}; // stationary - nothing to lead

        float direction = 0.0f;
        if (velocity.x != 0.0f || velocity.y != 0.0f)
            direction = trig::arcTangent2(velocity.y, velocity.x) * trig::kRadiansToDegrees;

        // Turn-rate prediction: compare our heading against the direction the previous record implies,
        // convert the difference into per-tick yaw rate, reject wild swings (>35 degrees between
        // records) and zero out anything above 6 degrees/tick.
        float directionChange = 0.0f;
        if (slot->count > 1) {
            const auto& prev = slot->records[(slot->head - 1 + kMaxRecords) % kMaxRecords];
            if (prev.valid) {
                const auto dt = latest.simulationTime - prev.simulationTime;
                if (dt > 0.0f) {
                    const auto dx = latest.origin.x - prev.origin.x;
                    const auto dy = latest.origin.y - prev.origin.y;
                    float prevDir = 0.0f;
                    if (dx != 0.0f || dy != 0.0f)
                        prevDir = trig::arcTangent2(dy, dx) * trig::kRadiansToDegrees;
                    auto angleDiff = direction - prevDir;
                    while (angleDiff > 180.0f) angleDiff -= 360.0f;
                    while (angleDiff < -180.0f) angleDiff += 360.0f;
                    if (trig::squareRoot(angleDiff * angleDiff) > 35.0f)
                        return {};
                    directionChange = (angleDiff / dt) * kTickInterval;
                }
            }
        }
        if (directionChange > 6.0f || directionChange < -6.0f)
            directionChange = 0.0f;

        ExtrapolationData data{latest.origin, velocity, {}, {}, {}, latest.simulationTime, direction};
        readCollisionBounds(entity, data.mins, data.maxs);
        std::memcpy(&data.flags, entityBytes + *flagsOffset, sizeof(data.flags));

        const auto gravity = hookContext.template make<CvarSystem>().readFloatConVar("sv_gravity").value_or(800.0f);
        for (int i = 0; i < ticksToExtrapolate; ++i) {
            data.direction += directionChange;
            while (data.direction > 180.0f) data.direction -= 360.0f;
            while (data.direction < -180.0f) data.direction += 360.0f;

            const auto radians = data.direction * trig::kDegreesToRadians;
            const auto currentSpeed = trig::squareRoot(data.velocity.x * data.velocity.x + data.velocity.y * data.velocity.y);
            data.velocity.x = trig::cosine(radians) * currentSpeed;
            data.velocity.y = trig::sine(radians) * currentSpeed;
            data.simTime += kTickInterval;

            predictMovement(data, gravity, entity);
        }

        const auto deltaX = data.origin.x - latest.origin.x;
        const auto deltaY = data.origin.y - latest.origin.y;
        const auto deltaZ = data.origin.z - latest.origin.z;
        if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < 0.01f)
            return {}; // predicted position unchanged

        Record extrapolated = latest;
        extrapolated.origin = data.origin;
        extrapolated.simulationTime = data.simTime;
        extrapolated.extrapolated = true;
        for (int i = 0; i < extrapolated.boneCount && i < kMaxBoneCount; ++i) {
            extrapolated.bones[i].position.x += deltaX;
            extrapolated.bones[i].position.y += deltaY;
            extrapolated.bones[i].position.z += deltaZ;
        }

        return extrapolated;
    }
    // The aim-facing wrapper: lowest-FOV priority-bone point on the EXTRAPOLATED skeleton, so the
    // aimbot can consume a predicted-future shot exactly like a past backtrack one.
    [[nodiscard]] Optional<Result> extrapolatedResult(cs2::C_BaseEntity* entity, const cs2::Vector& eye, float pitch, float yaw, bool head, bool chest, bool stomach, bool arms, bool legs, int maxTicks) const noexcept
    {
        constexpr int kBones = 5;
        constexpr int kBoneIndices[kBones] = {6, 4, 2, 9, 25};   // head, chest, stomach, arms, legs
        constexpr int kHitgroups[kBones] = {1, 2, 3, 4, 6};
        const bool wants[kBones] = {head, chest, stomach, arms, legs};

        const auto extrapolated = extrapolate(entity, maxTicks);
        if (!extrapolated.hasValue())
            return {};

        Optional<Result> best;
        float bestFov = 1e9f;
        for (int b = 0; b < kBones; ++b) {
            if (!wants[b] || kBoneIndices[b] >= extrapolated.value().boneCount)
                continue;
            const auto& position = extrapolated.value().bones[kBoneIndices[b]].position;
            const auto angles = shot_geometry::anglesTo(eye, position);
            const float dPitch = angles.pitch - pitch;
            const float dYaw = trig::normalizeDegrees(angles.yaw - yaw);
            const float fov = trig::squareRoot(dPitch * dPitch + dYaw * dYaw);
            if (fov < bestFov) {
                bestFov = fov;
                best = Result{position, kHitgroups[b], extrapolated.value().simulationTime};
            }
            break;
        }
        return best;
    }

    // The freshest bone snapshot for `entity`: newest valid record if any, else a direct read of the
    // LIVE bone cache (same 32-byte entry layout). Multipoint scanning consumes this - hitbox capsules
    // are oriented by each bone's rotation, which positions-only reads never had.
    [[nodiscard]] bool skeletonFor(cs2::C_BaseEntity* entity, Bone* outBones, int& outCount) const noexcept
    {
        if (const auto* slot = findSlot(entity); slot && slot->count > 0) {
            const auto& latest = slot->records[slot->head];
            if (latest.valid && latest.boneCount > 0) {
                std::memcpy(outBones, latest.bones, sizeof(Bone) * latest.boneCount);
                outCount = latest.boneCount;
                return true;
            }
        }

        auto&& baseEntity = hookContext.template make<BaseEntity>(entity);
        auto&& node = baseEntity.gameSceneNode();
        if (!node || !node.raw())
            return false;
        const auto modelStateOffset = hookContext.schemaSystem().getFieldOffset("CSkeletonInstance", "m_modelState");
        if (!modelStateOffset.has_value() || *modelStateOffset <= 0)
            return false;
        auto* const modelState = reinterpret_cast<std::byte*>(node.raw()) + *modelStateOffset;

        std::uint32_t boneCount{};
        std::memcpy(&boneCount, modelState + kModelStateBoneCountOffset, sizeof(boneCount));
        if (boneCount == 0 || boneCount > kMaxSaneBoneCount)
            return false;
        if (boneCount > kMaxBoneCount)
            boneCount = kMaxBoneCount;

        std::byte* cache{};
        std::memcpy(&cache, modelState + kModelStateBonesOffset, sizeof(cache));
        if (!cache)
            return false;

        std::memcpy(outBones, cache, static_cast<std::size_t>(boneCount) * kBoneStride);
        outCount = static_cast<int>(boneCount);
        return true;
    }

private:
    [[nodiscard]] static const cs2::Vector& bonePosition(const Record& record, int boneIndex) noexcept
    {
        return record.bones[boneIndex].position;
    }

    [[nodiscard]] std::byte* liveBoneCache(const Record& record) const noexcept
    {
        if (!record.node)
            return nullptr;
        const auto modelStateOffset = hookContext.schemaSystem().getFieldOffset("CSkeletonInstance", "m_modelState");
        if (!modelStateOffset.has_value() || *modelStateOffset <= 0)
            return nullptr;
        auto* const modelState = reinterpret_cast<std::byte*>(record.node) + *modelStateOffset;
        std::byte* cache{};
        std::memcpy(&cache, modelState + kModelStateBonesOffset, sizeof(cache));
        return cache;
    }

    // Clamps a record's bone count against the CURRENT live cache before any memcpy through
    // record.node: a model swap (or a node that changed identity since the capture) can leave the
    // live array smaller than what was recorded - writing record.boneCount entries then would
    // overflow it. Also backs up the live side with the SAME clamped size, so restore() undoes
    // exactly what apply() wrote.
    [[nodiscard]] int safeBoneCopyCount(const Record& record) const noexcept
    {
        if (record.boneCount <= 0 || record.boneCount > kMaxBoneCount)
            return 0;
        const auto liveCount = liveBoneCount(record);
        if (liveCount <= 0)
            return 0;
        return record.boneCount < liveCount ? record.boneCount : liveCount;
    }

    [[nodiscard]] int liveBoneCount(const Record& record) const noexcept
    {
        if (!record.node)
            return 0;
        const auto modelStateOffset = hookContext.schemaSystem().getFieldOffset("CSkeletonInstance", "m_modelState");
        if (!modelStateOffset.has_value() || *modelStateOffset <= 0)
            return 0;
        auto* const modelState = reinterpret_cast<std::byte*>(record.node) + *modelStateOffset;
        std::uint32_t boneCount{};
        std::memcpy(&boneCount, modelState + kModelStateBoneCountOffset, sizeof(boneCount));
        if (boneCount == 0 || boneCount > kMaxSaneBoneCount)
            return 0;
        return boneCount > kMaxBoneCount ? kMaxBoneCount : static_cast<int>(boneCount);
    }

    [[nodiscard]] Slot& slotFor(cs2::C_BaseEntity* entity, std::uint32_t handleValue) const noexcept
    {
        for (auto& slot : slots) {
            if (slot.pawn == entity) {
                if (slot.handleValue != handleValue) {
                    // Same address, different entity identity - the pool recycled this pointer.
                    // Drop the stale history; do not let the new pawn inherit it.
                    slot.head = -1;
                    slot.count = 0;
                }
                slot.handleValue = handleValue;
                return slot;
            }
        }
        for (auto& slot : slots) {
            if (slot.pawn == nullptr) {
                slot.pawn = entity;
                slot.handleValue = handleValue;
                slot.head = -1;
                slot.count = 0;
                return slot;
            }
        }
        // Pool full: reuse slot 0 (rare: more simultaneous enemies than slots).
        slots[0].pawn = entity;
        slots[0].handleValue = handleValue;
        slots[0].head = -1;
        slots[0].count = 0;
        return slots[0];
    }

    [[nodiscard]] static const Slot* findSlot(cs2::C_BaseEntity* entity) noexcept
    {
        for (const auto& slot : slots) {
            if (slot.pawn == entity)
                return &slot;
        }
        return nullptr;
    }

    [[nodiscard]] static cs2::Vector readVector(const cs2::CGameSceneNode* node, std::ptrdiff_t offset) noexcept
    {
        cs2::Vector v{};
        std::memcpy(&v, reinterpret_cast<const std::byte*>(node) + offset, sizeof(v));
        return v;
    }

    static void writeVector(cs2::CGameSceneNode* node, std::ptrdiff_t offset, const cs2::Vector& v) noexcept
    {
        std::memcpy(reinterpret_cast<std::byte*>(node) + offset, &v, sizeof(v));
    }

    // Non-schema CModelState offsets - our in-game-verified Linux layout (see bonePosition notes).
    static constexpr std::ptrdiff_t kModelStateBoneCountOffset = 0x5C;
    static constexpr std::ptrdiff_t kModelStateBonesOffset = 0x80;
    static constexpr std::uint32_t kMaxSaneBoneCount = 256;
    // CGameSceneNode offsets from the 2026-08-20 cs2-dumper output.
    static constexpr std::ptrdiff_t kOriginOffset = 0x80;      // m_vecOrigin
    static constexpr std::ptrdiff_t kRotationOffset = 0xB8;    // m_angRotation
    static constexpr std::ptrdiff_t kAbsOriginOffset = 0xC8;   // m_vecAbsOrigin
    static constexpr std::ptrdiff_t kAbsRotationOffset = 0xD4; // m_angAbsRotation
    // velocity's rebuild-call arguments.
    static constexpr std::uint32_t kMeshGroupAllMask = 0xFFFFF;
    static constexpr std::uint32_t kSkeletonRebuildMask = 0x100;

    mutable struct {
        const Record* record{};
        Bone backup[kMaxBoneCount]{};
    } applied;

    // velocity's ExtrapolationData + predict_movement (impl/extrapolation.cpp): one tick of movement
    // with gravity (or grounded z-kill), a hull sweep with up to two wall slides, then a ground
    // re-check. Our Tracing::traceHull replaces their tracing wrapper.
    struct ExtrapolationData {
        cs2::Vector origin{};
        cs2::Vector velocity{};
        cs2::Vector mins{};
        cs2::Vector maxs{};
        std::uint32_t flags{};
        float simTime{};
        float direction{};
    };

    void predictMovement(ExtrapolationData& data, float gravity, cs2::C_BaseEntity* skipEntity) const noexcept
    {
        constexpr std::uint32_t kFlOnGround = 1u << 0;

        if (data.flags & kFlOnGround)
            data.velocity.z = 0.0f;
        else
            data.velocity.z -= gravity * kTickInterval;

        const cs2::Vector moveEnd{
            data.origin.x + data.velocity.x * kTickInterval,
            data.origin.y + data.velocity.y * kTickInterval,
            data.origin.z + data.velocity.z * kTickInterval,
        };

        auto trace = Tracing::traceHull(data.origin, moveEnd, data.mins, data.maxs, skipEntity);

        if (trace.fraction != 1.0f) {
            for (int i = 0; i < 2; ++i) {
                clipVelocity(data.velocity, trace.normal);
                const float remaining = 1.0f - trace.fraction;
                const cs2::Vector clipEnd{
                    trace.endPos.x + data.velocity.x * kTickInterval * remaining,
                    trace.endPos.y + data.velocity.y * kTickInterval * remaining,
                    trace.endPos.z + data.velocity.z * kTickInterval * remaining,
                };
                trace = Tracing::traceHull(trace.endPos, clipEnd, data.mins, data.maxs, skipEntity);
                if (trace.fraction == 1.0f)
                    break;
            }
        }

        data.origin = (trace.fraction == 1.0f) ? moveEnd : trace.endPos;

        const cs2::Vector groundEnd{data.origin.x, data.origin.y, data.origin.z - 2.0f};
        const auto ground = Tracing::traceHull(data.origin, groundEnd, data.mins, data.maxs, skipEntity);
        data.flags &= ~kFlOnGround;
        if (ground.fraction != 1.0f && ground.normal.z > 0.7f)
            data.flags |= kFlOnGround;
    }

    static void clipVelocity(cs2::Vector& velocity, const cs2::Vector& normal) noexcept
    {
        const float dot = velocity.x * normal.x + velocity.y * normal.y + velocity.z * normal.z;
        velocity.x -= normal.x * dot;
        velocity.y -= normal.y * dot;
        velocity.z -= normal.z * dot;

        const float adjust = velocity.x * normal.x + velocity.y * normal.y + velocity.z * normal.z;
        if (adjust < 0.0f) {
            velocity.x -= normal.x * adjust;
            velocity.y -= normal.y * adjust;
            velocity.z -= normal.z * adjust;
        }
    }

    // The target's OBB from its CCollisionProperty; standing-player defaults if unresolvable.
    void readCollisionBounds(cs2::C_BaseEntity* entity, cs2::Vector& mins, cs2::Vector& maxs) const noexcept
    {
        auto&& schema = hookContext.schemaSystem();
        const auto collisionOffset = schema.getFieldOffset("C_BaseEntity", "m_pCollision");
        if (!collisionOffset.has_value() || *collisionOffset <= 0)
            return;
        void* collision{};
        std::memcpy(&collision, reinterpret_cast<const std::byte*>(entity) + *collisionOffset, sizeof(collision));
        if (!collision)
            return;
        const auto minsOffset = schema.getFieldOffset("CCollisionProperty", "m_vecMins");
        const auto maxsOffset = schema.getFieldOffset("CCollisionProperty", "m_vecMaxs");
        if (!minsOffset.has_value() || *minsOffset <= 0 || !maxsOffset.has_value() || *maxsOffset <= 0)
            return;
        std::memcpy(&mins, reinterpret_cast<const std::byte*>(collision) + *minsOffset, sizeof(mins));
        std::memcpy(&maxs, reinterpret_cast<const std::byte*>(collision) + *maxsOffset, sizeof(maxs));
    }

    inline static Slot slots[32]{};

    // TEMPORARY verification accumulators (static: feature objects are rebuilt per command).
    inline static int verifyEnemies{0};
    inline static int verifyRecords{0};
    inline static int verifyTicks{0};

    HookContext& hookContext;
};


