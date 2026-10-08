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

        
        
        if (game_events::is(event, "begin_new_match") || game_events::is(event, "warmup_end") || game_events::is(event, "cs_win_panel_match")) {
            reset();
            return;
        }

        if (!game_events::is(event, "player_hurt"))
            return;

        onPlayerHurt(event);
    }

    
    
    
    [[nodiscard]] static int totalTeamDamageForSlot(std::int64_t slot) noexcept
    {
        if (slot < 0 || slot >= kSlotCount)
            return 0;
        return records[slot].damage;
    }

private:
    
    static constexpr int kSlotCount = static_cast<int>(PlayerSlotLookup<HookContext>::kMaxPlayerSlot) + 1;

    
    
    static constexpr int kDamageThreshold = 300;
    static constexpr int kKillThreshold = 3;

    
    
    static constexpr float kMessageCooldownSeconds = 0.7f;

    struct Record {
        int damage{};
        int kills{};
        int announcedKills{};
        float lastMessageTime{};
    };

    
    
    
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

        
        
        if (attackerSlot == victimSlot)
            return;

        auto&& attackerPawn = lookup.pawnBySlot(attackerSlot);
        auto&& victimPawn = lookup.pawnBySlot(victimSlot);
        if (!attackerPawn || !victimPawn)
            return;

        
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
        
        
        if (!GET_CONFIG_VAR(TeamDamageTrackerEnabled))
            return;

        const auto now = hookContext.globalVars().curtime();
        if (!now.hasValue())
            return;

        
        
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

        
        
        
        hookContext.template make<ChatPrinter>().print(builder.cstring());
    }

    HookContext& hookContext;
};
