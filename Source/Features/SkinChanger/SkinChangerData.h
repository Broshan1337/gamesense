#pragma once

#include <optional>

#include <CS2/Econ/ItemDefinitionIndex.h>
#include <CS2/Econ/PaintKitDatabase.h>
#include <Features/SkinChanger/SkinChangerConfigVariables.h>
#include <HookContext/HookContextMacros.h>

// Weapon -> configured skin. The curated whitelist is GONE: selections are raw paint kit ids
// validated against PaintKitDatabase (generated from the game's own items_game.txt), which
// covers every finish the game itself allows on each weapon. The database IS the weapon/skin
// compatibility validation - the UI can only ever select from a weapon's list, and the apply
// path re-validates before writing (an incompatible weapon+kit combination SIGFPEs the
// composite-material system).
class SkinChangerData {
public:
    // Paint kit id for a weapon's config var, with the three cases the caller needs kept apart:
    //   nullopt     -> this weapon has no config variable (not in our tables) - leave alone
    //   Some(0)     -> user chose "None" - any applied skin must be REVERTED
    //   Some(kitId) -> apply (the CALLER validates against paintKitListFor before writing)
    [[nodiscard]] static std::optional<int> configuredPaintKitId(auto& hookContext, cs2::ItemDefinitionIndex weapon) noexcept
    {
        using namespace skin_changer_vars;
        switch (weapon) {
        case cs2::ItemDefinitionIndex::M4A4: return static_cast<int>(GET_CONFIG_VAR(M4A4Skin));
        case cs2::ItemDefinitionIndex::AK47: return static_cast<int>(GET_CONFIG_VAR(AK47Skin));
        case cs2::ItemDefinitionIndex::AWP: return static_cast<int>(GET_CONFIG_VAR(AWPSkin));
        case cs2::ItemDefinitionIndex::DesertEagle: return static_cast<int>(GET_CONFIG_VAR(DesertEagleSkin));
        case cs2::ItemDefinitionIndex::USPS: return static_cast<int>(GET_CONFIG_VAR(USPSSkin));
        case cs2::ItemDefinitionIndex::Glock18: return static_cast<int>(GET_CONFIG_VAR(Glock18Skin));
        case cs2::ItemDefinitionIndex::M249: return static_cast<int>(GET_CONFIG_VAR(M249Skin));
        case cs2::ItemDefinitionIndex::XM1014: return static_cast<int>(GET_CONFIG_VAR(XM1014Skin));
        case cs2::ItemDefinitionIndex::MAG7: return static_cast<int>(GET_CONFIG_VAR(MAG7Skin));
        case cs2::ItemDefinitionIndex::Negev: return static_cast<int>(GET_CONFIG_VAR(NegevSkin));
        case cs2::ItemDefinitionIndex::SawedOff: return static_cast<int>(GET_CONFIG_VAR(SawedOffSkin));
        case cs2::ItemDefinitionIndex::Nova: return static_cast<int>(GET_CONFIG_VAR(NovaSkin));
        case cs2::ItemDefinitionIndex::DualBerettas: return static_cast<int>(GET_CONFIG_VAR(DualBerettasSkin));
        case cs2::ItemDefinitionIndex::FiveSeveN: return static_cast<int>(GET_CONFIG_VAR(FiveSeveNSkin));
        case cs2::ItemDefinitionIndex::Tec9: return static_cast<int>(GET_CONFIG_VAR(Tec9Skin));
        case cs2::ItemDefinitionIndex::P2000: return static_cast<int>(GET_CONFIG_VAR(P2000Skin));
        case cs2::ItemDefinitionIndex::P250: return static_cast<int>(GET_CONFIG_VAR(P250Skin));
        case cs2::ItemDefinitionIndex::CZ75Auto: return static_cast<int>(GET_CONFIG_VAR(CZ75AutoSkin));
        case cs2::ItemDefinitionIndex::R8Revolver: return static_cast<int>(GET_CONFIG_VAR(R8RevolverSkin));
        case cs2::ItemDefinitionIndex::AUG: return static_cast<int>(GET_CONFIG_VAR(AUGSkin));
        case cs2::ItemDefinitionIndex::Famas: return static_cast<int>(GET_CONFIG_VAR(FamasSkin));
        case cs2::ItemDefinitionIndex::GalilAR: return static_cast<int>(GET_CONFIG_VAR(GalilARSkin));
        case cs2::ItemDefinitionIndex::SG553: return static_cast<int>(GET_CONFIG_VAR(SG553Skin));
        case cs2::ItemDefinitionIndex::M4A1S: return static_cast<int>(GET_CONFIG_VAR(M4A1SSkin));
        case cs2::ItemDefinitionIndex::MAC10: return static_cast<int>(GET_CONFIG_VAR(MAC10Skin));
        case cs2::ItemDefinitionIndex::P90: return static_cast<int>(GET_CONFIG_VAR(P90Skin));
        case cs2::ItemDefinitionIndex::MP5SD: return static_cast<int>(GET_CONFIG_VAR(MP5SDSkin));
        case cs2::ItemDefinitionIndex::UMP45: return static_cast<int>(GET_CONFIG_VAR(UMP45Skin));
        case cs2::ItemDefinitionIndex::PPBizon: return static_cast<int>(GET_CONFIG_VAR(PPBizonSkin));
        case cs2::ItemDefinitionIndex::MP7: return static_cast<int>(GET_CONFIG_VAR(MP7Skin));
        case cs2::ItemDefinitionIndex::MP9: return static_cast<int>(GET_CONFIG_VAR(MP9Skin));
        case cs2::ItemDefinitionIndex::G3SG1: return static_cast<int>(GET_CONFIG_VAR(G3SG1Skin));
        case cs2::ItemDefinitionIndex::SCAR20: return static_cast<int>(GET_CONFIG_VAR(SCAR20Skin));
        case cs2::ItemDefinitionIndex::SSG08: return static_cast<int>(GET_CONFIG_VAR(SSG08Skin));
        default: return std::nullopt;
        }
    }

