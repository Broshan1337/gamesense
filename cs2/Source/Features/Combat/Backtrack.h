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
                return; 

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
                break; 
            }
        }
        return best;
    }

private:
    static constexpr int kBones = 5;
    static constexpr int kMaxRecords = 16;
    static constexpr int kMaxSlots = 64;
    static constexpr int kBoneIndices[kBones] = {6, 4, 2, 9, 25};    
    static constexpr int kHitgroups[kBones] = {1, 2, 3, 4, 6};

    struct Record {
        float simulationTime;
        bool valid[kBones];
        cs2::Vector bones[kBones];
    };

    struct Slot {
        cs2::C_BaseEntity* owner;
        int head;   
        int count;
        Record records[kMaxRecords];
    };

    
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
