#pragma once

#include <algorithm>

#include "ConfigVariableTypes.h"
#include "ConfigOverrideState.h"
#include <HookContext/HookContextMacros.h>

template <typename HookContext>
class ConfigSchema {
public:
    explicit ConfigSchema(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] decltype(auto) performConversion(auto&& configConversion)
    {
        configConversion.beginRoot();
        combatObject(configConversion);
        skinChangerObject(configConversion);
        hudObject(configConversion);
        visualsObject(configConversion);
        soundObject(configConversion);
        gameObject(configConversion);
        menuObject(configConversion);
        discordRpcObject(configConversion);
        inventoryChangerObject(configConversion);
        chatToolsObject(configConversion);
        combatStatsObject(configConversion);
        statusPanelObject(configConversion);
        
        playerAnalyzerObject(configConversion);
        fakePremierObject(configConversion);
        fakeCommendsObject(configConversion);
        glitchGeneratorObject(configConversion);
        scoreboardEquipmentObject(configConversion);
        return configConversion.endRoot();
    }

private:
    
    void fakePremierObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"FakePremier");
        configConversion.boolean(u8"Enabled", loadVariable<FakePremierEnabled>(), saveVariable<FakePremierEnabled>());
        configConversion.uint(u8"Score", loadVariable<FakePremierScore>(), saveVariable<FakePremierScore>());
        configConversion.endObject();
    }

    void fakeCommendsObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"FakeCommends");
        configConversion.boolean(u8"Enabled", loadVariable<FakeCommendsEnabled>(), saveVariable<FakeCommendsEnabled>());
        
        configConversion.uint(u8"Friendly", loadVariable<FakeCommendsFriendly>(), saveVariable<FakeCommendsFriendly>());
        configConversion.uint(u8"Teaching", loadVariable<FakeCommendsTeaching>(), saveVariable<FakeCommendsTeaching>());
        configConversion.uint(u8"Leader", loadVariable<FakeCommendsLeader>(), saveVariable<FakeCommendsLeader>());
        configConversion.endObject();
    }

    void glitchGeneratorObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"GlitchGenerator");
        configConversion.uint(u8"Style", loadVariable<glitch_gen_vars::Style>(), saveVariable<glitch_gen_vars::Style>());
        configConversion.uint(u8"Intensity", loadVariable<glitch_gen_vars::Intensity>(), saveVariable<glitch_gen_vars::Intensity>());
        
        configConversion.uint(u8"Preset", loadVariable<glitch_gen_vars::Preset>(), saveVariable<glitch_gen_vars::Preset>());
        configConversion.endObject();
    }

    void scoreboardEquipmentObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"ScoreboardEquipment");
        configConversion.boolean(u8"Enabled", loadVariable<scoreboard_equipment_vars::Enabled>(), saveVariable<scoreboard_equipment_vars::Enabled>());
        configConversion.endObject();
    }

    
    void discordRpcObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"DiscordRpc");
        configConversion.boolean(u8"Enabled", loadVariable<discord_rpc_vars::Enabled>(), saveVariable<discord_rpc_vars::Enabled>());
        configConversion.endObject();
    }

    
    void inventoryChangerObject(auto&& configConversion)
    {
        
        
        
        configConversion.beginObject(u8"InventoryChanger");
        configConversion.boolean(u8"Enabled", [](bool) {}, [] { return false; });
        configConversion.uint(u8"CaseDefIndex", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"KeyDefIndex", [](std::uint64_t) {}, [] { return 0; });
        
        
        configConversion.uint(u8"AgentDef", loadVariable<agent_changer_vars::AgentDef>(), saveVariable<agent_changer_vars::AgentDef>());
        configConversion.endObject();
    }

    
    void chatToolsObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"ChatTools");
        configConversion.boolean(u8"SpamEnabled", loadVariable<chat_vars::SpamEnabled>(), saveVariable<chat_vars::SpamEnabled>());
        configConversion.uint(u8"SpamCount", loadVariable<chat_vars::SpamCount>(), saveVariable<chat_vars::SpamCount>());
        configConversion.uint(u8"SpamInterval", loadVariable<chat_vars::SpamInterval>(), saveVariable<chat_vars::SpamInterval>());
        configConversion.boolean(u8"WheelEnabled", loadVariable<chat_vars::WheelEnabled>(), saveVariable<chat_vars::WheelEnabled>());
        configConversion.uint(u8"RadioPhrase", loadVariable<chat_vars::RadioPhrase>(), saveVariable<chat_vars::RadioPhrase>());
        configConversion.uint(u8"WheelInterval", loadVariable<chat_vars::WheelInterval>(), saveVariable<chat_vars::WheelInterval>());
        configConversion.boolean(u8"PingSpamEnabled", loadVariable<chat_vars::PingSpamEnabled>(), saveVariable<chat_vars::PingSpamEnabled>());
        configConversion.uint(u8"PingInterval", loadVariable<chat_vars::PingInterval>(), saveVariable<chat_vars::PingInterval>());
        configConversion.boolean(u8"HudColorCycle", loadVariable<chat_vars::HudColorCycle>(), saveVariable<chat_vars::HudColorCycle>());
        configConversion.uint(u8"HudColorCycleSpeed", loadVariable<chat_vars::HudColorCycleSpeed>(), saveVariable<chat_vars::HudColorCycleSpeed>());
        configConversion.boolean(u8"NameCycleEnabled", loadVariable<chat_vars::NameCycleEnabled>(), saveVariable<chat_vars::NameCycleEnabled>());
        configConversion.uint(u8"NameCycleInterval", loadVariable<chat_vars::NameCycleInterval>(), saveVariable<chat_vars::NameCycleInterval>());
        configConversion.uint(u8"TheaterDelay", loadVariable<chat_vars::TheaterDelay>(), saveVariable<chat_vars::TheaterDelay>());
        configConversion.boolean(u8"IntelEnabled", loadVariable<chat_vars::IntelEnabled>(), saveVariable<chat_vars::IntelEnabled>());
        configConversion.uint(u8"IntelBind", loadVariable<chat_vars::IntelBind>(), saveVariable<chat_vars::IntelBind>());
        configConversion.boolean(u8"StreakRadioEnabled", loadVariable<chat_vars::StreakRadioEnabled>(), saveVariable<chat_vars::StreakRadioEnabled>());
        configConversion.boolean(u8"LiveBadgeEnabled", loadVariable<chat_vars::LiveBadgeEnabled>(), saveVariable<chat_vars::LiveBadgeEnabled>());
        configConversion.uint(u8"KickReason", loadVariable<chat_vars::KickReason>(), saveVariable<chat_vars::KickReason>());
        configConversion.uint(u8"KickKey", loadVariable<chat_vars::KickKey>(), saveVariable<chat_vars::KickKey>());
        
        configConversion.boolean(u8"NameForceReconnect", loadVariable<chat_vars::NameForceReconnect>(), saveVariable<chat_vars::NameForceReconnect>());
        configConversion.boolean(u8"ClanTagEnabled", loadVariable<chat_vars::ClanTagEnabled>(), saveVariable<chat_vars::ClanTagEnabled>());
        
        configConversion.boolean(u8"ClanTagAnimateEnabled", loadVariable<chat_vars::ClanTagAnimateEnabled>(), saveVariable<chat_vars::ClanTagAnimateEnabled>());
        configConversion.uint(u8"ClanTagAnimateMode", loadVariable<chat_vars::ClanTagAnimateMode>(), saveVariable<chat_vars::ClanTagAnimateMode>());
        configConversion.uint(u8"ClanTagAnimateSpeed", loadVariable<chat_vars::ClanTagAnimateSpeed>(), saveVariable<chat_vars::ClanTagAnimateSpeed>());
        configConversion.endObject();
    }

    
    void combatStatsObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"CombatStats");
        configConversion.uint(u8"FeedLifetime", loadVariable<combat_stats_vars::FeedLifetime>(), saveVariable<combat_stats_vars::FeedLifetime>());
        configConversion.uint(u8"FeedOffsetX", loadVariable<combat_stats_vars::FeedOffsetX>(), saveVariable<combat_stats_vars::FeedOffsetX>());
        configConversion.uint(u8"FeedOffsetY", loadVariable<combat_stats_vars::FeedOffsetY>(), saveVariable<combat_stats_vars::FeedOffsetY>());
        configConversion.floating(u8"CountersOffsetX", loadVariable<combat_stats_vars::CountersOffsetX>(), saveVariable<combat_stats_vars::CountersOffsetX>());
        configConversion.floating(u8"CountersOffsetY", loadVariable<combat_stats_vars::CountersOffsetY>(), saveVariable<combat_stats_vars::CountersOffsetY>());
        configConversion.endObject();
    }

    
    void statusPanelObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"StatusPanel");
        configConversion.floating(u8"OffsetX", loadVariable<status_panel_vars::OffsetX>(), saveVariable<status_panel_vars::OffsetX>());
        configConversion.floating(u8"OffsetY", loadVariable<status_panel_vars::OffsetY>(), saveVariable<status_panel_vars::OffsetY>());
        configConversion.endObject();
    }

    
    
    void playerAnalyzerObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"PlayerAnalyzer");
        configConversion.boolean(u8"Enabled", loadVariable<analyzer_vars::Enabled>(), saveVariable<analyzer_vars::Enabled>());
        configConversion.boolean(u8"EspTag", loadVariable<analyzer_vars::EspTag>(), saveVariable<analyzer_vars::EspTag>());
        configConversion.uint(u8"SnapThreshold", loadVariable<analyzer_vars::SnapThreshold>(), saveVariable<analyzer_vars::SnapThreshold>());
        
        configConversion.boolean(u8"VoiceProbe", loadVariable<analyzer_vars::VoiceProbe>(), saveVariable<analyzer_vars::VoiceProbe>());
        configConversion.boolean(u8"VoiceLog", loadVariable<analyzer_vars::VoiceLog>(), saveVariable<analyzer_vars::VoiceLog>());
        configConversion.boolean(u8"Callout", loadVariable<analyzer_vars::Callout>(), saveVariable<analyzer_vars::Callout>());
        configConversion.boolean(u8"CalloutTeamChat", loadVariable<analyzer_vars::CalloutTeamChat>(), saveVariable<analyzer_vars::CalloutTeamChat>());
        configConversion.uint(u8"CalloutThreshold", loadVariable<analyzer_vars::CalloutThreshold>(), saveVariable<analyzer_vars::CalloutThreshold>());
        configConversion.endObject();
    }
    void combatObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"Combat");

        configConversion.beginObject(u8"NoScopeInaccuracyVis");
        configConversion.boolean(u8"Enabled", loadVariable<no_scope_inaccuracy_vis_vars::Enabled>(), saveVariable<no_scope_inaccuracy_vis_vars::Enabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"Triggerbot");
        configConversion.boolean(u8"Enabled", loadVariable<triggerbot_vars::Enabled>(), saveVariable<triggerbot_vars::Enabled>());
        configConversion.uint(u8"DelayMillisecondsMin", loadVariable<triggerbot_vars::DelayMilliseconds>(), saveVariable<triggerbot_vars::DelayMilliseconds>());
        configConversion.uint(u8"DelayMillisecondsMax", loadVariable<triggerbot_vars::DelayMillisecondsMax>(), saveVariable<triggerbot_vars::DelayMillisecondsMax>());
        configConversion.boolean(u8"AccuracyCheck", loadVariable<triggerbot_vars::AccuracyCheck>(), saveVariable<triggerbot_vars::AccuracyCheck>());
        configConversion.uint(u8"AccuracyRadius", loadVariable<triggerbot_vars::AccuracyRadius>(), saveVariable<triggerbot_vars::AccuracyRadius>());
        configConversion.boolean(u8"HeadOnly", loadVariable<triggerbot_vars::HeadOnly>(), saveVariable<triggerbot_vars::HeadOnly>());
        configConversion.uint(u8"Hitchance", loadVariable<triggerbot_vars::Hitchance>(), saveVariable<triggerbot_vars::Hitchance>());
        configConversion.boolean(u8"MaxAccuracyOnly", loadVariable<triggerbot_vars::MaxAccuracyOnly>(), saveVariable<triggerbot_vars::MaxAccuracyOnly>());
        configConversion.boolean(u8"WallCheck", loadVariable<triggerbot_vars::WallCheck>(), saveVariable<triggerbot_vars::WallCheck>());
        configConversion.boolean(u8"Autowall", loadVariable<triggerbot_vars::Autowall>(), saveVariable<triggerbot_vars::Autowall>());
        configConversion.uint(u8"AutowallMaxThickness", loadVariable<triggerbot_vars::AutowallMaxThickness>(), saveVariable<triggerbot_vars::AutowallMaxThickness>());
        configConversion.boolean(u8"SpreadCompensation", loadVariable<triggerbot_vars::SpreadCompensation>(), saveVariable<triggerbot_vars::SpreadCompensation>());
        configConversion.uint(u8"HoldKey", loadVariable<triggerbot_vars::HoldKey>(), saveVariable<triggerbot_vars::HoldKey>());
        configConversion.boolean(u8"SeededFire", loadVariable<triggerbot_vars::SeededFire>(), saveVariable<triggerbot_vars::SeededFire>());
        configConversion.boolean(u8"ThroughWalls", loadVariable<triggerbot_vars::ThroughWalls>(), saveVariable<triggerbot_vars::ThroughWalls>());
        configConversion.uint(u8"MinDamage", loadVariable<triggerbot_vars::MinDamage>(), saveVariable<triggerbot_vars::MinDamage>());
        configConversion.boolean(u8"Backtrack", loadVariable<triggerbot_vars::Backtrack>(), saveVariable<triggerbot_vars::Backtrack>());
        configConversion.uint(u8"BacktrackTicks", loadVariable<triggerbot_vars::BacktrackTicks>(), saveVariable<triggerbot_vars::BacktrackTicks>());
        
        
        
        configConversion.boolean(u8"DebugLog", [](bool) {}, [] { return false; });
        configConversion.endObject();

        configConversion.beginObject(u8"Aimbot");
        configConversion.boolean(u8"Enabled", loadVariable<aimbot_vars::Enabled>(), saveVariable<aimbot_vars::Enabled>());
        configConversion.uint(u8"TargetSelection", loadVariable<aimbot_vars::TargetSelection>(), saveVariable<aimbot_vars::TargetSelection>());
        configConversion.boolean(u8"TargetLock", loadVariable<aimbot_vars::TargetLock>(), saveVariable<aimbot_vars::TargetLock>());
        
        
        
        
        configConversion.uint(u8"Fov", [](std::uint64_t) {}, [] { return 0; });
        configConversion.boolean(u8"DrawFov", [](bool) {}, [] { return false; });
        configConversion.uint(u8"FovCircleHue", [](std::uint64_t) {}, [] { return 0; });
        configConversion.boolean(u8"HitHead", loadVariable<aimbot_vars::HitHead>(), saveVariable<aimbot_vars::HitHead>());
        configConversion.boolean(u8"HitChest", loadVariable<aimbot_vars::HitChest>(), saveVariable<aimbot_vars::HitChest>());
        configConversion.boolean(u8"HitStomach", loadVariable<aimbot_vars::HitStomach>(), saveVariable<aimbot_vars::HitStomach>());
        configConversion.boolean(u8"HitArms", loadVariable<aimbot_vars::HitArms>(), saveVariable<aimbot_vars::HitArms>());
        configConversion.boolean(u8"HitLegs", loadVariable<aimbot_vars::HitLegs>(), saveVariable<aimbot_vars::HitLegs>());
        configConversion.boolean(u8"SpreadCompensation", loadVariable<aimbot_vars::SpreadCompensation>(), saveVariable<aimbot_vars::SpreadCompensation>());
        configConversion.boolean(u8"RecoilCompensation", loadVariable<aimbot_vars::RecoilCompensation>(), saveVariable<aimbot_vars::RecoilCompensation>());
        configConversion.boolean(u8"ForceShot", loadVariable<aimbot_vars::ForceShot>(), saveVariable<aimbot_vars::ForceShot>());
        configConversion.boolean(u8"ForceShotAir", loadVariable<aimbot_vars::ForceShotAir>(), saveVariable<aimbot_vars::ForceShotAir>());
        configConversion.boolean(u8"Extrapolate", loadVariable<aimbot_vars::Extrapolate>(), saveVariable<aimbot_vars::Extrapolate>());
        configConversion.uint(u8"ExtrapolateTicks", loadVariable<aimbot_vars::ExtrapolateTicks>(), saveVariable<aimbot_vars::ExtrapolateTicks>());
        configConversion.boolean(u8"BodyAim", loadVariable<aimbot_vars::BodyAim>(), saveVariable<aimbot_vars::BodyAim>());
        configConversion.uint(u8"Hitchance", loadVariable<aimbot_vars::Hitchance>(), saveVariable<aimbot_vars::Hitchance>());
        configConversion.uint(u8"MinDamage", loadVariable<aimbot_vars::MinDamage>(), saveVariable<aimbot_vars::MinDamage>());
        configConversion.boolean(u8"Multipoint", loadVariable<aimbot_vars::Multipoint>(), saveVariable<aimbot_vars::Multipoint>());
        configConversion.boolean(u8"Backtrack", loadVariable<aimbot_vars::Backtrack>(), saveVariable<aimbot_vars::Backtrack>());
        configConversion.uint(u8"BacktrackTicks", loadVariable<aimbot_vars::BacktrackTicks>(), saveVariable<aimbot_vars::BacktrackTicks>());
        
        
        configConversion.boolean(u8"SpreadCircleFov", loadVariable<aimbot_vars::SpreadCircleFov>(), saveVariable<aimbot_vars::SpreadCircleFov>());
        
        
        
        configConversion.boolean(u8"SeedCorrectionMode", [](bool) {}, [] { return false; });
        
        configConversion.uint(u8"PointScale", loadVariable<aimbot_vars::PointScale>(), saveVariable<aimbot_vars::PointScale>());
        configConversion.boolean(u8"DynamicPointscale", loadVariable<aimbot_vars::DynamicPointscale>(), saveVariable<aimbot_vars::DynamicPointscale>());
        configConversion.boolean(u8"WallCheck", loadVariable<aimbot_vars::WallCheck>(), saveVariable<aimbot_vars::WallCheck>());
        configConversion.boolean(u8"Autowall", loadVariable<aimbot_vars::Autowall>(), saveVariable<aimbot_vars::Autowall>());
        configConversion.boolean(u8"AutoStop", loadVariable<aimbot_vars::AutoStop>(), saveVariable<aimbot_vars::AutoStop>());
        configConversion.boolean(u8"SpreadGate", loadVariable<aimbot_vars::SpreadGate>(), saveVariable<aimbot_vars::SpreadGate>());
        configConversion.boolean(u8"SeedFallback", loadVariable<aimbot_vars::SeedFallback>(), saveVariable<aimbot_vars::SeedFallback>());
        configConversion.boolean(u8"ForceShotWait", loadVariable<aimbot_vars::ForceShotWait>(), saveVariable<aimbot_vars::ForceShotWait>());
        configConversion.uint(u8"ForceShotWaitTicks", loadVariable<aimbot_vars::ForceShotWaitTicks>(), saveVariable<aimbot_vars::ForceShotWaitTicks>());
        configConversion.endObject();

        configConversion.beginObject(u8"LegitAimbot");
        configConversion.boolean(u8"VisibleAim", loadVariable<legit_aimbot_vars::VisibleAim>(), saveVariable<legit_aimbot_vars::VisibleAim>());
        configConversion.uint(u8"Mode", loadVariable<legit_aimbot_vars::Mode>(), saveVariable<legit_aimbot_vars::Mode>());
        configConversion.boolean(u8"AlwaysOn", loadVariable<legit_aimbot_vars::AlwaysOn>(), saveVariable<legit_aimbot_vars::AlwaysOn>());
        configConversion.boolean(u8"OnlyWhileFiring", loadVariable<legit_aimbot_vars::OnlyWhileFiring>(), saveVariable<legit_aimbot_vars::OnlyWhileFiring>());
        configConversion.boolean(u8"RequireMouseMovement", loadVariable<legit_aimbot_vars::RequireMouseMovement>(), saveVariable<legit_aimbot_vars::RequireMouseMovement>());
        configConversion.boolean(u8"IgnoreFlash", loadVariable<legit_aimbot_vars::IgnoreFlash>(), saveVariable<legit_aimbot_vars::IgnoreFlash>());
        configConversion.boolean(u8"RecoilCompensation", loadVariable<legit_aimbot_vars::RecoilCompensation>(), saveVariable<legit_aimbot_vars::RecoilCompensation>());
        configConversion.uint(u8"Strength", loadVariable<legit_aimbot_vars::Strength>(), saveVariable<legit_aimbot_vars::Strength>());
        configConversion.floating(u8"Deadzone", loadVariable<legit_aimbot_vars::Deadzone>(), saveVariable<legit_aimbot_vars::Deadzone>());
        configConversion.floating(u8"MaxSpeed", loadVariable<legit_aimbot_vars::MaxSpeed>(), saveVariable<legit_aimbot_vars::MaxSpeed>());
        configConversion.uint(u8"ReactionMs", loadVariable<legit_aimbot_vars::ReactionMs>(), saveVariable<legit_aimbot_vars::ReactionMs>());
        configConversion.uint(u8"SwitchDelayMs", loadVariable<legit_aimbot_vars::SwitchDelayMs>(), saveVariable<legit_aimbot_vars::SwitchDelayMs>());
        configConversion.floating(u8"SnapFov", loadVariable<legit_aimbot_vars::SnapFov>(), saveVariable<legit_aimbot_vars::SnapFov>());

        configConversion.boolean(u8"WallCheck", loadVariable<legit_aimbot_vars::WallCheck>(), saveVariable<legit_aimbot_vars::WallCheck>());
        configConversion.boolean(u8"Enabled", loadVariable<legit_aimbot_vars::Enabled>(), saveVariable<legit_aimbot_vars::Enabled>());
        configConversion.uint(u8"TargetSelection", loadVariable<legit_aimbot_vars::TargetSelection>(), saveVariable<legit_aimbot_vars::TargetSelection>());
        configConversion.boolean(u8"TargetLock", loadVariable<legit_aimbot_vars::TargetLock>(), saveVariable<legit_aimbot_vars::TargetLock>());
        configConversion.uint(u8"Fov", loadVariable<legit_aimbot_vars::Fov>(), saveVariable<legit_aimbot_vars::Fov>());
        configConversion.uint(u8"Smooth", loadVariable<legit_aimbot_vars::Smooth>(), saveVariable<legit_aimbot_vars::Smooth>());
        configConversion.boolean(u8"HitHead", loadVariable<legit_aimbot_vars::HitHead>(), saveVariable<legit_aimbot_vars::HitHead>());
        configConversion.boolean(u8"HitChest", loadVariable<legit_aimbot_vars::HitChest>(), saveVariable<legit_aimbot_vars::HitChest>());
        configConversion.boolean(u8"HitStomach", loadVariable<legit_aimbot_vars::HitStomach>(), saveVariable<legit_aimbot_vars::HitStomach>());
        configConversion.boolean(u8"HitArms", loadVariable<legit_aimbot_vars::HitArms>(), saveVariable<legit_aimbot_vars::HitArms>());
        configConversion.boolean(u8"HitLegs", loadVariable<legit_aimbot_vars::HitLegs>(), saveVariable<legit_aimbot_vars::HitLegs>());
        
        
        configConversion.boolean(u8"DrawFov", loadVariable<legit_aimbot_vars::DrawFov>(), saveVariable<legit_aimbot_vars::DrawFov>());
        
        
        
        
        configConversion.uint(u8"FovCircleHue", [](std::uint64_t) {}, [] { return 0; });
        
        configConversion.boolean(u8"SpreadCircleFov", loadVariable<legit_aimbot_vars::SpreadCircleFov>(), saveVariable<legit_aimbot_vars::SpreadCircleFov>());
        configConversion.uint(u8"AimKey", loadVariable<legit_aimbot_vars::AimKey>(), saveVariable<legit_aimbot_vars::AimKey>());
        configConversion.uint(u8"FovCircleColor", loadVariable<legit_aimbot_vars::FovCircleColor>(), saveVariable<legit_aimbot_vars::FovCircleColor>());
        configConversion.endObject();

        configConversion.beginObject(u8"Rcs");
        configConversion.boolean(u8"Enabled", loadVariable<rcs_vars::Enabled>(), saveVariable<rcs_vars::Enabled>());
        configConversion.uint(u8"Strength", loadVariable<rcs_vars::Strength>(), saveVariable<rcs_vars::Strength>());
        configConversion.endObject();

        
        
        configConversion.beginObject(u8"SpreadCircleVis");
        configConversion.boolean(u8"Enabled", loadVariable<spread_circle_vars::Enabled>(), saveVariable<spread_circle_vars::Enabled>());
        configConversion.uint(u8"Color", loadVariable<spread_circle_vars::SpreadCircleColor>(), saveVariable<spread_circle_vars::SpreadCircleColor>());
        configConversion.endObject();

        configConversion.endObject();
    }

    void skinChangerObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"SkinChanger");
        configConversion.uint(u8"KnifeModel", loadVariable<skin_changer_vars::KnifeModel>(), saveVariable<skin_changer_vars::KnifeModel>());
        configConversion.uint(u8"KnifeSkin", loadVariable<skin_changer_vars::KnifeSkin>(), saveVariable<skin_changer_vars::KnifeSkin>());
