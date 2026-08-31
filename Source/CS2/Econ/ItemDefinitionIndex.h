#pragma once

#include <cstdint>

namespace cs2
{

// item_definition_index_t
enum class ItemDefinitionIndex : std::uint16_t {
    DesertEagle = 1,
    DualBerettas = 2,
    FiveSeveN = 3,
    Glock18 = 4,
    AK47 = 7,
    AUG = 8,
    AWP = 9,
    Famas = 10,
    G3SG1 = 11,
    GalilAR = 13,
    M249 = 14,
    M4A4 = 16,
    MAC10 = 17,
    P90 = 19,
    MP5SD = 23,
    UMP45 = 24,
    XM1014 = 25,
    PPBizon = 26,
    MAG7 = 27,
    Negev = 28,
    SawedOff = 29,
    Tec9 = 30,
    P2000 = 32,
    MP7 = 33,
    MP9 = 34,
    Nova = 35,
    P250 = 36,
    SCAR20 = 38,
    SG553 = 39,
    SSG08 = 40,
    M4A1S = 60,
    USPS = 61,
    CZ75Auto = 63,
    R8Revolver = 64,
    Flashbang = 43,
    HEGrenade = 44,
    SmokeGrenade = 45,
    Incendiary = 48,

    // Generic "weapon_knife" entity definitions - what a live C_Knife entity's own
    // m_iItemDefinitionIndex reads as before any type override. Several legacy values
    // exist in the schema (era/team-default variants); Knife (42) is the one seen in
    // practice on a currently-equipped knife.
    Knife = 42,
    KnifeT = 59,

    // Specific knife types (item schema economy definitions - what a player's loadout
    // resolves to for "which knife model to show"). Cross-platform, same on every OS.
    Bayonet = 500,
    ClassicKnife = 503,
    FlipKnife = 505,
    GutKnife = 506,
    Karambit = 507,
    M9Bayonet = 508,
    HuntsmanKnife = 509,
    FalchionKnife = 512,
    BowieKnife = 514,
    ButterflyKnife = 515,
    ShadowDaggers = 516,
    ParacordKnife = 517,
    SurvivalKnife = 518,
    UrsusKnife = 519,
    NavajaKnife = 520,
    NomadKnife = 521,
    StilettoKnife = 522,
    TalonKnife = 523,
    SkeletonKnife = 525,
    KukriKnife = 526,

    // Gloves.
    StuddedBloodhoundGloves = 5027,
    SportyGloves = 5030,
    SlickGloves = 5031,
    LeatherHandwraps = 5032,
    MotorcycleGloves = 5033,
    SpecialistGloves = 5034,
    StuddedHydraGloves = 5035,
    StuddedBrokenFangGloves = 4725
};

}
