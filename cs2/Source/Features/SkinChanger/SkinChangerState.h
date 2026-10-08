#pragma once

#include <optional>

#include <CS2/Classes/EntitySystem/CEntityHandle.h>
#include <CS2/Econ/PaintKitIndex.h>








class SkinChangerState {
public:
    static constexpr auto kMaxTracked = 32;

    struct OriginalState {
        int paintKit{};
        int seed{};
        int wearPermille{};
    };

    [[nodiscard]] bool alreadyApplied(cs2::CEntityHandle handle, int paintKit, int seed, int wearPermille, int statTrak) const noexcept
    {
        for (const auto& entry : entries) {
            if (entry.tracked && entry.handle == handle)
                return entry.paintKit == paintKit && entry.seed == seed && entry.wearPermille == wearPermille && entry.statTrak == statTrak;
        }
        return false;
    }

    
    
    
    
    
    
    
    
    
    
    
    void rememberOriginalPaintKit(cs2::CEntityHandle handle, int originalPaintKit, int originalSeed, int originalWearPermille) noexcept
    {
        for (const auto& entry : entries) {
            if (entry.tracked && entry.handle == handle)
                return;
        }
        for (auto& entry : entries) {
            if (!entry.tracked) {
                entry = Entry{handle, originalPaintKit, originalSeed, originalWearPermille, 0, originalPaintKit, originalSeed, originalWearPermille, true};
                return;
            }
        }
    }

    [[nodiscard]] std::optional<OriginalState> originalState(cs2::CEntityHandle handle) const noexcept
    {
        for (const auto& entry : entries) {
            if (entry.tracked && entry.handle == handle)
                return OriginalState{entry.originalPaintKit, entry.originalSeed, entry.originalWearPermille};
        }
        return std::nullopt;
    }

    void markApplied(cs2::CEntityHandle handle, int paintKit, int seed, int wearPermille, int statTrak) noexcept
    {
        for (auto& entry : entries) {
            if (entry.tracked && entry.handle == handle) {
                entry.paintKit = paintKit;
                entry.seed = seed;
                entry.wearPermille = wearPermille;
                entry.statTrak = statTrak;
                return;
            }
        }
        for (auto& entry : entries) {
            if (!entry.tracked) {
                
                
                
                
                entry = Entry{handle, paintKit, seed, wearPermille, statTrak, paintKit, seed, wearPermille, true};
                return;
            }
        }
    }

    
    
    
    template <typename IsEntityHandleAlive>
    void releaseDeadEntities(IsEntityHandleAlive&& isEntityHandleAlive) noexcept
    {
        for (auto& entry : entries) {
            if (entry.tracked && !isEntityHandleAlive(entry.handle))
                entry.tracked = false;
        }
    }

private:
    struct Entry {
        cs2::CEntityHandle handle{};
        
        int paintKit{};
        int seed{};
        int wearPermille{};
        int statTrak{};
        
        
        int originalPaintKit{};
        int originalSeed{};
        int originalWearPermille{};
        bool tracked{false};
    };

    Entry entries[kMaxTracked]{};
};
