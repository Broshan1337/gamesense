#pragma once

#include <cstdint>
#include <cstring>
#include <mutex>

#include <Utils/SpinLock.h>

// CHEAT O METER shared state. Two ownership domains:
//
//  * selection - the menu (present thread) writes it, the analyzer (game thread) reads it.
//    SpinLock-protected like every other cross-thread snapshot in this project.
//  * per-slot analysis + the HUD snapshot - stats are ONLY touched on the game thread
//    (sampling in PlayerAnalyzer::run, events in onFireEventClientSide, tag queries from the
//    in-world panel loop, which is also the game thread). The HUD rows cross to the present
//    thread through the publish/snapshot spinlock pair, same shape as SpectatorSnapshot.
//
// Players are keyed by SLOT (entity index - 1, the same numbering game events carry), so the
// picker, the sampler and the ESP tag agree without pointer identity games. A controller-handle
// guard in the analyzer resets a slot's stats when a different player takes it over.
namespace cheat_ometer
{

constexpr int kMaxSlots = 64;
// panel/picker cap - the HUD lists at most this many scanned players
constexpr int kMaxRows = 10;
constexpr int kMaxNameLen = 40;

// ---- selection (menu <-> game thread) ---------------------------------------------------

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

// Returns false when the slot is out of range or the scan list is already full.
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

// ---- per-slot analysis (game thread only) ------------------------------------------------

struct Stats {
    bool seen = false;            // currently being sampled
    bool sampled = false;         // have a baseline eye-angle sample for delta computation
    std::uint32_t controllerHandle = 0; // guard: reset when another player takes the slot
    std::uint64_t steamId = 0;    // controller m_steamID - matches CSVCMsg_VoiceData.xuid
    char name[kMaxNameLen]{};
    float startTime = 0.0f;       // curtime when this scan window began
    float lastSampleTime = 0.0f;
    float lastYaw = 0.0f;
    float peakSpeed = 0.0f;       // deg/s, single-sample peak within this scan window
    int snaps = 0;                // single-frame yaw jumps >= the configured threshold
    int shots = 0;                // weapon_fire events while selected
    int hits = 0;                 // player_hurt events where this player was the attacker
    int headshots = 0;            // hits with hitgroup == 1
    int voiceStrikes = 0;         // weird-voice wire packets attributed via steamId match
    int score = 0;                // suspicion 0-100, recomputed every frame while sampled
    float lastCalloutTime = 0.0f; // chat callout cooldown anchor (curtime)
    int lastCalloutScore = 0;     // re-announce only when the score climbs by this delta

    // SENSITIVITY QUANTUM VECTOR (the Minecraft gcd trick, ported to CS2): a legit player's
    // per-update yaw delta = integer mouse counts x 0.022 x sensitivity, so all deltas sit on
    // multiples of one quantum. We fit the quantum from a rolling delta ring; deltas off the
    // grid = angle-writing cheats (anti-aim, camera aimbots). sensitivity = quantum / 0.022.
    float yawDeltas[160]{};       // ring of |dyaw| per network update (> 0.001 deg)
    int yawDeltaCount = 0;
    int yawDeltaHead = 0;
    float sensQuantum = 0.0f;     // fitted deg/count (0 = not enough data)
    float sensFitError = 1.0f;    // mean residual / quantum of the last fit (low = confident)
    int sensSamples = 0;          // deltas fitted so far
    int aimStrikes = 0;           // deltas off the quantum grid (rate-limited per fit round)
    float lastShotTime = -10.0f;  // weapon_fire anchor - view punch poisons deltas for ~0.6s
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

// Voice-tap attribution (game thread, called from the voice tap drain): a weird wire packet's
// xuid is a SteamID64 - match it against the analyzer's per-slot controller steam IDs and count
// a voice strike. Returns the matched player's name, or nullptr when nobody matches (xuid=0 /
// non-player senders) - the caller still logs the packet.
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

// ESP tag query (game thread): a slot shows a tag while it is selected AND has been sampled
// at least once (score may legitimately be 0 - "scanned, nothing suspicious yet").
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

// ---- HUD snapshot (game thread publishes, present thread draws) ---------------------------

struct HudRow {
    char name[kMaxNameLen]{};
    int slot = -1;
    int team = 0;
    int score = 0;
    int peakSpeed = 0;   // deg/s
    int snapsPerMin = 0;
    int accPct = -1;     // -1 = not enough shots yet
    int hsPct = -1;      // -1 = not enough hits yet
    int voiceStrikes = 0;// weird-voice wire packets attributed to this player
    float sensitivity = -1.0f; // fitted from the rotation quantum (-1 = not enough data)
    float sensFitError = 1.0f;
    int aimStrikes = 0;  // rotations off the sensitivity grid
    int shots = 0;
    int elapsed = 0;     // seconds in this scan window
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
