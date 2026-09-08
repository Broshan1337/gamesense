#pragma once

#include <cstddef>
#include <cstring>
#include <cstdint>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Vector.h>

// Reads a pawn's REAL hitbox capsules out of its model - the foundation everything exact in the
// ragebot sits on: true multipoint (capsule anchors + radius), per-hitbox hitchance geometry and
// hitgroup mapping.
//
// LINUX CHAIN (fully re-derived + live-verified 2026-09-05 against the running client via
// /proc/pid/mem; found by following the game's own code, not by porting velocity's Windows
// offsets):
//
//   modelState = gameSceneNode + 0x140                      (schema CSkeletonInstance::m_modelState)
//   entry      = *(void**)(modelState + 0xA0)               (CModelState::m_hModel - on this Linux
//                                                            client a POINTER to a resource entry,
//                                                            not a plain CModel*)
//   cmodel     = *(void**)entry                             (the CModel*; entry+8 is its name string,
//                                                            e.g. "agents/models/ctm_sas/ctm_sas.vmdl")
//   setIdx     = *(std::uint8_t*)(node + 0x40C)             (CSkeletonInstance::m_nHitboxSet, schema;
//                                                            0 for players)
//   partCount  = *(std::uint32_t*)(cmodel + 0x70)           (PermModelExtPart list count; 5 on players)
//   part       = ((void**)*(cmodel + 0x78))[0]              (first part - velocity's "render meshes"
//                                                            pointer lives at the same +0x78)
//   sets data  = gated by (*(std::uint32_t*)(part + 0x164) & 0x7fffffff) != 0, then
//                *(void**)(part + 0x168); element count at part + 0x174
//   set        = setsData + setIdx * 72                     (72-byte elements; the game's own
//                                                            CModel::GetHitboxSet helper computes
//                                                            exactly this: [data + idx*72 + 0x18])
//   hitboxCount = *(std::uint32_t*)(set + 0x28)             (19 on player models)
//   entries    = *(void**)(set + 0x30)                      (0x70-byte strides)
//   per entry:  mins vec3 @ +0x18, maxs vec3 @ +0x24, radius f32 @ +0x30,
//               shape u32 @ +0x3C (2 = capsule; player models are all-capsule),
//               translation-only u8 @ +0x3D, bone-remap index u16 @ +0x48
//   bone resolution (identical to the game's GetBoneIndexForHitboxForMesh @ libclient 0x2471490 and
//   to velocity's Windows cmodel offsets - 0x220/0x228/0x240/0x2F0 are THE SAME on Linux):
//               remapCount = *(cmodel + 0x220); remap = *(cmodel + 0x228);
//               meshA = *(cmodel + 0x240); meshB = *(cmodel + 0x2F0)
//               slot = meshA[0] + meshB[0] + remapIndex;  bone = (int16) remap[slot]
//   The resolved bone indexes the LIVE/recorded bone cache DIRECTLY (cache[i] is model bone i -
//   live-verified: head hitbox -> bone 7, cache[7] transforms land at head height).
//
// Live validation on ctm_sas: 19 hitboxes in CS:GO-standard order (0 head r=4.3, 1 neck, 2 pelvis,
// 3 stomach, 4-6 spine/chest, 7-12 limbs, 13-14 hands, 15-18 legs/feet), every remap resolving to a
// unique plausible bone.
//
// All reads are plain memcpy through possibly-dead pointers guarded by null + plausibility checks;
// any failure returns an empty set, never crashes. Offsets inside CModelState/CModel are build
// data (like the bone cache 0x5C/0x80 pair) - re-verify after a game update using the recipe in
// the Claude memory (reference_pastoskeet_dump + the /proc/pid/mem scripts).
class Hitboxes {
public:
    static constexpr int kMaxHitboxes = 20;

private:
    // Rejects the garbage a wrong-offset/stale read typically produces. Real CS2 heap pointers on
    // Linux sit above 64 KiB, below the 128 TiB user boundary, and are at least 8-byte aligned.
    [[nodiscard]] static bool plausiblePointer(std::uintptr_t value) noexcept
    {
        constexpr std::uintptr_t kMinAddress = 0x10000;
        constexpr std::uintptr_t kMaxUserAddress = 0x7FFFFFFFFFFFull;
        return value >= kMinAddress && value <= kMaxUserAddress && (value & 0x7u) == 0;
    }

public:

