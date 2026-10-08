#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>


















class InputHistory {
public:
    using AddAllocatedFn = void* (*)(void* field, void* element);

    explicit InputHistory(cs2::CUserCmd* cmd) noexcept
        : field{fieldOf(cmd)}
    {
    }

    [[nodiscard]] static std::byte* fieldOf(cs2::CUserCmd* cmd) noexcept
    {
        if (!cmd)
            return nullptr;
        return reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::InputHistory::kFieldOffset;
    }

    
    
    [[nodiscard]] int currentSize() const noexcept
    {
        if (!field)
            return -1;
        int size{};
        std::memcpy(&size, field + fieldRelative(cs2::CUserCmd::InputHistory::kCurrentSizeOffset), sizeof(size));
        return size;
    }

    
    
    
    
    
    
    
    
    [[nodiscard]] bool looksValid() const noexcept
    {
        const int current = currentSize();
        if (current < 0 || current > cs2::CUserCmd::kMaxInputHistoryEntries)
            return false;

        int total{};
        std::memcpy(&total, field + fieldRelative(cs2::CUserCmd::InputHistory::kTotalSizeOffset), sizeof(total));
        if (total < 0 || total > kSaneTotalLimit || current > total)
            return false;

        std::byte* rep = nullptr;
        std::memcpy(&rep, field + fieldRelative(cs2::CUserCmd::InputHistory::kRepOffset), sizeof(rep));
        if (!rep)
            return current == 0 && total == 0; 
        if ((reinterpret_cast<std::uintptr_t>(rep) & 0x7) != 0)
            return false;

        int allocated{};
        std::memcpy(&allocated, rep + cs2::CUserCmd::InputHistory::kRepAllocatedSizeOffset, sizeof(allocated));
        if (allocated < current || allocated > total)
            return false;

        
        
        
        for (int i = 0; i < current; ++i) {
            std::byte* entry = nullptr;
            std::memcpy(&entry, rep + cs2::CUserCmd::InputHistory::kRepElementsOffset + static_cast<std::ptrdiff_t>(i) * sizeof(entry), sizeof(entry));
            if (!entry)
                return false;
        }
        return true;
    }

    
    
    
    [[nodiscard]] int spareSlots() const noexcept
    {
        if (!looksValid())
            return 0;

        std::byte* rep = nullptr;
        std::memcpy(&rep, field + fieldRelative(cs2::CUserCmd::InputHistory::kRepOffset), sizeof(rep));
        if (!rep)
            return 0;
        int allocated{}, current{};
        std::memcpy(&allocated, rep + cs2::CUserCmd::InputHistory::kRepAllocatedSizeOffset, sizeof(allocated));
        std::memcpy(&current, field + fieldRelative(cs2::CUserCmd::InputHistory::kCurrentSizeOffset), sizeof(current));
        return allocated > current ? allocated - current : 0;
    }

    
    [[nodiscard]] int freeSlots() const noexcept
    {
        if (!looksValid())
            return 0;
        return cs2::CUserCmd::kMaxInputHistoryEntries - currentSize();
    }

    
    
    [[nodiscard]] std::byte* entryAt(int index) const noexcept
    {
        if (!looksValid() || index < 0 || index >= currentSize())
            return nullptr;

        std::byte* rep = nullptr;
        std::memcpy(&rep, field + fieldRelative(cs2::CUserCmd::InputHistory::kRepOffset), sizeof(rep));
        std::byte* entry = nullptr;
        std::memcpy(&entry, rep + cs2::CUserCmd::InputHistory::kRepElementsOffset + static_cast<std::ptrdiff_t>(index) * sizeof(entry), sizeof(entry));
        return entry;
    }

    
    
    
    
    
    
    
    
    enum class PublishResult { Refused, PublishedFastPath, PublishedSlotRecycle, PublishedByGame };

    template <typename AddAllocated>
    [[nodiscard]] PublishResult publish(std::byte* entry, AddAllocated&& addAllocated) noexcept
    {
        if (!entry || static_cast<bool>(addAllocated) == false || !looksValid())
            return PublishResult::Refused;

        
        
        
        std::byte* rep = nullptr;
        std::memcpy(&rep, field + fieldRelative(cs2::CUserCmd::InputHistory::kRepOffset), sizeof(rep));

        int current{}, total{}, allocated{};
        std::memcpy(&current, field + fieldRelative(cs2::CUserCmd::InputHistory::kCurrentSizeOffset), sizeof(current));
        std::memcpy(&total, field + fieldRelative(cs2::CUserCmd::InputHistory::kTotalSizeOffset), sizeof(total));
        std::memcpy(&allocated, rep + cs2::CUserCmd::InputHistory::kRepAllocatedSizeOffset, sizeof(allocated));

        if (current >= cs2::CUserCmd::kMaxInputHistoryEntries)
            return PublishResult::Refused;

        if (rep) {
            
            
            if (current != total && allocated != total) {
                if (current < allocated) {
                    std::byte* moved = nullptr;
                    std::memcpy(&moved, rep + cs2::CUserCmd::InputHistory::kRepElementsOffset + static_cast<std::ptrdiff_t>(current) * sizeof(moved), sizeof(moved));
                    std::memcpy(rep + cs2::CUserCmd::InputHistory::kRepElementsOffset + static_cast<std::ptrdiff_t>(allocated) * sizeof(moved), &moved, sizeof(moved));
                }
                std::memcpy(rep + cs2::CUserCmd::InputHistory::kRepElementsOffset + static_cast<std::ptrdiff_t>(current) * sizeof(entry), &entry, sizeof(entry));
                const int grown = current + 1;
                std::memcpy(field + fieldRelative(cs2::CUserCmd::InputHistory::kCurrentSizeOffset), &grown, sizeof(grown));
                ++allocated;
                std::memcpy(rep + cs2::CUserCmd::InputHistory::kRepAllocatedSizeOffset, &allocated, sizeof(allocated));
                return PublishResult::PublishedFastPath;
            }

            
            
            
            if (current != total && allocated == total) {
                void* const fieldArena = readArena();
                if (fieldArena == nullptr) {
                    std::memcpy(rep + cs2::CUserCmd::InputHistory::kRepElementsOffset + static_cast<std::ptrdiff_t>(current) * sizeof(entry), &entry, sizeof(entry));
                    const int grown = current + 1;
                    std::memcpy(field + fieldRelative(cs2::CUserCmd::InputHistory::kCurrentSizeOffset), &grown, sizeof(grown));
                    return PublishResult::PublishedSlotRecycle;
                }
            }
            
            
        }

        const auto result = addAllocated(field, entry);
        if (!result)
            return PublishResult::Refused;

        
        
        int after{};
        std::memcpy(&after, field + fieldRelative(cs2::CUserCmd::InputHistory::kCurrentSizeOffset), sizeof(after));
        return after == current + 1 ? PublishResult::PublishedByGame : PublishResult::Refused;
    }

private:
    [[nodiscard]] void* readArena() const noexcept
    {
        void* arena{nullptr};
        std::memcpy(&arena, field, sizeof(arena));
        return arena;
    }
    [[nodiscard]] static constexpr std::ptrdiff_t fieldRelative(std::ptrdiff_t absoluteCommandOffset) noexcept
    {
        return absoluteCommandOffset - cs2::CUserCmd::InputHistory::kFieldOffset;
    }

    static constexpr int kSaneTotalLimit = cs2::CUserCmd::kMaxInputHistoryEntries * 4;

    std::byte* field;
};
