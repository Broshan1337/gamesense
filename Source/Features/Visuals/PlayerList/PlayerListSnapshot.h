#pragma once

#include <cstdint>

#include <Utils/SpinLock.h>

// FrameworkCS2 port (Source/Features/PlayerList) - snapshot plumbing. The game thread publishes
// a fixed-size row set every frame (ViewRenderHook_onRenderStart); the present thread draws it
// as an ImGui table from Neverlose render(). Same publish/snapshot shape as overlay_layer.
namespace player_list
{

struct Row {
    char name[40]{};
    int health = 0;
    int maxHealth = 0;
    int money = 0;
    int ping = 0;
    int rank = 0;          // m_iCompetitiveRanking (raw; 11+rating = premier reading is rankType's job)
    int rankType = 0;      // m_iCompetitiveRankType (0xb = premier)
    int team = 0;          // 2 = T, 3 = CT
    int kills = 0;         // CCSPlayer_ActionTrackingServices::m_iKills (scoreboard K column)
    int teamDamage = 0;    // from TeamDamageTracker's per-slot records
    int observerMode = -1; // CPlayer_ObserverServices::m_iObserverMode; -1 = none (alive)
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