    [[nodiscard]] static std::pair<int, int> configuredWearAndSeed(auto& hookContext, cs2::ItemDefinitionIndex weapon) noexcept
    {
        using namespace skin_changer_vars;
        switch (weapon) {
        case cs2::ItemDefinitionIndex::M4A4: return {GET_CONFIG_VAR(M4A4SkinWear), GET_CONFIG_VAR(M4A4SkinSeed)};
        case cs2::ItemDefinitionIndex::AK47: return {GET_CONFIG_VAR(AK47SkinWear), GET_CONFIG_VAR(AK47SkinSeed)};
        case cs2::ItemDefinitionIndex::AWP: return {GET_CONFIG_VAR(AWPSkinWear), GET_CONFIG_VAR(AWPSkinSeed)};
        case cs2::ItemDefinitionIndex::DesertEagle: return {GET_CONFIG_VAR(DesertEagleSkinWear), GET_CONFIG_VAR(DesertEagleSkinSeed)};
        case cs2::ItemDefinitionIndex::USPS: return {GET_CONFIG_VAR(USPSSkinWear), GET_CONFIG_VAR(USPSSkinSeed)};
        case cs2::ItemDefinitionIndex::Glock18: return {GET_CONFIG_VAR(Glock18SkinWear), GET_CONFIG_VAR(Glock18SkinSeed)};
        case cs2::ItemDefinitionIndex::M249: return {GET_CONFIG_VAR(M249SkinWear), GET_CONFIG_VAR(M249SkinSeed)};
        case cs2::ItemDefinitionIndex::XM1014: return {GET_CONFIG_VAR(XM1014SkinWear), GET_CONFIG_VAR(XM1014SkinSeed)};
        case cs2::ItemDefinitionIndex::MAG7: return {GET_CONFIG_VAR(MAG7SkinWear), GET_CONFIG_VAR(MAG7SkinSeed)};
        case cs2::ItemDefinitionIndex::Negev: return {GET_CONFIG_VAR(NegevSkinWear), GET_CONFIG_VAR(NegevSkinSeed)};
        case cs2::ItemDefinitionIndex::SawedOff: return {GET_CONFIG_VAR(SawedOffSkinWear), GET_CONFIG_VAR(SawedOffSkinSeed)};
        case cs2::ItemDefinitionIndex::Nova: return {GET_CONFIG_VAR(NovaSkinWear), GET_CONFIG_VAR(NovaSkinSeed)};
        case cs2::ItemDefinitionIndex::DualBerettas: return {GET_CONFIG_VAR(DualBerettasSkinWear), GET_CONFIG_VAR(DualBerettasSkinSeed)};
        case cs2::ItemDefinitionIndex::FiveSeveN: return {GET_CONFIG_VAR(FiveSeveNSkinWear), GET_CONFIG_VAR(FiveSeveNSkinSeed)};
        case cs2::ItemDefinitionIndex::Tec9: return {GET_CONFIG_VAR(Tec9SkinWear), GET_CONFIG_VAR(Tec9SkinSeed)};
        case cs2::ItemDefinitionIndex::P2000: return {GET_CONFIG_VAR(P2000SkinWear), GET_CONFIG_VAR(P2000SkinSeed)};
        case cs2::ItemDefinitionIndex::P250: return {GET_CONFIG_VAR(P250SkinWear), GET_CONFIG_VAR(P250SkinSeed)};
        case cs2::ItemDefinitionIndex::CZ75Auto: return {GET_CONFIG_VAR(CZ75AutoSkinWear), GET_CONFIG_VAR(CZ75AutoSkinSeed)};
        case cs2::ItemDefinitionIndex::R8Revolver: return {GET_CONFIG_VAR(R8RevolverSkinWear), GET_CONFIG_VAR(R8RevolverSkinSeed)};
        case cs2::ItemDefinitionIndex::AUG: return {GET_CONFIG_VAR(AUGSkinWear), GET_CONFIG_VAR(AUGSkinSeed)};
        case cs2::ItemDefinitionIndex::Famas: return {GET_CONFIG_VAR(FamasSkinWear), GET_CONFIG_VAR(FamasSkinSeed)};
        case cs2::ItemDefinitionIndex::GalilAR: return {GET_CONFIG_VAR(GalilARSkinWear), GET_CONFIG_VAR(GalilARSkinSeed)};
        case cs2::ItemDefinitionIndex::SG553: return {GET_CONFIG_VAR(SG553SkinWear), GET_CONFIG_VAR(SG553SkinSeed)};
        case cs2::ItemDefinitionIndex::M4A1S: return {GET_CONFIG_VAR(M4A1SSkinWear), GET_CONFIG_VAR(M4A1SSkinSeed)};
        case cs2::ItemDefinitionIndex::MAC10: return {GET_CONFIG_VAR(MAC10SkinWear), GET_CONFIG_VAR(MAC10SkinSeed)};
        case cs2::ItemDefinitionIndex::P90: return {GET_CONFIG_VAR(P90SkinWear), GET_CONFIG_VAR(P90SkinSeed)};
        case cs2::ItemDefinitionIndex::MP5SD: return {GET_CONFIG_VAR(MP5SDSkinWear), GET_CONFIG_VAR(MP5SDSkinSeed)};
        case cs2::ItemDefinitionIndex::UMP45: return {GET_CONFIG_VAR(UMP45SkinWear), GET_CONFIG_VAR(UMP45SkinSeed)};
        case cs2::ItemDefinitionIndex::PPBizon: return {GET_CONFIG_VAR(PPBizonSkinWear), GET_CONFIG_VAR(PPBizonSkinSeed)};
        case cs2::ItemDefinitionIndex::MP7: return {GET_CONFIG_VAR(MP7SkinWear), GET_CONFIG_VAR(MP7SkinSeed)};
        case cs2::ItemDefinitionIndex::MP9: return {GET_CONFIG_VAR(MP9SkinWear), GET_CONFIG_VAR(MP9SkinSeed)};
        case cs2::ItemDefinitionIndex::G3SG1: return {GET_CONFIG_VAR(G3SG1SkinWear), GET_CONFIG_VAR(G3SG1SkinSeed)};
        case cs2::ItemDefinitionIndex::SCAR20: return {GET_CONFIG_VAR(SCAR20SkinWear), GET_CONFIG_VAR(SCAR20SkinSeed)};
        case cs2::ItemDefinitionIndex::SSG08: return {GET_CONFIG_VAR(SSG08SkinWear), GET_CONFIG_VAR(SSG08SkinSeed)};
        default: return {10, 0}; // 0.010 wear / seed 0 - the previously-hardcoded values
        }
    }

