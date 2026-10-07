#pragma once

#include <cstdint>

#include <Utils/SpinLock.h>




namespace player_list
{

struct Row {
    char name[40]{};
    int slot = -1;         
    int health = 0;
    int maxHealth = 0;
    int money = 0;
    int ping = 0;
    int rank = 0;          
    int rankType = 0;      
    int team = 0;          
    int kills = 0;         
    int teamDamage = 0;    
    int observerMode = -1; 
    bool alive = false;
    bool isLocalPlayer = false;
};

constexpr int kMaxRows = 12;

inline SpinLock lock;
inline Row rows[kMaxRows];
inline int rowCount = 0;

inline void publish(const Row* newRows, int count) noexcept
{
    if (count > kMaxRows)
        count = kMaxRows;
    const std::lock_guard guard{lock};
    for (int i = 0; i < count; ++i)
        rows[i] = newRows[i];
    rowCount = count;
}

struct Snapshot {
    Row rows[kMaxRows];
    int count = 0;
};

[[nodiscard]] inline Snapshot snapshot() noexcept
{
    const std::lock_guard guard{lock};
    Snapshot s;
    s.count = rowCount;
    for (int i = 0; i < rowCount; ++i)
        s.rows[i] = rows[i];
    return s;
}

}
