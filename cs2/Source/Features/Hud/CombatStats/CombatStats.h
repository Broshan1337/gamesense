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
            return;   

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

    
    
    [[nodiscard]] auto localTeamNumber() const noexcept
    {
        return hookContext.activeLocalPlayerPawn().teamNumber();
    }

    void pushFeedEntry(char kind, const char* name, int damage, bool headshot) const noexcept
    {
        
        
        if (kind == 'm') {
            if (missStreak > 0) {
                feedEntries[0].missCount = ++missStreak;
                return;
            }
            ++missStreak;
        } else {
            missStreak = 0;
        }

        
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

    
    
    inline static std::uint32_t shotsFired{0};
    inline static std::uint32_t hitsLanded{0};
    inline static int missStreak{0};
    inline static combat_stats_hud::FeedEntry feedEntries[combat_stats_hud::kFeedLines]{};

    HookContext& hookContext;
};