    // Knife MODEL selection - which knife type to impersonate. Unlike the gun path there is no
    // per-weapon keying: a player carries exactly one knife and this replaces its type outright,
    // so one global setting is the whole model. Returns nullopt when the feature is off.
    [[nodiscard]] static std::optional<cs2::ItemDefinitionIndex> resolveKnifeModel(KnifeModelSelection selection) noexcept
    {
        switch (selection) {
        case KnifeModelSelection::Bayonet: return cs2::ItemDefinitionIndex::Bayonet;
        case KnifeModelSelection::BowieKnife: return cs2::ItemDefinitionIndex::BowieKnife;
        case KnifeModelSelection::ButterflyKnife: return cs2::ItemDefinitionIndex::ButterflyKnife;
        case KnifeModelSelection::ClassicKnife: return cs2::ItemDefinitionIndex::ClassicKnife;
        case KnifeModelSelection::FalchionKnife: return cs2::ItemDefinitionIndex::FalchionKnife;
        case KnifeModelSelection::FlipKnife: return cs2::ItemDefinitionIndex::FlipKnife;
        case KnifeModelSelection::GutKnife: return cs2::ItemDefinitionIndex::GutKnife;
        case KnifeModelSelection::HuntsmanKnife: return cs2::ItemDefinitionIndex::HuntsmanKnife;
        case KnifeModelSelection::Karambit: return cs2::ItemDefinitionIndex::Karambit;
        case KnifeModelSelection::KukriKnife: return cs2::ItemDefinitionIndex::KukriKnife;
        case KnifeModelSelection::M9Bayonet: return cs2::ItemDefinitionIndex::M9Bayonet;
        case KnifeModelSelection::NavajaKnife: return cs2::ItemDefinitionIndex::NavajaKnife;
        case KnifeModelSelection::NomadKnife: return cs2::ItemDefinitionIndex::NomadKnife;
        case KnifeModelSelection::ParacordKnife: return cs2::ItemDefinitionIndex::ParacordKnife;
        case KnifeModelSelection::ShadowDaggers: return cs2::ItemDefinitionIndex::ShadowDaggers;
        case KnifeModelSelection::SkeletonKnife: return cs2::ItemDefinitionIndex::SkeletonKnife;
        case KnifeModelSelection::StilettoKnife: return cs2::ItemDefinitionIndex::StilettoKnife;
        case KnifeModelSelection::SurvivalKnife: return cs2::ItemDefinitionIndex::SurvivalKnife;
        case KnifeModelSelection::TalonKnife: return cs2::ItemDefinitionIndex::TalonKnife;
        case KnifeModelSelection::UrsusKnife: return cs2::ItemDefinitionIndex::UrsusKnife;
        default: return std::nullopt;
        }
    }