    struct Entry {
        int index;
        int bone;
        cs2::Vector mins;
        cs2::Vector maxs;
        float radius;
        bool boxShape;          // shape @ +0x3C != 2: treated as a box rather than a capsule
        bool translationOnly;   // @ +0x3D: hitbox transform ignores bone rotation
    };

    struct Set {
        Entry entries[kMaxHitboxes];
        int count{0};

        [[nodiscard]] const Entry* find(int hitboxIndex) const noexcept
        {
            for (int i = 0; i < count; ++i) {
                if (entries[i].index == hitboxIndex)
                    return &entries[i];
            }
            return nullptr;
        }
    };

    [[nodiscard]] static Set query(const cs2::CGameSceneNode* gameSceneNode) noexcept
    {
        Set result;
        if (!gameSceneNode)
            return result;

        // modelState (schema field CSkeletonInstance::m_modelState, 0x140 in every dump since
        // 2026-08) then m_hModel (CModelState + 0xA0, same offset AgentChanger writes).
        const auto* modelState = reinterpret_cast<const std::byte*>(gameSceneNode) + kModelStateOffset;
        const auto modelEntry = read<std::uintptr_t>(modelState, kModelStateModelOffset);
        if (!plausiblePointer(modelEntry))
            return result;

        const auto cmodel = read<std::uintptr_t>(reinterpret_cast<const void*>(modelEntry), 0);
        if (!plausiblePointer(cmodel))
            return result;

        const auto hitboxSetIndex = read<std::uint8_t>(gameSceneNode, kHitboxSetIndexOffset);

        const auto partCount = read<std::uint32_t>(reinterpret_cast<const void*>(cmodel), 0x70);
        if (partCount == 0 || partCount > kMaxSanePartCount)
            return result;
        const auto parts = read<std::uintptr_t>(reinterpret_cast<const void*>(cmodel), 0x78);
        if (!plausiblePointer(parts))
            return result;
        const auto part = read<std::uintptr_t>(reinterpret_cast<const void*>(parts), 0);
        if (!plausiblePointer(part))
            return result;

        if ((read<std::uint32_t>(reinterpret_cast<const void*>(part), 0x164) & 0x7FFFFFFFu) == 0)
            return result;
        const auto setsData = read<std::uintptr_t>(reinterpret_cast<const void*>(part), 0x168);
        if (!plausiblePointer(setsData))
            return result;
        const auto setCount = read<std::uint32_t>(reinterpret_cast<const void*>(part), 0x174);
        if (hitboxSetIndex >= setCount)
            return result;

        const auto* set = reinterpret_cast<const std::byte*>(setsData)
            + static_cast<std::size_t>(hitboxSetIndex) * kHitboxSetStride;
        const auto hitboxCount = read<std::uint32_t>(set, 0x28);
        if (hitboxCount == 0 || hitboxCount > kMaxHitboxes)
            return result;
        const auto hitboxes = read<std::uintptr_t>(set, 0x30);
        if (!plausiblePointer(hitboxes))
            return result;

        const auto remapCount = read<std::uint32_t>(reinterpret_cast<const void*>(cmodel), 0x220);
        const auto remapTable = read<std::uintptr_t>(reinterpret_cast<const void*>(cmodel), 0x228);
        const auto meshA = read<std::uintptr_t>(reinterpret_cast<const void*>(cmodel), 0x240);
        const auto meshB = read<std::uintptr_t>(reinterpret_cast<const void*>(cmodel), 0x2F0);

        std::size_t remapBase = 0;
        const bool hasRemap = plausiblePointer(remapTable) && plausiblePointer(meshA) && plausiblePointer(meshB)
            && remapCount > 0 && remapCount <= kMaxSaneRemapCount;
        if (hasRemap) {
            remapBase = static_cast<std::size_t>(read<std::uint16_t>(reinterpret_cast<const void*>(meshA), 0))
                + static_cast<std::size_t>(read<std::uint16_t>(reinterpret_cast<const void*>(meshB), 0));
        }

        for (std::uint32_t i = 0; i < hitboxCount; ++i) {
            const auto base = hitboxes + static_cast<std::uintptr_t>(i) * kHitboxStride;

            auto bone = -1;
            if (hasRemap) {
                const auto remapIndex = read<std::uint16_t>(reinterpret_cast<const void*>(base), 0x48);
                const auto slot = remapBase + remapIndex;
                if (slot < static_cast<std::size_t>(remapCount))
                    bone = read<std::int16_t>(reinterpret_cast<const void*>(remapTable), 2 * slot);
            }

            if (bone < 0)
                continue;

            const auto radius = read<float>(reinterpret_cast<const void*>(base), 0x30);
            if (radius < 0.0f || radius > 100.0f)
                continue;

            auto& entry = result.entries[result.count++];
            entry.index = static_cast<int>(i);
            entry.bone = bone;
            entry.mins = read<cs2::Vector>(reinterpret_cast<const void*>(base), 0x18);
            entry.maxs = read<cs2::Vector>(reinterpret_cast<const void*>(base), 0x24);
            entry.radius = radius;
            entry.boxShape = read<std::uint32_t>(reinterpret_cast<const void*>(base), 0x3C) != kShapeCapsule;
            entry.translationOnly = read<std::uint8_t>(reinterpret_cast<const void*>(base), 0x3D) != 0;
        }

        return result;
    }

