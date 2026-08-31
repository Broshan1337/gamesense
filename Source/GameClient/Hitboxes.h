#pragma once

#include <cstddef>
#include <cstring>
#include <cstdint>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Vector.h>

// Port of velocity-cs2's systems::hitboxes::query (systems/impl/hitboxes.cpp) - reads a pawn's REAL
// hitbox capsules out of its model, which is the foundation everything exact in the ragebot sits on:
// true multipoint (capsule anchors + radius), per-hitbox hitchance geometry and hitgroup mapping.
//
// The chain (offsets verified against velocity's working implementation):
//   gameSceneNode + 0x210 = model handle (CDMA)
//   *modelHandle          = CModel (cmodel)
//   *(cmodel + 0x78)      = ptr -> render meshes
//   renderMeshes + 0x150  = hitbox set
//   hitboxSet + 0x28      = entry count (sanity-capped at 20)
//   hitboxSet + 0x30      = entries array, 0x70-byte strides:
//       +0x18 mins (vec3)   +0x24 maxs (vec3)   +0x30 radius (float)
//       +0x3C shape type    +0x48 bone index into the model's bone remap space (u16)
//   Bone resolution: cmodel + 0x220 remap count / + 0x228 remap table / + 0x240 & + 0x2F0 mesh offset
//   tables (u16 each); slot = hitboxBoneIdx + *meshA + *meshB; bone = remapTable[slot] (i16). Without
//   a resolvable bone the hitbox is skipped - it cannot be aimed at through the skeleton.
//
// All reads are plain memcpy through possibly-dead pointers guarded by null checks; any failure
// returns an empty set, never crashes.
//
// CRASH NOTE (2026-08-24 00:50 SEGV, libOsiris.so+0x1f4a5): velocity guards every read with
// memory::safe_read (SEH on Windows). A null CHECK is not that - the multipoint scan crashed here
// dereferencing a NON-null but invalid model handle read from gameSceneNode+0x210 (that offset comes
// from velocity's WINDOWS build; on this Linux client it may not even be the model handle field).
// Until this chain is re-verified against the Linux client, every pointer stage is additionally run
// through plausiblePointer() and the whole query result should be treated as best-effort.
//
// LINUX DUMP VERDICT (cs2-dumper 2026-08, libclient_so.hpp): CONFIRMED WRONG for this client. The
// dumped layout is CGameSceneNode fields ending ~0x12C, then CSkeletonInstance::m_modelState = 0x140
// (next schema field m_bUseParentRenderBounds at 0x400) - so CModelState spans node+0x140..node+0x3FF
// and gameSceneNode+0x210 is CModelState+0xD0, INTERNAL MODEL STATE DATA, not a model handle. Reading
// it as a pointer is exactly what produced the garbage dereference above. The real model reference on
// Linux is CModelState::m_hModel (CStrongHandle<InfoForResourceTypeCModel>) at modelState+0xA0 =
// node+0x1E0 - a RESOURCE handle that needs the resource system to resolve to a CModel*, NOT a raw
// pointer. Before re-enabling anything that calls query(), rebuild this chain from the Linux client
// (resolve m_hModel through the resource system, then re-derive the cmodel/renderMeshes/hitboxSet
// offsets); the plausibility gate below reduces but cannot eliminate the risk (a freed allocation
// still passes).
class Hitboxes {
public:
    static constexpr int kMaxHitboxes = 20;

private:
    // Rejects the garbage a wrong-offset/stale read typically produces. Null was already checked by
    // the caller; what actually segfaults us is a small integer, a packed-float bit pattern read as a
    // pointer, or a kernel/non-canonical address. Real CS2 heap pointers on Linux sit above 64 KiB,
    // below the 128 TiB user boundary, and are at least 8-byte aligned. NOT a guarantee - a freed
    // allocation still passes - but it catches every invalid-pointer shape observed in practice.
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
        bool boxShape;          // shapeType @ +0x3C != 0 means a box rather than a capsule
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

