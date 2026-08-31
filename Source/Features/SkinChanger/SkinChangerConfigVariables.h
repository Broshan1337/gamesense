#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>

// Ordinal position (None=0, Skin1=1, ...) is exactly the dropdown's selectedIndex - see
// SkinChangerDropdownSelectionChangeHandler.h and SkinChangerData.h::resolvePaintKit.
enum class SkinChangerSelection : std::uint8_t {
    None,
    Skin1,
    Skin2,
    Skin3,
    Skin4,
    Skin5,
    Skin6,
    Skin7,
    Skin8,
    Skin9,
    Skin10
};

// Which knife MODEL to impersonate. Separate from SkinChangerSelection because this picks a
// weapon type rather than a finish, and because there are 20 knives - more entries than that
// enum has. Ordinal position is the dropdown's selectedIndex, same convention as above; the
// order matches the alphabetical order the dropdown lists them in, so the two never drift.
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

namespace skin_changer_vars
{

CONFIG_VARIABLE(KnifeModel, KnifeModelSelection, KnifeModelSelection::None);
CONFIG_VARIABLE(KnifeSkin, SkinChangerSelection, SkinChangerSelection::None);

CONFIG_VARIABLE(M4A4Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(AK47Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(AWPSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(DesertEagleSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(USPSSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(Glock18Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(M249Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(XM1014Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(MAG7Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(NegevSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(SawedOffSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(NovaSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(DualBerettasSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(FiveSeveNSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(Tec9Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(P2000Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(P250Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(CZ75AutoSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(R8RevolverSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(AUGSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(FamasSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(GalilARSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(SG553Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(M4A1SSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(MAC10Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(P90Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(MP5SDSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(UMP45Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(PPBizonSkin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(MP7Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(MP9Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(G3SG1Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(SCAR20Skin, SkinChangerSelection, SkinChangerSelection::None);
CONFIG_VARIABLE(SSG08Skin, SkinChangerSelection, SkinChangerSelection::None);

}