    [[nodiscard]] static std::optional<cs2::ItemDefinitionIndex> configuredKnifeModel(auto& hookContext) noexcept
    {
        return resolveKnifeModel(GET_CONFIG_VAR(skin_changer_vars::KnifeModel));
    }

    // Knife finish - a raw paint kit id (0 = vanilla knife). Some finishes are model-exclusive
    // (Lore / Black Laminate / Autotronic / Ultraviolet ship as per-model kits), so the id is
    // validated against the IMPERSONATED model's PaintKitDatabase list by the caller; the
    // validation collapses to the shared generic set when no model is selected.
    [[nodiscard]] static int configuredKnifePaintKitId(auto& hookContext) noexcept
    {
        return static_cast<int>(GET_CONFIG_VAR(skin_changer_vars::KnifeSkin));
    }

    [[nodiscard]] static std::pair<int, int> configuredKnifeWearAndSeed(auto& hookContext) noexcept
    {
        return {GET_CONFIG_VAR(skin_changer_vars::KnifeSkinWear), GET_CONFIG_VAR(skin_changer_vars::KnifeSkinSeed)};
    }

    // Validates a paint kit id against a weapon's list. Weapon compat: kit ids that don't
    // belong to the weapon (e.g. a Glock finish on an AK, or a karambit-only finish on a
    // generic knife) must never reach the engine.
    [[nodiscard]] static bool isPaintKitValidFor(cs2::ItemDefinitionIndex weapon, int kitId) noexcept
    {
        if (kitId <= 0)
            return false;
        return cs2::paintKitFor(static_cast<std::uint16_t>(weapon), kitId) != nullptr;
    }

    // The finishes every knife can wear (the intersection of all knife lists in
    // PaintKitDatabase). Used when no knife model is impersonated: the real equipped knife type
    // isn't known at config time, and a model-exclusive finish (Lore / Black Laminate /
    // Autotronic / Ultraviolet ship as per-model kits) must never be applied to a knife it
    // doesn't belong to.
    [[nodiscard]] static bool isGenericKnifePaintKit(int kitId) noexcept
    {
        switch (kitId) {
        case 12:  // Crimson Web
        case 38:  // Fade
        case 59:  // Slaughter
        case 409: // Tiger Tooth
        case 410: // Damascus Steel
        case 411: // Damascus Steel (Variant)
        case 413: // Marble Fade
        case 414: // Rust Coat
        case 415: case 416: case 417: // Doppler Ruby / Sapphire / Black Pearl
        case 418: case 419: case 420: case 421: // Doppler Phase 1-4
        case 568: // Gamma Doppler (Emerald)
        case 569: case 570: case 571: case 572: // Gamma Doppler Phase 1-4
        case 617: case 618: case 619: // rare gem variants
            return true;
        default:
            return false;
        }
    }

    // Validates a knife finish: against the impersonated model's list when one is selected,
    // against the generic-knife set otherwise.
    [[nodiscard]] static bool isKnifePaintKitValid(std::optional<cs2::ItemDefinitionIndex> impersonatedModel, int kitId) noexcept
    {
        if (kitId <= 0)
            return false;
        if (impersonatedModel.has_value())
            return isPaintKitValidFor(*impersonatedModel, kitId);
        return isGenericKnifePaintKit(kitId);
    }
};
