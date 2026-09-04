#pragma once

#include <algorithm>

#include "ConfigVariableTypes.h"
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
        return configConversion.endRoot();
    }

private:
    // Appended LAST (top-level objects must go last to keep old configs parsing).
    void discordRpcObject(auto&& configConversion)
    {
        configConversion.beginObject(u8"DiscordRpc");
        configConversion.boolean(u8"Enabled", loadVariable<discord_rpc_vars::Enabled>(), saveVariable<discord_rpc_vars::Enabled>());
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
        configConversion.endObject();

        configConversion.beginObject(u8"Aimbot");
        configConversion.boolean(u8"Enabled", loadVariable<aimbot_vars::Enabled>(), saveVariable<aimbot_vars::Enabled>());
        // Removed settings (the old user-settable FOV slider and rage fov circle - the rage aimbot now
        // uses velocity's fixed max_fov gate). They are still PARSED AND DISCARDED in their original
        // positions because the config loader is an order-sensitive streaming parser: dropping the keys
        // would stall every later key on old config files that still contain them.
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
        // NEW keys go LAST in an object (see the config-loader ordering rule at the top of this file's history:
        // appending is backwards-compatible, inserting mid-object desyncs old files).
        configConversion.boolean(u8"SpreadCircleFov", loadVariable<aimbot_vars::SpreadCircleFov>(), saveVariable<aimbot_vars::SpreadCircleFov>());
        // SeedCorrectionMode was removed (not the behavior the user wanted) - its config key is kept
        // as a parse-and-discard placeholder so existing config files keep loading; see the Aimbot
        // object's other removed keys above.
        configConversion.boolean(u8"SeedCorrectionMode", [](bool) {}, [] { return false; });
        // NEW keys go LAST in an object - appending keeps old config files parsing.
        configConversion.uint(u8"PointScale", loadVariable<aimbot_vars::PointScale>(), saveVariable<aimbot_vars::PointScale>());
        configConversion.boolean(u8"DynamicPointscale", loadVariable<aimbot_vars::DynamicPointscale>(), saveVariable<aimbot_vars::DynamicPointscale>());
        configConversion.boolean(u8"WallCheck", loadVariable<aimbot_vars::WallCheck>(), saveVariable<aimbot_vars::WallCheck>());
        configConversion.boolean(u8"Autowall", loadVariable<aimbot_vars::Autowall>(), saveVariable<aimbot_vars::Autowall>());
        configConversion.boolean(u8"AutoStop", loadVariable<aimbot_vars::AutoStop>(), saveVariable<aimbot_vars::AutoStop>());
        configConversion.boolean(u8"SpreadGate", loadVariable<aimbot_vars::SpreadGate>(), saveVariable<aimbot_vars::SpreadGate>());
        configConversion.boolean(u8"SeedFallback", loadVariable<aimbot_vars::SeedFallback>(), saveVariable<aimbot_vars::SeedFallback>());
        configConversion.endObject();

        configConversion.beginObject(u8"LegitAimbot");
        configConversion.boolean(u8"Enabled", loadVariable<legit_aimbot_vars::Enabled>(), saveVariable<legit_aimbot_vars::Enabled>());
        configConversion.uint(u8"Fov", loadVariable<legit_aimbot_vars::Fov>(), saveVariable<legit_aimbot_vars::Fov>());
        configConversion.uint(u8"Smooth", loadVariable<legit_aimbot_vars::Smooth>(), saveVariable<legit_aimbot_vars::Smooth>());
        configConversion.boolean(u8"HitHead", loadVariable<legit_aimbot_vars::HitHead>(), saveVariable<legit_aimbot_vars::HitHead>());
        configConversion.boolean(u8"HitChest", loadVariable<legit_aimbot_vars::HitChest>(), saveVariable<legit_aimbot_vars::HitChest>());
        configConversion.boolean(u8"HitStomach", loadVariable<legit_aimbot_vars::HitStomach>(), saveVariable<legit_aimbot_vars::HitStomach>());
        configConversion.boolean(u8"HitArms", loadVariable<legit_aimbot_vars::HitArms>(), saveVariable<legit_aimbot_vars::HitArms>());
        configConversion.boolean(u8"HitLegs", loadVariable<legit_aimbot_vars::HitLegs>(), saveVariable<legit_aimbot_vars::HitLegs>());
        // NEW keys go LAST in an object: appending keeps old config files parsing (a missing trailing
        // key just keeps its default), while inserting mid-object would desync the streaming parser.
        configConversion.boolean(u8"DrawFov", loadVariable<legit_aimbot_vars::DrawFov>(), saveVariable<legit_aimbot_vars::DrawFov>());
        // FovCircleHue was replaced by the full RGBA FovCircleColor (appended LAST below); the old
        // hue key stays as a parse-and-discard placeholder in its original position so existing
        // config files keep loading (the loader is an order-sensitive streaming parser - see the
        // Aimbot object's note).
        configConversion.uint(u8"FovCircleHue", [](std::uint64_t) {}, [] { return 0; });
        // NEW keys go LAST in an object - appending keeps old config files parsing.
        configConversion.boolean(u8"SpreadCircleFov", loadVariable<legit_aimbot_vars::SpreadCircleFov>(), saveVariable<legit_aimbot_vars::SpreadCircleFov>());
        configConversion.uint(u8"AimKey", loadVariable<legit_aimbot_vars::AimKey>(), saveVariable<legit_aimbot_vars::AimKey>());
        configConversion.uint(u8"FovCircleColor", loadVariable<legit_aimbot_vars::FovCircleColor>(), saveVariable<legit_aimbot_vars::FovCircleColor>());
        configConversion.endObject();

        configConversion.beginObject(u8"Rcs");
        configConversion.boolean(u8"Enabled", loadVariable<rcs_vars::Enabled>(), saveVariable<rcs_vars::Enabled>());
        configConversion.uint(u8"Strength", loadVariable<rcs_vars::Strength>(), saveVariable<rcs_vars::Strength>());
        configConversion.endObject();

        // NEW objects go LAST in their parent - appending keeps old config files parsing (a missing
        // object just keeps its defaults), while inserting earlier would desync old files.
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
        configConversion.uint(u8"M4A4Skin", loadVariable<skin_changer_vars::M4A4Skin>(), saveVariable<skin_changer_vars::M4A4Skin>());
        configConversion.uint(u8"AK47Skin", loadVariable<skin_changer_vars::AK47Skin>(), saveVariable<skin_changer_vars::AK47Skin>());
        configConversion.uint(u8"AWPSkin", loadVariable<skin_changer_vars::AWPSkin>(), saveVariable<skin_changer_vars::AWPSkin>());
        configConversion.uint(u8"DesertEagleSkin", loadVariable<skin_changer_vars::DesertEagleSkin>(), saveVariable<skin_changer_vars::DesertEagleSkin>());
        configConversion.uint(u8"USPSSkin", loadVariable<skin_changer_vars::USPSSkin>(), saveVariable<skin_changer_vars::USPSSkin>());
        configConversion.uint(u8"Glock18Skin", loadVariable<skin_changer_vars::Glock18Skin>(), saveVariable<skin_changer_vars::Glock18Skin>());
        configConversion.uint(u8"M249Skin", loadVariable<skin_changer_vars::M249Skin>(), saveVariable<skin_changer_vars::M249Skin>());
        configConversion.uint(u8"XM1014Skin", loadVariable<skin_changer_vars::XM1014Skin>(), saveVariable<skin_changer_vars::XM1014Skin>());
        configConversion.uint(u8"MAG7Skin", loadVariable<skin_changer_vars::MAG7Skin>(), saveVariable<skin_changer_vars::MAG7Skin>());
        configConversion.uint(u8"NegevSkin", loadVariable<skin_changer_vars::NegevSkin>(), saveVariable<skin_changer_vars::NegevSkin>());
        configConversion.uint(u8"SawedOffSkin", loadVariable<skin_changer_vars::SawedOffSkin>(), saveVariable<skin_changer_vars::SawedOffSkin>());
        configConversion.uint(u8"NovaSkin", loadVariable<skin_changer_vars::NovaSkin>(), saveVariable<skin_changer_vars::NovaSkin>());
        configConversion.uint(u8"DualBerettasSkin", loadVariable<skin_changer_vars::DualBerettasSkin>(), saveVariable<skin_changer_vars::DualBerettasSkin>());
        configConversion.uint(u8"FiveSeveNSkin", loadVariable<skin_changer_vars::FiveSeveNSkin>(), saveVariable<skin_changer_vars::FiveSeveNSkin>());
        configConversion.uint(u8"Tec9Skin", loadVariable<skin_changer_vars::Tec9Skin>(), saveVariable<skin_changer_vars::Tec9Skin>());
        configConversion.uint(u8"P2000Skin", loadVariable<skin_changer_vars::P2000Skin>(), saveVariable<skin_changer_vars::P2000Skin>());
        configConversion.uint(u8"P250Skin", loadVariable<skin_changer_vars::P250Skin>(), saveVariable<skin_changer_vars::P250Skin>());
        configConversion.uint(u8"CZ75AutoSkin", loadVariable<skin_changer_vars::CZ75AutoSkin>(), saveVariable<skin_changer_vars::CZ75AutoSkin>());
        configConversion.uint(u8"R8RevolverSkin", loadVariable<skin_changer_vars::R8RevolverSkin>(), saveVariable<skin_changer_vars::R8RevolverSkin>());
        configConversion.uint(u8"AUGSkin", loadVariable<skin_changer_vars::AUGSkin>(), saveVariable<skin_changer_vars::AUGSkin>());
        configConversion.uint(u8"FamasSkin", loadVariable<skin_changer_vars::FamasSkin>(), saveVariable<skin_changer_vars::FamasSkin>());
        configConversion.uint(u8"GalilARSkin", loadVariable<skin_changer_vars::GalilARSkin>(), saveVariable<skin_changer_vars::GalilARSkin>());
        configConversion.uint(u8"SG553Skin", loadVariable<skin_changer_vars::SG553Skin>(), saveVariable<skin_changer_vars::SG553Skin>());
        configConversion.uint(u8"M4A1SSkin", loadVariable<skin_changer_vars::M4A1SSkin>(), saveVariable<skin_changer_vars::M4A1SSkin>());
        configConversion.uint(u8"MAC10Skin", loadVariable<skin_changer_vars::MAC10Skin>(), saveVariable<skin_changer_vars::MAC10Skin>());
        configConversion.uint(u8"P90Skin", loadVariable<skin_changer_vars::P90Skin>(), saveVariable<skin_changer_vars::P90Skin>());
        configConversion.uint(u8"MP5SDSkin", loadVariable<skin_changer_vars::MP5SDSkin>(), saveVariable<skin_changer_vars::MP5SDSkin>());
        configConversion.uint(u8"UMP45Skin", loadVariable<skin_changer_vars::UMP45Skin>(), saveVariable<skin_changer_vars::UMP45Skin>());
        configConversion.uint(u8"PPBizonSkin", loadVariable<skin_changer_vars::PPBizonSkin>(), saveVariable<skin_changer_vars::PPBizonSkin>());
        configConversion.uint(u8"MP7Skin", loadVariable<skin_changer_vars::MP7Skin>(), saveVariable<skin_changer_vars::MP7Skin>());
        configConversion.uint(u8"MP9Skin", loadVariable<skin_changer_vars::MP9Skin>(), saveVariable<skin_changer_vars::MP9Skin>());
        configConversion.uint(u8"G3SG1Skin", loadVariable<skin_changer_vars::G3SG1Skin>(), saveVariable<skin_changer_vars::G3SG1Skin>());
        configConversion.uint(u8"SCAR20Skin", loadVariable<skin_changer_vars::SCAR20Skin>(), saveVariable<skin_changer_vars::SCAR20Skin>());
        configConversion.uint(u8"SSG08Skin", loadVariable<skin_changer_vars::SSG08Skin>(), saveVariable<skin_changer_vars::SSG08Skin>());
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
        // appended last (order-sensitive streaming parser): segment toggles + box offset
        configConversion.boolean(u8"ShowFps", loadVariable<watermark_vars::ShowFps>(), saveVariable<watermark_vars::ShowFps>());
        configConversion.boolean(u8"ShowSpeed", loadVariable<watermark_vars::ShowSpeed>(), saveVariable<watermark_vars::ShowSpeed>());
        configConversion.boolean(u8"ShowPing", loadVariable<watermark_vars::ShowPing>(), saveVariable<watermark_vars::ShowPing>());
        configConversion.boolean(u8"ShowTeamDamage", loadVariable<watermark_vars::ShowTeamDamage>(), saveVariable<watermark_vars::ShowTeamDamage>());
        configConversion.boolean(u8"ShowClock", loadVariable<watermark_vars::ShowClock>(), saveVariable<watermark_vars::ShowClock>());
        configConversion.uint(u8"OffsetX", loadVariable<watermark_vars::OffsetX>(), saveVariable<watermark_vars::OffsetX>());
        configConversion.uint(u8"OffsetY", loadVariable<watermark_vars::OffsetY>(), saveVariable<watermark_vars::OffsetY>());
        configConversion.endObject();

        // appended last (order-sensitive streaming parser)
        configConversion.beginObject(u8"BindsList");
        configConversion.boolean(u8"Enabled", loadVariable<binds_list_vars::Enabled>(), saveVariable<binds_list_vars::Enabled>());
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
        // players always glow with the EnemyColor/AllyColor RGBA pickers now; ColorMode is a
        // parse-and-discard placeholder so older config files keep loading (order-sensitive
        // streaming parser - see the Aimbot object's note)
        configConversion.uint(u8"ColorMode", [](std::uint64_t) {}, [] { return 0; });
        configConversion.endObject();
        configConversion.boolean(u8"Weapons", loadVariable<model_glow_vars::GlowWeapons>(), saveVariable<model_glow_vars::GlowWeapons>());
        configConversion.boolean(u8"DroppedBomb", loadVariable<model_glow_vars::GlowDroppedBomb>(), saveVariable<model_glow_vars::GlowDroppedBomb>());
        configConversion.boolean(u8"TickingBomb", loadVariable<model_glow_vars::GlowTickingBomb>(), saveVariable<model_glow_vars::GlowTickingBomb>());
        configConversion.boolean(u8"DefuseKits", loadVariable<model_glow_vars::GlowDefuseKits>(), saveVariable<model_glow_vars::GlowDefuseKits>());
        configConversion.boolean(u8"GrenadeProjectiles", loadVariable<model_glow_vars::GlowGrenadeProjectiles>(), saveVariable<model_glow_vars::GlowGrenadeProjectiles>());
        configConversion.beginObject(u8"Hues");
        // the player/team/health hue keys are parse-and-discard placeholders (player colors are
        // the EnemyColor/AllyColor RGBA pickers now) so older config files keep loading
        configConversion.uint(u8"PlayerBlue", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerGreen", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerYellow", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerOrange", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerPurple", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"TeamT", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"TeamCT", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"LowHealth", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"HighHealth", [](std::uint64_t) {}, [] { return 0; });
        // Enemy/Ally are full RGBA colors now (see EnemyColor/AllyColor appended below); the old
        // hue keys stay as parse-and-discard placeholders so older config files keep loading
        // (the loader is an order-sensitive streaming parser - see the Aimbot object's note).
        configConversion.uint(u8"Enemy", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"Ally", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"Molotov", loadVariable<model_glow_vars::MolotovHue>(), saveVariable<model_glow_vars::MolotovHue>());
        configConversion.uint(u8"Flashbang", loadVariable<model_glow_vars::FlashbangHue>(), saveVariable<model_glow_vars::FlashbangHue>());
        configConversion.uint(u8"HEGrenade", loadVariable<model_glow_vars::HEGrenadeHue>(), saveVariable<model_glow_vars::HEGrenadeHue>());
        configConversion.uint(u8"SmokeGrenade", loadVariable<model_glow_vars::SmokeGrenadeHue>(), saveVariable<model_glow_vars::SmokeGrenadeHue>());
        configConversion.uint(u8"DroppedBomb", loadVariable<model_glow_vars::DroppedBombHue>(), saveVariable<model_glow_vars::DroppedBombHue>());
        configConversion.uint(u8"TickingBomb", loadVariable<model_glow_vars::TickingBombHue>(), saveVariable<model_glow_vars::TickingBombHue>());
        configConversion.uint(u8"DefuseKit", loadVariable<model_glow_vars::DefuseKitHue>(), saveVariable<model_glow_vars::DefuseKitHue>());
        // NEW keys go LAST in an object - appending keeps old config files parsing.
        configConversion.uint(u8"EnemyColor", loadVariable<model_glow_vars::EnemyColor>(), saveVariable<model_glow_vars::EnemyColor>());
        configConversion.uint(u8"AllyColor", loadVariable<model_glow_vars::AllyColor>(), saveVariable<model_glow_vars::AllyColor>());
        configConversion.endObject();
        configConversion.endObject();

        configConversion.beginObject(u8"OutlineGlow");
        configConversion.boolean(u8"Enabled", loadVariable<outline_glow_vars::Enabled>(), saveVariable<outline_glow_vars::Enabled>());
        configConversion.beginObject(u8"Players");
        configConversion.boolean(u8"Enabled", loadVariable<outline_glow_vars::GlowPlayers>(), saveVariable<outline_glow_vars::GlowPlayers>());
        configConversion.boolean(u8"OnlyEnemies", loadVariable<outline_glow_vars::GlowOnlyEnemies>(), saveVariable<outline_glow_vars::GlowOnlyEnemies>());
        // players always glow with the EnemyColor/AllyColor RGBA pickers now; ColorMode is a
        // parse-and-discard placeholder (order-sensitive streaming parser - see the Aimbot note)
        configConversion.uint(u8"ColorMode", [](std::uint64_t) {}, [] { return 0; });
        configConversion.endObject();
        configConversion.boolean(u8"Weapons", loadVariable<outline_glow_vars::GlowWeapons>(), saveVariable<outline_glow_vars::GlowWeapons>());
        configConversion.boolean(u8"DroppedBomb", loadVariable<outline_glow_vars::GlowDroppedBomb>(), saveVariable<outline_glow_vars::GlowDroppedBomb>());
        configConversion.boolean(u8"TickingBomb", loadVariable<outline_glow_vars::GlowTickingBomb>(), saveVariable<outline_glow_vars::GlowTickingBomb>());
        configConversion.boolean(u8"DefuseKits", loadVariable<outline_glow_vars::GlowDefuseKits>(), saveVariable<outline_glow_vars::GlowDefuseKits>());
        configConversion.boolean(u8"Hostages", loadVariable<outline_glow_vars::GlowHostages>(), saveVariable<outline_glow_vars::GlowHostages>());
        configConversion.boolean(u8"GrenadeProjectiles", loadVariable<outline_glow_vars::GlowGrenadeProjectiles>(), saveVariable<outline_glow_vars::GlowGrenadeProjectiles>());
        configConversion.beginObject(u8"Hues");
        // the player/team/health hue keys are parse-and-discard placeholders (player colors are
        // the EnemyColor/AllyColor RGBA pickers now) so older config files keep loading
        configConversion.uint(u8"PlayerBlue", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerGreen", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerYellow", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerOrange", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"PlayerPurple", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"TeamT", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"TeamCT", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"LowHealth", [](std::uint64_t) {}, [] { return 0; });
        configConversion.uint(u8"HighHealth", [](std::uint64_t) {}, [] { return 0; });
        // Enemy/Ally are full RGBA colors now (see EnemyColor/AllyColor appended below); the old
        // hue keys stay as parse-and-discard placeholders so older config files keep loading
        // (the loader is an order-sensitive streaming parser - see the Aimbot object's note).
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
        // NEW keys go LAST in an object - appending keeps old config files parsing.
        configConversion.uint(u8"EnemyColor", loadVariable<outline_glow_vars::EnemyColor>(), saveVariable<outline_glow_vars::EnemyColor>());
        configConversion.uint(u8"AllyColor", loadVariable<outline_glow_vars::AllyColor>(), saveVariable<outline_glow_vars::AllyColor>());
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
        // MasterSwitch removed (the FOV toggle gates its own feature now) - kept as a
        // parse-and-discard placeholder per the config-loader ordering rule.
        configConversion.boolean(u8"Enabled", [](bool) {}, [] { return false; });
        configConversion.boolean(u8"ModifyFov", loadVariable<viewmodel_mod_vars::ModifyFov>(), saveVariable<viewmodel_mod_vars::ModifyFov>());
        configConversion.uint(u8"Fov", loadVariable<viewmodel_mod_vars::Fov>(), saveVariable<viewmodel_mod_vars::Fov>());
        configConversion.boolean(u8"ModifyPosition", loadVariable<viewmodel_mod_vars::ModifyPosition>(), saveVariable<viewmodel_mod_vars::ModifyPosition>());
        configConversion.floating(u8"OffsetX", loadVariable<viewmodel_mod_vars::OffsetX>(), saveVariable<viewmodel_mod_vars::OffsetX>());
        configConversion.floating(u8"OffsetY", loadVariable<viewmodel_mod_vars::OffsetY>(), saveVariable<viewmodel_mod_vars::OffsetY>());
        configConversion.floating(u8"OffsetZ", loadVariable<viewmodel_mod_vars::OffsetZ>(), saveVariable<viewmodel_mod_vars::OffsetZ>());
        configConversion.endObject();

        // FrameworkCS2 port batch (appended LAST - the loader is an order-sensitive streaming
        // parser and removed keys stay as parse-and-discard placeholders).
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
        // absolute PosX/PosY replaced by OffsetX/Y (the watermark slider pattern) - the old keys
        // stay as parse-and-discard placeholders per the config-loader ordering rule
        configConversion.floating(u8"PosX", [](float) {}, [] { return 0.0f; });
        configConversion.floating(u8"PosY", [](float) {}, [] { return 0.0f; });
        configConversion.floating(u8"OffsetX", loadVariable<PlayerListOffsetX>(), saveVariable<PlayerListOffsetX>());
        configConversion.floating(u8"OffsetY", loadVariable<PlayerListOffsetY>(), saveVariable<PlayerListOffsetY>());
        configConversion.endObject();

        // NEW keys go LAST in an object - appending keeps old config files parsing.
        configConversion.beginObject(u8"GrenadeTimers");
        configConversion.boolean(u8"Enabled", loadVariable<grenade_timers_vars::Enabled>(), saveVariable<grenade_timers_vars::Enabled>());
        configConversion.boolean(u8"SmokeTimers", loadVariable<grenade_timers_vars::SmokeTimers>(), saveVariable<grenade_timers_vars::SmokeTimers>());
        configConversion.boolean(u8"MolotovTimers", loadVariable<grenade_timers_vars::MolotovTimers>(), saveVariable<grenade_timers_vars::MolotovTimers>());
        configConversion.endObject();

        configConversion.endObject();
    }

    // Menu UI theme (Neverlose menu colors). Own top-level object, appended LAST.
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
        configConversion.endObject();
        configConversion.endObject();
    }

    // Experiment instrumentation, kept in its own top-level object so features added here later
    // don't have to be shoehorned into Sound/Visuals just to be persisted.
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
        configConversion.endObject();

        configConversion.beginObject(u8"Bunnyhop");
        configConversion.boolean(u8"Enabled", loadVariable<BunnyhopEnabled>(), saveVariable<BunnyhopEnabled>());
        configConversion.boolean(u8"AutoStrafe", loadVariable<AutoStrafeEnabled>(), saveVariable<AutoStrafeEnabled>());
        configConversion.boolean(u8"TestStrafer", loadVariable<TestStraferEnabled>(), saveVariable<TestStraferEnabled>());
        configConversion.endObject();

        // NEW keys go LAST in an object - appending keeps old config files parsing.
        configConversion.beginObject(u8"Movement");
        configConversion.boolean(u8"EdgeJump", loadVariable<movement_vars::EdgeJump>(), saveVariable<movement_vars::EdgeJump>());
        configConversion.boolean(u8"EdgeStop", loadVariable<movement_vars::EdgeStop>(), saveVariable<movement_vars::EdgeStop>());
        configConversion.boolean(u8"SlowWalk", loadVariable<movement_vars::SlowWalk>(), saveVariable<movement_vars::SlowWalk>());
        configConversion.uint(u8"SlowWalkSpeed", loadVariable<movement_vars::SlowWalkSpeed>(), saveVariable<movement_vars::SlowWalkSpeed>());
        configConversion.boolean(u8"FastLadder", loadVariable<movement_vars::FastLadder>(), saveVariable<movement_vars::FastLadder>());
        configConversion.boolean(u8"JumpBug", loadVariable<movement_vars::JumpBug>(), saveVariable<movement_vars::JumpBug>());
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
        configConversion.endObject();

        configConversion.beginObject(u8"SpawnProtection");
        configConversion.boolean(u8"Enabled", loadVariable<SpawnProtectionSoundEnabled>(), saveVariable<SpawnProtectionSoundEnabled>());
        configConversion.endObject();

        configConversion.beginObject(u8"Visualizations");

        // ImpactMarkers / BulletTracers / GrenadeTrajectory / OffScreenArrows (velocity visual
        // ports, removed with the Velocity ESP tab) stay as parse-and-discard placeholders
        // because the config loader is an order-sensitive streaming parser.
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

        // Chams (velocity visual port) was removed; the object stays as a parse-and-discard
        // placeholder because the config loader is an order-sensitive streaming parser.
        configConversion.beginObject(u8"Chams");
        configConversion.boolean(u8"Enabled", [](bool) {}, [] { return false; });
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
                return static_cast<typename ConfigVariable::ValueType::ValueType>(GET_CONFIG_VAR(ConfigVariable));
            };
        } else if constexpr (std::is_enum_v<typename ConfigVariable::ValueType>) {
            return [this] {
                return static_cast<std::underlying_type_t<typename ConfigVariable::ValueType>>(GET_CONFIG_VAR(ConfigVariable));
            };
        } else {
            return [this] {
                return GET_CONFIG_VAR(ConfigVariable);
            };
        }
    }

    HookContext& hookContext;
};
