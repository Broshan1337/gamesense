#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>










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
    CONFIG_VARIABLE(base##Wear, std::uint16_t, 10);          \
    CONFIG_VARIABLE(base##Seed, std::uint16_t, 0)

namespace skin_changer_vars
{

CONFIG_VARIABLE(KnifeModel, KnifeModelSelection, KnifeModelSelection::None);




NS_SKIN_CHANGER_DEFINE_TRIPLE(KnifeSkin);




CONFIG_VARIABLE(StatTrakEnabled, bool, false);
CONFIG_VARIABLE(StatTrakValue, std::uint16_t, 1337);

#define NS_SKIN_CHANGER_F(base) NS_SKIN_CHANGER_DEFINE_TRIPLE(base);
NS_SKIN_CHANGER_GUNS(NS_SKIN_CHANGER_F)
#undef NS_SKIN_CHANGER_F

}

#undef NS_SKIN_CHANGER_DEFINE_TRIPLE