    // CS2 hitbox-index -> hitgroup, from velocity's hitgroup_from_hitbox. The live player model
    // confirmed the CS:GO-standard hitbox order this table was written for (0 head, 1 neck,
    // 2 pelvis, 3 stomach, 4-6 chest, 7-12 legs+feet, 13-18 arms/hands). Used for damage scaling
    // and for validating that an off-center multipoint actually landed in its hitbox's group.
    [[nodiscard]] static int hitgroupFromHitbox(int hitbox) noexcept
    {
        switch (hitbox) {
        case 0:  return 1;  // head
        case 1:  return 8;  // neck
        case 2:
        case 3:  return 3;  // stomach / pelvis
        case 4:
        case 5:
        case 6:  return 2;  // chest (lower, upper, ...)
        case 7:
        case 9:
        case 11: return 7;  // right leg
        case 8:
        case 10:
        case 12: return 6;  // left leg / feet
        case 13:
        case 15:
        case 16: return 5;  // right arm
        case 14:
        case 17:
        case 18: return 4;  // left arm / hands
        default: return 0;
        }
    }

private:
    template <typename T>
    [[nodiscard]] static T read(const void* base, std::uintptr_t offset) noexcept
    {
        T value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(base) + offset, sizeof(T));
        return value;
    }

    // CGameSceneNode / CModelState layout (see header comment for the derivation).
    static constexpr std::ptrdiff_t kModelStateOffset = 0x140;       // CSkeletonInstance::m_modelState (schema)
    static constexpr std::ptrdiff_t kModelStateModelOffset = 0xA0;   // CModelState::m_hModel (resource entry ptr)
    static constexpr std::ptrdiff_t kHitboxSetIndexOffset = 0x40C;   // CSkeletonInstance::m_nHitboxSet (schema)
    // CModel / part / hitbox-set layout.
    static constexpr std::size_t kHitboxSetStride = 72;              // per-set element stride inside a part
    static constexpr std::size_t kHitboxStride = 0x70;               // per-hitbox entry stride
    static constexpr std::uint32_t kShapeCapsule = 2;
    static constexpr std::uint32_t kMaxSanePartCount = 16;
    static constexpr std::uint32_t kMaxSaneRemapCount = 4096;
};
