#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <Features/Game/TeamDamageConfigVariables.h>
#include <GameClient/ChatPrinter.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/PlayerSlotLookup.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/StringBuilder.h>

// Tracks how much damage each player has done to their OWN team, and prints a running total to
// chat. Purely a local observation feature: it reads events the client already receives and writes
// to our own chat display. Nothing is sent to the server and no other player can see any of it.
//
// Modelled on a known-working reference implementation of the same idea, minus everything outside
// the agreed scope (no party messages, no file persistence, no separate overlay) - in particular
// its party-broadcast path is deliberately NOT reproduced, because that would be real network
// traffic.
template <typename HookContext>
class TeamDamageTracker {
public:
    explicit TeamDamageTracker(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event)
            return;

        // Totals are per-match, so they reset on the same boundaries the round/match flow uses.
        // Without this, totals from a finished match would leak into the next one.
        if (game_events::is(event, "begin_new_match") || game_events::is(event, "warmup_end") || game_events::is(event, "cs_win_panel_match")) {
            reset();
            return;
        }

        if (!game_events::is(event, "player_hurt"))
            return;

        onPlayerHurt(event);
    }

    // Watermark consumption: total team damage recorded for one attacker slot (0 when out of
    // range or nothing recorded). Accumulation is independent of the chat-announcement toggle -
    // the toggle only silences the chat line, so the number here is always live.
    [[nodiscard]] static int totalTeamDamageForSlot(std::int64_t slot) noexcept
    {
        if (slot < 0 || slot >= kSlotCount)
            return 0;
        return records[slot].damage;
    }

private:
    // Sized from the shared lookup's limit so the two cannot drift apart.
    static constexpr int kSlotCount = static_cast<int>(PlayerSlotLookup<HookContext>::kMaxPlayerSlot) + 1;

    // Thresholds only affect the text ("137/300"); nothing is enforced or acted on. They exist so
    // the number has a familiar frame of reference, matching the reference implementation.
    static constexpr int kDamageThreshold = 300;
    static constexpr int kKillThreshold = 3;

    // Per-player message cooldown. Team damage arrives as a burst of individual player_hurt events
    // (one per bullet of a spray), and without this a single burst would print a line per bullet.
    static constexpr float kMessageCooldownSeconds = 0.7f;

    struct Record {
        int damage{};
        int kills{};
        int announcedKills{};
        float lastMessageTime{};
    };

    // Process-lifetime state rather than a member of the feature-states struct: this feature is an
    // experiment, and keeping its state self-contained means it can be deleted in one piece.
    // Constant-initialised and trivially destructible, so no __cxa_guard under -nostdlib.
    inline static Record records[kSlotCount]{};

    static void reset() noexcept
    {
        for (auto& record : records)
            record = Record{};
    }

    void onPlayerHurt(cs2::IGameEvent* event) const noexcept
    {
        auto&& lookup = hookContext.template make<PlayerSlotLookup>();

        const auto attackerSlot = game_events::entityForKey(event, "attacker");
        const auto victimSlot = game_events::entityForKey(event, "userid");

        if (!lookup.isValidSlot(attackerSlot) || !lookup.isValidSlot(victimSlot))
            return;

        // Self-damage is not team damage. The reference implementation excludes it explicitly and
        // so must we, or every fall/nade self-hit would count against the player.
        if (attackerSlot == victimSlot)
            return;

        auto&& attackerPawn = lookup.pawnBySlot(attackerSlot);
        auto&& victimPawn = lookup.pawnBySlot(victimSlot);
        if (!attackerPawn || !victimPawn)
            return;

        // The whole point of the feature: only count it when both are on the same side.
        if (attackerPawn.teamNumber() != victimPawn.teamNumber())
            return;

        const auto damage = game_events::intForKey(event, "dmg_health");
        if (damage <= 0)
            return;

        auto& record = records[attackerSlot];
        record.damage += static_cast<int>(damage);
        if (game_events::intForKey(event, "health", -1) == 0)
            ++record.kills;

        announce(attackerSlot, record);
    }

    void announce(std::int64_t attackerSlot, Record& record) const noexcept
    {
        // Only the CHAT LINE is gated by the toggle - accumulation always runs so the watermark
        // stat stays live.
        if (!GET_CONFIG_VAR(TeamDamageTrackerEnabled))
            return;

        const auto now = hookContext.globalVars().curtime();
        if (!now.hasValue())
            return;

        // A team kill always reports immediately - it is the significant event, and swallowing it
        // behind the anti-spam cooldown would hide the thing most worth seeing.
        const auto killJustHappened = record.kills != record.announcedKills;
        if (!killJustHappened && now.value() < record.lastMessageTime + kMessageCooldownSeconds)
            return;

        record.lastMessageTime = now.value();
        record.announcedKills = record.kills;

        StringBuilderStorage<192> storage;
        auto builder = storage.builder();
        builder.put(hookContext.template make<PlayerSlotLookup>().nameBySlot(attackerSlot), " has ", record.damage, "/", kDamageThreshold, " team damage");
        if (record.kills > 0)
            builder.put(" and ", record.kills, "/", kKillThreshold, " team kills");

        // builder.cstring(), NOT storage.builder().cstring() - storage.builder() hands back a
        // fresh builder positioned at the start of the buffer, so terminating through it would
        // write '\0' at index 0 and print an empty line.
        hookContext.template make<ChatPrinter>().print(builder.cstring());
    }

    HookContext& hookContext;
};
