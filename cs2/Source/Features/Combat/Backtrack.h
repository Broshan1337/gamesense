#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Vector.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <Features/Combat/ShotGeometry.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

// velocity-cs2's lag-comp backtracking. Each frame it snapshots every enemy's target bones (a ring of
// past positions). When the rage aimbot fires with backtrack on, it can aim at one of those PAST
// positions and stamp that record's server tick into the outgoing input_history entry, so the server
// rewinds the enemy there and the shot connects. (On a local no-latency server this has no visible
// effect - the lag-comp window it exploits is ~0 tick - but it is built for parity.)
//
// Records are keyed by the raw pawn pointer (no STL map): a fixed pool of slots, each a small ring of
// records. Only stored when m_flSimulationTime advances, so consecutive records are distinct ticks and
// "N ticks back" == N records back.
template <typename HookContext>
class Backtrack {
public:
    explicit Backtrack(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    struct Result {
        cs2::Vector aimPoint;
        int hitgroup;
        float simulationTime;
    };

    // Snapshot every alive enemy's target bones this frame. Cheap: a handful of bone reads per enemy.
    void update() const noexcept
    {
        const auto simOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_flSimulationTime");
        if (!simOffset.has_value() || *simOffset <= 0)
            return;

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& identity) {
            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(identity.entity));
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;
            auto&& target = baseEntity.template as<PlayerPawn>();
            if (!target || target.isControlledByLocalPlayer() || target.isEnemy() != true || target.isAlive() != true)
                return;

            auto* const entity = static_cast<cs2::C_BaseEntity*>(target.baseEntity());
            float simTime{};
            std::memcpy(&simTime, reinterpret_cast<const std::byte*>(entity) + *simOffset, sizeof(simTime));

            auto& slot = slotFor(entity);
            if (slot.count > 0 && slot.records[slot.head].simulationTime == simTime)
                return; // same tick, nothing new

            auto&& node = target.baseEntity().gameSceneNode();
            Record record{};
            record.simulationTime = simTime;
            for (int i = 0; i < kBones; ++i) {
                const auto bone = node.bonePosition(kBoneIndices[i]);
                record.valid[i] = bone.hasValue();
                if (bone.hasValue())
                    record.bones[i] = bone.value();
            }

            slot.head = (slot.head + 1) % kMaxRecords;
            slot.records[slot.head] = record;
            if (slot.count < kMaxRecords)
                ++slot.count;
        });
    }

    // The best (lowest-FOV from the current view) PAST target point for `entity`, within `maxTicks`
    // records, honoring the same hitbox priority the aimbot uses. {} if backtracking has no usable record.
    [[nodiscard]] Optional<Result> bestRecord(cs2::C_BaseEntity* entity, const cs2::Vector& eye, float pitch, float yaw, bool head, bool chest, bool stomach, bool arms, bool legs, int maxTicks) const noexcept
    {
        const auto* const slot = findSlot(entity);
        if (!slot || slot->count == 0 || maxTicks <= 0)
            return {};

        const bool wants[kBones] = {head, chest, stomach, arms, legs};

        Optional<Result> best;
        float bestFov = 1e9f;
        const int limit = maxTicks < slot->count ? maxTicks : slot->count;
        for (int back = 0; back < limit; ++back) {
            const int index = ((slot->head - back) % kMaxRecords + kMaxRecords) % kMaxRecords;
            const auto& record = slot->records[index];

            for (int b = 0; b < kBones; ++b) {
                if (!wants[b] || !record.valid[b])
                    continue;
                const auto angles = shot_geometry::anglesTo(eye, record.bones[b]);
                const float dPitch = angles.pitch - pitch;
                const float dYaw = trig::normalizeDegrees(angles.yaw - yaw);
                const float fov = trig::squareRoot(dPitch * dPitch + dYaw * dYaw);
                if (fov < bestFov) {
                    bestFov = fov;
                    best = Result{record.bones[b], kHitgroups[b], record.simulationTime};
                }
                break; // highest-priority resolvable bone for this record only
            }
        }
        return best;
    }

private:
    static constexpr int kBones = 5;
    static constexpr int kMaxRecords = 16;
    static constexpr int kMaxSlots = 64;
    static constexpr int kBoneIndices[kBones] = {6, 4, 2, 9, 25};    // head, chest, stomach, arms, legs
    static constexpr int kHitgroups[kBones] = {1, 2, 3, 4, 6};

    struct Record {
        float simulationTime;
        bool valid[kBones];
        cs2::Vector bones[kBones];
    };

    struct Slot {
        cs2::C_BaseEntity* owner;
        int head;   // index of the most recent record
        int count;
        Record records[kMaxRecords];
    };

    // Finds `entity`'s slot, or claims a free/stale one for it (resetting its ring).
    [[nodiscard]] static Slot& slotFor(cs2::C_BaseEntity* entity) noexcept
    {
        for (auto& slot : slots)
            if (slot.owner == entity)
                return slot;
        for (auto& slot : slots) {
            if (slot.owner == nullptr) {
                slot.owner = entity;
                slot.head = 0;
                slot.count = 0;
                return slot;
            }
        }
        // Pool full: reuse slot 0 (rare; more than kMaxSlots simultaneous enemies).
        slots[0].owner = entity;
        slots[0].head = 0;
        slots[0].count = 0;
        return slots[0];
    }

    [[nodiscard]] static const Slot* findSlot(cs2::C_BaseEntity* entity) noexcept
    {
        for (const auto& slot : slots)
            if (slot.owner == entity)
                return &slot;
        return nullptr;
    }

    inline static Slot slots[kMaxSlots]{};

    HookContext& hookContext;
};
