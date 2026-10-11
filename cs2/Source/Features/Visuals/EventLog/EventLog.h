#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <CS2/Classes/IGameEventManager2.h>
#include <Features/Visuals/EventLog/EventLogConfigVariables.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <GameClient/PlayerSlotLookup.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/OverlayLayer.h>
#include <Utils/SpinLock.h>
#include <Utils/Optional.h>

// Native event log (batch I, vesta event_log.cpp shape): game events append
// timestamped lines to a fixed ring; the render side draws the last ~6 as a
// fading panel (BindsList chrome). Hits (we -> player_hurt), misses (our shot
// landed nothing - resolver feed), bomb (plant/defuse/explode), round start/
// end, connect/disconnect.
namespace event_log
{

inline constexpr int kMaxLines = 16;
inline constexpr int kLineChars = 96;

struct Entry {
    char text[kLineChars];
    float ageSeconds;   // set by the draw side each frame
    std::uint64_t rgba;
    double timestamp;   // CLOCK_MONOTONIC seconds at append
};

inline Entry entries[kMaxLines];
inline int entryCount = 0;
inline int entryNext = 0;
inline SpinLock lock;

[[nodiscard]] inline double nowSeconds() noexcept
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1.0e-9;
}

inline void append(std::uint64_t rgba, const char* format, ...) noexcept
{
    char text[kLineChars];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (written <= 0)
        return;

    Entry& entry = entries[entryNext];
    for (int i = 0; i < kLineChars; ++i)
        entry.text[i] = text[i] != '\0' ? text[i] : '\0';
    entry.text[kLineChars - 1] = '\0';
    entry.timestamp = nowSeconds();
    entry.rgba = rgba;
    entry.ageSeconds = 0.0f;
    entryNext = (entryNext + 1) % kMaxLines;
    if (entryCount < kMaxLines)
        ++entryCount;
}

// Snapshot for the render side (oldest first).
struct Snapshot {
    Entry entries[kMaxLines];
    int count = 0;
    int firstIndex = 0;
    double now = 0.0;
};

[[nodiscard]] inline Snapshot snapshot() noexcept
{
    const std::lock_guard guard{lock};
    Snapshot out;
    out.count = entryCount;
    out.firstIndex = entryNext >= entryCount ? entryNext - entryCount : entryNext + kMaxLines - entryCount;
    if (out.firstIndex < 0)
        out.firstIndex = 0;
    for (int i = 0; i < entryCount; ++i)
        out.entries[i] = entries[(out.firstIndex + i) % kMaxLines];
    out.now = nowSeconds();
    return out;
}

template <typename HookContext>
void onFireEventClientSide(HookContext& hookContext, cs2::IGameEvent* event) noexcept
{
    if (!GET_CONFIG_VAR(event_log_vars::Enabled) || !event)
        return;
    if (game_events::is(event, "player_hurt") && GET_CONFIG_VAR(event_log_vars::LogHits)) {
        if (!game_events::localPlayerIsAttacker(hookContext, event))
            return;
        const auto victimSlot = game_events::entityForKey(event, "userid");
        auto&& lookup = hookContext.template make<PlayerSlotLookup>();
        if (!lookup.isValidSlot(victimSlot))
            return;
        auto&& victimPawn = lookup.pawnBySlot(victimSlot);
        const auto health = victimPawn ? victimPawn.health() : Optional<int>{};
        const int damage = game_events::intForKey(event, "dmg_health", 0);
        const bool lethal = !health.hasValue() || health.value() <= 0;
        append(lethal ? 0x90FF6064u : 0x90FFFFFFu, lethal ? "kill (+%d)" : "hit %d", damage);
        return;
    }
    if (game_events::is(event, "bomb_planted") && GET_CONFIG_VAR(event_log_vars::LogBomb)) {
        append(0x90FF8C64u, "bomb planted");
        return;
    }
    if (game_events::is(event, "bomb_defused") && GET_CONFIG_VAR(event_log_vars::LogBomb)) {
        append(0x9064C8FFu, "bomb defused");
        return;
    }
    if (game_events::is(event, "bomb_exploded") && GET_CONFIG_VAR(event_log_vars::LogBomb)) {
        append(0x90FF6464u, "bomb exploded");
        return;
    }
    if (game_events::is(event, "round_start") && GET_CONFIG_VAR(event_log_vars::LogRounds)) {
        append(0x90E6E6ECu, "round start");
        return;
    }
    if (game_events::is(event, "round_end") && GET_CONFIG_VAR(event_log_vars::LogRounds)) {
        append(0x90E6E6ECu, "round end");
        return;
    }
    if (GET_CONFIG_VAR(event_log_vars::LogConnects)) {
        if (game_events::is(event, "player_connect")) {
            append(0x90B8C8D8u, "connect %s", game_events::stringForKey(event, "name"));
            return;
        }
        if (game_events::is(event, "player_disconnect")) {
            append(0x90B8C8D8u, "disconnect %s", game_events::stringForKey(event, "name"));
            return;
        }
    }
}

inline void onUnload() noexcept
{
    const std::lock_guard guard{lock};
    entryCount = 0;
    entryNext = 0;
}

}