#pragma once

#include <cstddef>
#include <cstring>
#include <cstdint>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Vector.h>














































class Hitboxes {
public:
    static constexpr int kMaxHitboxes = 20;

private:
    
    
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
        bool boxShape;          
        bool translationOnly;   
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

    
    
    
    
    [[nodiscard]] static int hitgroupFromHitbox(int hitbox) noexcept
    {
        switch (hitbox) {
        case 0:  return 1;  
        case 1:  return 8;  
        case 2:
        case 3:  return 3;  
        case 4:
        case 5:
        case 6:  return 2;  
        case 7:
        case 9:
        case 11: return 7;  
        case 8:
        case 10:
        case 12: return 6;  
        case 13:
        case 15:
        case 16: return 5;  
        case 14:
        case 17:
        case 18: return 4;  
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

    
    static constexpr std::ptrdiff_t kModelStateOffset = 0x140;       
    static constexpr std::ptrdiff_t kModelStateModelOffset = 0xA0;   
    static constexpr std::ptrdiff_t kHitboxSetIndexOffset = 0x40C;   
    
    static constexpr std::size_t kHitboxSetStride = 72;              
    static constexpr std::size_t kHitboxStride = 0x70;               
    static constexpr std::uint32_t kShapeCapsule = 2;
    static constexpr std::uint32_t kMaxSanePartCount = 16;
    static constexpr std::uint32_t kMaxSaneRemapCount = 4096;
};
