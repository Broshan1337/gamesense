// Equipment icon names for the ImGui surfaces (inventory tab, player list rows).
//
// Source of truth: pak01_dir.vpk, panorama/images/icons/equipment/<name>.vsvg_c
// (101 files, names re-verified against the CURRENT depot on 2026-10-10 - e.g. the
// karambit icon is 'knife_karambit' (not 'karambit'), m9 is 'knife_m9_bayonet',
// the classic knife is 'knife' and the default T knife is 'knife_t').
// Rasterization: Utils/VpkIconLoader.h (nanosvg, vendored under ThirdParty/nanosvg).
#pragma once

#include <cstdint>

namespace vpk_icons
{

inline constexpr struct {
    std::uint16_t defIndex;
    const char* name;
} kDefIndexIcons[]{
    {1, "deagle"}, {2, "elite"}, {3, "fiveseven"}, {4, "glock"},
    {7, "ak47"}, {8, "aug"}, {9, "awp"}, {10, "famas"},
    {11, "g3sg1"}, {13, "galilar"}, {14, "m249"}, {16, "m4a4"},
    {17, "mac10"}, {19, "p90"}, {23, "mp5sd"}, {24, "ump45"},
    {25, "xm1014"}, {26, "bizon"}, {27, "mag7"}, {28, "negev"},
    {29, "sawedoff"}, {30, "tec9"}, {32, "p2000"}, {33, "mp7"},
    {34, "mp9"}, {35, "nova"}, {36, "p250"}, {38, "scar20"},
    {39, "sg556"}, {40, "ssg08"}, {41, "knife_t"}, {42, "knife_t"},
    {43, "flashbang"}, {44, "hegrenade"}, {45, "smokegrenade"},
    {46, "molotov"}, {48, "incgrenade"}, {49, "decoy"},
    {60, "m4a1_silencer"}, {61, "usp_silencer"}, {63, "cz75a"},
    {64, "revolver"}, {443, "healthshot"},
    {500, "bayonet"}, {503, "knife_css"}, {505, "knife_flip"},
    {506, "knife_gut"}, {507, "knife_karambit"}, {508, "knife_m9_bayonet"},
    {509, "knife_tactical"}, {512, "knife_falchion"}, {514, "knife_bowie"},
    {515, "knife_butterfly"}, {516, "knife_push"}, {517, "knife_cord"},
    {518, "knife_canis"}, {519, "knife_ursus"}, {520, "knife_navaja"},
    {521, "knife_outdoor"}, {522, "knife_stiletto"}, {523, "knife_talon"},
    {525, "knife_skeleton"}, {526, "knife_kukri"},
};

[[nodiscard]] inline const char* iconNameForDefIndex(std::uint16_t defIndex) noexcept
{
    for (const auto& entry : kDefIndexIcons)
        if (entry.defIndex == defIndex)
            return entry.name;
    return nullptr;
}

}