#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

#include <CS2/Classes/IGameEventManager2.h>
#include <Features/Hud/CombatStats/CombatStatsHudState.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <GameClient/PlayerSlotLookup.h>
#include <HookContext/HookContextMacros.h>

// Hits/misses counting + a small event feed. Purely local bookkeeping over events the client
// already receives; nothing is sent anywhere.
//
//   shots = weapon_fire events by the local player - one event per TRIGGER PULL, which matches
//           the intended semantics exactly: every shot counts, wherever it lands (bullet_impact
//           was tried first and is wrong for this: it only fires when the bullet actually hits a
//           surface, so skybox/near-miss shots were never counted as misses)
//   hits  = player_hurt events by the local player against an ENEMY (friendly damage and world
//           damage excluded - those are not "hits" in any useful sense)
//   misses = shots - hits (saturating; converges once both events of a shot arrive)
//
// Everything above is game-thread only; the HUD windows in the ImGui overlay draw a published
// snapshot (CombatStatsHudState.h - the cheat o meter's publish/snapshot pattern).
template <typename HookContext>
class CombatStats {
public:
    explicit CombatStats(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event)
            return;

        // Same reset boundaries TeamDamageTracker uses - session stats are per-match, or a
        // "HITS 400" from last night's session would still be on screen tomorrow.
        if (game_events::is(event, "begin_new_match") || game_events::is(event, "warmup_end") || game_events::is(event, "cs_win_panel_match")) {
            shotsFired = 0;
            hitsLanded = 0;
            missStreak = 0;
            for (auto& entry : feedEntries)
                entry = combat_stats_hud::FeedEntry{};
            return;
        }

        if (game_events::is(event, "weapon_fire")) {
            if (game_events::localPlayerIsSlot(hookContext, game_events::entityForKey(event, "userid")))
                ++shotsFired;
            return;
        }

        if (game_events::is(event, "player_hurt")) {
            onPlayerHurt(event);
            return;
        }

        if (game_events::is(event, "player_death"))
            onPlayerDeath(event);
    }

    void run() const noexcept
    {
        // The Panorama boxes only existed while the in-game HUD did; carry the same gate over
        // so the ImGui windows never float over the main menu. While not live, stop publishing
        // (the last snapshot would otherwise sit on screen forever).
        if (!hookContext.activeLocalPlayerPawn()) {
            combat_stats_hud::hudLive.store(false, std::memory_order_relaxed);
            return;
        }
        combat_stats_hud::hudLive.store(true, std::memory_order_relaxed);
        combat_stats_hud::publish(shotsFired, hitsLanded, feedEntries,
            static_cast<int>(sizeof(feedEntries) / sizeof(feedEntries[0])));
    }

private:
    void onPlayerHurt(cs2::IGameEvent* event) const noexcept
    {
        if (!game_events::localPlayerIsAttacker(hookContext, event))
            return;

        const auto victimSlot = game_events::entityForKey(event, "userid");
        auto&& lookup = hookContext.template make<PlayerSlotLookup>();
        if (!lookup.isValidSlot(victimSlot))
            return;

        auto&& victimPawn = lookup.pawnBySlot(victimSlot);
        if (!victimPawn || victimPawn.teamNumber() == localTeamNumber())
            return;   // friendly fire / self-damage is not a "hit" in any useful sense

        ++hitsLanded;

        pushFeedEntry('h', lookup.nameBySlot(victimSlot),
            game_events::intForKey(event, "dmg_health"),
            game_events::intForKey(event, "hitgroup") == 1);
    }

    void onPlayerDeath(cs2::IGameEvent* event) const noexcept
    {
        if (!game_events::localPlayerIsAttacker(hookContext, event))
            return;

        const auto victimSlot = game_events::entityForKey(event, "userid");
        auto&& lookup = hookContext.template make<PlayerSlotLookup>();
        if (!lookup.isValidSlot(victimSlot))
            return;

        pushFeedEntry('k', lookup.nameBySlot(victimSlot), 0, false);
    }

    // The victim's team is only meaningful compared against OURS - the local pawn's team number
    // read is the same one the triggerbot/aimbot targeting uses.
    [[nodiscard]] auto localTeamNumber() const noexcept
    {
        return hookContext.activeLocalPlayerPawn().teamNumber();
    }

    void pushFeedEntry(char kind, const char* name, int damage, bool headshot) const noexcept
    {
        // Coalesce consecutive misses: "missed" -> "missed x2" -> ... in place (the counter
        // ticks up without re-running the entrance animation), instead of a pill per bullet.
        if (kind == 'm') {
            if (missStreak > 0) {
                feedEntries[0].missCount = ++missStreak;
                return;
            }
            ++missStreak;
        } else {
            missStreak = 0;
        }

        // Newest first: shift the ring down, drop the oldest past the line count.
        constexpr std::size_t kFeedLines = sizeof(feedEntries) / sizeof(feedEntries[0]);
        for (std::size_t i = kFeedLines - 1; i > 0; --i)
            feedEntries[i] = feedEntries[i - 1];
        combat_stats_hud::FeedEntry& slot = feedEntries[0];
        slot.kind = kind;
        slot.damage = damage;
        slot.headshot = headshot;
        slot.missCount = 0;
        std::strncpy(slot.name, name ? name : "", sizeof(slot.name) - 1);
        slot.name[sizeof(slot.name) - 1] = '\0';
        slot.spawnTime = monotonicSeconds();
    }

    [[nodiscard]] static double monotonicSeconds() noexcept
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1.0e-9;
    }

    // Process-lifetime counters + feed ring (feature objects are rebuilt per call). Game thread
    // only - the present thread reads the published snapshot.
    inline static std::uint32_t shotsFired{0};
    inline static std::uint32_t hitsLanded{0};
    inline static int missStreak{0};
    inline static combat_stats_hud::FeedEntry feedEntries[combat_stats_hud::kFeedLines]{};

    HookContext& hookContext;
};