        const auto modelHandle = read<std::uintptr_t>(gameSceneNode, 0x210);
        if (!plausiblePointer(modelHandle))
            return result;

        const auto cmodel = read<std::uintptr_t>(reinterpret_cast<const void*>(modelHandle), 0);
        if (!plausiblePointer(cmodel))
            return result;

        const auto renderMeshesPtr = read<std::uintptr_t>(reinterpret_cast<const void*>(cmodel), 0x78);
        if (!plausiblePointer(renderMeshesPtr))
            return result;
        const auto renderMeshes = read<std::uintptr_t>(reinterpret_cast<const void*>(renderMeshesPtr), 0);
        if (!plausiblePointer(renderMeshes))
            return result;

        const auto hitboxSet = read<std::uintptr_t>(reinterpret_cast<const void*>(renderMeshes), 0x150);
        if (!plausiblePointer(hitboxSet))
            return result;

        const auto count = read<int>(reinterpret_cast<const void*>(hitboxSet), 0x28);
        if (count <= 0 || count > kMaxHitboxes)
            return result;

        const auto arrayPtr = read<std::uintptr_t>(reinterpret_cast<const void*>(hitboxSet), 0x30);
        if (!plausiblePointer(arrayPtr))
            return result;

        const auto remapCount = read<int>(reinterpret_cast<const void*>(cmodel), 0x220);
        const auto remapTable = read<std::uintptr_t>(reinterpret_cast<const void*>(cmodel), 0x228);
        const auto meshA = read<std::uintptr_t>(reinterpret_cast<const void*>(cmodel), 0x240);
        const auto meshB = read<std::uintptr_t>(reinterpret_cast<const void*>(cmodel), 0x2F0);

        constexpr auto kHitboxStride{0x70};

        for (int i = 0; i < count; ++i) {
            const auto base = arrayPtr + static_cast<std::uintptr_t>(i) * kHitboxStride;

            auto bone = -1;
            if (plausiblePointer(remapTable) && plausiblePointer(meshA) && plausiblePointer(meshB) && remapCount > 0) {
                const auto hitboxBoneIdx = read<std::uint16_t>(reinterpret_cast<const void*>(base), 0x48);
                const auto offsetA = read<std::uint16_t>(reinterpret_cast<const void*>(meshA), 0);
                const auto offsetB = read<std::uint16_t>(reinterpret_cast<const void*>(meshB), 0);
                const auto slot = static_cast<std::size_t>(hitboxBoneIdx) + offsetA + offsetB;

                if (slot < static_cast<std::size_t>(remapCount))
                    bone = read<std::int16_t>(reinterpret_cast<const void*>(remapTable), 2 * slot);
            }

            if (bone < 0)
                continue;

            const auto radius = read<float>(reinterpret_cast<const void*>(base), 0x30);
            if (radius < 0.0f || radius > 100.0f)
                continue;

            auto& entry = result.entries[result.count++];
            entry.index = i;
            entry.bone = bone;
            entry.mins = read<cs2::Vector>(reinterpret_cast<const void*>(base), 0x18);
            entry.maxs = read<cs2::Vector>(reinterpret_cast<const void*>(base), 0x24);
            entry.radius = radius;
            entry.boxShape = read<std::uint8_t>(reinterpret_cast<const void*>(base), 0x3C) != 0;
            entry.translationOnly = read<std::uint8_t>(reinterpret_cast<const void*>(base), 0x3D) != 0;
        }

        return result;
    }

    // CS2 hitbox-index -> hitgroup, from velocity's hitgroup_from_hitbox. Used for damage scaling and
    // for validating that an off-center multipoint actually landed in its hitbox's group.
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
        case 11: return 7;  // legs
        case 8:
        case 10:
        case 12: return 6;  // feet... (velocity's mapping kept verbatim)
        case 13:
        case 15:
        case 16: return 5;  // arms
        case 14:
        case 17:
        case 18: return 4;  // hands
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
};
