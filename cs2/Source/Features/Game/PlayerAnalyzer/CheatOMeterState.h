#pragma once

#include <cstdint>
#include <cstring>
#include <mutex>

#include <Utils/SpinLock.h>













namespace cheat_ometer
{

constexpr int kMaxSlots = 64;

constexpr int kMaxRows = 10;
constexpr int kMaxNameLen = 40;



inline SpinLock selectionLock;
inline bool selected[kMaxSlots]{};
inline int selectionCount = 0;

inline void clearSelection() noexcept
{
    const std::lock_guard guard{selectionLock};
    for (int i = 0; i < kMaxSlots; ++i)
        selected[i] = false;
    selectionCount = 0;
}


inline bool setSelected(int slot, bool on) noexcept
{
    if (slot < 0 || slot >= kMaxSlots)
        return false;
    const std::lock_guard guard{selectionLock};
    if (selected[slot] == on)
        return true;
    if (on && selectionCount >= kMaxRows)
        return false;
    selected[slot] = on;
    selectionCount += on ? 1 : -1;
    return true;
}

inline bool isSelected(int slot) noexcept
{
    if (slot < 0 || slot >= kMaxSlots)
        return false;
    const std::lock_guard guard{selectionLock};
    return selected[slot];
}

inline int selectedCount() noexcept
{
    const std::lock_guard guard{selectionLock};
    return selectionCount;
}



struct Stats {
    bool seen = false;            
    bool sampled = false;         
    std::uint32_t controllerHandle = 0; 
    std::uint64_t steamId = 0;    
    char name[kMaxNameLen]{};
    float startTime = 0.0f;       
    float lastSampleTime = 0.0f;
    float lastYaw = 0.0f;
    float peakSpeed = 0.0f;       
    int snaps = 0;                
    int shots = 0;                
    int hits = 0;                 
    int headshots = 0;            
    int voiceStrikes = 0;         
    int score = 0;                
    float lastCalloutTime = 0.0f; 
    int lastCalloutScore = 0;     

    
    
    
    
    float yawDeltas[160]{};       
    int yawDeltaCount = 0;
    int yawDeltaHead = 0;
    float sensQuantum = 0.0f;     
    float sensFitError = 1.0f;    
    int sensSamples = 0;          
    int aimStrikes = 0;           
    float lastShotTime = -10.0f;  
};

inline Stats stats[kMaxSlots];

inline void resetStats(int slot) noexcept
{
    if (slot < 0 || slot >= kMaxSlots)
        return;
    stats[slot] = Stats{};
}

inline void resetAllStats() noexcept
{
    for (int i = 0; i < kMaxSlots; ++i)
        stats[i] = Stats{};
}





[[nodiscard]] inline const char* addVoiceStrike(std::uint64_t xuid) noexcept
{
    if (xuid == 0)
        return nullptr;
    for (int i = 0; i < kMaxSlots; ++i) {
        if (stats[i].steamId == xuid && stats[i].controllerHandle != 0) {
            ++stats[i].voiceStrikes;
            return stats[i].name;
        }
    }
    return nullptr;
}



struct Tag {
    bool active = false;
    int score = 0;
};

inline Tag tagForSlot(int slot) noexcept
{
    Tag tag;
    if (slot < 0 || slot >= kMaxSlots)
        return tag;
    if (!isSelected(slot))
        return tag;
    const Stats& s = stats[slot];
    tag.active = s.seen && s.sampled;
    tag.score = s.score;
    return tag;
}



struct HudRow {
    char name[kMaxNameLen]{};
    int slot = -1;
    int team = 0;
    int score = 0;
    int peakSpeed = 0;   
    int snapsPerMin = 0;
    int accPct = -1;     
    int hsPct = -1;      
    int voiceStrikes = 0;
    float sensitivity = -1.0f; 
    float sensFitError = 1.0f;
    int aimStrikes = 0;  
    int shots = 0;
    int elapsed = 0;     
};

inline SpinLock hudLock;
inline HudRow hudRows[kMaxRows];
inline int hudRowCount = 0;

inline void publishHud(const HudRow* rows, int count) noexcept
{
    if (count > kMaxRows)
        count = kMaxRows;
    const std::lock_guard guard{hudLock};
    if (rows) {
        for (int i = 0; i < count; ++i)
            hudRows[i] = rows[i];
    }
    hudRowCount = rows ? count : 0;
}

struct HudSnapshot {
    HudRow rows[kMaxRows];
    int count = 0;
};

[[nodiscard]] inline HudSnapshot hudSnapshot() noexcept
{
    const std::lock_guard guard{hudLock};
    HudSnapshot s;
    s.count = hudRowCount;
    for (int i = 0; i < hudRowCount; ++i)
        s.rows[i] = hudRows[i];
    return s;
}

}
