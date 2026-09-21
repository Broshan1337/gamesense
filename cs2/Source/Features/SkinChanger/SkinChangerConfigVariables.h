#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>

// Which knife MODEL to impersonate. Separate from the paint-kit ids because this picks a
// weapon type rather than a finish, and because there are 20 knives. Ordinal position is the
// dropdown's selectedIndex; the order matches the alphabetical order the dropdown lists them
// in, so the two never drift.
//
// Deliberately a single global setting rather than one entry per knife type: a player only
// ever carries one knife, and this feature REPLACES the knife's type rather than skinning a
// type you already own, so "which knife am I carrying" is the only meaningful question. The
// gun path's per-weapon keying doesn't apply.
enum class KnifeModelSelection : std::uint8_t {
    None,
    Bayonet,
    BowieKnife,
    ButterflyKnife,
    ClassicKnife,
    FalchionKnife,
    FlipKnife,
    GutKnife,
    HuntsmanKnife,
    Karambit,
    KukriKnife,
    M9Bayonet,
    NavajaKnife,
    NomadKnife,
    ParacordKnife,
    ShadowDaggers,
    SkeletonKnife,
    StilettoKnife,
    SurvivalKnife,
    TalonKnife,
    UrsusKnife
};

// Skin selections are now RAW PAINT KIT IDS (see PaintKitDatabase.h), not ordinals into a
// curated list: 0 = None (revert to the weapon's original finish), anything else is a paint
// kit id from the game's own items_game.txt. The apply path validates the id against the
// weapon's PaintKitDatabase list before writing, so a stale/invalid value from an old config
// file can never reach the engine (an incompatible weapon+kit combination SIGFPEs the
// composite-material system).
//
// Wear is stored in PERMILLE (0-1000 -> 0.000-1.000 float the engine expects); Seed is the
// raw pattern seed the engine expects. One of each per weapon - the engine reads them as a
// triple, so "flagship" needs the full triple user-editable, not just the kit.
#define NS_SKIN_CHANGER_GUNS(F)   \
    F(M4A4Skin)                   \
    F(AK47Skin)                   \
    F(AWPSkin)                    \
    F(DesertEagleSkin)            \
    F(USPSSkin)                   \
    F(Glock18Skin)                \
    F(M249Skin)                   \
    F(XM1014Skin)                 \
    F(MAG7Skin)                   \
    F(NegevSkin)                  \
    F(SawedOffSkin)               \
    F(NovaSkin)                   \
    F(DualBerettasSkin)           \
    F(FiveSeveNSkin)              \
    F(Tec9Skin)                   \
    F(P2000Skin)                  \
    F(P250Skin)                   \
    F(CZ75AutoSkin)               \
    F(R8RevolverSkin)             \
    F(AUGSkin)                    \
    F(FamasSkin)                  \
    F(GalilARSkin)                \
    F(SG553Skin)                  \
    F(M4A1SSkin)                  \
    F(MAC10Skin)                  \
    F(P90Skin)                    \
    F(MP5SDSkin)                  \
    F(UMP45Skin)                  \
    F(PPBizonSkin)                \
    F(MP7Skin)                    \
    F(MP9Skin)                    \
    F(G3SG1Skin)                  \
    F(SCAR20Skin)                 \
    F(SSG08Skin)

#define NS_SKIN_CHANGER_DEFINE_TRIPLE(base)                                            \
    CONFIG_VARIABLE(base, std::uint16_t, 0);                                           \
    CONFIG_VARIABLE(base##Wear, std::uint16_t, 10);  /* permille, 10 = 0.010 */        \
    CONFIG_VARIABLE(base##Seed, std::uint16_t, 0)

namespace skin_changer_vars
{

CONFIG_VARIABLE(KnifeModel, KnifeModelSelection, KnifeModelSelection::None);

// Knife finish (paint kit id, 0 = vanilla knife) + wear/seed. Valid ids depend on the
// IMPERSONATED knife model (some finishes are model-exclusive) - the UI list follows the
// KnifeModel selection and the apply path validates against the impersonated model.
NS_SKIN_CHANGER_DEFINE_TRIPLE(KnifeSkin);

// StatTrak is global (one knife + whatever gun is held; per-weapon counters were overkill):
// when enabled, weapons apply with m_iEntityQuality = 9 (STRANGE) and the fallback StatTrak
// counter set to the value below.
CONFIG_VARIABLE(StatTrakEnabled, bool, false);
CONFIG_VARIABLE(StatTrakValue, std::uint16_t, 1337);

#define NS_SKIN_CHANGER_F(base) NS_SKIN_CHANGER_DEFINE_TRIPLE(base);
NS_SKIN_CHANGER_GUNS(NS_SKIN_CHANGER_F)
#undef NS_SKIN_CHANGER_F

}

#undef NS_SKIN_CHANGER_DEFINE_TRIPLE
