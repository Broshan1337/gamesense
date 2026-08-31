#pragma once

#include <optional>

#include <CS2/Econ/ItemDefinitionIndex.h>
#include <CS2/Econ/PaintKitIndex.h>
#include <Features/SkinChanger/SkinChangerConfigVariables.h>
#include <HookContext/HookContextMacros.h>

// Curated weapon -> skin whitelist. Every entry here is either empirically confirmed live
// in-game (AsiimovM4A4) or sourced directly from the public CS2 item schema and cross-checked
// against that confirmed data point (see PaintKitIndex.h) - never write a combination here
// that hasn't been verified as a real, weapon-appropriate finish. This whitelist IS the
// weapon/skin compatibility validation: the UI can only ever select from what's listed here.
class SkinChangerData {
public:
    [[nodiscard]] static std::optional<cs2::PaintKitIndex> resolvePaintKit(cs2::ItemDefinitionIndex weapon, SkinChangerSelection selection) noexcept
    {
        if (selection == SkinChangerSelection::None)
            return std::nullopt;

        switch (weapon) {
        case cs2::ItemDefinitionIndex::M4A4:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::AsiimovM4A4;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::M4A4Daybreak;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::M4A4BulletRain;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::M4A4Mainframe;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::M4A4GlobalOffensive;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::M4A4Howl;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::M4A4DragonKing;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::M4A4CyberSecurity;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::M4A4DesolateSpace;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::M4A4PolyMag;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::AK47:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::AK47TheOutsiders;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::AK47Hydroponic;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::AK47SearingRage;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::AK47AUTOEXEC;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::AK47CraneFlight;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::AK47ConsequenceOfTheJinn;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::AK47TheOligarch;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::AK47Inheritance;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::AK47Cartel;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::AK47PhantomDisruptor;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::AWP:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::AWPGraphite;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::AWPWormGod;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::AWPManOWar;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::AWPFade;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::AWPPAW;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::AWPLightningStrike;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::AWPSilkTiger;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::AWPBlackBox;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::AWPIceCoaled;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::AWPLongdog;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::DesertEagle:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::DesertEagleBlaze;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::DesertEagleHypnotic;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::DesertEagleCobaltDisruption;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::DesertEagleBronzeDeco;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::DesertEagleMeteorite;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::DesertEagleNightHeist;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::DesertEagleEmeraldJrmungandr;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::DesertEagleTheBronze;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::DesertEagleGoldenKoi;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::DesertEagleSunsetStorm;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::USPS:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::USPSSerum;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::USPSStainless;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::USPSDarkWater;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::USPSPurpleDDPAT;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::USPSTargetAcquired;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::USPSOrangeAnolis;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::USPSCaiman;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::USPSBusinessClass;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::USPSBlackLotus;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::USPSCortex;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::Glock18:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::Glock18Fade;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::Glock18DragonTattoo;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::Glock18SteelDisruption;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::Glock18Moonrise;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::Glock18HighBeam;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::Glock18TwilightGalaxy;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::Glock18Reactor;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::Glock18NuclearGarden;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::Glock18Brass;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::Glock18BunsenBurner;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::M249:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::M249Aztec;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::M249Magma;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::M249DeepRelief;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::M249Downtown;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::M249Submerged;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::M249SystemLock;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::M249Spectre;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::M249OSIPR;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::M249NebulaCrusader;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::M249Warbird;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::XM1014:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::XM1014AncientLore;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::XM1014Charter;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::XM1014FrostBorre;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::XM1014ElegantVines;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::XM1014BoneMachine;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::XM1014ZombieOffensive;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::XM1014BlueSteel;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::XM1014TecluBurner;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::XM1014XOXO;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::XM1014Scumbria;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::MAG7:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::MAG7CarbonFiber;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::MAG7Chainmail;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::MAG7HardWater;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::MAG7Sonar;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::MAG7NavySheen;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::MAG7MetallicDDPAT;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::MAG7Silver;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::MAG7SWAG7;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::MAG7RustCoat;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::MAG7HeavenGuard;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::Negev:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::NegevArmySheen;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::NegevManOWar;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::NegevAnodizedNavy;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::NegevBratatat;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::NegevLoudmouth;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::NegevDropMe;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::NegevDevTexture;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::NegevPowerLoader;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::NegevPrototype;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::NegevDesertStrike;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::SawedOff:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::SawedOffAmberFade;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::SawedOffBrakeLight;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::SawedOffCopper;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::SawedOffMorris;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::SawedOffHighwayman;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::SawedOffZander;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::SawedOffRustCoat;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::SawedOffFirstClass;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::SawedOffLimelight;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::SawedOffApocalypto;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::Nova:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::NovaArmySheen;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::NovaGraphite;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::NovaRedQuartz;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::NovaGila;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::NovaCagedSteel;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::NovaBaroqueOrange;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::NovaExo;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::NovaRustCoat;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::NovaAntique;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::NovaPlume;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::DualBerettas:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::DualBerettasCobaltQuartz;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::DualBerettasHeist;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::DualBerettasHemoglobin;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::DualBerettasEmerald;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::DualBerettasAnodizedNavy;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::DualBerettasCartel;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::DualBerettasStained;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::DualBerettasFloraCarnivora;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::DualBerettasTwinTurbo;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::DualBerettasDualingDragons;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::FiveSeveN:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::FiveSeveNBerriesAndCherries;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::FiveSeveNCopperGalaxy;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::FiveSeveNSilverQuartz;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::FiveSeveNAnodizedGunmetal;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::FiveSeveNFowlPlay;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::FiveSeveNHeatTreated;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::FiveSeveNScumbria;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::FiveSeveNAngryMob;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::FiveSeveNViolentDaimyo;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::FiveSeveNFairyTale;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::Tec9:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::Tec9RedQuartz;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::Tec9TitaniumBit;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::Tec9Ossified;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::Tec9ReEntry;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::Tec9IceCap;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::Tec9BlueTitanium;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::Tec9Brass;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::Tec9CutOut;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::Tec9Isaac;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::Tec9Avalanche;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::P2000:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::P2000AmberFade;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::P2000SpaceRace;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::P2000PantherCamo;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::P2000Chainmail;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::P2000Dispatch;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::P2000OceanFoam;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::P2000Imperial;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::P2000Scorpion;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::P2000Silver;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::P2000AcidEtched;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::P250:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::P250Nevermore;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::P250DigitalArchitect;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::P250SteelDisruption;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::P250Undertow;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::P250Ripple;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::P250DarkFiligree;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::P250MetallicDDPAT;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::P250Cartel;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::P250Valence;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::P250Verdigris;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::CZ75Auto:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::CZ75AutoArmySheen;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::CZ75AutoCopperFiber;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::CZ75AutoEmeraldQuartz;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::CZ75AutoPolymer;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::CZ75AutoTreadPlate;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::CZ75AutoTheFuschiaIsNow;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::CZ75AutoTwist;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::CZ75AutoPoisonDart;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::CZ75AutoChalice;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::CZ75AutoEmerald;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::R8Revolver:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::R8RevolverAmberFade;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::R8RevolverFade;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::R8RevolverBlaze;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::R8RevolverPhoenixMarker;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::R8RevolverLeafhopper;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::R8RevolverReboot;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::R8RevolverSurvivalist;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::R8RevolverSkullCrusher;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::R8RevolverBananaCannon;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::R8RevolverBoneForged;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::AUG:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::AUGAmberFade;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::AUGDeathByPuppy;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::AUGRicochet;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::AUGMidnightLily;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::AUGRandomAccess;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::AUGSurveillance;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::AUGCarvedJade;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::AUGFlameJrmungandr;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::AUGAnodizedNavy;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::AUGHotRod;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::Famas:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::FamasFaultyWiring;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::FamasNeuralNet;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::FamasMeltdown;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::FamasStyx;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::FamasPrimeConspiracy;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::FamasDarkWater;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::FamasSergeant;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::FamasValence;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::FamasDjinn;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::FamasAfterimage;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::GalilAR:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::GalilARAmberFade;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::GalilARAquaTerrace;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::GalilARBlueTitanium;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::GalilARRainbowSpoon;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::GalilARCerberus;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::GalilARChatterbox;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::GalilARBlackSand;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::GalilARSugarRush;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::GalilARChromaticAberration;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::GalilARDestroyer;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::SG553:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::SG553DesertBlossom;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::SG553LushRuins;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::SG553Hypnotic;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::SG553ArmySheen;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::SG553AnodizedNavy;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::SG553DamascusSteel;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::SG553Traveler;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::SG553Aerial;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::SG553Atlas;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::SG553HazardPay;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::M4A1S:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::M4A1SFade;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::M4A1SMossQuartz;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::M4A1SAtomicAlloy;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::M4A1SBluePhosphor;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::M4A1SKnight;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::M4A1SDarkWater;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::M4A1SHotRod;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::M4A1SBasilisk;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::M4A1SGuardian;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::M4A1SMasterPiece;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::MAC10:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::MAC10Fade;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::MAC10AmberFade;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::MAC10LastDive;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::MAC10GoldBrick;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::MAC10CopperBorre;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::MAC10Aloha;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::MAC10LapisGator;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::MAC10Malachite;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::MAC10Oceanic;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::MAC10NuclearGarden;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::P90:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::P90AncientEarth;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::P90AstralJormungandr;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::P90ColdBlooded;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::P90TigerPit;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::P90BaroqueRed;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::P90Module;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::P90Leather;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::P90DeathByKitty;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::P90EmeraldDragon;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::P90RunAndHide;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::MP5SD:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::MP5SDCoProcessor;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::MP5SDDesertStrike;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::MP5SDLiquidation;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::MP5SDConditionZero;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::MP5SDAcidWash;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::MP5SDAgent;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::MP5SDPhosphor;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::MP5SDNecroJr;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::MP5SDOxideOasis;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::MP5SDGauss;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::UMP45:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::UMP45Mechanism;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::UMP45Fade;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::UMP45Blaze;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::UMP45Moonrise;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::UMP45CarbonFiber;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::UMP45Oscillator;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::UMP45GrandPrix;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::UMP45MetalFlowers;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::UMP45MinotaursLabyrinth;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::UMP45Briefing;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::PPBizon:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::PPBizonBreakerBox;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::PPBizonCarbonFiber;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::PPBizonCobaltHalftone;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::PPBizonBrass;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::PPBizonRustCoat;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::PPBizonRMX;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::PPBizonTraitor;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::PPBizonOsiris;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::PPBizonHighRoller;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::PPBizonAntique;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::MP7:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::MP7Fade;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::MP7Motherboard;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::MP7VaultHeist;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::MP7OceanFoam;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::MP7AnodizedNavy;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::MP7ArmorCore;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::MP7UrbanHazard;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::MP7SpecialDelivery;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::MP7AbyssalApparition;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::MP7Guerrilla;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::MP9:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::MP9SandScale;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::MP9MountFuji;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::MP9PandorasBox;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::MP9Hypnotic;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::MP9ArmySheen;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::MP9DarkAge;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::MP9MusicBox;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::MP9Bioleak;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::MP9RubyPoisonDart;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::MP9StainedGlass;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::G3SG1:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::G3SG1AncientRitual;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::G3SG1Murky;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::G3SG1VioletMurano;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::G3SG1Chronos;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::G3SG1BlackSand;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::G3SG1TheExecutioner;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::G3SG1DreamGlade;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::G3SG1KeepingTabs;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::G3SG1HighSeas;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::G3SG1Hunter;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::SCAR20:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::SCAR20ArmySheen;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::SCAR20CarbonFiber;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::SCAR20Emerald;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::SCAR20Brass;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::SCAR20Grotto;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::SCAR20Blueprint;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::SCAR20WildBerry;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::SCAR20Cardiac;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::SCAR20Assault;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::SCAR20Cyrex;
            default: return std::nullopt;
            }
        case cs2::ItemDefinitionIndex::SSG08:
            switch (selection) {
            case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::SSG08AcidFade;
            case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::SSG08CarbonFiber;
            case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::SSG08ThreatDetected;
            case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::SSG08DarkWater;
            case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::SSG08Abyss;
            case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::SSG08BloodInTheWater;
            case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::SSG08Parallax;
            case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::SSG08DeathsHead;
            case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::SSG08Dragonfire;
            case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::SSG08FeverDream;
            default: return std::nullopt;
            }
        default:
            return std::nullopt;
        }
    }

    // Reads the persisted dropdown selection for a weapon def index, if this weapon is one of
    // the curated ones with a config variable at all.
    //
    // Deliberately returns the SELECTION rather than a resolved paint kit, because the caller
    // needs to tell two cases apart that a plain optional<PaintKitIndex> collapses into one:
    //   - nullopt          -> this weapon isn't curated, we have no business touching it
    //   - Some(None)       -> curated, but the user explicitly chose "None", so any skin we
    //                         previously applied must be REVERTED
    // Returning only a paint kit made both look identical, which is why selecting "None" used
    // to silently leave the last applied skin on the weapon forever.
    [[nodiscard]] static std::optional<SkinChangerSelection> configuredSelection(auto& hookContext, cs2::ItemDefinitionIndex weapon) noexcept
    {
        switch (weapon) {
        case cs2::ItemDefinitionIndex::M4A4: return GET_CONFIG_VAR(skin_changer_vars::M4A4Skin);
        case cs2::ItemDefinitionIndex::AK47: return GET_CONFIG_VAR(skin_changer_vars::AK47Skin);
        case cs2::ItemDefinitionIndex::AWP: return GET_CONFIG_VAR(skin_changer_vars::AWPSkin);
        case cs2::ItemDefinitionIndex::DesertEagle: return GET_CONFIG_VAR(skin_changer_vars::DesertEagleSkin);
        case cs2::ItemDefinitionIndex::USPS: return GET_CONFIG_VAR(skin_changer_vars::USPSSkin);
        case cs2::ItemDefinitionIndex::Glock18: return GET_CONFIG_VAR(skin_changer_vars::Glock18Skin);
        case cs2::ItemDefinitionIndex::M249: return GET_CONFIG_VAR(skin_changer_vars::M249Skin);
        case cs2::ItemDefinitionIndex::XM1014: return GET_CONFIG_VAR(skin_changer_vars::XM1014Skin);
        case cs2::ItemDefinitionIndex::MAG7: return GET_CONFIG_VAR(skin_changer_vars::MAG7Skin);
        case cs2::ItemDefinitionIndex::Negev: return GET_CONFIG_VAR(skin_changer_vars::NegevSkin);
        case cs2::ItemDefinitionIndex::SawedOff: return GET_CONFIG_VAR(skin_changer_vars::SawedOffSkin);
        case cs2::ItemDefinitionIndex::Nova: return GET_CONFIG_VAR(skin_changer_vars::NovaSkin);
        case cs2::ItemDefinitionIndex::DualBerettas: return GET_CONFIG_VAR(skin_changer_vars::DualBerettasSkin);
        case cs2::ItemDefinitionIndex::FiveSeveN: return GET_CONFIG_VAR(skin_changer_vars::FiveSeveNSkin);
        case cs2::ItemDefinitionIndex::Tec9: return GET_CONFIG_VAR(skin_changer_vars::Tec9Skin);
        case cs2::ItemDefinitionIndex::P2000: return GET_CONFIG_VAR(skin_changer_vars::P2000Skin);
        case cs2::ItemDefinitionIndex::P250: return GET_CONFIG_VAR(skin_changer_vars::P250Skin);
        case cs2::ItemDefinitionIndex::CZ75Auto: return GET_CONFIG_VAR(skin_changer_vars::CZ75AutoSkin);
        case cs2::ItemDefinitionIndex::R8Revolver: return GET_CONFIG_VAR(skin_changer_vars::R8RevolverSkin);
        case cs2::ItemDefinitionIndex::AUG: return GET_CONFIG_VAR(skin_changer_vars::AUGSkin);
        case cs2::ItemDefinitionIndex::Famas: return GET_CONFIG_VAR(skin_changer_vars::FamasSkin);
        case cs2::ItemDefinitionIndex::GalilAR: return GET_CONFIG_VAR(skin_changer_vars::GalilARSkin);
        case cs2::ItemDefinitionIndex::SG553: return GET_CONFIG_VAR(skin_changer_vars::SG553Skin);
        case cs2::ItemDefinitionIndex::M4A1S: return GET_CONFIG_VAR(skin_changer_vars::M4A1SSkin);
        case cs2::ItemDefinitionIndex::MAC10: return GET_CONFIG_VAR(skin_changer_vars::MAC10Skin);
        case cs2::ItemDefinitionIndex::P90: return GET_CONFIG_VAR(skin_changer_vars::P90Skin);
        case cs2::ItemDefinitionIndex::MP5SD: return GET_CONFIG_VAR(skin_changer_vars::MP5SDSkin);
        case cs2::ItemDefinitionIndex::UMP45: return GET_CONFIG_VAR(skin_changer_vars::UMP45Skin);
        case cs2::ItemDefinitionIndex::PPBizon: return GET_CONFIG_VAR(skin_changer_vars::PPBizonSkin);
        case cs2::ItemDefinitionIndex::MP7: return GET_CONFIG_VAR(skin_changer_vars::MP7Skin);
        case cs2::ItemDefinitionIndex::MP9: return GET_CONFIG_VAR(skin_changer_vars::MP9Skin);
        case cs2::ItemDefinitionIndex::G3SG1: return GET_CONFIG_VAR(skin_changer_vars::G3SG1Skin);
        case cs2::ItemDefinitionIndex::SCAR20: return GET_CONFIG_VAR(skin_changer_vars::SCAR20Skin);
        case cs2::ItemDefinitionIndex::SSG08: return GET_CONFIG_VAR(skin_changer_vars::SSG08Skin);
        default: return std::nullopt;
        }
    }

    // Convenience wrapper: resolved paint kit for a weapon, or nullopt when the weapon isn't
    // curated OR the user chose "None". Callers that need to distinguish those two must use
    // configuredSelection() directly.
    [[nodiscard]] static std::optional<cs2::PaintKitIndex> configuredPaintKit(auto& hookContext, cs2::ItemDefinitionIndex weapon) noexcept
    {
        const auto selection = configuredSelection(hookContext, weapon);
        if (!selection.has_value())
            return std::nullopt;
        return resolvePaintKit(weapon, *selection);
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

    // Knife FINISH selection. Every knife takes the same finish set, so unlike resolvePaintKit()
    // this needs no weapon argument - which is also why these must never leak into the gun path
    // (a knife-exclusive finish on a gun SIGFPEs the composite-material system, see
    // PaintKitIndex.h). Returns nullopt for "None", meaning a vanilla/unpainted knife: the model
    // swap still applies, the finish just doesn't.
    [[nodiscard]] static std::optional<cs2::PaintKitIndex> resolveKnifePaintKit(SkinChangerSelection selection) noexcept
    {
        switch (selection) {
        case SkinChangerSelection::Skin1: return cs2::PaintKitIndex::KnifeFade;
        case SkinChangerSelection::Skin2: return cs2::PaintKitIndex::KnifeMarbleFade;
        case SkinChangerSelection::Skin3: return cs2::PaintKitIndex::KnifeDopplerPhase1;
        case SkinChangerSelection::Skin4: return cs2::PaintKitIndex::KnifeDopplerRuby;
        case SkinChangerSelection::Skin5: return cs2::PaintKitIndex::KnifeDopplerSapphire;
        case SkinChangerSelection::Skin6: return cs2::PaintKitIndex::KnifeDopplerBlackPearl;
        case SkinChangerSelection::Skin7: return cs2::PaintKitIndex::KnifeTigerTooth;
        case SkinChangerSelection::Skin8: return cs2::PaintKitIndex::KnifeDamascusSteel;
        case SkinChangerSelection::Skin9: return cs2::PaintKitIndex::KnifeRustCoat;
        case SkinChangerSelection::Skin10: return cs2::PaintKitIndex::KnifeCrimsonWeb;
        default: return std::nullopt;
        }
    }

    [[nodiscard]] static std::optional<cs2::ItemDefinitionIndex> configuredKnifeModel(auto& hookContext) noexcept
    {
        return resolveKnifeModel(GET_CONFIG_VAR(skin_changer_vars::KnifeModel));
    }

    [[nodiscard]] static std::optional<cs2::PaintKitIndex> configuredKnifePaintKit(auto& hookContext) noexcept
    {
        return resolveKnifePaintKit(GET_CONFIG_VAR(skin_changer_vars::KnifeSkin));
    }
};
