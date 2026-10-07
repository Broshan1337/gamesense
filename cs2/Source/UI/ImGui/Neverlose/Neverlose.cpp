#include "Neverlose.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_vulkan.h>

#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <ThirdParty/stb/stb_image.h>

#include <GameClient/Bind.h>
#include "FeatureBinds.h"
#include <Features/Hud/SpectatorList/SpectatorSnapshot.h>
#include <Features/Lua/LuaManager.h>
#include <Features/Game/MovementConfigVariables.h>
#include <Features/Game/NetLagConfigVariables.h>
#include <Features/Game/AutoPeekConfigVariables.h>
#include <Features/Misc/DiscordRpc.h>
#include <Features/Hud/SteamPersona.h>
#include <Features/Hud/ThemeAccent.h>
#include <Features/Hud/HudThemeColorConfigVariables.h>
#include <CS2/Econ/PaintKitDatabase.h>
#include <CS2/Econ/ItemDefDatabase.h>
#include <Features/SkinChanger/SkinChangerData.h>
#include <UI/ImGui/Neverlose/LogoAsset.h>
#include <Utils/ColorUtils.h>
#include <Utils/NsStr.h>
#include <Utils/StatusReport.h>

#include <Hooks/Graphics/VulkanHook.h>
#include <Platform/Linux/LinuxPlatformApi.h>

#include <UI/ImGui/GUI.h>
#include <UI/ImGui/GuiLog.h>
#include <UI/ImGui/OverlayLayer.h>
#include <UI/ImGui/SdlImGuiBackend.h>
#include <UI/ImGui/UiConfig.h>

#include <Features/Combat/Aimbot/AimbotConfigVariables.h>
#include <Features/Combat/LegitAimbot/LegitAimbotConfigVariables.h>
#include <Features/Combat/Rcs/RcsConfigVariables.h>
#include <Features/Combat/SniperRifles/NoScopeInaccuracyVis/NoScopeInaccuracyVisConfigVariables.h>
#include <Features/Combat/SpreadCircleVis/SpreadCircleVisConfigVariables.h>
#include <Features/Combat/Triggerbot/TriggerbotConfigVariables.h>
#include <Features/Game/BlockbotConfigVariables.h>
#include <Features/Game/BunnyhopConfigVariables.h>
#include <Features/Game/ChatTools.h>
#include <Features/Game/CooldownRevealerConfigVariables.h>
#include <Features/Game/RevealRadarConfigVariables.h>
#include <Features/Game/SpectateEnemiesConfigVariables.h>
#include <Features/Game/FakeLevelConfigVariables.h>
#include <Features/Game/FakePremierConfigVariables.h>
#include <Features/Game/FakeCommendsConfigVariables.h>
#include <Features/Game/GlitchGeneratorConfigVariables.h>
#include <Features/Game/FakePrimeConfigVariables.h>
#include <Features/Game/FvaConfigVariables.h>
#include <Features/Game/HitLogConfigVariables.h>
#include <Features/Game/KillsayConfigVariables.h>
#include <Features/Game/MatchAutoAcceptConfigVariables.h>
#include <Features/Game/PanicKeyConfigVariables.h>
#include <Features/Game/TeamDamageConfigVariables.h>
#include <Features/Game/ServerLagger.h>
#include <Features/Game/ServerLaggerConfigVariables.h>
#include <Features/Game/UserInfoFlood.h>
#include <Features/Game/UserInfoFloodConfigVariables.h>
#include <Features/Game/ValveDsSpoofConfigVariables.h>
#include <Features/Game/VoteRevealerConfigVariables.h>
#include <Features/Hud/BombPlantAlert/BombPlantAlertConfigVariables.h>
#include <Features/Visuals/Chams/ChamsConfigVariables.h>
#include <Features/Hud/BombTimer/BombTimerConfigVariables.h>
#include <Features/Hud/DefusingAlert/DefusingAlertConfigVariables.h>
#include <Features/Hud/KillfeedPreserver/KillfeedPreserverConfigVariables.h>
#include <Features/Hud/PostRoundTimer/PostRoundTimerConfigVariables.h>
#include <Features/Hud/Watermark/WatermarkConfigVariables.h>
#include <Features/Radio/RadioConfigVariables.h>
#include <Features/Radio/RadioManager.h>
#include <Features/Radio/SoundBoard.h>
#include <Features/Radio/SoundBoardConfigVariables.h>
#include <Features/SkinChanger/SkinChangerConfigVariables.h>
#include <Features/Game/AgentChangerConfigVariables.h>
#include <Features/Sound/HitSoundConfigVariables.h>
#include <Features/Sound/SpawnProtectionSoundConfigVariables.h>
#include <Features/Sound/SoundVisualizationConfigVariables.h>
#include <Features/Hud/Watermark/WatermarkPanelParams.h>
#include <UI/ImGui/Neverlose/MenuThemeConfigVariables.h>
#include <Features/Visuals/GrenadeTimers/GrenadeTimersConfigVariables.h>
#include <Features/Visuals/Hitmarker/HitmarkerConfigVariables.h>
#include <Features/Visuals/ModelGlow/ModelGlowConfigVariables.h>
#include <Features/Visuals/OutlineGlow/OutlineGlowConfigVariables.h>
#include <Features/Visuals/PlayerInfoInWorld/PlayerInfoInWorldConfigVariables.h>
#include <Features/Visuals/PlayerList/PlayerListConfigVariables.h>
#include <Features/Visuals/PlayerList/PlayerListSnapshot.h>
#include <Features/Game/PlayerAnalyzer/CheatOMeterState.h>
#include <Features/Game/PlayerAnalyzer/PlayerAnalyzerConfigVariables.h>
#include <Features/Hud/BindsList/BindsListConfigVariables.h>
#include <Features/Hud/CombatStats/CombatStatsConfigVariables.h>
#include <Features/Hud/CombatStats/CombatStatsHudState.h>
#include <Features/Hud/StatusPanel/StatusPanelConfigVariables.h>
#include <Features/Visuals/Removals/RemovalsConfigVariables.h>
#include <Features/Visuals/ThirdPerson/ThirdPersonConfigVariables.h>
#include <Features/Visuals/ViewmodelMod/ViewmodelModConfigVariables.h>
#include <Features/Visuals/WorldColors/WorldColorsConfigVariables.h>

// Neverlose-style menu - see Neverlose.h. Layout metrics and colors follow the design contract
// (FORFUTURETESTS/neverlose-last/DESIGN.md); the row/card/nav primitives are adapted from the
// reference implementation, rebound from demo state onto our config system.
namespace
{

// --- design tokens -----------------------------------------------------------
//
// Base metrics are the 1.0-design values; every frame applyMetrics() derives the actual
// metrics from menuScale (user-adjustable in the profile popover). The text sizes must match
// the loaded font pixel sizes, so loadFonts() reads them too - changing the scale reloads the
// fonts before the next atlas build.

constexpr ImU32 C(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

// Shared color tokens - the hand-drawn chrome re-uses these everywhere, so a theme pass is a
// one-line change per token instead of hunting dozens of literal C(...) calls.
// COLORING NOTE: the GTK experiment was reverted per user decision - the original near-black
// palette IS the look; the design (corners, switch geometry, structure) stays GTK-shaped, and
// colors stay in the user's hand (config accents + these tokens).
constexpr ImU32 kCardBg = C(16, 16, 18, 204);        // card body fill (soft translucency: the scene glows through)
constexpr ImU32 kSidebarBg = C(16, 16, 18, 204);     // nav rail fill
constexpr ImU32 kHairline = C(26, 26, 30);           // in-card separators
constexpr ImU32 kHairlineSoft = C(30, 30, 33);       // control/shell outlines
constexpr ImU32 kPillBg = C(24, 24, 26);             // value pill / button resting fill
constexpr ImU32 kPillBgHover = C(32, 32, 36);        // value pill / button hovered fill
constexpr ImU32 kRowHover = C(37, 37, 41);           // nav/list row hover fill (slightly lighter than pills)
constexpr ImU32 kShellBg = C(12, 12, 13, 244);       // shell window body
constexpr ImU32 kToolbarBg = C(10, 10, 11, 216);     // toolbar band over the shell
constexpr ImU32 kInsetBg = C(23, 23, 25);            // inset surfaces: search field, list rows, script windows
constexpr ImU32 kPopupBg = C(18, 18, 20, 255);        // popover body - fully opaque: page text
                                                      // must never bleed through (mid-fade frames
                                                      // used to ghost rows over the cards)
constexpr ImU32 kPopupBorder = C(54, 54, 60, 205);   // popover outline
constexpr ImU32 kTextBodyCol = C(207, 209, 218);     // row label text
constexpr ImU32 kTextFaint = C(114, 119, 132);       // captions/eyebrows: readable-dim (the old
                                                     // C(91..) pair was ~2.3:1 on the shell)

float kTextBody = 15.0f;
float kTextControl = 14.0f;
float kTextSmall = 12.0f;
float kTextCaption = 11.0f;
float kTextTitle = 16.0f;
float kTextIcon = 13.0f;

float kShellWidth = 748.0f;
float kShellHeight = 576.0f;
float kSidebarWidth = 158.0f;
float kToolbarHeight = 56.0f;
float kRowHeight = 37.0f;

float menuScale = 1.0f;     // 1.0 = design size; adjusted in the profile popover
bool menuScaleInitialized = false;
float shellHeightBase = 576.0f; // centering baseline (design height * menuScale) - the adaptive
                                // shell animates kShellHeight per page; the base keeps the
                                // default centering / drag clamps from drifting while it eases
float lastContentHeight = 0.0f; // previous frame's page content height (adaptive height target)
float navScroll = 0.0f;     // sidebar nav rail scroll (the expanded Visuals list can outgrow
                            // the space above the account bar - wheel-scrolls, clipped)
float navContentEnd = 0.0f; // shell-absolute bottom of last frame's laid-out nav rail (the rail
                            // scroll clamp reads it - hand-counted estimates drifted stale)

// Live glow color for the popover swatch: in rainbow mode the swatch shows the CURRENT hue.
ImU32 glowTintPreview() noexcept
{
    const auto glowColor = ui_config::get<MenuGlowColor>();
    float r = glowColor.r() / 255.0f;
    float g = glowColor.g() / 255.0f;
    float bch = glowColor.b() / 255.0f;
    if (ui_config::get<MenuGlowRainbow>()) {
        const float speed = ui_config::get<MenuGlowSpeed>();
        float hue = std::fmod(static_cast<float>(ImGui::GetTime()) * speed * 0.1f, 1.0f);
        ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, r, g, bch);
    }
    return C(static_cast<int>(r * 255.0f), static_cast<int>(g * 255.0f), static_cast<int>(bch * 255.0f), glowColor.a());
}

// Derives the menu accent from the watermark's text color while keeping it READABLE on the from the watermark's text color while keeping it READABLE on the
// dark menu: the perceived luminance (Rec. 709 weights) decides whether the source color is
// on the darker or the lighter side, and is clamped into a readable band by mixing toward
// white (too dark - invisible on the dark UI) or toward black (too bright - glaring next to
// the dim shell). Done at compile time; the watermark green (~0.74 luma) passes through
// unchanged, darker picks get lifted, near-white picks get tamed.
constexpr float kMinReadableLuma = 0.42f;

constexpr std::uint8_t lerpChannel(std::uint8_t c, std::uint8_t target, float t) noexcept
{
    return static_cast<std::uint8_t>(c + (static_cast<float>(target) - c) * t + 0.5f);
}

constexpr ImU32 accentFromWatermark() noexcept
{
    // Rec. 709 luminance, 0..1
    constexpr auto wm = watermark_panel_params::kTextColor;
    const float luma = (0.2126f * wm.r() + 0.7152f * wm.g() + 0.0722f * wm.b()) / 255.0f;
    if (luma < kMinReadableLuma) {
        // too dark for the dark shell -> lighten toward white just enough to clear the floor
        const float t = (kMinReadableLuma - luma) / (1.0f - luma);
        return C(lerpChannel(wm.r(), 255, t), lerpChannel(wm.g(), 255, t), lerpChannel(wm.b(), 255, t));
    }
    if (luma > 0.92f) {
        // too bright -> pull back toward the accent blue so it stops glaring
        const float t = (luma - 0.9f) / luma;
        return C(lerpChannel(wm.r(), 75, t), lerpChannel(wm.g(), 126, t), lerpChannel(wm.b(), 255, t));
    }
    return C(wm.r(), wm.g(), wm.b());
}

// Menu theme colors, editable in the profile popover (RGBA channel popover, like the feature
// color pickers) and persisted via config. Primary accent tints nav/chrome/text highlights;
// the SECONDARY colors cover the interactive controls: buttons (toggles, checkboxes, keybind
// pills) and sliders (track fills). All three are refreshed from config every frame.
ImU32 g_accent = accentFromWatermark();
ImU32 g_buttonAccent = C(150, 127, 238);
ImU32 g_sliderAccent = C(150, 127, 238);

// Present-thread refresh from the config vars (called once per frame from renderGameOverlay).
void refreshMenuTheme() noexcept
{
    const auto accent = ui_config::get<MenuAccentColor>();
    g_accent = C(accent.r(), accent.g(), accent.b(), accent.a());
    const auto button = ui_config::get<MenuButtonColor>();
    g_buttonAccent = C(button.r(), button.g(), button.b(), button.a());
    const auto slider = ui_config::get<MenuSliderColor>();
    g_sliderAccent = C(slider.r(), slider.g(), slider.b(), slider.a());

    // Fading-RGB style: recolors the three theme accents with the same hue clock the glow
    // rainbow uses (MenuGlowSpeed), so style and glow cycle together when both are on. Only
    // the RGB channels cycle - each color keeps its configured alpha.
    if (ui_config::get<MenuStyleRainbow>()) {
        const float hue = std::fmod(static_cast<float>(ImGui::GetTime()) * ui_config::get<MenuGlowSpeed>() * 0.1f, 1.0f);
        float r = 0.0f, g = 0.0f, b = 0.0f;
        ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, r, g, b);
        const auto tinted = [r, g, b](const color::Rgba& base) {
            return C(static_cast<int>(r * 255.0f), static_cast<int>(g * 255.0f), static_cast<int>(b * 255.0f), base.a());
        };
        g_accent = tinted(accent);
        g_buttonAccent = tinted(button);
        g_sliderAccent = tinted(slider);
    }

    // Publish for the game-thread panorama HUD (the HudThemeColor recolor follows the theme).
    theme_accent::publish(static_cast<std::uint8_t>(g_accent >> IM_COL32_R_SHIFT & 0xFF),
        static_cast<std::uint8_t>(g_accent >> IM_COL32_G_SHIFT & 0xFF),
        static_cast<std::uint8_t>(g_accent >> IM_COL32_B_SHIFT & 0xFF));
}

struct StylePreset {
    const char* name;
    ImU32 color;
};

constexpr StylePreset kStylePresets[] = {
    {"Default v2", C(150, 127, 238)}, // the logo's light purple - the new default theme
    {"Default (green)", accentFromWatermark()},
    {"Blue", C(75, 126, 255)},
    {"Cyan", C(115, 214, 210)},
    {"Purple", C(171, 70, 255)},
    {"Green", C(81, 192, 124)},
    {"Orange", C(255, 138, 61)},
    {"Red", C(235, 87, 87)},
};

// Scales a 1.0-design measurement by the menu scale.
[[nodiscard]] float s(float v) noexcept { return v * menuScale; }

// Vertical offset centering a control of the given height inside the current row.
[[nodiscard]] float rowCentered(float controlHeight) noexcept { return (kRowHeight - controlHeight) * 0.5f; }

void applyMetrics() noexcept
{
    kTextBody = 15.0f * menuScale;
    kTextControl = 14.0f * menuScale;
    kTextSmall = 12.0f * menuScale;
    kTextCaption = 11.0f * menuScale;
    kTextTitle = 16.0f * menuScale;
    kTextIcon = 13.0f * menuScale;
    neverlose::g_menuUiScale.store(menuScale, std::memory_order_relaxed);
    kShellWidth = 748.0f * menuScale;
    kShellHeight = 576.0f * menuScale;
    shellHeightBase = 576.0f * menuScale;
    kSidebarWidth = 158.0f * menuScale;
    kToolbarHeight = 56.0f * menuScale;
    kRowHeight = 37.0f * menuScale;
}

// Page identity - order matches the navigation rail.
enum class Page
{
    Rage,
    Legit,
    Movement,
    PlayerInfo,
    Glow,
    Viewmodel,
    Effects,
    Hud,
    Sound,
    Inventory,
    Radio,
    Scripts,
    Misc
};

// --- embedded fonts (objcopy symbols, see Source/CMakeLists.txt) ----------------

extern "C" const unsigned char _binary_Inter_Medium_ttf_start[];
extern "C" const unsigned char _binary_Inter_Medium_ttf_end[];
extern "C" const unsigned char _binary_Inter_SemiBold_ttf_start[];
extern "C" const unsigned char _binary_Inter_SemiBold_ttf_end[];
extern "C" const unsigned char _binary_fa_solid_900_ttf_start[];
extern "C" const unsigned char _binary_fa_solid_900_ttf_end[];
// CJK fallback subset (a few KB): the codepoints our UI strings use, generated from the
// system's Noto Sans CJK by fonts/make-cjk-subset.py (the Inter fonts are Latin-only; without
// this, skin names like "\xE9\xBE\x8D\xE7\x8E\x8B (Dragon King)" render as boxes).
extern "C" const unsigned char _binary_NotoCJK_subset_ttf_start[];
extern "C" const unsigned char _binary_NotoCJK_subset_ttf_end[];

// The FontAwesome codepoints this UI actually uses - a compact glyph range keeps the atlas
// small (the full FA range is ~7k glyphs).
constexpr ImWchar kIconCodepoints[] = {
    0xf002, // search          (global search)
    0xf005, // star            (radio favorites)
    0xf007, // user            (Player Info)
    0xf013, // cog             (Misc)
    0xf028, // volume-up       (Sound)
    0xf03d, // video           (Viewmodel)
    0xf03e, // image           (Visuals group)
    0xf04b, // play            (Radio)
    0xf04d, // stop            (Radio)
    0xf05b, // crosshairs      (Rage)
    0xf06e, // eye             (Outline Glow)
    0xf0c7, // save            (toolbar)
    0xf0d0, // magic           (Effects)
    0xf108, // desktop         (Hud)
    0xf1fc, // paint-brush     (Model Glow)
    0xf70c, // person-running  (Movement)
    0xf519, // broadcast-tower (Radio)
    0xf6cb, // dagger          (Inventory)
    0xf121, // code            (Scripts)
    0xf8cc, // mouse           (Legit)
    0xf11c, // keyboard        (bind indicator on bound toggle rows)
    0
};

// Icon glyphs used inline as UTF-8 string literals. A wrong byte used to render as the atlas
// fallback (the LAST range glyph - the mouse), so these are decoded and checked against
// kIconCodepoints at compile time. Add new constants here instead of raw literals.
constexpr ImWchar iconCodepointOf(const char (&utf8)[4]) noexcept
{
    return static_cast<ImWchar>(((utf8[0] & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F));
}

constexpr bool iconInAtlas(ImWchar codepoint) noexcept
{
    for (int i = 0; kIconCodepoints[i] != 0; ++i) {
        if (kIconCodepoints[i] == codepoint)
            return true;
    }
    return false;
}

constexpr char kIconStar[4] = {'\xEF', '\x80', '\x85', '\0'}; // f005 star (radio favorites, knife preview)
static_assert(iconInAtlas(iconCodepointOf(kIconStar)), "star glyph missing from kIconCodepoints");

constexpr char kIconKeyboard[4] = {'\xEF', '\x84', '\x9C', '\0'}; // f11c keyboard (bind indicator)
static_assert(iconInAtlas(iconCodepointOf(kIconKeyboard)), "keyboard glyph missing from kIconCodepoints");

// --- state ---------------------------------------------------------------------

struct SelectPopup {
    bool open = false;
    int owner = -1;
    int count = 0;
    const char* const* options = nullptr;
    void (*apply)(int index) = nullptr; // writes the committed option into config
    ImVec2 anchor{};
    float width = 134.0f;
    int openedFrame = 0;

    // Paint-kit picker mode (Inventory page): rows come from PaintKitDatabase for
    // paintKitDefIndex instead of the static options[] table, the apply callback receives the
    // RAW KIT ID (not a row index), and the list is searchable + wheel-scrollable (60+ entries
    // for some weapons - a plain dropdown would be twice the menu's height).
    bool paintKitMode = false;
    std::uint16_t paintKitDefIndex = 0;
    int paintKitCurrentId = 0; // highlights the committed row
    float paintKitScroll = 0.0f;
    char paintKitSearch[24] = "";

    // Item-def picker mode (Inventory page LOCAL ITEMS): rows come from an arbitrary
    // ItemDefEntry list (cases / keys from ItemDefDatabase) instead of PaintKitDatabase -
    // same search/scroll/apply machinery, apply still receives the raw def index.
    const cs2::ItemDefEntry* itemList = nullptr;
    int itemCount = 0;

    // String-list mode (persona presets): rows are plain text options - apply receives the
    // option INDEX.
    const char* const* stringList = nullptr;
    int stringCount = 0;

    // Skips the virtual "None" row (defIndex 0) for pickers where "none" is meaningless -
    // ALSO required whenever an entry legitimately uses defIndex 0, or it would collide
    // with the None row's PushID (the 2026-09-07 radio-phrase ID conflict).
    bool omitNone = false;
};

struct State {
    Page page = Page::Rage;
    bool visualsExpanded = false;
    bool scriptsExpanded = false;
    SelectPopup popup;
    // keybind capture (mirrors the Panorama KeybindCapture state machine); the row being
    // captured is identified by its control id
    enum class Capture { Inactive, WaitingRelease, WaitingPress };
    Capture capture = Capture::Inactive;
    int captureOwner = -1;
    float captureAge = 0.0f;   // time spent in WaitingRelease (2s cap - see keybindRow)
    int editingSlider = -1; // control id of the slider whose pill is being text-edited
    char editBuffer[12] = "";
    bool profileOpen = false;
    int profileOpenedFrame = -1;
    float scalePreview = 1.0f; // scale slider drag carry: last previewed value, committed on release
    bool scaleDragging = false;

    // multi-select combo popover (rows registered by multiSelectVar via generic accessors -
    // the popover is drawn outside this row's template context, after the content clip)
    bool multiSelectOpen = false;
    int multiSelectOwner = -1;
    int multiSelectOpenedFrame = -1;
    ImVec2 multiSelectAnchor{};
    int multiSelectCount = 0;
    bool (*multiSelectGet)(std::size_t) = nullptr;
    void (*multiSelectToggle)(std::size_t) = nullptr;
    const char* const* multiSelectNames = nullptr;

    // color picker popover (same accessor pattern, for one RGBA config var)
    bool colorPickerOpen = false;
    int colorPickerOwner = -1;
    int colorPickerOpenedFrame = -1;
    ImVec2 colorPickerAnchor{};
    color::Rgba (*colorGet)() = nullptr;
    void (*colorSet)(color::Rgba) = nullptr;

    // feature-bind popup (right-click on a registered toggle): edits the FeatureBinds registry
    // entry at featureBindIndex - key capture reuses the State::Capture machine with a dedicated
    // owner id (never a real control id, which are small positive ints)
    bool featureBindOpen = false;
    int featureBindOpenedFrame = -1;
    int featureBindIndex = -1;
    ImVec2 featureBindAnchor{};
    static constexpr int kFeatureBindCaptureOwner = 0x40000000;
};

bool fontReloadPending = false;
float pendingMenuScale = 1.0f;

struct StyleSelectState {
    bool open = false;
    ImVec2 anchor{};
    float width = 72.0f;
    int openedFrame = 0;
};
StyleSelectState styleSelect;

State state;

// The glow pages share one nav tab: a segmented control at the top of the page switches between
// the Outline Glow and Model Glow feature sections (0 = outline, 1 = model).
int glowSubTab = 0;
// Which glow sub-tab the rows currently being rendered belong to - search index entries are
// tagged with it so a search hit opens the right sub-tab.
int searchGlowSubTab = 0;

// Sub-page groups for the other tabbed pages (pageSubTabPills): Rage splits aim/accuracy,
// Legit splits aim/triggerbot, Misc splits general/chat/other. The search index tags rows with
// the same searchGlowSubTab global (it is the generic sub-tab tag; inventory's categories reuse
// it too), and activateSearchMatch maps a hit back onto the page's own sub-tab variable.
int rageSubTab = 0;
constexpr const char* const kRageSubTabs[] = {"Aimbot", "Accuracy"};
int legitSubTab = 0;
constexpr const char* const kLegitSubTabs[] = {"Aimbot", "Triggerbot"};
int miscSubTab = 0;
constexpr const char* const kMiscSubTabs[] = {"General", "Chat", "Other"};

// content scrolling (manual - the hand-drawn content does not live in an ImGui child).
// scrollTarget is what the wheel writes; scrollOffset (the rendered position) eases toward it
// with a frame-rate independent exponential decay - wheel notches land as a short glide
// instead of a 40px jump, and trackpads accumulate naturally.
float scrollOffset = 0.0f;
float dropdownScroll = 0.0f; // (retired with the plain dropdown layer - kept so the save struct offsets stay stable)
float scrollTarget = 0.0f;
float maxScroll = 0.0f;

ImVec2 shellBase{0.0f, 0.0f}; // window position, set each frame in render()

// --- global search ---------------------------------------------------------------------
// Rows self-register (label, page, content-relative y) into this index while their page
// renders. The full index is built lazily on first search open by ghost-rendering one page
// per frame into an offscreen throwaway window (widgets are skipped there - only our layout
// code runs), so the index can never drift from the actual row labels.

constexpr int kSearchIndexCap = 512;

struct SearchEntry {
    const char* label;
    Page page;
    float contentY; // top of the row, relative to the content area top
    int subTab = 0; // glow page sub-tab the row belongs to (0 = outline, 1 = model)
};

SearchEntry searchIndex[kSearchIndexCap];
int searchIndexCount = 0;
int searchIndexPageCursor = 0; // next Page to ghost-render during the build
bool searchIndexBuilt = false;
bool searchIndexing = false;   // true only during a ghost page run

bool searchOpen = false;
int searchOpenedFrame = -1;
char searchQuery[64] = "";
int searchSelected = 0; // keyboard selection into the visible match list

constexpr const char* kPageNames[] = {
    "Rage", "Legit", "Movement", "Player Info", "Glow",
    "Viewmodel", "Effects", "Hud", "Sound", "Inventory", "Radio", "Scripts", "Misc"
};
// gui.page() maps names -> ScriptPage (LuaManager.h) and the menu renders by Page: the two
// tables must stay in lockstep.
static_assert(lua::kScriptPageCount == static_cast<int>(sizeof(kPageNames) / sizeof(kPageNames[0])),
    "kPageNames and lua::kScriptPageNames diverged");
static_assert(static_cast<int>(lua::ScriptPage::Misc) == static_cast<int>(Page::Misc),
    "ScriptPage order no longer matches Page");

// Case-insensitive substring locator; returns nullptr when absent (or the needle is empty).
[[nodiscard]] const char* searchMatchPosition(const char* haystack, const char* needle) noexcept
{
    if (!needle[0])
        return nullptr;
    for (const char* h = haystack; *h; ++h) {
        int i = 0;
        while (needle[i] && h[i] && std::tolower(static_cast<unsigned char>(h[i])) == std::tolower(static_cast<unsigned char>(needle[i])))
            ++i;
        if (!needle[i])
            return h;
    }
    return nullptr;
}

// Case-insensitive substring match (strcasestr is GNU-only; keep it portable and local).
[[nodiscard]] bool searchMatches(const char* haystack, const char* needle) noexcept
{
    return searchMatchPosition(haystack, needle) != nullptr;
}

// cinematic open: 0 right after the menu is toggled open, eased to 1 while rendering
float reveal = 1.0f;
// Set on the menu-close falling edge (beginDismiss); the reveal transform then runs BACKWARDS
// (scale down + slide + fade) while GUI.cpp keeps the render path alive past the alpha fade.
// Cleared once the animation lands (reveal ~ 0) and on reopen (beginReveal wins).
bool dismissActive = false;

bool configPopoverOpen = false;
std::uint64_t lastCleanConfigEpoch = 0; // ui_config::changeEpoch at the last save/switch/restore
int configPopoverOpenedFrame = -1;
ImVec2 menuOffset{0.0f, 0.0f}; // user drag offset from the screen-centered position
bool draggingWindow = false;
ImVec2 dragStartMouse{0.0f, 0.0f};
ImVec2 dragStartOffset{0.0f, 0.0f};
char newConfigName[41] = "";
char configSearch[32] = "";
constexpr std::uint8_t ConfigVisibleCap = 16;

// trigger/rect tracking for close-on-outside-click
ImVec2 configChipMin, configChipMax;
ImVec2 unloadButtonMin, unloadButtonMax;
ImVec2 searchButtonMin, searchButtonMax;
ImVec2 accountBarMin, accountBarMax;
ImVec2 styleAnchorMin, styleAnchorMax;

[[nodiscard]] bool clickedOutside(ImVec2 min, ImVec2 max) noexcept
{
    return ImGui::IsMouseClicked(0) && !ImGui::IsMouseHoveringRect(min, max);
}

// --- animation helpers (reference implementation) --------------------------------

// Set once per frame in render(); motion() and the transforms read it instead of hitting the
// config system on every call (motion() runs dozens of times per frame).
bool g_reduceMotion = false;

float motion(ImGuiID id, float target, float speed = 16.0f, float initial = -1.0f) noexcept
{
    float* value = ImGui::GetStateStorage()->GetFloatRef(id, initial < 0.0f ? target : initial);
    if (g_reduceMotion) { // accessibility: snap straight to the target, no easing
        *value = target;
        return target;
    }
    *value = ImLerp(*value, target, 1.0f - std::exp(-speed * ImGui::GetIO().DeltaTime));
    if (std::fabs(*value - target) < 0.0005f)
        *value = target;
    return *value;
}

// Animation slots MUST key on the row's control id, never on ImGui::GetItemID(): a row covered
// by an open popup submits a Dummy (item id 0 - see hit()), and a GetItemID-keyed slot was then
// SHARED by every covered row - while any dropdown was open, all covered sliders/toggles eased
// toward each other's values through one storage float (visible drift) and snapped back when
// the popup closed. The salt keeps the domains of different primitives disjoint.
ImGuiID animKey(ImU32 salt, int id) noexcept
{
    return ImHashData(&id, sizeof(id), salt);
}

ImU32 mix(ImU32 a, ImU32 b, float t) noexcept
{
    return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(a), ImGui::ColorConvertU32ToFloat4(b), ImSaturate(t)));
}

void text(ImDrawList* d, ImVec2 p, ImU32 color, const char* value, float size, ImFont* font = nullptr) noexcept
{
    d->AddText(font ? font : ImGui::GetFont(), size, p, color, value);
}

void textY(ImDrawList* d, float x, float y, float h, ImU32 color, const char* value, float size, ImFont* font = nullptr) noexcept
{
    ImFont* f = font ? font : ImGui::GetFont();
    const float textHeight = f->CalcTextSizeA(size, FLT_MAX, 0.0f, value).y;
    text(d, ImVec2(x, y + std::floor((h - textHeight) * 0.5f)), color, value, size, f);
}

// Horizontally AND vertically centered text inside a rect - pill labels (mode pills, unbind,
// keybind buttons) read as labels, not sentences, so they sit centered like the value pills.
void textYCentered(ImDrawList* d, float x, float w, float y, float h, ImU32 color, const char* value, float size, ImFont* font = nullptr) noexcept
{
    ImFont* f = font ? font : ImGui::GetFont();
    const float textWidth = f->CalcTextSizeA(size, FLT_MAX, 0.0f, value).x;
    textY(d, x + std::floor((w - textWidth) * 0.5f), y, h, color, value, size, f);
}

// Slider value pill text as one centered run: bright number + dimmed unit ("120 ms"). The
// unit reads as metadata; the number is the payload (same two-level text hierarchy the
// watermark chips use).
void drawValuePillText(ImDrawList* d, float x, float w, float y, float h, const char* number, const char* suffix) noexcept
{
    ImFont* f = ImGui::GetFont();
    const float numberWidth = f->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, number).x;
    const bool hasSuffix = suffix && suffix[0];
    const float suffixWidth = hasSuffix ? f->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, suffix).x : 0.0f;
    const float startX = x + std::floor((w - numberWidth - suffixWidth) * 0.5f);
    textY(d, startX, y, h, kTextBodyCol, number, kTextControl, nullptr);
    if (hasSuffix)
        textY(d, startX + numberWidth, y, h, kTextFaint, suffix, kTextControl, nullptr);
}

// The toggle affordance everywhere: a SWITCH - pill track 1.6x wider than tall with a round
// knob that slides between the dark resting track and the button accent, knob eases with `on`
// (hover previews at ~48%). Track height is 85% of the passed footprint (boxSize = the old
// checkbox chip size) - vertically centered on it, RIGHT edge aligned to the footprint's right
// edge, extends LEFTWARD - so vertical row metrics and the right edge all callers aligned to
// stay put. Returns the drawn track rect so callers can center halos/hit areas on the real
// geometry.
ImRect drawCheckboxChip(ImDrawList* d, ImVec2 p, float boxSize, float on) noexcept
{
    const float trackHeight = boxSize * 0.85f;
    const float trackWidth = boxSize * 1.6f;
    const float trackY = p.y + (boxSize - trackHeight) * 0.5f;
    const ImVec2 trackMin{p.x + boxSize - trackWidth, trackY};
    const ImVec2 trackMax{p.x + boxSize, trackY + trackHeight};
    const float radius = trackHeight * 0.5f;
    d->AddRectFilled(trackMin, trackMax, mix(C(27, 27, 29), g_buttonAccent, on), radius);
    d->AddRect(trackMin, trackMax, mix(kHairlineSoft, g_buttonAccent, on), radius);
    // knob: rides the track with a small inset, diameter = track height minus the inset
    const float knob = trackHeight - s(3.5f);
    const float travel = trackWidth - knob - s(3.5f);
    const float knobX = trackMin.x + s(1.75f) + travel * ImSaturate(on);
    const float knobY = trackMin.y + (trackHeight - knob) * 0.5f;
    d->AddCircleFilled(ImVec2(knobX + knob * 0.5f, knobY + knob * 0.5f), knob * 0.5f, C(238, 239, 244));
    return ImRect(trackMin, trackMax);
}

// --- popup occlusion tracking -------------------------------------------------------------
//
// The hand-drawn popups all render into the same window with no ImGui occlusion between them,
// so hit-testing is done by hand: every popup records the rect it drew (popupCur), the snapshot
// is rotated into popupPrev at the end of the frame, and a hit() whose rect overlaps a recorded
// popup submits NO interactive item at all (a Dummy). In imgui 1.91.7 the first-submitted item
// under the mouse claims HoveredId - even a DISABLED one (ItemHoverable: SetHoveredID runs
// before the ImGuiItemFlags_Disabled early-out; later items are rejected while HoveredId is
// taken) - so a covered page control must not submit an InvisibleButton, or every popup row
// drawn above it would be dead. Controls NOT covered by a popup stay fully live while one is
// open: clicking one activates it AND dismisses the popup through the popup's own
// click-outside check, so nothing ever needs a throwaway click first.

enum PopupKind { PopupConfig, PopupProfile, PopupStyle, PopupDropdown, PopupMultiSelect, PopupColor, PopupFeatureBind, PopupKindCount };

struct PopupRect { ImVec2 min, max; bool valid; };
PopupRect popupPrev[PopupKindCount] = {}; // complete snapshot of last frame's popups
PopupRect popupCur[PopupKindCount] = {};  // rebuilt by the popups as they render this frame

bool coveredByPrevPopup(int exceptKind, ImVec2 min, ImVec2 max) noexcept
{
    for (int i = 0; i < PopupKindCount; ++i) {
        if (i == exceptKind || !popupPrev[i].valid)
            continue;
        if (min.x < popupPrev[i].max.x && max.x > popupPrev[i].min.x && min.y < popupPrev[i].max.y && max.y > popupPrev[i].min.y)
            return true;
    }
    return false;
}

void recordPopupRect(int kind, ImVec2 min, ImVec2 max) noexcept
{
    popupCur[kind] = {min, max, true};
}

bool hit(const char* id, ImVec2 p, ImVec2 size) noexcept
{
    ImGui::SetCursorScreenPos(p);
    if (coveredByPrevPopup(-1, p, p + size)) {
        // Covered by an open popup: submit a Dummy and NO interactive item. In imgui 1.91.7 even
        // a DISABLED InvisibleButton claims HoveredId (ItemHoverable calls SetHoveredID before the
        // ImGuiItemFlags_Disabled early-out), and any later item at that position is then rejected
        // - which made every popup row drawn over a page control unclickable. A Dummy (id == 0)
        // never claims HoveredId, so the popup's row above wins the hover/click instead.
        ImGui::Dummy(size);
        return false;
    }
    return ImGui::InvisibleButton(id, size);
}

// Rows INSIDE a hand-drawn popup: never gated by the popup's own recorded rect, but still dodge
// popups rendered above it (e.g. the RGBA picker or preset list hanging inside the profile
// popover would otherwise lose their rows to the host rows underneath them).
bool hitPopupRow(const char* id, ImVec2 p, ImVec2 size, int ownKind) noexcept
{
    ImGui::SetCursorScreenPos(p);
    if (coveredByPrevPopup(ownKind, p, p + size)) {
        ImGui::Dummy(size); // see hit(): a Dummy never claims HoveredId, the popup above wins
        return false;
    }
    return ImGui::InvisibleButton(id, size);
}

// For the leaf popups' own rows/strips (multi-select, RGBA picker, style presets): never gated,
// they are the topmost content at their position.
bool hitModal(const char* id, ImVec2 p, ImVec2 size) noexcept
{
    ImGui::SetCursorScreenPos(p);
    return ImGui::InvisibleButton(id, size);
}

// 9-slice of the precomputed gaussian stamp (white, alpha = blur profile) tinted by `tint`:
// used for the drop shadow (black tint) and the outer menu glow (accent/rainbow tint).
// Returns false when the stamp has not been uploaded yet (caller falls back).
bool drawStampSlice(ImDrawList* d, ImVec2 min, ImVec2 max, float margin, ImU32 tint) noexcept
{
    if (const auto stamp = reinterpret_cast<ImTextureID>(VulkanHook::shadow_texture::query())) {
        using VulkanHook::shadow_texture::kMargin;
        using VulkanHook::shadow_texture::kStampSize;
        constexpr float inv = 1.0f / static_cast<float>(kStampSize);
        const float edge = kMargin * inv;                          // uv where the box edge sits
        const float coreEnd = (kStampSize - kMargin) * inv;
        const float m = margin;                                    // one blur band on screen

        const ImVec2 a = min - ImVec2(m, m);
        const ImVec2 b = max + ImVec2(m, m);
        // corners (m x m): the blur band around each rounded corner curve. The gaussian inside the
        // stamp's corner square is still strong near the box edge (up to ~50% just outside the
        // curve), which over a bright game scene reads as a square grey wedge hugging the corner -
        // dim the corner pieces and fade each one's outer tip to zero so the corner falloff
        // always looks round instead of a hard square patch.
        const int cornerBase = d->VtxBuffer.Size;
        d->AddImage(stamp, a, min, ImVec2(0.0f, 0.0f), ImVec2(edge, edge), tint);
        d->AddImage(stamp, ImVec2(max.x, a.y), ImVec2(b.x, min.y), ImVec2(coreEnd, 0.0f), ImVec2(1.0f, edge), tint);
        d->AddImage(stamp, ImVec2(a.x, max.y), ImVec2(min.x, b.y), ImVec2(0.0f, coreEnd), ImVec2(edge, 1.0f), tint);
        d->AddImage(stamp, max, b, ImVec2(coreEnd, coreEnd), ImVec2(1.0f, 1.0f), tint);
        for (int corner = 0; corner < 4; ++corner) {
            ImDrawVert* quad = &d->VtxBuffer[cornerBase + corner * 4];
            for (int v = 0; v < 4; ++v) {
                // outer tip of the corner quad sits at quad vertex index == corner (TL=0, TR=1, BL=2, BR=3)
                const ImU32 alpha = (v == corner)
                    ? 0u
                    : ((quad[v].col >> IM_COL32_A_SHIFT) & 255u) * 55u / 100u;
                quad[v].col = (quad[v].col & 0x00ffffffu) | (alpha << IM_COL32_A_SHIFT);
            }
        }
        // edges (band strips): gaussian profile across, stretched along the length
        d->AddImage(stamp, ImVec2(min.x, a.y), ImVec2(max.x, min.y), ImVec2(edge, 0.0f), ImVec2(coreEnd, edge), tint);
        d->AddImage(stamp, ImVec2(min.x, max.y), ImVec2(max.x, b.y), ImVec2(edge, coreEnd), ImVec2(coreEnd, 1.0f), tint);
        d->AddImage(stamp, ImVec2(a.x, min.y), ImVec2(min.x, max.y), ImVec2(0.0f, edge), ImVec2(edge, coreEnd), tint);
        d->AddImage(stamp, max, ImVec2(b.x, b.y), ImVec2(coreEnd, edge), ImVec2(1.0f, coreEnd), tint);
        return true;
    }

    d->AddRectFilled(min - ImVec2(margin, margin), max + ImVec2(margin, margin), C(0, 0, 0, 16), margin);
    d->AddRectFilled(min - ImVec2(margin / 2, margin / 2), max + ImVec2(margin / 2, margin / 2), C(0, 0, 0, 22), margin / 2);
    d->AddRectFilled(min - ImVec2(margin / 6, margin / 6), max + ImVec2(margin / 6, margin / 6), C(0, 0, 0, 28), margin / 6);
    return false;
}

void softShadow(ImDrawList* d, ImVec2 min, ImVec2 max, float rounding, float margin = 0.0f) noexcept
{
    // real gaussian falloff via the precomputed shadow stamp (shadow_texture in VulkanHook.h);
    // the layered-rect fake runs until the stamp upload completes (first frames) or permanently
    // if it failed.
    drawStampSlice(d, min, max, margin > 0.0f ? margin : s(12.0f), C(0, 0, 0, 110));
}

void chevron(ImDrawList* d, ImVec2 p, ImU32 color) noexcept
{
    d->AddLine(p, p + ImVec2(s(3), s(3)), color, s(1.3f));
    d->AddLine(p + ImVec2(s(3), s(3)), p + ImVec2(s(0), s(6)), color, s(1.3f));
}

// Pulsing accent dot: soft breathing halo + bright core (the build-chip recipe, applied to
// every header dot in the menu so they all breathe in sync).
void pulsingDot(ImDrawList* d, ImVec2 center, float radius, ImU32 color, float speed = 2.2f) noexcept
{
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * speed);
    d->AddCircleFilled(center, radius * 1.9f, (color & 0x00FFFFFFu) | (static_cast<ImU32>(32 * pulse) << IM_COL32_A_SHIFT));
    d->AddCircleFilled(center, radius, (color & 0x00FFFFFFu) | (static_cast<ImU32>(150 + 90 * pulse) << IM_COL32_A_SHIFT));
}

// --- toasts ---------------------------------------------------------------------------
// Small bottom-center feedback pills (config saved/switched/created, failures). Ring of 4,
// newest at the bottom; drawn last so they sit above popups.

struct Toast { char text[96]; double born; ImU32 tint; };
Toast toasts[4] = {};
int toastCount = 0;

void pushToast(const char* message, ImU32 tint) noexcept
{
    if (toastCount == 4) {
        for (int i = 0; i < 3; ++i)
            toasts[i] = toasts[i + 1];
        --toastCount;
    }
    Toast& t = toasts[toastCount++];
    std::snprintf(t.text, sizeof(t.text), "%s", message);
    t.born = ImGui::GetTime();
    t.tint = tint;
}

void drawToasts(ImDrawList* d, ImVec2 base) noexcept
{
    constexpr double kHold = 2.4;
    constexpr double kIn = 0.16;
    constexpr double kOut = 0.30;
    const double now = ImGui::GetTime();

    int keep = 0;
    for (int i = 0; i < toastCount; ++i)
        if (now - toasts[i].born <= kHold + kOut)
            toasts[keep++] = toasts[i];
    toastCount = keep;

    const float rowHeight = s(26.0f);
    const float gap = s(6.0f);
    for (int i = toastCount - 1; i >= 0; --i) {
        const Toast& t = toasts[i];
        const double age = now - t.born;
        float alpha = 1.0f;
        float rise = 0.0f;
        if (!g_reduceMotion) {
            if (age < kIn) {
                const float k = static_cast<float>(age / kIn);
                alpha = k;
                rise = (1.0f - k) * s(8.0f);
            } else if (age > kHold) {
                const float k = static_cast<float>((age - kHold) / kOut);
                alpha = 1.0f - k;
                rise = -k * s(4.0f);
            }
        }
        const float textWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, t.text).x;
        const float width = textWidth + s(34.0f);
        const float x = base.x + kSidebarWidth + (kShellWidth - kSidebarWidth) * 0.5f - width * 0.5f;
        const float y = base.y + kShellHeight - s(14.0f) - rowHeight - (toastCount - 1 - i) * (rowHeight + gap) + rise;
        const ImU32 a = static_cast<ImU32>(alpha * 255.0f);
        d->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + rowHeight), (kInsetBg & 0x00FFFFFFu) | (static_cast<ImU32>(244 * alpha) << IM_COL32_A_SHIFT), rowHeight * 0.5f);
        d->AddRect(ImVec2(x, y), ImVec2(x + width, y + rowHeight), (t.tint & 0x00FFFFFFu) | (static_cast<ImU32>(110 * alpha) << IM_COL32_A_SHIFT), rowHeight * 0.5f);
        d->AddCircleFilled(ImVec2(x + s(15), y + rowHeight * 0.5f), s(2.4f), (t.tint & 0x00FFFFFFu) | (a << IM_COL32_A_SHIFT));
        textY(d, x + s(24), y, rowHeight, (C(224, 227, 235) & 0x00FFFFFFu) | (a << IM_COL32_A_SHIFT), t.text, kTextControl, nullptr);
    }
}

ImFont* strongFont() noexcept
{
    auto& fonts = ImGui::GetIO().Fonts->Fonts;
    return fonts.Size > 1 ? fonts[1] : ImGui::GetFont();
}

ImFont* iconFont() noexcept
{
    auto& fonts = ImGui::GetIO().Fonts->Fonts;
    return fonts.Size > 2 ? fonts[2] : ImGui::GetFont();
}

// --- row layout -----------------------------------------------------------------

struct CardContext {
    ImVec2 origin; // first row's top-left on screen
    float width = 0.0f;
    float row = 0.0f; // fractional: cardDivider() consumes partial rows for sub-group captions
    float labelReserve = 0.0f; // row's right-control reserve, set BEFORE beginRow (0 = widest s(158))
    float lastLabelWidth = 0.0f; // DRAWN (possibly truncated) label width of the previous row
};

CardContext card;

// Central label fitting: draws the label ellipsized to `maxWidth` when too long (UTF-8 aware),
// returns the DRAWN width so followers (bind glyph etc.) can position after it. Before this,
// long labels just ran under the right-hand controls; hueRow hand-rolled its own dodge.
float drawLabelFit(ImDrawList* d, float x, float y, float h, ImU32 color, const char* label, float size, ImFont* font, float maxWidth) noexcept
{
    font = font ? font : ImGui::GetFont();
    const float width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label).x;
    if (width <= maxWidth) {
        textY(d, x, y, h, color, label, size, font);
        return width;
    }
    char buf[96];
    float w = 0.0f;
    int len = 0;
    const float ellipsisW = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "...").x;
    while (label[len] && len < static_cast<int>(sizeof(buf)) - 1) {
        const int charLen = label[len] < 0x80 ? 1 : label[len] < 0xE0 ? 2 : label[len] < 0xF0 ? 3 : 4;
        for (int i = 0; i < charLen && label[len + i]; ++i)
            buf[len + i] = label[len + i];
        const int next = len + charLen;
        const float nextW = font->CalcTextSizeA(size, FLT_MAX, 0.0f, buf, buf + next).x;
        if (nextW + ellipsisW > maxWidth)
            break;
        len = next;
        w = nextW;
    }
    std::memcpy(buf + len, "...", 4);
    textY(d, x, y, h, color, buf, size, font);
    return w + ellipsisW;
}

void beginRow(ImDrawList* d, const char* label) noexcept
{
    const float y = card.origin.y + card.row * kRowHeight;
    if (searchIndexing && searchIndexCount < kSearchIndexCap)
        searchIndex[searchIndexCount++] = {label, static_cast<Page>(searchIndexPageCursor), y - (shellBase.y + kToolbarHeight + s(24.0f)), searchGlowSubTab};
    if (card.row > 0.5f)
        d->AddLine(ImVec2(card.origin.x + s(12), y), ImVec2(card.origin.x + card.width - s(12), y), kHairline);
    // label fitting: keep the text clear of the row's right-hand control. Rows set
    // card.labelReserve = distance from the card's right edge to the control's left edge;
    // 0 falls back to the widest control set (slider track + value pill ~s(158)).
    const float reserve = card.labelReserve > 0.0f ? card.labelReserve : s(158.0f);
    card.labelReserve = 0.0f;
    card.lastLabelWidth = drawLabelFit(d, card.origin.x + s(13), y, kRowHeight, kTextBodyCol, label, kTextBody, nullptr, card.width - s(13) - reserve);
    card.row += 1.0f;
}

// In-card sub-group caption: a hairline + small caption consuming one row slot. Splits long
// cards (e.g. the triggerbot) into scannable sections without a second card frame. The accent
// tick before the caption distinguishes it from a real card header band at a glance.
void cardDivider(const char* caption) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float y = card.origin.y + card.row * kRowHeight;
    if (searchIndexing && searchIndexCount < kSearchIndexCap)
        searchIndex[searchIndexCount++] = {caption, static_cast<Page>(searchIndexPageCursor), y - (shellBase.y + kToolbarHeight + s(24.0f)), searchGlowSubTab};
    if (card.row > 0.5f)
        d->AddLine(ImVec2(card.origin.x + s(12), y), ImVec2(card.origin.x + card.width - s(12), y), kHairline);
    // ImVec2(card.origin + ImVec2(x, y)) would DOUBLE-add origin.y (y already includes it) -
    // the accent tick used to drift down the page (the orphan purple marks of 2026-09-09).
    d->AddRectFilled(ImVec2(card.origin.x + s(13), y + rowCentered(s(10))), ImVec2(card.origin.x + s(13) + s(2.5f), y + rowCentered(s(10)) + s(10)),
        (g_accent & 0x00FFFFFFu) | (130u << IM_COL32_A_SHIFT), s(1.2f));
    textY(d, card.origin.x + s(21), y, kRowHeight, kTextFaint, caption, kTextCaption, nullptr);
    card.row += 1.0f;
}

[[nodiscard]] ImVec2 rowControlPos(float widthFromRight) noexcept
{
    const float y = card.origin.y + (card.row - 1) * kRowHeight;
    return ImVec2(card.origin.x + card.width - widthFromRight, y);
}

// --- row primitives ---------------------------------------------------------------

bool toggle(const char* label, bool* value, int id, bool* rightClicked = nullptr) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    card.labelReserve = s(36.0f); // checkbox chip
    beginRow(d, label);

    ImVec2 p = rowControlPos(s(36.0f));
    p.y += rowCentered(s(20.0f));

    ImGui::PushID(id);
    const bool clicked = hit("##toggle", p - ImVec2(s(14), s(6)), ImVec2(s(38), s(32)));
    if (rightClicked)
        *rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    // Hover preview, same as the popover checkboxes (Outer Glow etc.): hovering an OFF switch
    // tints it halfway toward the accent - the fill then commits on click. ON switches stay lit.
    const float on = motion(animKey(0x55aa721u, id), *value ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f, 19.0f);
    const float hover = motion(animKey(0x118a0u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f, 20.0f);
    ImGui::PopID();
    if (clicked)
        *value = !*value;

    const ImRect track = drawCheckboxChip(d, p, s(20), on);
    // soft accent halo while the switch is ON - the knob feels "lit" instead of just colored
    if (on > 0.05f) {
        d->AddRectFilled(track.Min - ImVec2(s(3), s(3)), track.Max + ImVec2(s(3), s(3)), (g_buttonAccent & 0x00FFFFFFu) | (static_cast<ImU32>(26 * on) << IM_COL32_A_SHIFT), s(10));
        d->AddRectFilled(track.Min - ImVec2(s(6), s(6)), track.Max + ImVec2(s(6), s(6)), (g_buttonAccent & 0x00FFFFFFu) | (static_cast<ImU32>(10 * on) << IM_COL32_A_SHIFT), s(13));
    }
    static_cast<void>(hover);
    return clicked;
}

bool selectList(const char* label, int* index, const char* const* options, int count, int id,
    void (*apply)(int)) noexcept; // the unified dropdown popup (defined below)

bool select(const char* label, int* index, const char* const* options, int count, int id,
    void (*apply)(int) = nullptr) noexcept
{
    // UNIFIED DROPDOWN: every dropdown in the menu opens the same searchable, wheel-scrollable
    // list popup (the old fixed 10-row dropdown is gone - one widget, one behavior).
    selectList(label, index, options, count, id, apply);
    return false;
}

// The one true dropdown popup - searchable, wheel-scrollable, scrollbar-draggable. Both the
// select() alias and every list-style picker (presets, regions, kick reasons) route here.
bool selectList(const char* label, int* index, const char* const* options, int count, int id,
    void (*apply)(int) = nullptr) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    card.labelReserve = controlWidth + s(13.0f);
    beginRow(d, label);

    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool click = hit("##select", cp, ImVec2(controlWidth, s(23)));
    const float response = motion(animKey(0x6161u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();

    if (click) {
        state.popup.open = !(state.popup.open && state.popup.owner == id);
        state.popup.owner = id;
        state.popup.options = options; // the control reads its current value from this
        state.popup.count = count;
        state.popup.apply = apply;
        state.popup.anchor = cp;
        state.popup.width = controlWidth;
        state.popup.openedFrame = ImGui::GetFrameCount();
        state.popup.paintKitMode = true;
        // CLEAR every other popup mode's state first - the layer fields persist across popups,
        // and a stale paintKitDefIndex/stringList from a previous picker would silently shadow
        // this popup's option list (rows rendered from the wrong source).
        state.popup.paintKitDefIndex = 0;
        state.popup.itemList = nullptr;
        state.popup.itemCount = 0;
        state.popup.omitNone = true;
        state.popup.stringList = options;
        state.popup.stringCount = count;
        state.popup.paintKitCurrentId = *index;
        state.popup.paintKitScroll = 0.0f;
        state.popup.paintKitSearch[0] = '\0';
    }

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));
    // Clip to the pill: option labels (player names, preset names) are routinely wider than
    // the 134px control and used to spill over the neighboring rows.
    d->PushClipRect(cp + ImVec2(s(7), 0.0f), cp + ImVec2(controlWidth - s(14), s(23)), true);
    textY(d, cp.x + s(7), cp.y, s(23), (*index >= 0 && *index < count) ? C(200, 203, 212) : C(120, 124, 134), (*index >= 0 && *index < count) ? options[*index] : "Select", kTextControl, nullptr);
    d->PopClipRect();
    d->AddLine(cp + ImVec2(controlWidth - s(13), s(9)), cp + ImVec2(controlWidth - s(9), s(13)), C(139, 143, 154), 1.0f);
    d->AddLine(cp + ImVec2(controlWidth - s(9), s(13)), cp + ImVec2(controlWidth - s(5), s(9)), C(139, 143, 154), 1.0f);
    return false;
}

bool sliderRow(const char* label, int* value, int min, int max, int id, const char* suffix) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    beginRow(d, label);

    const int previousValue = *value;
    const bool editing = state.editingSlider == id;
    const float position = static_cast<float>(*value - min) / static_cast<float>(max - min);

    if (!editing) {
        ImVec2 start = rowControlPos(s(145.0f));
        start.y += rowCentered(s(3.0f));
        const float trackWidth = s(80.0f);

        ImGui::PushID(id);
        hit("##slider", start - ImVec2(s(5), s(8)), ImVec2(trackWidth + s(10), s(19)));
        if (ImGui::IsItemActive()) {
            const float newPosition = ImClamp((ImGui::GetIO().MousePos.x - start.x) / trackWidth, 0.0f, 1.0f);
            *value = min + static_cast<int>(newPosition * static_cast<float>(max - min) + 0.5f);
        }
        const float shown = motion(animKey(0xa8f1u, id), position, 14.0f, position);
        const bool dragging = ImGui::IsItemActive();
        ImGui::PopID();

        d->AddRectFilled(start, start + ImVec2(trackWidth, s(3)), C(33, 33, 36), s(2));
        d->AddRectFilled(start, start + ImVec2(trackWidth * shown, s(3)), g_sliderAccent, s(2));
        // while dragging: accent ring around the thumb + a value bubble riding above it
        if (dragging) {
            const ImVec2 thumb = start + ImVec2(trackWidth * shown, s(1.5f));
            d->AddCircle(thumb, s(7.5f), (g_sliderAccent & 0x00FFFFFFu) | (90u << IM_COL32_A_SHIFT), 0, s(1.2f));
            d->AddCircle(thumb, s(10.0f), (g_sliderAccent & 0x00FFFFFFu) | (36u << IM_COL32_A_SHIFT), 0, s(1.0f));
            char bubble[32];
            std::snprintf(bubble, sizeof(bubble), "%d", *value);
            const float bubbleTextWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, bubble).x;
            const float bubbleWidth = bubbleTextWidth + s(10.0f);
            ImVec2 bubblePos(thumb.x - bubbleWidth * 0.5f, thumb.y - s(22.0f));
            bubblePos.x = ImClamp(bubblePos.x, card.origin.x + s(4), card.origin.x + card.width - bubbleWidth - s(4));
            d->AddRectFilled(bubblePos, bubblePos + ImVec2(bubbleWidth, s(17)), (kInsetBg & 0x00FFFFFFu) | (242u << IM_COL32_A_SHIFT), s(7));
            d->AddRect(bubblePos, bubblePos + ImVec2(bubbleWidth, s(17)), (g_sliderAccent & 0x00FFFFFFu) | (110u << IM_COL32_A_SHIFT), s(7));
            textY(d, bubblePos.x + (bubbleWidth - bubbleTextWidth) * 0.5f, bubblePos.y, s(17), C(233, 235, 241), bubble, kTextSmall, nullptr);
        }
        d->AddCircleFilled(start + ImVec2(trackWidth * shown, s(1.5f)), s(5.5f), C(247, 248, 252));
    }

    // Value pill: click to type an exact value (editable sliders). The pill grows leftward to
    // fit the text (big values like fog distance "3873.73" used to spill out of the fixed 42px)
    // but is CAPPED below the slider track's start (rowControlPos(s(145))) - uncapped, huge
    // values ("11800ms") grew into the track in EVERY tab that has a wide-range slider.
    char pillNumber[16];
    std::snprintf(pillNumber, sizeof(pillNumber), "%d", *value);
    const char* pillSuffix = suffix ? suffix : "";
    const float pillTextWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, pillNumber).x
        + ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, pillSuffix).x;
    const float pillWidth = ImClamp(pillTextWidth + s(12.0f), s(42.0f), s(118.0f));
    ImVec2 pill = rowControlPos(s(13.0f) + pillWidth);
    pill.y += rowCentered(s(21.0f));
    d->AddRectFilled(pill, pill + ImVec2(pillWidth, s(21)), kPillBg, s(7));

    ImGui::PushID(id + 500000);
    if (editing) {
        ImGui::SetCursorScreenPos(pill + ImVec2(s(4), s(3)));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kHairlineSoft);
        ImGui::PushStyleColor(ImGuiCol_Border, g_accent);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(4), s(2)));
        ImGui::PushItemWidth(pillWidth - s(8));
        ImGui::SetKeyboardFocusHere(0); // focus on frame 1 - without this the not-yet-focused
        ImGui::InputText("##edit", state.editBuffer, sizeof(state.editBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll); // input trips lostFocus instantly
        const bool committed = ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
        const bool lostFocus = ImGui::IsItemDeactivatedAfterEdit() || (!ImGui::IsItemActive() && !ImGui::IsItemFocused() && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered());
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (committed || lostFocus) {
            const int parsed = std::atoi(state.editBuffer);
            *value = ImClamp(parsed, min, max);
            state.editingSlider = -1;
        }
    } else {
        if (hit("##pill_edit", pill, ImVec2(pillWidth, s(21))) && ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) {
            state.editingSlider = id;
            std::snprintf(state.editBuffer, sizeof(state.editBuffer), "%d", *value);
        }
        // Clip to the pill: a capped pill must never paint its text over the track.
        d->PushClipRect(pill, pill + ImVec2(pillWidth, s(21)), true);
        drawValuePillText(d, pill.x, pillWidth, pill.y, s(21), pillNumber, pillSuffix);
        d->PopClipRect();
    }
    ImGui::PopID();
    return *value != previousValue;
}

// Fractional-slider row: the same sliderRow visuals (track + thumb + drag ring + value bubble
// + click-to-type pill) for float values. Shared widget - script float sliders (gui.*) and any
// future native float row render through this, never a local variant.
bool sliderRowFloat(const char* label, float* value, float min, float max, int id, const char* suffix) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    beginRow(d, label);

    const float span = max > min ? (max - min) : 1.0f;
    const float previousValue = *value;
    const bool editing = state.editingSlider == id;
    const float position = (*value - min) / span;

    if (!editing) {
        ImVec2 start = rowControlPos(s(145.0f));
        start.y += rowCentered(s(3.0f));
        const float trackWidth = s(80.0f);

        ImGui::PushID(id);
        hit("##slider", start - ImVec2(s(5), s(8)), ImVec2(trackWidth + s(10), s(19)));
        if (ImGui::IsItemActive()) {
            const float newPosition = ImClamp((ImGui::GetIO().MousePos.x - start.x) / trackWidth, 0.0f, 1.0f);
            *value = min + newPosition * span;
        }
        const float shown = motion(animKey(0xa8f2u, id), position, 14.0f, position);
        const bool dragging = ImGui::IsItemActive();
        ImGui::PopID();

        d->AddRectFilled(start, start + ImVec2(trackWidth, s(3)), C(33, 33, 36), s(2));
        d->AddRectFilled(start, start + ImVec2(trackWidth * shown, s(3)), g_sliderAccent, s(2));
        if (dragging) {
            const ImVec2 thumb = start + ImVec2(trackWidth * shown, s(1.5f));
            d->AddCircle(thumb, s(7.5f), (g_sliderAccent & 0x00FFFFFFu) | (90u << IM_COL32_A_SHIFT), 0, s(1.2f));
            d->AddCircle(thumb, s(10.0f), (g_sliderAccent & 0x00FFFFFFu) | (36u << IM_COL32_A_SHIFT), 0, s(1.0f));
            char bubble[32];
            std::snprintf(bubble, sizeof(bubble), "%.4g", static_cast<double>(*value));
            const float bubbleTextWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, bubble).x;
            const float bubbleWidth = bubbleTextWidth + s(10.0f);
            ImVec2 bubblePos(thumb.x - bubbleWidth * 0.5f, thumb.y - s(22.0f));
            bubblePos.x = ImClamp(bubblePos.x, card.origin.x + s(4), card.origin.x + card.width - bubbleWidth - s(4));
            d->AddRectFilled(bubblePos, bubblePos + ImVec2(bubbleWidth, s(17)), (kInsetBg & 0x00FFFFFFu) | (242u << IM_COL32_A_SHIFT), s(7));
            d->AddRect(bubblePos, bubblePos + ImVec2(bubbleWidth, s(17)), (g_sliderAccent & 0x00FFFFFFu) | (110u << IM_COL32_A_SHIFT), s(7));
            textY(d, bubblePos.x + (bubbleWidth - bubbleTextWidth) * 0.5f, bubblePos.y, s(17), C(233, 235, 241), bubble, kTextSmall, nullptr);
        }
        d->AddCircleFilled(start + ImVec2(trackWidth * shown, s(1.5f)), s(5.5f), C(247, 248, 252));
    }

    char pillNumber[16];
    std::snprintf(pillNumber, sizeof(pillNumber), "%.4g", static_cast<double>(*value));
    const char* pillSuffix = suffix ? suffix : "";
    const float pillTextWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, pillNumber).x
        + ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, pillSuffix).x;
    const float pillWidth = ImClamp(pillTextWidth + s(12.0f), s(42.0f), s(118.0f));
    ImVec2 pill = rowControlPos(s(13.0f) + pillWidth);
    pill.y += rowCentered(s(21.0f));
    d->AddRectFilled(pill, pill + ImVec2(pillWidth, s(21)), kPillBg, s(7));

    ImGui::PushID(id + 500000);
    if (editing) {
        ImGui::SetCursorScreenPos(pill + ImVec2(s(4), s(3)));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kHairlineSoft);
        ImGui::PushStyleColor(ImGuiCol_Border, g_accent);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(4), s(2)));
        ImGui::PushItemWidth(pillWidth - s(8));
        ImGui::SetKeyboardFocusHere(0);
        ImGui::InputText("##edit", state.editBuffer, sizeof(state.editBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool committed = ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
        const bool lostFocus = ImGui::IsItemDeactivatedAfterEdit() || (!ImGui::IsItemActive() && !ImGui::IsItemFocused() && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered());
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (committed || lostFocus) {
            const float parsed = static_cast<float>(std::atof(state.editBuffer));
            *value = ImClamp(parsed, min, max);
            state.editingSlider = -1;
        }
    } else {
        if (hit("##pill_edit", pill, ImVec2(pillWidth, s(21))) && ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) {
            state.editingSlider = id;
            std::snprintf(state.editBuffer, sizeof(state.editBuffer), "%.4g", static_cast<double>(*value));
        }
        d->PushClipRect(pill, pill + ImVec2(pillWidth, s(21)), true);
        drawValuePillText(d, pill.x, pillWidth, pill.y, s(21), pillNumber, pillSuffix);
        d->PopClipRect();
    }
    ImGui::PopID();
    return *value != previousValue;
}

// Single-line text-input row: label on the left, pill-styled InputText on the right.
// Shared widget - script text inputs (gui.*), the glitch generator and any future native text
// row use this. extraFlags lets callers add ImGuiInputTextFlags_ReadOnly (preview fields).
// fullWidth switches to the native template-row geometry (chatTemplateRow / DiscordRpc): the
// field starts s(96) in and spans the rest of the card, instead of the compact right-aligned
// script-item pill - same widget, same interaction, the card's own width scale.
//
// The InputText is the ONLY ImGui item in the pill: the old version submitted an
// InvisibleButton first (hover tracking), but that button claimed the click's ActiveId and the
// InputText could NEVER gain keyboard focus - "cannot type anything in the field". Pill hover
// comes from a raw mouse-rect test instead, and hand-drawn popup occlusion is kept by
// submitting a Dummy instead of the input while a popup covers the row.
bool textInputRow(const char* label, char* buffer, int bufferSize, int id, ImGuiInputTextFlags extraFlags = 0, bool fullWidth = false) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float fieldWidth = fullWidth ? card.width - s(96.0f) - s(13.0f) : ImMin(s(200.0f), card.width * 0.55f);
    card.labelReserve = fullWidth ? s(96.0f) : fieldWidth + s(13.0f);
    beginRow(d, label);

    ImVec2 fp = fullWidth
        ? ImVec2(card.origin.x + s(96.0f), card.origin.y + (card.row - 1) * kRowHeight + rowCentered(s(21.0f)))
        : [&] { auto p = rowControlPos(fieldWidth + s(13.0f)); p.y += rowCentered(s(21.0f)); return p; }();

    const bool covered = coveredByPrevPopup(-1, fp, fp + ImVec2(fieldWidth, s(21)));
    const bool hovered = !covered && ImGui::IsMouseHoveringRect(fp, fp + ImVec2(fieldWidth, s(21)));
    const float response = motion(animKey(0x71e1u, id), hovered ? 1.0f : 0.0f);
    d->AddRectFilled(fp, fp + ImVec2(fieldWidth, s(21)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(fp, fp + ImVec2(fieldWidth, s(21)), mix(kHairlineSoft, g_accent, response), s(7));

    ImGui::PushID(id + 700000);
    ImGui::SetCursorScreenPos(fp + ImVec2(s(6), s(2)));
    if (covered) {
        ImGui::Dummy(ImVec2(fieldWidth - s(12), s(17))); // never claims hover (see hit())
        ImGui::PopID();
        return false;
    }
    ImGui::PushItemWidth(fieldWidth - s(12));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    const bool changed = ImGui::InputText("##text", buffer, static_cast<std::size_t>(bufferSize), extraFlags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopItemWidth();
    ImGui::PopID();
    return changed;
}

// Generic one-shot action row: label on the left, a centered-text pill button on the right
// (the chatActionRow / laggerProbeRow / userinfoRestoreRow visual). Returns true on click.
bool actionRow(const char* label, const char* buttonText, int id) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    card.labelReserve = controlWidth + s(13.0f);
    beginRow(d, label);

    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool clicked = hit("##action", cp, ImVec2(controlWidth, s(23)));
    const float response = motion(animKey(0x9a11u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));
    const float textWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, buttonText).x;
    textY(d, cp.x + (controlWidth - textWidth) * 0.5f, cp.y, s(23), mix(C(170, 173, 184), C(226, 228, 235), response), buttonText, kTextControl, nullptr);
    return clicked;
}

bool hueRow(const char* label, float* hue, int id) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    card.labelReserve = s(55.0f); // hue pill; the track dodges the label itself
    beginRow(d, label);

    const float previousHue = *hue;

    ImVec2 start = rowControlPos(s(175.0f));
    start.y += rowCentered(s(3.0f));
    float trackWidth = s(110.0f);
    // Long labels (e.g. "FOV Circle Hue") would run into the fixed-position track - shift the
    // track right past the label and shrink it instead of overlapping, keeping it clear of the
    // value pill (which starts s(55) from the card's right edge).
    const float labelEnd = card.origin.x + s(13.0f) + ImGui::GetFont()->CalcTextSizeA(kTextBody, FLT_MAX, 0.0f, label).x;
    const float trackRightEdge = card.origin.x + card.width - s(61.0f);
    if (start.x < labelEnd + s(8.0f))
        start.x = labelEnd + s(8.0f);
    if (start.x + trackWidth > trackRightEdge)
        trackWidth = ImMax(s(40.0f), trackRightEdge - start.x);

    ImGui::PushID(id);
    hit("##hue", start - ImVec2(s(5), s(8)), ImVec2(trackWidth + s(10), s(19)));
    if (ImGui::IsItemActive())
        *hue = ImClamp((ImGui::GetIO().MousePos.x - start.x) / trackWidth, 0.0f, 1.0f) * 359.0f;
    const float shown = motion(animKey(0x4d21u, id), *hue / 359.0f, 14.0f, *hue / 359.0f);
    ImGui::PopID();

    float r = 0, g = 0, b = 0;
    constexpr int segments = 12;
    for (int i = 0; i < segments; ++i) {
        ImGui::ColorConvertHSVtoRGB((i + 0.5f) / segments, 1.0f, 1.0f, r, g, b);
        d->AddRectFilled(start + ImVec2(trackWidth * i / segments, 0), start + ImVec2(trackWidth * (i + 1) / segments, s(3)),
            C(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255)), 1);
    }
    ImGui::ColorConvertHSVtoRGB(*hue / 359.0f, 1.0f, 1.0f, r, g, b);
    d->AddCircleFilled(start + ImVec2(trackWidth * shown, s(1.5f)), s(5.5f), C(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255)));

    ImVec2 pill = rowControlPos(s(55.0f));
    pill.y += rowCentered(s(21.0f));
    d->AddRectFilled(pill, pill + ImVec2(s(42), s(21)), kPillBg, s(7));
    d->AddRectFilled(pill + ImVec2(s(4), s(4)), pill + ImVec2(s(38), s(17)), C(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255)), s(3));
    char pillText[16];
    std::snprintf(pillText, sizeof(pillText), "%.0f", *hue);
    const float pillTextWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, pillText).x;
    textY(d, pill.x + (s(42) - pillTextWidth) * 0.5f, pill.y, s(21), C(166, 169, 179), pillText, kTextSmall, nullptr);
    return *hue != previousHue;
}

// --- multi-select combo (bitflag-style over bool config vars) ---------------------------

template <typename Var>
bool multiSelectGetter() noexcept
{
    return ui_config::get<Var>();
}

template <typename Var>
void multiSelectToggler() noexcept
{
    ui_config::set<Var>(!ui_config::get<Var>());
}

template <typename... BoolVars>
void multiSelectVar(const char* label, int id, const char* const (&optionNames)[sizeof...(BoolVars)]) noexcept
{
    constexpr std::size_t kCount = sizeof...(BoolVars);
    static constexpr bool (*const kGetters[kCount])() noexcept = { &multiSelectGetter<BoolVars>... };

    ImDrawList* d = ImGui::GetWindowDrawList();
    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    card.labelReserve = controlWidth + s(13.0f);
    beginRow(d, label);

    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool ownPopup = state.multiSelectOpen && state.multiSelectOwner == id;
    const bool click = ownPopup ? hitModal("##multiselect", cp, ImVec2(controlWidth, s(23)))
                                : hit("##multiselect", cp, ImVec2(controlWidth, s(23)));
    const bool open = state.multiSelectOpen && state.multiSelectOwner == id;
    const float response = motion(animKey(0x71abu, id), (open || ImGui::IsItemHovered()) ? 1.0f : 0.0f);
    ImGui::PopID();

    if (click) {
        state.multiSelectOpen = !open;
        state.multiSelectOwner = id;
        state.colorPickerOpen = false;
        styleSelect.open = false;
        state.multiSelectOpenedFrame = ImGui::GetFrameCount();
        state.multiSelectAnchor = cp;
        state.multiSelectCount = static_cast<int>(kCount);
        state.multiSelectGet = [](std::size_t i) { constexpr bool (*const getters[kCount])() noexcept = { &multiSelectGetter<BoolVars>... }; return getters[i](); };
        state.multiSelectToggle = [](std::size_t i) { constexpr void (*const toggles[kCount])() noexcept = { &multiSelectToggler<BoolVars>... }; toggles[i](); };
        state.multiSelectNames = optionNames;
    }

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));

    std::size_t selected = 0;
    for (std::size_t i = 0; i < kCount; ++i)
        selected += kGetters[i]() ? 1 : 0;
    char pillText[16];
    std::snprintf(pillText, sizeof(pillText), "%u/%u", static_cast<unsigned>(selected), static_cast<unsigned>(kCount));
    textY(d, cp.x + s(7), cp.y, s(23), C(170, 173, 184), pillText, kTextControl, nullptr);
    d->AddLine(cp + ImVec2(controlWidth - s(13), s(9)), cp + ImVec2(controlWidth - s(9), s(13)), C(139, 143, 154), 1.0f);
    d->AddLine(cp + ImVec2(controlWidth - s(9), s(13)), cp + ImVec2(controlWidth - s(5), s(9)), C(139, 143, 154), 1.0f);
}

void multiSelectPopover(ImDrawList* d) noexcept
{
    if (!state.multiSelectOpen || !state.multiSelectGet)
        return;

    const float width = s(160.0f);
    const float rowHeight = s(26.0f);
    const ImVec2 size(width, state.multiSelectCount * rowHeight + s(6.0f));
    // flip above the anchor control when the popover would run past the clip bottom - the shell
    // window clips its own draw list, so the clip rect (not the display) is the real bound
    const bool openAbove = state.multiSelectAnchor.y + s(23.0f) + s(4.0f) + size.y > d->GetClipRectMax().y;
    const ImVec2 p(state.multiSelectAnchor.x,
        openAbove ? state.multiSelectAnchor.y - size.y - s(4.0f) : state.multiSelectAnchor.y + s(23.0f) + s(4.0f));
    recordPopupRect(PopupMultiSelect, p, p + size);
    softShadow(d, p, p + size, s(10.0f));
    d->AddRectFilled(p, p + size, kPopupBg, s(8));
    d->AddRect(p, p + size, kPopupBorder, s(8));

    const bool accepts = ImGui::GetFrameCount() > state.multiSelectOpenedFrame;
    for (int i = 0; i < state.multiSelectCount; ++i) {
        const ImVec2 rp = p + ImVec2(s(4), s(3) + i * rowHeight);
        ImGui::PushID(7500 + i);
        const bool clicked = hitModal("##ms_row", rp, ImVec2(size.x - s(8), rowHeight - s(4)));
        const float hover = motion(animKey(0xa11ceu, 7500 + i), ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
        ImGui::PopID();
        if (hover > 0.001f)
            d->AddRectFilled(rp, rp + ImVec2(size.x - s(8), rowHeight - s(4)), C(255, 255, 255, static_cast<int>(12 * hover)), s(6));

        const bool on = state.multiSelectGet(static_cast<std::size_t>(i));
        const ImVec2 cb = rp + ImVec2(s(8), (rowHeight - s(4) - s(12)) * 0.5f);
        d->AddRectFilled(cb, cb + ImVec2(s(12), s(12)), on ? g_buttonAccent : kPillBg, s(3));
        if (on) {
            d->AddLine(cb + ImVec2(s(2.5f), s(6.0f)), cb + ImVec2(s(5.0f), s(9.0f)), C(255, 255, 255), s(1.4f));
            d->AddLine(cb + ImVec2(s(5.0f), s(9.0f)), cb + ImVec2(s(9.5f), s(2.5f)), C(255, 255, 255), s(1.4f));
        }
        textY(d, rp.x + s(28), rp.y, rowHeight - s(4), C(182, 185, 196), state.multiSelectNames[i], kTextControl, nullptr);
        if (clicked && accepts)
            state.multiSelectToggle(static_cast<std::size_t>(i));
    }

    if (accepts && clickedOutside(p, p + size))
        state.multiSelectOpen = false;
}

// --- player multi-select (CHEAT O METER scan targets) ------------------------------------
// The same popover machinery as the static multi-select above, but the option rows are the
// CURRENT players - refreshed every rendered frame from the player-list snapshot (steal-list
// style), with the same live-reorder caveat - and the checkboxes read/write the analyzer's
// cross-thread selection set (cheat_ometer) instead of config vars. Selection keys on the
// 0-based player slot, so the analyzer and the ESP tags agree with the picker.
struct AnalyzerSelRow {
    int slot = -1;
    char name[cheat_ometer::kMaxNameLen]{};
};
AnalyzerSelRow analyzerSelCache[cheat_ometer::kMaxRows]{};
const char* analyzerSelOptions[cheat_ometer::kMaxRows]{};
int analyzerSelRowCount = 0;

void refreshAnalyzerSelList() noexcept
{
    const auto snap = player_list::snapshot();
    analyzerSelRowCount = 0;
    for (int i = 0; i < snap.count && analyzerSelRowCount < cheat_ometer::kMaxRows; ++i) {
        if (snap.rows[i].isLocalPlayer || snap.rows[i].name[0] == '\0' || snap.rows[i].slot < 0)
            continue;
        analyzerSelCache[analyzerSelRowCount].slot = snap.rows[i].slot;
        std::snprintf(analyzerSelCache[analyzerSelRowCount].name, sizeof(analyzerSelCache[0].name), "%s", snap.rows[i].name);
        analyzerSelOptions[analyzerSelRowCount] = analyzerSelCache[analyzerSelRowCount].name;
        ++analyzerSelRowCount;
    }
}

bool analyzerSelGet(std::size_t i) noexcept
{
    return i < static_cast<std::size_t>(analyzerSelRowCount) && cheat_ometer::isSelected(analyzerSelCache[i].slot);
}

void analyzerSelToggle(std::size_t i) noexcept
{
    if (i >= static_cast<std::size_t>(analyzerSelRowCount))
        return;
    cheat_ometer::setSelected(analyzerSelCache[i].slot, !cheat_ometer::isSelected(analyzerSelCache[i].slot));
}

void analyzerMultiSelect(const char* label, int id) noexcept
{
    refreshAnalyzerSelList();

    ImDrawList* d = ImGui::GetWindowDrawList();
    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    card.labelReserve = controlWidth + s(13.0f);
    beginRow(d, label);

    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    static const char* const kNoPlayers[1] = {"no players - live match needed"};

    ImGui::PushID(id);
    const bool ownPopup = state.multiSelectOpen && state.multiSelectOwner == id;
    const bool click = ownPopup ? hitModal("##multiselect", cp, ImVec2(controlWidth, s(23)))
                                : hit("##multiselect", cp, ImVec2(controlWidth, s(23)));
    const bool open = state.multiSelectOpen && state.multiSelectOwner == id;
    const float response = motion(animKey(0x71abu, id), (open || ImGui::IsItemHovered()) ? 1.0f : 0.0f);
    ImGui::PopID();

    if (click) {
        state.multiSelectOpen = !open;
        state.multiSelectOwner = id;
        state.colorPickerOpen = false;
        styleSelect.open = false;
        state.multiSelectOpenedFrame = ImGui::GetFrameCount();
        state.multiSelectAnchor = cp;
        state.multiSelectCount = analyzerSelRowCount > 0 ? analyzerSelRowCount : 1;
        state.multiSelectGet = &analyzerSelGet;
        state.multiSelectToggle = &analyzerSelToggle;
        state.multiSelectNames = analyzerSelRowCount > 0 ? analyzerSelOptions : kNoPlayers;
    }

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));

    char pillText[16];
    std::snprintf(pillText, sizeof(pillText), "%d/%d", cheat_ometer::selectedCount(), analyzerSelRowCount);
    textY(d, cp.x + s(7), cp.y, s(23), C(170, 173, 184), pillText, kTextControl, nullptr);
    d->AddLine(cp + ImVec2(controlWidth - s(13), s(9)), cp + ImVec2(controlWidth - s(9), s(13)), C(139, 143, 154), 1.0f);
    d->AddLine(cp + ImVec2(controlWidth - s(9), s(13)), cp + ImVec2(controlWidth - s(5), s(9)), C(139, 143, 154), 1.0f);
}

// --- feature-bind popup (right-click on a registered toggle) -----------------------------
//
// Context menu over the toggled row: capture a key (the shared State::Capture machine with a
// dedicated owner id), pick Toggle/Hold mode, unbind. Edits go straight into the FeatureBinds
// registry and persist to <configDir>/feature_binds.txt on every change (tiny file). Closes on
// outside click / Escape - but never while a key capture is in flight (the user is pressing
// keys somewhere, possibly outside the popup).

void featureBindPopover(ImDrawList* d) noexcept
{
    if (!state.featureBindOpen)
        return;
    if (state.featureBindIndex < 0 || state.featureBindIndex >= static_cast<int>(feature_binds::entryCount)) {
        state.featureBindOpen = false;
        return;
    }
    auto& entry = feature_binds::entries[state.featureBindIndex];

    // Another popup took over - yield (the openers close us too; this is belt-and-braces).
    if (state.popup.open || state.multiSelectOpen || state.colorPickerOpen || styleSelect.open)
        state.featureBindOpen = false;

    const bool accepts = ImGui::GetFrameCount() > state.featureBindOpenedFrame;
    const bool capturing = state.capture != State::Capture::Inactive && state.captureOwner == State::kFeatureBindCaptureOwner;

    constexpr float width = 190.0f;
    const float rowH = s(25.0f);
    const float gap = s(7.0f);
    const float headerH = s(30.0f);
    const float height = headerH + rowH + gap + rowH + gap + rowH + s(10.0f);
    ImVec2 p = state.featureBindAnchor;
    // clamp inside the shell clip rect (same rule as the dropdown popover)
    const ImVec2 clipMin = d->GetClipRectMin();
    const ImVec2 clipMax = d->GetClipRectMax();
    p.x = ImClamp(p.x, clipMin.x + s(10.0f), ImMax(clipMin.x + s(10.0f), clipMax.x - width - s(10.0f)));
    p.y = ImClamp(p.y, clipMin.y + s(10.0f), ImMax(clipMin.y + s(10.0f), clipMax.y - height - s(10.0f)));
    const ImVec2 size{width, height};

    recordPopupRect(PopupFeatureBind, p, p + size);
    softShadow(d, p, p + size, s(14.0f));
    d->AddRectFilled(p, p + size, (kInsetBg & 0x00FFFFFFu) | (248u << IM_COL32_A_SHIFT), s(12));
    // the menu's card depth: wide top-light wash + inner highlight
    d->AddRectFilledMultiColor(p + ImVec2(s(10), s(1)), p + ImVec2(width - s(10), s(26)), C(255, 255, 255, 12), C(255, 255, 255, 12), 0, 0);
    d->AddLine(p + ImVec2(s(8), s(0.75f)), p + ImVec2(width - s(8), s(0.75f)), C(255, 255, 255, 18), 1.0f);
    d->AddRect(p, p + size, C(56, 56, 62, 215), s(12));

    // header band: accent dot + label + hairline divider
    pulsingDot(d, p + ImVec2(s(15), headerH * 0.5f - s(1)), s(2.2f), g_accent);
    textY(d, p.x + s(23), p.y + s(6), s(16), C(196, 199, 208), entry.label ? entry.label : "Bind", kTextCaption, strongFont());
    d->AddLine(p + ImVec2(s(8), headerH - s(2)), p + ImVec2(width - s(8), headerH - s(2)), kHairline, 1.0f);

    float y = p.y + headerH + s(4);

    // --- key pill (click to capture) ---
    {
        ImGui::PushID(91001);
        const bool clicked = hitModal("##fb_key", ImVec2{p.x + s(10), y}, ImVec2{width - s(20), rowH});
        ImGui::PopID();
        if (clicked && state.capture == State::Capture::Inactive) {
            state.capture = State::Capture::WaitingRelease;
            state.captureOwner = State::kFeatureBindCaptureOwner;
        }

        bool held = false;
        if (state.captureOwner == State::kFeatureBindCaptureOwner) {
            if (state.capture == State::Capture::WaitingRelease) {
                if (!gui_sdl::anyInputHeld())
                    state.capture = State::Capture::WaitingPress;
            } else if (state.capture == State::Capture::WaitingPress) {
                if (gui_sdl::scancodeDown[76]) { // Delete clears
                    entry.key = Bind::kOff;
                    feature_binds::save();
                    state.capture = State::Capture::Inactive;
                } else if (gui_sdl::scancodeDown[41]) { // Escape cancels
                    state.capture = State::Capture::Inactive;
                } else {
                    const std::uint32_t mask = gui_sdl::liveMouseMask();
                    const auto down = [mask](std::uint32_t button) { return (mask & (1u << (button - 1))) != 0; };
                    int bind = Bind::kOff;
                    if (down(4)) bind = Bind::kMouse4;
                    else if (down(5)) bind = Bind::kMouse5;
                    else if (down(3)) bind = Bind::kMouse3;
                    else if (down(1)) bind = Bind::kMouse1;
                    else if (down(2)) bind = Bind::kMouse2;
                    else {
                        for (int scancode = Bind::kMinScancode; scancode <= Bind::kMaxScancode; ++scancode) {
                            if (gui_sdl::scancodeDown[scancode]) {
                                bind = scancode;
                                break;
                            }
                        }
                    }
                    if (bind != Bind::kOff) {
                        entry.key = bind;
                        entry.lastKeyDown = true; // the captured key is down right now - do not let
                                                  // the next apply() tick treat it as a fresh edge
                        feature_binds::save();
                        state.capture = State::Capture::Inactive;
                    }
                }
            }
        }
        held = entry.key != Bind::kOff && Bind::isDown(entry.key) && state.capture == State::Capture::Inactive;

        const char* keyText = (state.captureOwner == State::kFeatureBindCaptureOwner && state.capture == State::Capture::WaitingPress) ? "PRESS ANY KEY"
            : (state.captureOwner == State::kFeatureBindCaptureOwner && state.capture == State::Capture::WaitingRelease) ? "RELEASE ALL"
            : Bind::displayName(entry.key);
        d->AddRectFilled(ImVec2{p.x + s(10), y}, ImVec2{p.x + s(10) + width - s(20), y + rowH},
                         held ? g_buttonAccent : kPillBg, s(7));
        d->AddRect(ImVec2{p.x + s(10), y}, ImVec2{p.x + s(10) + width - s(20), y + rowH},
                   held ? g_buttonAccent : kHairlineSoft, s(7));
        textYCentered(d, p.x + s(10), width - s(20), y, rowH, held ? kInsetBg : C(170, 173, 184), keyText, kTextControl, nullptr);
        y += rowH + gap;
    }

    // --- mode pills: TOGGLE | HOLD ---
    {
        const float pillWidth = (width - s(20) - gap) * 0.5f;
        for (int mode = 0; mode < 2; ++mode) {
            const float x = p.x + s(10) + mode * (pillWidth + s(6));
            ImGui::PushID(91010 + mode);
            const bool clicked = hitModal("##fb_mode", ImVec2{x, y}, ImVec2{pillWidth, rowH});
            ImGui::PopID();
            if (clicked && accepts && entry.key != Bind::kOff && entry.holdMode != (mode != 0)) {
                entry.holdMode = mode != 0;
                entry.lastKeyDown = false;
                feature_binds::save();
            }
            const bool active = entry.holdMode == (mode != 0);
            d->AddRectFilled(ImVec2{x, y}, ImVec2{x + pillWidth, y + rowH},
                             active ? mix(kPillBg, g_buttonAccent, 0.55f) : kPillBg, s(7));
            d->AddRect(ImVec2{x, y}, ImVec2{x + pillWidth, y + rowH},
                       active ? g_buttonAccent : kHairlineSoft, s(7));
            textYCentered(d, x, pillWidth, y, rowH, active ? C(248, 249, 252) : C(170, 173, 184), mode == 0 ? "TOGGLE" : "HOLD", kTextControl, nullptr);
        }
        y += rowH + gap;
    }

    // --- unbind pill (only meaningful with a key) ---
    if (entry.key != Bind::kOff) {
        ImGui::PushID(91020);
        const bool clicked = hitModal("##fb_unbind", ImVec2{p.x + s(10), y}, ImVec2{width - s(20), rowH});
        ImGui::PopID();
        if (clicked && accepts) {
            entry.key = Bind::kOff;
            entry.lastKeyDown = false;
            feature_binds::save();
        }
        d->AddRectFilled(ImVec2{p.x + s(10), y}, ImVec2{p.x + s(10) + width - s(20), y + rowH}, kPillBg, s(7));
        d->AddRect(ImVec2{p.x + s(10), y}, ImVec2{p.x + s(10) + width - s(20), y + rowH}, kHairlineSoft, s(7));
        textYCentered(d, p.x + s(10), width - s(20), y, rowH, C(206, 110, 110), "UNBIND", kTextControl, nullptr);
    }

    if (accepts && !capturing && state.capture == State::Capture::Inactive
        && (clickedOutside(p, p + size) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
        state.featureBindOpen = false;
}

// --- RGBA color picker ------------------------------------------------------------------
//
// One RGBA config var (color::Rgba, 0xRRGGBBAA). The popover shows four channel strips
// (gradient segments in the hueRow style); dragging a strip rewrites that channel.

template <typename Var>
color::Rgba colorPickerGetter() noexcept
{
    return ui_config::get<Var>();
}

template <typename Var>
void colorPickerSetter(color::Rgba value) noexcept
{
    ui_config::set<Var>(value);
}

template <typename Var>
void colorVar(const char* label, int id) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    card.labelReserve = s(55.0f); // compact swatch
    beginRow(d, label);

    // Compact swatch control: a full-width pill around a small color square read as a big
    // empty space - the swatch IS the control.
    const float controlWidth = s(42.0f);
    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool ownPicker = state.colorPickerOpen && state.colorPickerOwner == id;
    const bool click = ownPicker ? hitModal("##color", cp, ImVec2(controlWidth, s(23)))
                                 : hit("##color", cp, ImVec2(controlWidth, s(23)));
    const bool open = state.colorPickerOpen && state.colorPickerOwner == id;
    const float response = motion(animKey(0xc010u, id), (open || ImGui::IsItemHovered()) ? 1.0f : 0.0f);
    ImGui::PopID();

    if (click) {
        state.colorPickerOpen = !open;
        state.colorPickerOwner = id;
        state.colorPickerOpenedFrame = ImGui::GetFrameCount();
        state.colorPickerAnchor = cp;
        state.colorGet = &colorPickerGetter<Var>;
        state.colorSet = &colorPickerSetter<Var>;
        state.multiSelectOpen = false;
        styleSelect.open = false;
    }

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));

    const auto color = ui_config::get<Var>();
    const ImVec2 swatch = cp + ImVec2(s(6), s(4));
    d->AddRectFilled(swatch, swatch + ImVec2(s(20), s(15)), C(color.r(), color.g(), color.b(), color.a()), s(3));
    // hex readout lives in the picker popover now - the row just shows the swatch
}

// Spread-circle color row: like colorVar, but alpha 0 means "follow the crosshair" - the pill
// shows a "Crosshair" placeholder instead of a swatch, and setting alpha to 0 in the picker
// resets to that state (an "unpick" path the plain colorVar rows don't have).
void spreadCircleColorVar(const char* label, int id) noexcept
{
    const auto color = ui_config::get<spread_circle_vars::SpreadCircleColor>();
    if (color.a() == 0) {
        // not customized: render the pill with the placeholder text, still opens the picker
        ImDrawList* d = ImGui::GetWindowDrawList();
        const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
        card.labelReserve = controlWidth + s(13.0f);
        beginRow(d, label);
        ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
        cp.y += rowCentered(s(23.0f));

        ImGui::PushID(id);
        const bool ownPicker = state.colorPickerOpen && state.colorPickerOwner == id;
        const bool click = ownPicker ? hitModal("##color", cp, ImVec2(controlWidth, s(23)))
                                     : hit("##color", cp, ImVec2(controlWidth, s(23)));
        const bool open = state.colorPickerOpen && state.colorPickerOwner == id;
        const float response = motion(animKey(0xc011u, id), (open || ImGui::IsItemHovered()) ? 1.0f : 0.0f);
        ImGui::PopID();

        if (click) {
            state.colorPickerOpen = !open;
            state.colorPickerOwner = id;
            state.colorPickerOpenedFrame = ImGui::GetFrameCount();
            state.colorPickerAnchor = cp;
            state.colorGet = &colorPickerGetter<spread_circle_vars::SpreadCircleColor>;
            state.colorSet = &colorPickerSetter<spread_circle_vars::SpreadCircleColor>;
            state.multiSelectOpen = false;
            styleSelect.open = false;
        }

        d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
        d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));
        textY(d, cp.x + (controlWidth - ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, "Crosshair").x) * 0.5f,
            cp.y, s(23), C(140, 144, 154), "Crosshair", kTextControl, nullptr);
        return;
    }
    colorVar<spread_circle_vars::SpreadCircleColor>(label, id);
}

// Discord-style picker: saturation/value square + hue bar + alpha bar + hex readout.
void colorPickerPopover(ImDrawList* d) noexcept
{
    if (!state.colorPickerOpen || !state.colorGet)
        return;

    const auto color = state.colorGet();

    // HSV state per open: re-seed from the current color whenever a different picker opens
    // (same statics shared by all colorVar rows - only one popover is ever open).
    static float hue = 0.0f, sat = 0.0f, val = 0.0f;
    static int seededOwner = -1;
    static int dragRegion = 0; // 0 none, 1 sv, 2 hue, 3 alpha
    if (seededOwner != state.colorPickerOwner) {
        seededOwner = state.colorPickerOwner;
        dragRegion = 0;
        float rgb[3] = {color.r() / 255.0f, color.g() / 255.0f, color.b() / 255.0f};
        ImGui::ColorConvertRGBtoHSV(rgb[0], rgb[1], rgb[2], hue, sat, val);
        if (sat <= 0.0f && val <= 0.0f)
            hue = 0.0f; // grayscale keeps a usable square
    }

    const float width = s(170.0f);
    const float squareH = s(96.0f);
    const float barH = s(9.0f);
    const float gap = s(9.0f);
    const float height = s(10) + squareH + gap + barH + gap + barH + gap + s(18);
    // flip above the anchor swatch when the popover would run past the clip bottom - the shell
    // window clips its own draw list, so the clip rect (not the display) is the real bound
    const bool openAbove = state.colorPickerAnchor.y + s(23.0f) + s(4.0f) + height > d->GetClipRectMax().y;
    // horizontal clamp: pills on the left side of a card used to push the picker past the
    // window's right clip edge (half the SV square/hex readout cut off)
    float pickerX = state.colorPickerAnchor.x;
    const ImVec2 clipMin = d->GetClipRectMin();
    const float clipMaxX = d->GetClipRectMax().x;
    pickerX = ImMax(pickerX, clipMin.x + s(6.0f));
    pickerX = ImMin(pickerX, clipMaxX - width - s(6.0f));
    const ImVec2 p(pickerX,
        openAbove ? state.colorPickerAnchor.y - height - s(4.0f) : state.colorPickerAnchor.y + s(23.0f) + s(4.0f));
    const ImVec2 size(width, height);
    recordPopupRect(PopupColor, p, p + size);
    softShadow(d, p, p + size, s(12.0f));
    // opaque body: the gradient's corner caps are painted opaque too - at 245 the caps would
    // show as mismatched lighter patches over the body (and whatever is behind the picker)
    d->AddRectFilled(p, p + size, kInsetBg, s(12));
    d->AddRect(p, p + size, kPopupBorder, s(12));

    const ImVec2 squarePos = p + ImVec2(s(9), s(9));
    const ImVec2 squareSize = ImVec2(width - s(18), squareH);
    const ImVec2 huePos = ImVec2(squarePos.x, squarePos.y + squareH + gap);
    const ImVec2 alphaPos = ImVec2(squarePos.x, huePos.y + barH + gap);
    const float barWidth = squareSize.x;

    // --- interaction: raw mouse hit tests, NO ImGui items. The page rows behind the picker are
    // submitted first and (in imgui 1.91.7) claim HoveredId for the rest of the frame - any
    // InvisibleButton submitted by the picker loses them the click. Mouse-position hit testing
    // cannot lose that race, and the covered rows stay Dummy-suppressed via the popup rect.
    const bool clickReady = ImGui::GetFrameCount() > state.colorPickerOpenedFrame;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const auto inRect = [](const ImVec2& m, const ImVec2& min, const ImVec2& max) {
        return m.x >= min.x && m.x <= max.x && m.y >= min.y && m.y <= max.y;
    };
    if (clickReady && ImGui::IsMouseClicked(0)) {
        if (inRect(mouse, squarePos, squarePos + squareSize))
            dragRegion = 1;
        else if (inRect(mouse, huePos, huePos + ImVec2(barWidth, barH)))
            dragRegion = 2;
        else if (inRect(mouse, alphaPos, alphaPos + ImVec2(barWidth, barH)))
            dragRegion = 3;
    }
    if (ImGui::IsMouseReleased(0))
        dragRegion = 0;

    const auto applyHsv = [&]() {
        float rr = 0.0f, gg = 0.0f, bb = 0.0f;
        ImGui::ColorConvertHSVtoRGB(hue, sat, val, rr, gg, bb);
        state.colorSet(color::Rgba{static_cast<std::uint8_t>(rr * 255 + 0.5f), static_cast<std::uint8_t>(gg * 255 + 0.5f),
            static_cast<std::uint8_t>(bb * 255 + 0.5f), color.a()});
    };

    // rounded-corner caps: paint the popover-bg "corner square minus quarter disc" piece over
    // already-drawn content (AddRectFilledMultiColor cannot round itself). Star-shaped from the
    // corner, so a small triangle fan is exact - no convexity limit.
    const auto cornerCap = [&](ImVec2 k, ImVec2 cOffset, float amin, float amax, float r) {
        constexpr ImU32 bg = IM_COL32(18, 18, 20, 255);
        const ImVec2 c = k + cOffset;
        constexpr int steps = 10;
        ImVec2 prev = k;
        for (int i = 0; i <= steps; ++i) {
            const float a = amin + (amax - amin) * static_cast<float>(i) / static_cast<float>(steps);
            const ImVec2 pt(c.x + r * std::cos(a), c.y + r * std::sin(a));
            if (i > 0)
                d->AddTriangleFilled(k, prev, pt, bg);
            prev = pt;
        }
    };
    const auto roundRectCorners = [&](ImVec2 min, ImVec2 max, float r) {
        cornerCap(min, ImVec2(r, r), IM_PI, IM_PI * 1.5f, r);                      // TL
        cornerCap(ImVec2(max.x, min.y), ImVec2(-r, r), -IM_PI * 0.5f, 0.0f, r);    // TR
        cornerCap(ImVec2(min.x, max.y), ImVec2(r, -r), IM_PI * 0.5f, IM_PI, r);    // BL
        cornerCap(max, ImVec2(-r, -r), 0.0f, IM_PI * 0.5f, r);                     // BR
    };

    // --- SV square: white->hue horizontally, then a black vertical fade on top (imgui's trick) ---
    float hr = 0.0f, hg = 0.0f, hb = 0.0f;
    ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, hr, hg, hb);
    const ImU32 hueColor = C(static_cast<int>(hr * 255), static_cast<int>(hg * 255), static_cast<int>(hb * 255));
    d->AddRectFilledMultiColor(squarePos, squarePos + squareSize, C(255, 255, 255), hueColor, hueColor, C(255, 255, 255));
    d->AddRectFilledMultiColor(squarePos, squarePos + squareSize, C(0, 0, 0, 0), C(0, 0, 0, 0), C(0, 0, 0, 255), C(0, 0, 0, 255));
    roundRectCorners(squarePos, squarePos + squareSize, s(6.0f));
    d->AddRect(squarePos, squarePos + squareSize, C(0, 0, 0, 110), s(6.0f));
    if (dragRegion == 1 && ImGui::IsMouseDown(0)) {
        sat = ImClamp((mouse.x - squarePos.x) / squareSize.x, 0.0f, 1.0f);
        val = 1.0f - ImClamp((mouse.y - squarePos.y) / squareSize.y, 0.0f, 1.0f);
        applyHsv();
    }
    d->AddCircle(ImVec2(squarePos.x + squareSize.x * sat, squarePos.y + squareSize.y * (1.0f - val)), s(4), C(255, 255, 255, 230), 0, s(1.5f));

    // --- hue bar: one smooth rainbow fade (interpolated quads between the six stops) ---
    constexpr ImU32 hueStops[6] = {C(255, 0, 0), C(255, 255, 0), C(0, 255, 0), C(0, 255, 255), C(0, 0, 255), C(255, 0, 255)};
    for (int i = 0; i < 5; ++i) {
        const float x0 = huePos.x + barWidth * static_cast<float>(i) / 5.0f;
        const float x1 = huePos.x + barWidth * static_cast<float>(i + 1) / 5.0f;
        d->AddRectFilledMultiColor(ImVec2(x0, huePos.y), ImVec2(x1, huePos.y + barH), hueStops[i], hueStops[i + 1], hueStops[i + 1], hueStops[i]);
    }
    if (dragRegion == 2 && ImGui::IsMouseDown(0)) {
        hue = ImClamp((mouse.x - huePos.x) / barWidth, 0.0f, 0.999f);
        applyHsv();
    }
    roundRectCorners(huePos, huePos + ImVec2(barWidth, barH), s(4.0f));
    d->AddRect(huePos, huePos + ImVec2(barWidth, barH), C(0, 0, 0, 90), s(4.0f));
    d->AddCircleFilled(ImVec2(huePos.x + barWidth * hue, huePos.y + barH * 0.5f), s(4.5f), C(247, 248, 252));
    d->AddCircle(ImVec2(huePos.x + barWidth * hue, huePos.y + barH * 0.5f), s(4.5f), C(0, 0, 0, 120), 0, s(1.2f));

    // --- alpha bar: checkerboard under a transparent->color fade ---
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 2; ++j)
            d->AddRectFilled(ImVec2(alphaPos.x + barWidth / 8 * i, alphaPos.y + barH / 2 * j),
                ImVec2(alphaPos.x + barWidth / 8 * (i + 1), alphaPos.y + barH / 2 * (j + 1)),
                ((i + j) & 1) ? C(70, 70, 74) : C(112, 112, 118));
    d->AddRectFilledMultiColor(alphaPos, alphaPos + ImVec2(barWidth, barH), C(color.r(), color.g(), color.b(), 0), C(color.r(), color.g(), color.b(), 255), C(color.r(), color.g(), color.b(), 255), C(color.r(), color.g(), color.b(), 0));
    if (dragRegion == 3 && ImGui::IsMouseDown(0)) {
        const float newAlpha = ImClamp((mouse.x - alphaPos.x) / barWidth, 0.0f, 1.0f);
        state.colorSet(color::Rgba{color.r(), color.g(), color.b(), static_cast<std::uint8_t>(newAlpha * 255 + 0.5f)});
    }
    roundRectCorners(alphaPos, alphaPos + ImVec2(barWidth, barH), s(4.0f));
    d->AddRect(alphaPos, alphaPos + ImVec2(barWidth, barH), C(0, 0, 0, 90), s(4.0f));
    d->AddCircleFilled(ImVec2(alphaPos.x + barWidth * (color.a() / 255.0f), alphaPos.y + barH * 0.5f), s(4.5f), C(247, 248, 252));
    d->AddCircle(ImVec2(alphaPos.x + barWidth * (color.a() / 255.0f), alphaPos.y + barH * 0.5f), s(4.5f), C(0, 0, 0, 120), 0, s(1.2f));

    // hex readout
    char hex[10];
    std::snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X", color.r(), color.g(), color.b(), color.a());
    const float hexWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, hex).x;
    textY(d, squarePos.x + (width - s(18) - hexWidth) * 0.5f, alphaPos.y + barH + gap, s(14), C(170, 173, 184), hex, kTextControl, nullptr);

    // click anywhere outside the picker (and its anchor swatch) closes it
    if (ImGui::GetFrameCount() > state.colorPickerOpenedFrame
        && clickedOutside(p, p + size)
        && clickedOutside(state.colorPickerAnchor, state.colorPickerAnchor + ImVec2(s(72.0f), s(20.0f))))
        state.colorPickerOpen = false;
}

// --- RGBA color picker end ---------------------------------------------------------------

// --- float slider (config var: InRange<float, min, max>) ---------------------------------
// Same interaction model as the int sliderRow: drag the track or click the pill to type an
// exact value. Values serialize as JSON numbers with 2 decimals.

template <typename Var>
void floatSliderVar(const char* label, int id, const char* suffix = "") noexcept
{
    using Range = typename Var::ValueType;
    ImDrawList* d = ImGui::GetWindowDrawList();
    beginRow(d, label);

    constexpr float kMin = static_cast<float>(Range::kMin);
    constexpr float kMax = static_cast<float>(Range::kMax);
    const float value = static_cast<float>(ui_config::get<Var>());
    const bool editing = state.editingSlider == id;
    const float position = ImClamp((value - kMin) / (kMax - kMin), 0.0f, 1.0f);

    if (!editing) {
        ImVec2 start = rowControlPos(s(145.0f));
        start.y += rowCentered(s(3.0f));
        const float trackWidth = s(80.0f);

        ImGui::PushID(id);
        hit("##fslider", start - ImVec2(s(5), s(8)), ImVec2(trackWidth + s(10), s(19)));
        if (ImGui::IsItemActive()) {
            const float t = ImClamp((ImGui::GetIO().MousePos.x - start.x) / trackWidth, 0.0f, 1.0f);
            ui_config::set<Var>(Range{kMin + t * (kMax - kMin)});
        }
        const float shown = motion(animKey(0xf8a1u, id), position, 14.0f, position);
        ImGui::PopID();

        d->AddRectFilled(start, start + ImVec2(trackWidth, s(3)), C(33, 33, 36), s(2));
        d->AddRectFilled(start, start + ImVec2(trackWidth * shown, s(3)), g_sliderAccent, s(2));
        d->AddCircleFilled(start + ImVec2(trackWidth * shown, s(1.5f)), s(5.5f), C(247, 248, 252));
    }

    // Value pill: click to type an exact value. Adaptive decimals + a pill that grows leftward
    // keep big values (fog distance "3873.73") inside the pill.
    char pillNumber[24];
    if (value >= 100.0f || value <= -100.0f)
        std::snprintf(pillNumber, sizeof(pillNumber), "%.0f", static_cast<double>(value));
    else if (value >= 10.0f || value <= -10.0f)
        std::snprintf(pillNumber, sizeof(pillNumber), "%.1f", static_cast<double>(value));
    else
        std::snprintf(pillNumber, sizeof(pillNumber), "%.2f", static_cast<double>(value));
    const char* pillSuffix = suffix ? suffix : "";
    const float pillTextWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, pillNumber).x
        + ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, pillSuffix).x;
    const float pillWidth = ImClamp(pillTextWidth + s(12.0f), s(42.0f), s(118.0f));
    ImVec2 pill = rowControlPos(s(13.0f) + pillWidth);
    pill.y += rowCentered(s(21.0f));
    d->AddRectFilled(pill, pill + ImVec2(pillWidth, s(21)), kPillBg, s(7));

    ImGui::PushID(id + 600000);
    if (editing) {
        ImGui::SetCursorScreenPos(pill + ImVec2(s(4), s(3)));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kHairlineSoft);
        ImGui::PushStyleColor(ImGuiCol_Border, g_accent);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(4), s(2)));
        ImGui::PushItemWidth(pillWidth - s(8));
        ImGui::SetKeyboardFocusHere(0);
        ImGui::InputText("##fedit", state.editBuffer, sizeof(state.editBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool committed = ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
        const bool lostFocus = ImGui::IsItemDeactivatedAfterEdit() || (!ImGui::IsItemActive() && !ImGui::IsItemFocused() && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered());
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (committed || lostFocus) {
            const float parsed = std::strtof(state.editBuffer, nullptr);
            ui_config::set<Var>(Range{ImClamp(parsed, kMin, kMax)});
            state.editingSlider = -1;
        }
    } else {
        if (hit("##fpill_edit", pill, ImVec2(pillWidth, s(21))) && ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) {
            state.editingSlider = id;
            std::snprintf(state.editBuffer, sizeof(state.editBuffer), "%.2f", static_cast<double>(value));
        }
        d->PushClipRect(pill, pill + ImVec2(pillWidth, s(21)), true);
        drawValuePillText(d, pill.x, pillWidth, pill.y, s(21), pillNumber, pillSuffix);
        d->PopClipRect();
    }
    ImGui::PopID();
}

bool keybindRow(const char* label, int* bindValue, int id) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    card.labelReserve = s(133.0f); // key pill
    beginRow(d, label);

    const float buttonWidth = s(120.0f);
    ImVec2 bp = rowControlPos(buttonWidth + s(13.0f));
    bp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    hit("##bind", bp, ImVec2(buttonWidth, s(23)));

    if (state.capture == State::Capture::Inactive) {
        if (ImGui::IsItemClicked()) {
            state.capture = State::Capture::WaitingRelease;
            state.captureOwner = id;
            state.captureAge = 0.0f;
        } else if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && *bindValue != Bind::kOff) {
            *bindValue = Bind::kOff; // right-click clears the bind
        }
    } else if (state.captureOwner == id) {
        // 2026-10-05: WaitingRelease used to wait for EVERY key/button to be released via
        // the event-fed scancodeDown array - one missed KEY-UP (alt-tab, focus loss, a
        // game-side stall dropping events) stuck the capture at "RELEASE ALL" forever and
        // the whole binds panel became unrebindable. Cap the phase at 2 seconds.
        if (state.capture == State::Capture::WaitingRelease) {
            state.captureAge += ImGui::GetIO().DeltaTime;
            if (!gui_sdl::anyInputHeld() || state.captureAge > 2.0f)
                state.capture = State::Capture::WaitingPress;
        } else if (state.capture == State::Capture::WaitingPress) {
            if (gui_sdl::scancodeDown[76]) { // Delete clears the bind
                *bindValue = Bind::kOff;
                state.capture = State::Capture::Inactive;
            } else if (gui_sdl::scancodeDown[41]) { // Escape cancels
                state.capture = State::Capture::Inactive;
            } else {
                // mouse buttons first (thumb priority), then the lowest held scancode
                const std::uint32_t mask = gui_sdl::liveMouseMask();
                const auto down = [mask](std::uint32_t button) { return (mask & (1u << (button - 1))) != 0; };
                int bind = Bind::kOff;
                if (down(4)) bind = Bind::kMouse4;
                else if (down(5)) bind = Bind::kMouse5;
                else if (down(3)) bind = Bind::kMouse3;
                else if (down(1)) bind = Bind::kMouse1;
                else if (down(2)) bind = Bind::kMouse2;
                else {
                    for (int scancode = Bind::kMinScancode; scancode <= Bind::kMaxScancode; ++scancode) {
                        if (gui_sdl::scancodeDown[scancode]) {
                            bind = scancode;
                            break;
                        }
                    }
                }
                if (bind != Bind::kOff) {
                    *bindValue = bind;
                    state.capture = State::Capture::Inactive;
                }
            }
        }
    }
    const float response = motion(animKey(0x9241u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();

    // The pill glows in the button accent while the bound key is physically held, so the binds
    // panel (and every Hold Key row) doubles as a live input indicator.
    const bool held = *bindValue != Bind::kOff && Bind::isDown(*bindValue) && state.capture == State::Capture::Inactive;
    d->AddRectFilled(bp, bp + ImVec2(buttonWidth, s(23)), held ? g_buttonAccent : mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(bp, bp + ImVec2(buttonWidth, s(23)), mix(kHairlineSoft, g_buttonAccent, response), s(7));
    const char* buttonText = (state.captureOwner == id && state.capture == State::Capture::WaitingPress) ? "PRESS ANY KEY"
        : (state.captureOwner == id && state.capture == State::Capture::WaitingRelease) ? "RELEASE ALL"
        : Bind::displayName(*bindValue);
    textYCentered(d, bp.x, buttonWidth, bp.y, s(23), held ? kInsetBg : C(170, 173, 184), buttonText, kTextControl, nullptr);
    return false;
}

// --- config-bound row helpers ------------------------------------------------------
//
// read config -> render primitive -> write back. Select commits arrive from the popup layer
// through a static applier (no closures, no std::function).

template <typename ConfigVar>
void toggleVar(const char* label, int id) noexcept
{
    bool value = ui_config::get<ConfigVar>();
    bool rightClicked = false;
    if (toggle(label, &value, id, &rightClicked))
        ui_config::set<ConfigVar>(typename ConfigVar::ValueType{value});

    // Bind affordance: the right-click bind system is invisible otherwise - rows with a
    // registered key show a small keyboard pill right after the label. Clicking the pill (or
    // right-clicking the row) opens the bind popover.
    bool openBindPopover = rightClicked;
    if (const auto* entry = feature_binds::entryFor<ConfigVar>(); entry && entry->key != Bind::kOff) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const float pillX = card.origin.x + s(13) + card.lastLabelWidth + s(7);
        const float pillY = card.origin.y + (card.row - 1) * kRowHeight + rowCentered(s(18.0f));
        const char* keyName = Bind::displayName(entry->key);
        const float glyphWidth = iconFont()->CalcTextSizeA(kTextCaption, FLT_MAX, 0.0f, kIconKeyboard).x;
        const float keyWidth = ImGui::GetFont()->CalcTextSizeA(s(10), FLT_MAX, 0.0f, keyName).x;
        const float pillWidth = s(6) + glyphWidth + s(4) + keyWidth + s(6);
        // never push into the row's control zone: labels that truncate down to the reserve
        // leave no room for the pill - skip it rather than overlap the checkbox chip
        const float available = (card.origin.x + card.width - s(40.0f)) - pillX;
        if (pillWidth <= available) {
            d->AddRectFilled(ImVec2(pillX, pillY), ImVec2(pillX + pillWidth, pillY + s(18)), kPillBg, s(6));
            d->AddRect(ImVec2(pillX, pillY), ImVec2(pillX + pillWidth, pillY + s(18)), kHairlineSoft, s(6));
            textY(d, pillX + s(6), pillY, s(18), (g_accent & 0x00FFFFFFu) | (140u << IM_COL32_A_SHIFT), kIconKeyboard, kTextCaption, iconFont());
            textY(d, pillX + s(6) + glyphWidth + s(4), pillY, s(18), kTextFaint, keyName, s(10), nullptr);
            ImGui::PushID(id + 400000);
            if (hit("##bindpill", ImVec2(pillX, pillY), ImVec2(pillWidth, s(18))))
                openBindPopover = true;
            ImGui::PopID();
        }
    }

    // Feature-bind integration: right-click on the row (or a left-click on the bind pill)
    // opens the bind popup. ANY toggle can be bound: if the var is not registered yet, a
    // right-click registers it on the spot (the registry persists to feature_binds.txt and the
    // binds HUD list picks it up immediately).
    if (openBindPopover) {
        auto* entry = feature_binds::entryFor<ConfigVar>();
        if (!entry) {
            feature_binds::registerToggle<ConfigVar>(label);
            entry = feature_binds::entryFor<ConfigVar>();
        }
        if (entry) {
            state.featureBindOpen = true;
            state.featureBindOpenedFrame = ImGui::GetFrameCount();
            state.featureBindIndex = static_cast<int>(entry - feature_binds::entries);
            state.featureBindAnchor = ImGui::GetIO().MousePos;
            state.popup.open = false;
            state.multiSelectOpen = false;
            state.colorPickerOpen = false;
            styleSelect.open = false;
        }
    }
}

template <typename ConfigVar>
void selectApply(int index) noexcept
{
    ui_config::set<ConfigVar>(static_cast<typename ConfigVar::ValueType>(index));
}

template <typename ConfigVar>
void selectVar(const char* label, const char* const* options, int count, int id) noexcept
{
    const auto current = ui_config::get<ConfigVar>();
    int index = static_cast<int>(current);
    select(label, &index, options, count, id, &selectApply<ConfigVar>);
}

// Script dropdown (gui.dropdown): rendered with the same selectList popup as config vars, but
// the value lives in lua::GuiItem and the deferred popup commit routes through static open-time
// indices (the applier must be a plain static function - same pattern as paintKitApply).
static int scriptDropdownOwnerScript = -1;
static int scriptDropdownOwnerItem = -1;

void scriptDropdownApply(int index) noexcept
{
    if (scriptDropdownOwnerScript < 0 || scriptDropdownOwnerScript >= lua::kMaxScripts)
        return;
    lua::Script& script = lua::scripts[scriptDropdownOwnerScript];
    if (!script.L || scriptDropdownOwnerItem < 0 || scriptDropdownOwnerItem >= script.guiItemCount)
        return;
    lua::GuiItem& item = script.guiItems[scriptDropdownOwnerItem];
    if (item.type != lua::GuiItem::Type::Dropdown || item.optionCount <= 0)
        return;
    item.intValue = index < 0 ? 0 : (index >= item.optionCount ? item.optionCount - 1 : index);
}

void scriptDropdownRow(lua::Script& script, int scriptIndex, int itemIndex, int id) noexcept
{
    lua::GuiItem& item = script.guiItems[itemIndex];
    if (item.optionCount <= 0)
        return;
    scriptDropdownOwnerScript = scriptIndex;
    scriptDropdownOwnerItem = itemIndex;
    selectList(item.label, &item.intValue, item.optionPtrs, item.optionCount, id, &scriptDropdownApply);
}

// Script color picker (gui.color): the same shared color-picker popover the config colorVar
// rows use, but the value lives in lua::GuiItem. The popover reads/writes through static
// getter/setter fn pointers, so the clicked row publishes its item pointer (stable while the
// script stays loaded - Script slots are static storage) plus a name for the stale-picker
// guard at the popover call site.
static lua::GuiItem* scriptColorTarget = nullptr;
static char scriptColorOwnerName[lua::kMaxScriptName] = {};

color::Rgba scriptColorGetter() noexcept
{
    return scriptColorTarget ? color::Rgba{scriptColorTarget->colorValue} : color::Rgba{0xFFFFFFFFu};
}

void scriptColorSetter(color::Rgba value) noexcept
{
    if (scriptColorTarget)
        scriptColorTarget->colorValue = static_cast<std::uint32_t>(value);
}

void scriptColorRow(lua::Script& script, int itemIndex, int id) noexcept
{
    lua::GuiItem& item = script.guiItems[itemIndex];
    ImDrawList* d = ImGui::GetWindowDrawList();
    card.labelReserve = s(55.0f); // compact swatch, same as colorVar
    beginRow(d, item.label);

    const float controlWidth = s(42.0f);
    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool ownPicker = state.colorPickerOpen && state.colorPickerOwner == id;
    const bool click = ownPicker ? hitModal("##color", cp, ImVec2(controlWidth, s(23)))
                                 : hit("##color", cp, ImVec2(controlWidth, s(23)));
    const bool open = state.colorPickerOpen && state.colorPickerOwner == id;
    const float response = motion(animKey(0xc013u, id), (open || ImGui::IsItemHovered()) ? 1.0f : 0.0f);
    ImGui::PopID();

    if (click) {
        state.colorPickerOpen = !open;
        state.colorPickerOwner = id;
        state.colorPickerOpenedFrame = ImGui::GetFrameCount();
        state.colorPickerAnchor = cp;
        state.colorGet = &scriptColorGetter;
        state.colorSet = &scriptColorSetter;
        scriptColorTarget = &item;
        std::snprintf(scriptColorOwnerName, sizeof(scriptColorOwnerName), "%s", script.name);
        state.multiSelectOpen = false;
        styleSelect.open = false;
    }

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));

    const std::uint32_t packed = item.colorValue;
    const ImVec2 swatch = cp + ImVec2(s(6), s(4));
    d->AddRectFilled(swatch, swatch + ImVec2(s(20), s(15)),
        C((packed >> 24) & 255, (packed >> 16) & 255, (packed >> 8) & 255, packed & 255), s(3));
    // hex readout lives in the picker popover - the row just shows the swatch
}

// Paint-kit row: same visual as select(), but the popup lists the weapon's PaintKitDatabase
// finishes (searchable, scrollable) and commits a raw kit id into the config var (uint16, 0 =
// None). The commit is deferred to a later frame via the popup layer, so both the config var
// type and the defIndex must be encoded in static state: the applier is a static template
// function, the defIndex/current value are copied into state.popup at open time.
template <typename ConfigVar>
void paintKitApply(int kitId) noexcept
{
    ui_config::set<ConfigVar>(static_cast<typename ConfigVar::ValueType>(kitId));
}

template <typename ConfigVar>
void paintKitRow(const char* label, std::uint16_t defIndex, int id) noexcept
{
    const auto current = ui_config::get<ConfigVar>();
    const int currentId = static_cast<int>(current);
    const auto* kit = currentId != 0 ? cs2::paintKitById(currentId) : nullptr;

    ImDrawList* d = ImGui::GetWindowDrawList();
    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    card.labelReserve = controlWidth + s(13.0f);
    beginRow(d, label);

    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool click = hit("##paintkit", cp, ImVec2(controlWidth, s(23)));
    const float response = motion(animKey(0x6161u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();

    if (click) {
        state.popup.open = !(state.popup.open && state.popup.owner == id);
        state.popup.owner = id;
        state.popup.options = nullptr;
        state.popup.count = 0;
        state.popup.apply = &paintKitApply<ConfigVar>;
        state.popup.anchor = cp;
        state.popup.width = controlWidth;
        state.popup.openedFrame = ImGui::GetFrameCount();
        state.popup.paintKitMode = true;
        state.popup.paintKitDefIndex = defIndex;
        state.popup.itemList = nullptr;
        state.popup.itemCount = 0;
        state.popup.stringList = nullptr; // clear other popup modes' state (stale-state shadowing)
        state.popup.stringCount = 0;
        state.popup.omitNone = false;
        state.popup.paintKitCurrentId = currentId;
        state.popup.paintKitScroll = 0.0f;
        state.popup.paintKitSearch[0] = '\0';
    }

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));
    // Clip to the pill: finish/agent names routinely exceed the 134px control.
    d->PushClipRect(cp + ImVec2(s(7), 0.0f), cp + ImVec2(controlWidth - s(14), s(23)), true);
    textY(d, cp.x + s(7), cp.y, s(23), currentId == 0 ? C(120, 124, 134) : C(200, 203, 212), kit ? kit->name : currentId == 0 ? "None" : "?", kTextControl, nullptr);
    d->PopClipRect();
    d->AddLine(cp + ImVec2(controlWidth - s(13), s(9)), cp + ImVec2(controlWidth - s(9), s(13)), C(139, 143, 154), 1.0f);
    d->AddLine(cp + ImVec2(controlWidth - s(9), s(13)), cp + ImVec2(controlWidth - s(5), s(9)), C(139, 143, 154), 1.0f);
}

// Case/key picker row (Inventory page LOCAL ITEMS): same pill as paintKitRow, but the popup
// lists an arbitrary ItemDefEntry table (cases / keys) and apply receives the raw def index.
template <typename ConfigVar>
void itemDefRow(const char* label, const cs2::ItemDefEntry* list, int count, int id) noexcept
{
    const auto current = ui_config::get<ConfigVar>();
    const int currentId = static_cast<int>(current);
    const auto* entry = currentId != 0 ? cs2::itemDefById(list, count, static_cast<std::uint16_t>(currentId)) : nullptr;

    ImDrawList* d = ImGui::GetWindowDrawList();
    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    card.labelReserve = controlWidth + s(13.0f);
    beginRow(d, label);

    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool click = hit("##itemdef", cp, ImVec2(controlWidth, s(23)));
    const float response = motion(animKey(0x6161u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();

    if (click) {
        state.popup.open = !(state.popup.open && state.popup.owner == id);
        state.popup.owner = id;
        state.popup.options = nullptr;
        state.popup.count = 0;
        state.popup.apply = &paintKitApply<ConfigVar>;
        state.popup.anchor = cp;
        state.popup.width = controlWidth;
        state.popup.openedFrame = ImGui::GetFrameCount();
        state.popup.paintKitMode = true;
        state.popup.paintKitDefIndex = 0;
        state.popup.itemList = list;
        state.popup.itemCount = count;
        state.popup.stringList = nullptr; // clear other popup modes' state (stale-state shadowing)
        state.popup.stringCount = 0;
        state.popup.omitNone = false;
        state.popup.paintKitCurrentId = currentId;
        state.popup.paintKitScroll = 0.0f;
        state.popup.paintKitSearch[0] = '\0';
    }

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));
    d->PushClipRect(cp + ImVec2(s(7), 0.0f), cp + ImVec2(controlWidth - s(14), s(23)), true);
    textY(d, cp.x + s(7), cp.y, s(23), currentId == 0 ? C(120, 124, 134) : C(200, 203, 212), entry ? entry->name : currentId == 0 ? "None" : "?", kTextControl, nullptr);
    d->PopClipRect();
    d->AddLine(cp + ImVec2(controlWidth - s(13), s(9)), cp + ImVec2(controlWidth - s(9), s(13)), C(139, 143, 154), 1.0f);
    d->AddLine(cp + ImVec2(controlWidth - s(9), s(13)), cp + ImVec2(controlWidth - s(5), s(9)), C(139, 143, 154), 1.0f);
}

// Wear row: the config var stores permille (0-1000 = 0.000-1.000 wear); the pill shows the
// permille value (70 = 0.070).
template <typename ConfigVar>
void wearRow(const char* label, int id) noexcept
{
    const auto current = ui_config::get<ConfigVar>();
    int permille = static_cast<int>(current);
    if (sliderRow(label, &permille, 0, 1000, id, nullptr))
        ui_config::set<ConfigVar>(static_cast<typename ConfigVar::ValueType>(permille));
}

// Pattern seed row (0-1000, the engine's seed domain).
template <typename ConfigVar>
void seedRow(const char* label, int id) noexcept
{
    const auto current = ui_config::get<ConfigVar>();
    int seed = static_cast<int>(current);
    if (sliderRow(label, &seed, 0, 1000, id, nullptr))
        ui_config::set<ConfigVar>(static_cast<typename ConfigVar::ValueType>(seed));
}

// 'Enemies / All Players / Off': two bool vars (PlayerInfoInWorldDropdownSelectionChangeHandler
// semantics; 'Off' leaves OnlyEnemies untouched).
template <typename EnabledVar, typename OnlyEnemiesVar>
void enemiesAllOffApply(int index) noexcept
{
    switch (index) {
    case 0:
        ui_config::set<EnabledVar>(typename EnabledVar::ValueType{true});
        ui_config::set<OnlyEnemiesVar>(typename OnlyEnemiesVar::ValueType{true});
        break;
    case 1:
        ui_config::set<EnabledVar>(typename EnabledVar::ValueType{true});
        ui_config::set<OnlyEnemiesVar>(typename OnlyEnemiesVar::ValueType{false});
        break;
    default:
        ui_config::set<EnabledVar>(typename EnabledVar::ValueType{false});
        break;
    }
}

template <typename EnabledVar, typename OnlyEnemiesVar>
void enemiesAllOffVar(const char* label, int id) noexcept
{
    const bool enabled = ui_config::get<EnabledVar>();
    const bool onlyEnemies = ui_config::get<OnlyEnemiesVar>();
    int index = enabled ? (onlyEnemies ? 0 : 1) : 2;
    static const char* const options[] = {"Enemies", "All Players", "Off"};
    select(label, &index, options, 3, id, &enemiesAllOffApply<EnabledVar, OnlyEnemiesVar>);
}

template <typename ConfigVar>
void sliderVar(const char* label, int id, const char* suffix = nullptr) noexcept
{
    const auto current = ui_config::get<ConfigVar>();
    int value = static_cast<int>(static_cast<typename ConfigVar::ValueType::ValueType>(current));
    if (sliderRow(label, &value, static_cast<int>(ConfigVar::ValueType::kMin), static_cast<int>(ConfigVar::ValueType::kMax), id, suffix))
        ui_config::set<ConfigVar>(typename ConfigVar::ValueType{static_cast<typename ConfigVar::ValueType::ValueType>(value)});
}

template <typename ConfigVar>
void hueVar(const char* label, int id) noexcept
{
    const auto current = ui_config::get<ConfigVar>();
    float hue = static_cast<float>(static_cast<typename ConfigVar::ValueType::ValueType>(current));
    if (hueRow(label, &hue, id)) {
        auto clamped = static_cast<typename ConfigVar::ValueType::ValueType>(hue);
        if (clamped < ConfigVar::ValueType::kMin)
            clamped = ConfigVar::ValueType::kMin;
        if (clamped > ConfigVar::ValueType::kMax)
            clamped = ConfigVar::ValueType::kMax;
        ui_config::set<ConfigVar>(typename ConfigVar::ValueType{clamped});
    }
}

template <typename ConfigVar>
void keybindVar(const char* label, int id) noexcept
{
    const auto current = ui_config::get<ConfigVar>();
    int bind = static_cast<int>(static_cast<typename ConfigVar::ValueType::ValueType>(current));
    keybindRow(label, &bind, id);
    if (state.capture == State::Capture::Inactive
        && bind != static_cast<int>(static_cast<typename ConfigVar::ValueType::ValueType>(current)))
        ui_config::set<ConfigVar>(typename ConfigVar::ValueType{static_cast<typename ConfigVar::ValueType::ValueType>(bind)});
}

// --- select popup layer ------------------------------------------------------------

// --- paint-kit picker (Inventory page) ----------------------------------------------
//
// Row control + popup for picking a paint kit from the weapon's PaintKitDatabase list. Unlike
// the generic select() dropdown the list is searchable and wheel-scrollable: the big weapons
// have 60+ finishes, so a plain dropdown would be twice the menu's height.

// ASCII case-insensitive substring match - this TU keeps to a minimal libc surface, and the
// finish names are ASCII (the localized weapon names may not be, but they aren't searched).
bool containsCaseInsensitive(const char* haystack, const char* needle) noexcept
{
    if (!*needle)
        return true;
    for (const char* h = haystack; *h; ++h) {
        int i = 0;
        while (needle[i]) {
            char a = h[i];
            const char b = needle[i];
            if (!a)
                return false;
            if (a >= 'A' && a <= 'Z')
                a = static_cast<char>(a - 'A' + 'a');
            const char lb = b >= 'A' && b <= 'Z' ? static_cast<char>(b - 'A' + 'a') : b;
            if (a != lb)
                break;
            ++i;
        }
        if (!needle[i])
            return true;
    }
    return false;
}

void paintKitPopupLayer(ImDrawList* d) noexcept
{
    const float open = motion(ImGui::GetID("##popup_open"), state.popup.open ? 1.0f : 0.0f, 20.0f, 0.0f);
    if (open < 0.002f || !state.popup.apply)
        return;

    const auto* list = state.popup.paintKitDefIndex != 0 ? cs2::paintKitListFor(state.popup.paintKitDefIndex) : nullptr;
    if (state.popup.paintKitDefIndex != 0 && !state.popup.itemList && !list && !state.popup.stringList) {
        state.popup.open = false;
        return;
    }

    // Virtual row 0 = "None", then the source list - an ItemDefEntry list (cases / keys,
    // item-def picker mode) when itemList is set, otherwise the weapon's kits (or, in generic
    // mode - paintKitDefIndex 0 with no itemList, used when no knife model is impersonated and
    // the real equipped knife type is unknown - the SkinChangerData generic-knife set),
    // filtered by the search text. The per-row ids and the filtered rows are plain
    // zero-initialized statics (no dynamic init - this project links -nostdlib and
    // function-local statics with guards fail the link).
    constexpr int kMaxRows = 160; // the agent list is the largest (141 entries)
    static int rows[kMaxRows];
    int rowCount = 0;
    if (!state.popup.omitNone)
        rows[rowCount++] = 0; // "None"
    if (state.popup.itemList) {
        for (int i = 0; i < state.popup.itemCount && rowCount < kMaxRows; ++i) {
            const auto& entry = state.popup.itemList[i];
            if (state.popup.paintKitSearch[0] == '\0'
                || containsCaseInsensitive(entry.name, state.popup.paintKitSearch))
                rows[rowCount++] = entry.defIndex;
        }
    } else if (list) {
        for (int i = 0; i < list->kitCount && rowCount < kMaxRows; ++i) {
            const int kitId = cs2::kPaintKits[list->kitIndices[i]].id;
            if (state.popup.paintKitSearch[0] == '\0'
                || containsCaseInsensitive(cs2::kPaintKits[list->kitIndices[i]].name, state.popup.paintKitSearch))
                rows[rowCount++] = kitId;
        }
    } else if (state.popup.stringList) {
        for (int i = 0; i < state.popup.stringCount && rowCount < kMaxRows; ++i) {
            if (state.popup.paintKitSearch[0] == '\0'
                || containsCaseInsensitive(state.popup.stringList[i], state.popup.paintKitSearch))
                rows[rowCount++] = i; // the row id IS the option index - apply receives it
        }
    } else {
        for (const auto& kit : cs2::kPaintKits) {
            if (rowCount >= kMaxRows)
                break;
            if (!SkinChangerData::isGenericKnifePaintKit(kit.id))
                continue;
            if (state.popup.paintKitSearch[0] == '\0' || containsCaseInsensitive(kit.name, state.popup.paintKitSearch))
                rows[rowCount++] = kit.id;
        }
    }

    constexpr float kRowHeight = 32.0f;
    constexpr int kMaxVisibleRows = 11;
    const float width = s(196.0f);
    const float searchHeight = s(30.0f);
    const float listHeight = ImMin(rowCount, kMaxVisibleRows) * s(kRowHeight);
    const ImVec2 size(width, searchHeight + s(12.0f) + listHeight + s(8.0f));
    // Drop DOWN from the control (a list this tall centered on the anchor would cover it).
    ImVec2 p(state.popup.anchor.x - s(28.0f), state.popup.anchor.y + s(27.0f));
    const ImVec2 clipMin = d->GetClipRectMin();
    const ImVec2 clipMax = d->GetClipRectMax();
    p.x = ImClamp(p.x, clipMin.x + s(10.0f), clipMax.x - size.x - s(10.0f));
    p.y = ImClamp(p.y, clipMin.y + s(10.0f), ImMax(clipMin.y + s(10.0f), clipMax.y - size.y - s(10.0f)));
    recordPopupRect(PopupDropdown, p, p + size);
    const int first = d->VtxBuffer.Size;
    softShadow(d, p, p + size, s(16.0f));
    d->AddRectFilled(p - ImVec2(s(5), s(2)), p + size + ImVec2(s(5), s(8)), C(0, 0, 0, 55), s(18));
    d->AddRectFilled(p, p + size, kPopupBg, s(18));
    d->AddRect(p, p + size, kPopupBorder, s(18));
    d->AddLine(p + ImVec2(s(16), s(1)), p + ImVec2(size.x - s(16), s(1)), C(255, 255, 255, 22));

    // Search field (raw-mouse popover interior - see the color picker's InputText pattern).
    {
        const ImVec2 sp(p.x + s(6), p.y + s(8));
        ImGui::PushID(9071);
        ImGui::SetCursorScreenPos(sp);
        if (ImGui::GetFrameCount() == state.popup.openedFrame)
            ImGui::SetKeyboardFocusHere(0);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kPillBg);
        ImGui::PushStyleColor(ImGuiCol_Border, C(54, 54, 60));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(6), s(3)));
        ImGui::PushItemWidth(size.x - s(12));
        ImGui::InputTextWithHint("##kit_search", "search...", state.popup.paintKitSearch, sizeof(state.popup.paintKitSearch));
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

    const float listTop = p.y + searchHeight + s(12.0f);
    const float fullListHeight = rowCount * s(kRowHeight);
    const float maxScroll = ImMax(0.0f, fullListHeight - listHeight);
    // Wheel-scroll while the cursor is inside the list area - one ROW per notch (the old 48px
    // = 1.5-row step made the list jump erratically and mis-aims feel like wrong selections).
    if (ImGui::IsMouseHoveringRect(ImVec2(p.x, listTop), ImVec2(p.x + size.x, listTop + listHeight)) && ImGui::GetIO().MouseWheel != 0.0f)
        state.popup.paintKitScroll = ImClamp(state.popup.paintKitScroll - ImGui::GetIO().MouseWheel * s(kRowHeight), 0.0f, maxScroll);
    else
        state.popup.paintKitScroll = ImClamp(state.popup.paintKitScroll, 0.0f, maxScroll);

    // Popup scrollbar: thin thumb on the right edge of the list, visible while scrollable,
    // draggable to scroll long lists (141 agents) without the wheel.
    {
        const float maxListScroll = ImMax(0.0f, rowCount * s(kRowHeight) - listHeight);
        if (maxListScroll > 1.0f) {
            const float track = listHeight - s(6.0f);
            const float th = ImClamp(listHeight * listHeight / (listHeight + maxListScroll), s(18.0f), track);
            const float tt = maxListScroll > 0.0f ? state.popup.paintKitScroll / maxListScroll : 0.0f;
            float thumbY = listTop + s(3.0f) + tt * (track - th);
            const ImVec2 tMin(p.x + size.x - s(7.0f), listTop);
            const ImVec2 tMax(p.x + size.x, listTop + listHeight);
            static bool draggingPopupScroll = false;
            static float grabPopup = 0.0f;
            const bool hoveredPopup = ImGui::IsMouseHoveringRect(ImVec2(tMin.x, thumbY), ImVec2(tMax.x, thumbY + th));
            if ((hoveredPopup || ImGui::IsMouseHoveringRect(tMin, tMax)) && ImGui::IsMouseClicked(0)) {
                draggingPopupScroll = true;
                grabPopup = ImClamp(ImGui::GetIO().MousePos.y, thumbY, thumbY + th) - thumbY;
            }
            if (!ImGui::IsMouseDown(0))
                draggingPopupScroll = false;
            if (draggingPopupScroll)
                state.popup.paintKitScroll = ImClamp((ImGui::GetIO().MousePos.y - grabPopup - (listTop + s(3.0f))) / (track - th), 0.0f, 1.0f) * maxListScroll;
            const float tNow = maxListScroll > 0.0f ? state.popup.paintKitScroll / maxListScroll : 0.0f;
            const float yNow = listTop + s(3.0f) + tNow * (track - th);
            d->AddRectFilled(ImVec2(p.x + size.x - s(5.0f), yNow), ImVec2(p.x + size.x - s(1.0f), yNow + th),
                C(200, 203, 212, draggingPopupScroll ? 150 : 95), s(2));
        }
    }
    d->PushClipRect(ImVec2(p.x + s(2), listTop), ImVec2(p.x + size.x - s(2), listTop + listHeight), true);
    const bool accepts = ImGui::GetFrameCount() > state.popup.openedFrame;
    const int currentKit = state.popup.paintKitCurrentId;
    for (int j = 0; j < rowCount; ++j) {
        const int kitId = rows[j];
        const float rowY = listTop + j * s(kRowHeight) - state.popup.paintKitScroll;
        if (rowY + s(kRowHeight) < listTop || rowY > listTop + listHeight)
            continue;
        const char* label = nullptr;
        if (state.popup.stringList)
            label = state.popup.stringList[kitId];
        else if (kitId == 0)
            label = "None";
        else if (state.popup.itemList) {
            const auto* entry = cs2::itemDefById(state.popup.itemList, state.popup.itemCount, static_cast<std::uint16_t>(kitId));
            label = entry ? entry->name : "?";
        } else
            label = cs2::paintKitById(kitId)->name;
        const ImVec2 rp(p.x + s(4), rowY);
        // RAW-MOUSE hit testing (the color picker's lesson): popup rows never submit
        // ImGui items, so nothing can claim/steal their hover or click regardless of what
        // else is submitted this frame. One behavior for every dropdown in the menu.
        const ImVec2 rowSize(size.x - s(8), s(kRowHeight));
        const bool hovered = ImGui::IsMouseHoveringRect(rp, rp + rowSize);
        const float hover = motion(animKey(0x9921u, 9200 + kitId), hovered ? 1.0f : 0.0f, 22.0f);
        if (hover > 0.001f)
            d->AddRectFilled(rp, rp + rowSize, (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(25 * hover) << IM_COL32_A_SHIFT), s(10));
        const bool selected = kitId == currentKit;
        // Clip to the row: agent names especially ("'Medium Rare' Crasswater | Guerrilla
        // Warfare") are far wider than the popup.
        d->PushClipRect(ImVec2(rp.x + s(12), rp.y - 2.0f), ImVec2(p.x + size.x - s(6), rp.y + s(kRowHeight) + 2.0f), true);
        textY(d, rp.x + s(12), rp.y, s(kRowHeight), selected ? g_accent : C(182, 185, 196), label, kTextControl, nullptr);
        d->PopClipRect();
        if (accepts && hovered && ImGui::IsMouseClicked(0)) {
            state.popup.apply(kitId);
            state.popup.open = false;
        }
    }
    d->PopClipRect();

    if (accepts && clickedOutside(p, p + size))
        state.popup.open = false;
    const float eased = g_reduceMotion ? 1.0f : 1.0f - std::pow(1.0f - open, 3.0f);
    const ImVec2 pivot(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    for (int i = first; i < d->VtxBuffer.Size; ++i) {
        ImDrawVert& v = d->VtxBuffer[i];
        v.pos = pivot + (v.pos - pivot) * ImLerp(0.96f, 1.0f, eased) + ImVec2(s(4.0f) * (1.0f - eased), 0);
        const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
        v.col = (v.col & 0x00ffffffu) | ((ImU32)(a * eased) << IM_COL32_A_SHIFT);
    }
}

void popupLayer(ImDrawList* d) noexcept
{
    // ONE dropdown widget: every option popup renders through the unified searchable,
    // wheel-scrollable layer (the old fixed 10-row dropdown branch is gone).
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        state.popup.open = false;
    if (state.popup.paintKitMode)
        paintKitPopupLayer(d);
}

// --- option tables (static - the popup layer outlives the opening frame) -----------

constexpr const char* const kPositionArrowColors[] = {"Player / Team Color", "Team Color"};
constexpr const char* const kHealthTextColors[] = {"Health-based", "White"};
// --- pages -----------------------------------------------------------------------
//
// Every page = section cards flowing into two balanced columns (scrollable). addCard assigns
// columns from the min-max plan computed at resetContent (see cardColumnPlan), draws the card
// frame and runs the row callbacks against the card context.

float columnYs[2] = {};
int controlId = 0;

// Height-aware column balancing: LAST frame's measured card height (+ gap) per (page, card
// index), and the column assignment for this frame computed once per page at resetContent by
// an exact min-max two-column partition (subset-sum DP) over those heights. addCard just reads
// the plan; cards beyond the planned count (first frame on a page / card count changed) fall
// back to the greedy shortest-column pick. 1-frame-stale heights are invisible - layout is
// stable frame-to-frame (same pattern as lastContentHeight / navContentEnd). Never updated
// during the search-index ghost pass, so real and ghost layouts can't fight over the cache.
constexpr int kCardSlotsPerPage = 48;
constexpr int kMaxCardPages = 13;
float cardHeights[kMaxCardPages * kCardSlotsPerPage] = {};
int cardCountLast[kMaxCardPages] = {};
int cardColumnPlan[kMaxCardPages * kCardSlotsPerPage] = {};

// Exact partition: find the subset of card heights with the largest sum <= total/2 (subset-sum
// DP with per-sum last-card tracking for reconstruction); that subset takes column 1, the rest
// column 0. Bails to the greedy fallback (all plans -1) when the cache has a hole or the DP
// range would overflow.
void planPageColumns(int pageIdx) noexcept
{
    for (int i = 0; i < kCardSlotsPerPage; ++i)
        cardColumnPlan[pageIdx * kCardSlotsPerPage + i] = -1;
    const int count = ImMin(cardCountLast[pageIdx], kCardSlotsPerPage);
    if (count <= 1)
        return;
    static int sums[kCardSlotsPerPage];
    long total = 0;
    for (int i = 0; i < count; ++i) {
        sums[i] = static_cast<int>(cardHeights[pageIdx * kCardSlotsPerPage + i] + 0.5f);
        if (sums[i] <= 0)
            return; // no measurement for this card yet
        total += sums[i];
    }
    if (total >= 32768)
        return;
    static bool reach[32768];
    static short chosen[32768];
    for (long s = 0; s <= total; ++s) {
        reach[s] = false;
        chosen[s] = -1;
    }
    reach[0] = true;
    for (int i = 0; i < count; ++i) {
        for (long s = total; s >= sums[i]; --s) {
            if (!reach[s] && reach[s - sums[i]]) {
                reach[s] = true;
                chosen[s] = static_cast<short>(i);
            }
        }
    }
    long best = 0;
    for (long s = total / 2; s > 0; --s) {
        if (reach[s]) {
            best = s;
            break;
        }
    }
    static bool inSecond[kCardSlotsPerPage];
    for (int i = 0; i < count; ++i)
        inSecond[i] = false;
    for (long s = best; s > 0;) {
        const int i = chosen[s];
        inSecond[i] = true;
        s -= sums[i];
    }
    for (int i = 0; i < count; ++i)
        cardColumnPlan[pageIdx * kCardSlotsPerPage + i] = inSecond[i] ? 1 : 0;
}

// Page-switch stagger: resetContent() detects the page change and arms the clock; each card
// fades/rises in with a small incremental delay. Suppressed by Reduce Motion.
double pageSwitchTime = 0.0;
Page lastRenderedPage = Page::Rage;
int pageCardIndex = 0;

void resetContent() noexcept
{
    columnYs[0] = columnYs[1] = 0.0f;
    controlId = static_cast<int>(state.page) * 1000;
    if (!searchIndexing)
        planPageColumns(static_cast<int>(state.page));
    if (state.page != lastRenderedPage) {
        pageSwitchTime = ImGui::GetTime();
        lastRenderedPage = state.page;
    }
    pageCardIndex = 0;
}

void addCard(const char* title, int rowCount, void (*renderRows)()) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    // The ghost pass renders a DIFFERENT page than state.page - key the height cache by the
    // page actually being laid out (same identity beginRow records into the search index).
    const int pageIdx = searchIndexing ? searchIndexPageCursor : static_cast<int>(state.page);
    const int cardSlot = pageIdx * kCardSlotsPerPage + ImMin(pageCardIndex, kCardSlotsPerPage - 1);
    // Column choice: the min-max plan from LAST frame's measured heights; cards beyond the
    // plan (first frame / count changed) use the old greedy shortest-column pick.
    const int planned = cardColumnPlan[cardSlot];
    const int column = planned >= 0 ? planned : (columnYs[1] < columnYs[0] ? 1 : 0);
    const float columnWidth = s(281.0f);
    // Draw-list calls take screen-absolute positions: the demo could use canvas-local ones
    // only because its window sat at (0,0); ours is centered.
    const float x = shellBase.x + kSidebarWidth + s(9.0f) + column * (columnWidth + s(10.0f));
    const float y = shellBase.y + kToolbarHeight + s(24.0f) + columnYs[column] - scrollOffset;

    // Per-card entrance on page switches: 40ms stagger per card, fade + rise.
    const float staggerDelay = 0.04f * pageCardIndex++;
    const float staggerT = g_reduceMotion ? 1.0f : ImSaturate(static_cast<float>((ImGui::GetTime() - pageSwitchTime - staggerDelay) / 0.30));
    const float stagger = 1.0f - std::pow(1.0f - staggerT, 3.0f);
    const float rise = (1.0f - stagger) * s(10.0f);

    constexpr float kHeaderHeight = 32.0f; // design units: title band inside the card

    // Self-healing card sizing: the rows render FIRST (channel 1), the frame afterwards
    // (channel 0) using the height the rows ACTUALLY took. The channels keep the frame painted
    // underneath the row content regardless of submission order. The declared rowCount becomes
    // a floor - a stale hand-count can only make a card too TALL, never clip rows.
    const int vtxBegin = d->VtxBuffer.Size;
    d->ChannelsSplit(2);
    d->ChannelsSetCurrent(1);
    card = CardContext{ImVec2(x, y + s(6.0f + kHeaderHeight) + rise), columnWidth, 0.0f};
    renderRows();
    const float height = ImMax(static_cast<float>(rowCount) * kRowHeight, card.row * kRowHeight) + s(12.0f + kHeaderHeight);

    d->ChannelsSetCurrent(0);
    const ImVec2 p(x, y + rise);
    softShadow(d, p, p + ImVec2(columnWidth, height), s(16.0f));
    d->AddRectFilled(p, p + ImVec2(columnWidth, height), kCardBg, s(16.0f));
    // top-light: a soft white wash fading down the header area + a 1px inner highlight along
    // the top edge (inset past the rounded corners) - reads as "machined" instead of flat. The
    // highlight wraps around the rounded corners: the same rounded path stroked inside corner-
    // local clips, tangent-joined to the straight line, so the light follows the rounding.
    d->AddRectFilledMultiColor(p + ImVec2(s(14), s(1)), p + ImVec2(columnWidth - s(14), s(30)), C(255, 255, 255, 9), C(255, 255, 255, 9), 0, 0);
    d->AddLine(p + ImVec2(s(12), s(0.75f)), p + ImVec2(columnWidth - s(12), s(0.75f)), C(255, 255, 255, 16), 1.0f);
    for (int corner = 0; corner < 2; ++corner) {
        constexpr float arcSpan = 15.25f; // inset-path radius: where its arc meets the straight edge
        const float x0 = corner == 0 ? 0.0f : columnWidth - s(arcSpan);
        d->PushClipRect(p + ImVec2(x0, 0.0f), p + ImVec2(x0 + s(arcSpan), s(17.0f)), true);
        d->AddRect(p + ImVec2(0.75f, 0.75f), p + ImVec2(columnWidth - 0.75f, height - 0.75f), C(255, 255, 255, 16), s(16.0f) - 0.75f, ImDrawFlags_RoundCornersTop);
        d->PopClipRect();
    }

    // header band: accent dot + semibold title + hairline divider
    pulsingDot(d, p + ImVec2(s(17), s(15)), s(2.2f), g_accent);
    text(d, p + ImVec2(s(25), s(9)), C(196, 199, 208), title, kTextSmall, strongFont());
    d->AddLine(p + ImVec2(s(12), s(31)), p + ImVec2(columnWidth - s(12), s(31)), kHairline, 1.0f);
    d->AddRect(p, p + ImVec2(columnWidth, height), kHairlineSoft, s(16.0f));
    d->ChannelsMerge();
    const int vtxEnd = d->VtxBuffer.Size;
    if (stagger < 0.999f) {
        for (int i = vtxBegin; i < vtxEnd; ++i) {
            ImDrawVert& v = d->VtxBuffer[i];
            const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
            v.col = (v.col & 0x00ffffffu) | ((ImU32)(a * stagger) << IM_COL32_A_SHIFT);
        }
    }

    columnYs[column] += height + s(20.0f); // card + gap (the title band lives inside now)
    if (!searchIndexing) {
        cardHeights[cardSlot] = height + s(20.0f); // next frame's plan input
        cardCountLast[pageIdx] = pageCardIndex;    // pageCardIndex now = cards so far this frame
    }
}

// --- page content (bindings 1:1 with the Panorama inventory) ------------------------

// Sub-page pill tabs: the segmented control at the top of a page that splits its cards into
// groups (the Glow page's two-tab control and the inventory's category pills, generalized).
// Equal-width pills across the two-column card area; switching closes any open popup (rows
// belong to a group). The caller tags search rows by setting searchSubTab before rendering a
// group, so a search hit jumps to the right group (the ghost pass renders every group).
void pageSubTabPills(ImDrawList* d, int& current, const char* const* labels, int count, int idBase) noexcept
{
    const float gap = s(8.0f);
    const float contentWidth = 2 * s(281.0f) + s(10.0f); // the two-column card area
    const float pillWidth = (contentWidth - (count - 1) * gap) / count;
    const float pillHeight = s(30.0f);
    const float x = shellBase.x + kSidebarWidth + s(9.0f);
    const float y = shellBase.y + kToolbarHeight + s(24.0f) + s(4.0f) + columnYs[0] - scrollOffset;

    for (int i = 0; i < count; ++i) {
        const ImVec2 p(x + i * (pillWidth + gap), y);
        ImGui::PushID(idBase + i);
        const bool clicked = hit("##page_subtab", p, ImVec2(pillWidth, pillHeight));
        const bool active = current == i;
        const float r = motion(animKey(0x671cu, idBase + i), active ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
        ImGui::PopID();
        if (clicked && !active) {
            current = i;
            // popovers belong to the other group's rows - close them
            state.popup.open = false;
            state.colorPickerOpen = false;
            state.multiSelectOpen = false;
            styleSelect.open = false;
        }
        d->AddRectFilled(p, p + ImVec2(pillWidth, pillHeight), mix(C(0, 0, 0, 0), kRowHover, r), pillHeight * 0.5f);
        d->AddRect(p, p + ImVec2(pillWidth, pillHeight), mix(kHairlineSoft, g_accent, r), pillHeight * 0.5f);
        textYCentered(d, p.x, pillWidth, p.y, pillHeight, mix(C(145, 149, 159), C(226, 228, 235), r), labels[i], kTextControl, nullptr);
    }
    // +s(30) after the pills: card section captions are drawn s(16) above their card, so without
    // extra clearance the caption text starts exactly at the pill's bottom edge
    columnYs[0] = columnYs[1] = s(4.0f) + pillHeight + s(30.0f);
}

void pageRageAimbot() noexcept
{
    using namespace aimbot_vars;
    addCard("AIMBOT", 7, [] {
        toggleVar<Enabled>("Silent Aim", ++controlId);
        toggleVar<BodyAim>("Force Body Aim", ++controlId);
        toggleVar<Multipoint>("Multipoint", ++controlId);
        sliderVar<PointScale>("Point Scale ", ++controlId, "%");
        toggleVar<DynamicPointscale>("Dynamic Point Scale", ++controlId);
        toggleVar<Backtrack>("Backtrack", ++controlId);
        toggleVar<AutoStop>("Auto Stop", ++controlId);
    });
    addCard("TIMING", 3, [] {
        sliderVar<BacktrackTicks>("Backtrack Ticks", ++controlId);
        sliderVar<ExtrapolateTicks>("Lead Ticks", ++controlId);
        sliderVar<ForceShotWaitTicks>("Auto Shoot Wait", ++controlId);
    });
    addCard("TARGETS", 2, [] {
        toggleVar<SpreadCircleFov>("Spread FOV", ++controlId);
        // The names array MUST have static lifetime: multiSelectVar stores the pointer for the
        // popover to read on LATER frames, and a braced temporary bound to the reference parameter
        // dies at the end of this call - the dangling array was read as garbage string pointers in
        // the popover (strlen(NULL) -> SIGSEGV in libc on first click of the control).
        static constexpr const char* const kHitboxNames[]{ "Head", "Chest", "Stomach", "Arms", "Legs" };
        multiSelectVar<HitHead, HitChest, HitStomach, HitArms, HitLegs>("Hitboxes", ++controlId, kHitboxNames);
    });
}

void pageRageAccuracy() noexcept
{
    using namespace aimbot_vars;
    addCard("ACCURACY", 4, [] {
        toggleVar<SpreadCompensation>("Compensate Spread", ++controlId);
        toggleVar<SpreadGate>("Hold Fire Until Exact", ++controlId);
        toggleVar<SeedFallback>("Fire Lucky Seeds", ++controlId);
        toggleVar<RecoilCompensation>("Compensate Recoil", ++controlId);
    });
    addCard("AUTO SHOOT", 7, [] {
        toggleVar<ForceShot>("Auto Shoot Ground", ++controlId);
        toggleVar<ForceShotAir>("Auto Shoot Air", ++controlId);
        toggleVar<ForceShotWait>("Wait For Accuracy", ++controlId);
        sliderVar<Hitchance>("Min Hitchance", ++controlId, "%");
        sliderVar<MinDamage>("Min Damage", ++controlId);
        toggleVar<WallCheck>("Shoot Visible", ++controlId);
        toggleVar<Autowall>("Shoot Walls", ++controlId);
    });
    addCard("EXTRAS", 2, [] {
        toggleVar<Extrapolate>("Lead Targets", ++controlId);
        toggleVar<autopeek_vars::Enabled>("Auto Peek", ++controlId);
    });
}

void pageRage() noexcept
{
    if (!searchIndexing) {
        pageSubTabPills(ImGui::GetWindowDrawList(), rageSubTab, kRageSubTabs, 2, 9720);
        searchGlowSubTab = rageSubTab;
        controlId += rageSubTab * 500; // per-sub-tab control-id ranges (popups/motion storage)
        if (rageSubTab == 0)
            pageRageAimbot();
        else
            pageRageAccuracy();
        return;
    }

    // ghost render for the search index: lay out BOTH groups, each exactly as it appears when
    // active (so a search hit scrolls to the right offset after opening its group)
    for (int t = 0; t < 2; ++t) {
        pageSubTabPills(ImGui::GetWindowDrawList(), rageSubTab, kRageSubTabs, 2, 9720);
        searchGlowSubTab = t;
        controlId += t * 500;
        if (t == 0)
            pageRageAimbot();
        else
            pageRageAccuracy();
        columnYs[0] = columnYs[1] = 0;
    }
}

void pageLegitAim() noexcept
{
    using namespace legit_aimbot_vars;
    addCard("AIM ASSIST", 7, [] {
        toggleVar<Enabled>("Smooth Aim", ++controlId);
        keybindVar<AimKey>("Hold Key", ++controlId);
        sliderVar<Fov>("Field Of View", ++controlId, " deg");
        sliderVar<Smooth>("Smoothing", ++controlId);
        toggleVar<DrawFov>("Draw FOV Circle", ++controlId);
        colorVar<FovCircleColor>("FOV Circle Color", ++controlId);
        toggleVar<SpreadCircleFov>("Spread Circle FOV", ++controlId);
    });
    addCard("HITBOXES", 5, [] {
        toggleVar<HitHead>("Target Head", ++controlId);
        toggleVar<HitChest>("Target Chest", ++controlId);
        toggleVar<HitStomach>("Target Stomach", ++controlId);
        toggleVar<HitArms>("Target Arms", ++controlId);
        toggleVar<HitLegs>("Target Legs", ++controlId);
    });
    addCard("RECOIL & SCOPES", 5, [] {
        toggleVar<rcs_vars::Enabled>("Control Recoil", ++controlId);
        sliderVar<rcs_vars::Strength>("Strength", ++controlId, "%");
        toggleVar<no_scope_inaccuracy_vis_vars::Enabled>("No-scope Inaccuracy Vis", ++controlId);
        toggleVar<spread_circle_vars::Enabled>("Draw Weapon Spread", ++controlId);
        spreadCircleColorVar("Circle Color", ++controlId);
    });
}

void pageLegitTriggerbot() noexcept
{
    addCard("TRIGGERBOT", 10, [] {
        toggleVar<triggerbot_vars::Enabled>("Triggerbot", ++controlId);
        keybindVar<triggerbot_vars::HoldKey>("Hold Key", ++controlId);
        sliderVar<triggerbot_vars::DelayMilliseconds>("Min Reaction Delay", ++controlId, " ms");
        sliderVar<triggerbot_vars::DelayMillisecondsMax>("Max Reaction Delay", ++controlId, " ms");
        cardDivider("ACCURACY");
        toggleVar<triggerbot_vars::AccuracyCheck>("Shoot When Accurate", ++controlId);
        sliderVar<triggerbot_vars::AccuracyRadius>("Max Bullet Deviation", ++controlId, " u");
        toggleVar<triggerbot_vars::HeadOnly>("Shoot At The Head", ++controlId);
        sliderVar<triggerbot_vars::Hitchance>("Minimum Hitchance", ++controlId, "%");
        toggleVar<triggerbot_vars::MaxAccuracyOnly>("Shoot At Max Accuracy", ++controlId);
    });
    addCard("TRIGGERBOT VISIBILITY", 4, [] {
        toggleVar<triggerbot_vars::WallCheck>("Shoot Visible", ++controlId);
        toggleVar<triggerbot_vars::Autowall>("Shoot Walls", ++controlId);
        sliderVar<triggerbot_vars::AutowallMaxThickness>("Max Wall Thickness", ++controlId, " u");
        toggleVar<triggerbot_vars::SeededFire>("Seeded Fire", ++controlId);
    });
}

void pageLegit() noexcept
{
    if (!searchIndexing) {
        pageSubTabPills(ImGui::GetWindowDrawList(), legitSubTab, kLegitSubTabs, 2, 9730);
        searchGlowSubTab = legitSubTab;
        controlId += legitSubTab * 500;
        if (legitSubTab == 0)
            pageLegitAim();
        else
            pageLegitTriggerbot();
        return;
    }

    for (int t = 0; t < 2; ++t) {
        pageSubTabPills(ImGui::GetWindowDrawList(), legitSubTab, kLegitSubTabs, 2, 9730);
        searchGlowSubTab = t;
        controlId += t * 500;
        if (t == 0)
            pageLegitAim();
        else
            pageLegitTriggerbot();
        columnYs[0] = columnYs[1] = 0;
    }
}

void pagePlayerInfo() noexcept
{
    using namespace player_info_vars;
    addCard("PLAYER INFO IN WORLD", 1, [] {
        enemiesAllOffVar<Enabled, OnlyEnemies>("Master Switch", ++controlId);
    });
    addCard("POSITION", 2, [] {
        toggleVar<PlayerPositionArrowEnabled>("Show Player Position Arrow", ++controlId);
        selectVar<PlayerPositionArrowColorMode>("Arrow Color", kPositionArrowColors, 2, ++controlId);
    });
    addCard("HEALTH", 2, [] {
        toggleVar<PlayerHealthEnabled>("Player Health", ++controlId);
        selectVar<PlayerHealthColorMode>("Health Text Color", kHealthTextColors, 2, ++controlId);
    });
    addCard("WEAPON", 2, [] {
        toggleVar<ActiveWeaponIconEnabled>("Active Weapon Icon", ++controlId);
        toggleVar<ActiveWeaponAmmoEnabled>("Active Weapon Ammo", ++controlId);
    });
    addCard("OBJECTIVES", 2, [] {
        toggleVar<BombCarrierIconEnabled>("Bomb Carrier Icon", ++controlId);
        toggleVar<BombPlantIconEnabled>("Bomb Planting Icon", ++controlId);
    });
    addCard("STATE ICONS", 4, [] {
        toggleVar<BombDefuseIconEnabled>("Defuse Icon", ++controlId);
        toggleVar<HostagePickupIconEnabled>("Picking Up Hostage Icon", ++controlId);
        toggleVar<HostageRescueIconEnabled>("Rescuing Hostage Icon", ++controlId);
        toggleVar<BlindedIconEnabled>("Blinded By Flashbang Icon", ++controlId);
    });
}

// --- glow page -------------------------------------------------------------------------
//
// One nav tab for both glow features: a segmented control at the top switches between the
// Outline Glow and Model Glow sections (same card layout, separate config variables).

void pageOutlineGlow() noexcept;
void pageModelGlow() noexcept;

void glowSubTabPills(ImDrawList* d) noexcept
{
    struct SubTab {
        const char* icon;
        const char* label;
    };
    static constexpr SubTab kSubTabs[] = {
        {"\xEF\x81\xAE", "Outline Glow"}, // eye
        {"\xEF\x87\xBC", "Model Glow"},   // paint-brush
    };

    const float pillWidth = s(172.0f);
    const float pillHeight = s(34.0f);
    const float gap = s(8.0f);
    const float contentWidth = 2 * s(281.0f) + s(10.0f); // the two-column card area
    const float x = shellBase.x + kSidebarWidth + s(9.0f) + (contentWidth - (2 * pillWidth + gap)) * 0.5f;
    const float y = shellBase.y + kToolbarHeight + s(24.0f) + s(4.0f) + columnYs[0] - scrollOffset;

    for (int i = 0; i < 2; ++i) {
        const ImVec2 p(x + i * (pillWidth + gap), y);
        ImGui::PushID(9700 + i);
        const bool clicked = hit("##glow_subtab", p, ImVec2(pillWidth, pillHeight));
        const bool active = glowSubTab == i;
        const float r = motion(animKey(0x671cu, 9700 + i), active ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
        ImGui::PopID();
        if (clicked && !active) {
            glowSubTab = i;
            // popovers belong to the other sub-tab's rows - close them
            state.popup.open = false;
            state.colorPickerOpen = false;
            state.multiSelectOpen = false;
            styleSelect.open = false;
        }
        d->AddRectFilled(p, p + ImVec2(pillWidth, pillHeight), mix(C(0, 0, 0, 0), kRowHover, r), pillHeight * 0.5f);
        d->AddRect(p, p + ImVec2(pillWidth, pillHeight), mix(kHairlineSoft, g_accent, r), pillHeight * 0.5f);
        textY(d, p.x + s(16), p.y, pillHeight, mix(C(137, 142, 153), g_accent, r), kSubTabs[i].icon, kTextIcon, iconFont());
        textY(d, p.x + s(38), p.y, pillHeight, mix(C(145, 149, 159), C(226, 228, 235), r), kSubTabs[i].label, kTextControl, nullptr);
    }
    // +s(30) after the pills: card section captions are drawn s(16) above their card, so without
    // extra clearance the caption text (e.g. "PLAYERS") starts exactly at the pill's bottom edge
    columnYs[0] = columnYs[1] = s(4.0f) + pillHeight + s(30.0f);
}

void pageGlow() noexcept
{
    if (!searchIndexing) {
        glowSubTabPills(ImGui::GetWindowDrawList());
        controlId += glowSubTab * 500; // per-sub-tab control-id ranges (popups/motion storage)
        searchGlowSubTab = glowSubTab;
        if (glowSubTab == 0)
            pageOutlineGlow();
        else
            pageModelGlow();
        return;
    }

    // ghost render for the search index: lay out BOTH sub-tabs, each exactly as it appears when
    // active (so a search hit scrolls to the right offset after opening its sub-tab)
    glowSubTabPills(ImGui::GetWindowDrawList());
    controlId += glowSubTab * 500;
    searchGlowSubTab = 0;
    pageOutlineGlow();
    columnYs[0] = columnYs[1] = 0;
    controlId += 500;
    glowSubTabPills(ImGui::GetWindowDrawList());
    searchGlowSubTab = 1;
    pageModelGlow();
}

void pageOutlineGlow() noexcept
{
    using namespace outline_glow_vars;
    addCard("OUTLINE GLOW", 1, [] {
        toggleVar<Enabled>("Master Switch", ++controlId);
    });
    addCard("PLAYERS", 1, [] {
        enemiesAllOffVar<GlowPlayers, GlowOnlyEnemies>("Glow Players", ++controlId);
    });
    addCard("PLAYER COLORS", 2, [] {
        colorVar<EnemyColor>("Enemy", ++controlId);
        colorVar<AllyColor>("Ally", ++controlId);
    });
    addCard("WEAPONS & GRENADES", 6, [] {
        toggleVar<GlowWeapons>("Glow Weapons on Ground", ++controlId);
        toggleVar<GlowGrenadeProjectiles>("Glow Grenade Projectiles", ++controlId);
        colorVar<model_glow_vars::FlashbangColor>("Flashbang", ++controlId);
        colorVar<model_glow_vars::HEGrenadeColor>("HE Grenade", ++controlId);
        colorVar<model_glow_vars::SmokeGrenadeColor>("Smoke Grenade", ++controlId);
        colorVar<model_glow_vars::MolotovColor>("Molotov / Incendiary", ++controlId);
    });
    addCard("BOMB & DEFUSE KIT", 6, [] {
        toggleVar<GlowDroppedBomb>("Glow Dropped Bomb", ++controlId);
        toggleVar<GlowTickingBomb>("Glow Ticking Bomb", ++controlId);
        toggleVar<GlowDefuseKits>("Glow Defuse Kits", ++controlId);
        colorVar<model_glow_vars::DroppedBombColor>("Dropped Bomb", ++controlId);
        colorVar<model_glow_vars::TickingBombColor>("Ticking Bomb", ++controlId);
        colorVar<model_glow_vars::DefuseKitColor>("Defuse Kit", ++controlId);
    });
    addCard("HOSTAGES", 2, [] {
        toggleVar<GlowHostages>("Glow Hostages", ++controlId);
        colorVar<outline_glow_vars::HostageColor>("Hostage", ++controlId);
    });
}

void pageModelGlow() noexcept
{
    using namespace model_glow_vars;
    addCard("MODEL GLOW", 1, [] {
        toggleVar<Enabled>("Master Switch", ++controlId);
    });
    addCard("PLAYERS", 1, [] {
        enemiesAllOffVar<GlowPlayers, GlowOnlyEnemies>("Player Models", ++controlId);
    });
    addCard("PLAYER COLORS", 2, [] {
        colorVar<EnemyColor>("Enemy", ++controlId);
        colorVar<AllyColor>("Ally", ++controlId);
    });
    addCard("WEAPONS & GRENADES", 6, [] {
        toggleVar<GlowWeapons>("Glow Weapon Models on Ground", ++controlId);
        toggleVar<GlowGrenadeProjectiles>("Glow Grenade Projectile Models", ++controlId);
        colorVar<outline_glow_vars::FlashbangColor>("Flashbang", ++controlId);
        colorVar<outline_glow_vars::HEGrenadeColor>("HE Grenade", ++controlId);
        colorVar<outline_glow_vars::SmokeGrenadeColor>("Smoke Grenade", ++controlId);
        colorVar<outline_glow_vars::MolotovColor>("Molotov / Incendiary", ++controlId);
    });
    addCard("BOMB & DEFUSE KIT", 6, [] {
        toggleVar<GlowDroppedBomb>("Glow Dropped Bomb Model", ++controlId);
        toggleVar<GlowTickingBomb>("Glow Ticking Bomb Model", ++controlId);
        toggleVar<GlowDefuseKits>("Glow Defuse Kit Models", ++controlId);
        colorVar<outline_glow_vars::DroppedBombColor>("Dropped Bomb", ++controlId);
        colorVar<outline_glow_vars::TickingBombColor>("Ticking Bomb", ++controlId);
        colorVar<outline_glow_vars::DefuseKitColor>("Defuse Kit", ++controlId);
    });
}

void pageViewmodel() noexcept
{
    using namespace viewmodel_mod_vars;
    addCard("VIEWMODEL FOV", 2, [] {
        toggleVar<ModifyFov>("Modify Viewmodel Fov", ++controlId);
        sliderVar<Fov>("Fov", ++controlId);
    });
    addCard("VIEWMODEL POSITION", 4, [] {
        toggleVar<ModifyPosition>("Modify Position", ++controlId);
        floatSliderVar<OffsetX>("Offset X", ++controlId, " u");
        floatSliderVar<OffsetY>("Offset Y", ++controlId, " u");
        floatSliderVar<OffsetZ>("Offset Z", ++controlId, " u");
    });
}

// FrameworkCS2 port batch: hitmarker, third person, removals, world colors, player list.
void pageEffects() noexcept
{
    addCard("HITMARKER", 5, [] {
        toggleVar<HitmarkerEnabled>("Hit Marker", ++controlId);
        floatSliderVar<HitmarkerLength>("Length", ++controlId);
        floatSliderVar<HitmarkerGap>("Gap", ++controlId);
        floatSliderVar<HitmarkerTimeout>("Fade Time (ms)", ++controlId);
        colorVar<HitmarkerColor>("Marker Color", ++controlId);
    });
    addCard("CAMERA", 3, [] {
        toggleVar<ForceThirdPersonEnabled>("Force Third Person", ++controlId);
        floatSliderVar<ForceThirdPersonDistance>("Distance", ++controlId);
        toggleVar<RemoveViewPunch>("Remove View Punch", ++controlId);
    });
    addCard("REMOVALS", 3, [] {
        toggleVar<RemoveLegs>("Remove First-Person Legs", ++controlId);
        toggleVar<RemoveFlashOverlay>("Remove Flash Overlay", ++controlId);
        toggleVar<RemoveMenuAds>("Remove Main Menu Ads", ++controlId);
    });
    addCard("CHAMS", 2, [] {
        toggleVar<chams_vars::Enabled>("Enemy Chams", ++controlId);
        colorVar<chams_vars::EnemyColor>("Enemy Color", ++controlId);
    });
    addCard("WORLD COLORS", 16, [] {
        toggleVar<WorldColorsInfernoEnabled>("Recolor Fire", ++controlId);
        colorVar<MolotovColor>("Molotov Color", ++controlId);
        colorVar<IncendiaryColor>("Incendiary Color", ++controlId);
        toggleVar<WorldColorsLightsEnabled>("Recolor Lights", ++controlId);
        colorVar<WorldColorsLightColor>("Light Color", ++controlId);
        toggleVar<WorldColorsSkyEnabled>("Recolor Sky", ++controlId);
        colorVar<WorldColorsSkyColor>("Sky Color", ++controlId);
        floatSliderVar<WorldColorsSkyBrightness>("Sky Brightness", ++controlId, "x");
        toggleVar<WorldColorsWorldEnabled>("Recolor World", ++controlId);
        colorVar<WorldColorsWorldColor>("World Color", ++controlId);
        toggleVar<WorldColorsFogEnabled>("Gradient Fog", ++controlId);
        colorVar<WorldColorsFogColor>("Fog Color", ++controlId);
        sliderVar<WorldColorsFogDensity>("Fog Density", ++controlId, "%");
        floatSliderVar<WorldColorsFogDistance>("Fog Distance", ++controlId);
        toggleVar<WorldColorsBloomEnabled>("Sky Bloom", ++controlId);
        sliderVar<WorldColorsBloomStrength>("Bloom Strength", ++controlId, "%");
    });
    addCard("GRENADE TIMERS", 3, [] {
        toggleVar<grenade_timers_vars::Enabled>("Master Switch", ++controlId);
        toggleVar<grenade_timers_vars::SmokeTimers>("Smoke Timers", ++controlId);
        toggleVar<grenade_timers_vars::MolotovTimers>("Molotov Timers", ++controlId);
    });
}

void pageHud() noexcept
{
    addCard("BOMB", 3, [] {
        toggleVar<BombTimerEnabled>("Bomb Explosion Countdown And Site", ++controlId);
        toggleVar<DefusingAlertEnabled>("Bomb Defuse Countdown", ++controlId);
        toggleVar<BombPlantAlertEnabled>("Bomb Plant Alert", ++controlId);
    });
    addCard("KILLFEED", 1, [] {
        toggleVar<KillfeedPreserverEnabled>("Preserve Killfeed", ++controlId);
    });
    addCard("WATERMARK", 8, [] {
        toggleVar<watermark_vars::Enabled>("Watermark", ++controlId);
        toggleVar<watermark_vars::ShowFps>("Show FPS", ++controlId);
        toggleVar<watermark_vars::ShowSpeed>("Show Speed", ++controlId);
        toggleVar<watermark_vars::ShowPing>("Show Ping", ++controlId);
        toggleVar<watermark_vars::ShowTeamDamage>("Show Team Damage", ++controlId);
        toggleVar<watermark_vars::ShowClock>("Show Clock", ++controlId);
        sliderVar<watermark_vars::OffsetX>("X Offset", ++controlId);
        sliderVar<watermark_vars::OffsetY>("Y Offset", ++controlId);
    });
    addCard("KEYBIND LIST", 1, [] {
        toggleVar<binds_list_vars::Enabled>("Keybind List", ++controlId);
    });
    addCard("SPECTATORS", 1, [] {
        toggleVar<spectator_list_params::SpectatorListEnabled>("Spectator List", ++controlId);
    });
    addCard("TIME", 1, [] {
        toggleVar<PostRoundTimerEnabled>("Post-round Timer", ++controlId);
    });
    addCard("HUD THEME", 1, [] {
        toggleVar<hud_theme_vars::Enabled>("Theme HUD Colors", ++controlId);
    });
    addCard("PLAYER LIST", 3, [] {
        toggleVar<PlayerListEnabled>("Player List", ++controlId);
        floatSliderVar<PlayerListOffsetX>("X Offset", ++controlId);
        floatSliderVar<PlayerListOffsetY>("Y Offset", ++controlId);
    });
    addCard("HIT COUNTERS", 2, [] {
        floatSliderVar<combat_stats_vars::CountersOffsetX>("X Offset", ++controlId);
        floatSliderVar<combat_stats_vars::CountersOffsetY>("Y Offset", ++controlId);
    });
    addCard("HIT FEED", 3, [] {
        sliderVar<combat_stats_vars::FeedLifetime>("Hide After", ++controlId, "s");
        sliderVar<combat_stats_vars::FeedOffsetX>("X Offset", ++controlId);
        sliderVar<combat_stats_vars::FeedOffsetY>("Y Offset", ++controlId);
    });
    addCard("STATUS CHIPS", 2, [] {
        floatSliderVar<status_panel_vars::OffsetX>("X Offset", ++controlId);
        floatSliderVar<status_panel_vars::OffsetY>("Y Offset", ++controlId);
    });
}

// VoiceDataFormat_t names (soundboard_vars::VoiceFormat; decoded from the embedded
// netmessages.proto descriptor: STEAM=0, ENGINE=1, OPUS=2).
constexpr const char* const kVoiceFormatNames[] = {"Steam", "Engine", "Opus"};

// Clip dropdown applier: stores the scanned list index in the config var.
void soundboardClipApply(int index) noexcept
{
    if (index < 0)
        return;
    ui_config::set<soundboard_vars::ClipIndex>(typename soundboard_vars::ClipIndex::ValueType{static_cast<std::uint8_t>(index)});
}

void pageSound() noexcept
{
    addCard("SOUNDS", 2, [] {
        toggleVar<HitSoundEnabled>("Hit Sound", ++controlId);
        toggleVar<SpawnProtectionSoundEnabled>("Spawn Protection End Sound", ++controlId);
    });
    addCard("PLAYER SOUND VISUALIZATION", 1, [] {
        toggleVar<FootstepSoundVisualizationEnabled>("Player Footstep Sound", ++controlId);
    });
    addCard("BOMB SOUND VISUALIZATION", 3, [] {
        toggleVar<BombPlantSoundVisualizationEnabled>("Bomb Plant Sound", ++controlId);
        toggleVar<BombBeepSoundVisualizationEnabled>("Bomb Beep Sound", ++controlId);
        toggleVar<BombDefuseSoundVisualizationEnabled>("Bomb Defuse Sound", ++controlId);
    });
    addCard("WEAPON SOUND VISUALIZATION", 2, [] {
        toggleVar<WeaponScopeSoundVisualizationEnabled>("Weapon Scope Sound", ++controlId);
        toggleVar<WeaponReloadSoundVisualizationEnabled>("Weapon Reload Sound", ++controlId);
    });
    // Sound board: in-process voice injection (Radio tab owns the Voice Key + Broadcast To
    // Voice rows). Clips come from <configDir>/sounds/*.wav; the dropdown renders the scanned
    // list (soundboard re-scans the folder once a second on the game thread; these statics are
    // its output, read here on the present thread).
    addCard("SOUNDBOARD", 7, [] {
        toggleVar<radio_vars::AirhornEnabled>("Soundboard", ++controlId);
        toggleVar<radio_vars::AirhornFirstBlood>("First Blood", ++controlId);
        toggleVar<radio_vars::AirhornHeadshot>("Headshot", ++controlId);
        toggleVar<radio_vars::AirhornRoundWin>("Round Win", ++controlId);
        static const char* const kNoClipPlaceholder[1] = {"no clips - drop wavs in configs/sounds"};
        static const char* clipOptions[soundboard::kMaxClips]{};
        int clipCountNow = 0;
        for (int i = 0; i < soundboard::clipCount && clipCountNow < soundboard::kMaxClips; ++i)
            clipOptions[clipCountNow++] = soundboard::clipNamePtrs[i];
        const bool haveClips = clipCountNow > 0;
        int clipSelected = haveClips ? static_cast<int>(ui_config::get<soundboard_vars::ClipIndex>()) : 0;
        selectList("Clip", &clipSelected, haveClips ? clipOptions : kNoClipPlaceholder,
                   haveClips ? clipCountNow : 1, ++controlId, &soundboardClipApply);
        keybindVar<soundboard_vars::SoundKeyBind>("Sound Key", ++controlId);
        selectVar<soundboard_vars::VoiceFormat>("Voice Format", kVoiceFormatNames,
                                                static_cast<int>(sizeof(kVoiceFormatNames) / sizeof(kVoiceFormatNames[0])), ++controlId);
    });
}

// --- movement (dedicated tab): automation + the edge/speed suite ---------------------

// USERINFO FLOOD restore pill: stages a one-shot restore the game thread consumes
// (snapshots the flood took at engage go back through setinfo). Same geometry as chatActionRow.
void userinfoRestoreRow(const char* label, const char* buttonText, int id) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    card.labelReserve = controlWidth + s(13.0f);
    beginRow(d, label);

    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool clicked = hit("##userinfo_restore", cp, ImVec2(controlWidth, s(23)));
    const float response = motion(animKey(0x8d5au, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();

    if (clicked)
        userinfo_flood::pendingRestore.store(1, std::memory_order_release);

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));
    const float textWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, buttonText).x;
    textY(d, cp.x + (controlWidth - textWidth) * 0.5f, cp.y, s(23), mix(C(170, 173, 184), C(226, 228, 235), response), buttonText, kTextControl, nullptr);
}

// Server lagger payload content modes (index = server_lagger_vars::PayloadMode). Zeros compress
// to ~nothing at the server relay = parse/decode-call storm; Random/Counter are incompressible =
// real relay bandwidth amplification (and audible white noise for listeners). No constant-run
// mode: the engine's voice parse validates audio content (all-0xFF failed the factory parse,
// live-verified 2026-09-13).
constexpr const char* const kLaggerPayloadNames[] = {"Zeros", "Rand7", "Count7", "Static (voice_data)", "Varint Mix"};

// Lagger profile presets (the friend v2's mode dropdown, mirrored): index 0 = Custom, 1+
// writes the three profile sliders. Mode 1 = the friend-source parse-storm profile (65x14,
// ~1.5KB msgs); Mode 2 = the 6x119 relay amplifier (~16KB msgs); Mode 3 = the friend's
// "extreme" (65x1000 - the engine's ~98KB/tick send buffer refuses past ~119 datagrams, so
// the count just rides the backpressure ceiling while it saturates: WILL self-kick).
constexpr const char* const kLaggerPresetNames[] = {"Custom", "Mode 1: 65x14", "Mode 2: 6x119", "Mode 3: 65x1000 (extreme)"};

struct LaggerPresetProfile {
    unsigned msgsPerBatch;
    unsigned audioKB;
    unsigned batchesPerTick;
};
constexpr LaggerPresetProfile kLaggerPresets[] = {
    {65, 1, 14},   // parse storm: max messages, small payloads - server parse/decode-call storm
    {6, 15, 119},  // relay amplifier: max payloads - the server relays every voice byte
    {65, 1, 2000}, // extreme: rides the backpressure ceiling (the friend's Mode 3)
};
static_assert(sizeof(kLaggerPresets) / sizeof(kLaggerPresets[0]) == sizeof(kLaggerPresetNames) / sizeof(kLaggerPresetNames[0]) - 1);

int laggerPresetIndex = 0;

void laggerPresetApply(int index) noexcept
{
    // ALWAYS commit the selection (including Custom = 0) - the per-frame sync re-reads the
    // config var, so a click that only mutates the local index snaps back within a frame
    // (the "cannot click Custom" bug).
    using PresetType = server_lagger_vars::Preset::ValueType;
    static_cast<void>(ui_config::set<server_lagger_vars::Preset>(PresetType{static_cast<std::uint8_t>(index)}));
    if (index <= 0 || index > static_cast<int>(sizeof(kLaggerPresets) / sizeof(kLaggerPresets[0])))
        return; // Custom: the sliders stay exactly as the user left them
    const auto& preset = kLaggerPresets[index - 1];
    using MsgsType = server_lagger_vars::MsgsPerBatch::ValueType;
    using KbType = server_lagger_vars::AudioKB::ValueType;
    using AmountType = server_lagger_vars::Amount::ValueType;
    static_cast<void>(ui_config::set<server_lagger_vars::MsgsPerBatch>(MsgsType{static_cast<unsigned char>(preset.msgsPerBatch)}));
    static_cast<void>(ui_config::set<server_lagger_vars::AudioKB>(KbType{static_cast<unsigned char>(preset.audioKB)}));
    static_cast<void>(ui_config::set<server_lagger_vars::Amount>(AmountType{static_cast<unsigned short>(preset.batchesPerTick)}));
}

// Probe button row: stages the request; the game thread answers with a [lagger] probe line
// (works while the lagger is disabled - server_lagger::probeRequest).
void laggerProbeRow(const char* label, const char* buttonText, int id) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    card.labelReserve = controlWidth + s(13.0f);
    beginRow(d, label);

    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool clicked = hit("##lagger_probe", cp, ImVec2(controlWidth, s(23)));
    const float response = motion(animKey(0x1a02u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();

    if (clicked)
        server_lagger::probeRequest.store(1, std::memory_order_release);

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));
    const float textWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, buttonText).x;
    textY(d, cp.x + (controlWidth - textWidth) * 0.5f, cp.y, s(23), mix(C(170, 173, 184), C(226, 228, 235), response), buttonText, kTextControl, nullptr);
}

void pageMovement() noexcept
{
    addCard("AUTOMATION", 4, [] {
        toggleVar<BlockbotEnabled>("Blockbot", ++controlId);
        toggleVar<BunnyhopEnabled>("Bunnyhop", ++controlId);
        toggleVar<AutoStrafeEnabled>("Auto Strafe", ++controlId);
        toggleVar<TestStraferEnabled>("Test Strafer", ++controlId);
    });
    addCard("EDGE & SPEED", 7, [] {
        toggleVar<movement_vars::EdgeJump>("Edge Jump", ++controlId);
        toggleVar<movement_vars::EdgeStop>("Edge Stop", ++controlId);
        toggleVar<movement_vars::SlowWalk>("Slow Walk", ++controlId);
        sliderVar<movement_vars::SlowWalkSpeed>("Slow Walk Speed", ++controlId, "%");
        toggleVar<movement_vars::FastLadder>("Fast Ladder", ++controlId);
        toggleVar<movement_vars::JumpBug>("Jump Bug", ++controlId);
        toggleVar<movement_vars::Desubtick>("Desubtick", ++controlId);
    });
    addCard("LAST TICK", 2, [] {
        toggleVar<last_tick_vars::Enabled>("Last Tick Defuse", ++controlId);
        keybindVar<last_tick_vars::DefuseKey>("Defuse Key", ++controlId);
    });
    addCard("GRENADES", 1, [] {
        toggleVar<supertoss_vars::Enabled>("Super Toss", ++controlId);
    });
    addCard("NET LAG", 8, [] {
        toggleVar<net_lag_vars::Enabled>("Net Lag Master", ++controlId);
        toggleVar<net_lag_vars::FakelagAlways>("Fakelag Always", ++controlId);
        keybindVar<net_lag_vars::ChokeKeyBind>("Choke Key", ++controlId);
        sliderVar<net_lag_vars::ChokeTicks>("Choke Ticks", ++controlId);
        sliderVar<net_lag_vars::BlipCount>("Blip Count", ++controlId);
        sliderVar<net_lag_vars::DupCount>("Dup Count", ++controlId);
        toggleVar<net_lag_vars::DelayEnabled>("Delay", ++controlId);
        sliderVar<net_lag_vars::DelayMs>("Delay Ms", ++controlId);
    });
    addCard("FLOOD", 5, [] {
        sliderVar<net_lag_vars::FloodBurstCount>("Flood Burst", ++controlId);
        keybindVar<net_lag_vars::FloodKeyBind>("Flood Key", ++controlId);
        sliderVar<net_lag_vars::ConnlessFloodCount>("Connless Flood", ++controlId);
        keybindVar<net_lag_vars::ConnlessKeyBind>("Connless Key", ++controlId);
        toggleVar<net_lag_vars::StatsEnabled>("Stats File", ++controlId);
    });
    addCard("USERINFO FLOOD", 7, [] {
        toggleVar<userinfo_flood_vars::Enabled>("Userinfo Flood", ++controlId);
        toggleVar<userinfo_flood_vars::DirectMode>("Direct Net Msg", ++controlId);
        static constexpr const char* const kUserinfoFieldNames[]{"Name", "Clutch Mode", "Team Color", "Crosshair Style", "Crosshair Color", "Crosshair Size", "Crosshair Gap", "Crosshair Thick", "Crosshair Outline", "Crosshair Dot", "Crosshair Alpha", "Crosshair Sniper", "Show Loadout", "Crosshair T", "Crosshair Outline Thick", "Crosshair R", "Crosshair G", "Crosshair B"};
        multiSelectVar<userinfo_flood_vars::FloodName, userinfo_flood_vars::FloodClutch, userinfo_flood_vars::FloodTeamColor,
                       userinfo_flood_vars::FloodXhStyle, userinfo_flood_vars::FloodXhColor, userinfo_flood_vars::FloodXhSize,
                       userinfo_flood_vars::FloodXhGap, userinfo_flood_vars::FloodXhThick, userinfo_flood_vars::FloodXhOutline,
                       userinfo_flood_vars::FloodXhDot, userinfo_flood_vars::FloodXhAlpha, userinfo_flood_vars::FloodXhSniper,
                       userinfo_flood_vars::FloodLoadout, userinfo_flood_vars::FloodTeamId, userinfo_flood_vars::FloodXhOutline,
                       userinfo_flood_vars::FloodXhColorR, userinfo_flood_vars::FloodXhColorG, userinfo_flood_vars::FloodXhColorB>("Fields", ++controlId, kUserinfoFieldNames);
        sliderVar<userinfo_flood_vars::SendsPerTick>("Sends Per Tick", ++controlId);
        sliderVar<userinfo_flood_vars::EveryTicks>("Every N Ticks", ++controlId);
        toggleVar<userinfo_flood_vars::HeavyMode>("Heavy Values", ++controlId);
        userinfoRestoreRow("Restore Values", "RESTORE", ++controlId);
    });
    addCard("SERVER LAGGER", 22, [] {
        toggleVar<server_lagger_vars::Enabled>("Server Lagger", ++controlId);
        laggerPresetIndex = static_cast<int>(ui_config::get<server_lagger_vars::Preset>());
        select("Preset", &laggerPresetIndex, kLaggerPresetNames,
               static_cast<int>(sizeof(kLaggerPresetNames) / sizeof(kLaggerPresetNames[0])), ++controlId, &laggerPresetApply);
        sliderVar<server_lagger_vars::MsgsPerBatch>("Msgs / Batch", ++controlId);
        sliderVar<server_lagger_vars::AudioKB>("Audio KB", ++controlId, "KB");
        sliderVar<server_lagger_vars::Amount>("Batches / Tick", ++controlId);
        selectVar<server_lagger_vars::PayloadMode>("Payload", kLaggerPayloadNames,
                                                   static_cast<int>(sizeof(kLaggerPayloadNames) / sizeof(kLaggerPayloadNames[0])), ++controlId);
        toggleVar<server_lagger_vars::DatagramMode>("Datagram Mode", ++controlId);
        toggleVar<server_lagger_vars::LoopFreeze>("Loop Freeze", ++controlId);
        sliderVar<server_lagger_vars::FreezeTicks>("Freeze Ticks", ++controlId);
        toggleVar<server_lagger_vars::AutoStop>("Auto Stop", ++controlId);
        toggleVar<server_lagger_vars::SlowRamp>("Slow Ramp", ++controlId);
        sliderVar<server_lagger_vars::RampInterval>("Ramp Step", ++controlId, "s");
        keybindVar<server_lagger_vars::LaggerKey>("Lagger Key", ++controlId);
        toggleVar<server_lagger_vars::NumPackets>("Decode Storm", ++controlId);
        toggleVar<server_lagger_vars::PulseMode>("Pulse Mode", ++controlId);
        sliderVar<server_lagger_vars::PulseOn>("Pulse On", ++controlId, " ticks");
        sliderVar<server_lagger_vars::PulseOff>("Pulse Off", ++controlId, " ticks");
        toggleVar<server_lagger_vars::MixMode>("Mix Mode", ++controlId);
        toggleVar<server_lagger_vars::PopulationScale>("Population Scale", ++controlId);
        toggleVar<server_lagger_vars::Misattribute>("Misattribute XUID", ++controlId);
        toggleVar<server_lagger_vars::MeterEnabled>("Lag-O-Meter", ++controlId);
        laggerProbeRow("NetChan", "PROBE NETCHAN", ++controlId);
    });
}

// Discord RPC template row: an InputText into the feature's static template buffer, persisted
// to <configDir>/discord_rpc.txt when the edit deactivates. Captureless so addCard can take it.
void discordRpcTemplateRow(const char* label, bool details, int id) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    card.labelReserve = s(96.0f); // InputText starts at a fixed x
    beginRow(d, label);

    char* buffer = nullptr;
    static_cast<void>(ui_config::withContext([&](auto&& hookContext) {
        buffer = details ? hookContext.template make<DiscordRpc>().detailsBuffer()
                         : hookContext.template make<DiscordRpc>().stateBuffer();
    }));
    if (!buffer) {
        const ImVec2 row = card.origin + ImVec2(0, (card.row - 1) * kRowHeight);
        textY(d, card.origin.x + s(96), row.y, kRowHeight, C(110, 114, 124), "unavailable", kTextSmall, nullptr);
        return;
    }

    const ImVec2 row = card.origin + ImVec2(0, (card.row - 1) * kRowHeight);
    ImGui::SetCursorScreenPos(ImVec2(row.x + s(96), row.y + rowCentered(s(25))));
    ImGui::PushItemWidth(card.width - s(96) - s(13));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kInsetBg);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextBodyCol);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(8), s(5)));
    ImGui::PushID(id);
    ImGui::InputText("##rpc_template", buffer, 192);
    ImGui::PopID();
    const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopItemWidth();
    if (deactivated)
        static_cast<void>(ui_config::withContext([](auto&& hookContext) {
            hookContext.template make<DiscordRpc>().saveTemplates();
        }));
}

// Kick reason picker: the searchable list popup with the full ENetworkDisconnectReason
// table (ChatTools.h kKickReasonEntries); defIndex = the code the server receives.
void kickReasonApply(int defIndex) noexcept
{
    if (defIndex < 1 || defIndex > 255)
        return;
    ui_config::set<chat_vars::KickReason>(chat_vars::KickReason::ValueType{static_cast<std::uint8_t>(defIndex)});
}

void kickReasonRow(const char* label, int id) noexcept
{
    const auto current = static_cast<int>(static_cast<chat_vars::KickReason::ValueType::ValueType>(ui_config::get<chat_vars::KickReason>()));

    ImDrawList* d = ImGui::GetWindowDrawList();
    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    card.labelReserve = controlWidth + s(13.0f);
    beginRow(d, label);

    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool click = hit("##kick_reason", cp, ImVec2(controlWidth, s(23)));
    const float response = motion(animKey(0x6161u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();

    if (click) {
        state.popup.open = !(state.popup.open && state.popup.owner == id);
        state.popup.owner = id;
        state.popup.options = nullptr;
        state.popup.count = 0;
        state.popup.apply = &kickReasonApply;
        state.popup.anchor = cp;
        state.popup.width = controlWidth;
        state.popup.openedFrame = ImGui::GetFrameCount();
        state.popup.paintKitMode = true;
        state.popup.paintKitDefIndex = 0;
        state.popup.itemList = chat_tools::kKickReasonEntries;
        state.popup.itemCount = chat_tools::kKickReasonCount;
        state.popup.stringList = nullptr; // clear other popup modes' state (stale-state shadowing)
        state.popup.stringCount = 0;
        state.popup.omitNone = true;
        state.popup.paintKitCurrentId = current;
        state.popup.paintKitScroll = 0.0f;
        state.popup.paintKitSearch[0] = '\0';
    }

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));
    const char* display = "?";
    for (int i = 0; i < chat_tools::kKickReasonCount; ++i) {
        if (static_cast<int>(chat_tools::kKickReasonEntries[i].defIndex) == current) {
            display = chat_tools::kKickReasonEntries[i].name;
            break;
        }
    }
    textY(d, cp.x + s(7), cp.y, s(23), C(182, 185, 196), display, kTextControl, nullptr);
    d->AddLine(cp + ImVec2(controlWidth - s(13), s(9)), cp + ImVec2(controlWidth - s(9), s(13)), C(139, 143, 154), 1.0f);
    d->AddLine(cp + ImVec2(controlWidth - s(9), s(13)), cp + ImVec2(controlWidth - s(5), s(9)), C(139, 143, 154), 1.0f);
}

// Radio phrase picker: opens the searchable/scrollable list popup in itemList mode with the

// Radio phrase picker: opens the searchable/scrollable list popup in itemList mode with the
// chat-wheel phrase entries (ChatTools.h kWheelRadioEntries); the apply writes the phrase
// index into the config. Selecting sets it for the Radio Spam loop instantly.
void radioPhraseApply(int defIndex) noexcept
{
    // defIndex is 1-based (0 was the suppressed None row); the config stores it directly
    if (defIndex < 1 || defIndex > chat_tools::kRadioPhraseCount)
        return;
    ui_config::set<chat_vars::RadioPhrase>(chat_vars::RadioPhrase::ValueType{static_cast<std::uint8_t>(defIndex)});
}

void radioPhraseRow(const char* label, int id) noexcept
{
    const auto current = ui_config::get<chat_vars::RadioPhrase>();
    const int currentIndex = static_cast<int>(current);
    const char* display = currentIndex >= 0 && currentIndex < chat_tools::kRadioPhraseCount
        ? chat_tools::kRadioPhrases[currentIndex].display
        : "?";

    ImDrawList* d = ImGui::GetWindowDrawList();
    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    card.labelReserve = controlWidth + s(13.0f);
    beginRow(d, label);

    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool click = hit("##radio_phrase", cp, ImVec2(controlWidth, s(23)));
    const float response = motion(animKey(0x6161u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();

    if (click) {
        state.popup.open = !(state.popup.open && state.popup.owner == id);
        state.popup.owner = id;
        state.popup.options = nullptr;
        state.popup.count = 0;
        state.popup.apply = &radioPhraseApply;
        state.popup.anchor = cp;
        state.popup.width = controlWidth;
        state.popup.openedFrame = ImGui::GetFrameCount();
        state.popup.paintKitMode = true;
        state.popup.paintKitDefIndex = 0;
        state.popup.itemList = chat_tools::kWheelRadioEntries;
        state.popup.itemCount = chat_tools::kRadioPhraseCount;
        state.popup.stringList = nullptr; // clear other popup modes' state (stale-state shadowing)
        state.popup.stringCount = 0;
        state.popup.omitNone = true; // the radio picker has no "none" - and entry 1-based
        state.popup.paintKitCurrentId = currentIndex + 1;
        state.popup.paintKitScroll = 0.0f;
        state.popup.paintKitSearch[0] = '\0';
    }

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));
    textY(d, cp.x + s(7), cp.y, s(23), C(182, 185, 196), display, kTextControl, nullptr);
    d->AddLine(cp + ImVec2(controlWidth - s(13), s(9)), cp + ImVec2(controlWidth - s(9), s(13)), C(139, 143, 154), 1.0f);
    d->AddLine(cp + ImVec2(controlWidth - s(9), s(13)), cp + ImVec2(controlWidth - s(5), s(9)), C(139, 143, 154), 1.0f);
}

// Chat tools sidecar template row
// Chat tools sidecar template row: InputText over ChatTools' static buffer, flushed to
// <configDir>/<sidecar> when the edit deactivates (DiscordRpc template pattern).
void chatTemplateRow(const char* label, int kind, int id) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    card.labelReserve = s(96.0f); // InputText starts at a fixed x
    beginRow(d, label);

    char* buffer = nullptr;
    static_cast<void>(ui_config::withContext([&](auto&& hookContext) {
        buffer = hookContext.template make<ChatTools>().buffer(kind);
    }));
    if (!buffer) {
        const ImVec2 row = card.origin + ImVec2(0, (card.row - 1) * kRowHeight);
        textY(d, card.origin.x + s(96), row.y, kRowHeight, C(110, 114, 124), "unavailable", kTextSmall, nullptr);
        return;
    }

    const ImVec2 row = card.origin + ImVec2(0, (card.row - 1) * kRowHeight);
    ImGui::SetCursorScreenPos(ImVec2(row.x + s(96), row.y + rowCentered(s(25))));
    ImGui::PushItemWidth(card.width - s(96) - s(13));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kInsetBg);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextBodyCol);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(8), s(5)));
    ImGui::PushID(id);
    ImGui::InputText("##chat_sidecar", buffer, chat_tools::kBufferTextSize);
    ImGui::PopID();
    const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopItemWidth();
    if (deactivated)
        static_cast<void>(ui_config::withContext([&](auto&& hookContext) {
            if (kind == chat_tools::kNameBuffer)
                hookContext.template make<ChatTools>().template saveBufferToFile<chat_tools::kNameBuffer>();
            else if (kind == chat_tools::kSpamBuffer)
                hookContext.template make<ChatTools>().template saveBufferToFile<chat_tools::kSpamBuffer>();
            else if (kind == chat_tools::kWheelBuffer)
                hookContext.template make<ChatTools>().template saveBufferToFile<chat_tools::kWheelBuffer>();
            else if (kind == chat_tools::kAnimatorBuffer)
                hookContext.template make<ChatTools>().template saveBufferToFile<chat_tools::kAnimatorBuffer>();
            else
                hookContext.template make<ChatTools>().template saveBufferToFile<chat_tools::kIntelBuffer>();
        }));
}

// Name stealer (CHAT card): the dropdown lists the CURRENT players, refreshed from the
// player-list snapshot every frame. Picking one stages the sanitized name through ChatTools'
// apply pipeline - the same `name "<text>"` + Steam-persona path as APPLY NAME. Steam itself
// rate-limits persona renames (see ChatTools setSteamPersonaName) - a rejected rename is
// Steam's cooldown, not a broken feature. The option table is file-scope because select()'s
// popover commits through the applier in a LATER frame.
constexpr int kStealMax = player_list::kMaxRows;
char stealNameBufs[kStealMax][40]{};
const char* stealOptions[kStealMax]{};
player_list::Row stealRowCache[kStealMax]{};
int stealRowCount = 0;
int stealSelected = 0;

void refreshStealList() noexcept
{
    const auto snap = player_list::snapshot();
    stealRowCount = 0;
    for (int i = 0; i < snap.count && stealRowCount < kStealMax; ++i) {
        if (snap.rows[i].isLocalPlayer || snap.rows[i].name[0] == '\0')
            continue;
        stealRowCache[stealRowCount] = snap.rows[i];
        std::snprintf(stealNameBufs[stealRowCount], sizeof(stealNameBufs[0]), "%s", snap.rows[i].name);
        stealOptions[stealRowCount] = stealNameBufs[stealRowCount];
        ++stealRowCount;
    }
}

void stolenNameApply(int index) noexcept
{
    if (index < 0 || index >= stealRowCount)
        return;
    char* buffer = nullptr;
    static_cast<void>(ui_config::withContext([&](auto&& hookContext) {
        buffer = hookContext.template make<ChatTools>().buffer(chat_tools::kNameBuffer);
    }));
    if (!buffer)
        return;
    static_cast<void>(chat_tools::sanitizeInto(stealRowCache[index].name, buffer, chat_tools::kBufferTextSize));
    std::memcpy(chat_tools::pendingNameText, buffer, chat_tools::kBufferTextSize);
    chat_tools::pendingNameApply.store(1, std::memory_order_release);
}

// Chat tools one-shot action row: mode 0 = APPLY NAME (stage buffer + apply), 1 = clone the
// top-fragger's name, 2 = ban theater. All publish request atomics; the game thread acts.
void chatActionRow(const char* label, const char* buttonText, int id, int action) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    card.labelReserve = controlWidth + s(13.0f);
    beginRow(d, label);

    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool clicked = hit("##chat_action", cp, ImVec2(controlWidth, s(23)));
    const float response = motion(animKey(0x7c21u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();

    if (clicked) {
        if (action == 0) {
            // stage the buffer text for the game thread (one-click path, no sidecar read needed)
            static_cast<void>(ui_config::withContext([](auto&& hookContext) {
                char* buffer = hookContext.template make<ChatTools>().buffer(chat_tools::kNameBuffer);
                std::memcpy(chat_tools::pendingNameText, buffer, chat_tools::kBufferTextSize);
            }));
            chat_tools::pendingNameApply.store(1, std::memory_order_release);
        } else if (action == 1) {
            chat_tools::pendingCloneFragger.store(1, std::memory_order_release);
        } else if (action == 2) {
            chat_tools::pendingBanTheater.store(1, std::memory_order_release);
        }
    }

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(kPillBg, kPillBgHover, response), s(7));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(kHairlineSoft, g_accent, response), s(7));
    const float textWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, buttonText).x;
    textY(d, cp.x + (controlWidth - textWidth) * 0.5f, cp.y, s(23), mix(C(170, 173, 184), C(226, 228, 235), response), buttonText, kTextControl, nullptr);
}

// Name animator modes (order = name_animator_vars::Mode values).
constexpr const char* const kAnimatorModeNames[] = {"Typewriter", "Glitch", "Marquee", "Scramble", "Binary", "Flicker", "Backwards", "Mocking", "Pulse", "Strobe", "Wave", "Crawler", "Storm", "Nystagmus", "Emoji Strobe", "Flashbang", "Twitch", "Face Storm", "Super Wave", "RLO Flip", "Vaporwave", "Invisible Chaos", "Zalgo"};
// Clan tag animator modes (order = chat_vars::ClanTagAnimateMode values; tuned for short tags).
constexpr const char* const kClanTagModeNames[] = {"Typewriter", "Glitch", "Marquee", "Wave", "Strobe", "Pulse"};

// Persona preset dropdown: commits the chosen recipe into the name buffer, stages it for the
// game thread and fires the apply in one click. Session-static selection index.
constexpr const char* const kPersonaPresetNames[] = {
    "cat Enjoyer", "s1mple", "Valve Employee", "VAC Live", "VAC Live (plain)", "Fake VAC Ban", "Invisible", "Fullwidth", "Reversed",
    "Server Restart", "Cooldown", "Vote Kick", "Admin Msg", "GOTV",
    "Long Excuse", "Long Discord", "Long Baiter", "Long GG Next",
    "Report Confirm", "Overwatch Verdict", "Rank Update", "Premier Drop", "Trust Factor", "Item Drop", "Reconnecting", "Trade Ban",
    "Zero-Width Mix", "RLO Flip", "Bidi Stack", "Combining Marks", "Zalgo Recipe",
    "Superscript Mix", "Fullwidth", "Persian Digits", "NBSP Padding", "Template Bait", "Format Partials",
    "Zalgo Wall", "Bidi Wall", "Zero-Width Wall", "Chaos Wall", "Fullwidth Wall", "Mixed Glyph Wall",
    "Nines Wall", "NaN Chain", "Sci Overflow", "Overflow Wall", "SQL Theater", "Format Wall",
};
int chatPresetIndex = -1;

// Fake-kick reason names (order = chat_vars::KickReason; the codes live in
// chat_tools::kKickReasonCodes)

void chatPresetApply(int index) noexcept
{
    if (index < 0 || index >= chat_tools::kNamePresetCount)
        return;
    char* buffer = nullptr;
    static_cast<void>(ui_config::withContext([&](auto&& hookContext) {
        buffer = hookContext.template make<ChatTools>().buffer(chat_tools::kNameBuffer);
    }));
    if (!buffer)
        return;
    std::snprintf(buffer, chat_tools::kBufferTextSize, "%s", chat_tools::kNamePresets[index]);
    std::memcpy(chat_tools::pendingNameText, buffer, chat_tools::kBufferTextSize);
    chat_tools::pendingNameApply.store(1, std::memory_order_release);
}

// --- glitch text generator (glyphy-style; Misc > GLITCH TEXT) ----------------------------
// The user types a source text, picks a style + intensity and the GLITCHED result renders live
// in an output field they can copy from (the COPY pill puts it on the REAL desktop clipboard via
// the SDL clipboard bridge; Ctrl+C on a selection in the read-only field works too). The output
// is meant to be pasted into the name feature (Fake Name / the animator's Animate Text), not
// applied directly.
constexpr const char* const kGlitchStyleNames[] = {"Zalgo", "Cursed", "Demon", "Alien", "Strike", "Random", "Heavy"};
constexpr const char* const kCursedPresetNames[] = {
    "Off", "Bottom Whisper", "Top Sigh", "Dual Whisper", "Underdot", "Tremor", "Tremor Underdot",
    "Ring Seal", "Hook", "Balanced", "Heavy Pressure", "Depravity", "Heaven-Earth", "Triple Doom",
    "Triple Abyss", "Cross Slash", "Strike Hex", "Wall x5", "Wall x8", "Wall x12", "Eldritch x15",
};

// Combining-mark pools. These render in CS2 names when attached to base letters (the Zalgo
// Recipe lesson: a base letter between mark stacks is what renders; per-glyph stacks are fine).
// 2-byte range U+0300-0x36F + the extended 3-byte ranges (U+1AB0 combining extended, U+20D0
// marks-for-symbols, U+FE20 half marks) that give the HEAVY wall its glyph variety - some of the
// extended glyphs render as boxes in CS2, which only adds to the wall.
struct GlitchMarkRange { std::uint32_t lo, hi; };
constexpr GlitchMarkRange kMarksUp[] = {{0x300, 0x306}, {0x309, 0x30F}, {0x311, 0x312}, {0x315, 0x315}, {0x31A, 0x31E}};
constexpr GlitchMarkRange kMarksDown[] = {{0x316, 0x323}, {0x327, 0x329}, {0x32C, 0x32E}, {0x331, 0x335}};
constexpr GlitchMarkRange kMarksMid[] = {{0x334, 0x338}};
constexpr GlitchMarkRange kMarksExtendedUp[] = {{0x1AB0, 0x1AFF}, {0x20D0, 0x20F0}};
constexpr GlitchMarkRange kMarksHalf[] = {{0xFE20, 0xFE2F}};

inline std::uint32_t glitchGenSeed{0x9E3779B9u};

[[nodiscard]] std::uint32_t glitchGenRandom() noexcept
{
    glitchGenSeed = glitchGenSeed * 1664525u + 1013904223u;
    return glitchGenSeed >> 9;
}

[[nodiscard]] std::uint32_t glitchMarkFrom(const GlitchMarkRange* ranges, int count, std::uint32_t roll) noexcept
{
    std::uint32_t total = 0;
    for (int i = 0; i < count; ++i)
        total += ranges[i].hi - ranges[i].lo + 1;
    std::uint32_t k = roll % total;
    for (int i = 0; i < count; ++i) {
        const std::uint32_t span = ranges[i].hi - ranges[i].lo + 1;
        if (k < span)
            return ranges[i].lo + k;
        k -= span;
    }
    return ranges[0].lo;
}

[[nodiscard]] std::uint32_t glitchMarkForStyle(int style) noexcept
{    // 0 Zalgo: up+down, 1 Cursed: everything classic, 2 Demon: down+mid, 3 Alien: up only,
    // 4 Strike: mid only, 5 Random: any family per mark,
    // 6 HEAVY: classic up/down dominate, extended ranges layered in (the glyphy wall).
    std::uint32_t family;
    switch (style) {
    case 0: family = glitchGenRandom() % 2; break;
    case 1: family = glitchGenRandom() % 3; break;
    case 2: family = 1 + glitchGenRandom() % 2; break;
    case 3: family = 0; break;
    case 4: family = 2; break;
    case 5: family = glitchGenRandom() % 3; break;
    default: {
        const std::uint32_t roll = glitchGenRandom() % 10;
        family = roll < 3 ? 0 : roll < 6 ? 1 : roll < 8 ? 3 : roll < 9 ? 4 : 2;
        break;
    }
    }
    switch (family) {
    case 0: return glitchMarkFrom(kMarksUp, static_cast<int>(sizeof(kMarksUp) / sizeof(kMarksUp[0])), glitchGenRandom());
    case 1: return glitchMarkFrom(kMarksDown, static_cast<int>(sizeof(kMarksDown) / sizeof(kMarksDown[0])), glitchGenRandom());
    case 2: return glitchMarkFrom(kMarksMid, static_cast<int>(sizeof(kMarksMid) / sizeof(kMarksMid[0])), glitchGenRandom());
    case 3: return glitchMarkFrom(kMarksExtendedUp, static_cast<int>(sizeof(kMarksExtendedUp) / sizeof(kMarksExtendedUp[0])), glitchGenRandom());
    default: return glitchMarkFrom(kMarksHalf, static_cast<int>(sizeof(kMarksHalf) / sizeof(kMarksHalf[0])), glitchGenRandom());
    }
}

// Fixed cursed recipes (the symboldb generator's cards): one FIXED mark stack applied to every
// glyph - deterministic, unlike the random Style path. All marks live in the 2-byte U+0300-0x36F
// range. Index 0 of the dropdown = Off (Style/Intensity path instead).
struct CursedPreset {
    const char* name;
    std::uint32_t marks[18];
    int count;
};
constexpr CursedPreset kCursedPresets[] = {
    {"Off", {}, 0},
    {"Bottom Whisper", {0x336}, 1},
    {"Top Sigh", {0x300}, 1},
    {"Dual Whisper", {0x30F, 0x336}, 2},
    {"Underdot", {0x323}, 1},
    {"Tremor", {0x308}, 1},
    {"Tremor Underdot", {0x308, 0x323}, 2},
    {"Ring Seal", {0x31A}, 1},
    {"Hook", {0x327}, 1},
    {"Balanced", {0x306, 0x336}, 2},
    {"Heavy Pressure", {0x310, 0x30E}, 2},
    {"Depravity", {0x322, 0x323}, 2},
    {"Heaven-Earth", {0x302, 0x308, 0x31D}, 3},
    {"Triple Doom", {0x310, 0x30E, 0x311}, 3},
    {"Triple Abyss", {0x322, 0x323, 0x326}, 3},
    {"Cross Slash", {0x336, 0x308}, 2},
    {"Strike Hex", {0x335, 0x336}, 2},
    {"Wall x5", {0x336, 0x336, 0x336, 0x336, 0x336}, 5},
    {"Wall x8", {0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336}, 8},
    {"Wall x12", {0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336}, 12},
    {"Eldritch x15", {0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336, 0x336}, 15},
};
constexpr int kCursedPresetCount = static_cast<int>(sizeof(kCursedPresets) / sizeof(kCursedPresets[0]));
static_assert(sizeof(kCursedPresetNames) / sizeof(kCursedPresetNames[0]) == kCursedPresetCount,
    "kCursedPresetNames / kCursedPresets diverged");
static_assert(kCursedPresetCount - 1 <= 20, "kCursedPresetCount exceeds the Preset config range");

void generateGlitchText(const char* input, char* output, std::size_t outputCap) noexcept
{
    // The output feeds the name pipeline: strip what sanitizeInto would strip up front, so a
    // copied result pastes into Fake Name / the animator unchanged.
    char safe[96];
    static_cast<void>(chat_tools::sanitizeInto(input, safe, sizeof(safe)));

    const int style = static_cast<int>(ui_config::get<glitch_gen_vars::Style>());
    const int intensity = static_cast<int>(ui_config::get<glitch_gen_vars::Intensity>());
    const int preset = static_cast<int>(ui_config::get<glitch_gen_vars::Preset>());
    const CursedPreset* fixedPreset = preset > 0 && preset < kCursedPresetCount ? &kCursedPresets[preset] : nullptr;

    int maxMarks = 1 + intensity / 12; // 0% -> 1, 100% -> 9 marks per glyph
    if (!fixedPreset) {
        if (style == 1)                    // Cursed: double the density
            maxMarks *= 2;
        else if (style == 6)               // HEAVY: the glyphy wall - 10..60 marks per glyph
            maxMarks = 10 + intensity / 2;
    }

    std::size_t o = 0;
    for (std::size_t i = 0; safe[i] != '\0' && o + 5 < outputCap;) {
        const unsigned char lead = static_cast<unsigned char>(safe[i]);
        int span = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        while (span > 1 && safe[i + span] == '\0')
            --span;
        // base glyph first - marks only render when attached to a base letter
        std::memcpy(output + o, safe + i, static_cast<std::size_t>(span));
        o += span;
        if (fixedPreset) {
            // deterministic fixed stack - the same marks on every glyph, like the reference cards
            for (int m = 0; m < fixedPreset->count && o + 5 < outputCap; ++m) {
                const std::uint32_t cp = fixedPreset->marks[m];
                output[o++] = static_cast<char>(0xC0 | (cp >> 6));
                output[o++] = static_cast<char>(0x80 | (cp & 0x3F));
            }
        } else {
            const int marks = maxMarks > 0 ? static_cast<int>(glitchGenRandom() % static_cast<std::uint32_t>(maxMarks + 1)) : 0;
            for (int m = 0; m < marks && o + 5 < outputCap; ++m) {
                const std::uint32_t cp = glitchMarkForStyle(style);
                if (cp < 0x800) {
                    output[o++] = static_cast<char>(0xC0 | (cp >> 6));
                    output[o++] = static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    output[o++] = static_cast<char>(0xE0 | (cp >> 12));
                    output[o++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    output[o++] = static_cast<char>(0x80 | (cp & 0x3F));
                }
            }
        }
        i += span;
    }
    output[o] = '\0';
}

char glitchGenInput[96] = "";
char glitchGenOutput[4096] = "";
char glitchGenLastInput[96] = {'\xFF', '\0'}; // forces the first generation
int glitchGenLastStyle = -1;
int glitchGenLastIntensity = -1;
int glitchGenLastPreset = -1;

// Regenerates whenever the inputs changed (live preview, no Enter needed); Reroll force-rolls
// fresh noise. Returns true when a regeneration happened.
bool refreshGlitchGenOutput() noexcept
{
    const int style = static_cast<int>(ui_config::get<glitch_gen_vars::Style>());
    const int intensity = static_cast<int>(ui_config::get<glitch_gen_vars::Intensity>());
    const int preset = static_cast<int>(ui_config::get<glitch_gen_vars::Preset>());
    if (style == glitchGenLastStyle && intensity == glitchGenLastIntensity && preset == glitchGenLastPreset
        && std::strcmp(glitchGenInput, glitchGenLastInput) == 0)
        return false;
    glitchGenLastStyle = style;
    glitchGenLastIntensity = intensity;
    glitchGenLastPreset = preset;
    std::memcpy(glitchGenLastInput, glitchGenInput, sizeof(glitchGenLastInput));
    generateGlitchText(glitchGenInput, glitchGenOutput, sizeof(glitchGenOutput));
    return true;
}

void pageMiscGeneral() noexcept
{
    addCard("INTERFACE", 1, [] {
        toggleVar<MenuReduceMotion>("Reduce Motion", ++controlId);
    });

    addCard("LOGGING", 5, [] {
        toggleVar<HitLogEnabled>("Log Hits", ++controlId);
        toggleVar<TeamDamageTrackerEnabled>("Team Damage Tracker", ++controlId);
        toggleVar<VoteRevealerEnabled>("Vote Revealer", ++controlId);
        toggleVar<CooldownRevealerEnabled>("Cooldown Revealer", ++controlId);
        toggleVar<reveal_radar_vars::Enabled>("Reveal Radar", ++controlId);
    });

    addCard("ACCOUNT", 11, [] {
        toggleVar<FakePrimeEnabled>("Fake Prime", ++controlId);
        toggleVar<FakeLevelEnabled>("Fake Level", ++controlId);
        sliderVar<FakeLevelValue>("Level", ++controlId);
        sliderVar<FakeLevelXp>("Level Xp", ++controlId);
        toggleVar<FakePremierEnabled>("Fake Premier Score", ++controlId);
        sliderVar<FakePremierScore>("Premier Score", ++controlId);
        toggleVar<FakeCommendsEnabled>("Fake Commends", ++controlId);
        sliderVar<FakeCommendsFriendly>("Friendly Commends", ++controlId);
        sliderVar<FakeCommendsTeaching>("Teaching Commends", ++controlId);
        sliderVar<FakeCommendsLeader>("Leader Commends", ++controlId);
        toggleVar<MatchAutoAcceptEnabled>("Match Auto Accept", ++controlId);
    });

    addCard("PLAYER ANALYZER", 9, [] {
        toggleVar<analyzer_vars::Enabled>("Cheat O Meter", ++controlId);
        analyzerMultiSelect("Scan Targets", ++controlId);
        toggleVar<analyzer_vars::EspTag>("ESP Tag", ++controlId);
        sliderVar<analyzer_vars::SnapThreshold>("Snap Threshold", ++controlId, " deg");
        toggleVar<analyzer_vars::VoiceProbe>("Voice Probe", ++controlId);
        toggleVar<analyzer_vars::VoiceLog>("Voice Log", ++controlId);
        toggleVar<analyzer_vars::Callout>("Callout", ++controlId);
        toggleVar<analyzer_vars::CalloutTeamChat>("Callout To Team", ++controlId);
        sliderVar<analyzer_vars::CalloutThreshold>("Callout Score", ++controlId);
    });
}

void pageMiscChat() noexcept
{
    // chat tools: everything here is NETWORKED (`say` / `playerchatwheel` / the `name`
    // userinfo convar) - the server relays it to every player. Text lives in sidecar files
    // (chat_name.txt / chat_spam.txt / chatwheel.txt / chat_names.txt next to the configs),
    // edited in-place. {nl} in any text becomes a real line break in chat (U+2028).
    addCard("CHAT", 18, [] {
        chatTemplateRow("Fake Name", chat_tools::kNameBuffer, ++controlId);
        chatActionRow("Apply Name", "APPLY NAME", ++controlId, 0);
        selectList("Persona Preset", &chatPresetIndex, kPersonaPresetNames, static_cast<int>(sizeof(kPersonaPresetNames) / sizeof(kPersonaPresetNames[0])), ++controlId, &chatPresetApply);
        chatActionRow("Impersonate", "CLONE TOP FRAGGER", ++controlId, 1);
        refreshStealList();
        static const char* const kStealPlaceholder[1] = {"no players - live match needed"};
        select("Steal Name", &stealSelected, stealRowCount > 0 ? stealOptions : kStealPlaceholder,
               stealRowCount > 0 ? stealRowCount : 1, ++controlId, &stolenNameApply);
        // CS2 servers read the name at connect: with this on, a successful rename runs `retry`
        // so the server picks the new name up immediately.
        toggleVar<chat_vars::NameForceReconnect>("Force Reconnect", ++controlId);
        toggleVar<chat_vars::NameCycleEnabled>("Name Cycle", ++controlId);
        sliderVar<chat_vars::NameCycleInterval>("Cycle Every", ++controlId, "s");
        toggleVar<name_animator_vars::Enabled>("Name Animator", ++controlId);
        chatTemplateRow("Animate Text", chat_tools::kAnimatorBuffer, ++controlId);
        selectVar<name_animator_vars::Mode>("Animate Mode", kAnimatorModeNames,
                                            static_cast<int>(sizeof(kAnimatorModeNames) / sizeof(kAnimatorModeNames[0])), ++controlId);
        sliderVar<name_animator_vars::Speed>("Animate Speed", ++controlId);
        toggleVar<name_animator_vars::DirectSend>("Direct Rename", ++controlId);
        // Clan tag spoof (2026-09-23 update): local display rewrite of the controller's
        // sanitized clan tag. The networked tag is Steam-clan/GC authoritative - this shows
        // only on OUR screen (scoreboard, chat decoration, death notices). The animator
        // drives the SAME rewrite frame by frame (no clan userinfo cvar exists in CS2, so
        // there is no server-visible setinfo route).
        toggleVar<chat_vars::ClanTagEnabled>("Clan Tag", ++controlId);
        chatTemplateRow("Clan Tag Text", chat_tools::kClanTagBuffer, ++controlId);
        toggleVar<chat_vars::ClanTagAnimateEnabled>("Animate Clan Tag", ++controlId);
        selectVar<chat_vars::ClanTagAnimateMode>("Tag Mode", kClanTagModeNames,
                                                 static_cast<int>(sizeof(kClanTagModeNames) / sizeof(kClanTagModeNames[0])), ++controlId);
        sliderVar<chat_vars::ClanTagAnimateSpeed>("Tag Speed", ++controlId);
    });

    addCard("GLITCH TEXT", 7, [] {
        textInputRow("Text", glitchGenInput, sizeof(glitchGenInput), ++controlId);
        selectVar<glitch_gen_vars::Preset>("Curse", kCursedPresetNames,
                                           static_cast<int>(sizeof(kCursedPresetNames) / sizeof(kCursedPresetNames[0])), ++controlId);
        selectVar<glitch_gen_vars::Style>("Style", kGlitchStyleNames,
                                          static_cast<int>(sizeof(kGlitchStyleNames) / sizeof(kGlitchStyleNames[0])), ++controlId);
        sliderVar<glitch_gen_vars::Intensity>("Intensity", ++controlId, "%");
        refreshGlitchGenOutput();
        // read-only preview: selecting text in it + Ctrl+C copies through the REAL clipboard
        textInputRow("Output", glitchGenOutput, sizeof(glitchGenOutput), ++controlId, ImGuiInputTextFlags_ReadOnly);
        if (actionRow("Copy", "COPY", ++controlId)) {
            if (gui_sdl::functions.setClipboardText)
                gui_sdl::functions.setClipboardText(glitchGenOutput);
            pushToast("Copied to clipboard", g_accent);
        }
        if (actionRow("Reroll", "REROLL", ++controlId)) {
            generateGlitchText(glitchGenInput, glitchGenOutput, sizeof(glitchGenOutput));
        }
    });

    addCard("PRANKS", 7, [] {
        chatActionRow("Ban Theater", "RUN IT", ++controlId, 2);
        sliderVar<chat_vars::TheaterDelay>("Retry After", ++controlId, "s");
        chatTemplateRow("Intel Text", chat_tools::kIntelBuffer, ++controlId);
        toggleVar<chat_vars::IntelEnabled>("Fake Intel", ++controlId);
        keybindVar<chat_vars::IntelBind>("Intel Key", ++controlId);
        kickReasonRow("Kick Reason", ++controlId);
        keybindVar<chat_vars::KickKey>("Fake Kick Key", ++controlId);
    });

    addCard("CHAT SPAM", 5, [] {
        chatTemplateRow("Spam Text", chat_tools::kSpamBuffer, ++controlId);
        toggleVar<chat_vars::SpamEnabled>("Chat Spam", ++controlId);
        sliderVar<chat_vars::SpamCount>("Burst Lines", ++controlId);
        sliderVar<chat_vars::SpamInterval>("Spam Interval", ++controlId, "00ms");
        toggleVar<KillsayEnabled>("Killsay From File", ++controlId);
    });

    addCard("CHAT FUN", 9, [] {
        radioPhraseRow("Radio Phrase", ++controlId);
        toggleVar<chat_vars::WheelEnabled>("Radio Spam", ++controlId);
        sliderVar<chat_vars::WheelInterval>("Radio Interval", ++controlId, "00ms");
        toggleVar<chat_vars::PingSpamEnabled>("Ping Spam", ++controlId);
        sliderVar<chat_vars::PingInterval>("Ping Interval", ++controlId, "00ms");
        toggleVar<chat_vars::HudColorCycle>("Fast Color Cycle", ++controlId);
        sliderVar<chat_vars::HudColorCycleSpeed>("Cycle Speed", ++controlId, " /s");
        toggleVar<chat_vars::StreakRadioEnabled>("Killstreak Radio", ++controlId);
        toggleVar<chat_vars::LiveBadgeEnabled>("Stream Live Badge", ++controlId);
    });
}

void pageMiscOther() noexcept
{
    addCard("VIEW ANGLES", 7, [] {
        toggleVar<FvaEnabled>("FVA Rotation Chains", ++controlId);
        sliderVar<fva_vars::Substeps>("Chain Steps", ++controlId);
        toggleVar<FvaReseedChains>("Reseed Chains After Publish", ++controlId);
        toggleVar<FvaChainOnFireOnly>("Publish Only While Firing", ++controlId);
        toggleVar<FvaZeroOriginSpoof>("Zero Origin Experiment", ++controlId);
        toggleVar<FvaSilentShots>("Silent Shots Experiment", ++controlId);
        toggleVar<ValveDsSpoofEnabled>("Spoof Valve DS Flag", ++controlId);
    });
    addCard("PANIC", 1, [] {
        keybindVar<panic_vars::Bind>("Combat Panic", ++controlId);
    });
    addCard("SPECTATE ENEMIES", 1, [] {
        toggleVar<spectate_vars::Enabled>("Spectate Enemies", ++controlId);
    });
    addCard("DISCORD RPC", 3, [] {
        toggleVar<discord_rpc_vars::Enabled>("Rich Presence", ++controlId);
        discordRpcTemplateRow("Details", true, ++controlId);
        discordRpcTemplateRow("Status", false, ++controlId);
    });
}

void pageMisc() noexcept
{
    if (!searchIndexing) {
        pageSubTabPills(ImGui::GetWindowDrawList(), miscSubTab, kMiscSubTabs, 3, 9740);
        searchGlowSubTab = miscSubTab;
        controlId += miscSubTab * 500;
        if (miscSubTab == 0)
            pageMiscGeneral();
        else if (miscSubTab == 1)
            pageMiscChat();
        else
            pageMiscOther();
        return;
    }

    for (int t = 0; t < 3; ++t) {
        pageSubTabPills(ImGui::GetWindowDrawList(), miscSubTab, kMiscSubTabs, 3, 9740);
        searchGlowSubTab = t;
        controlId += t * 500;
        if (t == 0)
            pageMiscGeneral();
        else if (t == 1)
            pageMiscChat();
        else
            pageMiscOther();
        columnYs[0] = columnYs[1] = 0;
    }
}

// --- inventory (skin changer) ---------------------------------------------------------
// Six category pills over one per-category card. Every weapon row is a full skin triple:
// a searchable PaintKitDatabase finish picker + wear + pattern seed. Only the active
// category's rows exist per frame, so the ~100 rows never render at once.

int inventoryCategory = 0;

constexpr const char* const kKnifeModels[] = {"None", "Bayonet", "Bowie Knife", "Butterfly Knife", "Classic Knife", "Falchion Knife", "Flip Knife", "Gut Knife", "Huntsman Knife", "Karambit", "Kukri Knife", "M9 Bayonet", "Navaja Knife", "Nomad Knife", "Paracord Knife", "Shadow Daggers", "Skeleton Knife", "Stiletto Knife", "Survival Knife", "Talon Knife", "Ursus Knife"};

void inventoryPills(ImDrawList* d) noexcept
{
    static constexpr const char* const kNames[] = {"Knives", "Pistols", "SMGs", "Heavy", "Rifles", "Snipers"};
    constexpr int kCount = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));
    const float gap = s(8.0f);
    const float contentWidth = 2 * s(281.0f) + s(10.0f);
    const float pillWidth = (contentWidth - (kCount - 1) * gap) / kCount;
    const float pillHeight = s(30.0f);
    const float x = shellBase.x + kSidebarWidth + s(9.0f);
    const float y = shellBase.y + kToolbarHeight + s(24.0f) + s(4.0f) + columnYs[0] - scrollOffset;

    for (int i = 0; i < kCount; ++i) {
        const ImVec2 p(x + i * (pillWidth + gap), y);
        ImGui::PushID(9800 + i);
        const bool clicked = hit("##inv_category", p, ImVec2(pillWidth, pillHeight));
        const bool active = inventoryCategory == i;
        const float r = motion(animKey(0x1acau, 9800 + i), active ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
        ImGui::PopID();
        if (clicked && !active) {
            inventoryCategory = i;
            // popovers belong to the other category's rows - close them
            state.popup.open = false;
            state.colorPickerOpen = false;
            state.multiSelectOpen = false;
            styleSelect.open = false;
        }
        d->AddRectFilled(p, p + ImVec2(pillWidth, pillHeight), mix(C(0, 0, 0, 0), kRowHover, r), pillHeight * 0.5f);
        d->AddRect(p, p + ImVec2(pillWidth, pillHeight), mix(kHairlineSoft, g_accent, r), pillHeight * 0.5f);
        const float labelWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, kNames[i]).x;
        textY(d, p.x + (pillWidth - labelWidth) * 0.5f, p.y, pillHeight, mix(C(145, 149, 159), C(226, 228, 235), r), kNames[i], kTextControl, nullptr);
    }
    // clearance below the pills so the card caption does not touch them
    columnYs[0] = columnYs[1] = s(4.0f) + pillHeight + s(30.0f);
}

// One weapon's skin triple: finish picker (raw paint kit id, validated against the weapon's
// PaintKitDatabase list by the apply path too) + wear + pattern seed.
template <typename SkinVar, typename WearVar, typename SeedVar>
void weaponSkinRows(std::uint16_t defIndex) noexcept
{
    const auto* list = cs2::paintKitListFor(defIndex);
    paintKitRow<SkinVar>(list ? list->weaponName : "?", defIndex, ++controlId);
    wearRow<WearVar>("Wear", ++controlId);
    seedRow<SeedVar>("Seed", ++controlId);
}

void pageInventoryCategory(int category) noexcept
{
    using namespace skin_changer_vars;
    switch (category) {
    case 0: {
        // Knife finish ids are validated against the IMPERSONATED model's list (per-model
        // finish kits exist). With no model selected only the generic-knife finishes are
        // offered (defIndex 0 = the popup's generic mode), since the real equipped knife type
        // is unknown at config time. addCard takes a plain function pointer, so the resolved
        // def index goes through a static instead of a lambda capture.
        const auto knifeModel = static_cast<KnifeModelSelection>(ui_config::get<KnifeModel>());
        static std::uint16_t knifeFinishDefIndex = 0;
        knifeFinishDefIndex = SkinChangerData::resolveKnifeModel(knifeModel).has_value()
            ? static_cast<std::uint16_t>(*SkinChangerData::resolveKnifeModel(knifeModel))
            : std::uint16_t{0};
        addCard("KNIVES", 6, [] {
            selectVar<KnifeModel>("Knife Model", kKnifeModels, 21, ++controlId);
            paintKitRow<KnifeSkin>("Knife Finish", knifeFinishDefIndex, ++controlId);
            wearRow<KnifeSkinWear>("Wear", ++controlId);
            seedRow<KnifeSkinSeed>("Seed", ++controlId);
            toggleVar<StatTrakEnabled>("StatTrak", ++controlId);
            {
                const auto value = ui_config::get<StatTrakValue>();
                int statTrak = static_cast<int>(value);
                if (sliderRow("StatTrak Value", &statTrak, 0, 9999, ++controlId, nullptr))
                    ui_config::set<StatTrakValue>(static_cast<std::uint16_t>(statTrak));
            }
        });
        break;
    }
    case 1:
        addCard("PISTOLS", 30, [] {
            weaponSkinRows<DesertEagleSkin, DesertEagleSkinWear, DesertEagleSkinSeed>(1);
            weaponSkinRows<DualBerettasSkin, DualBerettasSkinWear, DualBerettasSkinSeed>(2);
            weaponSkinRows<FiveSeveNSkin, FiveSeveNSkinWear, FiveSeveNSkinSeed>(3);
            weaponSkinRows<Glock18Skin, Glock18SkinWear, Glock18SkinSeed>(4);
            weaponSkinRows<P2000Skin, P2000SkinWear, P2000SkinSeed>(32);
            weaponSkinRows<P250Skin, P250SkinWear, P250SkinSeed>(36);
            weaponSkinRows<Tec9Skin, Tec9SkinWear, Tec9SkinSeed>(30);
            weaponSkinRows<CZ75AutoSkin, CZ75AutoSkinWear, CZ75AutoSkinSeed>(63);
            weaponSkinRows<R8RevolverSkin, R8RevolverSkinWear, R8RevolverSkinSeed>(64);
            weaponSkinRows<USPSSkin, USPSSkinWear, USPSSkinSeed>(61);
        });
        break;
    case 2:
        addCard("SMGS", 21, [] {
            weaponSkinRows<MAC10Skin, MAC10SkinWear, MAC10SkinSeed>(17);
            weaponSkinRows<MP5SDSkin, MP5SDSkinWear, MP5SDSkinSeed>(23);
            weaponSkinRows<MP7Skin, MP7SkinWear, MP7SkinSeed>(33);
            weaponSkinRows<MP9Skin, MP9SkinWear, MP9SkinSeed>(34);
            weaponSkinRows<P90Skin, P90SkinWear, P90SkinSeed>(19);
            weaponSkinRows<PPBizonSkin, PPBizonSkinWear, PPBizonSkinSeed>(26);
            weaponSkinRows<UMP45Skin, UMP45SkinWear, UMP45SkinSeed>(24);
        });
        break;
    case 3:
        addCard("HEAVY", 18, [] {
            weaponSkinRows<M249Skin, M249SkinWear, M249SkinSeed>(14);
            weaponSkinRows<MAG7Skin, MAG7SkinWear, MAG7SkinSeed>(27);
            weaponSkinRows<NegevSkin, NegevSkinWear, NegevSkinSeed>(28);
            weaponSkinRows<NovaSkin, NovaSkinWear, NovaSkinSeed>(35);
            weaponSkinRows<SawedOffSkin, SawedOffSkinWear, SawedOffSkinSeed>(29);
            weaponSkinRows<XM1014Skin, XM1014SkinWear, XM1014SkinSeed>(25);
        });
        break;
    case 4:
        addCard("RIFLES", 21, [] {
            weaponSkinRows<AK47Skin, AK47SkinWear, AK47SkinSeed>(7);
            weaponSkinRows<AUGSkin, AUGSkinWear, AUGSkinSeed>(8);
            weaponSkinRows<FamasSkin, FamasSkinWear, FamasSkinSeed>(10);
            weaponSkinRows<GalilARSkin, GalilARSkinWear, GalilARSkinSeed>(13);
            weaponSkinRows<M4A1SSkin, M4A1SSkinWear, M4A1SSkinSeed>(60);
            weaponSkinRows<M4A4Skin, M4A4SkinWear, M4A4SkinSeed>(16);
            weaponSkinRows<SG553Skin, SG553SkinWear, SG553SkinSeed>(39);
        });
        break;
    case 5:
        addCard("SNIPER RIFLES", 12, [] {
            weaponSkinRows<AWPSkin, AWPSkinWear, AWPSkinSeed>(9);
            weaponSkinRows<G3SG1Skin, G3SG1SkinWear, G3SG1SkinSeed>(11);
            weaponSkinRows<SCAR20Skin, SCAR20SkinWear, SCAR20SkinSeed>(38);
            weaponSkinRows<SSG08Skin, SSG08SkinWear, SSG08SkinSeed>(40);
        });
        break;
    }
}

void pageInventoryAgent() noexcept
{
    using namespace agent_changer_vars;
    addCard("AGENT", 1, [] {
        itemDefRow<AgentDef>("Agent Model", cs2::kAgentItems, static_cast<int>(sizeof(cs2::kAgentItems) / sizeof(cs2::kAgentItems[0])), ++controlId);
    });
}

void pageInventory() noexcept
{
    if (!searchIndexing) {
        inventoryPills(ImGui::GetWindowDrawList());
        // per-category control-id ranges: 6 categories * 150 + ~31 rows stay inside the
        // page's own [page*1000, (page+1)*1000) id space (the old *500 leaked into Scripts')
        controlId += inventoryCategory * 150;
        pageInventoryCategory(inventoryCategory);
        pageInventoryAgent();
        return;
    }

    // ghost render for the search index: lay out EVERY category exactly as it appears when
    // active (so a search hit scrolls to the right offset after opening its category)
    for (int category = 0; category < 6; ++category) {
        searchGlowSubTab = category; // reused as the generic sub-tab tag of the indexed row
        inventoryPills(ImGui::GetWindowDrawList());
        if (category)
            controlId += 150;
        pageInventoryCategory(category);
        pageInventoryAgent();
        columnYs[0] = columnYs[1] = 0;
    }
}

// --- web radio (dedicated tab) -------------------------------------------------------
//
// TuneIn/RadioTime web radio, driven through RadioManager: the regional local-station list,
// free-text station search, and click-to-play. Fetches and playback run asynchronously on the
// HOST (steam-runtime-launch-client -> curl/mpv with ffplay fallback, see RadioManager.h);
// this page only draws
// state and fires actions. One full-width card instead of the two-column flow: a list needs the
// room, and the row pool below is driven by the live result count.

// URL-encodes a search query into one token (RadioManager passes it straight into the endpoint
// URL, and the old Panorama side did the encoding - now that is this UI's job).
void percentEncodeUrl(const char* src, char* dst, std::size_t cap) noexcept
{
    constexpr char hex[] = "0123456789ABCDEF";
    std::size_t o = 0;
    for (std::size_t i = 0; src[i] != '\0' && o + 4 < cap; ++i) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '.' || c == '_' || c == '~';
        if (unreserved) {
            dst[o++] = static_cast<char>(c);
        } else {
            dst[o++] = '%';
            dst[o++] = hex[c >> 4];
            dst[o++] = hex[c & 0xF];
        }
    }
    dst[o] = '\0';
}

template <typename Functor>
void withRadio(Functor&& functor) noexcept
{
    // Returns false when the context is missing/shutting down - dropping the action then is the
    // intended teardown behavior, so the result is explicitly discarded.
    static_cast<void>(ui_config::withContext([&functor](auto&& hookContext) {
        functor(hookContext.template make<RadioManager>());
    }));
}

void pageRadio() noexcept
{
    withRadio([](auto&& radio) { radio.poll(); });

    // First visit of the session: pull the regional local-station list so the tab is never
    // empty on open. (Also fires once during the search-index ghost pass - a harmless extra
    // fetch, results are shared static state.)
    static bool initialBrowseFired = false;
    if (!initialBrowseFired) {
        initialBrowseFired = true;
        withRadio([](auto&& radio) { radio.startBrowseLocal(); });
    }

    static char searchBuf[64] = "";
    char encodedQuery[200] = "";

    // Snapshot state once (all of it lives in RadioManager's statics, so the pointers stay valid
    // for the rest of the frame); the manager is only touched for actions afterwards.
    int stationCount = 0;
    const RadioStation* stations = nullptr;
    char headerText[160] = "Stations";
    bool fetching = false;
    const char* playingId = nullptr;
    const char* playingName = nullptr;
    int favCount = 0;
    int recCount = 0;
    // Snapshot caps mirror RadioManager::kMaxFavorites / kMaxRecent (kept in sync by the loops).
    constexpr int kFavSnap = 16;
    constexpr int kRecSnap = 8;
    const char* favIdSnap[kFavSnap] = {};
    const char* favNameSnap[kFavSnap] = {};
    const char* recIdSnap[kRecSnap] = {};
    const char* recNameSnap[kRecSnap] = {};
    constexpr int kStationSnap = 40; // mirrors RadioManager::kMaxResults
    bool stationFavSnap[kStationSnap] = {};
    withRadio([&](auto&& radio) {
        stationCount = radio.stationCount();
        stations = stationCount > 0 ? &radio.station(0) : nullptr;
        fetching = radio.fetching();
        playingId = radio.lastPlayed();
        playingName = radio.lastPlayedName();
        for (int i = 0; i < stationCount && i < kStationSnap; ++i)
            stationFavSnap[i] = radio.isFavorite(stations[i].id);
        favCount = ImMin(radio.favoriteCount(), kFavSnap);
        for (int i = 0; i < favCount; ++i) {
            favIdSnap[i] = radio.favoriteId(i);
            favNameSnap[i] = radio.favoriteName(i);
        }
        recCount = ImMin(radio.recentCount(), kRecSnap);
        for (int i = 0; i < recCount; ++i) {
            recIdSnap[i] = radio.recentId(i);
            recNameSnap[i] = radio.recentName(i);
        }
        if (const char* h = radio.header(); h[0] != '\0')
            std::snprintf(headerText, sizeof(headerText), "%s", h);
    });

    ImDrawList* d = ImGui::GetWindowDrawList();
    constexpr int kFixedRows = 6; // search / now playing / volume / broadcast / voice key / results header
    const int favRows = favCount > 0 ? favCount + 1 : 0; // + section header
    const int recRows = recCount > 0 ? recCount + 1 : 0; // + section header
    const int listRows = stationCount > 0 ? stationCount : 1;
    const float width = kShellWidth - kSidebarWidth - s(13.0f);
    const float height = (kFixedRows + favRows + recRows + listRows) * kRowHeight + s(12.0f);

    const float x = shellBase.x + kSidebarWidth + s(9.0f);
    const float y = shellBase.y + kToolbarHeight + s(24.0f) + columnYs[0] - scrollOffset;

    text(d, ImVec2(x, y - s(16.0f)), C(89, 94, 106), "WEB RADIO", kTextCaption, nullptr);
    const ImVec2 p(x, y);
    softShadow(d, p, p + ImVec2(width, height), s(14.0f));
    d->AddRectFilled(p, p + ImVec2(width, height), kCardBg, s(16.0f));
    d->AddRect(p, p + ImVec2(width, height), kHairlineSoft, s(14.0f));

    card = CardContext{p + ImVec2(0, s(6.0f)), width, 0};

    auto pillButton = [&](int id, const char* label, ImVec2 pos, float buttonWidth, ImFont* font, float fontSize) {
        ImGui::PushID(id);
        const bool clicked = hit("##pill_btn", pos, ImVec2(buttonWidth, s(23)));
        const float hover = motion(animKey(0x3a11u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
        ImGui::PopID();
        d->AddRectFilled(pos, pos + ImVec2(buttonWidth, s(23)), mix(kPillBg, kPillBgHover, hover), s(7));
        d->AddRect(pos, pos + ImVec2(buttonWidth, s(23)), mix(kHairlineSoft, g_accent, hover), s(7));
        const float labelWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label).x;
        textY(d, pos.x + (buttonWidth - labelWidth) * 0.5f, pos.y, s(23), C(170, 173, 184), label, fontSize, font);
        return clicked;
    };

    // Row 0: search field (Enter submits) + LOCAL (back to the regional list).
    beginRow(d, "Search");
    {
        const ImVec2 row = card.origin + ImVec2(0, 0 * kRowHeight);
        const ImVec2 inputPos(row.x + s(70), row.y + rowCentered(s(25)));
        const float inputWidth = width - s(70) - s(76);
        ImGui::SetCursorScreenPos(inputPos);
        ImGui::PushItemWidth(inputWidth);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kInsetBg);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextBodyCol);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(8), s(5)));
        const bool submitted = ImGui::InputTextWithHint("##radio_search", "search stations...", searchBuf, sizeof(searchBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopItemWidth();
        if (submitted && searchBuf[0] != '\0') {
            percentEncodeUrl(searchBuf, encodedQuery, sizeof(encodedQuery));
            withRadio([&encodedQuery](auto&& radio) { radio.startSearch(encodedQuery); });
        }
        if (pillButton(++controlId, "LOCAL", ImVec2(p.x + width - s(13) - s(56), inputPos.y), s(56), ImGui::GetFont(), kTextControl))
            withRadio([](auto&& radio) { radio.startBrowseLocal(); });
    }

    // Row 1: now playing + RESUME / STOP.
    beginRow(d, "Now Playing");
    {
        const float rowY = card.origin.y + 1 * kRowHeight;
        const float stopX = p.x + width - s(13) - s(56);
        const float resumeX = stopX - s(64) - s(6);
        const bool stopClicked = pillButton(++controlId, "\xEF\x81\x8D", ImVec2(stopX, rowY + rowCentered(s(23))), s(56), iconFont(), kTextIcon); // stop
        const bool resumeClicked = pillButton(++controlId, "\xEF\x81\x8B", ImVec2(resumeX, rowY + rowCentered(s(23))), s(64), iconFont(), kTextIcon); // play
        if (stopClicked)
            withRadio([](auto&& radio) { radio.stop(); });
        if (resumeClicked)
            withRadio([](auto&& radio) { radio.resume(); });

        const float nameX = p.x + s(105);
        d->PushClipRect(ImVec2(nameX, rowY), ImVec2(resumeX - s(8), rowY + kRowHeight), true);
        if (playingName != nullptr && playingName[0] != '\0') {
            d->AddCircleFilled(ImVec2(nameX + s(5), rowY + kRowHeight * 0.5f), s(3), g_accent);
            textY(d, nameX + s(14), rowY, kRowHeight, g_accent, playingName, kTextControl, nullptr);
        } else {
            textY(d, nameX, rowY, kRowHeight, C(110, 114, 124), "nothing - pick a station below", kTextControl, nullptr);
        }
        d->PopClipRect();
    }

    // Row 2: volume (standard slider primitive; pushed live into mpv's IPC socket while a
    // station plays - no re-press needed. ffplay fallback applies it at the next play).
    sliderVar<radio_vars::Volume>("Volume", ++controlId, "%");

    // Row 3: mic broadcast - while a station plays, CS2's mic capture is switched to the radio
    // (host-side virtual source fed by ffmpeg) and a SYNTHETIC push-to-talk press holds the
    // voice gate open, so the whole team hears the station with zero setup: no Voice Key
    // needed (one is auto-bound to F9 when yours is Off/mouse - unbound again on stop).
    // Tip: Volume 0 keeps it team-only (you hear nothing, they hear everything).
    // On stop/toggle-off the real mic comes back and PTT behavior is restored.
    toggleVar<radio_vars::MicBroadcast>("Broadcast To Voice", ++controlId);

    // Voice key row: OPTIONAL manual override for the synthetic PTT press above (and the
    // airhorn clips). Leave it Off (or on a mouse button) and broadcast auto-binds F9 for
    // hands-free transmit; set it to your in-game keyboard voice key only if you prefer
    // your own bind over the auto-key.
    // The airhorn trigger toggles live on the SOUND tab; the mic routing machinery lives
    // here with the broadcast.
    keybindVar<radio_vars::VoiceKeyBind>("Voice Key (optional)", ++controlId);

    // NOW PLAYING HUD box: the bottom-left element mirroring this tab's playback (station +
    // track via the host-side ICY burst probe). While nothing plays it can instead mirror the
    // host desktop's media players (MPRIS via playerctl). Both draggable in game.
    toggleVar<radio_vars::ShowNowPlaying>("Show Now Playing HUD", ++controlId);
    toggleVar<radio_vars::ShowMediaPlayers>("Show Desktop Players", ++controlId);

    // Sections: persisted favorites (star toggles back off) and this session's recently played.
    auto sectionHeader = [&](const char* title, const char* right) {
        const float rowY = card.origin.y + card.row * kRowHeight;
        if (card.row)
            d->AddLine(ImVec2(card.origin.x + s(12), rowY), ImVec2(card.origin.x + width - s(12), rowY), kHairline);
        textY(d, card.origin.x + s(13), rowY, kRowHeight, C(150, 154, 165), title, kTextControl, nullptr);
        if (right && right[0] != '\0') {
            const float rightWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, right).x;
            textY(d, card.origin.x + width - s(13) - rightWidth, rowY, kRowHeight, C(110, 114, 124), right, kTextSmall, nullptr);
        }
        ++card.row;
    };

    auto savedStationRow = [&](int idSalt, const char* id, const char* name, bool starred, bool playing) {
        const float rowY = card.origin.y + card.row * kRowHeight;
        d->AddLine(ImVec2(card.origin.x + s(12), rowY), ImVec2(card.origin.x + width - s(12), rowY), kHairline);

        // star toggle at the right edge: on favorites rows it removes; on result rows it adds
        const float starX = p.x + width - s(13) - s(22);
        ImGui::PushID(idSalt);
        const bool starClicked = hit("##radio_star", ImVec2(starX, rowY + rowCentered(s(22))), ImVec2(s(22), s(22))) && !searchIndexing;
        const float starHover = motion(animKey(0x5a27u, idSalt), ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
        ImGui::PopID();
        if (starClicked)
            withRadio([&](auto&& radio) { radio.toggleFavorite(id, name); });

        // name area plays the station
        ImGui::PushID(idSalt + 10000);
        const bool clicked = hit("##radio_saved", ImVec2(p.x + s(4), rowY), ImVec2(width - s(8) - s(26), kRowHeight));
        const float hover = motion(animKey(0xbe22u, idSalt + 10000), ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
        ImGui::PopID();
        if (hover > 0.001f)
            d->AddRectFilled(ImVec2(p.x + s(4), rowY), ImVec2(p.x + width - s(4) - s(26), rowY + kRowHeight), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(14 * hover) << IM_COL32_A_SHIFT), s(6));

        textY(d, p.x + s(13), rowY, kRowHeight, starred ? g_accent : mix(C(110, 114, 124), C(190, 194, 204), starHover), kIconStar, s(12.0f), iconFont()); // star
        textY(d, p.x + s(38), rowY, kRowHeight, playing ? g_accent : kTextBodyCol, name, kTextControl, nullptr);
        return clicked;
    };

    if (favCount > 0) {
        sectionHeader("FAVORITES", "click star to remove");
        for (int i = 0; i < favCount; ++i) {
            const bool playing = playingId != nullptr && playingId[0] != '\0' && std::strcmp(playingId, favIdSnap[i]) == 0;
            if (savedStationRow(++controlId, favIdSnap[i], favNameSnap[i], true, playing) && !searchIndexing) {
                const int playedIndex = i;
                withRadio([&playedIndex](auto&& radio) { radio.playFavorite(playedIndex); });
            }
            ++card.row;
        }
    }
    if (recCount > 0) {
        sectionHeader("RECENTLY PLAYED", "this session");
        for (int i = 0; i < recCount; ++i) {
            const bool playing = playingId != nullptr && playingId[0] != '\0' && std::strcmp(playingId, recIdSnap[i]) == 0;
            if (savedStationRow(++controlId, recIdSnap[i], recNameSnap[i], false, playing) && !searchIndexing) {
                const int playedIndex = i;
                withRadio([&playedIndex](auto&& radio) { radio.playRecent(playedIndex); });
            }
            ++card.row;
        }
    }

    // Row 3: results header + count / loading state.
    {
        const float rowY = card.origin.y + card.row * kRowHeight;
        if (card.row)
            d->AddLine(ImVec2(card.origin.x + s(12), rowY), ImVec2(card.origin.x + width - s(12), rowY), kHairline);
        textY(d, card.origin.x + s(13), rowY, kRowHeight, C(150, 154, 165), headerText, kTextControl, nullptr);
        char right[32];
        if (fetching)
            std::snprintf(right, sizeof(right), "loading...");
        else if (stationCount > 0)
            std::snprintf(right, sizeof(right), "%d station%s", stationCount, stationCount == 1 ? "" : "s");
        else
            right[0] = '\0';
        if (right[0] != '\0') {
            const float rightWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, right).x;
            textY(d, card.origin.x + width - s(13) - rightWidth, rowY, kRowHeight, C(110, 114, 124), right, kTextSmall, nullptr);
        }
        ++card.row;
    }

    // Station rows: click to play. The playing station gets the accent treatment.
    if (stationCount == 0) {
        const float rowY = card.origin.y + card.row * kRowHeight;
        textY(d, card.origin.x + s(13), rowY, kRowHeight, C(110, 114, 124),
            fetching ? "fetching stations..." : "no stations found", kTextControl, nullptr);
        ++card.row;
    } else {
        for (int i = 0; i < stationCount; ++i) {
            const RadioStation& st = stations[i];
            const bool playing = playingId != nullptr && playingId[0] != '\0' && std::strcmp(playingId, st.id) == 0;
            const float rowY = card.origin.y + card.row * kRowHeight;
            d->AddLine(ImVec2(card.origin.x + s(12), rowY), ImVec2(card.origin.x + width - s(12), rowY), kHairline);

            ImGui::PushID(4200 + i);
            const bool clicked = hit("##station", ImVec2(p.x + s(4), rowY), ImVec2(width - s(8) - s(26), kRowHeight));
            const float hover = motion(animKey(0xbe21u, 4200 + i), ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
            ImGui::PopID();
            if (hover > 0.001f)
                d->AddRectFilled(ImVec2(p.x + s(4), rowY), ImVec2(p.x + width - s(4) - s(26), rowY + kRowHeight), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(14 * hover) << IM_COL32_A_SHIFT), s(6));

            // favorite star at the right edge of the row
            {
                const float starX = p.x + width - s(13) - s(22);
                const bool fav = i < kStationSnap && stationFavSnap[i];
                ImGui::PushID(4400 + i);
                const bool starClicked = hit("##radio_star", ImVec2(starX, rowY + rowCentered(s(22))), ImVec2(s(22), s(22))) && !searchIndexing;
                const float starHover = motion(animKey(0x5a27u, 4400 + i), ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
                ImGui::PopID();
                if (starClicked) {
                    const RadioStation& stRef = stations[i];
                    withRadio([&](auto&& radio) { radio.toggleFavorite(stRef.id, stRef.text); });
                }
                textY(d, starX + s(5), rowY, kRowHeight, fav ? g_accent : mix(C(110, 114, 124), C(190, 194, 204), starHover), kIconStar, s(12.0f), iconFont()); // star
            }

            constexpr float iconX = 13.0f;
            constexpr float nameX = 38.0f;
            textY(d, p.x + s(iconX), rowY, kRowHeight, playing ? g_accent : mix(C(120, 124, 134), C(226, 228, 235), hover), "\xEF\x81\x8B", s(11.0f), iconFont()); // play
            textY(d, p.x + s(nameX), rowY, kRowHeight, playing ? g_accent : kTextBodyCol, st.text, kTextControl, nullptr);

            if (st.subtext[0] != '\0') {
                const float subRight = card.origin.x + width - s(39); // clears the favorite star
                const float subRegionW = width - s(273.0f) - s(26.0f);
                d->PushClipRect(ImVec2(card.origin.x + s(260.0f), rowY), ImVec2(subRight, rowY + kRowHeight), true);
                const float subWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, st.subtext).x;
                textY(d, card.origin.x + s(260.0f) + ImMax(0.0f, subRegionW - subWidth), rowY, kRowHeight, C(110, 114, 124), st.subtext, kTextSmall, nullptr);
                d->PopClipRect();
            }

            if (clicked && !searchIndexing) {
                const int playedIndex = i;
                withRadio([&playedIndex](auto&& radio) { radio.playResult(playedIndex); });
            }
            ++card.row;
        }
    }

    columnYs[0] += height + s(30.0f);
    columnYs[1] = columnYs[0];
}

// --- scripts (Lua framework tab) -------------------------------------------------------
// One full-width card: scripts directory listing with per-row LOAD/UNLOAD/EDIT/DELETE, a create
// row, and the script editor as a separate RESIZABLE ImGui window (opened per script). Script
// state lives in lua:: (Features/Lua/LuaManager.h); this page only drives it.

struct ScriptEditor {
    bool open = false;
    char name[lua::kMaxScriptName] = {};      // file being edited (with .lua)
    char buffer[98 * 1024] = {};              // editor content (fits kMaxScriptBytes)
    bool dirty = false;
    char status[160] = {};                    // last save/load feedback
    double statusTime = 0.0;                  // shown for a few seconds after a save/revert
};

ScriptEditor scriptEditor;

// Control-id base for script-owned gui.* items - far above the page*1000 controlId space and
// the sliderRow id+500000 pills, so nothing collides.
constexpr int kScriptGuiControlBase = 900000;

// One script-owned gui.* row, shared by the Scripts-subtab cards (pageScriptControls) and the
// native-page cards (renderScriptPageCardRows) so widget coverage can never drift between the
// two. Every GuiItem::Type renders through a shared menu widget - never a local variant.
void renderScriptGuiItem(lua::Script& script, int scriptSlot, int itemIndex, int id) noexcept
{
    lua::GuiItem& item = script.guiItems[itemIndex];
    if (item.type == lua::GuiItem::Type::Checkbox)
        toggle(item.label, &item.boolValue, id);
    else if (item.type == lua::GuiItem::Type::Dropdown)
        scriptDropdownRow(script, scriptSlot, itemIndex, id);
    else if (item.type == lua::GuiItem::Type::Color)
        scriptColorRow(script, itemIndex, id);
    else if (item.type == lua::GuiItem::Type::Keybind)
        keybindRow(item.label, &item.intValue, id);
    else if (item.type == lua::GuiItem::Type::FloatSlider)
        sliderRowFloat(item.label, &item.floatValue, item.floatMin, item.floatMax, id, "");
    else if (item.type == lua::GuiItem::Type::Text)
        textInputRow(item.label, item.textValue, sizeof(item.textValue), id);
    else if (item.type == lua::GuiItem::Type::Divider)
        cardDivider(item.label);
    else
        sliderRow(item.label, &item.intValue, item.minValue, item.maxValue, id, "");
}

// --- script editor: undo/redo + Lua syntax highlighting --------------------------------
//
// The InputTextMultiline renders with INVISIBLE text (alpha 0) and we draw the same layout
// ourselves on top: 1.91.7 multiline does not soft-wrap (one AddText per whole buffer, lines
// advance by FontSize, horizontal chunk-scroll = InputTextState::Scroll.x, vertical scroll is
// zero because the input is sized to its exact content height inside our own scroll child), so
// per-line token drawing lands pixel-exact on the native layout. The native selection
// background (semi-transparent) still renders above the highlight, and the native caret is
// invisible with the text, so we draw our own.

// Snapshot undo/redo (freestanding statics, no heap): the input runs with
// ImGuiInputTextFlags_NoUndoRedo so ImGui's own per-character undo never fights ours. A
// snapshot is taken on every edit burst (>0.5s gap), so Ctrl+Z rewinds in word-burst steps.
constexpr int kEditorUndoDepth = 16;
constexpr int kEditorRedoDepth = 8;

struct ScriptEditorUndo {
    char undo[kEditorUndoDepth][98 * 1024];
    int undoLengths[kEditorUndoDepth] = {};
    int undoCount = 0;
    char redo[kEditorRedoDepth][98 * 1024];
    int redoLengths[kEditorRedoDepth] = {};
    int redoCount = 0;
    char preEdit[98 * 1024] = {}; // content as it was at the start of this frame
    double lastEditTime = -100.0;
};
ScriptEditorUndo scriptEditorUndo;
int scriptEditorPendingHistory = 0; // 1 = undo, 2 = redo - deferred one frame past ClearActiveID

void scriptEditorPushUndo() noexcept
{
    auto& u = scriptEditorUndo;
    const int length = static_cast<int>(std::strlen(u.preEdit));
    if (u.undoCount == kEditorUndoDepth) {
        std::memmove(u.undo[0], u.undo[1], sizeof(u.undo[0]) * (kEditorUndoDepth - 1));
        std::memmove(u.undoLengths, u.undoLengths + 1, sizeof(int) * (kEditorUndoDepth - 1));
        --u.undoCount;
    }
    std::memcpy(u.undo[u.undoCount], u.preEdit, static_cast<std::size_t>(length) + 1);
    u.undoLengths[u.undoCount] = length;
    ++u.undoCount;
    u.redoCount = 0; // a new edit branch invalidates redo
}

bool scriptEditorApplyHistory(ScriptEditor& editor, bool undoDir) noexcept
{
    auto& u = scriptEditorUndo;
    char (*srcStack)[98 * 1024];
    int* srcLengths;
    int srcCount;
    char (*dstStack)[98 * 1024];
    int* dstLengths;
    int dstCount;
    int dstDepth;
    if (undoDir) {
        if (u.undoCount == 0)
            return false;
        srcStack = u.undo;
        srcLengths = u.undoLengths;
        srcCount = u.undoCount;
        dstStack = u.redo;
        dstLengths = u.redoLengths;
        dstCount = u.redoCount;
        dstDepth = kEditorRedoDepth;
    } else {
        if (u.redoCount == 0)
            return false;
        srcStack = u.redo;
        srcLengths = u.redoLengths;
        srcCount = u.redoCount;
        dstStack = u.undo;
        dstLengths = u.undoLengths;
        dstCount = u.undoCount;
        dstDepth = kEditorUndoDepth;
    }
    const int length = static_cast<int>(std::strlen(editor.buffer));
    if (dstCount == dstDepth) {
        std::memmove(dstStack[0], dstStack[1], sizeof(dstStack[0]) * (dstDepth - 1));
        std::memmove(dstLengths, dstLengths + 1, sizeof(int) * (dstDepth - 1));
        --dstCount;
    }
    std::memcpy(dstStack[dstCount], editor.buffer, static_cast<std::size_t>(length) + 1);
    dstLengths[dstCount] = length;
    ++dstCount;
    --srcCount;
    std::memcpy(editor.buffer, srcStack[srcCount], static_cast<std::size_t>(srcLengths[srcCount]) + 1);
    // write the (possibly trimmed) counts back - src/dst alias different members, no overlap
    if (undoDir) {
        u.undoCount = srcCount;
        u.redoCount = dstCount;
    } else {
        u.redoCount = srcCount;
        u.undoCount = dstCount;
    }
    return true;
}

int scriptEditorTabCallback(ImGuiInputTextCallbackData* data) noexcept
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
        data->InsertChars(data->CursorPos, "    "); // Tab indents instead of stealing focus
    return 0;
}

int scriptEditorLineCount() noexcept
{
    int count = 1;
    for (const char* p = scriptEditor.buffer; (p = std::strchr(p, '\n')) != nullptr; ++p)
        ++count;
    return count;
}

// Lua tokenizer + renderer for one editor frame. Draws into the scroll child's draw list at
// the input's exact text origin. Colors follow the menu palette.
void drawScriptEditorHighlight(ImDrawList* d, const ImVec2& origin, const ImVec2& clipMin, const ImVec2& clipMax,
    float scrollX, const int* cursorByte) noexcept
{
    constexpr ImU32 kDefault = IM_COL32(207, 209, 218, 255);
    constexpr ImU32 kKeyword = IM_COL32(158, 130, 240, 255);
    constexpr ImU32 kString = IM_COL32(140, 200, 120, 255);
    constexpr ImU32 kNumber = IM_COL32(230, 170, 90, 255);
    constexpr ImU32 kComment = IM_COL32(96, 106, 120, 255);
    constexpr ImU32 kApi = IM_COL32(90, 170, 230, 255);
    static constexpr const char* kKeywords[] = {"and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};
    static constexpr const char* kApis[] = {"client", "renderer", "gui", "entity", "memory", "http", "ffi", "bit", "jit"};

    const auto identStart = [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    const auto identChar = [&identStart](unsigned char c) { return identStart(c) || (c >= '0' && c <= '9'); };
    const auto digitChar = [](unsigned char c) { return c >= '0' && c <= '9'; };
    const auto hexChar = [&digitChar](unsigned char c) { return digitChar(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
    const auto wordIs = [](const char* p, const char* end, const char* word) {
        const std::size_t length = std::strlen(word);
        return static_cast<std::size_t>(end - p) == length && std::memcmp(p, word, length) == 0;
    };

    ImFont* font = ImGui::GetIO().Fonts->Fonts[0];
    const float fontSize = font->FontSize;
    const double time = ImGui::GetTime();
    const bool caretVisible = std::fmod(time, 1.2) < 0.8; // matches ConfigInputTextCursorBlink cadence

    d->PushClipRect(clipMin, clipMax, true);
    int lineIndex = 0;
    int lineStartOffset = 0;
    bool inBlockComment = false;
    float caretX = 0.0f;
    float caretY = 0.0f;
    bool caretFound = false;
    for (const char* lineStart = scriptEditor.buffer;;) {
        const char* newline = std::strchr(lineStart, '\n');
        const char* lineEnd = newline ? newline : lineStart + std::strlen(lineStart);
        const int lineEndOffset = lineStartOffset + static_cast<int>(lineEnd - lineStart);
        const float y = origin.y + lineIndex * fontSize;
        if (y > clipMax.y)
            break;
        if (y + fontSize >= clipMin.y) {
            float x = origin.x - scrollX;
            const char* p = lineStart;
            while (p < lineEnd) {
                const char* runStart = p;
                const char* runEnd = lineEnd;
                ImU32 color = kDefault;
                if (inBlockComment) {
                    color = kComment;
                    const char* c = p;
                    while (c + 1 < lineEnd && !(c[0] == ']' && c[1] == ']'))
                        ++c;
                    if (c + 1 < lineEnd && c[0] == ']' && c[1] == ']') {
                        runEnd = c + 2;
                        inBlockComment = false;
                    }
                } else if (p + 1 < lineEnd && p[0] == '-' && p[1] == '-') {
                    color = kComment;
                    if (p + 3 < lineEnd && p[2] == '[' && p[3] == '[')
                        inBlockComment = true; // --[[ opens a block; the rest of the line is comment
                } else if (*p == '"' || *p == '\'') {
                    color = kString;
                    const char quote = *p;
                    const char* c = p + 1;
                    while (c < lineEnd) {
                        if (*c == '\\' && c + 1 < lineEnd)
                            c += 2;
                        else if (*c == quote) {
                            ++c;
                            break;
                        } else
                            ++c;
                    }
                    runEnd = c < lineEnd ? c : lineEnd;
                } else if (digitChar(static_cast<unsigned char>(*p))
                    || (*p == '.' && p + 1 < lineEnd && digitChar(static_cast<unsigned char>(p[1])))) {
                    color = kNumber;
                    const char* c = p;
                    if (c[0] == '0' && c + 1 < lineEnd && (c[1] == 'x' || c[1] == 'X')) {
                        c += 2;
                        while (c < lineEnd && hexChar(static_cast<unsigned char>(*c)))
                            ++c;
                    } else {
                        while (c < lineEnd && (digitChar(static_cast<unsigned char>(*c)) || *c == '.'))
                            ++c;
                    }
                    runEnd = c;
                } else if (identStart(static_cast<unsigned char>(*p))) {
                    const char* c = p;
                    while (c < lineEnd && identChar(static_cast<unsigned char>(*c)))
                        ++c;
                    for (const char* keyword : kKeywords)
                        if (wordIs(p, c, keyword)) { color = kKeyword; break; }
                    if (color == kDefault)
                        for (const char* api : kApis)
                            if (wordIs(p, c, api)) { color = kApi; break; }
                    runEnd = c;
                } else {
                    // punctuation / whitespace run
                    const char* c = p;
                    while (c < lineEnd && !identStart(static_cast<unsigned char>(*c)) && !digitChar(static_cast<unsigned char>(*c))
                        && *c != '"' && *c != '\'' && !(c + 1 < lineEnd && c[0] == '-' && c[1] == '-'))
                        ++c;
                    runEnd = c > p ? c : p + 1;
                }
                bool hasVisible = false;
                for (const char* c = runStart; c < runEnd; ++c)
                    if (*c != ' ' && *c != '\t') { hasVisible = true; break; }
                if (hasVisible)
                    d->AddText(font, fontSize, ImVec2(x, y), color, runStart, runEnd);
                x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, runStart, runEnd).x;
                p = runEnd;
            }
            // caret column (the native one is invisible along with the ghost text)
            if (cursorByte && !caretFound && *cursorByte >= lineStartOffset && *cursorByte <= lineEndOffset) {
                const int cursorInLine = (*cursorByte - lineStartOffset < lineEnd - lineStart) ? *cursorByte - lineStartOffset : static_cast<int>(lineEnd - lineStart);
                caretX = origin.x - scrollX + font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, lineStart, lineStart + cursorInLine).x;
                caretY = y;
                caretFound = true;
            }
        }
        if (!newline)
            break;
        lineStart = newline + 1;
        lineStartOffset = lineEndOffset + 1;
        ++lineIndex;
    }
    if (cursorByte && caretFound && caretVisible)
        d->AddRectFilled(ImVec2(caretX, caretY + 1.0f), ImVec2(caretX + 1.5f, caretY + fontSize - 1.0f), IM_COL32(226, 228, 235, 220));
    d->PopClipRect();
}

void drawScriptEditorWindow() noexcept
{
    if (!scriptEditor.open || !GUI::isMenuOpen())
        return;

    ImGui::SetNextWindowSize(ImVec2(860.0f * menuScale, 540.0f * menuScale), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s(12), s(10)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kInsetBg);
    ImGui::PushStyleColor(ImGuiCol_Border, C(30, 30, 33, 220));
    const ImGuiWindowFlags editorFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar; // the input scrolls its own content

    // The window id IS the title string: it must never change between frames or ImGui treats
    // this as a brand-new window - position resets to the default and keyboard focus (the
    // InputText) dies. That was exactly the "backspace moves the window and drops the caret"
    // bug when the dirty asterisk was part of the title; the marker renders in the caption
    // row below instead.
    char title[160];
    std::snprintf(title, sizeof(title), "%s##script_editor", scriptEditor.name);
    if (ImGui::Begin(title, &scriptEditor.open, editorFlags)) {
        ImDrawList* d = ImGui::GetWindowDrawList();

        // glow: same nested rounded-ring pass as the shell (drawMenuGlow). It MUST render on the
        // foreground draw list: the window's own draw list clips at the window rect, which made
        // the in-window version invisible (only the shell's close-animation warp ever leaked
        // vertices back inside the clip).
        if (ui_config::get<MenuGlowEnabled>()) {
            const auto glowColor = ui_config::get<MenuGlowColor>();
            const float glowSize = static_cast<float>(ui_config::get<MenuGlowSize>());
            float r = glowColor.r() / 255.0f;
            float g = glowColor.g() / 255.0f;
            float b = glowColor.b() / 255.0f;
            if (ui_config::get<MenuGlowRainbow>()) {
                float hue = std::fmod(static_cast<float>(ImGui::GetTime()) * ui_config::get<MenuGlowSpeed>() * 0.1f, 1.0f);
                ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, r, g, b);
            }
            const int aC = static_cast<int>(glowColor.a());
            ImDrawList* glowList = ImGui::GetForegroundDrawList();
            const ImVec2 gMin = ImGui::GetWindowPos();
            const ImVec2 gMax = gMin + ImGui::GetWindowSize();
            const int rings = ImClamp(static_cast<int>(glowSize / 2.5f), 8, 24);
            const float thickness = glowSize / rings + 2.0f;
            for (int i = rings; i >= 1; --i) {
                const float outer = glowSize * static_cast<float>(i) / static_cast<float>(rings);
                const float inner = glowSize * static_cast<float>(i - 1) / static_cast<float>(rings);
                const float offset = (outer + inner) * 0.5f - 0.5f;
                const float fade = 1.0f - static_cast<float>(i) / static_cast<float>(rings);
                const float ringAlpha = static_cast<float>(aC) * (fade * fade * (3.0f - 2.0f * fade));
                if (ringAlpha < 1.0f)
                    continue;
                glowList->AddRect(ImVec2(gMin.x - offset, gMin.y - offset), ImVec2(gMax.x + offset, gMax.y + offset),
                    IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255), static_cast<int>(ringAlpha)),
                    s(10.0f) + offset, 0, thickness + 1.0f);
            }
        }

        // caption: scripts folder ... + dirty/save status on the right
        const bool showStatus = scriptEditor.status[0] != '\0' && ImGui::GetTime() - scriptEditor.statusTime < 3.0;
        const char* stateText = showStatus ? scriptEditor.status : (scriptEditor.dirty ? "unsaved changes" : "");
        if (stateText[0]) {
            const float stateWidth = ImGui::GetFont()->CalcTextSizeA(kTextCaption, FLT_MAX, 0.0f, stateText).x;
            textY(d, ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - s(14) - stateWidth, ImGui::GetWindowPos().y + s(2), s(16),
                scriptEditor.dirty ? C(232, 180, 96) : C(140, 200, 120), stateText, kTextCaption, nullptr);
        }

        // editor body: the input lives in OUR scroll child (content-sized input -> its internal
        // scrolling stays at zero, so the highlight overlay always knows the exact offsets).
        const ImVec2 viewSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - s(34));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, C(19, 19, 21));
        ImGui::BeginChild("##editor_view", viewSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
        ImDrawList* vd = ImGui::GetWindowDrawList();
        const ImVec2 vMin = ImGui::GetWindowPos();
        const ImVec2 vMax = vMin + viewSize;
        vd->AddRectFilled(vMin, vMax, C(19, 19, 21), s(4));

        ImFont* editorFont = ImGui::GetIO().Fonts->Fonts[0];
        const float lineHeight = editorFont->FontSize;
        const int lineCount = scriptEditorLineCount();
        const float contentHeight = (lineCount + 1) * lineHeight + ImGui::GetStyle().FramePadding.y * 2.0f;

        // snapshot the pre-edit content for the undo burst detection
        std::memcpy(scriptEditorUndo.preEdit, scriptEditor.buffer, std::strlen(scriptEditor.buffer) + 1);

        ImGui::PushStyleColor(ImGuiCol_FrameBg, C(19, 19, 21, 0)); // input bg invisible, ours above
        ImGui::PushStyleColor(ImGuiCol_Text, C(207, 209, 218, 0)); // ghost text: the highlight draws it
        ImGui::PushFont(editorFont);
        const bool edited = ImGui::InputTextMultiline("##script_source", scriptEditor.buffer, sizeof(scriptEditor.buffer),
            ImVec2(ImGui::GetContentRegionAvail().x, contentHeight),
            ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_NoUndoRedo, scriptEditorTabCallback);
        const ImGuiID inputId = ImGui::GetItemID();
        const bool inputActive = ImGui::IsItemActive();
        ImGui::PopFont();
        ImGui::PopStyleColor(2);

        if (edited) {
            scriptEditor.dirty = true;
            const double now = ImGui::GetTime();
            if (now - scriptEditorUndo.lastEditTime > 0.5) {
                scriptEditorPushUndo(); // burst start: snapshot the pre-edit content
            }
            scriptEditorUndo.lastEditTime = now;
        }

        // undo/redo: keyboard (input inactive -> apply now, active -> deactivate and apply on
        // the next frame, after InputText's own deactivated-state reapply has settled)
        const bool editorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        bool wantUndo = editorFocused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false);
        bool wantRedo = editorFocused && ImGui::GetIO().KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (ImGui::IsKeyPressed(ImGuiKey_Z, false) && ImGui::GetIO().KeyShift));
        if (scriptEditorPendingHistory == 1)
            wantUndo = true;
        if (scriptEditorPendingHistory == 2)
            wantRedo = true;
        scriptEditorPendingHistory = 0;
        if (wantUndo || wantRedo) {
            if (inputActive) {
                ImGui::ClearActiveID();
                scriptEditorPendingHistory = wantUndo ? 1 : 2;
            } else if (scriptEditorApplyHistory(scriptEditor, wantUndo)) {
                scriptEditor.dirty = true;
                std::snprintf(scriptEditor.status, sizeof(scriptEditor.status), wantUndo ? "undo" : "redo");
                scriptEditor.statusTime = ImGui::GetTime();
            }
        }

        // syntax highlight overlay: exact native layout (origin + per-line advance + Scroll.x)
        {
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImGuiInputTextState* inputState = ImGui::GetInputTextState(inputId);
            const float scrollX = inputState ? inputState->Scroll.x : 0.0f;
            int cursorByte = inputState ? inputState->GetCursorPos() : -1;
            drawScriptEditorHighlight(vd, ImVec2(itemMin.x + ImGui::GetStyle().FramePadding.x, itemMin.y + ImGui::GetStyle().FramePadding.y),
                vMin, vMax, scrollX, inputState ? &cursorByte : nullptr);

            // Caret-follow scroll: the input is content-sized inside THIS child, so nothing else
            // ever scrolls it toward the caret. Without this, pressing Enter near the bottom
            // (or Backspace at the top while scrolled) moves the caret line out of the visible
            // area - typing keeps working but everything renders outside the clip, which felt
            // like the editor "dying" until the next click re-anchored the view.
            if (inputActive && inputState && cursorByte >= 0) {
                int caretLine = 0;
                for (int i = 0; i < cursorByte && scriptEditor.buffer[i] != '\0'; ++i)
                    caretLine += scriptEditor.buffer[i] == '\n' ? 1 : 0;
                const float caretY = static_cast<float>(caretLine) * lineHeight + ImGui::GetStyle().FramePadding.y;
                const float viewH = vMax.y - vMin.y;
                const float scrollY = ImGui::GetScrollY();
                if (caretY < scrollY + lineHeight)
                    ImGui::SetScrollY(ImMax(0.0f, caretY - viewH * 0.35f));
                else if (caretY > scrollY + viewH - 2.0f * lineHeight)
                    ImGui::SetScrollY(ImClamp(caretY - viewH + 2.5f * lineHeight, 0.0f, ImGui::GetScrollMaxY()));
            }
        }

        // scroll thumb
        const float scrollMaxY = ImGui::GetScrollMaxY();
        if (scrollMaxY > 0.0f) {
            const float viewH = vMax.y - vMin.y;
            const float thumbH = ImMax(s(24.0f), viewH * viewH / (viewH + scrollMaxY));
            const float thumbY = vMin.y + (viewH - thumbH) * (ImGui::GetScrollY() / scrollMaxY);
            vd->AddRectFilled(ImVec2(vMax.x - s(4), thumbY), ImVec2(vMax.x - s(2), thumbY + thumbH), C(60, 62, 70, 180), s(2));
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        // action row: UNDO + REDO (left), SAVE + REVERT + RUN (right)
        const float y = ImGui::GetCursorScreenPos().y + s(4);
        const float saveWidth = s(64);
        const float revertWidth = s(96);
        const float runWidth = s(56);
        const float undoWidth = s(64);
        const float redoWidth = s(64);
        const float rowLeft = ImGui::GetWindowPos().x + s(12);
        const float rowRight = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - s(12);

        auto editorButton = [&](int id, const char* label, float x, float width) {
            ImGui::PushID(id);
            const bool clicked = hit("##editor_btn", ImVec2(x, y), ImVec2(width, s(25)));
            const float hover = motion(animKey(0x5eedu, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
            ImGui::PopID();
            d->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + s(25)), mix(kPillBg, kPillBgHover, hover), s(7));
            d->AddRect(ImVec2(x, y), ImVec2(x + width, y + s(25)), mix(kHairlineSoft, g_accent, hover), s(7));
            const float labelWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, label).x;
            textY(d, x + (width - labelWidth) * 0.5f, y, s(25), C(170, 173, 184), label, kTextControl, nullptr);
            return clicked;
        };

        auto saveEditor = [&]() {
            const std::size_t length = std::strlen(scriptEditor.buffer);
            if (lua::writeScript(scriptEditor.name, scriptEditor.buffer, length)) {
                scriptEditor.dirty = false;
                std::snprintf(scriptEditor.status, sizeof(scriptEditor.status), "saved");
                scriptEditor.statusTime = ImGui::GetTime();
                if (lua::loadedIndex(scriptEditor.name) >= 0)
                    lua::load(scriptEditor.name); // live-reload: keep a loaded script in sync
            } else {
                std::snprintf(scriptEditor.status, sizeof(scriptEditor.status), "save failed");
                scriptEditor.statusTime = ImGui::GetTime();
            }
        };

        if (editorButton(4, "UNDO", rowLeft, undoWidth))
            scriptEditorPendingHistory = 1;
        if (editorButton(5, "REDO", rowLeft + undoWidth + s(6), redoWidth))
            scriptEditorPendingHistory = 2;

        if ((editorButton(1, "SAVE", rowRight - saveWidth, saveWidth) && scriptEditor.dirty)
            || (editorFocused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)))
            saveEditor();
        if (editorButton(2, "REVERT", rowRight - saveWidth - s(6) - revertWidth, revertWidth)) {
            long size = 0;
            if (lua::readScript(scriptEditor.name, scriptEditor.buffer, sizeof(scriptEditor.buffer), &size)) {
                scriptEditor.dirty = false;
                std::snprintf(scriptEditor.status, sizeof(scriptEditor.status), "reloaded from disk");
                scriptEditor.statusTime = ImGui::GetTime();
            }
        }
        if (editorButton(3, "RUN", rowRight - saveWidth - s(6) - revertWidth - s(6) - runWidth, runWidth))
            lua::load(scriptEditor.name);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

// --- Scripts page sub-tabs ---------------------------------------------------------------
// Tab 0 = MANAGE (the script file list). Every loaded script that created gui.* items gets its
// own sub-tab (named by gui.tab(), default the file name), so one feature-packed lua organizes
// all of its controls on a dedicated tab that lives INSIDE the Scripts page - between Scripts
// and Misc in the nav rail, exactly where the page already sits.
constexpr int kMaxScriptTabs = 12; // MANAGE + up to 11 script tabs
static int scriptsSubTab = 0;
static char scriptsTabLabels[kMaxScriptTabs][48];
static const char* scriptsTabLabelPtrs[kMaxScriptTabs];
static int scriptsTabCount = 0;

void buildScriptTabs() noexcept
{
    scriptsTabCount = 0;
    std::snprintf(scriptsTabLabels[0], sizeof(scriptsTabLabels[0]), "MANAGE");
    scriptsTabLabelPtrs[0] = scriptsTabLabels[0];
    scriptsTabCount = 1;

    for (int i = 0; i < lua::kMaxScripts && scriptsTabCount < kMaxScriptTabs; ++i) {
        const lua::Script& script = lua::scripts[i];
        if (!script.L)
            continue;
        // a script whose items ALL live on native pages (gui.page) needs no sub-tab here
        bool hasSubtabItems = false;
        for (int k = 0; k < script.guiItemCount && !hasSubtabItems; ++k)
            hasSubtabItems = script.guiItems[k].page == static_cast<int>(lua::ScriptPage::Subtab);
        if (!hasSubtabItems)
            continue;
        // label: gui.tab() wins, else the file name without ".lua", capped
        if (script.tabLabel[0] != '\0')
            std::snprintf(scriptsTabLabels[scriptsTabCount], sizeof(scriptsTabLabels[0]), "%s", script.tabLabel);
        else {
            const std::size_t length = std::strlen(script.name);
            const std::size_t stem = length > 4 ? length - 4 : 0;
            std::size_t copy = stem < sizeof(scriptsTabLabels[0]) - 1 ? stem : sizeof(scriptsTabLabels[0]) - 1;
            std::memcpy(scriptsTabLabels[scriptsTabCount], script.name, copy);
            scriptsTabLabels[scriptsTabCount][copy] = '\0';
        }
        // upper-case the label like the other sub-tab pills (nav style consistency)
        for (char* c = scriptsTabLabels[scriptsTabCount]; *c != '\0'; ++c)
            *c = (*c >= 'a' && *c <= 'z') ? static_cast<char>(*c - 'a' + 'A') : *c;
        scriptsTabLabelPtrs[scriptsTabCount] = scriptsTabLabels[scriptsTabCount];
        ++scriptsTabCount;
    }

    if (scriptsSubTab >= scriptsTabCount)
        scriptsSubTab = 0; // a script unloaded out from under the active tab
}

// The controls card of the listPosition-th (0-based) script that owns gui items. Rendered only
// on that script's sub-tab.
void pageScriptControls(int listPosition) noexcept
{
    lua::Script* script = nullptr;
    int scriptSlot = -1;
    int seen = 0;
    for (int i = 0; i < lua::kMaxScripts; ++i) {
        if (lua::scripts[i].L && lua::scripts[i].guiItemCount > 0) {
            // only items that still live on the script's own sub-tab count here - items
            // assigned to a native page via gui.page() render there instead
            bool hasSubtabItems = false;
            for (int k = 0; k < lua::scripts[i].guiItemCount && !hasSubtabItems; ++k)
                hasSubtabItems = lua::scripts[i].guiItems[k].page == static_cast<int>(lua::ScriptPage::Subtab);
            if (!hasSubtabItems)
                continue;
            if (seen == listPosition) {
                script = &lua::scripts[i];
                scriptSlot = i;
                break;
            }
            ++seen;
        }
    }
    if (!script)
        return;

    // rows: sub-tab items only (+ the error row when the script is disabled)
    int rowCount = script->errored ? 1 : 0;
    for (int i = 0; i < script->guiItemCount; ++i)
        if (script->guiItems[i].page == static_cast<int>(lua::ScriptPage::Subtab))
            ++rowCount;

    const float width = kShellWidth - kSidebarWidth - s(13.0f);
    const float height = (1 + rowCount) * kRowHeight + s(12.0f);
    const float x = shellBase.x + kSidebarWidth + s(9.0f);
    const float y = shellBase.y + kToolbarHeight + s(24.0f) + columnYs[0] - scrollOffset;

    ImDrawList* d = ImGui::GetWindowDrawList();
    char caption[96];
    std::snprintf(caption, sizeof(caption), "SCRIPT - %s", script->name);
    text(d, ImVec2(x, y - s(16.0f)), C(89, 94, 106), caption, kTextCaption, nullptr);
    const ImVec2 cp(x, y);
    softShadow(d, cp, cp + ImVec2(width, height), s(14.0f));
    d->AddRectFilled(cp, cp + ImVec2(width, height), kCardBg, s(16.0f));
    d->AddRect(cp, cp + ImVec2(width, height), kHairlineSoft, s(14.0f));
    card = CardContext{cp + ImVec2(0, s(6.0f)), width, 0};

    if (script->errored) {
        // an errored script keeps its gui items but stops running - show why, in place
        textY(d, card.origin.x + s(13), card.origin.y + card.row * kRowHeight, kRowHeight, C(232, 96, 96), script->lastError, kTextSmall, nullptr);
        ++card.row;
    }
    for (int itemIndex = 0; itemIndex < script->guiItemCount; ++itemIndex) {
        lua::GuiItem& item = script->guiItems[itemIndex];
        if (item.page != static_cast<int>(lua::ScriptPage::Subtab))
            continue;
        const int id = kScriptGuiControlBase + scriptSlot * lua::kMaxGuiItems + itemIndex;
        renderScriptGuiItem(*script, scriptSlot, itemIndex, id);
    }

    columnYs[0] += height + s(30.0f);
    columnYs[1] = columnYs[0];
}

// --- script cards on NATIVE pages (gui.page) ----------------------------------------------
// addCard takes captureless fn pointers, so the renderRows callback reads which script/page it
// belongs to from these statics, set right before the addCard call.
static int scriptPageCardScriptSlot = -1;
static int scriptPageCardPage = -1;

void renderScriptPageCardRows() noexcept
{
    lua::Script& script = lua::scripts[scriptPageCardScriptSlot];
    if (script.errored) {
        // an errored script keeps its gui items but stops running - show why, in place
        ImDrawList* rd = ImGui::GetWindowDrawList();
        textY(rd, card.origin.x + s(13), card.origin.y + card.row * kRowHeight, kRowHeight, C(232, 96, 96), script.lastError, kTextSmall, nullptr);
        ++card.row;
    }
    for (int itemIndex = 0; itemIndex < script.guiItemCount; ++itemIndex) {
        lua::GuiItem& item = script.guiItems[itemIndex];
        if (item.page != scriptPageCardPage)
            continue;
        const int id = kScriptGuiControlBase + scriptPageCardScriptSlot * lua::kMaxGuiItems + itemIndex;
        renderScriptGuiItem(script, scriptPageCardScriptSlot, itemIndex, id);
    }
}

// Appends one card per script that owns gui items on `page`, after the page's own content.
// Runs in the REAL page render (interactive) and the search-index ghost pass (labels only -
// beginRow records them, the ghost window's SkipItems makes every hit() inert).
void renderScriptCardsForPage(Page page) noexcept
{
    const int pageId = static_cast<int>(page);
    for (int i = 0; i < lua::kMaxScripts; ++i) {
        lua::Script& script = lua::scripts[i];
        if (!script.L)
            continue;
        int pageItemCount = 0;
        for (int k = 0; k < script.guiItemCount; ++k)
            if (script.guiItems[k].page == pageId)
                ++pageItemCount;
        if (pageItemCount == 0)
            continue;
        const int rowCount = pageItemCount + (script.errored ? 1 : 0); // + the error row

        // card title: gui.tab() label wins, else the file name stem - upper-cased like the
        // native card titles
        char title[64];
        if (script.tabLabel[0] != '\0')
            std::snprintf(title, sizeof(title), "%s", script.tabLabel);
        else {
            const std::size_t length = std::strlen(script.name);
            const std::size_t stem = length > 4 ? length - 4 : 0;
            std::size_t copy = stem < sizeof(title) - 1 ? stem : sizeof(title) - 1;
            std::memcpy(title, script.name, copy);
            title[copy] = '\0';
        }
        for (char* c = title; *c != '\0'; ++c)
            *c = (*c >= 'a' && *c <= 'z') ? static_cast<char>(*c - 'a' + 'A') : *c;

        scriptPageCardScriptSlot = i;
        scriptPageCardPage = pageId;
        addCard(title, rowCount, &renderScriptPageCardRows);
    }
}

void pageScripts() noexcept
{
    // Snapshot the directory listing once per frame (cheap: opendir on a small dir).
    constexpr int kMaxList = 32;
    lua::FileEntry entries[kMaxList];
    const int fileCount = lua::listFiles(entries, kMaxList);

    // Tab selection lives in the NAV RAIL now (the Scripts row expands into its children, like
    // the Visuals group) - buildScriptTabs() runs there each frame. No pills on the page.
    buildScriptTabs();
    searchGlowSubTab = scriptsSubTab; // global search rows record their scripts subtab

    if (scriptsSubTab > 0) {
        pageScriptControls(scriptsSubTab - 1);
        return;
    }

    // --- MANAGE: the script file card ---
    int loadedCount = 0;
    for (int i = 0; i < lua::kMaxScripts; ++i)
        loadedCount += lua::scripts[i].L ? 1 : 0;

    const int listRows = fileCount > 0 ? fileCount : 1;
    constexpr int kFixedRows = 3; // header / create / hint
    const float width = kShellWidth - kSidebarWidth - s(13.0f);
    const float height = (kFixedRows + listRows) * kRowHeight + s(12.0f);

    const float x = shellBase.x + kSidebarWidth + s(9.0f);
    const float y = shellBase.y + kToolbarHeight + s(24.0f) + columnYs[0] - scrollOffset;

    ImDrawList* d = ImGui::GetWindowDrawList();
    text(d, ImVec2(x, y - s(16.0f)), C(89, 94, 106), "LUA SCRIPTS", kTextCaption, nullptr);
    const ImVec2 p(x, y);
    softShadow(d, p, p + ImVec2(width, height), s(14.0f));
    d->AddRectFilled(p, p + ImVec2(width, height), kCardBg, s(16.0f));
    d->AddRect(p, p + ImVec2(width, height), kHairlineSoft, s(14.0f));

    card = CardContext{p + ImVec2(0, s(6.0f)), width, 0};

    auto pillButton = [&](int id, const char* label, ImVec2 pos, float buttonWidth) {
        ImGui::PushID(id);
        const bool clicked = hit("##pill_btn", pos, ImVec2(buttonWidth, s(23)));
        const float hover = motion(animKey(0x1ea7u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
        ImGui::PopID();
        d->AddRectFilled(pos, pos + ImVec2(buttonWidth, s(23)), mix(kPillBg, kPillBgHover, hover), s(7));
        d->AddRect(pos, pos + ImVec2(buttonWidth, s(23)), mix(kHairlineSoft, g_accent, hover), s(7));
        const float labelWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, label).x;
        textY(d, pos.x + (buttonWidth - labelWidth) * 0.5f, pos.y, s(23), C(170, 173, 184), label, kTextControl, nullptr);
        return clicked;
    };

    // Row 0: header - folder path + loaded count.
    {
        const float rowY = card.origin.y + 0 * kRowHeight;
        textY(d, card.origin.x + s(13), rowY, kRowHeight, C(89, 94, 106), lua::scriptsDirPath[0] ? lua::scriptsDirPath : "(scripts folder unavailable)", kTextCaption, nullptr);
        char loadedLabel[48];
        std::snprintf(loadedLabel, sizeof(loadedLabel), "%d loaded / %d max", loadedCount, lua::kMaxScripts);
        const float rightWidth = ImGui::GetFont()->CalcTextSizeA(kTextCaption, FLT_MAX, 0.0f, loadedLabel).x;
        textY(d, card.origin.x + width - s(13) - rightWidth, rowY, kRowHeight, C(89, 94, 106), loadedLabel, kTextCaption, nullptr);
        ++card.row;
    }

    // Row 1: create-new (name + CREATE button).
    static char newScriptName[96] = "";
    {
        beginRow(d, "New Script");
        const ImVec2 row = card.origin + ImVec2(0, 1 * kRowHeight);
        const float inputWidth = width - s(120) - s(84);
        ImGui::SetCursorScreenPos(ImVec2(row.x + s(105), row.y + rowCentered(s(25))));
        ImGui::PushItemWidth(inputWidth);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kInsetBg);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextBodyCol);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(8), s(5)));
        const bool submitted = ImGui::InputTextWithHint("##script_new", "name.lua (creates a template)", newScriptName, sizeof(newScriptName), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopItemWidth();

        auto nameToFileName = [](const char* raw, char* out, std::size_t outSize) {
            if (std::strstr(newScriptName, ".lua") == newScriptName + std::strlen(newScriptName) - 4)
                std::snprintf(out, outSize, "%s", newScriptName);
            else
                std::snprintf(out, outSize, "%s.lua", newScriptName);
        };

        const bool createClicked = pillButton(++controlId, "CREATE", ImVec2(p.x + width - s(13) - s(70), row.y + rowCentered(s(23))), s(70));
        if ((submitted || createClicked) && newScriptName[0] != '\0' && !searchIndexing) {
            char fileName[128];
            nameToFileName(newScriptName, fileName, sizeof(fileName));
            if (lua::createScript(fileName)) {
                lua::readScript(fileName, scriptEditor.buffer, sizeof(scriptEditor.buffer));
                std::strncpy(scriptEditor.name, fileName, sizeof(scriptEditor.name) - 1);
                scriptEditor.name[sizeof(scriptEditor.name) - 1] = '\0';
                scriptEditor.dirty = false;
                scriptEditor.open = true;
                newScriptName[0] = '\0';
            }
        }
        // beginRow already advanced card.row - the extra increment here used to shift every row
        // below down by one, putting the "Scripts" hint label on top of the first file row.
    }

    // Row 2: hint.
    beginRow(d, "Scripts");

    // Rows 3+: one row per .lua file in the scripts folder.
    for (int i = 0; i < fileCount; ++i) {
        const float rowY = card.origin.y + (3 + i) * kRowHeight;
        d->AddLine(ImVec2(card.origin.x + s(12), rowY), ImVec2(card.origin.x + width - s(12), rowY), kHairline);

        const int loaded = lua::loadedIndex(entries[i].name);
        const lua::Script* scriptState = loaded >= 0 ? &lua::scripts[loaded] : nullptr;

        // buttons at the right edge: EDIT | RELOAD/LOAD | UNLOAD
        const float unloadX = p.x + width - s(13) - s(64);
        const float loadX = unloadX - s(72) - s(6);
        const float editX = loadX - s(52) - s(6);
        const bool unloadClicked = scriptState && pillButton(++controlId, "UNLOAD", ImVec2(unloadX, rowY + rowCentered(s(23))), s(64));
        const bool loadClicked = pillButton(++controlId, scriptState ? "RELOAD" : "LOAD", ImVec2(loadX, rowY + rowCentered(s(23))), s(72));
        const bool editClicked = pillButton(++controlId, "EDIT", ImVec2(editX, rowY + rowCentered(s(23))), s(52));

        if (unloadClicked && !searchIndexing) {
            lua::unloadScript(loaded);
        }
        if (loadClicked && !searchIndexing)
            lua::load(entries[i].name);
        if (editClicked && !searchIndexing) {
            lua::readScript(entries[i].name, scriptEditor.buffer, sizeof(scriptEditor.buffer));
            std::strncpy(scriptEditor.name, entries[i].name, sizeof(scriptEditor.name) - 1);
            scriptEditor.name[sizeof(scriptEditor.name) - 1] = '\0';
            scriptEditor.dirty = false;
            scriptEditor.open = true;
        }

        // file icon + indented name (radio-row style) so the names don't sit flush against the
        // left edge where they read as one blob with the "Scripts" label above them.
        textY(d, card.origin.x + s(16), rowY, kRowHeight, scriptState ? g_accent : C(120, 124, 134), "\xEF\x84\xA1", s(12.0f), iconFont()); // code
        textY(d, card.origin.x + s(38), rowY, kRowHeight, scriptState ? g_accent : kTextBodyCol, entries[i].name, kTextControl, nullptr);
        if (scriptState && scriptState->errored) {
            d->PushClipRect(ImVec2(card.origin.x + s(220), rowY), ImVec2(editX - s(8), rowY + kRowHeight), true);
            textY(d, card.origin.x + s(220), rowY, kRowHeight, C(232, 96, 96), scriptState->lastError, kTextSmall, nullptr);
            d->PopClipRect();
        } else {
            char sizeLabel[32];
            std::snprintf(sizeLabel, sizeof(sizeLabel), "%ld B", entries[i].size);
            const float sizeWidth = ImGui::GetFont()->CalcTextSizeA(kTextCaption, FLT_MAX, 0.0f, sizeLabel).x;
            textY(d, editX - s(10) - sizeWidth, rowY, kRowHeight, C(110, 114, 124), sizeLabel, kTextCaption, nullptr);
        }
        ++card.row;
    }

    if (fileCount == 0) {
        const float rowY = card.origin.y + 3 * kRowHeight;
        textY(d, card.origin.x + s(13), rowY, kRowHeight, C(110, 114, 124), "no scripts yet - create one above", kTextControl, nullptr);
        ++card.row;
    }

    columnYs[0] += height + s(30.0f);
    columnYs[1] = columnYs[0];
}

// --- global search: index build + overlay --------------------------------------------
//
// Ghost-rendering one page per frame into an offscreen window: SkipItems short-circuits every
// ImGui widget (no input side effects), while our own layout code (beginRow) still runs and
// records the rows. The throwaway window's draw list is discarded by ImGui (fully clipped).

bool changePage(Page next) noexcept; // defined in the sidebar section below

void indexNextSearchPage() noexcept
{
    if (searchIndexBuilt || searchIndexPageCursor > static_cast<int>(Page::Misc))
        return;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x + 100.0f, ImGui::GetIO().DisplaySize.y + 100.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kShellWidth, kShellHeight), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, 0);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;

    // NOTE: Begin() returns false for fully-clipped windows - but that is exactly the state we
    // want: SkipItems short-circuits every ImGui widget (no input side effects) while OUR
    // layout code below still runs and records rows. So no `if (Begin())` guard here.
    ImGui::Begin("##search_index", nullptr, flags);
    {
        const ImVec2 savedShellBase = shellBase;
        const float savedScroll = scrollOffset;
        shellBase = ImGui::GetWindowPos();
        scrollOffset = 0.0f;

        searchIndexing = true;
        resetContent();
        switch (static_cast<Page>(searchIndexPageCursor)) {
        case Page::Rage: pageRage(); break;
        case Page::Legit: pageLegit(); break;
        case Page::Movement: pageMovement(); break;
        case Page::PlayerInfo: pagePlayerInfo(); break;
        case Page::Glow: pageGlow(); break;
        case Page::Viewmodel: pageViewmodel(); break;
        case Page::Effects: pageEffects(); break;
        case Page::Hud: pageHud(); break;
        case Page::Sound: pageSound(); break;
        case Page::Inventory: pageInventory(); break;
        case Page::Radio: pageRadio(); break;
        case Page::Scripts: pageScripts(); break;
        case Page::Misc: pageMisc(); break;
        }
        renderScriptCardsForPage(static_cast<Page>(searchIndexPageCursor)); // labels enter the search index too
        searchIndexing = false;

        shellBase = savedShellBase;
        scrollOffset = savedScroll;
        ++searchIndexPageCursor;
        if (searchIndexPageCursor > static_cast<int>(Page::Misc))
            searchIndexBuilt = true;
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void searchOverlay(ImDrawList* d, ImVec2 b) noexcept
{
    if (!searchOpen)
        return;

    const ImVec2 contentMin(b.x + kSidebarWidth + s(2), b.y + kToolbarHeight + s(24));
    const ImVec2 contentMax(b.x + kShellWidth - s(4), b.y + kShellHeight - s(6));
    d->AddRectFilled(contentMin, contentMax, kShellBg, s(14));

    // input field (autofocused on the open frame)
    ImGui::SetCursorScreenPos(contentMin + ImVec2(s(16), s(16)));
    ImGui::PushItemWidth(contentMax.x - contentMin.x - s(32));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kInsetBg);
    ImGui::PushStyleColor(ImGuiCol_Border, g_accent);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(8), s(6)));
    if (ImGui::GetFrameCount() == searchOpenedFrame)
        ImGui::SetKeyboardFocusHere(0);
    ImGui::InputTextWithHint("##search_query", "search settings...", searchQuery, sizeof(searchQuery));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopItemWidth();

    // Input diagnostics: the field once filled with '?' - log the raw UTF-8 bytes we receive
    // on change, so a layout/encoding problem is visible in the log instead of guessable only
    // from rendered glyphs. (SDL text input is deliberately NOT used by this backend - text
    // comes from scancode synthesis, see SdlImGuiBackend.h - so there is no SDL state to probe.)
    static char lastLoggedQuery[sizeof(searchQuery)] = "";
    if (std::strcmp(searchQuery, lastLoggedQuery) != 0) {
        std::memcpy(lastLoggedQuery, searchQuery, sizeof(lastLoggedQuery));
        char hex[3 * sizeof(searchQuery)] = "";
        std::size_t hexIndex = 0;
        for (std::size_t i = 0; i < sizeof(searchQuery) && searchQuery[i]; ++i)
            hexIndex += static_cast<std::size_t>(std::snprintf(hex + hexIndex, sizeof(hex) - hexIndex, "%02X ", static_cast<unsigned char>(searchQuery[i])));
        gui_log::write("search query: [%s] bytes: %s", searchQuery, hex);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(b, b + ImVec2(kSidebarWidth, kShellHeight)) && ImGui::GetFrameCount() > searchOpenedFrame))
        searchOpen = false;

    if (!searchQuery[0]) {
        textY(d, contentMin.x + s(16), contentMin.y + s(58), s(24), C(110, 114, 124), "type to search across every page", kTextSmall, nullptr);
        return;
    }

    // results: collect matches first so keyboard selection has a stable list
    const float rowHeight = s(30.0f);
    const float listTop = contentMin.y + s(56.0f);
    constexpr int kMaxShown = 9;
    int matches[kMaxShown];
    int matchCount = 0;
    for (int i = 0; i < searchIndexCount && matchCount < kMaxShown; ++i) {
        if (searchMatches(searchIndex[i].label, searchQuery))
            matches[matchCount++] = i;
    }

    // reset the selection when the query changes
    static char lastSelectedQuery[sizeof(searchQuery)] = "";
    if (std::strcmp(searchQuery, lastSelectedQuery) != 0) {
        std::memcpy(lastSelectedQuery, searchQuery, sizeof(lastSelectedQuery));
        searchSelected = 0;
    }
    if (searchSelected >= matchCount)
        searchSelected = matchCount - 1;
    if (searchSelected < 0)
        searchSelected = 0;
    if (matchCount > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
            searchSelected = (searchSelected + 1) % matchCount;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
            searchSelected = (searchSelected + matchCount - 1) % matchCount;
    }

    auto activateSearchMatch = [&](int matchIndex) {
        const SearchEntry& entry = searchIndex[matches[matchIndex]];
        const bool visualsPage = entry.page >= Page::PlayerInfo && entry.page <= Page::Sound;
        if (entry.page == Page::Glow)
            glowSubTab = entry.subTab;
        else if (entry.page == Page::Inventory)
            inventoryCategory = entry.subTab; // subTab doubles as the category tag
        else if (entry.page == Page::Rage)
            rageSubTab = entry.subTab;
        else if (entry.page == Page::Legit)
            legitSubTab = entry.subTab;
        else if (entry.page == Page::Misc)
            miscSubTab = entry.subTab;
        else if (entry.page == Page::Scripts) {
            scriptsSubTab = entry.subTab;
            state.scriptsExpanded = true;
        }
        changePage(entry.page);
        if (visualsPage)
            state.visualsExpanded = true;
        scrollOffset = ImMax(0.0f, entry.contentY - s(60.0f));
        scrollTarget = scrollOffset; // search jumps snap, no glide
        searchOpen = false;
    };

    for (int shown = 0; shown < matchCount; ++shown) {
        const int i = matches[shown];
        const bool selected = shown == searchSelected;

        const ImVec2 rp(contentMin.x + s(12), listTop + shown * rowHeight);
        ImGui::PushID(7000 + i);
        const bool clicked = hit("##search_row", rp, ImVec2(contentMax.x - contentMin.x - s(24), rowHeight));
        const float hover = motion(animKey(0x9e21u, 7000 + i), (selected || ImGui::IsItemHovered()) ? 1.0f : 0.0f, 22.0f);
        ImGui::PopID();
        if (hover > 0.001f)
            d->AddRectFilled(rp, rp + ImVec2(contentMax.x - contentMin.x - s(24), rowHeight), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(22 * hover) << IM_COL32_A_SHIFT), s(8));
        if (selected)
            d->AddRectFilled(rp, rp + ImVec2(s(2.5f), rowHeight), g_accent, s(1.5f));

        // label with the matched substring highlighted in the accent color
        const char* label = searchIndex[i].label;
        const char* matchPos = searchMatchPosition(label, searchQuery);
        const float textHeight = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, label).y;
        const float labelY = rp.y + std::floor((rowHeight - textHeight) * 0.5f);
        if (matchPos) {
            const char* matchEnd = matchPos + std::strlen(searchQuery);
            const float preWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, label, matchPos).x;
            const float matchWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, matchPos, matchEnd).x;
            const float labelX = rp.x + s(10);
            d->AddText(ImGui::GetFont(), kTextControl, ImVec2(labelX, labelY), C(150, 154, 165), label, matchPos);
            d->AddText(ImGui::GetFont(), kTextControl, ImVec2(labelX + preWidth, labelY), g_accent, matchPos, matchEnd);
            d->AddText(ImGui::GetFont(), kTextControl, ImVec2(labelX + preWidth + matchWidth, labelY), kTextBodyCol, matchEnd);
        } else {
            textY(d, rp.x + s(10), rp.y, rowHeight, kTextBodyCol, label, kTextControl, nullptr);
        }

        const char* pageName = kPageNames[static_cast<int>(searchIndex[i].page)];
        const float pageNameWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, pageName).x;
        textY(d, rp.x + (contentMax.x - contentMin.x - s(24)) - pageNameWidth - s(10), rp.y, rowHeight, C(110, 114, 124), pageName, kTextSmall, nullptr);

        if (clicked)
            activateSearchMatch(shown);
    }

    if (matchCount == 0)
        textY(d, contentMin.x + s(16), listTop, rowHeight, C(110, 114, 124), "no matches", kTextSmall, nullptr);

    // enter jumps to the selected match (up/down move the selection above)
    if ((ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) && matchCount > 0)
        activateSearchMatch(searchSelected);

    if (searchIndexCount >= kSearchIndexCap && matchCount < 9)
        textY(d, contentMin.x + s(16), contentMax.y - s(20), rowHeight, C(90, 94, 104), "(index full)", kTextCaption, nullptr);
}

float y_nav = 64.0f; // sidebar layout cursor, advanced while drawing the rail

ImFont* loadFontFromMemory(const unsigned char* begin, const unsigned char* end, float sizePixels, const ImWchar* glyphRanges) noexcept
{
    // The objcopy data lives in .rodata and outlives everything; stb only reads it, so the
    // atlas can reference it directly (FontDataOwnedByAtlas=false -> ImGui never frees it).
    const auto sizeBytes = static_cast<std::size_t>(end - begin);
    ImFontConfig config{};
    config.FontData = const_cast<unsigned char*>(begin);
    config.FontDataSize = static_cast<int>(sizeBytes);
    config.FontDataOwnedByAtlas = false;
    config.SizePixels = sizePixels;
    config.GlyphRanges = glyphRanges;
    return ImGui::GetIO().Fonts->AddFont(&config);
}

void openStylePresetPopup(int& preset, ImVec2 anchor, float width) noexcept
{
    styleSelect.open = !styleSelect.open;
    styleSelect.anchor = anchor;
    // Fit the popup to the widest preset label (plus dot and padding) instead of reusing the
    // chip's 72-unit width - the list was cramped and clipped its names.
    float widest = 0.0f;
    for (int i = 0; i < static_cast<int>(sizeof(kStylePresets) / sizeof(kStylePresets[0])); ++i)
        widest = ImMax(widest, ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, kStylePresets[i].name).x);
    styleSelect.width = ImMax(s(28.0f) + widest + s(14.0f), width);
    styleSelect.openedFrame = ImGui::GetFrameCount();
    styleAnchorMin = anchor;
    styleAnchorMax = anchor + ImVec2(styleSelect.width, s(20.0f));
}

bool changePage(Page next) noexcept
{
    if (state.page == next)
        return false;
    state.page = next;
    scrollOffset = 0.0f;
    scrollTarget = 0.0f;
    state.popup.open = false;
    // re-arm the content slide-in animation
    *ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("##page_mix"), 1.0f) = 0.0f;
    return true;
}

void sidebar(ImDrawList* d, ImVec2 base) noexcept
{
    d->AddRectFilled(base, base + ImVec2(kSidebarWidth, kShellHeight), kSidebarBg, s(17.0f), ImDrawFlags_RoundCornersLeft);
    d->AddRectFilled(base + ImVec2(s(145), 0), base + ImVec2(kSidebarWidth, kShellHeight), kSidebarBg);
    d->AddLine(base + ImVec2(kSidebarWidth, 0), base + ImVec2(kSidebarWidth, kShellHeight), C(30, 33, 43));

    d->AddRectFilled(base + ImVec2(s(15), s(11)), base + ImVec2(s(45), s(43)), C(22, 22, 25), s(7));
    // brand chip: the HQ swirl (same uploaded texture the account chip falls back to), aspect-fit
    // in the s(30)x s(32) square; the "NS" monogram only shows while the texture is uploading.
    if (const ImTextureID chipTex = reinterpret_cast<ImTextureID>(VulkanHook::logo_texture::query())) {
        const float chipW = s(30.0f) * 0.86f;
        const float chipH = chipW * static_cast<float>(logo_asset::kHeight) / static_cast<float>(logo_asset::kWidth);
        d->AddImage(chipTex,
            base + ImVec2(s(15) + (s(30) - chipW) * 0.5f, s(11) + (s(32) - chipH) * 0.5f),
            base + ImVec2(s(15) + (s(30) + chipW) * 0.5f, s(11) + (s(32) + chipH) * 0.5f));
    } else {
        text(d, base + ImVec2(s(21), s(18)), g_accent, "NS", kTextTitle, strongFont());
    }
    {
        NS_STR(brandTitle, "Neversnooze");
        text(d, base + ImVec2(s(53), s(14)), C(228, 230, 236), brandTitle, kTextTitle, strongFont());
    }
    text(d, base + ImVec2(s(53), s(33)), kTextFaint, "Counter-Strike 2", s(8));
    // build/status chip: pulsing green dot + compile date
    {
        const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(ImGui::GetTime()) * 2.4f);
        d->AddCircleFilled(base + ImVec2(s(21), s(50)), s(4.2f), C(84, 214, 120, static_cast<int>(26 * pulse)));
        d->AddCircleFilled(base + ImVec2(s(21), s(50)), s(2.2f), C(84, 214, 120, static_cast<int>(110 + 110 * pulse)));
        text(d, base + ImVec2(s(29), s(46)), kTextFaint, "build " __DATE__, s(9), nullptr);
    }
    d->AddLine(base + ImVec2(s(10), s(56)), base + ImVec2(s(147), s(56)), kHairline);

    int id = 100;

    auto eyebrow = [&](const char* label) {
        text(d, base + ImVec2(s(16), y_nav), kTextFaint, label, kTextCaption, nullptr);
        y_nav += s(20.0f);
    };

    auto nav = [&](const char* icon, const char* label, Page target, float indent = 0.0f) {
        const ImVec2 p = base + ImVec2(s(7.0f + indent), y_nav);
        ImGui::PushID(++id);
        const bool clicked = hit("##nav", p, ImVec2(s(140.0f - indent), s(30.0f)));
        const bool selected = state.page == target;
        // The background box follows the MOUSE only; the selected page is marked by the accent
        // rail + accent text. (A full background on the selected row made non-hovered nav rows
        // read as "hovered without the mouse".) The explicit rect check hardens against a stuck
        // ActiveId/hover reporting a row as hovered while the mouse sits anywhere else.
        const bool hovered = ImGui::IsItemHovered() && ImGui::IsMouseHoveringRect(p, p + ImVec2(s(140.0f - indent), s(30.0f)));
        // Slot keys must be STABLE per row (see the animKey note above): the ++id counter is
        // positional and shifts whenever a collapsible sub-list below/above changes size, which
        // made rows inherit the frozen hover/selection slots of rows that disappeared (ghost
        // accent bar on Misc, hover box on Radio after collapsing the Visuals sub-list).
        const int slot = 0x3000 + static_cast<int>(target);
        const float r = motion(animKey(0x7771u, slot), (!selected && hovered) ? 0.48f : 0.0f);
        const float sel = motion(animKey(0x7773u, slot), selected ? 1.0f : 0.0f);
        ImGui::PopID();
        if (clicked)
            changePage(target);
        if (r > 0.001f)
            d->AddRectFilled(p, p + ImVec2(s(140.0f - indent), s(30.0f)), mix(C(0, 0, 0, 0), kRowHover, r), s(6));
        // selected-page accent rail on the row's left edge
        if (sel > 0.001f)
            d->AddRectFilled(p, p + ImVec2(s(2.5f), s(30.0f)), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(255 * sel) << IM_COL32_A_SHIFT), s(1.5f));
        textY(d, p.x + s(10), p.y, s(30), mix(C(137, 142, 153), g_accent, ImMax(r, sel)), icon, kTextIcon, iconFont());
        textY(d, p.x + s(31), p.y, s(30), mix(C(145, 149, 159), C(226, 228, 235), ImMax(r, sel * 0.8f)), label, kTextBody, nullptr);
        y_nav += s(32.0f);
    };

    // The expanded Visuals sub-list (8 pages) can outgrow the rail space between the logo and
    // the account bar - clamp the nav content with a clip rect and wheel-scroll it instead of
    // letting Misc slide behind the Neversnooze chip.
    const float expand = motion(ImGui::GetID("##visual_expand"), state.visualsExpanded ? 1.0f : 0.0f, 18.0f);
    const float navTop = base.y + s(58.0f);
    const float navBottom = base.y + kShellHeight - s(45.0f) - s(4.0f);
    float navMaxScroll = 0.0f;
    {
        // Measured from LAST frame's laid-out nav content (navContentEnd, recorded after the
        // items below). The old hand-counted estimate drifted every time a nav item was added
        // (it assumed 2 rows in COMBAT / 3 in OTHER) and could never scroll the last items
        // into view once the adaptive shell shrunk the rail.
        navMaxScroll = navContentEnd > navBottom ? navContentEnd - navBottom : 0.0f;
        if (navMaxScroll > 0.0f && ImGui::IsMouseHoveringRect(ImVec2(base.x, navTop), ImVec2(base.x + kSidebarWidth, navBottom)))
            navScroll -= ImGui::GetIO().MouseWheel * s(24.0f);
        navScroll = ImClamp(navScroll, 0.0f, navMaxScroll);
        if (navMaxScroll <= 0.0f)
            navScroll = 0.0f;
    }

    y_nav = s(64.0f) - navScroll;
    d->PushClipRect(ImVec2(base.x, navTop), ImVec2(base.x + kSidebarWidth, navBottom), true);
    eyebrow("COMBAT");
    nav("\xEF\x81\x9B", "Rage", Page::Rage);   // crosshairs
    nav("\xEF\xA3\x8C", "Legit", Page::Legit); // mouse
    nav("\xEF\x9C\x8C", "Movement", Page::Movement); // person-running
    y_nav += 2.0f;

    eyebrow("FEATURES");
    // The group parent navigates to Player Info and toggles the sub-list.
    {
        const ImVec2 p = base + ImVec2(s(7), y_nav);
        ImGui::PushID(++id);
        const bool clicked = hit("##nav", p, ImVec2(s(140), s(30)));
        const bool visuals = state.page >= Page::PlayerInfo && state.page <= Page::Sound;
        // background = hover only (rect-checked, same hardening as the nav rows); a visuals page
        // being active shows as accent TEXT, not a permanently lit row
        const float r = motion(animKey(0x7772u, 0x3200), (ImGui::IsItemHovered() && ImGui::IsMouseHoveringRect(p, p + ImVec2(s(140), s(30)))) ? 0.48f : 0.0f);
        const float sel = motion(animKey(0x7775u, 0x3200), visuals ? 1.0f : 0.0f);
        ImGui::PopID();
        if (clicked) {
            const bool wasVisuals = visuals;
            state.visualsExpanded = wasVisuals ? !state.visualsExpanded : true;
            changePage(Page::PlayerInfo);
        }
        if (r > 0.001f)
            d->AddRectFilled(p, p + ImVec2(s(140), s(30)), mix(C(0, 0, 0, 0), kRowHover, r), s(6));
        textY(d, p.x + s(10), p.y, s(30), mix(C(137, 142, 153), g_accent, ImMax(r, sel)), "\xEF\x80\xBE", kTextIcon, iconFont()); // image
        textY(d, p.x + s(31), p.y, s(30), mix(C(145, 149, 159), C(226, 228, 235), ImMax(r, sel * 0.8f)), "Visuals", kTextBody, nullptr);
        chevron(d, p + ImVec2(s(state.visualsExpanded ? 126.0f : 131.0f), s(12)), C(150, 154, 165));
        y_nav += s(32.0f);
    }
    if (expand > 0.02f) {
        const int first = d->VtxBuffer.Size;
        const float startY = y_nav;
        nav("\xEF\x80\x87", "Player Info", Page::PlayerInfo, 14);   // user
        nav("\xEF\x81\xAE", "Glow", Page::Glow, 14);                // eye (outline + model glow share the tab)
        nav("\xEF\x80\xBD", "Viewmodel", Page::Viewmodel, 14);      // video
        nav("\xEF\x83\x90", "Effects", Page::Effects, 14);         // magic
        nav("\xEF\x84\x88", "Hud", Page::Hud, 14);                  // desktop
        nav("\xEF\x80\xA8", "Sound", Page::Sound, 14);              // volume-up
        for (int i = first; i < d->VtxBuffer.Size; ++i) {
            ImDrawVert& v = d->VtxBuffer[i];
            const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
            v.col = (v.col & 0xffffffu) | ((ImU32)(a * expand) << IM_COL32_A_SHIFT);
        }
        y_nav = startY + 7 * s(32.0f) * expand;
    }
    y_nav += s(2.0f);

    eyebrow("OTHER");
    nav("\xEF\x9B\x8B", "Inventory", Page::Inventory); // dagger
    nav("\xEF\x94\x99", "Radio", Page::Radio);         // broadcast-tower

    // Scripts = a group parent like Visuals: the row navigates to the Scripts page and toggles
    // the child list - MANAGE + one entry per script tab (gui.tab()). Children set
    // scriptsSubTab directly, so each big lua is its own destination exactly like the Visuals
    // children are.
    {
        // the tab labels are built from the live framework state - refresh before rendering
        buildScriptTabs();

        const ImVec2 p = base + ImVec2(s(7), y_nav);
        ImGui::PushID(++id);
        const bool clicked = hit("##nav", p, ImVec2(s(140), s(30)));
        const bool scripts = state.page == Page::Scripts;
        const float r = motion(animKey(0x7772u, 0x3201), (ImGui::IsItemHovered() && ImGui::IsMouseHoveringRect(p, p + ImVec2(s(140), s(30)))) ? 0.48f : 0.0f);
        const float sel = motion(animKey(0x7775u, 0x3201), scripts ? 1.0f : 0.0f);
        ImGui::PopID();
        if (clicked) {
            const bool wasScripts = scripts;
            state.scriptsExpanded = wasScripts ? !state.scriptsExpanded : true;
            changePage(Page::Scripts);
        }
        if (r > 0.001f)
            d->AddRectFilled(p, p + ImVec2(s(140), s(30)), mix(C(0, 0, 0, 0), kRowHover, r), s(6));
        textY(d, p.x + s(10), p.y, s(30), mix(C(137, 142, 153), g_accent, ImMax(r, sel)), "\xEF\x84\xA1", kTextIcon, iconFont()); // code
        textY(d, p.x + s(31), p.y, s(30), mix(C(145, 149, 159), C(226, 228, 235), ImMax(r, sel * 0.8f)), "Scripts", kTextBody, nullptr);
        chevron(d, p + ImVec2(s(state.scriptsExpanded ? 126.0f : 131.0f), s(12)), C(150, 154, 165));
        y_nav += s(32.0f);
    }
    {
        const float expand = motion(ImGui::GetID("##scripts_expand"), state.scriptsExpanded ? 1.0f : 0.0f, 18.0f);
        if (expand > 0.02f) {
            const int first = d->VtxBuffer.Size;
            const float startY = y_nav;
            auto scriptNav = [&](const char* label, int index) {
                const ImVec2 p = base + ImVec2(s(7.0f + 14.0f), y_nav);
                ImGui::PushID(++id);
                const bool clicked = hit("##nav_script", p, ImVec2(s(126.0f), s(30.0f)));
                const bool selected = state.page == Page::Scripts && scriptsSubTab == index;
                const bool hovered = ImGui::IsItemHovered() && ImGui::IsMouseHoveringRect(p, p + ImVec2(s(126.0f), s(30.0f)));
                const float r = motion(animKey(0x7776u, 0x3300 + index), (!selected && hovered) ? 0.48f : 0.0f);
                const float sel = motion(animKey(0x7777u, 0x3300 + index), selected ? 1.0f : 0.0f);
                ImGui::PopID();
                if (clicked) {
                    scriptsSubTab = index;
                    if (state.page != Page::Scripts)
                        changePage(Page::Scripts);
                }
                if (r > 0.001f)
                    d->AddRectFilled(p, p + ImVec2(s(126.0f), s(30.0f)), mix(C(0, 0, 0, 0), kRowHover, r), s(6));
                if (selected)
                    d->AddRectFilled(p, p + ImVec2(s(2.5f), s(30.0f)), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(255 * sel) << IM_COL32_A_SHIFT), s(1.5f));
                textY(d, p.x + s(10), p.y, s(30), mix(C(145, 149, 159), g_accent, ImMax(r, sel * 0.8f)), label, kTextBody, nullptr);
                y_nav += s(32.0f);
            };
            const int children = ImMin(scriptsTabCount, 8); // rail space guard; the rest stay reachable by scrolling
            for (int i = 0; i < children; ++i)
                scriptNav(scriptsTabLabelPtrs[i], i);
            for (int i = first; i < d->VtxBuffer.Size; ++i) {
                ImDrawVert& v = d->VtxBuffer[i];
                const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
                v.col = (v.col & 0xffffffu) | ((ImU32)(a * expand) << IM_COL32_A_SHIFT);
            }
            y_nav = startY + children * s(32.0f) * expand;
        }
    }

    nav("\xEF\x80\x93", "Misc", Page::Misc);           // cog
    d->PopClipRect();

    // record the true laid-out nav bottom for next frame's rail scroll clamp - see the
    // navMaxScroll block above. y_nav is laid out ALREADY SHIFTED by -navScroll, so the scroll
    // offset must be added back or the measurement shrinks as the user scrolls (the clamp then
    // pulls the rail back up and caps the scroll at half the real overflow).
    navContentEnd = base.y + y_nav + navScroll + s(6.0f);

    // Scroll affordance for the rail: bottom fade + thin thumb whenever the sub-list overflows.
    if (navMaxScroll > 1.0f) {
        const ImU32 bgFade = kSidebarBg;
        const ImU32 clear = bgFade & 0x00FFFFFFu;
        const float fade = s(14.0f);
        d->AddRectFilledMultiColor(ImVec2(base.x, navBottom - fade), ImVec2(base.x + kSidebarWidth, navBottom), clear, clear, bgFade, bgFade);
        const float track = (navBottom - navTop) - s(8.0f);
        const float visible = navBottom - navTop;
        const float content = visible + navMaxScroll;
        const float thumbHeight = ImClamp(visible * visible / content, s(24.0f), track);
        const float t = navScroll / navMaxScroll;
        const float thumbY = navTop + s(4.0f) + t * (track - thumbHeight);
        d->AddRectFilled(ImVec2(base.x + kSidebarWidth - s(5.0f), thumbY), ImVec2(base.x + kSidebarWidth - s(2.0f), thumbY + thumbHeight), C(255, 255, 255, 26), s(2));
    }
}

// --- shell: toolbar (config chip + unload) -------------------------------------------

void toolbar(ImDrawList* d, ImVec2 base) noexcept
{
    // Translucent again (the opaque extension of 2026-09-09 read as a solid slab and broke the
    // shell's rounded top-right corner). Scrolled content is kept out by the CONTENT CLIP
    // instead: contentMin.y now sits at the toolbar hairline, so rows disappear cleanly under
    // the toolbar exactly like they clip at the bottom edge.
    d->AddRectFilled(base + ImVec2(kSidebarWidth, 0), base + ImVec2(kShellWidth, kToolbarHeight), kToolbarBg, s(17.0f), ImDrawFlags_RoundCornersTopRight);
    d->AddLine(base + ImVec2(kSidebarWidth, kToolbarHeight), base + ImVec2(kShellWidth, kToolbarHeight), C(26, 26, 29));

    // Toolbar background doubles as the drag handle. Buttons are excluded so their clicks never
    // start a drag; the drag updates the persistent offset the window is placed at next frame.
    const ImVec2 tbMin = base + ImVec2(kSidebarWidth, 0);
    const ImVec2 tbMax = base + ImVec2(kShellWidth, kToolbarHeight);
    if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(tbMin, tbMax)
        && !ImGui::IsMouseHoveringRect(configChipMin, configChipMax)
        && !ImGui::IsMouseHoveringRect(unloadButtonMin, unloadButtonMax)
        && !ImGui::IsMouseHoveringRect(searchButtonMin, searchButtonMax)) {
        draggingWindow = true;
        dragStartMouse = ImGui::GetIO().MousePos;
        dragStartOffset = menuOffset;
    }
    if (draggingWindow) {
        if (ImGui::IsMouseDown(0)) {
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            const ImVec2 defaultPos((display.x - kShellWidth) * 0.5f, (display.y - shellHeightBase) * 0.5f);
            menuOffset = dragStartOffset + (ImGui::GetIO().MousePos - dragStartMouse);
            // keep the shell on screen
            menuOffset.x = ImClamp(menuOffset.x, -defaultPos.x + s(8), display.x - kShellWidth - defaultPos.x + s(8));
            menuOffset.y = ImClamp(menuOffset.y, -defaultPos.y + s(8), display.y - kShellHeight - defaultPos.y + s(8));
        } else {
            draggingWindow = false;
        }
    }

    // Active-config chip: opens the config popover (right of the scaled sidebar).
    const ImVec2 profile = base + ImVec2(kSidebarWidth + s(11), s(14));
    configChipMin = profile;
    configChipMax = profile + ImVec2(s(180), s(30));
    ImGui::PushID(9000);
    const bool chipClicked = hit("##config_chip", profile, configChipMax - configChipMin);
    const float response = motion(animKey(0x51e7u, 9000), (configPopoverOpen || ImGui::IsItemHovered()) ? 1.0f : 0.0f);
    ImGui::PopID();
    if (chipClicked) {
        configPopoverOpen = !configPopoverOpen;
        configPopoverOpenedFrame = ImGui::GetFrameCount();
    }
    d->AddRectFilled(profile, configChipMax, mix(kInsetBg, kPillBg, response), s(8));
    d->AddRect(profile, configChipMax, mix(kHairline, g_accent, response), s(8));
    textY(d, profile.x + s(12), profile.y, s(30), g_accent, "\xEF\x83\x87", kTextIcon, iconFont()); // save
    textY(d, profile.x + s(34), profile.y, s(30), C(184, 187, 197), ui_config::activeConfigNameForDisplay(), kTextControl, nullptr);
    // unsaved-changes dot: ui_config::set() bumps changeEpoch; the clean epoch is armed by
    // save/switch/restore in the config popover
    if (ui_config::changeEpoch.load(std::memory_order_relaxed) != lastCleanConfigEpoch) {
        const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.2f);
        d->AddCircleFilled(profile + ImVec2(s(170), s(15)), s(2.4f), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(90 + 130 * pulse) << IM_COL32_A_SHIFT));
    }
    chevron(d, profile + ImVec2(s(160), s(11)), C(130, 135, 146));

    // Global search button, left of Unload.
    const ImVec2 searchBtn = base + ImVec2(kShellWidth - s(78) - s(42), s(14));
    const ImVec2 searchSize = ImVec2(s(34), s(30));
    searchButtonMin = searchBtn;
    searchButtonMax = searchBtn + searchSize;
    ImGui::PushID(9002);
    const bool searchClicked = hit("##search", searchBtn, searchSize);
    const float searchHover = motion(animKey(0x5eabu, 9002), (searchOpen || ImGui::IsItemHovered()) ? 1.0f : 0.0f);
    ImGui::PopID();
    if (searchClicked) {
        searchOpen = !searchOpen;
        searchOpenedFrame = ImGui::GetFrameCount();
        if (searchOpen)
            searchQuery[0] = '\0';
    }
    d->AddRectFilled(searchBtn, searchBtn + searchSize, mix(kInsetBg, kPillBg, searchHover), s(8));
    d->AddRect(searchBtn, searchBtn + searchSize, mix(kHairline, g_accent, searchHover), s(8));
    const char searchIcon[] = "\xEF\x80\x82";
    const float searchIconWidth = iconFont()->CalcTextSizeA(kTextIcon, FLT_MAX, 0.0f, searchIcon).x;
    textY(d, searchBtn.x + (searchSize.x - searchIconWidth) * 0.5f, searchBtn.y, searchSize.y, mix(C(170, 173, 184), C(226, 228, 235), searchHover), searchIcon, kTextIcon, iconFont());

    // Unload button, text centered.
    const ImVec2 unload = base + ImVec2(kShellWidth - s(78), s(14));
    const ImVec2 unloadSize = ImVec2(s(64), s(30));
    unloadButtonMin = unload;
    unloadButtonMax = unload + unloadSize;
    ImGui::PushID(9001);
    const bool unloadClicked = hit("##unload", unload, unloadSize);
    const float unloadHover = motion(animKey(0x0ea1u, 9001), ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();
    d->AddRectFilled(unload, unload + unloadSize, mix(C(30, 17, 20), C(120, 34, 40), unloadHover), s(8));
    d->AddRect(unload, unload + unloadSize, mix(C(50, 28, 32), C(190, 60, 66), unloadHover), s(8));
    const char* unloadText = "Unload";
    const float unloadTextWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, unloadText).x;
    textY(d, unload.x + (unloadSize.x - unloadTextWidth) * 0.5f, unload.y, unloadSize.y, mix(C(199, 164, 168), C(245, 214, 216), unloadHover), unloadText, kTextControl, nullptr);
    if (unloadClicked)
        GUI::requestUnload();
}

void accountBar(ImDrawList* d, ImVec2 base) noexcept
{
    const ImVec2 account = base + ImVec2(s(7), kShellHeight - s(45.0f));
    const float barWidth = kSidebarWidth - s(18.0f);
    accountBarMin = account;
    accountBarMax = account + ImVec2(barWidth, s(38));
    ImGui::PushID(8800);
    const bool clicked = hit("##account", account, ImVec2(barWidth, s(38)));
    const float r = motion(animKey(0x1932u, 8800), state.profileOpen ? 1.0f : ImGui::IsItemHovered() ? 0.5f : 0.0f);
    ImGui::PopID();
    if (clicked) {
        state.profileOpen = !state.profileOpen;
        state.profileOpenedFrame = ImGui::GetFrameCount();
    }
    if (r > 0.001f)
        d->AddRectFilled(account, account + ImVec2(barWidth, s(38)), (kRowHover & 0x00FFFFFFu) | (static_cast<ImU32>(235 * r) << IM_COL32_A_SHIFT), s(6));

    // avatar: user image from <config dir>/avatar.png first, then the steam persona fetch
    // (ns_steam_avatar.png in the exchange root from SteamPersona.h), NS monogram until either
    // is staged.
    const ImVec2 avatar = account + ImVec2(s(7), s(5));
    const float avatarRadius = s(14);
    static bool logoStaged = false; // present thread only
    if (!logoStaged) {
        logoStaged = true;
        auto* pixels = static_cast<std::uint8_t*>(std::malloc(logo_asset::kPixelBytes));
        if (pixels) {
            std::memcpy(pixels, logo_asset::kRgba, logo_asset::kPixelBytes);
            VulkanHook::logo_texture::request(pixels, logo_asset::kWidth, logo_asset::kHeight);
        }
    }
    const ImTextureID avatarTex = reinterpret_cast<ImTextureID>(VulkanHook::avatar_texture::query());
    const ImTextureID logoTex = reinterpret_cast<ImTextureID>(VulkanHook::logo_texture::query());
    if (avatarTex) {
        d->AddImageRounded(avatarTex, avatar, avatar + ImVec2(avatarRadius * 2.0f, avatarRadius * 2.0f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), C(255, 255, 255, 255), avatarRadius);
    } else if (logoTex) {
        // the swirl is wider than tall - aspect-fit it inside the circle, slightly inset
        const float drawW = avatarRadius * 2.0f * 0.92f;
        const float drawH = drawW * static_cast<float>(logo_asset::kHeight) / static_cast<float>(logo_asset::kWidth);
        d->AddImage(logoTex, avatar + ImVec2(avatarRadius - drawW * 0.5f, avatarRadius - drawH * 0.5f),
            avatar + ImVec2(avatarRadius + drawW * 0.5f, avatarRadius + drawH * 0.5f));
    } else {
        d->AddCircleFilled(avatar + ImVec2(avatarRadius, avatarRadius), avatarRadius, C(22, 22, 25));
        textY(d, avatar.x + s(5), avatar.y, s(28), g_accent, "NS", kTextControl, strongFont());
    }
    // profile glow: nested accent rings fading out (drawMenuGlow's smoothstep falloff, scaled
    // for the chip) instead of the hard classic circle outline.
    {
        const ImVec2 center = avatar + ImVec2(avatarRadius, avatarRadius);
        constexpr int rings = 7;
        constexpr float glowSize = 7.0f;
        for (int i = rings; i >= 1; --i) {
            const float outer = glowSize * static_cast<float>(i) / static_cast<float>(rings);
            const float inner = glowSize * static_cast<float>(i - 1) / static_cast<float>(rings);
            const float offset = (outer + inner) * 0.5f - 0.5f;
            const float fade = 1.0f - static_cast<float>(i) / static_cast<float>(rings);
            const float alpha = 200.0f * (fade * fade * (3.0f - 2.0f * fade));
            if (alpha < 1.0f)
                continue;
            d->AddCircle(center, avatarRadius + s(offset),
                (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT), 0, glowSize / rings + 1.2f);
        }
        d->AddCircle(center, avatarRadius, g_accent, 0, s(1.2f));
    }
    {
        NS_STR(fallbackPersona, "Neversnooze");
        textY(d, account.x + s(43), account.y, s(38), C(225, 227, 233), steam_persona::name()[0] ? steam_persona::name() : (const char*)fallbackPersona, kTextControl, nullptr);
    }
    chevron(d, account + ImVec2(barWidth - s(9), s(16)), C(181, 185, 195));
}

void profilePopover(ImDrawList* d, ImVec2 base) noexcept
{
    const float open = motion(ImGui::GetID("##profile_open"), state.profileOpen ? 1.0f : 0.0f, 17.0f, 0.0f);
    if (open < 0.002f)
        return;

    const float width = s(210.0f);
    const float rowHeight = s(30.0f);
    // 12 rows (scale label, style, 3 theme colors, 7 glow rows) + the scale slider block
    // (s(24)+s(2)) + s(8) top/bottom pads - the old count assumed 14 rows from a removed
    // "style rainbow / about" pair and left a dead band at the popover's bottom.
    const float height = rowHeight * 12.0f + s(26.0f) + s(16.0f);
    const ImVec2 p = base + ImVec2(s(7.0f), kShellHeight - s(45.0f) - height * open - s(6.0f));
    const ImVec2 size(width, height);
    recordPopupRect(PopupProfile, p, p + size);
    const int first = d->VtxBuffer.Size;
    softShadow(d, p, p + size, s(16.0f));
    d->AddRectFilled(p - ImVec2(s(4), s(2)), p + size + ImVec2(s(4), s(7)), C(0, 0, 0, 65), s(18));
    d->AddRectFilled(p, p + size, kInsetBg, s(16));
    d->AddRect(p, p + size, C(46, 46, 50), s(16));

    constexpr int kScaleEditId = 8803;

    float y = p.y + s(8.0f);

    // Menu Scale: label row (% pill - CLICK TO TYPE an exact value, like the page sliders),
    // slider underneath.
    textY(d, p.x + s(14), y, rowHeight, C(185, 188, 198), "Menu Scale", kTextControl, nullptr);

    // Slider geometry (shared by the interaction below and the drawing further down) and the
    // interaction itself run BEFORE the pill so the preview value can drive the pill label.
    constexpr float sliderRowHeight = 24.0f; // design units
    const float sliderY = y + rowHeight;
    const ImVec2 sliderStart(p.x + s(16.0f), sliderY + (s(sliderRowHeight) - s(6.0f)) * 0.5f);
    const float sliderTrackWidth = width - s(32.0f);

    // The drag only PREVIEWS - menuScale + applyMetrics used to update live here, which
    // rescaled the whole menu (track included) under the cursor mid-drag: the same mouse X
    // then mapped to a bigger t, and a fast 75->100 drag ran away to 200. The mapping below
    // now runs against frozen (drag-start scale) geometry; metrics + fonts commit on release.
    float previewScale = state.scalePreview;
    ImGui::PushID(8801);
    hitPopupRow("##scale", sliderStart - ImVec2(s(4), s(6)), ImVec2(sliderTrackWidth + s(8), s(18)), PopupProfile);
    const bool scaleDragging = ImGui::IsItemActive();
    if (scaleDragging) {
        const float t = ImClamp((ImGui::GetIO().MousePos.x - sliderStart.x) / sliderTrackWidth, 0.0f, 1.0f);
        previewScale = 0.75f + t * 1.25f;
        state.scalePreview = previewScale;
    }
    const bool scaleReleased = !scaleDragging && state.scaleDragging;
    state.scaleDragging = scaleDragging;
    ImGui::PopID();
    if (scaleReleased && previewScale != menuScale) {
        menuScale = previewScale;
        applyMetrics();
        pendingMenuScale = menuScale;
        fontReloadPending = true; // fonts reload outside this frame (processDeferred)
    }

    {
        const ImVec2 pill(p.x + width - s(56.0f), y + rowCentered(s(21.0f)));
        const bool editing = state.editingSlider == kScaleEditId;
        d->AddRectFilled(pill, pill + ImVec2(s(44), s(21)), kPillBg, s(7));

        ImGui::PushID(kScaleEditId);
        if (editing) {
            ImGui::SetCursorScreenPos(pill + ImVec2(s(4), s(3)));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, kHairlineSoft);
            ImGui::PushStyleColor(ImGuiCol_Border, g_accent);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(4), s(2)));
            ImGui::PushItemWidth(s(44) - s(8));
            ImGui::SetKeyboardFocusHere(0); // focus on frame 1
            ImGui::InputText("##scale_edit", state.editBuffer, sizeof(state.editBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool committed = ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
            const bool lostFocus = ImGui::IsItemDeactivatedAfterEdit() || (!ImGui::IsItemActive() && !ImGui::IsItemFocused() && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered());
            ImGui::PopItemWidth();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (committed || lostFocus) {
                pendingMenuScale = ImClamp(std::atoi(state.editBuffer), 75, 200) / 100.0f;
                state.scalePreview = pendingMenuScale;
                fontReloadPending = true; // fonts reload outside this frame (processDeferred)
                state.editingSlider = -1;
            }
        } else {
            char pillText[16];
            std::snprintf(pillText, sizeof(pillText), "%d%%", static_cast<int>(previewScale * 100.0f + 0.5f));
            const float pillTextWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, pillText).x;
            textY(d, pill.x + (s(44) - pillTextWidth) * 0.5f, pill.y, s(21), C(166, 169, 179), pillText, kTextSmall, nullptr);
            if (hitPopupRow("##scale_pill", pill, ImVec2(s(44), s(21)), PopupProfile)) {
                std::snprintf(state.editBuffer, sizeof(state.editBuffer), "%d", static_cast<int>(previewScale * 100.0f + 0.5f));
                state.editingSlider = kScaleEditId;
            }
        }
        ImGui::PopID();
    }
    y += rowHeight;
    {
        // Fill/label preview the drag value; the menu itself keeps the committed scale until
        // release (see the interaction block above).
        // animKey on the control id, never GetItemID: when the RGBA picker (Glow Color row)
        // overlaps this row, hitPopupRow submits a Dummy (item id 0) and the slot would be
        // shared - the 2026-09-07 dropdown-drift class. This was the last GetItemID-keyed site.
        const float shown = motion(animKey(0x5ca1eu, kScaleEditId), (previewScale - 0.75f) / 1.25f, 16.0f, (menuScale - 0.75f) / 1.25f);
        d->AddRectFilled(sliderStart, sliderStart + ImVec2(sliderTrackWidth, s(6)), C(44, 44, 47), s(3));
        d->AddRect(sliderStart, sliderStart + ImVec2(sliderTrackWidth, s(6)), C(66, 66, 72), s(3));
        d->AddRectFilled(sliderStart, sliderStart + ImVec2(sliderTrackWidth * shown, s(6)), g_sliderAccent, s(3));
        d->AddCircleFilled(sliderStart + ImVec2(sliderTrackWidth * shown, s(3.0f)), s(6.0f), C(247, 248, 252));
    }
    y += s(24.0f) + s(2.0f);

    // Style: label + preset chip (opens the accent mini-popup).
    {
        textY(d, p.x + s(14), y, rowHeight, C(185, 188, 198), "Style", kTextControl, nullptr);
        int preset = -1;
        for (int i = 0; i < static_cast<int>(sizeof(kStylePresets) / sizeof(kStylePresets[0])); ++i)
            if (kStylePresets[i].color == g_accent)
                preset = i;
        const ImVec2 sp(p.x + width - s(84.0f), y + rowCentered(s(20.0f)));
        ImGui::PushID(8802);
        if (hitPopupRow("##style", sp, ImVec2(s(72.0f), s(20.0f)), PopupProfile)) {
            state.colorPickerOpen = false;
            state.multiSelectOpen = false;
            openStylePresetPopup(preset, sp, s(72.0f)); // int& param; -1 (custom) is fine, popup highlights index 0
        }
        const float hover = motion(animKey(0x57e1u, 8802), ImGui::IsItemHovered() ? 1.0f : 0.0f);
        ImGui::PopID();
        d->AddRectFilled(sp, sp + ImVec2(s(72.0f), s(20.0f)), mix(kPillBg, kPillBgHover, hover), s(7));
        textY(d, sp.x + s(7), sp.y, s(20.0f), C(170, 173, 184), preset >= 0 ? kStylePresets[preset].name : "Custom", kTextSmall, nullptr);
        d->AddCircleFilled(sp + ImVec2(s(62.0f), s(10.0f)), s(4.0f), g_accent);
    }
    y += rowHeight;

    // Theme colors: Accent (primary) + Buttons/Sliders (secondary controls). Each opens the
    // same RGBA channel popover the feature color pickers use; values persist via config and
    // refreshMenuTheme() applies them next frame.
    {
        // one helper for all three rows: label + swatch pill that opens the RGBA popover.
        // colorGet/colorSet are bound INSIDE the click branch - all three rows run every
        // frame, so a bind-outside-the-click would leave the last row's pointers in state.
        auto themeColorRow = [&](const char* label, int id, ImU32 current, color::Rgba (*getter)(), void (*setter)(color::Rgba)) {
            textY(d, p.x + s(14), y, rowHeight, C(185, 188, 198), label, kTextControl, nullptr);
            const ImVec2 sp(p.x + width - s(84.0f), y + rowCentered(s(20.0f)));
            ImGui::PushID(id);
            const bool clicked = hitPopupRow("##theme_color", sp, ImVec2(s(72.0f), s(20.0f)), PopupProfile);
            const float hover = motion(animKey(0x6d10u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
            ImGui::PopID();
            d->AddRectFilled(sp, sp + ImVec2(s(72.0f), s(20.0f)), mix(kPillBg, kPillBgHover, hover), s(7));
            const ImVec2 swatch = sp + ImVec2(s(5), s(4));
            d->AddRectFilled(swatch, swatch + ImVec2(s(16), s(12)), current, s(3));
            char hex[8];
            std::snprintf(hex, sizeof(hex), "%02X%02X%02X", current & 0xFF, current >> 8 & 0xFF, current >> 16 & 0xFF); // ImU32 = 0xAABBGGRR -> print RRGGBB
            textY(d, sp.x + s(30), sp.y, s(20.0f), C(170, 173, 184), hex, kTextSmall, nullptr);
            if (clicked) {
                const bool open = state.colorPickerOpen && state.colorPickerOwner == id;
                state.colorPickerOpen = !open;
                state.colorPickerOwner = id;
                state.colorPickerOpenedFrame = ImGui::GetFrameCount();
                state.colorPickerAnchor = sp;
                state.colorGet = getter;
                state.colorSet = setter;
                styleSelect.open = false;
            }
        };

        themeColorRow("Accent", 8710, g_accent, &colorPickerGetter<MenuAccentColor>, &colorPickerSetter<MenuAccentColor>);
        y += rowHeight;
        themeColorRow("Buttons", 8711, g_buttonAccent, &colorPickerGetter<MenuButtonColor>, &colorPickerSetter<MenuButtonColor>);
        y += rowHeight;
        themeColorRow("Sliders", 8712, g_sliderAccent, &colorPickerGetter<MenuSliderColor>, &colorPickerSetter<MenuSliderColor>);
        y += rowHeight;

        // Outer glow: on/off, color (or rainbow fade driven by speed)
        {
            const bool glowOn = ui_config::get<MenuGlowEnabled>();
            textY(d, p.x + s(14), y, rowHeight, C(185, 188, 198), "Outer Glow", kTextControl, nullptr);
            const ImVec2 tp(p.x + width - s(43.0f), y + rowCentered(s(18.0f)));
            ImGui::PushID(8713);
            const bool clicked = hitPopupRow("##glow_toggle", tp - ImVec2(s(9), 0.0f), ImVec2(s(38.0f), s(18.0f)), PopupProfile);
            const float r = motion(animKey(0x6d13u, 8713), glowOn ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
            ImGui::PopID();
            drawCheckboxChip(d, tp + ImVec2(s(5.5f), 0.0f), s(18), r);
            if (clicked)
                ui_config::set<MenuGlowEnabled>(!glowOn);
        }
        y += rowHeight;

        themeColorRow("Glow Color", 8714, glowTintPreview(), &colorPickerGetter<MenuGlowColor>, &colorPickerSetter<MenuGlowColor>);
        y += rowHeight;

        {
            const bool rainbow = ui_config::get<MenuGlowRainbow>();
            textY(d, p.x + s(14), y, rowHeight, C(185, 188, 198), "Fading RGB", kTextControl, nullptr);
            const ImVec2 tp(p.x + width - s(43.0f), y + rowCentered(s(18.0f)));
            ImGui::PushID(8715);
            const bool clicked = hitPopupRow("##glow_rainbow", tp - ImVec2(s(9), 0.0f), ImVec2(s(38.0f), s(18.0f)), PopupProfile);
            const float r = motion(animKey(0x6d14u, 8715), rainbow ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
            ImGui::PopID();
            drawCheckboxChip(d, tp + ImVec2(s(5.5f), 0.0f), s(18), r);
            if (clicked)
                ui_config::set<MenuGlowRainbow>(!rainbow);
        }
        y += rowHeight;

        // Fading-RGB style: the accent/button/slider colors cycle with the same clock as the
        // glow rainbow (see refreshMenuTheme).
        {
            const bool styleRainbow = ui_config::get<MenuStyleRainbow>();
            textY(d, p.x + s(14), y, rowHeight, C(185, 188, 198), "Fading RGB Style", kTextControl, nullptr);
            const ImVec2 tp(p.x + width - s(43.0f), y + rowCentered(s(18.0f)));
            ImGui::PushID(8718);
            const bool clicked = hitPopupRow("##style_rainbow", tp - ImVec2(s(9), 0.0f), ImVec2(s(38.0f), s(18.0f)), PopupProfile);
            const float r = motion(animKey(0x6d17u, 8718), styleRainbow ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
            ImGui::PopID();
            drawCheckboxChip(d, tp + ImVec2(s(5.5f), 0.0f), s(18), r);
            if (clicked)
                ui_config::set<MenuStyleRainbow>(!styleRainbow);
        }
        y += rowHeight;

        // Glow speed: compact slider (rainbow cycle rate). Right-aligned track sized to clear
        // the longest row label (~s(84) at any scale) and vertically centered in the row, like
        // the color chips / toggles on the other rows.
        {
            textY(d, p.x + s(14), y, rowHeight, C(185, 188, 198), "RGB Speed", kTextControl, nullptr);
            const float min = MenuGlowSpeed::ValueType::kMin;
            const float max = MenuGlowSpeed::ValueType::kMax;
            float value = static_cast<float>(ui_config::get<MenuGlowSpeed>());
            const ImVec2 start(p.x + width - s(112.0f), y + (rowHeight - s(6.0f)) * 0.5f);
            const float trackWidth = s(98.0f);
            ImGui::PushID(8716);
            hitPopupRow("##glow_speed", start - ImVec2(s(4), s(6)), ImVec2(trackWidth + s(8), s(18)), PopupProfile);
            if (ImGui::IsItemActive()) {
                value = min + ImClamp((ImGui::GetIO().MousePos.x - start.x) / trackWidth, 0.0f, 1.0f) * (max - min);
                ui_config::set<MenuGlowSpeed>(MenuGlowSpeed::ValueType{value});
            }
            const float shown = motion(animKey(0x6d15u, 8716), (value - min) / (max - min), 16.0f, (value - min) / (max - min));
            ImGui::PopID();
            d->AddRectFilled(start, start + ImVec2(trackWidth, s(6)), C(44, 44, 47), s(3));
            d->AddRect(start, start + ImVec2(trackWidth, s(6)), C(66, 66, 72), s(3));
            d->AddRectFilled(start, start + ImVec2(trackWidth * shown, s(6)), g_sliderAccent, s(3));
            d->AddCircleFilled(start + ImVec2(trackWidth * shown, s(3.0f)), s(6.0f), C(247, 248, 252));
        }
        y += rowHeight;

        // Glow size: how far the glow band reaches around the shell (same geometry as RGB Speed)
        {
            textY(d, p.x + s(14), y, rowHeight, C(185, 188, 198), "Glow Size", kTextControl, nullptr);
            const float min = MenuGlowSize::ValueType::kMin;
            const float max = MenuGlowSize::ValueType::kMax;
            float value = static_cast<float>(ui_config::get<MenuGlowSize>());
            const ImVec2 start(p.x + width - s(112.0f), y + (rowHeight - s(6.0f)) * 0.5f);
            const float trackWidth = s(98.0f);
            ImGui::PushID(8717);
            hitPopupRow("##glow_size", start - ImVec2(s(4), s(6)), ImVec2(trackWidth + s(8), s(18)), PopupProfile);
            if (ImGui::IsItemActive()) {
                value = min + ImClamp((ImGui::GetIO().MousePos.x - start.x) / trackWidth, 0.0f, 1.0f) * (max - min);
                ui_config::set<MenuGlowSize>(MenuGlowSize::ValueType{value});
            }
            const float shown = motion(animKey(0x6d16u, 8717), (value - min) / (max - min), 16.0f, (value - min) / (max - min));
            ImGui::PopID();
            d->AddRectFilled(start, start + ImVec2(trackWidth, s(6)), C(44, 44, 47), s(3));
            d->AddRect(start, start + ImVec2(trackWidth, s(6)), C(66, 66, 72), s(3));
            d->AddRectFilled(start, start + ImVec2(trackWidth * shown, s(6)), g_sliderAccent, s(3));
            d->AddCircleFilled(start + ImVec2(trackWidth * shown, s(3.0f)), s(6.0f), C(247, 248, 252));
        }
        y += rowHeight;

        // Debug probes for the glow's 9-slice pieces
        {
            const bool debug = ui_config::get<MenuGlowDebug>();
            textY(d, p.x + s(14), y, rowHeight, C(150, 154, 165), "Glow Debug", kTextControl, nullptr);
            const ImVec2 tp(p.x + width - s(43.0f), y + rowCentered(s(18.0f)));
            ImGui::PushID(8719);
            const bool clicked = hitPopupRow("##glow_debug", tp - ImVec2(s(9), 0.0f), ImVec2(s(38.0f), s(18.0f)), PopupProfile);
            const float r = motion(animKey(0x6d17u, 8719), debug ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
            ImGui::PopID();
            drawCheckboxChip(d, tp + ImVec2(s(5.5f), 0.0f), s(18), r);
            if (clicked)
                ui_config::set<MenuGlowDebug>(!debug);
        }
        y += rowHeight;
    }

    // (the dead "ESP Scale / Soon" placeholder row was removed - visible stubs read as unfinished)

    const float eased = g_reduceMotion ? 1.0f : 1.0f - std::pow(1.0f - open, 3.0f);
    const ImVec2 pivot(p.x + size.x * 0.5f, p.y + size.y);
    for (int i = first; i < d->VtxBuffer.Size; ++i) {
        ImDrawVert& v = d->VtxBuffer[i];
        v.pos = pivot + (v.pos - pivot) * ImLerp(1.04f, 1.0f, eased);
        const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
        v.col = (v.col & 0x00ffffffu) | ((ImU32)(a * eased) << IM_COL32_A_SHIFT);
    }

    // The popover's child popups (RGBA picker, style preset list) render above it - clicks on
    // them must count as INSIDE, or the popover would close mid-interaction and orphan them.
    // Use the rects they actually drew last frame (already accounts for the picker's
    // flip-above placement); a picker left open from the page keeps the popover alive too,
    // which is fine - the picker owns those clicks anyway.
    ImVec2 keepMin = p - ImVec2(s(4), s(2));
    ImVec2 keepMax = p + size + ImVec2(s(4), s(7));
    if (popupPrev[PopupColor].valid) {
        keepMin = ImMin(keepMin, popupPrev[PopupColor].min - ImVec2(s(4), s(4)));
        keepMax = ImMax(keepMax, popupPrev[PopupColor].max + ImVec2(s(4), s(4)));
    }
    if (popupPrev[PopupStyle].valid) {
        keepMin = ImMin(keepMin, popupPrev[PopupStyle].min - ImVec2(s(4), s(4)));
        keepMax = ImMax(keepMax, popupPrev[PopupStyle].max + ImVec2(s(4), s(4)));
    }

    if (state.profileOpen && ImGui::GetFrameCount() > state.profileOpenedFrame
        && clickedOutside(keepMin, keepMax)
        && clickedOutside(accountBarMin, accountBarMax)) {
        state.profileOpen = false;
        if (state.editingSlider == kScaleEditId)
            state.editingSlider = -1; // drop a half-typed scale edit with the popover
    }
}

void configPopover(ImDrawList* d, ImVec2 base) noexcept
{
    const float open = motion(ImGui::GetID("##config_pop_open"), configPopoverOpen ? 1.0f : 0.0f, 20.0f, 0.0f);
    if (open < 0.002f)
        return;

    const std::uint8_t count = ui_config::listedConfigCount();
    const float width = s(200.0f);

    // visible rows after the search filter
    std::uint8_t visible[ConfigVisibleCap];
    std::uint8_t visibleCount = 0;
    for (std::uint8_t i = 0; i < count && visibleCount < ConfigVisibleCap; ++i) {
        if (!configSearch[0] || strncasecmp(configSearch, ui_config::listedConfigName(i), std::strlen(configSearch)) == 0)
            visible[visibleCount++] = i;
    }

    const float searchHeight = s(30.0f);
    const float listHeight = visibleCount * s(28.0f) + s(6.0f);
    const float actionHeight = s(134.0f); // SAVE + DUPLICATE + RESTORE + NEW/RENAME row, all inside the panel
    const ImVec2 p = base + ImVec2(kSidebarWidth + s(11), kToolbarHeight + s(4));
    const ImVec2 size(width, searchHeight + listHeight + actionHeight);
    recordPopupRect(PopupConfig, p, p + size);
    const int first = d->VtxBuffer.Size;
    softShadow(d, p, p + size, s(16.0f));
    d->AddRectFilled(p, p + size, kPopupBg, s(18));
    d->AddRect(p, p + size, kPopupBorder, s(18));
    d->AddLine(p + ImVec2(s(12), searchHeight), p + ImVec2(size.x - s(12), searchHeight), C(38, 38, 42));

    // search input (inside the panel, top row)
    ImGui::SetCursorScreenPos(p + ImVec2(s(8), s(4)));
    ImGui::PushItemWidth(width - s(16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(6), s(4)));
    ImGui::InputTextWithHint("##cfg_search", "search", configSearch, sizeof(configSearch));
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    // config rows (active is protected from deletion - autosave owns that file)
    const int activeIndex = ui_config::activeConfigIndex();
    for (std::uint8_t v = 0; v < visibleCount; ++v) {
        const std::uint8_t i = visible[v];
        ImVec2 rp = p + ImVec2(s(4), searchHeight + s(4) + v * s(28.0f));
        ImGui::PushID(9100 + i);
        const bool clicked = hitPopupRow("##cfg_row", rp, ImVec2(width - s(8), s(28)), PopupConfig);
        const float hover = motion(animKey(0x51e1u, 9100 + i), ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
        ImGui::PopID();
        if (hover > 0.001f && i != activeIndex)
            d->AddRectFilled(rp, rp + ImVec2(width - s(8), s(28)), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(25 * hover) << IM_COL32_A_SHIFT), s(8));
        const bool active = i == activeIndex;
        if (active)
            d->AddRectFilled(rp + ImVec2(s(2), s(5)), rp + ImVec2(s(5), s(23)), g_accent, s(3));
        textY(d, rp.x + s(12), rp.y, s(28), active ? C(224, 229, 243) : C(182, 185, 196), ui_config::listedConfigName(i), kTextControl, nullptr);
        if (clicked && !active) {
            ui_config::switchToConfig(i);
            lastCleanConfigEpoch = ui_config::changeEpoch.load(std::memory_order_relaxed);
            char toastText[96];
            std::snprintf(toastText, sizeof(toastText), "Switched to %s", ui_config::activeConfigNameForDisplay());
            pushToast(toastText, g_accent);
        }

        if (!active) {
            // visual circle stays s(18); the hit box is s(26) centered on it - an 18px target
            // sitting between two rows was a mis-click magnet
            const ImVec2 xc = rp + ImVec2(width - s(17.0f), s(16.0f));
            ImGui::PushID(9300 + i);
            const bool deleted = hitPopupRow("##cfg_del", xc - ImVec2(s(13), s(13)), ImVec2(s(26), s(26)), PopupConfig);
            const bool xHover = ImGui::IsItemHovered();
            ImGui::PopID();
            const ImU32 xcol = xHover ? C(245, 160, 164) : C(120, 124, 134);
            if (xHover)
                d->AddCircleFilled(xc, s(9), C(190, 60, 66, 90));
            d->AddLine(xc - ImVec2(s(3), s(3)), xc + ImVec2(s(3), s(3)), xcol, 1.4f);
            d->AddLine(xc + ImVec2(s(3), -s(3)), xc + ImVec2(-s(3), s(3)), xcol, 1.4f);
            if (deleted) {
                if (ui_config::deleteConfig(i))
                    pushToast("Config deleted", g_accent);
                else {
                    gui_log::write("config: failed to delete '%s'", ui_config::listedConfigName(i));
                    pushToast("Delete failed", C(229, 72, 77));
                }
            }
        }
    }

    // actions: SAVE / RESTORE DEFAULTS
    float ay = p.y + searchHeight + listHeight + s(4.0f);
    auto actionButton = [&](int id, const char* label) {
        const ImVec2 bp = p + ImVec2(s(8), ay - p.y);
        ImGui::PushID(id);
        const bool clicked = hitPopupRow("##cfg_action", bp, ImVec2(width - s(16), s(26)), PopupConfig);
        const float hover = motion(animKey(0xa1a1u, id), ImGui::IsItemHovered() ? 1.0f : 0.0f);
        ImGui::PopID();
        d->AddRectFilled(bp, bp + ImVec2(width - s(16), s(26)), mix(kPillBg, kPillBgHover, hover), s(6));
        textY(d, bp.x + s(10), bp.y, s(26), C(182, 185, 196), label, kTextControl, nullptr);
        ay += s(30.0f);
        return clicked;
    };
    if (actionButton(0, "SAVE")) {
        ui_config::saveActive();
        lastCleanConfigEpoch = ui_config::changeEpoch.load(std::memory_order_relaxed);
        pushToast("Config saved", g_accent);
    }
    if (actionButton(1, "DUPLICATE")) {
        if (ui_config::duplicateActiveConfig()) {
            lastCleanConfigEpoch = ui_config::changeEpoch.load(std::memory_order_relaxed);
            pushToast("Config duplicated", g_accent);
        } else
            pushToast("Duplicate failed", C(229, 72, 77));
    }
    if (actionButton(2, "RESTORE DEFAULTS")) {
        ui_config::restoreDefaults();
        lastCleanConfigEpoch = ui_config::changeEpoch.load(std::memory_order_relaxed);
        pushToast("Defaults restored", g_accent);
    }

    // NEW / RENAME: text input + buttons acting on the typed name (stock InputText, our theme)
    const ImVec2 inputPos = p + ImVec2(s(8), ay - p.y + s(2.0f));
    ImGui::SetCursorScreenPos(inputPos);
    ImGui::PushItemWidth(width - s(140.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(6), s(4)));
    ImGui::InputTextWithHint("##nl_new_cfg", "new name", newConfigName, sizeof(newConfigName));
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();
    ImGui::SameLine(0.0f, s(4));
    if (ImGui::Button("NEW", ImVec2(s(52.0f), 0.0f))) {
        if (ui_config::createAndSwitchToConfig(newConfigName)) {
            newConfigName[0] = '\0';
            lastCleanConfigEpoch = ui_config::changeEpoch.load(std::memory_order_relaxed);
            pushToast("Config created", g_accent);
        } else
            pushToast("Create failed", C(229, 72, 77));
    }
    ImGui::SameLine(0.0f, s(4));
    if (ImGui::Button("RENAME", ImVec2(s(70.0f), 0.0f))) {
        if (ui_config::renameActiveConfig(newConfigName)) {
            newConfigName[0] = '\0';
            pushToast("Config renamed", g_accent);
        } else
            pushToast("Rename failed", C(229, 72, 77));
    }
    ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f)); // park the cursor; nothing flow-laid-out follows

    const float eased = g_reduceMotion ? 1.0f : 1.0f - std::pow(1.0f - open, 3.0f);
    const ImVec2 pivot(p.x + size.x * 0.5f, p.y);
    for (int i = first; i < d->VtxBuffer.Size; ++i) {
        ImDrawVert& v = d->VtxBuffer[i];
        v.pos = pivot + (v.pos - pivot) * ImLerp(0.96f, 1.0f, eased) - ImVec2(0, s(4.0f) * (1.0f - eased));
        const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
        v.col = (v.col & 0x00ffffffu) | ((ImU32)(a * eased) << IM_COL32_A_SHIFT);
    }

    if (configPopoverOpen && ImGui::GetFrameCount() > configPopoverOpenedFrame
        && clickedOutside(p - ImVec2(s(4), s(2)), p + size + ImVec2(s(4), s(7)))
        && clickedOutside(configChipMin, configChipMax))
        configPopoverOpen = false;
}

} // namespace

// --- public API ----------------------------------------------------------------------

void neverlose::render() noexcept
{
    // One-shot icon atlas sanity check: a codepoint missing from the atlas renders as the
    // FALLBACK (the last range glyph - e.g. the mouse), which historically masqueraded as a
    // wrong icon choice (see the menu-icon-atlas memory). This makes a stale/partial atlas
    // visible in the anomaly log instead of a mystery glyph.
    static bool iconAtlasChecked = false;
    if (!iconAtlasChecked && ImGui::GetIO().Fonts->Fonts.Size > 2) {
        iconAtlasChecked = true;
        if (ImFont* icons = ImGui::GetIO().Fonts->Fonts[2]) {
            for (int i = 0; kIconCodepoints[i] != 0; ++i) {
                if (!icons->FindGlyphNoFallback(kIconCodepoints[i]))
                    gui_log::write("icon atlas missing glyph %#x - stale binary or atlas gap", kIconCodepoints[i]);
            }
        }
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 defaultPos((display.x - kShellWidth) * 0.5f, (display.y - shellHeightBase) * 0.5f);
    const ImVec2 shellPos(defaultPos + menuOffset);

    g_reduceMotion = ui_config::get<MenuReduceMotion>();

    // Adaptive shell height: fit the shell to the page's content so light pages don't leave
    // dead space and heavy pages scroll less. The target uses LAST frame's content height
    // (layout is stable frame-to-frame), clamped so the nav rail + account bar always fit and
    // the shell never exceeds ~90% of the screen. Content layout does not depend on the shell
    // height, so there is no feedback loop.
    {
        const float chrome = kToolbarHeight + s(24.0f) + s(6.0f) + s(6.0f);
        const float minH = ImMin(500.0f * menuScale, display.y * 0.9f);
        const float maxH = ImMin(700.0f * menuScale, display.y * 0.9f);
        const float target = lastContentHeight > 0.0f
            ? ImClamp(lastContentHeight + chrome, minH, maxH)
            : shellHeightBase; // first frame: design default until content was measured
        if (g_reduceMotion)
            kShellHeight = target;
        else {
            kShellHeight = ImLerp(kShellHeight, target, 1.0f - std::exp(-10.0f * ImGui::GetIO().DeltaTime));
            if (std::fabs(kShellHeight - target) < 0.5f)
                kShellHeight = target;
        }
    }

    ImGui::SetNextWindowPos(shellPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kShellWidth, kShellHeight), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, 0);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("Neverlose", nullptr, flags)) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 b = ImGui::GetWindowPos();
        shellBase = b;

        const int shellFirst = d->VtxBuffer.Size;

        // hit() gates page controls against the popups recorded LAST frame (see the popup
        // occlusion tracking block) - covered controls are inert, uncovered ones stay live.

        softShadow(d, b, b + ImVec2(kShellWidth, kShellHeight), s(17.0f), s(16.0f));

        d->AddRectFilled(b, b + ImVec2(kShellWidth, kShellHeight), kShellBg, s(17.0f));
        d->AddRect(b, b + ImVec2(kShellWidth, kShellHeight), kHairlineSoft, s(17.0f));
        // faint outer highlight lets the shell read as a floating pane over the game
        d->AddRect(b - ImVec2(1.0f, 1.0f), b + ImVec2(kShellWidth, kShellHeight) + ImVec2(1.0f, 1.0f), C(255, 255, 255, 14), s(18.0f));

        sidebar(d, b);
        toolbar(d, b);
        accountBar(d, b);
        const int contentFirst = d->VtxBuffer.Size;

        // content region: clip + manual scroll. The clip top sits AT the toolbar hairline (was
        // +s(24)): the 24px gutter below the toolbar used to be UNCLIPPED, so scrolled card
        // rows rendered there - the half-cut "Now Playing" row. Card captions drawn at y-s(16)
        // still land in the gutter at rest and stay visible; on scroll they clip at the line.
        const ImVec2 contentMin(b.x + kSidebarWidth + s(2), b.y + kToolbarHeight);
        const ImVec2 contentMax(b.x + kShellWidth - s(4), b.y + kShellHeight - s(6));
        d->PushClipRect(contentMin, contentMax, true);

        resetContent();
        switch (state.page) {
        case Page::Rage: pageRage(); break;
        case Page::Legit: pageLegit(); break;
        case Page::Movement: pageMovement(); break;
        case Page::PlayerInfo: pagePlayerInfo(); break;
        case Page::Glow: pageGlow(); break;
        case Page::Viewmodel: pageViewmodel(); break;
        case Page::Effects: pageEffects(); break;
        case Page::Hud: pageHud(); break;
        case Page::Sound: pageSound(); break;
        case Page::Inventory: pageInventory(); break;
        case Page::Radio: pageRadio(); break;
        case Page::Scripts: pageScripts(); break;
        case Page::Misc: pageMisc(); break;
        }
        renderScriptCardsForPage(state.page); // script gui.page(...) cards at the page bottom

        const float contentHeight = ImMax(columnYs[0], columnYs[1]);
        lastContentHeight = contentHeight; // adaptive shell height target (read next frame)
        const float visibleHeight = contentMax.y - contentMin.y;
        const float previousScrollOffset = scrollOffset;
        maxScroll = ImMax(0.0f, contentHeight - visibleHeight);
        if (ImGui::IsMouseHoveringRect(contentMin, contentMax) && ImGui::GetIO().MouseWheel != 0.0f
            // While ANY dropdown/picker popup is open the wheel belongs to its list (they all
            // scroll) - page scroll would move the page out from under the open popup (it also
            // used to close kit-mode popups as a stale anchor). The other modals (multi-select,
            // color picker) lock the page wheel too - one rule everywhere.
            && !(state.popup.open && state.popup.paintKitMode)
            && !state.multiSelectOpen
            && !state.colorPickerOpen)
            scrollTarget = ImClamp(scrollTarget - ImGui::GetIO().MouseWheel * 40.0f, 0.0f, maxScroll);
        if (scrollTarget > maxScroll)
            scrollTarget = maxScroll;
        // ease toward the target; settle exactly once close enough (avoids endless subpixel
        // text shimmer from a decaying-but-never-exact lerp)
        scrollOffset = ImLerp(scrollOffset, scrollTarget, 1.0f - std::exp(-14.0f * ImGui::GetIO().DeltaTime));
        if (std::fabs(scrollTarget - scrollOffset) < 0.3f)
            scrollOffset = scrollTarget;
        // the hand-drawn popups anchor to screen positions of the rows that opened them - when
        // the content scrolls, MOVE the open popups with their rows instead of closing them
        // (the popup layers re-clamp into the shell every frame, so a scrolled-away row leaves
        // the popup docked at the edge rather than gone)
        {
            const float scrollDelta = scrollOffset - previousScrollOffset;
            if (std::fabs(scrollDelta) > 0.001f) {
                state.popup.anchor.y -= scrollDelta;
                state.multiSelectAnchor.y -= scrollDelta;
                state.colorPickerAnchor.y -= scrollDelta;
                state.featureBindAnchor.y -= scrollDelta;
            }
        }

        // page transition: slide + fade the content in. Suppressed while the reveal is still
        // animating - the two transforms compound on the same vertices (glitchy overlap).
        const float pageMix = reveal < 0.999f ? 1.0f : motion(ImGui::GetID("##page_mix"), 1.0f, 15.0f, 0.0f);
        const float eased = 1.0f - std::pow(1.0f - pageMix, 3.0f);
        if (eased < 0.999f) {
            for (int i = contentFirst; i < d->VtxBuffer.Size; ++i) {
                ImDrawVert& v = d->VtxBuffer[i];
                v.pos.x += s(9.0f) * (1.0f - eased);
                const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
                v.col = (v.col & 0x00ffffffu) | ((ImU32)(a * eased) << IM_COL32_A_SHIFT);
            }
        }
        d->PopClipRect();

        // Scroll affordances for the content region (drawn unclipped, before the popups): an
        // edge fade wherever more content hides beyond the edge, plus a DRAGGABLE position
        // scrollbar on the right edge while the page overflows.
        {
            const ImU32 bgFade = kShellBg;
            const ImU32 clear = bgFade & 0x00FFFFFFu;
            const float fade = s(16.0f);
            if (scrollOffset > 1.0f)
                d->AddRectFilledMultiColor(contentMin, ImVec2(contentMax.x, contentMin.y + fade), bgFade, bgFade, clear, clear);
            if (scrollOffset < maxScroll - 1.0f)
                d->AddRectFilledMultiColor(ImVec2(contentMin.x, contentMax.y - fade), contentMax, clear, clear, bgFade, bgFade);
            if (maxScroll > 1.0f) {
                const float track = visibleHeight - s(8.0f);
                const float thumbHeight = ImClamp(visibleHeight * visibleHeight / contentHeight, s(30.0f), track);
                const float t = maxScroll > 0.0f ? scrollOffset / maxScroll : 0.0f;
                float thumbY = contentMin.y + s(4.0f) + t * (track - thumbHeight);
                // Thin thumb tucked into the region's right padding: AUTO-HIDDEN unless the
                // cursor is near it or a drag is active - a permanently visible bar overlapped
                // the row pills on the right column.
                const ImVec2 trackMin(contentMax.x - s(9.0f), contentMin.y);
                const ImVec2 trackMax(contentMax.x, contentMax.y);
                static bool dragging = false;
                static float grabOffset = 0.0f;
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const bool nearTrack = ImGui::IsMouseHoveringRect(trackMin - ImVec2(s(14), 0), trackMax);
                const bool hovered = ImGui::IsMouseHoveringRect(ImVec2(trackMin.x, thumbY), ImVec2(trackMax.x, thumbY + thumbHeight));
                if ((hovered || (nearTrack && ImGui::IsMouseClicked(0))) && ImGui::IsMouseClicked(0)) {
                    dragging = true;
                    grabOffset = ImClamp(mouse.y, thumbY, thumbY + thumbHeight) - thumbY;
                }
                if (!ImGui::IsMouseDown(0))
                    dragging = false;
                if (dragging)
                    scrollTarget = scrollOffset = ImClamp((mouse.y - grabOffset - (contentMin.y + s(4.0f))) / (track - thumbHeight), 0.0f, 1.0f) * maxScroll;

                if (hovered || dragging) {
                    const float tNow = maxScroll > 0.0f ? scrollOffset / maxScroll : 0.0f;
                    const float yNow = contentMin.y + s(4.0f) + tNow * (track - thumbHeight);
                    d->AddRectFilled(ImVec2(contentMax.x - s(5.0f), yNow), ImVec2(contentMax.x - s(1.0f), yNow + thumbHeight),
                        C(200, 203, 212, dragging ? 150 : 95), s(2));
                }
            }
        }

        // Escape dismisses every open modal layer in one go - previously only the dropdown,
        // feature-bind popover and search overlay reacted to it (each with its own handler).
        // Keybind capture and slider text-edit take precedence: Escape cancels those first.
        // The search overlay and the feature-bind popover keep their own capture-aware Escape.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)
            && state.capture == State::Capture::Inactive
            && state.editingSlider < 0) {
            state.popup.open = false;
            state.multiSelectOpen = false;
            state.colorPickerOpen = false;
            styleSelect.open = false;
            configPopoverOpen = false;
            state.profileOpen = false;
        }

        // Modal popup stack: while any hand-drawn popup is open, clicks are swallowed for
        // everything drawn BEFORE it (the popovers render in z-order; without this, a click on
        // the top popup also lands on the popover rows underneath). The modal popups' own rows
        // use hitModal() and keep working.
        configPopover(d, b);
        profilePopover(d, b);

        // global search: one ghost page indexed per frame while the overlay is open, so the
        // full index is ready ~11 frames (tens of ms) after the first open
        if (searchOpen)
            indexNextSearchPage();
        searchOverlay(d, b);

        // The script editor is its own top-level window on top of the shell - rendered here so
        // it stays up regardless of which nav tab is active (it self-gates on open + menu open).
        drawScriptEditorWindow();

        // Script-owned ImGui windows (imgui.* from the "menu" callback): rendered after the
        // shell + editor so they float above the menu, dispatched menu-open only (inside the
        // frame; without a live menu the game owns the mouse). Errors are contained in the
        // manager like every other callback; leaked windows are force-closed there too.
        lua::dispatchMenuWindows();

        // reveal: scale + fade the whole shell - forward from the open moment, reversed while
        // the menu dismisses (GUI.cpp keeps render() alive past the alpha fade until this
        // lands). The transform is symmetric in reveal, so the same vertex pass serves both.
        {
            if (dismissActive) {
                // Snappy dismissal (~120ms): the slow exp(-9) ease made INSERT-close feel
                // broken - the shell visibly lingered for ~0.5s while opening felt instant.
                reveal = ImLerp(reveal, 0.0f, 1.0f - std::exp(-20.0f * ImGui::GetIO().DeltaTime));
                if (reveal < 0.05f) {
                    reveal = 0.0f;
                    dismissActive = false; // shell fully gone - stop reserving render time
                }
            } else {
                reveal = ImLerp(reveal, 1.0f, 1.0f - std::exp(-5.0f * ImGui::GetIO().DeltaTime));
            }
            if (!(reveal >= 0.0f && reveal <= 1.0f))
                reveal = dismissActive ? 0.0f : 1.0f; // NaN/inf guard: one bad frame must not garble the vertex buffer
            if (reveal < 0.999f) {
                const float e = 1.0f - std::pow(1.0f - reveal, 4.0f);
                const float scale = ImLerp(0.92f, 1.0f, e);
                const ImVec2 pivot = b + ImVec2(kShellWidth * 0.5f, kShellHeight * 0.5f);
                for (int i = shellFirst; i < d->VtxBuffer.Size; ++i) {
                    ImDrawVert& v = d->VtxBuffer[i];
                    v.pos = pivot + (v.pos - pivot) * scale + ImVec2(0.0f, (1.0f - e) * s(14.0f));
                    const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
                    v.col = (v.col & 0x00ffffffu) | ((ImU32)(a * e) << IM_COL32_A_SHIFT);
                }
            }
        }

        // style preset mini-popup
        if (styleSelect.open) {
            const int count = static_cast<int>(sizeof(kStylePresets) / sizeof(kStylePresets[0]));
            const ImVec2 size(styleSelect.width, count * s(28.0f) + s(10.0f));
            ImVec2 sp = styleSelect.anchor + ImVec2(0.0f, s(22));
            // clamp inside the shell's clip rect, not the display (this draw list clips at the
            // shell edge - display-based clamping still lets the list clip in half)
            sp.x = ImClamp(sp.x, d->GetClipRectMin().x + s(10.0f), d->GetClipRectMax().x - size.x - s(10.0f));
            sp.y = ImClamp(sp.y, d->GetClipRectMin().y + s(10.0f), d->GetClipRectMax().y - size.y - s(10.0f));
            const bool accepts = ImGui::GetFrameCount() > styleSelect.openedFrame;
            recordPopupRect(PopupStyle, sp, sp + size);
            d->AddRectFilled(sp, sp + size, kPopupBg, s(14));
            d->AddRect(sp, sp + size, kPopupBorder, s(14));
            for (int i = 0; i < count; ++i) {
                ImVec2 rp = sp + ImVec2(s(5), s(5) + i * s(28.0f));
                ImGui::PushID(8900 + i);
                const bool clicked = hitModal("##style_row", rp, ImVec2(size.x - s(10), s(28)));
                const float hover = motion(animKey(0x77e1u, 8900 + i), ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
                ImGui::PopID();
                if (hover > 0.001f)
                    d->AddRectFilled(rp, rp + ImVec2(size.x - s(10), s(28)), C(255, 255, 255, static_cast<int>(14 * hover)), s(8));
                d->AddCircleFilled(rp + ImVec2(s(16), s(14)), s(5), kStylePresets[i].color);
                textY(d, rp.x + s(32), rp.y, s(28), C(182, 185, 196), kStylePresets[i].name, kTextControl, nullptr);
                if (accepts && clicked) {
                    // IM_COL32 packs 0xAABBGGRR (little-endian ImU32): low byte = red
                    const auto presetRgba = color::Rgba{static_cast<std::uint8_t>(kStylePresets[i].color & 0xFF), static_cast<std::uint8_t>(kStylePresets[i].color >> 8 & 0xFF), static_cast<std::uint8_t>(kStylePresets[i].color >> 16 & 0xFF), static_cast<std::uint8_t>(kStylePresets[i].color >> 24 & 0xFF)};
                    ui_config::set<MenuAccentColor>(presetRgba);
                    ui_config::set<MenuButtonColor>(presetRgba);
                    ui_config::set<MenuSliderColor>(presetRgba);
                    ui_config::set<MenuGlowColor>(color::Rgba{presetRgba.r(), presetRgba.g(), presetRgba.b(), static_cast<std::uint8_t>(menu_theme_vars::kDefaultGlowColor.a())});
                    refreshMenuTheme();
                    styleSelect.open = false;
                }
            }
            if (accepts && clickedOutside(sp, sp + size) && clickedOutside(styleAnchorMin, styleAnchorMax))
                styleSelect.open = false;
        }

        popupLayer(d);
        multiSelectPopover(d);
        // A script color row publishes a raw GuiItem pointer into the shared picker. An unload
        // (or slot reuse by another script) between click and popover would retarget the picker
        // at a dead/foreign item - force-close instead. Native pickers (owner id outside the
        // script control range) never trip this.
        if (state.colorPickerOpen && scriptColorTarget) {
            const int owner = state.colorPickerOwner;
            const bool scriptOwned = owner >= kScriptGuiControlBase
                && owner < kScriptGuiControlBase + lua::kMaxScripts * lua::kMaxGuiItems;
            if (scriptOwned) {
                const int slot = (owner - kScriptGuiControlBase) / lua::kMaxGuiItems;
                const int item = (owner - kScriptGuiControlBase) % lua::kMaxGuiItems;
                lua::Script& script = lua::scripts[slot];
                const bool live = script.L && item < script.guiItemCount
                    && script.guiItems[item].type == lua::GuiItem::Type::Color
                    && &script.guiItems[item] == scriptColorTarget
                    && std::strcmp(script.name, scriptColorOwnerName) == 0;
                if (!live) {
                    state.colorPickerOpen = false;
                    scriptColorTarget = nullptr;
                }
            }
        }
        colorPickerPopover(d);
        featureBindPopover(d);

        // rotate the popup rect snapshot: what the popups drew THIS frame gates page controls
        // NEXT frame (popupPrev holds the complete last-frame set; popupCur starts empty again)
        for (int i = 0; i < PopupKindCount; ++i) {
            popupPrev[i] = popupCur[i];
            popupCur[i].valid = false;
        }

        drawToasts(d, b); // topmost layer inside the shell
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// Game-anchored overlay pass: runs EVERY frame from GUI::render (independent of menu alpha),
// because the hitmarker and the player list are gameplay HUD, not menu.
void drawPlayerListWindow() noexcept; // defined below
    float drawBindsListWindow(float extraYOffset) noexcept; // defined below; returns window height (0 = not drawn)
    void drawSpectatorListWindow(float& yOffsetForBindsList) noexcept; // defined below
    float drawCheatOMeterWindow(float extraYOffset) noexcept; // defined below; returns window height (0 = not drawn)
    float drawLagOMeterWindow(float extraYOffset) noexcept; // defined below; returns window height (0 = not drawn)
    float drawCombatCountersWindow() noexcept; // defined below; bottom-left, returns window height (0 = not drawn)
    float drawHitFeedWindow() noexcept; // defined below; top-left, returns window height (0 = not drawn)
    float drawStatusChipsWindow() noexcept; // defined below; bottom-left, returns window height (0 = not drawn)
void drawLiveBadge() noexcept;
void drawNowPlayingWindow(float combatListHeight) noexcept; // defined below; bottom-left, above COMBAT

// Outer menu glow, drawn on the FOREGROUND draw list: the menu window clips its own draw list to
// the shell rect, which is why the in-window version was invisible (only leaked out during the
// close animation when the reveal warp pulled vertices back inside the clip). The band sits
// OUTSIDE the shell rect so the foreground list never covers menu content. `menuAlpha` ties the
// glow to the shell's open/close fade.
void neverlose::drawMenuGlow(float menuAlpha) noexcept
{
    if (!ui_config::get<MenuGlowEnabled>() || menuAlpha <= 0.0f)
        return;

    const auto glowColor = ui_config::get<MenuGlowColor>();
    const float size = static_cast<float>(ui_config::get<MenuGlowSize>());
    float r = glowColor.r() / 255.0f;
    float g = glowColor.g() / 255.0f;
    float bch = glowColor.b() / 255.0f;
    if (ui_config::get<MenuGlowRainbow>()) {
        const float speed = ui_config::get<MenuGlowSpeed>();
        float hue = std::fmod(static_cast<float>(ImGui::GetTime()) * speed * 0.1f, 1.0f);
        ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, r, g, bch);
    }
    const int rC = static_cast<int>(r * 255.0f);
    const int gC = static_cast<int>(g * 255.0f);
    const int bC = static_cast<int>(bch * 255.0f);
    const int aC = static_cast<int>(glowColor.a() * ImSaturate(menuAlpha));

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 shellPos((display.x - kShellWidth) * 0.5f + menuOffset.x, (display.y - shellHeightBase) * 0.5f + menuOffset.y);
    const ImVec2 shellEnd = shellPos + ImVec2(kShellWidth, kShellHeight);

    // Nested rounded-rect rings around the shell: every ring keeps the shell's own corner
    // rounding (an offset rounded rect stays parallel to the original curve), so the glow hugs
    // the corners at ANY size - the 9-slice stamp could not (its corner fade left a square
    // notch, and its left/right edge pieces were zero-height anyway). Painted outside-in so the
    // brighter inner rings draw over the outer falloff.
    const int rings = ImClamp(static_cast<int>(size / 2.5f), 8, 24);
    const float thickness = size / rings + 2.0f; // +1.5 overlap so the rings fuse into one band
    for (int i = rings; i >= 1; --i) {
        // the ring band covers [offset(i-1), offset(i)]; its center sits at the band midpoint,
        // with +1px bias inward so the innermost ring's inner edge touches the shell exactly
        const float outer = size * static_cast<float>(i) / static_cast<float>(rings);
        const float inner = size * static_cast<float>(i - 1) / static_cast<float>(rings);
        const float offset = (outer + inner) * 0.5f - 0.5f;
        const float fade = 1.0f - static_cast<float>(i) / static_cast<float>(rings);
        const float ringAlpha = static_cast<float>(aC) * (fade * fade * (3.0f - 2.0f * fade)); // smoothstep falloff
        if (ringAlpha < 1.0f)
            continue;
        fg->AddRect(ImVec2(shellPos.x - offset, shellPos.y - offset),
                    ImVec2(shellEnd.x + offset, shellEnd.y + offset),
                    C(rC, gC, bC, static_cast<int>(ringAlpha)), s(17.0f) + offset, 0, thickness + 1.0f);
    }

    // DEBUG (Menu > account popover > Glow Debug): white outline = shell rect, cyan outline =
    // the glow's outer edge.
    if (ui_config::get<MenuGlowDebug>()) {
        fg->AddRect(shellPos, shellEnd, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
        fg->AddRect(ImVec2(shellPos.x - size, shellPos.y - size),
                    ImVec2(shellEnd.x + size, shellEnd.y + size), IM_COL32(0, 255, 255, 200), 0.0f, 0, 1.0f);
    }
}

// Feature binds: the curated list of bindable toggles (right-click in the menu opens the bind
// popup) registered once per session, then the binds applied every rendered frame - menu closed
// or not. Registration happens on the present thread like everything else bind-related (see
// FeatureBinds.h: no locking needed, the registry never leaves this thread).
void registerFeatureBinds() noexcept
{
    static bool done = false;
    if (done)
        return;
    done = true;

    using namespace aimbot_vars;
    using namespace triggerbot_vars;
    using namespace legit_aimbot_vars;
    using namespace rcs_vars;
    using namespace no_scope_inaccuracy_vis_vars;
    using namespace spread_circle_vars;
    using namespace outline_glow_vars;
    using namespace model_glow_vars;
    using namespace viewmodel_mod_vars;
    using namespace grenade_timers_vars;
    using namespace watermark_vars;
    using namespace binds_list_vars;

    // --- rage ---
    feature_binds::registerToggle<aimbot_vars::Enabled>("Silent Aim");
    feature_binds::registerToggle<aimbot_vars::BodyAim>("Force Body Aim");
    feature_binds::registerToggle<aimbot_vars::Multipoint>("Multipoint");
    feature_binds::registerToggle<aimbot_vars::DynamicPointscale>("Dynamic Point Scale");
    feature_binds::registerToggle<aimbot_vars::Backtrack>("Backtrack");
    feature_binds::registerToggle<aimbot_vars::AutoStop>("Auto Stop");
    feature_binds::registerToggle<aimbot_vars::SpreadCompensation>("Compensate Spread");
    feature_binds::registerToggle<aimbot_vars::SpreadGate>("Hold Fire Until Exact");
    feature_binds::registerToggle<aimbot_vars::RecoilCompensation>("Compensate Recoil");
    feature_binds::registerToggle<aimbot_vars::ForceShot>("Auto Shoot Ground");
    feature_binds::registerToggle<aimbot_vars::ForceShotAir>("Auto Shoot Air");
    feature_binds::registerToggle<aimbot_vars::WallCheck>("Rage Shoot Visible");
    feature_binds::registerToggle<aimbot_vars::Autowall>("Rage Shoot Walls");
    feature_binds::registerToggle<aimbot_vars::SpreadCircleFov>("Rage Spread FOV");
    feature_binds::registerToggle<aimbot_vars::Extrapolate>("Lead Targets");

    // --- legit / triggerbot ---
    feature_binds::registerToggle<legit_aimbot_vars::Enabled>("Smooth Aim");
    feature_binds::registerToggle<legit_aimbot_vars::DrawFov>("Draw FOV Circle");
    feature_binds::registerToggle<legit_aimbot_vars::SpreadCircleFov>("Legit Spread Circle FOV");
    feature_binds::registerToggle<aimbot_vars::HitHead>("Target Head");
    feature_binds::registerToggle<aimbot_vars::HitChest>("Target Chest");
    feature_binds::registerToggle<aimbot_vars::HitStomach>("Target Stomach");
    feature_binds::registerToggle<aimbot_vars::HitArms>("Target Arms");
    feature_binds::registerToggle<aimbot_vars::HitLegs>("Target Legs");
    feature_binds::registerToggle<triggerbot_vars::Enabled>("Triggerbot");
    feature_binds::registerToggle<triggerbot_vars::AccuracyCheck>("Shoot When Accurate");
    feature_binds::registerToggle<triggerbot_vars::HeadOnly>("Shoot At The Head");
    feature_binds::registerToggle<triggerbot_vars::MaxAccuracyOnly>("Shoot At Max Accuracy");
    feature_binds::registerToggle<triggerbot_vars::WallCheck>("Triggerbot Shoot Visible");
    feature_binds::registerToggle<triggerbot_vars::Autowall>("Triggerbot Shoot Walls");
    feature_binds::registerToggle<rcs_vars::Enabled>("Control Recoil");
    feature_binds::registerToggle<no_scope_inaccuracy_vis_vars::Enabled>("No-scope Inaccuracy Vis");
    feature_binds::registerToggle<spread_circle_vars::Enabled>("Draw Weapon Spread");

    // --- visuals ---
    feature_binds::registerToggle<outline_glow_vars::Enabled>("Outline Glow");
    feature_binds::registerToggle<model_glow_vars::Enabled>("Model Glow");
    feature_binds::registerToggle<viewmodel_mod_vars::ModifyFov>("Modify Viewmodel Fov");
    feature_binds::registerToggle<::HitmarkerEnabled>("Hit Marker");
    feature_binds::registerToggle<::ForceThirdPersonEnabled>("Force Third Person");
    feature_binds::registerToggle<::RemoveViewPunch>("Remove View Punch");
    feature_binds::registerToggle<::RemoveLegs>("Remove First-Person Legs");
    feature_binds::registerToggle<::RemoveFlashOverlay>("Remove Flash Overlay");
    feature_binds::registerToggle<::RemoveMenuAds>("Remove Main Menu Ads");
    feature_binds::registerToggle<::WorldColorsInfernoEnabled>("Recolor Fire");
    feature_binds::registerToggle<::WorldColorsLightsEnabled>("Recolor Lights");
    feature_binds::registerToggle<::WorldColorsSkyEnabled>("Recolor Sky");
    feature_binds::registerToggle<::WorldColorsWorldEnabled>("Recolor World");
    feature_binds::registerToggle<::WorldColorsFogEnabled>("Gradient Fog");
    feature_binds::registerToggle<::WorldColorsBloomEnabled>("Sky Bloom");
    feature_binds::registerToggle<::PlayerListEnabled>("Player List");
    feature_binds::registerToggle<player_info_vars::PlayerPositionArrowEnabled>("Show Player Position Arrow");
    feature_binds::registerToggle<player_info_vars::PlayerHealthEnabled>("Player Health");
    feature_binds::registerToggle<player_info_vars::ActiveWeaponIconEnabled>("Active Weapon Icon");
    feature_binds::registerToggle<player_info_vars::ActiveWeaponAmmoEnabled>("Active Weapon Ammo");
    feature_binds::registerToggle<player_info_vars::BombCarrierIconEnabled>("Bomb Carrier Icon");
    feature_binds::registerToggle<player_info_vars::BombPlantIconEnabled>("Bomb Planting Icon");
    feature_binds::registerToggle<player_info_vars::BombDefuseIconEnabled>("Defuse Icon");
    feature_binds::registerToggle<player_info_vars::HostagePickupIconEnabled>("Picking Up Hostage Icon");
    feature_binds::registerToggle<player_info_vars::HostageRescueIconEnabled>("Rescuing Hostage Icon");
    feature_binds::registerToggle<player_info_vars::BlindedIconEnabled>("Blinded By Flashbang Icon");
    feature_binds::registerToggle<grenade_timers_vars::Enabled>("Grenade Timers");
    feature_binds::registerToggle<grenade_timers_vars::SmokeTimers>("Smoke Timers");
    feature_binds::registerToggle<grenade_timers_vars::MolotovTimers>("Molotov Timers");
    feature_binds::registerToggle<::BombTimerEnabled>("Bomb Explosion Countdown And Site");
    feature_binds::registerToggle<::DefusingAlertEnabled>("Bomb Defuse Countdown");
    feature_binds::registerToggle<::BombPlantAlertEnabled>("Bomb Plant Alert");
    feature_binds::registerToggle<::KillfeedPreserverEnabled>("Preserve Killfeed");
    feature_binds::registerToggle<watermark_vars::Enabled>("Watermark");
    feature_binds::registerToggle<binds_list_vars::Enabled>("Keybind List");
    feature_binds::registerToggle<spectator_list_params::SpectatorListEnabled>("Spectator List");
}

void neverlose::renderGameOverlay() noexcept
{
    registerFeatureBinds();
    feature_binds::apply();

    refreshMenuTheme();

    // Mic broadcast follows the radio's play state every frame (not only while the Radio tab is
    // open), so stopping a station always hands the microphone back - wherever the user is.
    withRadio([](auto&& radio) { radio.updateMicBroadcast(); });

    // Now playing (HUD box): live volume push + throttled title/MPRIS polls. Same reasoning -
    // the element tracks playback wherever the user is, Radio tab open or not.
    withRadio([](auto&& radio) { radio.updateNowPlaying(); });

    // Discord Rich Presence: 1Hz throttled inside, cheap gates outside; runs with the menu open
    // or closed so the presence tracks the match.
    static_cast<void>(ui_config::withContext([](auto&& hookContext) {
        hookContext.template make<DiscordRpc>().update();
    }));

    const auto snapshot = overlay_layer::snapshot();
    ImDrawList* fg = ImGui::GetForegroundDrawList();

    // FrameworkCS2-port hitmarker: four lines around the crosshair, pixel dimensions
    // straight from the snapshot (alpha already faded by the feature side).
    if (snapshot.hasHitmarker) {
        const float gap = snapshot.hitmarker.gap;
        const float length = snapshot.hitmarker.length;
        const ImU32 color = C(snapshot.hitmarker.rgba >> 24 & 0xFF, snapshot.hitmarker.rgba >> 16 & 0xFF, snapshot.hitmarker.rgba >> 8 & 0xFF, snapshot.hitmarker.rgba & 0xFF);
        const ImVec2 center = ImGui::GetIO().DisplaySize * 0.5f;
        const ImVec2 diag{gap, gap};
        const ImVec2 diagFlip{gap, -gap};
        const ImVec2 dir{length, length};
        const ImVec2 dirFlip{length, -length};
        fg->AddLine(center - diag, center - diag - dir, color, 1.6f);
        fg->AddLine(center + diag, center + diag + dir, color, 1.6f);
        fg->AddLine(center - diagFlip, center - diagFlip - dirFlip, color, 1.6f);
        fg->AddLine(center + diagFlip, center + diagFlip + dirFlip, color, 1.6f);
    }

    // World-anchored smoke/molotov burn timers: percent coordinates -> centered pixel text with a
    // 1px shadow for readability over bright smoke/fire particles.
    if (snapshot.timerCount > 0) {
        const auto& io = ImGui::GetIO();
        if (ImFont* font = io.Fonts->Fonts[0]) {
            const float fontSize = 14.0f;
            const ImVec2 display = io.DisplaySize;
            char text[8];
            for (int i = 0; i < snapshot.timerCount; ++i) {
                const auto& timer = snapshot.timers[i];
                std::snprintf(text, sizeof(text), "%.1fs", static_cast<double>(timer.seconds));
                const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
                const ImVec2 pos{timer.xPercent * 0.01f * display.x - size.x * 0.5f,
                                 timer.yPercent * 0.01f * display.y - size.y * 0.5f};
                const ImU32 color = C(timer.rgba >> 24 & 0xFF, timer.rgba >> 16 & 0xFF, timer.rgba >> 8 & 0xFF, timer.rgba & 0xFF);
                fg->AddText(font, fontSize, pos + ImVec2(1.0f, 1.0f), C(0, 0, 0, 160), text);
                fg->AddText(font, fontSize, pos, color, text);
            }
        }
    }

    drawPlayerListWindow();
    float bindsListOffset = 0.0f;
    drawSpectatorListWindow(bindsListOffset);
    // the right-hand HUD column stacks: spectators -> keybinds -> cheat o meter -> lag-o-meter
    const float bindsListHeight = drawBindsListWindow(bindsListOffset);
    const float cheatOMeterHeight = drawCheatOMeterWindow(bindsListOffset);
    drawLagOMeterWindow(bindsListOffset + bindsListHeight + cheatOMeterHeight);
    // bottom-left / top-left boxes (the old Panorama meters, now on the same visual language)
    const float combatListHeight = drawCombatCountersWindow();
    drawStatusChipsWindow();
    drawNowPlayingWindow(combatListHeight);
    drawHitFeedWindow();
    drawLiveBadge();

    // Lua scripts last: their paint callbacks draw on top of everything else. Each call is
    // pcall'd and instruction-budgeted inside the manager - a bad script errors, never crashes.
    lua::dispatchPaint(ImGui::GetForegroundDrawList());
}

// --- spectator list (in-game HUD overlay) ------------------------------------------------
// The friend-client port: boxed list of who is watching the POV - ours when alive, the
// spectated player's when dead. Names come from SpectatorSnapshot (game thread collects,
// this pass draws); hidden while nobody is spectating.

// LIVE badge: local-only by physics (an overlay lives in OUR process - spectators render
// their own game and can never see it; the networked half is ChatTools' spectator-joined
// say). Drawn as a streamer-style pill so the user knows when they are being watched and
// can play into it / screenshot it.
void drawLiveBadge() noexcept
{
    if (!ui_config::get<chat_vars::LiveBadgeEnabled>())
        return;
    const auto snap = spectator_list::snapshot();
    if (snap.count <= 0 || snap.spectatingOthers)
        return;

    constexpr ImGuiWindowFlags badgeFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoBackground;

    const float displayWidth = ImGui::GetIO().DisplaySize.x;
    ImGui::SetNextWindowPos(ImVec2(displayWidth * 0.5f - s(60.0f), s(10.0f)), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s(10), s(5)));
    if (ImGui::Begin("##ns_live_badge", nullptr, badgeFlags)) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        const float h = ImGui::GetWindowHeight();
        const float w = ImGui::GetWindowWidth();

        char label[48];
        std::snprintf(label, sizeof(label), "LIVE  %d watching", snap.count);

        const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(ImGui::GetTime()) * 4.0f);
        d->AddRectFilled(p, p + ImVec2(w, h), C(20, 10, 12, 225), h * 0.5f);
        d->AddRect(p, p + ImVec2(w, h), C(190, 60, 66, 180), h * 0.5f);
        d->AddCircleFilled(p + ImVec2(s(14), h * 0.5f), s(4.0f), C(229, 72, 77, static_cast<int>(110 + 130 * pulse)));
        textY(d, p.x + s(24), p.y, h, C(244, 226, 228), label, kTextControl, nullptr);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void drawSpectatorListWindow(float& yOffsetForBindsList) noexcept
{
    const auto snap = spectator_list::snapshot();
    if (!ui_config::get<spectator_list_params::SpectatorListEnabled>() || snap.count <= 0)
        return;

    constexpr ImGuiWindowFlags menuClosedFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
    constexpr ImGuiWindowFlags menuOpenFlags = (ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_AlwaysAutoResize);

    const float displayWidth = ImGui::GetIO().DisplaySize.x;
    const float windowWidth = s(170.0f);
    // top-right, under the watermark (above the keybind list, which shifts down while we render)
    ImGui::SetNextWindowPos(ImVec2(displayWidth - windowWidth - s(12.0f), s(44.0f)), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s(12), s(8)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kShellBg);
    ImGui::PushStyleColor(ImGuiCol_Border, C(30, 30, 33, 210));

    if (ImGui::Begin("Spectator list", nullptr, GUI::isMenuOpen() ? menuOpenFlags : menuClosedFlags)) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        const float winWidth = ImGui::GetWindowWidth();

        textY(d, winPos.x, winPos.y + s(6), s(16), C(137, 142, 153), snap.spectatingOthers ? "SPECTATORS OF THEM" : "SPECTATORS", kTextCaption, nullptr);
        d->AddLine(ImVec2(winPos.x + s(3), winPos.y + s(26)), ImVec2(winPos.x + winWidth - s(3), winPos.y + s(26)), (g_accent & 0x00FFFFFFu) | (200u << IM_COL32_A_SHIFT), 1.2f);

        float y = winPos.y + s(32.0f);
        const float rowHeight = s(24.0f);
        for (int i = 0; i < snap.count; ++i) {
            textY(d, winPos.x + s(12), y, rowHeight, kTextBodyCol, snap.names[i], kTextControl, nullptr);
            y += rowHeight;
        }
        yOffsetForBindsList = (y + s(6.0f)) - s(52.0f);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

// --- keybind list (in-game HUD overlay) -------------------------------------------------
// Player-list-style panel listing every bind with its key; the key pill glows in the button
// accent while the key is physically held, so it doubles as a live input indicator. Rendered
// every frame from the game-anchored pass, independent of the menu; interactive only while the
// menu is open (drag to reposition).

struct BindListEntry {
    const char* label;
    int value; // Bind encoding (Bind.h)
};

float drawBindsListWindow(float extraYOffset) noexcept
{
    if (!ui_config::get<binds_list_vars::Enabled>())
        return 0.0f;

    struct BindRowEntry { const char* label; int key; };
    const BindRowEntry entries[] = {
        {"Legit Aim", static_cast<int>(static_cast<legit_aimbot_vars::AimKey::ValueType::ValueType>(ui_config::get<legit_aimbot_vars::AimKey>()))},
        {"Triggerbot", static_cast<int>(static_cast<triggerbot_vars::HoldKey::ValueType::ValueType>(ui_config::get<triggerbot_vars::HoldKey>()))},
        {"Combat Panic", static_cast<int>(static_cast<panic_vars::Bind::ValueType::ValueType>(ui_config::get<panic_vars::Bind>()))},
        {"Net Lag Choke", static_cast<int>(static_cast<net_lag_vars::ChokeKeyBind::ValueType::ValueType>(ui_config::get<net_lag_vars::ChokeKeyBind>()))},
        {"Net Lag Flood", static_cast<int>(static_cast<net_lag_vars::FloodKeyBind::ValueType::ValueType>(ui_config::get<net_lag_vars::FloodKeyBind>()))},
        {"Fake Intel", static_cast<int>(static_cast<chat_vars::IntelBind::ValueType::ValueType>(ui_config::get<chat_vars::IntelBind>()))},
        {"Fake Kick", static_cast<int>(static_cast<chat_vars::KickKey::ValueType::ValueType>(ui_config::get<chat_vars::KickKey>()))},
        {"Last Tick Defuse", static_cast<int>(static_cast<last_tick_vars::DefuseKey::ValueType::ValueType>(ui_config::get<last_tick_vars::DefuseKey>()))},
    };

    // collect the rows that HAVE a key - Off/none entries never make the list
    BindRowEntry rows[24];
    int rowCount = 0;
    for (const auto& e : entries)
        if (e.key != Bind::kOff)
            rows[rowCount++] = e;
    for (std::size_t i = 0; i < feature_binds::entryCount && rowCount < static_cast<int>(sizeof(rows) / sizeof(rows[0])); ++i) {
        const auto& entry = feature_binds::entries[i];
        if (entry.key != Bind::kOff && entry.label)
            rows[rowCount++] = {entry.label, entry.key};
    }

    constexpr ImGuiWindowFlags menuClosedFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing;
    // NoMove: the position is pinned from the config every frame, so ImGui's own mover is
    // disabled and the drag below writes the position back through the config offsets instead.
    constexpr ImGuiWindowFlags menuOpenFlags = (ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoMove);

    const float displayWidth = ImGui::GetIO().DisplaySize.x;
    const float windowWidth = s(232.0f);
    const float headerHeight = s(38.0f);
    const float rowHeight = s(30.0f);
    const float listHeight = headerHeight + static_cast<float>(rowCount) * rowHeight + s(8.0f);

    // empty list (no bound keys at all) = no window at all
    if (rowCount == 0)
        return 0.0f;

    // top-right below the watermark band; the saved offsets shift the window left/down from
    // that anchor (mouse drag / the position is persisted in the config)
    const float offX = static_cast<float>(ui_config::get<binds_list_vars::OffsetX>());
    const float offY = static_cast<float>(ui_config::get<binds_list_vars::OffsetY>());
    ImGui::SetNextWindowPos(ImVec2(displayWidth - windowWidth - s(12.0f) - offX, s(52.0f) + extraYOffset + offY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth, listHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kSidebarBg);
    ImGui::PushStyleColor(ImGuiCol_Border, C(52, 52, 58, 220));

    if (ImGui::Begin("Keybind list", nullptr, GUI::isMenuOpen() ? menuOpenFlags : menuClosedFlags)) {
        // Drag to reposition: the window body is pure draw-list content (no items), so hover
        // anywhere on it. The drag writes the offsets the pinned position is derived from -
        // config autosave persists them across sessions.
        if (GUI::isMenuOpen()) {
            static bool dragging = false;
            static ImVec2 dragStartMouse{};
            static float startOffX = 0.0f;
            static float startOffY = 0.0f;
            if (!dragging && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
                dragging = true;
                dragStartMouse = ImGui::GetIO().MousePos;
                startOffX = offX;
                startOffY = offY;
            }
            if (dragging) {
                if (!ImGui::IsMouseDown(0)) {
                    dragging = false;
                } else {
                    using Range = binds_list_vars::OffsetX::ValueType;
                    // positive X offset = LEFT of the anchor, positive Y offset = DOWN from it
                    const float newX = ImClamp(startOffX - (ImGui::GetIO().MousePos.x - dragStartMouse.x), Range::kMin, Range::kMax);
                    const float newY = ImClamp(startOffY + (ImGui::GetIO().MousePos.y - dragStartMouse.y), Range::kMin, Range::kMax);
                    static_cast<void>(ui_config::set<binds_list_vars::OffsetX>(Range{newX}));
                    static_cast<void>(ui_config::set<binds_list_vars::OffsetY>(Range{newY}));
                }
            }
        }

        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        const float winWidth = ImGui::GetWindowWidth();
        const float winHeight = ImGui::GetWindowHeight();

        // the menu's card depth: WIDE top-light wash fading down + 1px inner highlight
        d->PushClipRect(winPos, winPos + ImVec2(winWidth, winHeight), true);
        d->AddRectFilledMultiColor(winPos + ImVec2(s(14), s(1)), winPos + ImVec2(winWidth - s(14), s(42)), C(255, 255, 255, 18), C(255, 255, 255, 18), 0, 0);
        d->AddLine(winPos + ImVec2(s(12), s(0.75f)), winPos + ImVec2(winWidth - s(12), s(0.75f)), C(255, 255, 255, 22), 1.0f);

        // header band: accent dot + semibold caption + hairline divider
        pulsingDot(d, winPos + ImVec2(s(17), s(17)), s(2.4f), g_accent);
        textY(d, winPos.x + s(25), winPos.y + s(10), s(14), C(196, 199, 208), "KEYBINDS", kTextCaption, strongFont());
        d->AddLine(winPos + ImVec2(s(12), headerHeight - s(2)), winPos + ImVec2(winWidth - s(12), headerHeight - s(2)), kHairline, 1.0f);

        float y = winPos.y + headerHeight;
        const auto drawBindRow = [&](const char* label, int key) {
            d->AddLine(ImVec2(winPos.x + s(12), y), ImVec2(winPos.x + winWidth - s(12), y), kHairline, 1.0f);

            textY(d, winPos.x + s(14), y, rowHeight, kTextBodyCol, label, kTextControl, nullptr);

            const bool held = key != Bind::kOff && Bind::isDown(key);
            const float pillWidth = s(76.0f);
            const float pillHeight = s(21.0f);
            const ImVec2 pill(winPos.x + winWidth - s(13) - pillWidth, y + (rowHeight - pillHeight) * 0.5f);
            // soft accent halo while the key is held (the menu's lit-toggle recipe)
            if (held) {
                d->AddRectFilled(pill - ImVec2(s(3), s(3)), pill + ImVec2(pillWidth + s(3), pillHeight + s(3)), (g_buttonAccent & 0x00FFFFFFu) | (26u << IM_COL32_A_SHIFT), s(12));
                d->AddRectFilled(pill - ImVec2(s(6), s(6)), pill + ImVec2(pillWidth + s(6), pillHeight + s(6)), (g_buttonAccent & 0x00FFFFFFu) | (10u << IM_COL32_A_SHIFT), s(15));
            }
            d->AddRectFilled(pill, pill + ImVec2(pillWidth, pillHeight), held ? g_buttonAccent : kPillBg, pillHeight * 0.5f);
            d->AddRect(pill, pill + ImVec2(pillWidth, pillHeight), held ? g_buttonAccent : kPillBgHover, pillHeight * 0.5f);
            const char* keyName = Bind::displayName(key);
            const float keyWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, keyName).x;
            textY(d, pill.x + ImMax(s(6), (pillWidth - keyWidth) * 0.5f), pill.y, pillHeight, held ? kInsetBg : C(170, 173, 184), keyName, kTextSmall, nullptr);
            y += rowHeight;
        };

        for (int i = 0; i < rowCount; ++i)
            drawBindRow(rows[i].label, rows[i].key);
        d->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return listHeight;
}

// --- cheat o meter (in-game HUD overlay) -------------------------------------------------
// Binds-list-styled panel fed by the analyzer's game-thread snapshot: one row per scanned
// player - score pill (green/amber/red), name, and the raw columns behind the score (peak aim
// speed, snaps/min, accuracy, headshot rate, scan duration). This panel (with the keybind
// list) is the HUD's REFERENCE look - the combat counters/status chips/hit feed windows below
// share its chrome. Shows while the analyzer is
// enabled AND at least one target is selected ("waiting for data..." keeps picked-but-silent
// distinguishable from not-picked).
[[nodiscard]] static ImU32 cheatOMeterScoreColor(int score) noexcept
{
    if (score >= 70)
        return C(255, 71, 71);
    if (score >= 40)
        return C(255, 180, 60);
    return C(120, 243, 79);
}

float drawCheatOMeterWindow(float extraYOffset) noexcept
{
    if (!ui_config::get<analyzer_vars::Enabled>())
        return 0.0f;

    const auto snap = cheat_ometer::hudSnapshot();
    const int pending = cheat_ometer::selectedCount();
    if (snap.count <= 0 && pending <= 0)
        return 0.0f;

    constexpr ImGuiWindowFlags menuClosedFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing;
    constexpr ImGuiWindowFlags menuOpenFlags = (ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing);

    const float displayWidth = ImGui::GetIO().DisplaySize.x;
    const float windowWidth = s(288.0f);
    const float headerHeight = s(38.0f);
    const float rowHeight = s(40.0f);
    const int rowsDrawn = snap.count > 0 ? snap.count : 1;
    const float listHeight = headerHeight + static_cast<float>(rowsDrawn) * rowHeight + s(6.0f);

    // top-right, stacked under the keybind list
    ImGui::SetNextWindowPos(ImVec2(displayWidth - windowWidth - s(12.0f), s(52.0f) + extraYOffset), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(windowWidth, listHeight), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kSidebarBg);
    ImGui::PushStyleColor(ImGuiCol_Border, C(52, 52, 58, 220));

    if (ImGui::Begin("Cheat o meter", nullptr, GUI::isMenuOpen() ? menuOpenFlags : menuClosedFlags)) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        const float winWidth = ImGui::GetWindowWidth();
        const float winHeight = ImGui::GetWindowHeight();

        d->PushClipRect(winPos, winPos + ImVec2(winWidth, winHeight), true);
        d->AddRectFilledMultiColor(winPos + ImVec2(s(14), s(1)), winPos + ImVec2(winWidth - s(14), s(42)), C(255, 255, 255, 18), C(255, 255, 255, 18), 0, 0);
        d->AddLine(winPos + ImVec2(s(12), s(0.75f)), winPos + ImVec2(winWidth - s(12), s(0.75f)), C(255, 255, 255, 22), 1.0f);

        // centered title, per the panel's design - with the keybind list's pulsing accent dot
        // riding left of it so the "live scanner" reads at a glance
        const char* const title = "CHEAT O METER";
        const float titleWidth = ImGui::GetFont()->CalcTextSizeA(kTextCaption, FLT_MAX, 0.0f, title).x;
        const float titleX = winPos.x + (winWidth - titleWidth) * 0.5f;
        textY(d, titleX, winPos.y + s(10), s(14), C(196, 199, 208), title, kTextCaption, strongFont());
        pulsingDot(d, ImVec2(titleX - s(11), winPos.y + s(17)), s(2.4f), g_accent);
        d->AddLine(winPos + ImVec2(s(12), headerHeight - s(2)), winPos + ImVec2(winWidth - s(12), headerHeight - s(2)), kHairline, 1.0f);

        float y = winPos.y + headerHeight;
        if (snap.count <= 0) {
            textY(d, winPos.x + s(14), y + s(12), s(16), C(137, 142, 153), "waiting for data...", kTextControl, nullptr);
        }
        for (int i = 0; i < snap.count; ++i) {
            const auto& row = snap.rows[i];
            d->AddLine(ImVec2(winPos.x + s(12), y), ImVec2(winPos.x + winWidth - s(12), y), kHairline, 1.0f);

            // score pill
            const float pillRadius = s(12.0f);
            const ImVec2 pillCenter(winPos.x + s(24), y + rowHeight * 0.5f);
            const ImU32 scoreCol = cheatOMeterScoreColor(row.score);
            d->AddCircleFilled(pillCenter, pillRadius, (scoreCol & 0x00FFFFFFu) | (36u << IM_COL32_A_SHIFT));
            d->AddCircle(pillCenter, pillRadius, scoreCol, 0, s(1.6f));
            char scoreText[8];
            std::snprintf(scoreText, sizeof(scoreText), "%d", row.score);
            // centered on the measured GLYPH RUN, not the font line box - the line box rides
            // high inside a tight circle
            const ImVec2 scoreTs = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, scoreText);
            text(d, pillCenter - scoreTs * 0.5f, scoreCol, scoreText, kTextSmall, nullptr);

            // name (line 1) + muted stat columns (line 2)
            const float nameX = winPos.x + s(44);
            textY(d, nameX, y + s(4), s(16), kTextBodyCol, row.name, kTextControl, nullptr);

            char statsLine[96];
            if (row.sensitivity > 0.0f) {
                // the fitted sensitivity doubles as the vector's confidence readout
                std::snprintf(statsLine, sizeof(statsLine), "sens %.2f%s  snap %d/min%s",
                              row.sensitivity,
                              row.aimStrikes > 0 ? " OFF-GRID" : "",
                              row.snapsPerMin,
                              row.voiceStrikes > 0 ? " voice" : "");
            } else if (row.voiceStrikes > 0) {
                if (row.accPct >= 0 && row.hsPct >= 0)
                    std::snprintf(statsLine, sizeof(statsLine), "snap %d/min  acc %d%%  hs %d%%  voice %d", row.snapsPerMin, row.accPct, row.hsPct, row.voiceStrikes);
                else
                    std::snprintf(statsLine, sizeof(statsLine), "snap %d/min  voice %d", row.snapsPerMin, row.voiceStrikes);
            } else if (row.accPct >= 0 && row.hsPct >= 0)
                std::snprintf(statsLine, sizeof(statsLine), "snap %d/min  acc %d%%  hs %d%%", row.snapsPerMin, row.accPct, row.hsPct);
            else if (row.accPct >= 0)
                std::snprintf(statsLine, sizeof(statsLine), "snap %d/min  acc %d%%", row.snapsPerMin, row.accPct);
            else
                std::snprintf(statsLine, sizeof(statsLine), "snap %d/min  %d shots", row.snapsPerMin, row.shots);
            const bool aimFlagged = row.aimStrikes > 0;
            textY(d, nameX, y + s(21), s(14), aimFlagged ? cheatOMeterScoreColor(70) : row.voiceStrikes > 0 ? cheatOMeterScoreColor(40) : C(137, 142, 153), statsLine, kTextSmall, nullptr);

            // right column: peak aim speed (line 1) + scan duration (line 2)
            char speedText[24];
            std::snprintf(speedText, sizeof(speedText), "%d deg/s", row.peakSpeed);
            const float speedW = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, speedText).x;
            textY(d, winPos.x + winWidth - s(12) - speedW, y + s(4), s(16), C(196, 199, 208), speedText, kTextSmall, nullptr);

            char timeText[16];
            std::snprintf(timeText, sizeof(timeText), "%d:%02d", row.elapsed / 60, row.elapsed % 60);
            const float timeW = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, timeText).x;
            textY(d, winPos.x + winWidth - s(12) - timeW, y + s(21), s(14), C(137, 142, 153), timeText, kTextSmall, nullptr);

            y += rowHeight;
        }
        d->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return listHeight;
}

// --- lag-o-meter (in-game HUD overlay) ---------------------------------------------------
// The SERVER LAGGER card's live flood readout as a keybind-list-style window: offered KB/s and
// refused batches per second, read from the game thread's relaxed-atomic counters with
// present-thread 1s window snapshots (the row never writes back - no cross-thread state).
// Refused batches tint amber while the channel is saturated. Draggable with the mouse (menu
// open), position persisted through MeterOffsetX/Y like the keybind list.
float drawLagOMeterWindow(float extraYOffset) noexcept
{
    if (!ui_config::get<server_lagger_vars::MeterEnabled>())
        return 0.0f;

    constexpr ImGuiWindowFlags menuClosedFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing;
    // NoMove: the position is pinned from the config every frame; the drag below writes the
    // offsets the pin is derived from.
    constexpr ImGuiWindowFlags menuOpenFlags = (ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoMove);

    const float displayWidth = ImGui::GetIO().DisplaySize.x;
    const float windowWidth = s(210.0f);
    const float headerHeight = s(38.0f);
    const float rowHeight = s(30.0f);
    constexpr int kMeterRows = 4;
    const float listHeight = headerHeight + static_cast<float>(kMeterRows) * rowHeight + s(8.0f);

    const std::uint64_t offered = server_lagger::statsOfferedBytes.load(std::memory_order_relaxed);
    const std::uint64_t refused = server_lagger::statsRefusedEvents.load(std::memory_order_relaxed);
    const std::uint64_t tx = server_lagger::statsTxBytes.load(std::memory_order_relaxed);

    // time since the connection's tick counter last advanced (server freeze indicator: a
    // server choking on the flood stops sending snapshots - the gap grows)
    const long long tickGapMs = server_lagger::lastSeenTick.load(std::memory_order_relaxed) < 0
        ? -1
        : server_lagger::monotonicMs() - server_lagger::lastTickAdvanceMs.load(std::memory_order_relaxed);

    // present-thread-only window state (single caller per frame)
    static double windowStart = 0.0;
    static std::uint64_t windowOffered = 0, windowRefused = 0, windowTx = 0;
    static double bytesPerSec = 0.0;
    static double txPerSec = 0.0;
    static std::uint64_t refusedPerSec = 0;

    const double now = ImGui::GetTime();
    if (windowStart == 0.0) {
        windowStart = now;
        windowOffered = offered;
        windowRefused = refused;
    } else if (const double elapsed = now - windowStart; elapsed >= 1.0) {
        bytesPerSec = static_cast<double>(offered - windowOffered) / elapsed;
        refusedPerSec = static_cast<std::uint64_t>(static_cast<double>(refused - windowRefused) / elapsed + 0.5);
        txPerSec = static_cast<double>(tx - windowTx) / elapsed;
        windowStart = now;
        windowOffered = offered;
        windowRefused = refused;
        windowTx = tx;
    }

    char offeredText[24];
    if (bytesPerSec >= 1024.0 * 1024.0)
        std::snprintf(offeredText, sizeof(offeredText), "%.1f MB/s", bytesPerSec / (1024.0 * 1024.0));
    else
        std::snprintf(offeredText, sizeof(offeredText), "%.0f KB/s", bytesPerSec / 1024.0);
    char refusedText[24];
    std::snprintf(refusedText, sizeof(refusedText), "%llu/s", static_cast<unsigned long long>(refusedPerSec));
    char txText[24];
    std::snprintf(txText, sizeof(txText), "%.0f KB/s", txPerSec / 1024.0);
    char tickGapText[24];
    if (tickGapMs < 0)
        std::snprintf(tickGapText, sizeof(tickGapText), "no server");
    else
        std::snprintf(tickGapText, sizeof(tickGapText), "%.1fs", tickGapMs / 1000.0);
    // >= 0.5s without a server tick = the connection is choking (the freeze indicator)
    const bool tickStalled = tickGapMs >= 500;

    // top-right, pinned to the SCREEN corner + the user's drag offsets (player-list model:
    // position truth lives in the config, never in a stacking chain - the cheat o meter's
    // auto-fit height changing used to drag this window around on every row change)
    const float offX = static_cast<float>(ui_config::get<server_lagger_vars::MeterOffsetX>());
    const float offY = static_cast<float>(ui_config::get<server_lagger_vars::MeterOffsetY>());
    ImGui::SetNextWindowPos(ImVec2(displayWidth - windowWidth - s(12.0f) - offX, s(52.0f) + offY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth, listHeight), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kSidebarBg);
    ImGui::PushStyleColor(ImGuiCol_Border, C(52, 52, 58, 220));

    if (ImGui::Begin("Lag-O-Meter", nullptr, GUI::isMenuOpen() ? menuOpenFlags : menuClosedFlags)) {
        // drag to reposition: the window body is pure draw-list content (no items), so hover
        // anywhere on it; the drag writes the offsets the pinned position is derived from.
        if (GUI::isMenuOpen()) {
            static bool dragging = false;
            static ImVec2 dragStartMouse{};
            static float startOffX = 0.0f;
            static float startOffY = 0.0f;
            if (!dragging && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
                dragging = true;
                dragStartMouse = ImGui::GetIO().MousePos;
                startOffX = offX;
                startOffY = offY;
            }
            if (dragging) {
                if (!ImGui::IsMouseDown(0)) {
                    dragging = false;
                } else {
                    using Range = server_lagger_vars::MeterOffsetX::ValueType;
                    // positive X offset = LEFT of the anchor, positive Y offset = DOWN from it
                    const float newX = ImClamp(startOffX - (ImGui::GetIO().MousePos.x - dragStartMouse.x), Range::kMin, Range::kMax);
                    const float newY = ImClamp(startOffY + (ImGui::GetIO().MousePos.y - dragStartMouse.y), Range::kMin, Range::kMax);
                    static_cast<void>(ui_config::set<server_lagger_vars::MeterOffsetX>(Range{newX}));
                    static_cast<void>(ui_config::set<server_lagger_vars::MeterOffsetY>(Range{newY}));
                }
            }
        }

        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        const float winWidth = ImGui::GetWindowWidth();
        const float winHeight = ImGui::GetWindowHeight();

        // the menu's card depth: WIDE top-light wash fading down + 1px inner highlight, header
        // band with accent dot + caption + hairline divider (the keybind list recipe)
        d->PushClipRect(winPos, winPos + ImVec2(winWidth, winHeight), true);
        d->AddRectFilledMultiColor(winPos + ImVec2(s(14), s(1)), winPos + ImVec2(winWidth - s(14), s(42)), C(255, 255, 255, 18), C(255, 255, 255, 18), 0, 0);
        d->AddLine(winPos + ImVec2(s(12), s(0.75f)), winPos + ImVec2(winWidth - s(12), s(0.75f)), C(255, 255, 255, 22), 1.0f);

        pulsingDot(d, winPos + ImVec2(s(17), s(17)), s(2.4f), g_accent);
        textY(d, winPos.x + s(25), winPos.y + s(10), s(14), C(196, 199, 208), "LAG-O-METER", kTextCaption, strongFont());
        d->AddLine(winPos + ImVec2(s(12), headerHeight - s(2)), winPos + ImVec2(winWidth - s(12), headerHeight - s(2)), kHairline, 1.0f);

        struct MeterRow {
            const char* label;
            const char* value;
            ImU32 color;
        };
        const MeterRow meterRows[kMeterRows]{
            {"OFFERED", offeredText, C(196, 199, 208)},
            {"REFUSED", refusedText, refusedPerSec > 0 ? C(232, 173, 96) : C(150, 153, 163)},
            {"TX", txText, txPerSec > 0 ? C(110, 215, 135) : C(150, 153, 163)},
            {"TICK GAP", tickGapText, tickGapMs < 0 ? C(150, 153, 163) : tickStalled ? C(232, 96, 96) : C(150, 153, 163)},
        };

        float y = winPos.y + headerHeight;
        for (const auto& row : meterRows) {
            d->AddLine(ImVec2(winPos.x + s(12), y), ImVec2(winPos.x + winWidth - s(12), y), kHairline, 1.0f);
            textY(d, winPos.x + s(14), y, rowHeight, kTextBodyCol, row.label, kTextControl, nullptr);
            const float valueWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, row.value).x;
            textY(d, winPos.x + winWidth - s(13) - valueWidth, y, rowHeight, row.color, row.value, kTextControl, nullptr);
            y += rowHeight;
        }
        d->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return listHeight;
}

// --- combat hud (hit counters / status chips / hit feed) ----------------------------------
// The old Panorama meter boxes, ported onto the ImGui HUD windows' ONE visual language (the
// keybinds list / cheat o meter look): same box chrome (top-light wash, pulsing accent dot,
// caption title, hairline dividers), same value-pill recipe, same drag model. Counters + feed
// draw the CombatStats game-thread snapshot (CombatStatsHudState.h); the status chips are
// computed right here - config vars + Bind/SDL polls are safe on this thread (the keybind
// list already does the same). Each box's drag writes the SAME offsets the Hud-page sliders
// hold (single position truth, autosave persists them).

[[nodiscard]] static double hudMonotonicSeconds() noexcept
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1.0e-9;
}

[[nodiscard]] static ImU32 hudWithAlpha(ImU32 color, float scale) noexcept
{
    const int a = static_cast<int>(static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * scale);
    return (color & 0x00FFFFFFu) | (ImClamp(a, 0, 255) << IM_COL32_A_SHIFT);
}

// The binds-list box chrome: wide top-light wash + 1px inner highlight, centered caption title
// with the pulsing accent dot riding left of it, hairline divider under the header band.
static void drawHudBoxHeader(ImDrawList* d, const ImVec2& winPos, float winWidth, float headerHeight, const char* title) noexcept
{
    d->AddRectFilledMultiColor(winPos + ImVec2(s(14), s(1)), winPos + ImVec2(winWidth - s(14), s(42)), C(255, 255, 255, 18), C(255, 255, 255, 18), 0, 0);
    d->AddLine(winPos + ImVec2(s(12), s(0.75f)), winPos + ImVec2(winWidth - s(12), s(0.75f)), C(255, 255, 255, 22), 1.0f);

    const float titleWidth = ImGui::GetFont()->CalcTextSizeA(kTextCaption, FLT_MAX, 0.0f, title).x;
    const float titleX = winPos.x + (winWidth - titleWidth) * 0.5f;
    textY(d, titleX, winPos.y + s(10), s(14), C(196, 199, 208), title, kTextCaption, strongFont());
    pulsingDot(d, ImVec2(titleX - s(11), winPos.y + s(17)), s(2.4f), g_accent);
    d->AddLine(winPos + ImVec2(s(12), headerHeight - s(2)), winPos + ImVec2(winWidth - s(12), headerHeight - s(2)), kHairline, 1.0f);
}

struct HudWindowDragState {
    bool dragging{false};
    ImVec2 dragStartMouse{};
    float startOffX{0.0f};
    float startOffY{0.0f};
};

// Shared drag: the window position is pinned from the config offsets every frame (ImGui's own
// mover is disabled), so the drag converts cursor deltas back into the SAME offsets the Hud
// sliders write. kFromBottom boxes anchor on the screen's bottom edge - dragging UP grows the
// offset. A hairline accent outline marks the hovered/dragged box so it reads as grabbable.
template <typename OffsetXVar, typename OffsetYVar, bool kFromBottom>
static void dragHudWindow(HudWindowDragState& st, float offX, float offY, ImDrawList* d, const ImVec2& winPos, float winWidth, float winHeight) noexcept
{
    if (!GUI::isMenuOpen()) {
        st.dragging = false;
        return;
    }
    const bool hovered = ImGui::IsWindowHovered();
    if (!st.dragging && hovered && ImGui::IsMouseClicked(0)) {
        st.dragging = true;
        st.dragStartMouse = ImGui::GetIO().MousePos;
        st.startOffX = offX;
        st.startOffY = offY;
    }
    if (st.dragging) {
        if (!ImGui::IsMouseDown(0)) {
            st.dragging = false;
        } else {
            const float dx = ImGui::GetIO().MousePos.x - st.dragStartMouse.x;
            const float dy = ImGui::GetIO().MousePos.y - st.dragStartMouse.y;
            using RangeX = typename OffsetXVar::ValueType;
            using RangeY = typename OffsetYVar::ValueType;
            const float newOffX = ImClamp(st.startOffX + dx, static_cast<float>(RangeX::kMin), static_cast<float>(RangeX::kMax));
            const float newOffY = ImClamp(kFromBottom ? st.startOffY - dy : st.startOffY + dy, static_cast<float>(RangeY::kMin), static_cast<float>(RangeY::kMax));
            static_cast<void>(ui_config::set<OffsetXVar>(RangeX{static_cast<typename RangeX::ValueType>(newOffX)}));
            static_cast<void>(ui_config::set<OffsetYVar>(RangeY{static_cast<typename RangeY::ValueType>(newOffY)}));
        }
    }
    if (hovered || st.dragging)
        d->AddRect(winPos, winPos + ImVec2(winWidth, winHeight), (g_accent & 0x00FFFFFFu) | ((st.dragging ? 220u : 110u) << IM_COL32_A_SHIFT), s(10.0f), 0, st.dragging ? 1.8f : 1.4f);
}

constexpr ImGuiWindowFlags kHudBoxMenuClosedFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove
    | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse
    | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing;
constexpr ImGuiWindowFlags kHudBoxMenuOpenFlags = (ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav
    | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing
    | ImGuiWindowFlags_NoMove);

// One value pill inside a box: dark fill + hairline border, muted label + colored value - the
// keybind list's key-pill recipe with the feed's two-level text hierarchy. Returns the pill's
// width so callers can lay out rows and size the window from the same measurement.
static float drawHudValuePill(ImDrawList* d, float x, float y, float pillHeight, const char* label, const char* value, ImU32 valueColor) noexcept
{
    const float labelWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, label).x;
    const float valueWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, value).x;
    const float pillWidth = labelWidth + s(4) + valueWidth + s(16);
    const ImVec2 pillMin{x, y};
    const ImVec2 pillMax{x + pillWidth, y + pillHeight};
    d->AddRectFilled(pillMin, pillMax, kPillBg, pillHeight * 0.5f);
    d->AddRect(pillMin, pillMax, kPillBgHover, pillHeight * 0.5f);
    textY(d, x + s(8), y, pillHeight, C(137, 142, 153), label, kTextSmall, nullptr);
    textY(d, x + s(8) + labelWidth + s(4), y, pillHeight, valueColor, value, kTextSmall, nullptr);
    return pillWidth;
}

// --- now playing (in-game HUD overlay) ---------------------------------------------------
// Bottom-left box in the same visual family as COMBAT/STATUS. While a web-radio station plays
// it shows the station + current track (host-side ICY burst probe via RadioManager); while
// nothing plays it mirrors the host desktop's MPRIS media players (playerctl burst) instead,
// so the element is never dead weight. Hidden entirely when the toggle is off or nothing is
// playing anywhere. Interactive only while the menu is open (drag to reposition).

static void copyCapped(char* dst, const char* src, std::size_t cap) noexcept
{
    std::size_t i = 0;
    for (; src && src[i] != '\0' && i < cap - 1; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

// Copies `text` into `buf`, cutting it down to maxWidth HUD pixels (kTextControl font) with a
// trailing "..." - station and track names regularly overflow the 232px box.
void truncateToWidth(char* buf, std::size_t cap, const char* text, float maxWidth) noexcept
{
    std::size_t len = 0;
    while (text[len] != '\0' && len + 4 < cap) {
        buf[len] = text[len];
        ++len;
    }
    buf[len] = '\0';
    ImFont* font = ImGui::GetFont();
    if (font->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, buf).x <= maxWidth)
        return;
    while (len > 0) {
        --len;
        buf[len] = '.';
        buf[len + 1] = '.';
        buf[len + 2] = '.';
        buf[len + 3] = '\0';
        if (font->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, buf).x <= maxWidth)
            return;
    }
}

[[nodiscard]] bool stageTextureFromFile(const char* path, bool music) noexcept;

void drawNowPlayingWindow(float combatListHeight) noexcept
{
    static_cast<void>(combatListHeight);
    if (!ui_config::get<radio_vars::ShowNowPlaying>())
        return;

    bool playing = false, paused = false;
    const char *stationName = nullptr, *track = nullptr, *playerTitle = nullptr, *playerArtist = nullptr;
    const char *artworkUrl = nullptr, *artworkFile = nullptr;
    withRadio([&](auto&& radio) {
        playing = radio.isPlaying();
        stationName = radio.lastPlayedName();
        track = radio.nowPlayingTrack();
        playerTitle = radio.mprisTrack();
        playerArtist = radio.mprisArtist();
        paused = radio.mprisIsPaused();
        artworkUrl = radio.mprisArtwork();
        artworkFile = radio.mprisArtworkFile();
    });

    char title[160]{}, artist[160]{};
    const bool useMpris = playerTitle && playerTitle[0] && (!playing || !paused);
    if (useMpris) {
        copyCapped(title, playerTitle, sizeof(title));
        if (playerArtist)
            copyCapped(artist, playerArtist, sizeof(artist));
    } else if (playing) {
        paused = false;
        copyCapped(title, track && track[0] ? track : (stationName && stationName[0] ? stationName : "Live radio"), sizeof(title));
        if (track && track[0] && stationName)
            copyCapped(artist, stationName, sizeof(artist));
    } else {
        return;
    }

    static char previousArtwork[512]{};
    static bool artworkStaged = false;
    const char* artwork = useMpris && artworkUrl ? artworkUrl : "";
    if (std::strcmp(previousArtwork, artwork) != 0) {
        VulkanHook::music_texture::release();
        copyCapped(previousArtwork, artwork, sizeof(previousArtwork));
        artworkStaged = false;
    }
    if (!artworkStaged && artwork[0] && artworkFile) {
        static_cast<void>(stageTextureFromFile(artworkFile, true));
        artworkStaged = true; // Unsupported artwork is attempted once, not every frame.
    }

    const auto display = ImGui::GetIO().DisplaySize;
    const float width = s(280.0f), height = s(56.0f), tile = s(36.0f);
    static HudWindowDragState dragState;
    const float offX = static_cast<float>(ui_config::get<radio_vars::NowPlayingOffsetX>());
    const float offY = static_cast<float>(ui_config::get<radio_vars::NowPlayingOffsetY>());
    ImGui::SetNextWindowPos(ImVec2(ImClamp(s(16) + offX, 0.0f, ImMax(0.0f, display.x - width)),
        ImClamp(display.y - s(16) - offY - height, 0.0f, ImMax(0.0f, display.y - height))), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, C(18, 19, 24, 220));
    ImGui::PushStyleColor(ImGuiCol_Border, C(255, 255, 255, 20));
    if (ImGui::Begin("Now playing", nullptr, GUI::isMenuOpen() ? kHudBoxMenuOpenFlags : kHudBoxMenuClosedFlags)) {
        auto* d = ImGui::GetWindowDrawList();
        const auto pos = ImGui::GetWindowPos();
        const auto icon = pos + ImVec2(s(10), s(10));
        const auto texture = useMpris ? reinterpret_cast<ImTextureID>(VulkanHook::music_texture::query()) : ImTextureID{};
        if (texture) {
            d->AddImageRounded(texture, icon, icon + ImVec2(tile, tile), ImVec2(0, 0), ImVec2(1, 1), C(255, 255, 255), s(5));
        } else {
            d->AddRectFilled(icon, icon + ImVec2(tile, tile), C(255, 255, 255, 8), s(5));
            // Draw the music note directly so it never depends on an icon font.
            const auto ink = paused ? C(125, 128, 140) : g_accent;
            d->AddLine(icon + ImVec2(s(15), s(24)), icon + ImVec2(s(15), s(11)), ink, s(2));
            d->AddLine(icon + ImVec2(s(15), s(11)), icon + ImVec2(s(25), s(9)), ink, s(2));
            d->AddLine(icon + ImVec2(s(25), s(9)), icon + ImVec2(s(25), s(22)), ink, s(2));
            d->AddEllipseFilled(icon + ImVec2(s(12), s(25)), ImVec2(s(4), s(3)), ink);
            d->AddEllipseFilled(icon + ImVec2(s(22), s(23)), ImVec2(s(4), s(3)), ink);
        }
        const float x = pos.x + s(56), textWidth = width - s(paused ? 82 : 68);
        truncateToWidth(title, sizeof(title), title, textWidth);
        truncateToWidth(artist, sizeof(artist), artist, textWidth);
        textY(d, x, pos.y + s(artist[0] ? 8 : 18), s(20), C(235, 237, 242), title, kTextControl, nullptr);
        if (artist[0])
            textY(d, x, pos.y + s(29), s(16), C(145, 148, 160), artist, kTextSmall, nullptr);
        if (paused) {
            const auto p = pos + ImVec2(width - s(20), s(23));
            d->AddRectFilled(p, p + ImVec2(s(2), s(10)), C(145, 148, 160), s(1));
            d->AddRectFilled(p + ImVec2(s(5), 0), p + ImVec2(s(7), s(10)), C(145, 148, 160), s(1));
        }
        dragHudWindow<radio_vars::NowPlayingOffsetX, radio_vars::NowPlayingOffsetY, true>(dragState, offX, offY, d, pos, width, height);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

// Bottom-left HITS / MISS / ACC counters, above the status chips box.
float drawCombatCountersWindow() noexcept
{
    if (!combat_stats_hud::hudLive.load(std::memory_order_relaxed))
        return 0.0f;
    const auto snap = combat_stats_hud::snapshot();

    char hitsText[16];
    std::snprintf(hitsText, sizeof(hitsText), "%u", snap.hitsLanded);
    char missText[16];
    std::snprintf(missText, sizeof(missText), "%u", snap.shotsFired >= snap.hitsLanded ? snap.shotsFired - snap.hitsLanded : 0u);
    char accText[16];
    if (snap.shotsFired == 0)
        std::snprintf(accText, sizeof(accText), "--");   // never reads as 0% skill
    else
        std::snprintf(accText, sizeof(accText), "%u%%", static_cast<unsigned>((static_cast<std::uint64_t>(snap.hitsLanded) * 100) / snap.shotsFired));

    struct CounterPill { const char* label; const char* value; ImU32 valueColor; };
    const CounterPill pills[]{
        {"HITS", hitsText, g_accent},
        {"MISS", missText, C(235, 95, 80)},
        {"ACC", accText, g_accent},
    };

    const float headerHeight = s(38.0f);
    const float rowHeight = s(36.0f);
    const float pillHeight = s(24.0f);
    const float listHeight = headerHeight + rowHeight + s(12.0f);

    float windowWidth = s(24.0f);
    for (const auto& pill : pills) {
        const float labelWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, pill.label).x;
        const float valueWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, pill.value).x;
        windowWidth += labelWidth + s(4) + valueWidth + s(16) + s(6);
    }
    windowWidth -= s(6); // no trailing gap after the last pill

    static HudWindowDragState dragState;
    const float offX = static_cast<float>(ui_config::get<combat_stats_vars::CountersOffsetX>());
    const float offY = static_cast<float>(ui_config::get<combat_stats_vars::CountersOffsetY>());
    const float displayHeight = ImGui::GetIO().DisplaySize.y;
    // bottom-left anchor, parked above the STATUS box (its anchor + box height + gap)
    ImGui::SetNextWindowPos(ImVec2(s(10.0f) + offX, displayHeight - s(422.0f) - offY - listHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth, listHeight), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kSidebarBg);
    ImGui::PushStyleColor(ImGuiCol_Border, C(52, 52, 58, 220));

    if (ImGui::Begin("Combat counters", nullptr, GUI::isMenuOpen() ? kHudBoxMenuOpenFlags : kHudBoxMenuClosedFlags)) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        const float winWidth = ImGui::GetWindowWidth();
        const float winHeight = ImGui::GetWindowHeight();

        drawHudBoxHeader(d, winPos, winWidth, headerHeight, "COMBAT");

        float x = winPos.x + s(12);
        const float pillY = winPos.y + headerHeight + (rowHeight - pillHeight) * 0.5f;
        for (const auto& pill : pills)
            x += drawHudValuePill(d, x, pillY, pillHeight, pill.label, pill.value, pill.valueColor) + s(6);

        dragHudWindow<combat_stats_vars::CountersOffsetX, combat_stats_vars::CountersOffsetY, true>(dragState, offX, offY, d, winPos, winWidth, winHeight);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return listHeight;
}

// Bottom-left STATUS chips: AIM (legit aim assist), TRIG (triggerbot), BLOCK (blockbot) - the
// "why is nothing happening?" answer at a glance. Bright green = enabled AND its key is held,
// dim green = enabled but idle, gray = disabled; the held state gets the keybind list's
// lit-toggle halo in green.
float drawStatusChipsWindow() noexcept
{
    if (!combat_stats_hud::hudLive.load(std::memory_order_relaxed))
        return 0.0f;

    constexpr ImU32 kHeldGreen = C(163, 212, 31);
    constexpr ImU32 kIdleGreen = C(81, 105, 20);
    constexpr ImU32 kOffGray = C(110, 110, 110);

    const struct {
        bool enabled;
        bool held;
        const char* text;
    } chips[]{
        {ui_config::get<legit_aimbot_vars::Enabled>(), Bind::isDown(static_cast<int>(static_cast<legit_aimbot_vars::AimKey::ValueType::ValueType>(ui_config::get<legit_aimbot_vars::AimKey>()))), "AIM"},
        {ui_config::get<triggerbot_vars::Enabled>(), Bind::isDown(static_cast<int>(static_cast<triggerbot_vars::HoldKey::ValueType::ValueType>(ui_config::get<triggerbot_vars::HoldKey>()))), "TRIG"},
        {ui_config::get<BlockbotEnabled>(), KeyboardState::isKeyDown(sdl3::scancode::kE), "BLOCK"},
    };

    const float headerHeight = s(38.0f);
    const float rowHeight = s(36.0f);
    const float pillHeight = s(24.0f);
    const float listHeight = headerHeight + rowHeight + s(12.0f);

    float windowWidth = s(24.0f);
    for (const auto& chip : chips)
        windowWidth += ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, chip.text).x + s(18) + s(6);
    windowWidth -= s(6);

    static HudWindowDragState dragState;
    const float offX = static_cast<float>(ui_config::get<status_panel_vars::OffsetX>());
    const float offY = static_cast<float>(ui_config::get<status_panel_vars::OffsetY>());
    const float displayHeight = ImGui::GetIO().DisplaySize.y;
    // bottom-left anchor, just above the vanilla money HUD + chat feed
    ImGui::SetNextWindowPos(ImVec2(s(10.0f) + offX, displayHeight - s(330.0f) - offY - listHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth, listHeight), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kSidebarBg);
    ImGui::PushStyleColor(ImGuiCol_Border, C(52, 52, 58, 220));

    if (ImGui::Begin("Status chips", nullptr, GUI::isMenuOpen() ? kHudBoxMenuOpenFlags : kHudBoxMenuClosedFlags)) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        const float winWidth = ImGui::GetWindowWidth();
        const float winHeight = ImGui::GetWindowHeight();

        drawHudBoxHeader(d, winPos, winWidth, headerHeight, "STATUS");

        float x = winPos.x + s(12);
        const float pillY = winPos.y + headerHeight + (rowHeight - pillHeight) * 0.5f;
        for (const auto& chip : chips) {
            const ImU32 color = chip.enabled ? (chip.held ? kHeldGreen : kIdleGreen) : kOffGray;
            const float textWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, chip.text).x;
            const float pillWidth = textWidth + s(18);
            const ImVec2 pillMin{x, pillY};
            const ImVec2 pillMax{x + pillWidth, pillY + pillHeight};
            if (chip.enabled && chip.held) {
                // soft green halo while the feature is actively working (the lit-toggle recipe)
                d->AddRectFilled(pillMin - ImVec2(s(3), s(3)), pillMax + ImVec2(s(3), s(3)), (kHeldGreen & 0x00FFFFFFu) | (26u << IM_COL32_A_SHIFT), pillHeight * 0.5f + s(3));
                d->AddRectFilled(pillMin - ImVec2(s(6), s(6)), pillMax + ImVec2(s(6), s(6)), (kHeldGreen & 0x00FFFFFFu) | (10u << IM_COL32_A_SHIFT), pillHeight * 0.5f + s(6));
            }
            d->AddRectFilled(pillMin, pillMax, kPillBg, pillHeight * 0.5f);
            d->AddRect(pillMin, pillMax, chip.enabled && chip.held ? kHeldGreen : kPillBgHover, pillHeight * 0.5f);
            textY(d, x + s(9), pillY, pillHeight, color, chip.text, kTextSmall, chip.enabled && chip.held ? strongFont() : nullptr);
            x += pillWidth + s(6);
        }

        dragHudWindow<status_panel_vars::OffsetX, status_panel_vars::OffsetY, true>(dragState, offX, offY, d, winPos, winWidth, winHeight);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return listHeight;
}

// Top-left hit feed under the radar: one row per event ("hit <name> for <dmg> in head" /
// "killed <name>" / "missed xN"), faint verbs + accent names/damage, each row sliding/fading
// in on spawn and expiring after the configured lifetime. Hidden while there is nothing to
// show (the keybind list's "no rows = no window" rule).

// Composes one feed row's colored segments (into `storage`, since the texts are built, not
// referenced); returns the segment count.
struct FeedSegmentDraft {
    const char* text;
    bool accent;
};

static int feedSegments(const combat_stats_hud::FeedEntry& entry, FeedSegmentDraft* segments, char* storage) noexcept
{
    char* write = storage;
    auto put = [&](const char* text) {
        const auto len = std::strlen(text) + 1;
        std::memcpy(write, text, len);
        const char* result = write;
        write += len;
        return result;
    };
    int count = 0;
    switch (entry.kind) {
    case 'h':
        segments[count++] = {put("hit"), false};
        segments[count++] = {put(entry.name), true};
        segments[count++] = {put("for"), false};
        {
            char damage[16];
            std::snprintf(damage, sizeof(damage), "%d", entry.damage);
            segments[count++] = {put(damage), true};
        }
        segments[count++] = {put(entry.headshot ? "in head" : "dmg"), false};
        break;
    case 'k':
        segments[count++] = {put("killed"), false};
        segments[count++] = {put(entry.name), true};
        break;
    default: // 'm'
        {
            segments[count++] = {put("missed"), false};
            char streak[16];
            std::snprintf(streak, sizeof(streak), "x%d", entry.missCount);
            segments[count++] = {put(streak), false};
        }
        break;
    }
    return count;
}

float drawHitFeedWindow() noexcept
{
    if (!combat_stats_hud::hudLive.load(std::memory_order_relaxed))
        return 0.0f;
    const auto snap = combat_stats_hud::snapshot();
    const float lifetime = static_cast<float>(ui_config::get<combat_stats_vars::FeedLifetime>());
    const double now = hudMonotonicSeconds();

    struct FeedRow {
        const combat_stats_hud::FeedEntry* entry;
        float alpha; // entrance ease * expiry fade
        float slide; // px offset the row still has to travel up
    };
    FeedRow rows[combat_stats_hud::kFeedLines];
    int rowCount = 0;
    float maxRowWidth = 0.0f;
    char storage[combat_stats_hud::kFeedLines][5 * 48];
    FeedSegmentDraft segments[combat_stats_hud::kFeedLines][5];
    int segmentCounts[combat_stats_hud::kFeedLines]{};
    for (int i = 0; i < combat_stats_hud::kFeedLines; ++i) {
        const auto& entry = snap.feedEntries[i];
        if (entry.kind == 0)
            continue;
        const double age = now - entry.spawnTime;
        if (age >= lifetime + combat_stats_hud::kFeedSlideSeconds)
            continue;
        float fade = 1.0f;
        if (age > lifetime)
            fade = 1.0f - static_cast<float>((age - lifetime) / combat_stats_hud::kFeedSlideSeconds);
        float t = static_cast<float>(age / combat_stats_hud::kFeedSlideSeconds);
        t = ImClamp(t, 0.0f, 1.0f);
        const float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        segmentCounts[rowCount] = feedSegments(entry, segments[rowCount], storage[rowCount]);
        float width = 0.0f;
        for (int j = 0; j < segmentCounts[rowCount]; ++j) {
            if (j > 0)
                width += s(5.0f);
            width += ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, segments[rowCount][j].text).x;
        }
        maxRowWidth = ImMax(maxRowWidth, width);
        rows[rowCount++] = {&entry, ease * fade, combat_stats_hud::kFeedSlidePixels * (1.0f - ease)};
    }
    if (rowCount == 0)
        return 0.0f;

    const float headerHeight = s(38.0f);
    const float rowHeight = s(28.0f);
    const float listHeight = headerHeight + static_cast<float>(rowCount) * rowHeight + s(8.0f);
    const float windowWidth = ImMax(s(180.0f), maxRowWidth + s(24.0f));

    static HudWindowDragState dragState;
    const float offX = static_cast<float>(ui_config::get<combat_stats_vars::FeedOffsetX>());
    const float offY = static_cast<float>(ui_config::get<combat_stats_vars::FeedOffsetY>());
    // top-left anchor, under the radar
    ImGui::SetNextWindowPos(ImVec2(s(10.0f) + offX, s(330.0f) + offY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowWidth, listHeight), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kSidebarBg);
    ImGui::PushStyleColor(ImGuiCol_Border, C(52, 52, 58, 220));

    if (ImGui::Begin("Hit feed", nullptr, GUI::isMenuOpen() ? kHudBoxMenuOpenFlags : kHudBoxMenuClosedFlags)) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        const float winWidth = ImGui::GetWindowWidth();
        const float winHeight = ImGui::GetWindowHeight();

        drawHudBoxHeader(d, winPos, winWidth, headerHeight, "HIT FEED");

        float y = winPos.y + headerHeight;
        for (int i = 0; i < rowCount; ++i) {
            d->AddLine(ImVec2(winPos.x + s(12), y), ImVec2(winPos.x + winWidth - s(12), y), kHairline, 1.0f);

            const float rowY = y + s(5) + rows[i].slide;
            float x = winPos.x + s(12);
            for (int j = 0; j < segmentCounts[i]; ++j) {
                const auto& segment = segments[i][j];
                if (segment.text[0] == '\0')
                    continue;
                const ImU32 base = segment.accent ? g_accent : C(158, 162, 173);
                textY(d, x, rowY, rowHeight - s(8), hudWithAlpha(base, rows[i].alpha), segment.text, kTextSmall, nullptr);
                x += ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, segment.text).x + s(5.0f);
            }
            y += rowHeight;
        }

        dragHudWindow<combat_stats_vars::FeedOffsetX, combat_stats_vars::FeedOffsetY, false>(dragState, offX, offY, d, winPos, winWidth, winHeight);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return listHeight;
}

// --- player list (FrameworkCS2 port) -----------------------------------------------------
// Present-thread ImGui table fed by PlayerList's game-thread snapshot. Interactive only while
// the menu is open; otherwise a click-through always-on-back display.

void drawPlayerListWindow() noexcept
{
    if (!ui_config::get<PlayerListEnabled>())
        return;

    // ALWAYS auto-resize: the window fits its rows every frame - growing with the player count
    // and shrinking back - while the width stays pinned by the SetNextWindowSize below (a 0
    // height with Cond_Always is the auto-fit-height trick). The config stores the position:
    // seeded on first use, written back while the menu drag moves the window (no ini file in
    // the embedded ImGui, so the config is the only persistence).
    constexpr ImGuiWindowFlags menuClosedFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_AlwaysAutoResize;
    constexpr ImGuiWindowFlags menuOpenFlags = menuClosedFlags | ImGuiWindowFlags_NoMove;

    // Pinned to the offsets every frame (the watermark's model): position truth lives in the
    // two sliders/config, never in a drag. Offset 0 = the window is FLUSH with the screen
    // border (older builds baked a hidden 10/64px margin into the base - that read as "offset 0
    // is not at the border").
    const float posX = static_cast<float>(ui_config::get<PlayerListOffsetX>());
    const float posY = static_cast<float>(ui_config::get<PlayerListOffsetY>());
    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);

    // Same shell palette as the menu: dark rounded panel, theme accent, muted text. The window
    // auto-fits BOTH axes and the table uses content-fit columns, so long names/teams widen the
    // window instead of truncating ("Te...", "Mo..." cells).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s(8), s(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(s(10), s(5)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kShellBg);
    ImGui::PushStyleColor(ImGuiCol_Border, C(30, 30, 33, 0));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, kCardBg);
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, C(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, C(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, C(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, C(255, 255, 255, 6));

    if (ImGui::Begin("Player list", nullptr, GUI::isMenuOpen() ? menuOpenFlags : menuClosedFlags)) {
        const auto snap = player_list::snapshot();

        // Muted header text; a theme-accent underline sits under the header row.
        ImGui::PushStyleColor(ImGuiCol_Text, C(137, 142, 153));
        if (ImGui::BeginTable("##player_list_rows", 8,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Team");
            ImGui::TableSetupColumn("Health");
            ImGui::TableSetupColumn("Money");
            ImGui::TableSetupColumn("Ping");
            ImGui::TableSetupColumn("Rank");
            ImGui::TableSetupColumn("TD");
            ImGui::TableSetupColumn("Status");
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            // theme-accent hairline under the header row, spanning the full table width
            // (window-relative: after TableHeadersRow the cursor sits right below the header)
            {
                ImDrawList* d = ImGui::GetWindowDrawList();
                const ImVec2 winPos = ImGui::GetWindowPos();
                const float underlineY = winPos.y + ImGui::GetCursorPosY() - s(1.0f);
                const float left = winPos.x + s(3.0f);
                const float right = winPos.x + ImGui::GetWindowWidth() - s(3.0f);
                const ImU32 underline = (g_accent & 0x00FFFFFFu) | (200u << IM_COL32_A_SHIFT);
                d->AddLine(ImVec2(left, underlineY), ImVec2(right, underlineY), underline, 1.2f);
            }

            if (snap.count == 0) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, C(110, 114, 124));
                ImGui::TextUnformatted("no players");
                ImGui::PopStyleColor();
                for (int c = 0; c < 7; ++c) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted("");
                }
            }

            for (int i = 0; i < snap.count; ++i) {
                const auto& row = snap.rows[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (row.isLocalPlayer)
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(g_accent), "%s", row.name);
                else
                    ImGui::TextUnformatted(row.name);
                ImGui::TableNextColumn();
                if (row.team == 3)
                    ImGui::TextColored(ImVec4{0.47f, 0.63f, 1.0f, 1.0f}, "CT");
                else if (row.team == 2)
                    ImGui::TextColored(ImVec4{0.87f, 0.58f, 0.22f, 1.0f}, "T");
                else
                    ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                // health: red -> green as it fills up (same green as the shell's success accents)
                const float healthFraction = row.maxHealth > 0 ? ImSaturate(static_cast<float>(row.health) / row.maxHealth) : 0.0f;
                const ImVec4 healthColor = ImGui::ColorConvertU32ToFloat4(mix(C(235, 87, 87), C(81, 192, 124), healthFraction));
                ImGui::TextColored(healthColor, "%d/%d", row.health, row.maxHealth > 0 ? row.maxHealth : 100);
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.money);
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.ping);
                ImGui::TableNextColumn();
                if (row.rank > 0) {
                    if (row.rankType == 0xb)
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(g_accent), "%d", row.rank); // premier rating
                    else
                        ImGui::Text("#%d", row.rank);
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableNextColumn();
                if (row.teamDamage > 0)
                    ImGui::TextColored(ImVec4{1.0f, 0.76f, 0.27f, 1.0f}, "%d", row.teamDamage);
                else
                    ImGui::TextDisabled("0");
                ImGui::TableNextColumn();
                if (!row.alive) {
                    if (row.observerMode >= 0)
                        ImGui::TextColored(ImVec4{0.85f, 0.5f, 0.9f, 1.0f}, "OBS %d", row.observerMode);
                    else
                        ImGui::TextColored(ImVec4{0.92f, 0.34f, 0.34f, 1.0f}, "DEAD");
                } else {
                    ImGui::TextUnformatted("");
                }
            }
            ImGui::EndTable();
        }
    }
    if (!GUI::isMenuOpen())
        ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
    ImGui::End();
    // 7 window-level colors remain (the 2 Text pushes were popped at their use sites);
    // all 4 vars are still on the stack.
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(4);
}

// --- avatar (account-bar image) -------------------------------------------------------
// The user drops avatar.png (or .jpg) into the config folder; it is decoded once on the present
// thread and handed to the Vulkan hook for upload (avatar_texture in VulkanHook.h). Anything
// missing or undecodable simply keeps the NS monogram fallback.

bool avatarLoadAttempted = false;

// Reads an image file, decodes it and stages it as the account-bar avatar texture.
// Returns true when a texture was staged (stop retrying then).
[[nodiscard]] bool stageTextureFromFile(const char* path, bool music) noexcept
{
    const int fd = LinuxPlatformApi::open(path, O_RDONLY);
    if (fd < 0)
        return false;

    struct stat st {};
    if (LinuxPlatformApi::fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 8 * 1024 * 1024) {
        LinuxPlatformApi::close(fd);
        return false;
    }
    auto* fileData = static_cast<std::uint8_t*>(std::malloc(static_cast<std::size_t>(st.st_size)));
    if (!fileData) {
        LinuxPlatformApi::close(fd);
        return false;
    }
    std::size_t totalRead = 0;
    while (totalRead < static_cast<std::size_t>(st.st_size)) {
        const auto n = LinuxPlatformApi::pread(fd, fileData + totalRead, static_cast<std::size_t>(st.st_size) - totalRead, static_cast<off_t>(totalRead));
        if (n <= 0)
            break;
        totalRead += static_cast<std::size_t>(n);
    }
    LinuxPlatformApi::close(fd);
    if (totalRead != static_cast<std::size_t>(st.st_size)) {
        std::free(fileData);
        return false;
    }

    int width = 0, height = 0;
    if (!stbi_info_from_memory(fileData, static_cast<int>(totalRead), &width, &height, nullptr)
        || width <= 0 || height <= 0 || (music && (width > 2048 || height > 2048))) {
        std::free(fileData);
        return false;
    }
    unsigned char* pixels = stbi_load_from_memory(fileData, static_cast<int>(totalRead), &width, &height, nullptr, 4);
    std::free(fileData);
    if (!pixels)
        return false;

    gui_log::write("avatar staged: %s (%dx%d)", path, width, height);
    if (music)
        VulkanHook::music_texture::request(pixels, width, height);
    else
        VulkanHook::avatar_texture::request(pixels, width, height);
    return true;
}

[[nodiscard]] bool stageAvatarFromFile(const char* path) noexcept
{
    return stageTextureFromFile(path, false);
}

// Priority: user avatar in the config dir, then the steam persona fetch in the exchange root
// (Source/Features/Hud/SteamPersona.h). Returns true when a texture was staged.
[[nodiscard]] bool loadAvatar() noexcept
{
    bool staged = false;
    static_cast<void>(ui_config::withContext([&](auto&& hookContext) {
        const auto* const directory = hookContext.osirisDirectoryPath().get();
        if (!directory)
            return;
        char path[512];
        std::snprintf(path, sizeof(path), "%s/avatar.png", directory);
        if (!(staged = stageAvatarFromFile(path))) {
            std::snprintf(path, sizeof(path), "%s/avatar.jpg", directory);
            staged = stageAvatarFromFile(path);
        }
    }));
    return staged || stageAvatarFromFile(steam_persona::avatarFile());
}

void neverlose::processDeferred() noexcept
{
    if (fontReloadPending) {
        fontReloadPending = false;
        menuScale = pendingMenuScale;
        if (ImGui::GetCurrentContext()) {
            // Menu submits are unchained (no semaphore waits), so their in-flight depth is NOT
            // bounded by the swapchain - a draw submitted frames ago can still be sampling the
            // current font texture. Letting the backend free it unsynced is the amdgpu
            // page-fault -> ring-timeout -> VK_ERROR_DEVICE_LOST recipe (2026-08-29 crash,
            // menu-scale slider release). Drain first; the atlas rebuild below dwarfs the wait.
            VulkanHook::waitUntilDeviceIdle();
            // Drop the uploaded font texture so the backend re-runs CreateFontsTexture (which
            // builds the re-scaled atlas) on the next ImGui_ImplVulkan_NewFrame. Must happen
            // OUTSIDE a frame.
            ImGui_ImplVulkan_DestroyFontsTexture();
            loadFonts();
        }
    }

    // Steam persona (name + avatar for the account bar): keep the host fetch spawned (rate-limited
    // retries until the name lands - a single failed spawn used to mean the fallback label for the
    // whole session), and stage the avatar every few seconds until it succeeds (a user avatar.png
    // in the config dir wins whenever it exists).
    steam_persona::ensureFetchStarted();
    static float nextAvatarTry = 0.0f; // present thread only
    if (!avatarLoadAttempted && ImGui::GetTime() >= nextAvatarTry) {
        nextAvatarTry = ImGui::GetTime() + 4.0f;
        if (loadAvatar())
            avatarLoadAttempted = true;
    }
}

void neverlose::cancelKeybindCapture() noexcept
{
    state.capture = State::Capture::Inactive;
    state.captureOwner = -1;
}

void neverlose::beginReveal() noexcept
{
    dismissActive = false; // reopening mid-dismissal: the open animation takes over
    reveal = 0.0f;
}

void neverlose::beginDismiss() noexcept
{
    // Keep the current reveal progress: closing mid-open-animation reverses from wherever the
    // shell actually is instead of popping it back to the fully shown state first.
    dismissActive = true;
}

bool neverlose::isDismissing() noexcept
{
    return dismissActive;
}

void neverlose::loadFonts() noexcept
{
    if (!menuScaleInitialized) {
        menuScaleInitialized = true;
        // Sensible default: design size at 1080p, proportionally larger on denser displays.
        float displayHeight = 1080.0f;
        if (gui_sdl::functions.getDisplayForWindow && gui_sdl::functions.getCurrentDisplayMode && gui_sdl::window) {
            const auto* mode = gui_sdl::functions.getCurrentDisplayMode(gui_sdl::functions.getDisplayForWindow(gui_sdl::window));
            if (mode && mode->h > 0)
                displayHeight = static_cast<float>(mode->h);
        }
        menuScale = ImClamp(displayHeight / 1080.0f, 0.8f, 2.0f);
    }
    applyMetrics();

    auto& fonts = ImGui::GetIO().Fonts;
    fonts->Clear();

    // Merged glyph ranges for text fonts: Latin (default) + simplified Chinese common (~2500
    // curated glyphs) so skin names like "\xE9\xBE\x8D\xE7\x8E\x8B (Dragon King)" render.
    // MUST be static storage: the atlas reads the ranges later, when it is built (first
    // ImGui_ImplVulkan_NewFrame), long after this stack frame is gone.
    static ImWchar textRanges[4096];
    static bool textRangesBuilt = false;
    if (!textRangesBuilt) {
        textRangesBuilt = true;
        int n = 0;
        for (const ImWchar* src : { fonts->GetGlyphRangesDefault(), fonts->GetGlyphRangesChineseSimplifiedCommon() }) {
            for (const ImWchar* p = src; p[0] != 0 && n + 2 < static_cast<int>(sizeof(textRanges) / sizeof(textRanges[0])); p += 2) {
                textRanges[n++] = p[0];
                textRanges[n++] = p[1];
            }
        }
        textRanges[n] = 0;
    }

    // Compact icon glyph range: only the codepoints this UI uses. Static for the same reason.
    static ImWchar iconRanges[64];
    {
        int n = 0;
        for (int k = 0; kIconCodepoints[k] != 0 && n + 2 < 60; ++k) {
            iconRanges[n++] = kIconCodepoints[k];
            iconRanges[n++] = kIconCodepoints[k];
        }
        iconRanges[n] = 0;
    }

    ImFont* body = loadFontFromMemory(_binary_Inter_Medium_ttf_start, _binary_Inter_Medium_ttf_end, kTextBody, textRanges);
    if (!body)
        body = fonts->AddFontDefault();
    // CJK fallback, merged into each TEXT font. AddFont with MergeMode merges into the MOST
    // RECENTLY ADDED font, so each merge must run immediately after its base font - doing them
    // in a loop after the icon font was added would land BOTH merges on the icon font.
    {
        ImFontConfig cjk{};
        cjk.FontData = const_cast<unsigned char*>(_binary_NotoCJK_subset_ttf_start);
        cjk.FontDataSize = static_cast<int>(_binary_NotoCJK_subset_ttf_end - _binary_NotoCJK_subset_ttf_start);
        cjk.FontDataOwnedByAtlas = false;
        cjk.SizePixels = kTextBody;
        cjk.GlyphRanges = textRanges; // the subset simply has no glyphs for anything else
        cjk.MergeMode = true;
        fonts->AddFont(&cjk);
    }

    loadFontFromMemory(_binary_Inter_SemiBold_ttf_start, _binary_Inter_SemiBold_ttf_end, kTextTitle, textRanges);
    {
        ImFontConfig cjk{};
        cjk.FontData = const_cast<unsigned char*>(_binary_NotoCJK_subset_ttf_start);
        cjk.FontDataSize = static_cast<int>(_binary_NotoCJK_subset_ttf_end - _binary_NotoCJK_subset_ttf_start);
        cjk.FontDataOwnedByAtlas = false;
        cjk.SizePixels = kTextTitle;
        cjk.GlyphRanges = textRanges;
        cjk.MergeMode = true;
        fonts->AddFont(&cjk);
    }

    loadFontFromMemory(_binary_fa_solid_900_ttf_start, _binary_fa_solid_900_ttf_end, kTextIcon, iconRanges);

    StatusReport::record("Neverlose menu fonts (Latin + CJK common)", body != nullptr);
}

bool neverlose::fontsLoaded() noexcept
{
    return ImGui::GetIO().Fonts->Fonts.Size > 1;
}