#define NS_SKIN_CHANGER_F(base) configConversion.uint(u8"" #base, loadVariable<skin_changer_vars::base>(), saveVariable<skin_changer_vars::base>());
        NS_SKIN_CHANGER_GUNS(NS_SKIN_CHANGER_F)
#undef NS_SKIN_CHANGER_F
        
        
        configConversion.uint(u8"KnifeSkinWear", loadVariable<skin_changer_vars::KnifeSkinWear>(), saveVariable<skin_changer_vars::KnifeSkinWear>());
        configConversion.uint(u8"KnifeSkinSeed", loadVariable<skin_changer_vars::KnifeSkinSeed>(), saveVariable<skin_changer_vars::KnifeSkinSeed>());
        configConversion.boolean(u8"StatTrakEnabled", loadVariable<skin_changer_vars::StatTrakEnabled>(), saveVariable<skin_changer_vars::StatTrakEnabled>());
        configConversion.uint(u8"StatTrakValue", loadVariable<skin_changer_vars::StatTrakValue>(), saveVariable<skin_changer_vars::StatTrakValue>());
#define NS_SKIN_CHANGER_F(base)                                                                     \
        configConversion.uint(u8"" #base "Wear", loadVariable<skin_changer_vars::base##Wear>(), saveVariable<skin_changer_vars::base##Wear>()); \
        configConversion.uint(u8"" #base "Seed", loadVariable<skin_changer_vars::base##Seed>(), saveVariable<skin_changer_vars::base##Seed>());
        NS_SKIN_CHANGER_GUNS(NS_SKIN_CHANGER_F)
#undef NS_SKIN_CHANGER_F
        configConversion.endObject();
    }

    void hudObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"Hud");

        configConversion.beginObject(u8"BombTimer");
        configConversion.boolean(u8"Enabled", loadVariable<BombTimerEnabled>(), saveVariable<BombTimerEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"BombDefuseAlert");
        configConversion.boolean(u8"Enabled", loadVariable<DefusingAlertEnabled>(), saveVariable<DefusingAlertEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"PreserveKillfeed");
        configConversion.boolean(u8"Enabled", loadVariable<KillfeedPreserverEnabled>(), saveVariable<KillfeedPreserverEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"PostRoundTimer");
        configConversion.boolean(u8"Enabled", loadVariable<PostRoundTimerEnabled>(), saveVariable<PostRoundTimerEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"BombPlantAlert");
        configConversion.boolean(u8"Enabled", loadVariable<BombPlantAlertEnabled>(), saveVariable<BombPlantAlertEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"Watermark");
        configConversion.boolean(u8"Enabled", loadVariable<watermark_vars::Enabled>(), saveVariable<watermark_vars::Enabled>());
        
        configConversion.boolean(u8"ShowFps", loadVariable<watermark_vars::ShowFps>(), saveVariable<watermark_vars::ShowFps>());
        configConversion.boolean(u8"ShowSpeed", loadVariable<watermark_vars::ShowSpeed>(), saveVariable<watermark_vars::ShowSpeed>());
        configConversion.boolean(u8"ShowPing", loadVariable<watermark_vars::ShowPing>(), saveVariable<watermark_vars::ShowPing>());
        configConversion.boolean(u8"ShowTeamDamage", loadVariable<watermark_vars::ShowTeamDamage>(), saveVariable<watermark_vars::ShowTeamDamage>());
        configConversion.boolean(u8"ShowClock", loadVariable<watermark_vars::ShowClock>(), saveVariable<watermark_vars::ShowClock>());
        configConversion.uint(u8"OffsetX", loadVariable<watermark_vars::OffsetX>(), saveVariable<watermark_vars::OffsetX>());
        configConversion.uint(u8"OffsetY", loadVariable<watermark_vars::OffsetY>(), saveVariable<watermark_vars::OffsetY>());
        configConversion.endObject();

        
        configConversion.beginObject(u8"BindsList");
        configConversion.boolean(u8"Enabled", loadVariable<binds_list_vars::Enabled>(), saveVariable<binds_list_vars::Enabled>());
        
        
        configConversion.floating(u8"OffsetX", loadVariable<binds_list_vars::OffsetX>(), saveVariable<binds_list_vars::OffsetX>());
        configConversion.floating(u8"OffsetY", loadVariable<binds_list_vars::OffsetY>(), saveVariable<binds_list_vars::OffsetY>());
        configConversion.endObject();

        configConversion.beginObject(u8"SpectatorList");
        configConversion.boolean(u8"Enabled", loadVariable<spectator_list_params::SpectatorListEnabled>(), saveVariable<spectator_list_params::SpectatorListEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"HudThemeColor");
        configConversion.boolean(u8"Enabled", loadVariable<hud_theme_vars::Enabled>(), saveVariable<hud_theme_vars::Enabled>());
        configConversion.endObject();

        configConversion.endObject();
    }

    void visualsObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"Visuals");

        configConversion.beginObject(u8"ModelGlow");
        configConversion.boolean(u8"Enabled", loadVariable<model_glow_vars::Enabled>(), saveVariable<model_glow_vars::Enabled>());
        configConversion.beginObject(u8"Players");
        configConversion.boolean(u8"Enabled", loadVariable<model_glow_vars::GlowPlayers>(), saveVariable<model_glow_vars::GlowPlayers>());
        configConversion.boolean(u8"OnlyEnemies", loadVariable<model_glow_vars::GlowOnlyEnemies>(), saveVariable<model_glow_vars::GlowOnlyEnemies>());
        
        
        
        configConversion.uint(u8"ColorMode", [](std::uint64_t) {}, [] { return 0; });
        configConversion.endObject();
        configConversion.boolean(u8"Weapons", loadVariable<model_glow_vars::GlowWeapons>(), saveVariable<model_glow_vars::GlowWeapons>());
        configConversion.boolean(u8"DroppedBomb", loadVariable<model_glow_vars::GlowDroppedBomb>(), saveVariable<model_glow_vars::GlowDroppedBomb>());
        configConversion.boolean(u8"TickingBomb", loadVariable<model_glow_vars::GlowTickingBomb>(), saveVariable<model_glow_vars::GlowTickingBomb>());
        configConversion.boolean(u8"DefuseKits", loadVariable<model_glow_vars::GlowDefuseKits>(), saveVariable<model_glow_vars::GlowDefuseKits>());
        configConversion.boolean(u8"GrenadeProjectiles", loadVariable<model_glow_vars::GlowGrenadeProjectiles>(), saveVariable<model_glow_vars::GlowGrenadeProjectiles>());
        configConversion.beginObject(u8"Hues");
        
        
        configConversion.uint(u8"PlayerBlue", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerGreen", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerYellow", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerOrange", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerPurple", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"TeamT", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"TeamCT", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"LowHealth", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"HighHealth", [](std::uint64_t) {}, [] { return 0; });
        
        
        
        configConversion.uint(u8"Enemy", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"Ally", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"Molotov", loadVariable<model_glow_vars::MolotovHue>(), saveVariable<model_glow_vars::MolotovHue>());
        configConversion.uint(u8"Flashbang", loadVariable<model_glow_vars::FlashbangHue>(), saveVariable<model_glow_vars::FlashbangHue>());
        configConversion.uint(u8"HEGrenade", loadVariable<model_glow_vars::HEGrenadeHue>(), saveVariable<model_glow_vars::HEGrenadeHue>());
        configConversion.uint(u8"SmokeGrenade", loadVariable<model_glow_vars::SmokeGrenadeHue>(), saveVariable<model_glow_vars::SmokeGrenadeHue>());
        configConversion.uint(u8"DroppedBomb", loadVariable<model_glow_vars::DroppedBombHue>(), saveVariable<model_glow_vars::DroppedBombHue>());
        configConversion.uint(u8"TickingBomb", loadVariable<model_glow_vars::TickingBombHue>(), saveVariable<model_glow_vars::TickingBombHue>());
        configConversion.uint(u8"DefuseKit", loadVariable<model_glow_vars::DefuseKitHue>(), saveVariable<model_glow_vars::DefuseKitHue>());
        
        configConversion.uint(u8"EnemyColor", loadVariable<model_glow_vars::EnemyColor>(), saveVariable<model_glow_vars::EnemyColor>());
        configConversion.uint(u8"AllyColor", loadVariable<model_glow_vars::AllyColor>(), saveVariable<model_glow_vars::AllyColor>());
        
        configConversion.uint(u8"FlashbangColor", loadVariable<model_glow_vars::FlashbangColor>(), saveVariable<model_glow_vars::FlashbangColor>());
        configConversion.uint(u8"HEGrenadeColor", loadVariable<model_glow_vars::HEGrenadeColor>(), saveVariable<model_glow_vars::HEGrenadeColor>());
        configConversion.uint(u8"SmokeGrenadeColor", loadVariable<model_glow_vars::SmokeGrenadeColor>(), saveVariable<model_glow_vars::SmokeGrenadeColor>());
        configConversion.uint(u8"MolotovColor", loadVariable<model_glow_vars::MolotovColor>(), saveVariable<model_glow_vars::MolotovColor>());
        configConversion.uint(u8"DroppedBombColor", loadVariable<model_glow_vars::DroppedBombColor>(), saveVariable<model_glow_vars::DroppedBombColor>());
        configConversion.uint(u8"TickingBombColor", loadVariable<model_glow_vars::TickingBombColor>(), saveVariable<model_glow_vars::TickingBombColor>());
        configConversion.uint(u8"DefuseKitColor", loadVariable<model_glow_vars::DefuseKitColor>(), saveVariable<model_glow_vars::DefuseKitColor>());

        configConversion.endObject();
        configConversion.endObject();

        configConversion.beginObject(u8"OutlineGlow");
        configConversion.boolean(u8"Enabled", loadVariable<outline_glow_vars::Enabled>(), saveVariable<outline_glow_vars::Enabled>());
        configConversion.beginObject(u8"Players");
        configConversion.boolean(u8"Enabled", loadVariable<outline_glow_vars::GlowPlayers>(), saveVariable<outline_glow_vars::GlowPlayers>());
        configConversion.boolean(u8"OnlyEnemies", loadVariable<outline_glow_vars::GlowOnlyEnemies>(), saveVariable<outline_glow_vars::GlowOnlyEnemies>());
        
        
        configConversion.uint(u8"ColorMode", [](std::uint64_t) {}, [] { return 0; });
        configConversion.endObject();
        configConversion.boolean(u8"Weapons", loadVariable<outline_glow_vars::GlowWeapons>(), saveVariable<outline_glow_vars::GlowWeapons>());
        configConversion.boolean(u8"DroppedBomb", loadVariable<outline_glow_vars::GlowDroppedBomb>(), saveVariable<outline_glow_vars::GlowDroppedBomb>());
        configConversion.boolean(u8"TickingBomb", loadVariable<outline_glow_vars::GlowTickingBomb>(), saveVariable<outline_glow_vars::GlowTickingBomb>());
        configConversion.boolean(u8"DefuseKits", loadVariable<outline_glow_vars::GlowDefuseKits>(), saveVariable<outline_glow_vars::GlowDefuseKits>());
        configConversion.boolean(u8"Hostages", loadVariable<outline_glow_vars::GlowHostages>(), saveVariable<outline_glow_vars::GlowHostages>());
        configConversion.boolean(u8"GrenadeProjectiles", loadVariable<outline_glow_vars::GlowGrenadeProjectiles>(), saveVariable<outline_glow_vars::GlowGrenadeProjectiles>());
        configConversion.beginObject(u8"Hues");
        
        
        configConversion.uint(u8"PlayerBlue", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerGreen", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerYellow", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerOrange", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerPurple", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"TeamT", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"TeamCT", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"LowHealth", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"HighHealth", [](std::uint64_t) {}, [] { return 0; });
        
        
        
        configConversion.uint(u8"Enemy", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"Ally", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"Molotov", loadVariable<outline_glow_vars::MolotovHue>(), saveVariable<outline_glow_vars::MolotovHue>());
        configConversion.uint(u8"Flashbang", loadVariable<outline_glow_vars::FlashbangHue>(), saveVariable<outline_glow_vars::FlashbangHue>());
        configConversion.uint(u8"HEGrenade", loadVariable<outline_glow_vars::HEGrenadeHue>(), saveVariable<outline_glow_vars::HEGrenadeHue>());
        configConversion.uint(u8"SmokeGrenade", loadVariable<outline_glow_vars::SmokeGrenadeHue>(), saveVariable<outline_glow_vars::SmokeGrenadeHue>());
        configConversion.uint(u8"DroppedBomb", loadVariable<outline_glow_vars::DroppedBombHue>(), saveVariable<outline_glow_vars::DroppedBombHue>());
        configConversion.uint(u8"TickingBomb", loadVariable<outline_glow_vars::TickingBombHue>(), saveVariable<outline_glow_vars::TickingBombHue>());
        configConversion.uint(u8"DefuseKit", loadVariable<outline_glow_vars::DefuseKitHue>(), saveVariable<outline_glow_vars::DefuseKitHue>());
        configConversion.uint(u8"Hostage", loadVariable<outline_glow_vars::HostageHue>(), saveVariable<outline_glow_vars::HostageHue>());
        
        configConversion.uint(u8"EnemyColor", loadVariable<outline_glow_vars::EnemyColor>(), saveVariable<outline_glow_vars::EnemyColor>());
        configConversion.uint(u8"AllyColor", loadVariable<outline_glow_vars::AllyColor>(), saveVariable<outline_glow_vars::AllyColor>());
        
        configConversion.uint(u8"FlashbangColor", loadVariable<outline_glow_vars::FlashbangColor>(), saveVariable<outline_glow_vars::FlashbangColor>());
        configConversion.uint(u8"HEGrenadeColor", loadVariable<outline_glow_vars::HEGrenadeColor>(), saveVariable<outline_glow_vars::HEGrenadeColor>());
        configConversion.uint(u8"SmokeGrenadeColor", loadVariable<outline_glow_vars::SmokeGrenadeColor>(), saveVariable<outline_glow_vars::SmokeGrenadeColor>());
        configConversion.uint(u8"MolotovColor", loadVariable<outline_glow_vars::MolotovColor>(), saveVariable<outline_glow_vars::MolotovColor>());
        configConversion.uint(u8"DroppedBombColor", loadVariable<outline_glow_vars::DroppedBombColor>(), saveVariable<outline_glow_vars::DroppedBombColor>());
        configConversion.uint(u8"TickingBombColor", loadVariable<outline_glow_vars::TickingBombColor>(), saveVariable<outline_glow_vars::TickingBombColor>());
        configConversion.uint(u8"DefuseKitColor", loadVariable<outline_glow_vars::DefuseKitColor>(), saveVariable<outline_glow_vars::DefuseKitColor>());
        configConversion.uint(u8"HostageColor", loadVariable<outline_glow_vars::HostageColor>(), saveVariable<outline_glow_vars::HostageColor>());

        configConversion.endObject();
        configConversion.endObject();

        configConversion.beginObject(u8"PlayerInfoInWorld");
        configConversion.boolean(u8"Enabled", loadVariable<player_info_vars::Enabled>(), saveVariable<player_info_vars::Enabled>());
        configConversion.boolean(u8"OnlyEnemies", loadVariable<player_info_vars::OnlyEnemies>(), saveVariable<player_info_vars::OnlyEnemies>());

        configConversion.beginObject(u8"PlayerPositionArrow");
        configConversion.boolean(u8"Enabled", loadVariable<player_info_vars::PlayerPositionArrowEnabled>(), saveVariable<player_info_vars::PlayerPositionArrowEnabled>());
        configConversion.uint(u8"ColorMode", loadVariable<player_info_vars::PlayerPositionArrowColorMode>(), saveVariable<player_info_vars::PlayerPositionArrowColorMode>());
        configConversion.endObject();

        configConversion.beginObject(u8"Health");
        configConversion.boolean(u8"Enabled", loadVariable<player_info_vars::PlayerHealthEnabled>(), saveVariable<player_info_vars::PlayerHealthEnabled>());
        configConversion.uint(u8"ColorMode", loadVariable<player_info_vars::PlayerHealthColorMode>(), saveVariable<player_info_vars::PlayerHealthColorMode>());
        configConversion.endObject();

        configConversion.boolean(u8"ActiveWeaponIcon", loadVariable<player_info_vars::ActiveWeaponIconEnabled>(), saveVariable<player_info_vars::ActiveWeaponIconEnabled>());
        configConversion.boolean(u8"BombCarrierIcon", loadVariable<player_info_vars::BombCarrierIconEnabled>(), saveVariable<player_info_vars::BombCarrierIconEnabled>());
        configConversion.boolean(u8"BombPlantIcon", loadVariable<player_info_vars::BombPlantIconEnabled>(), saveVariable<player_info_vars::BombPlantIconEnabled>());
        configConversion.boolean(u8"ActiveWeaponAmmo", loadVariable<player_info_vars::ActiveWeaponAmmoEnabled>(), saveVariable<player_info_vars::ActiveWeaponAmmoEnabled>());
        configConversion.boolean(u8"BombDefuseIcon", loadVariable<player_info_vars::BombDefuseIconEnabled>(), saveVariable<player_info_vars::BombDefuseIconEnabled>());
        configConversion.boolean(u8"HostagePickupIcon", loadVariable<player_info_vars::HostagePickupIconEnabled>(), saveVariable<player_info_vars::HostagePickupIconEnabled>());
        configConversion.boolean(u8"HostageRescueIcon", loadVariable<player_info_vars::HostageRescueIconEnabled>(), saveVariable<player_info_vars::HostageRescueIconEnabled>());
        configConversion.boolean(u8"BlindedIcon", loadVariable<player_info_vars::BlindedIconEnabled>(), saveVariable<player_info_vars::BlindedIconEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"ViewmodelMod");
        
        
        configConversion.boolean(u8"Enabled", [](bool) {}, [] { return false; });
        configConversion.boolean(u8"ModifyFov", loadVariable<viewmodel_mod_vars::ModifyFov>(), saveVariable<viewmodel_mod_vars::ModifyFov>());
        configConversion.uint(u8"Fov", loadVariable<viewmodel_mod_vars::Fov>(), saveVariable<viewmodel_mod_vars::Fov>());
        configConversion.boolean(u8"ModifyPosition", loadVariable<viewmodel_mod_vars::ModifyPosition>(), saveVariable<viewmodel_mod_vars::ModifyPosition>());
        configConversion.floating(u8"OffsetX", loadVariable<viewmodel_mod_vars::OffsetX>(), saveVariable<viewmodel_mod_vars::OffsetX>());
        configConversion.floating(u8"OffsetY", loadVariable<viewmodel_mod_vars::OffsetY>(), saveVariable<viewmodel_mod_vars::OffsetY>());
        configConversion.floating(u8"OffsetZ", loadVariable<viewmodel_mod_vars::OffsetZ>(), saveVariable<viewmodel_mod_vars::OffsetZ>());
        configConversion.floating(u8"Pitch", loadVariable<viewmodel_mod_vars::Pitch>(), saveVariable<viewmodel_mod_vars::Pitch>());
        configConversion.floating(u8"Roll", loadVariable<viewmodel_mod_vars::Roll>(), saveVariable<viewmodel_mod_vars::Roll>());
        configConversion.endObject();

        
        
        configConversion.beginObject(u8"Hitmarker");
        configConversion.boolean(u8"Enabled", loadVariable<HitmarkerEnabled>(), saveVariable<HitmarkerEnabled>());
        configConversion.floating(u8"Length", loadVariable<HitmarkerLength>(), saveVariable<HitmarkerLength>());
        configConversion.floating(u8"Gap", loadVariable<HitmarkerGap>(), saveVariable<HitmarkerGap>());
        configConversion.floating(u8"Timeout", loadVariable<HitmarkerTimeout>(), saveVariable<HitmarkerTimeout>());
        configConversion.uint(u8"Color", loadVariable<HitmarkerColor>(), saveVariable<HitmarkerColor>());
        configConversion.endObject();

        configConversion.beginObject(u8"ThirdPerson");
        configConversion.boolean(u8"Enabled", loadVariable<ForceThirdPersonEnabled>(), saveVariable<ForceThirdPersonEnabled>());
        configConversion.floating(u8"Distance", loadVariable<ForceThirdPersonDistance>(), saveVariable<ForceThirdPersonDistance>());
        configConversion.endObject();

        configConversion.beginObject(u8"Removals");
        configConversion.boolean(u8"ViewPunch", loadVariable<RemoveViewPunch>(), saveVariable<RemoveViewPunch>());
        configConversion.boolean(u8"Legs", loadVariable<RemoveLegs>(), saveVariable<RemoveLegs>());
        configConversion.boolean(u8"FlashOverlay", loadVariable<RemoveFlashOverlay>(), saveVariable<RemoveFlashOverlay>());
        configConversion.boolean(u8"MenuAds", loadVariable<RemoveMenuAds>(), saveVariable<RemoveMenuAds>());
        configConversion.endObject();

        configConversion.beginObject(u8"WorldColors");
        configConversion.boolean(u8"Inferno", loadVariable<WorldColorsInfernoEnabled>(), saveVariable<WorldColorsInfernoEnabled>());
        configConversion.uint(u8"MolotovColor", loadVariable<MolotovColor>(), saveVariable<MolotovColor>());
        configConversion.uint(u8"IncendiaryColor", loadVariable<IncendiaryColor>(), saveVariable<IncendiaryColor>());
        configConversion.boolean(u8"Lights", loadVariable<WorldColorsLightsEnabled>(), saveVariable<WorldColorsLightsEnabled>());
        configConversion.uint(u8"LightColor", loadVariable<WorldColorsLightColor>(), saveVariable<WorldColorsLightColor>());
        configConversion.boolean(u8"Sky", loadVariable<WorldColorsSkyEnabled>(), saveVariable<WorldColorsSkyEnabled>());
        configConversion.uint(u8"SkyColor", loadVariable<WorldColorsSkyColor>(), saveVariable<WorldColorsSkyColor>());
        configConversion.boolean(u8"World", loadVariable<WorldColorsWorldEnabled>(), saveVariable<WorldColorsWorldEnabled>());
        configConversion.uint(u8"WorldColor", loadVariable<WorldColorsWorldColor>(), saveVariable<WorldColorsWorldColor>());
        configConversion.boolean(u8"Fog", loadVariable<WorldColorsFogEnabled>(), saveVariable<WorldColorsFogEnabled>());
        configConversion.uint(u8"FogColor", loadVariable<WorldColorsFogColor>(), saveVariable<WorldColorsFogColor>());
        configConversion.uint(u8"FogDensity", loadVariable<WorldColorsFogDensity>(), saveVariable<WorldColorsFogDensity>());
        configConversion.floating(u8"FogDistance", loadVariable<WorldColorsFogDistance>(), saveVariable<WorldColorsFogDistance>());
        configConversion.boolean(u8"Bloom", loadVariable<WorldColorsBloomEnabled>(), saveVariable<WorldColorsBloomEnabled>());
        configConversion.uint(u8"BloomStrength", loadVariable<WorldColorsBloomStrength>(), saveVariable<WorldColorsBloomStrength>());
        configConversion.uint(u8"SkyBrightness", loadVariable<WorldColorsSkyBrightness>(), saveVariable<WorldColorsSkyBrightness>());
        configConversion.endObject();

        configConversion.beginObject(u8"PlayerList");
        configConversion.boolean(u8"Enabled", loadVariable<PlayerListEnabled>(), saveVariable<PlayerListEnabled>());
        
        
        configConversion.floating(u8"PosX", [](float) {}, [] { return 0.0f; });
        configConversion.floating(u8"PosY", [](float) {}, [] { return 0.0f; });
        configConversion.floating(u8"OffsetX", loadVariable<PlayerListOffsetX>(), saveVariable<PlayerListOffsetX>());
        configConversion.floating(u8"OffsetY", loadVariable<PlayerListOffsetY>(), saveVariable<PlayerListOffsetY>());
        configConversion.endObject();

        
        configConversion.beginObject(u8"GrenadeTimers");
        configConversion.boolean(u8"Enabled", loadVariable<grenade_timers_vars::Enabled>(), saveVariable<grenade_timers_vars::Enabled>());
        configConversion.boolean(u8"SmokeTimers", loadVariable<grenade_timers_vars::SmokeTimers>(), saveVariable<grenade_timers_vars::SmokeTimers>());
        configConversion.boolean(u8"MolotovTimers", loadVariable<grenade_timers_vars::MolotovTimers>(), saveVariable<grenade_timers_vars::MolotovTimers>());
        configConversion.endObject();

        configConversion.endObject();
    }

    
    void menuObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"Menu");
        configConversion.beginObject(u8"Theme");
        configConversion.uint(u8"Accent", loadVariable<MenuAccentColor>(), saveVariable<MenuAccentColor>());
        configConversion.uint(u8"Button", loadVariable<MenuButtonColor>(), saveVariable<MenuButtonColor>());
        configConversion.uint(u8"Slider", loadVariable<MenuSliderColor>(), saveVariable<MenuSliderColor>());
        configConversion.boolean(u8"Glow", loadVariable<MenuGlowEnabled>(), saveVariable<MenuGlowEnabled>());
        configConversion.uint(u8"GlowColor", loadVariable<MenuGlowColor>(), saveVariable<MenuGlowColor>());
        configConversion.boolean(u8"GlowRainbow", loadVariable<MenuGlowRainbow>(), saveVariable<MenuGlowRainbow>());
        configConversion.floating(u8"GlowSpeed", loadVariable<MenuGlowSpeed>(), saveVariable<MenuGlowSpeed>());
        configConversion.floating(u8"GlowSize", loadVariable<MenuGlowSize>(), saveVariable<MenuGlowSize>());
        configConversion.boolean(u8"GlowDebug", loadVariable<MenuGlowDebug>(), saveVariable<MenuGlowDebug>());
        configConversion.boolean(u8"StyleRainbow", loadVariable<MenuStyleRainbow>(), saveVariable<MenuStyleRainbow>());
        configConversion.boolean(u8"ReduceMotion", loadVariable<MenuReduceMotion>(), saveVariable<MenuReduceMotion>());
        configConversion.endObject();
        configConversion.endObject();
    }

    
    
    void gameObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"Game");

        configConversion.beginObject(u8"HitLog");
        configConversion.boolean(u8"Enabled", loadVariable<HitLogEnabled>(), saveVariable<HitLogEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"TeamDamage");
        configConversion.boolean(u8"Enabled", loadVariable<TeamDamageTrackerEnabled>(), saveVariable<TeamDamageTrackerEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"VoteRevealer");
        configConversion.boolean(u8"Enabled", loadVariable<VoteRevealerEnabled>(), saveVariable<VoteRevealerEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"CooldownRevealer");
        configConversion.boolean(u8"Enabled", loadVariable<CooldownRevealerEnabled>(), saveVariable<CooldownRevealerEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"Blockbot");
        configConversion.boolean(u8"Enabled", loadVariable<BlockbotEnabled>(), saveVariable<BlockbotEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"FakePrime");
        configConversion.boolean(u8"Enabled", loadVariable<FakePrimeEnabled>(), saveVariable<FakePrimeEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"MatchAutoAccept");
        configConversion.boolean(u8"Enabled", loadVariable<MatchAutoAcceptEnabled>(), saveVariable<MatchAutoAcceptEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"FakeLevel");
        configConversion.boolean(u8"Enabled", loadVariable<FakeLevelEnabled>(), saveVariable<FakeLevelEnabled>());
        configConversion.uint(u8"Level", loadVariable<FakeLevelValue>(), saveVariable<FakeLevelValue>());
        
        configConversion.uint(u8"Xp", loadVariable<FakeLevelXp>(), saveVariable<FakeLevelXp>());
        configConversion.endObject();

        configConversion.beginObject(u8"Bunnyhop");
        configConversion.uint(u8"AutoStrafeMode", loadVariable<AutoStrafeMode>(), saveVariable<AutoStrafeMode>());
        configConversion.uint(u8"LegitStrafeStrength", loadVariable<LegitStrafeStrength>(), saveVariable<LegitStrafeStrength>());
        configConversion.uint(u8"LegitStrafeMouseThreshold", loadVariable<LegitStrafeMouseThreshold>(), saveVariable<LegitStrafeMouseThreshold>());

        configConversion.boolean(u8"Enabled", loadVariable<BunnyhopEnabled>(), saveVariable<BunnyhopEnabled>());
        configConversion.boolean(u8"AutoStrafe", loadVariable<AutoStrafeEnabled>(), saveVariable<AutoStrafeEnabled>());
        configConversion.boolean(u8"TestStrafer", loadVariable<TestStraferEnabled>(), saveVariable<TestStraferEnabled>());
        configConversion.endObject();

        
        configConversion.beginObject(u8"Movement");
        configConversion.boolean(u8"EdgeJump", loadVariable<movement_vars::EdgeJump>(), saveVariable<movement_vars::EdgeJump>());
        configConversion.boolean(u8"EdgeStop", loadVariable<movement_vars::EdgeStop>(), saveVariable<movement_vars::EdgeStop>());
        configConversion.boolean(u8"SlowWalk", loadVariable<movement_vars::SlowWalk>(), saveVariable<movement_vars::SlowWalk>());
        configConversion.uint(u8"SlowWalkSpeed", loadVariable<movement_vars::SlowWalkSpeed>(), saveVariable<movement_vars::SlowWalkSpeed>());
        configConversion.boolean(u8"FastLadder", loadVariable<movement_vars::FastLadder>(), saveVariable<movement_vars::FastLadder>());
                configConversion.boolean(u8"JumpBug", loadVariable<movement_vars::JumpBug>(), saveVariable<movement_vars::JumpBug>());
        configConversion.boolean(u8"Desubtick", loadVariable<movement_vars::Desubtick>(), saveVariable<movement_vars::Desubtick>());
        configConversion.endObject();

        configConversion.beginObject(u8"LastTick");
        configConversion.boolean(u8"Enabled", loadVariable<last_tick_vars::Enabled>(), saveVariable<last_tick_vars::Enabled>());
        configConversion.uint(u8"DefuseKey", loadVariable<last_tick_vars::DefuseKey>(), saveVariable<last_tick_vars::DefuseKey>());
        configConversion.endObject();

        configConversion.beginObject(u8"SuperToss");
        configConversion.boolean(u8"Enabled", loadVariable<supertoss_vars::Enabled>(), saveVariable<supertoss_vars::Enabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"AutoPeek");
        configConversion.boolean(u8"Enabled", loadVariable<autopeek_vars::Enabled>(), saveVariable<autopeek_vars::Enabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"RevealRadar");
        configConversion.boolean(u8"Enabled", loadVariable<reveal_radar_vars::Enabled>(), saveVariable<reveal_radar_vars::Enabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"PanicKey");
        configConversion.uint(u8"Bind", loadVariable<panic_vars::Bind>(), saveVariable<panic_vars::Bind>());
        configConversion.endObject();

        configConversion.beginObject(u8"Killsay");
        configConversion.boolean(u8"Enabled", loadVariable<KillsayEnabled>(), saveVariable<KillsayEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"Fva");
        configConversion.boolean(u8"Enabled", loadVariable<FvaEnabled>(), saveVariable<FvaEnabled>());
        configConversion.uint(u8"Substeps", loadVariable<fva_vars::Substeps>(), saveVariable<fva_vars::Substeps>());
        configConversion.boolean(u8"ZeroOriginSpoof", loadVariable<FvaZeroOriginSpoof>(), saveVariable<FvaZeroOriginSpoof>());
        configConversion.boolean(u8"ReseedChains", loadVariable<FvaReseedChains>(), saveVariable<FvaReseedChains>());
        configConversion.boolean(u8"ChainOnFireOnly", loadVariable<FvaChainOnFireOnly>(), saveVariable<FvaChainOnFireOnly>());
        configConversion.boolean(u8"SilentShots", loadVariable<FvaSilentShots>(), saveVariable<FvaSilentShots>());
        configConversion.endObject();

        configConversion.boolean(u8"ValveDsSpoof", loadVariable<ValveDsSpoofEnabled>(), saveVariable<ValveDsSpoofEnabled>());

        
        configConversion.beginObject(u8"NetLag");
        configConversion.boolean(u8"Enabled", loadVariable<net_lag_vars::Enabled>(), saveVariable<net_lag_vars::Enabled>());
        configConversion.boolean(u8"FakelagAlways", loadVariable<net_lag_vars::FakelagAlways>(), saveVariable<net_lag_vars::FakelagAlways>());
        configConversion.uint(u8"ChokeKey", loadVariable<net_lag_vars::ChokeKeyBind>(), saveVariable<net_lag_vars::ChokeKeyBind>());
        configConversion.uint(u8"ChokeTicks", loadVariable<net_lag_vars::ChokeTicks>(), saveVariable<net_lag_vars::ChokeTicks>());
        configConversion.uint(u8"BlipCount", loadVariable<net_lag_vars::BlipCount>(), saveVariable<net_lag_vars::BlipCount>());
        configConversion.uint(u8"DupCount", loadVariable<net_lag_vars::DupCount>(), saveVariable<net_lag_vars::DupCount>());
        configConversion.boolean(u8"DelayEnabled", loadVariable<net_lag_vars::DelayEnabled>(), saveVariable<net_lag_vars::DelayEnabled>());
        configConversion.uint(u8"DelayMs", loadVariable<net_lag_vars::DelayMs>(), saveVariable<net_lag_vars::DelayMs>());
        configConversion.boolean(u8"StatsEnabled", loadVariable<net_lag_vars::StatsEnabled>(), saveVariable<net_lag_vars::StatsEnabled>());
        configConversion.uint(u8"FloodBurst", loadVariable<net_lag_vars::FloodBurstCount>(), saveVariable<net_lag_vars::FloodBurstCount>());
        configConversion.uint(u8"FloodKey", loadVariable<net_lag_vars::FloodKeyBind>(), saveVariable<net_lag_vars::FloodKeyBind>());
        configConversion.uint(u8"ConnlessFlood", loadVariable<net_lag_vars::ConnlessFloodCount>(), saveVariable<net_lag_vars::ConnlessFloodCount>());
        configConversion.uint(u8"ConnlessKey", loadVariable<net_lag_vars::ConnlessKeyBind>(), saveVariable<net_lag_vars::ConnlessKeyBind>());
        configConversion.endObject();

        
        configConversion.beginObject(u8"NameAnimator");
        configConversion.boolean(u8"Enabled", loadVariable<name_animator_vars::Enabled>(), saveVariable<name_animator_vars::Enabled>());
        configConversion.uint(u8"Mode", loadVariable<name_animator_vars::Mode>(), saveVariable<name_animator_vars::Mode>());
        configConversion.uint(u8"Speed", loadVariable<name_animator_vars::Speed>(), saveVariable<name_animator_vars::Speed>());
        configConversion.boolean(u8"DirectSend", loadVariable<name_animator_vars::DirectSend>(), saveVariable<name_animator_vars::DirectSend>());
        configConversion.endObject();

        
        configConversion.beginObject(u8"UserinfoFlood");
        configConversion.boolean(u8"Enabled", loadVariable<userinfo_flood_vars::Enabled>(), saveVariable<userinfo_flood_vars::Enabled>());
        configConversion.boolean(u8"HeavyMode", loadVariable<userinfo_flood_vars::HeavyMode>(), saveVariable<userinfo_flood_vars::HeavyMode>());
        configConversion.boolean(u8"FloodName", loadVariable<userinfo_flood_vars::FloodName>(), saveVariable<userinfo_flood_vars::FloodName>());
        configConversion.boolean(u8"FloodClutch", loadVariable<userinfo_flood_vars::FloodClutch>(), saveVariable<userinfo_flood_vars::FloodClutch>());
        configConversion.boolean(u8"FloodTeamColor", loadVariable<userinfo_flood_vars::FloodTeamColor>(), saveVariable<userinfo_flood_vars::FloodTeamColor>());
        configConversion.boolean(u8"FloodXhStyle", loadVariable<userinfo_flood_vars::FloodXhStyle>(), saveVariable<userinfo_flood_vars::FloodXhStyle>());
        configConversion.boolean(u8"FloodXhColor", loadVariable<userinfo_flood_vars::FloodXhColor>(), saveVariable<userinfo_flood_vars::FloodXhColor>());
        configConversion.boolean(u8"FloodXhSize", loadVariable<userinfo_flood_vars::FloodXhSize>(), saveVariable<userinfo_flood_vars::FloodXhSize>());
        configConversion.boolean(u8"FloodXhGap", loadVariable<userinfo_flood_vars::FloodXhGap>(), saveVariable<userinfo_flood_vars::FloodXhGap>());
        configConversion.boolean(u8"FloodXhThick", loadVariable<userinfo_flood_vars::FloodXhThick>(), saveVariable<userinfo_flood_vars::FloodXhThick>());
        configConversion.boolean(u8"FloodXhOutline", loadVariable<userinfo_flood_vars::FloodXhOutline>(), saveVariable<userinfo_flood_vars::FloodXhOutline>());
        configConversion.boolean(u8"FloodXhDot", loadVariable<userinfo_flood_vars::FloodXhDot>(), saveVariable<userinfo_flood_vars::FloodXhDot>());
        configConversion.boolean(u8"FloodXhAlpha", loadVariable<userinfo_flood_vars::FloodXhAlpha>(), saveVariable<userinfo_flood_vars::FloodXhAlpha>());
        configConversion.boolean(u8"FloodXhSniper", loadVariable<userinfo_flood_vars::FloodXhSniper>(), saveVariable<userinfo_flood_vars::FloodXhSniper>());
        configConversion.boolean(u8"FloodLoadout", loadVariable<userinfo_flood_vars::FloodLoadout>(), saveVariable<userinfo_flood_vars::FloodLoadout>());
        configConversion.boolean(u8"FloodTeamId", loadVariable<userinfo_flood_vars::FloodTeamId>(), saveVariable<userinfo_flood_vars::FloodTeamId>());
        
        
        configConversion.boolean(u8"FloodXhColorR", loadVariable<userinfo_flood_vars::FloodXhColorR>(), saveVariable<userinfo_flood_vars::FloodXhColorR>());
        configConversion.boolean(u8"FloodXhColorG", loadVariable<userinfo_flood_vars::FloodXhColorG>(), saveVariable<userinfo_flood_vars::FloodXhColorG>());
        configConversion.boolean(u8"FloodXhColorB", loadVariable<userinfo_flood_vars::FloodXhColorB>(), saveVariable<userinfo_flood_vars::FloodXhColorB>());
        configConversion.uint(u8"SendsPerTick", loadVariable<userinfo_flood_vars::SendsPerTick>(), saveVariable<userinfo_flood_vars::SendsPerTick>());
        configConversion.uint(u8"EveryTicks", loadVariable<userinfo_flood_vars::EveryTicks>(), saveVariable<userinfo_flood_vars::EveryTicks>());
        
        
        configConversion.boolean(u8"DirectMode", loadVariable<userinfo_flood_vars::DirectMode>(), saveVariable<userinfo_flood_vars::DirectMode>());
        configConversion.endObject();

        
        
        configConversion.beginObject(u8"ServerLagger");
        
        
        configConversion.uint(u8"Mode", [](unsigned) {}, [] { return 0u; });
        configConversion.boolean(u8"Enabled", loadVariable<server_lagger_vars::Enabled>(), saveVariable<server_lagger_vars::Enabled>());
        configConversion.uint(u8"MsgsPerBatch", loadVariable<server_lagger_vars::MsgsPerBatch>(), saveVariable<server_lagger_vars::MsgsPerBatch>());
        configConversion.uint(u8"AudioKB", loadVariable<server_lagger_vars::AudioKB>(), saveVariable<server_lagger_vars::AudioKB>());
        configConversion.uint(u8"Amount", loadVariable<server_lagger_vars::Amount>(), saveVariable<server_lagger_vars::Amount>());
        configConversion.uint(u8"PayloadMode", loadVariable<server_lagger_vars::PayloadMode>(), saveVariable<server_lagger_vars::PayloadMode>());
        
        configConversion.boolean(u8"MeterEnabled", loadVariable<server_lagger_vars::MeterEnabled>(), saveVariable<server_lagger_vars::MeterEnabled>());
        configConversion.floating(u8"MeterOffsetX", loadVariable<server_lagger_vars::MeterOffsetX>(), saveVariable<server_lagger_vars::MeterOffsetX>());
        configConversion.floating(u8"MeterOffsetY", loadVariable<server_lagger_vars::MeterOffsetY>(), saveVariable<server_lagger_vars::MeterOffsetY>());
        
        configConversion.uint(u8"Preset", loadVariable<server_lagger_vars::Preset>(), saveVariable<server_lagger_vars::Preset>());
        configConversion.boolean(u8"DatagramMode", loadVariable<server_lagger_vars::DatagramMode>(), saveVariable<server_lagger_vars::DatagramMode>());
        configConversion.boolean(u8"LoopFreeze", loadVariable<server_lagger_vars::LoopFreeze>(), saveVariable<server_lagger_vars::LoopFreeze>());
        configConversion.uint(u8"FreezeTicks", loadVariable<server_lagger_vars::FreezeTicks>(), saveVariable<server_lagger_vars::FreezeTicks>());
        configConversion.boolean(u8"AutoStop", loadVariable<server_lagger_vars::AutoStop>(), saveVariable<server_lagger_vars::AutoStop>());
        
        configConversion.boolean(u8"SlowRamp", loadVariable<server_lagger_vars::SlowRamp>(), saveVariable<server_lagger_vars::SlowRamp>());
        configConversion.uint(u8"RampInterval", loadVariable<server_lagger_vars::RampInterval>(), saveVariable<server_lagger_vars::RampInterval>());
        configConversion.uint(u8"LaggerKey", loadVariable<server_lagger_vars::LaggerKey>(), saveVariable<server_lagger_vars::LaggerKey>());
        configConversion.boolean(u8"NumPackets", loadVariable<server_lagger_vars::NumPackets>(), saveVariable<server_lagger_vars::NumPackets>());
        
        configConversion.boolean(u8"PulseMode", loadVariable<server_lagger_vars::PulseMode>(), saveVariable<server_lagger_vars::PulseMode>());
        configConversion.uint(u8"PulseOn", loadVariable<server_lagger_vars::PulseOn>(), saveVariable<server_lagger_vars::PulseOn>());
        configConversion.uint(u8"PulseOff", loadVariable<server_lagger_vars::PulseOff>(), saveVariable<server_lagger_vars::PulseOff>());
        configConversion.boolean(u8"MixMode", loadVariable<server_lagger_vars::MixMode>(), saveVariable<server_lagger_vars::MixMode>());
        configConversion.boolean(u8"PopulationScale", loadVariable<server_lagger_vars::PopulationScale>(), saveVariable<server_lagger_vars::PopulationScale>());
        configConversion.boolean(u8"Misattribute", loadVariable<server_lagger_vars::Misattribute>(), saveVariable<server_lagger_vars::Misattribute>());
        configConversion.endObject();

        
        
        configConversion.beginObject(u8"SpectateEnemies");
        configConversion.boolean(u8"Enabled", loadVariable<spectate_vars::Enabled>(), saveVariable<spectate_vars::Enabled>());
        configConversion.endObject();

        configConversion.endObject();
    }

    void soundObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"Sound");

        configConversion.beginObject(u8"HitSound");
        configConversion.boolean(u8"Enabled", loadVariable<HitSoundEnabled>(), saveVariable<HitSoundEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"Radio");
        configConversion.uint(u8"Volume", loadVariable<radio_vars::Volume>(), saveVariable<radio_vars::Volume>());
        configConversion.boolean(u8"MicBroadcast", loadVariable<radio_vars::MicBroadcast>(), saveVariable<radio_vars::MicBroadcast>());
        
        
        
        configConversion.boolean(u8"BoardEnabled", [](bool) {}, [] { return false; });
        configConversion.uint(u8"BoardKey1", [](unsigned) {}, [] { return 0; });
        configConversion.uint(u8"BoardKey2", [](unsigned) {}, [] { return 0; });
        configConversion.uint(u8"BoardKey3", [](unsigned) {}, [] { return 0; });
        configConversion.uint(u8"BoardKey4", [](unsigned) {}, [] { return 0; });
        
        configConversion.boolean(u8"AirhornEnabled", loadVariable<radio_vars::AirhornEnabled>(), saveVariable<radio_vars::AirhornEnabled>());
        configConversion.boolean(u8"AirhornFirstBlood", loadVariable<radio_vars::AirhornFirstBlood>(), saveVariable<radio_vars::AirhornFirstBlood>());
        configConversion.boolean(u8"AirhornHeadshot", loadVariable<radio_vars::AirhornHeadshot>(), saveVariable<radio_vars::AirhornHeadshot>());
        configConversion.boolean(u8"AirhornRoundWin", loadVariable<radio_vars::AirhornRoundWin>(), saveVariable<radio_vars::AirhornRoundWin>());
        configConversion.uint(u8"VoiceKey", loadVariable<radio_vars::VoiceKeyBind>(), saveVariable<radio_vars::VoiceKeyBind>());
        
        configConversion.boolean(u8"ShowNowPlaying", loadVariable<radio_vars::ShowNowPlaying>(), saveVariable<radio_vars::ShowNowPlaying>());
        configConversion.boolean(u8"ShowMediaPlayers", loadVariable<radio_vars::ShowMediaPlayers>(), saveVariable<radio_vars::ShowMediaPlayers>());
        configConversion.floating(u8"BoxOffsetX", loadVariable<radio_vars::NowPlayingOffsetX>(), saveVariable<radio_vars::NowPlayingOffsetX>());
        configConversion.floating(u8"BoxOffsetY", loadVariable<radio_vars::NowPlayingOffsetY>(), saveVariable<radio_vars::NowPlayingOffsetY>());
        configConversion.endObject();

        
        configConversion.beginObject(u8"Soundboard");
        configConversion.uint(u8"ClipIndex", loadVariable<soundboard_vars::ClipIndex>(), saveVariable<soundboard_vars::ClipIndex>());
        configConversion.uint(u8"SoundKey", loadVariable<soundboard_vars::SoundKeyBind>(), saveVariable<soundboard_vars::SoundKeyBind>());
        configConversion.uint(u8"VoiceFormat", loadVariable<soundboard_vars::VoiceFormat>(), saveVariable<soundboard_vars::VoiceFormat>());
        configConversion.endObject();

        configConversion.beginObject(u8"SpawnProtection");
        configConversion.boolean(u8"Enabled", loadVariable<SpawnProtectionSoundEnabled>(), saveVariable<SpawnProtectionSoundEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"Visualizations");

        
        
        
        configConversion.beginObject(u8"ImpactMarkers");
        configConversion.boolean(u8"Enabled", [](bool) {}, [] { return false; });
        configConversion.endObject();

        configConversion.beginObject(u8"BulletTracers");
        configConversion.boolean(u8"Enabled", [](bool) {}, [] { return false; });
        configConversion.endObject();

        configConversion.beginObject(u8"GrenadeTrajectory");
        configConversion.boolean(u8"Enabled", [](bool) {}, [] { return false; });
        configConversion.endObject();

        configConversion.beginObject(u8"OffScreenArrows");
        configConversion.boolean(u8"Enabled", [](bool) {}, [] { return false; });
        configConversion.endObject();

        
        
        
        configConversion.beginObject(u8"AnimationMods");
        configConversion.boolean(u8"Freeze", loadVariable<animation_mod_vars::Freeze>(), saveVariable<animation_mod_vars::Freeze>());
        configConversion.boolean(u8"DisableIK", loadVariable<animation_mod_vars::DisableIK>(), saveVariable<animation_mod_vars::DisableIK>());
        configConversion.boolean(u8"DisableRagdolls", loadVariable<animation_mod_vars::DisableRagdolls>(), saveVariable<animation_mod_vars::DisableRagdolls>());
        configConversion.boolean(u8"ModifyRagdollScale", loadVariable<animation_mod_vars::ModifyRagdollScale>(), saveVariable<animation_mod_vars::ModifyRagdollScale>());
        configConversion.floating(u8"RagdollScale", loadVariable<animation_mod_vars::RagdollScale>(), saveVariable<animation_mod_vars::RagdollScale>());
        configConversion.boolean(u8"AnimateViewmodel", loadVariable<animation_mod_vars::AnimateViewmodel>(), saveVariable<animation_mod_vars::AnimateViewmodel>());
        configConversion.floating(u8"ViewmodelSpinSpeed", loadVariable<animation_mod_vars::ViewmodelSpinSpeed>(), saveVariable<animation_mod_vars::ViewmodelSpinSpeed>());
        configConversion.floating(u8"ViewmodelPitchSway", loadVariable<animation_mod_vars::ViewmodelPitchSway>(), saveVariable<animation_mod_vars::ViewmodelPitchSway>());
        configConversion.endObject();
        configConversion.beginObject(u8"Chams");
        configConversion.boolean(u8"HideEnemies", loadVariable<chams_vars::HideEnemies>(), saveVariable<chams_vars::HideEnemies>());
        configConversion.boolean(u8"HideLocalPlayer", loadVariable<chams_vars::HideLocalPlayer>(), saveVariable<chams_vars::HideLocalPlayer>());
        configConversion.boolean(u8"Enabled", loadVariable<chams_vars::Enabled>(), saveVariable<chams_vars::Enabled>());
        configConversion.uint(u8"EnemyColor", loadVariable<chams_vars::EnemyColor>(), saveVariable<chams_vars::EnemyColor>());
        configConversion.endObject();

        configConversion.beginObject(u8"Footsteps");
        configConversion.boolean(u8"Enabled", loadVariable<FootstepSoundVisualizationEnabled>(), saveVariable<FootstepSoundVisualizationEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"BombPlant");
        configConversion.boolean(u8"Enabled", loadVariable<BombPlantSoundVisualizationEnabled>(), saveVariable<BombPlantSoundVisualizationEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"BombBeep");
        configConversion.boolean(u8"Enabled", loadVariable<BombBeepSoundVisualizationEnabled>(), saveVariable<BombBeepSoundVisualizationEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"BombDefuse");
        configConversion.boolean(u8"Enabled", loadVariable<BombDefuseSoundVisualizationEnabled>(), saveVariable<BombDefuseSoundVisualizationEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"WeaponScope");
        configConversion.boolean(u8"Enabled", loadVariable<WeaponScopeSoundVisualizationEnabled>(), saveVariable<WeaponScopeSoundVisualizationEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"WeaponReload");
        configConversion.boolean(u8"Enabled", loadVariable<WeaponReloadSoundVisualizationEnabled>(), saveVariable<WeaponReloadSoundVisualizationEnabled>());
        configConversion.endObject();

        configConversion.endObject();
        configConversion.endObject();
    }

    template <std::unsigned_integral T>
    [[nodiscard]] static T saturateCast(std::unsigned_integral auto value) noexcept
    {
        if (value < (std::numeric_limits<T>::min)())
            return (std::numeric_limits<T>::min)();
        if (value > (std::numeric_limits<T>::max)())
            return (std::numeric_limits<T>::max)();
        return static_cast<T>(value);
    }

    template <typename ConfigVariable>
    [[nodiscard]] auto loadVariable()
    {
        if constexpr (IsRangeConstrained<typename ConfigVariable::ValueType>::value) {
            return [this](auto value) {
                if constexpr (std::is_same_v<color::HueInteger, typename ConfigVariable::ValueType::ValueType>) {
                    color::HueInteger hue{std::clamp(saturateCast<color::HueInteger::UnderlyingType>(value), color::HueInteger::kMin, color::HueInteger::kMax)};
                    hookContext.config().template setVariableWithoutAutoSave<ConfigVariable>(typename ConfigVariable::ValueType{std::clamp(hue, ConfigVariable::ValueType::kMin, ConfigVariable::ValueType::kMax)});
                } else if constexpr (std::is_same_v<std::uint16_t, typename ConfigVariable::ValueType::ValueType>) {
                    hookContext.config().template setVariableWithoutAutoSave<ConfigVariable>(typename ConfigVariable::ValueType{std::clamp(saturateCast<std::uint16_t>(value), ConfigVariable::ValueType::kMin, ConfigVariable::ValueType::kMax)});
                } else if constexpr (std::is_same_v<std::uint8_t, typename ConfigVariable::ValueType::ValueType>) {
                    hookContext.config().template setVariableWithoutAutoSave<ConfigVariable>(typename ConfigVariable::ValueType{std::clamp(saturateCast<std::uint8_t>(value), ConfigVariable::ValueType::kMin, ConfigVariable::ValueType::kMax)});
                } else if constexpr (std::is_floating_point_v<typename ConfigVariable::ValueType::ValueType>) {
                    hookContext.config().template setVariableWithoutAutoSave<ConfigVariable>(typename ConfigVariable::ValueType{std::clamp(static_cast<typename ConfigVariable::ValueType::ValueType>(value), ConfigVariable::ValueType::kMin, ConfigVariable::ValueType::kMax)});
                } else {
                    static_assert(!std::is_same_v<ConfigVariable, ConfigVariable>, "Unsupported type");
                }
            };
        } else if constexpr (std::is_enum_v<typename ConfigVariable::ValueType>) {
            return [this](std::integral auto value) {
                hookContext.config().template setVariableWithoutAutoSave<ConfigVariable>(static_cast<ConfigVariable::ValueType>(value));
            };
        } else {
            return [this](ConfigVariable::ValueType value) {
                hookContext.config().template setVariableWithoutAutoSave<ConfigVariable>(value);
            };
        }
    }

    template <typename ConfigVariable>
    [[nodiscard]] auto saveVariable()
    {
        if constexpr (IsRangeConstrained<typename ConfigVariable::ValueType>::value) {
            return [this] {
                return config_overrides::valueForSave(ConfigVariableTypes::indexOf<ConfigVariable>(),
                    static_cast<typename ConfigVariable::ValueType::ValueType>(GET_CONFIG_VAR(ConfigVariable)));
            };
        } else if constexpr (std::is_enum_v<typename ConfigVariable::ValueType>) {
            return [this] {
                return static_cast<std::underlying_type_t<typename ConfigVariable::ValueType>>(GET_CONFIG_VAR(ConfigVariable));
            };
        } else {
            return [this] {
                return config_overrides::valueForSave(ConfigVariableTypes::indexOf<ConfigVariable>(), GET_CONFIG_VAR(ConfigVariable));
            };
        }
    }

    HookContext& hookContext;
};
