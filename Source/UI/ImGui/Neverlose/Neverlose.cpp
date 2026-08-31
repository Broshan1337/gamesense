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
#include <Features/Game/MovementConfigVariables.h>
#include <Utils/ColorUtils.h>
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
#include <Features/Game/CooldownRevealerConfigVariables.h>
#include <Features/Game/FakeLevelConfigVariables.h>
#include <Features/Game/FakePrimeConfigVariables.h>
#include <Features/Game/FvaConfigVariables.h>
#include <Features/Game/HitLogConfigVariables.h>
#include <Features/Game/KillsayConfigVariables.h>
#include <Features/Game/MatchAutoAcceptConfigVariables.h>
#include <Features/Game/PanicKeyConfigVariables.h>
#include <Features/Game/TeamDamageConfigVariables.h>
#include <Features/Game/ValveDsSpoofConfigVariables.h>
#include <Features/Game/VoteRevealerConfigVariables.h>
#include <Features/Hud/BombPlantAlert/BombPlantAlertConfigVariables.h>
#include <Features/Hud/BombTimer/BombTimerConfigVariables.h>
#include <Features/Hud/DefusingAlert/DefusingAlertConfigVariables.h>
#include <Features/Hud/KillfeedPreserver/KillfeedPreserverConfigVariables.h>
#include <Features/Hud/PostRoundTimer/PostRoundTimerConfigVariables.h>
#include <Features/Hud/Watermark/WatermarkConfigVariables.h>
#include <Features/Radio/RadioConfigVariables.h>
#include <Features/Radio/RadioManager.h>
#include <Features/SkinChanger/SkinChangerConfigVariables.h>
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
#include <Features/Hud/BindsList/BindsListConfigVariables.h>
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

float kTextBody = 15.0f;
float kTextControl = 14.0f;
float kTextSmall = 12.0f;
float kTextCaption = 10.0f;
float kTextTitle = 16.0f;
float kTextIcon = 13.0f;

float kShellWidth = 748.0f;
float kShellHeight = 576.0f;
float kSidebarWidth = 158.0f;
float kToolbarHeight = 56.0f;
float kRowHeight = 37.0f;

float menuScale = 1.0f;     // 1.0 = design size; adjusted in the profile popover
bool menuScaleInitialized = false;
float navScroll = 0.0f;     // sidebar nav rail scroll (the expanded Visuals list can outgrow
                            // the space above the account bar - wheel-scrolls, clipped)

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
ImU32 g_buttonAccent = C(0xA3, 0xD4, 0x1F);
ImU32 g_sliderAccent = C(0xA3, 0xD4, 0x1F);

// Present-thread refresh from the config vars (called once per frame from renderGameOverlay).
void refreshMenuTheme() noexcept
{
    const auto accent = ui_config::get<MenuAccentColor>();
    g_accent = C(accent.r(), accent.g(), accent.b(), accent.a());
    const auto button = ui_config::get<MenuButtonColor>();
    g_buttonAccent = C(button.r(), button.g(), button.b(), button.a());
    const auto slider = ui_config::get<MenuSliderColor>();
    g_sliderAccent = C(slider.r(), slider.g(), slider.b(), slider.a());
}

struct StylePreset {
    const char* name;
    ImU32 color;
};

constexpr StylePreset kStylePresets[] = {
    {"Default", accentFromWatermark()}, // default theme = the watermark's green
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
    kTextCaption = 10.0f * menuScale;
    kTextTitle = 16.0f * menuScale;
    kTextIcon = 13.0f * menuScale;
    kShellWidth = 748.0f * menuScale;
    kShellHeight = 576.0f * menuScale;
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
    0xf54b, // shoe-prints     (Movement)
    0xf519, // broadcast-tower (Radio)
    0xf6cb, // dagger          (Inventory)
    0xf8cc, // mouse           (Legit)
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
};

struct State {
    Page page = Page::Rage;
    bool visualsExpanded = false;
    SelectPopup popup;
    // keybind capture (mirrors the Panorama KeybindCapture state machine); the row being
    // captured is identified by its control id
    enum class Capture { Inactive, WaitingRelease, WaitingPress };
    Capture capture = Capture::Inactive;
    int captureOwner = -1;
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

// content scrolling (manual - the hand-drawn content does not live in an ImGui child).
// scrollTarget is what the wheel writes; scrollOffset (the rendered position) eases toward it
// with a frame-rate independent exponential decay - wheel notches land as a short glide
// instead of a 40px jump, and trackpads accumulate naturally.
float scrollOffset = 0.0f;
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

constexpr const char* kPageNames[] = {
    "Rage", "Legit", "Movement", "Player Info", "Glow",
    "Viewmodel", "Effects", "Hud", "Sound", "Inventory", "Radio", "Misc"
};

// Case-insensitive substring match (strcasestr is GNU-only; keep it portable and local).
[[nodiscard]] bool searchMatches(const char* haystack, const char* needle) noexcept
{
    if (!needle[0])
        return false;
    for (const char* h = haystack; *h; ++h) {
        int i = 0;
        while (needle[i] && h[i] && std::tolower(static_cast<unsigned char>(h[i])) == std::tolower(static_cast<unsigned char>(needle[i])))
            ++i;
        if (!needle[i])
            return true;
    }
    return false;
}

// cinematic open: 0 right after the menu is toggled open, eased to 1 while rendering
float reveal = 1.0f;
// Set on the menu-close falling edge (beginDismiss); the reveal transform then runs BACKWARDS
// (scale down + slide + fade) while GUI.cpp keeps the render path alive past the alpha fade.
// Cleared once the animation lands (reveal ~ 0) and on reopen (beginReveal wins).
bool dismissActive = false;

bool configPopoverOpen = false;
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

float motion(ImGuiID id, float target, float speed = 16.0f, float initial = -1.0f) noexcept
{
    float* value = ImGui::GetStateStorage()->GetFloatRef(id, initial < 0.0f ? target : initial);
    *value = ImLerp(*value, target, 1.0f - std::exp(-speed * ImGui::GetIO().DeltaTime));
    if (std::fabs(*value - target) < 0.0005f)
        *value = target;
    return *value;
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

void softShadow(ImDrawList* d, ImVec2 min, ImVec2 max, float rounding) noexcept
{
    // real gaussian falloff via the precomputed shadow stamp (shadow_texture in VulkanHook.h);
    // the layered-rect fake runs until the stamp upload completes (first frames) or permanently
    // if it failed.
    drawStampSlice(d, min, max, s(12.0f), C(0, 0, 0, 110));
}

void chevron(ImDrawList* d, ImVec2 p, ImU32 color) noexcept
{
    d->AddLine(p, p + ImVec2(s(3), s(3)), color, s(1.3f));
    d->AddLine(p + ImVec2(s(3), s(3)), p + ImVec2(s(0), s(6)), color, s(1.3f));
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
    int row = 0;
};

CardContext card;

void beginRow(ImDrawList* d, const char* label) noexcept
{
    const float y = card.origin.y + card.row * kRowHeight;
    if (searchIndexing && searchIndexCount < kSearchIndexCap)
        searchIndex[searchIndexCount++] = {label, static_cast<Page>(searchIndexPageCursor), y - (shellBase.y + kToolbarHeight + s(24.0f)), searchGlowSubTab};
    if (card.row)
        d->AddLine(ImVec2(card.origin.x + s(12), y), ImVec2(card.origin.x + card.width - s(12), y), C(26, 26, 30));
    textY(d, card.origin.x + s(13), y, kRowHeight, C(207, 209, 218), label, kTextBody, nullptr);
    ++card.row;
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
    beginRow(d, label);

    ImVec2 p = rowControlPos(s(43.0f));
    p.y += rowCentered(s(18.0f));

    ImGui::PushID(id);
    const bool clicked = hit("##toggle", p - ImVec2(s(5), s(6)), ImVec2(s(39), s(30)));
    const ImGuiID iid = ImGui::GetItemID();
    if (rightClicked)
        *rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    // Hover preview, same as the popover toggles (Outer Glow etc.): hovering an OFF toggle
    // slides the knob to the middle and tints the track halfway toward the accent - the knob
    // then commits to whichever side on click. ON toggles stay put under the cursor.
    const float on = motion(iid ^ 0x55aa721u, *value ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f, 19.0f);
    const float hover = motion(iid ^ 0x118a0u, ImGui::IsItemHovered() ? 1.0f : 0.0f, 20.0f);
    ImGui::PopID();
    if (clicked)
        *value = !*value;

    d->AddRectFilled(p - ImVec2(s(1 + hover), s(1 + hover)), p + ImVec2(s(30 + hover), s(19 + hover)), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(45 * hover) << IM_COL32_A_SHIFT), s(10));
    d->AddRectFilled(p, p + ImVec2(s(29), s(18)), mix(C(27, 27, 29), g_buttonAccent, on), s(9));
    d->AddCircleFilled(p + ImVec2(ImLerp(s(9), s(20), on), s(9)), s(7), mix(C(133, 144, 156), C(248, 249, 252), on));
    return clicked;
}

bool select(const char* label, int* index, const char* const* options, int count, int id,
    void (*apply)(int) = nullptr) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    beginRow(d, label);

    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool click = hit("##select", cp, ImVec2(controlWidth, s(23)));
    const ImGuiID iid = ImGui::GetItemID();
    const float response = motion(iid ^ 0x6161u, ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();

    // The popover commits through `apply` in a later frame, so the option table and the
    // applier must outlive this frame: callers pass static tables and a static function.
    if (click && apply) {
        state.popup.open = !(state.popup.open && state.popup.owner == id);
        state.popup.owner = id;
        state.popup.count = count;
        state.popup.options = options;
        state.popup.apply = apply;
        state.popup.anchor = cp;
        state.popup.width = controlWidth;
        state.popup.openedFrame = ImGui::GetFrameCount();
    }

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(C(24, 24, 26), C(32, 32, 36), response), s(5));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(C(30, 30, 33), g_accent, response), s(5));
    textY(d, cp.x + s(7), cp.y, s(23), C(170, 173, 184), (*index >= 0 && *index < count) ? options[*index] : "Select", kTextControl, nullptr);
    d->AddLine(cp + ImVec2(controlWidth - s(13), s(9)), cp + ImVec2(controlWidth - s(9), s(13)), C(139, 143, 154), 1.0f);
    d->AddLine(cp + ImVec2(controlWidth - s(9), s(13)), cp + ImVec2(controlWidth - s(5), s(9)), C(139, 143, 154), 1.0f);
    return false; // changes arrive through the popup layer
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
        const float shown = motion(ImGui::GetItemID() ^ 0xa8f1u, position, 14.0f, position);
        ImGui::PopID();

        d->AddRectFilled(start, start + ImVec2(trackWidth, s(3)), C(33, 33, 36), s(2));
        d->AddRectFilled(start, start + ImVec2(trackWidth * shown, s(3)), g_sliderAccent, s(2));
        d->AddCircleFilled(start + ImVec2(trackWidth * shown, s(1.5f)), s(5.5f), C(247, 248, 252));
    }

    // Value pill: click to type an exact value (editable sliders).
    ImVec2 pill = rowControlPos(s(55.0f));
    pill.y += rowCentered(s(21.0f));
    d->AddRectFilled(pill, pill + ImVec2(s(42), s(21)), C(24, 24, 26), s(5));

    ImGui::PushID(id + 500000);
    if (editing) {
        ImGui::SetCursorScreenPos(pill + ImVec2(s(4), s(3)));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, C(30, 30, 33));
        ImGui::PushStyleColor(ImGuiCol_Border, g_accent);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(4), s(2)));
        ImGui::PushItemWidth(s(42) - s(8));
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
        if (hit("##pill_edit", pill, ImVec2(s(42), s(21))) && ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) {
            state.editingSlider = id;
            std::snprintf(state.editBuffer, sizeof(state.editBuffer), "%d", *value);
        }
        char pillText[32];
        std::snprintf(pillText, sizeof(pillText), "%d%s", *value, suffix ? suffix : "");
        const float pillTextWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, pillText).x;
        textY(d, pill.x + (s(42) - pillTextWidth) * 0.5f, pill.y, s(21), C(166, 169, 179), pillText, kTextControl, nullptr);
    }
    ImGui::PopID();
    return *value != previousValue;
}

bool hueRow(const char* label, float* hue, int id) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    beginRow(d, label);

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
    const float shown = motion(ImGui::GetItemID() ^ 0x4d21u, *hue / 359.0f, 14.0f, *hue / 359.0f);
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
    d->AddRectFilled(pill, pill + ImVec2(s(42), s(21)), C(24, 24, 26), s(5));
    d->AddRectFilled(pill + ImVec2(s(4), s(4)), pill + ImVec2(s(38), s(17)), C(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255)), s(3));
    char pillText[16];
    std::snprintf(pillText, sizeof(pillText), "%.0f", *hue);
    const float pillTextWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, pillText).x;
    textY(d, pill.x + (s(42) - pillTextWidth) * 0.5f, pill.y, s(21), C(166, 169, 179), pillText, kTextSmall, nullptr);
    return true;
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
    beginRow(d, label);

    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool ownPopup = state.multiSelectOpen && state.multiSelectOwner == id;
    const bool click = ownPopup ? hitModal("##multiselect", cp, ImVec2(controlWidth, s(23)))
                                : hit("##multiselect", cp, ImVec2(controlWidth, s(23)));
    const ImGuiID iid = ImGui::GetItemID();
    const bool open = state.multiSelectOpen && state.multiSelectOwner == id;
    const float response = motion(iid ^ 0x71abu, (open || ImGui::IsItemHovered()) ? 1.0f : 0.0f);
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

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(C(24, 24, 26), C(32, 32, 36), response), s(5));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(C(30, 30, 33), g_accent, response), s(5));

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
    d->AddRectFilled(p, p + size, C(18, 18, 20, 245), s(8));
    d->AddRect(p, p + size, C(54, 54, 60, 205), s(8));

    const bool accepts = ImGui::GetFrameCount() > state.multiSelectOpenedFrame;
    for (int i = 0; i < state.multiSelectCount; ++i) {
        const ImVec2 rp = p + ImVec2(s(4), s(3) + i * rowHeight);
        ImGui::PushID(7500 + i);
        const bool clicked = hitModal("##ms_row", rp, ImVec2(size.x - s(8), rowHeight - s(4)));
        const float hover = motion(ImGui::GetItemID() ^ (0xa11ceu + i), ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
        ImGui::PopID();
        if (hover > 0.001f)
            d->AddRectFilled(rp, rp + ImVec2(size.x - s(8), rowHeight - s(4)), C(255, 255, 255, static_cast<int>(12 * hover)), s(6));

        const bool on = state.multiSelectGet(static_cast<std::size_t>(i));
        const ImVec2 cb = rp + ImVec2(s(8), (rowHeight - s(4) - s(12)) * 0.5f);
        d->AddRectFilled(cb, cb + ImVec2(s(12), s(12)), on ? g_buttonAccent : C(27, 27, 29), s(3));
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

    constexpr float width = 170.0f;
    const float rowH = s(23.0f);
    const float gap = s(6.0f);
    const float height = s(14.0f) + s(8.0f) + rowH + gap + rowH + gap + rowH + s(8.0f);
    ImVec2 p = state.featureBindAnchor;
    // clamp inside the shell clip rect (same rule as the dropdown popover)
    const ImVec2 clipMin = d->GetClipRectMin();
    const ImVec2 clipMax = d->GetClipRectMax();
    p.x = ImClamp(p.x, clipMin.x + s(10.0f), ImMax(clipMin.x + s(10.0f), clipMax.x - width - s(10.0f)));
    p.y = ImClamp(p.y, clipMin.y + s(10.0f), ImMax(clipMin.y + s(10.0f), clipMax.y - height - s(10.0f)));
    const ImVec2 size{width, height};

    recordPopupRect(PopupFeatureBind, p, p + size);
    softShadow(d, p, p + size, s(10.0f));
    d->AddRectFilled(p, p + size, C(18, 18, 20, 245), s(8));
    d->AddRect(p, p + size, C(54, 54, 60, 205), s(8));

    textY(d, p.x + s(10), p.y + s(7), s(14), C(137, 142, 153), entry.label ? entry.label : "Bind", kTextCaption, nullptr);

    float y = p.y + s(14) + s(8);

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
                         held ? g_buttonAccent : C(24, 24, 26), s(5));
        d->AddRect(ImVec2{p.x + s(10), y}, ImVec2{p.x + s(10) + width - s(20), y + rowH},
                   held ? g_buttonAccent : C(30, 30, 33), s(5));
        textY(d, p.x + s(10) + s(7), y, rowH, held ? C(18, 18, 20) : C(170, 173, 184), keyText, kTextControl, nullptr);
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
                             active ? mix(C(24, 24, 26), g_buttonAccent, 0.55f) : C(24, 24, 26), s(5));
            d->AddRect(ImVec2{x, y}, ImVec2{x + pillWidth, y + rowH},
                       active ? g_buttonAccent : C(30, 30, 33), s(5));
            textY(d, x, y, rowH, active ? C(248, 249, 252) : C(170, 173, 184), mode == 0 ? "TOGGLE" : "HOLD", kTextControl, nullptr);
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
        d->AddRectFilled(ImVec2{p.x + s(10), y}, ImVec2{p.x + s(10) + width - s(20), y + rowH}, C(24, 24, 26), s(5));
        d->AddRect(ImVec2{p.x + s(10), y}, ImVec2{p.x + s(10) + width - s(20), y + rowH}, C(30, 30, 33), s(5));
        textY(d, p.x + s(10), y, rowH, C(206, 110, 110), "UNBIND", kTextControl, nullptr);
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
    beginRow(d, label);

    const float controlWidth = ImMin(s(134.0f), card.width * 0.48f);
    ImVec2 cp = rowControlPos(controlWidth + s(13.0f));
    cp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    const bool ownPicker = state.colorPickerOpen && state.colorPickerOwner == id;
    const bool click = ownPicker ? hitModal("##color", cp, ImVec2(controlWidth, s(23)))
                                 : hit("##color", cp, ImVec2(controlWidth, s(23)));
    const ImGuiID iid = ImGui::GetItemID();
    const bool open = state.colorPickerOpen && state.colorPickerOwner == id;
    const float response = motion(iid ^ 0xc010u, (open || ImGui::IsItemHovered()) ? 1.0f : 0.0f);
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

    d->AddRectFilled(cp, cp + ImVec2(controlWidth, s(23)), mix(C(24, 24, 26), C(32, 32, 36), response), s(5));
    d->AddRect(cp, cp + ImVec2(controlWidth, s(23)), mix(C(30, 30, 33), g_accent, response), s(5));

    const auto color = ui_config::get<Var>();
    const ImVec2 swatch = cp + ImVec2(s(6), s(4));
    d->AddRectFilled(swatch, swatch + ImVec2(s(20), s(15)), C(color.r(), color.g(), color.b(), color.a()), s(3));
    char hex[10];
    std::snprintf(hex, sizeof(hex), "%06X", static_cast<unsigned>(static_cast<std::uint32_t>(color) >> 8));
    const float hexWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, hex).x;
    textY(d, cp.x + (controlWidth - hexWidth) * 0.5f, cp.y, s(23), C(170, 173, 184), hex, kTextControl, nullptr);
}

// gradient strip for one RGBA channel; returns true when the user dragged a new value in
bool colorChannelStrip(ImDrawList* d, ImVec2 start, float trackWidth, char channelLetter,
    std::uint8_t currentValue, std::uint8_t o0, std::uint8_t o1, std::uint8_t o2, int id, std::uint8_t& newValue) noexcept
{
    ImGui::PushID(id);
    hitModal("##channel", start - ImVec2(s(4), s(5)), ImVec2(trackWidth + s(8), s(15)));
    const bool active = ImGui::IsItemActive();
    if (active)
        newValue = static_cast<std::uint8_t>(ImClamp((ImGui::GetIO().MousePos.x - start.x) / trackWidth, 0.0f, 1.0f) * 255.0f + 0.5f);
    const float shown = motion(ImGui::GetItemID() ^ 0x3b01u, currentValue / 255.0f, 18.0f, currentValue / 255.0f);
    ImGui::PopID();

    constexpr int segments = 8;
    for (int i = 0; i < segments; ++i) {
        const auto t = static_cast<std::uint8_t>((i + 1) * 255 / segments);
        ImU32 segmentColor;
        if (channelLetter == 'R') segmentColor = C(t, o1, o2, o0);
        else if (channelLetter == 'G') segmentColor = C(o0, t, o1, o2);
        else if (channelLetter == 'B') segmentColor = C(o0, o1, t, o2);
        else segmentColor = C(o0, o1, o2, t);
        d->AddRectFilled(start + ImVec2(trackWidth * i / segments, 0), start + ImVec2(trackWidth * (i + 1) / segments, s(8)), segmentColor, s(2));
    }
    d->AddCircleFilled(start + ImVec2(trackWidth * shown, s(4.0f)), s(5), C(247, 248, 252));
    const char letter[2] = { channelLetter, 0 };
    textY(d, start.x - s(12), start.y - s(1), s(10), C(140, 144, 154), letter, kTextSmall, nullptr);
    return active;
}

void colorPickerPopover(ImDrawList* d) noexcept
{
    if (!state.colorPickerOpen || !state.colorGet)
        return;

    const auto color = state.colorGet();
    const float width = s(150.0f);
    const float trackWidth = width - s(34.0f);
    const ImVec2 size(width, s(10) + 4 * s(22) + s(6));
    // flip above the anchor swatch when the popover would run past the clip bottom - the shell
    // window clips its own draw list, so the clip rect (not the display) is the real bound
    const bool openAbove = state.colorPickerAnchor.y + s(23.0f) + s(4.0f) + size.y > d->GetClipRectMax().y;
    const ImVec2 p(state.colorPickerAnchor.x,
        openAbove ? state.colorPickerAnchor.y - size.y - s(4.0f) : state.colorPickerAnchor.y + s(23.0f) + s(4.0f));
    recordPopupRect(PopupColor, p, p + size);
    softShadow(d, p, p + size, s(10.0f));
    d->AddRectFilled(p, p + size, C(18, 18, 20, 245), s(8));
    d->AddRect(p, p + size, C(54, 54, 60, 205), s(8));

    const std::uint8_t channels[4] = { color.r(), color.g(), color.b(), color.a() };
    const char channelLetters[4] = { 'R', 'G', 'B', 'A' };

    for (int i = 0; i < 4; ++i) {
        const ImVec2 start = p + ImVec2(s(22), s(10) + i * s(22));
        std::uint8_t newValue = channels[i];
        const std::uint8_t others[3] = { channels[i == 0 ? 1 : 0], channels[i <= 1 ? 2 : 1], channels[i == 3 ? 2 : 3] };
        if (colorChannelStrip(d, start, trackWidth, channelLetters[i], channels[i], others[0], others[1], others[2], 880 + i, newValue)) {
            std::uint8_t updated[4] = { channels[0], channels[1], channels[2], channels[3] };
            updated[i] = newValue;
            state.colorSet(color::Rgba{updated[0], updated[1], updated[2], updated[3]});
        }
    }

    // click anywhere outside the picker (and its anchor swatch) closes it
    if (ImGui::GetFrameCount() > state.colorPickerOpenedFrame
        && clickedOutside(p, p + ImVec2(width, s(10) + 4 * s(22) + s(6)))
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
        const float shown = motion(ImGui::GetItemID() ^ 0xf8a1u, position, 14.0f, position);
        ImGui::PopID();

        d->AddRectFilled(start, start + ImVec2(trackWidth, s(3)), C(33, 33, 36), s(2));
        d->AddRectFilled(start, start + ImVec2(trackWidth * shown, s(3)), g_sliderAccent, s(2));
        d->AddCircleFilled(start + ImVec2(trackWidth * shown, s(1.5f)), s(5.5f), C(247, 248, 252));
    }

    // Value pill: click to type an exact value.
    ImVec2 pill = rowControlPos(s(55.0f));
    pill.y += rowCentered(s(21.0f));
    d->AddRectFilled(pill, pill + ImVec2(s(42), s(21)), C(24, 24, 26), s(5));

    ImGui::PushID(id + 600000);
    if (editing) {
        ImGui::SetCursorScreenPos(pill + ImVec2(s(4), s(3)));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, C(30, 30, 33));
        ImGui::PushStyleColor(ImGuiCol_Border, g_accent);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(4), s(2)));
        ImGui::PushItemWidth(s(42) - s(8));
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
        if (hit("##fpill_edit", pill, ImVec2(s(42), s(21))) && ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) {
            state.editingSlider = id;
            std::snprintf(state.editBuffer, sizeof(state.editBuffer), "%.2f", static_cast<double>(value));
        }
        char pillText[24];
        std::snprintf(pillText, sizeof(pillText), "%.2f%s", static_cast<double>(value), suffix ? suffix : "");
        const float pillTextWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, pillText).x;
        textY(d, pill.x + (s(42) - pillTextWidth) * 0.5f, pill.y, s(21), C(166, 169, 179), pillText, kTextControl, nullptr);
    }
    ImGui::PopID();
}

bool keybindRow(const char* label, int* bindValue, int id) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    beginRow(d, label);

    const float buttonWidth = s(120.0f);
    ImVec2 bp = rowControlPos(buttonWidth + s(13.0f));
    bp.y += rowCentered(s(23.0f));

    ImGui::PushID(id);
    hit("##bind", bp, ImVec2(buttonWidth, s(23)));
    const ImGuiID iid = ImGui::GetItemID();

    if (state.capture == State::Capture::Inactive) {
        if (ImGui::IsItemClicked()) {
            state.capture = State::Capture::WaitingRelease;
            state.captureOwner = id;
        } else if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && *bindValue != Bind::kOff) {
            *bindValue = Bind::kOff; // right-click clears the bind
        }
    } else if (state.captureOwner == id) {
        if (state.capture == State::Capture::WaitingRelease) {
            if (!gui_sdl::anyInputHeld())
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
    const float response = motion(iid ^ 0x9241u, ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();

    // The pill glows in the button accent while the bound key is physically held, so the binds
    // panel (and every Hold Key row) doubles as a live input indicator.
    const bool held = *bindValue != Bind::kOff && Bind::isDown(*bindValue) && state.capture == State::Capture::Inactive;
    d->AddRectFilled(bp, bp + ImVec2(buttonWidth, s(23)), held ? g_buttonAccent : mix(C(24, 24, 26), C(32, 32, 36), response), s(5));
    d->AddRect(bp, bp + ImVec2(buttonWidth, s(23)), mix(C(30, 30, 33), g_buttonAccent, response), s(5));
    const char* buttonText = (state.captureOwner == id && state.capture == State::Capture::WaitingPress) ? "PRESS ANY KEY"
        : (state.captureOwner == id && state.capture == State::Capture::WaitingRelease) ? "RELEASE ALL"
        : Bind::displayName(*bindValue);
    textY(d, bp.x + s(7), bp.y, s(23), held ? C(18, 18, 20) : C(170, 173, 184), buttonText, kTextControl, nullptr);
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

    // Feature-bind integration: for toggles registered in FeatureBinds, right-click opens the
    // bind popup (at the cursor - a context menu, not an anchored dropdown).
    if (rightClicked) {
        if (auto* entry = feature_binds::entryFor<ConfigVar>()) {
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

void popupLayer(ImDrawList* d) noexcept
{
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        state.popup.open = false;
    const float open = motion(ImGui::GetID("##popup_open"), state.popup.open ? 1.0f : 0.0f, 20.0f, 0.0f);
    if (open < 0.002f || !state.popup.apply)
        return;

    const ImVec2 size(state.popup.width, state.popup.count * s(32.0f) + s(8.0f));
    ImVec2 p(state.popup.anchor.x, state.popup.anchor.y + (s(23.0f) - size.y) * 0.5f);
    // Clamp to the window's clip rect (the shell), not the display - this draw list is clipped
    // at the shell edge, so a display-based clamp still lets the list clip in half (same bug
    // class as the color picker's flip fix).
    const ImVec2 clipMin = d->GetClipRectMin();
    const ImVec2 clipMax = d->GetClipRectMax();
    p.x = ImClamp(p.x, clipMin.x + s(10.0f), clipMax.x - size.x - s(10.0f));
    p.y = ImClamp(p.y, clipMin.y + s(10.0f), clipMax.y - size.y - s(10.0f));
    recordPopupRect(PopupDropdown, p, p + size);
    const int first = d->VtxBuffer.Size;
    softShadow(d, p, p + size, s(16.0f));
    d->AddRectFilled(p - ImVec2(s(5), s(2)), p + size + ImVec2(s(5), s(8)), C(0, 0, 0, 55), s(18));
    d->AddRectFilled(p, p + size, C(18, 18, 20, 240), s(16));
    d->AddRect(p, p + size, C(54, 54, 60, 205), s(16));
    d->AddLine(p + ImVec2(s(16), s(1)), p + ImVec2(size.x - s(16), s(1)), C(255, 255, 255, 22));
    const bool accepts = ImGui::GetFrameCount() > state.popup.openedFrame;
    for (int i = 0; i < state.popup.count; ++i) {
        ImVec2 rp = p + ImVec2(s(4), s(4) + i * s(32.0f));
        ImGui::PushID(5000 + i);
        const bool clicked = hitPopupRow("##popup_row", rp, ImVec2(size.x - s(8), s(32)), PopupDropdown);
        const float hover = motion(ImGui::GetItemID() ^ 0x9921u, ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
        ImGui::PopID();
        if (hover > 0.001f)
            d->AddRectFilled(rp, rp + ImVec2(size.x - s(8), s(32)), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(25 * hover) << IM_COL32_A_SHIFT), s(10));
        textY(d, rp.x + s(12), rp.y, s(32), C(182, 185, 196), state.popup.options[i], kTextControl, nullptr);
        if (accepts && clicked) {
            state.popup.apply(i);
            state.popup.open = false;
        }
    }
    // click anywhere outside the dropdown closes it (the clicked control still receives the
    // click - it is not covered by this popup, so hit() let it through)
    if (accepts && clickedOutside(p, p + size))
        state.popup.open = false;
    const float eased = 1.0f - std::pow(1.0f - open, 3.0f);
    const ImVec2 pivot(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    for (int i = first; i < d->VtxBuffer.Size; ++i) {
        ImDrawVert& v = d->VtxBuffer[i];
        v.pos = pivot + (v.pos - pivot) * ImLerp(0.96f, 1.0f, eased) + ImVec2(s(4.0f) * (1.0f - eased), 0);
        const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 255u;
        v.col = (v.col & 0x00ffffffu) | ((ImU32)(a * eased) << IM_COL32_A_SHIFT);
    }
}

// --- option tables (static - the popup layer outlives the opening frame) -----------

constexpr const char* const kPositionArrowColors[] = {"Player / Team Color", "Team Color"};
constexpr const char* const kHealthTextColors[] = {"Health-based", "White"};
// --- pages -----------------------------------------------------------------------
//
// Every page = section cards flowing into two balanced columns (scrollable). addCard picks the
// shorter column, draws the card frame and runs the row callbacks against the card context.

float columnYs[2] = {};
int controlId = 0;

void resetContent() noexcept
{
    columnYs[0] = columnYs[1] = 0.0f;
    controlId = static_cast<int>(state.page) * 1000;
}

void addCard(const char* title, int rowCount, void (*renderRows)()) noexcept
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    const int column = columnYs[1] < columnYs[0] ? 1 : 0;
    const float columnWidth = s(281.0f);
    // Draw-list calls take screen-absolute positions: the demo could use canvas-local ones
    // only because its window sat at (0,0); ours is centered.
    const float x = shellBase.x + kSidebarWidth + s(9.0f) + column * (columnWidth + s(10.0f));
    const float y = shellBase.y + kToolbarHeight + s(24.0f) + columnYs[column] - scrollOffset;

    // Self-healing card sizing: the rows render FIRST (channel 1), the frame afterwards
    // (channel 0) using the height the rows ACTUALLY took. The channels keep the frame painted
    // underneath the row content regardless of submission order. The declared rowCount becomes
    // a floor - a stale hand-count can only make a card too TALL, never clip rows.
    d->ChannelsSplit(2);
    d->ChannelsSetCurrent(1);
    card = CardContext{ImVec2(x, y + s(6.0f)), columnWidth, 0};
    renderRows();
    const float height = ImMax(static_cast<float>(rowCount) * kRowHeight, static_cast<float>(card.row) * kRowHeight) + s(12.0f);

    d->ChannelsSetCurrent(0);
    text(d, ImVec2(x, y - s(16.0f)), C(89, 94, 106), title, kTextCaption, nullptr);
    const ImVec2 p(x, y);
    softShadow(d, p, p + ImVec2(columnWidth, height), s(14.0f));
    d->AddRectFilled(p, p + ImVec2(columnWidth, height), C(16, 16, 18, 224), s(14.0f));
    d->AddRect(p, p + ImVec2(columnWidth, height), C(30, 30, 33), s(14.0f));
    d->ChannelsMerge();

    columnYs[column] += height + s(30.0f); // card + title band + gap
}

// --- page content (bindings 1:1 with the Panorama inventory) ------------------------

void pageRage() noexcept
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
    addCard("TIMING", 2, [] {
        sliderVar<BacktrackTicks>("Backtrack Ticks", ++controlId);
        sliderVar<ExtrapolateTicks>("Lead Ticks", ++controlId);
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
    addCard("ACCURACY", 4, [] {
        toggleVar<SpreadCompensation>("Compensate Spread", ++controlId);
        toggleVar<SpreadGate>("Hold Fire Until Exact", ++controlId);
        toggleVar<SeedFallback>("Fire Lucky Seeds", ++controlId);
        toggleVar<RecoilCompensation>("Compensate Recoil", ++controlId);
    });
    addCard("AUTO SHOOT", 6, [] {
        toggleVar<ForceShot>("Auto Shoot Ground", ++controlId);
        toggleVar<ForceShotAir>("Auto Shoot Air", ++controlId);
        sliderVar<Hitchance>("Min Hitchance", ++controlId, "%");
        sliderVar<MinDamage>("Min Damage", ++controlId);
        toggleVar<WallCheck>("Shoot Visible", ++controlId);
        toggleVar<Autowall>("Shoot Walls", ++controlId);
    });
    addCard("PREDICTION", 1, [] {
        toggleVar<Extrapolate>("Lead Targets", ++controlId);
    });
}

void pageLegit() noexcept
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
    addCard("TRIGGERBOT", 13, [] {
        toggleVar<triggerbot_vars::Enabled>("Triggerbot", ++controlId);
        keybindVar<triggerbot_vars::HoldKey>("Hold Key", ++controlId);
        sliderVar<triggerbot_vars::DelayMilliseconds>("Min Reaction Delay", ++controlId, " ms");
        sliderVar<triggerbot_vars::DelayMillisecondsMax>("Max Reaction Delay", ++controlId, " ms");
        toggleVar<triggerbot_vars::AccuracyCheck>("Shoot When Accurate", ++controlId);
        sliderVar<triggerbot_vars::AccuracyRadius>("Max Bullet Deviation", ++controlId, " u");
        toggleVar<triggerbot_vars::HeadOnly>("Shoot At The Head", ++controlId);
        sliderVar<triggerbot_vars::Hitchance>("Minimum Hitchance", ++controlId, "%");
        toggleVar<triggerbot_vars::MaxAccuracyOnly>("Shoot At Max Accuracy", ++controlId);
        toggleVar<triggerbot_vars::WallCheck>("Shoot Visible", ++controlId);
        toggleVar<triggerbot_vars::Autowall>("Shoot Walls", ++controlId);
        sliderVar<triggerbot_vars::AutowallMaxThickness>("Max Wall Thickness", ++controlId, " u");
        toggleVar<triggerbot_vars::SeededFire>("Seeded Fire", ++controlId);
    });
    addCard("RECOIL & SCOPES", 4, [] {
        toggleVar<rcs_vars::Enabled>("Control Recoil", ++controlId);
        sliderVar<rcs_vars::Strength>("Strength", ++controlId, "%");
        toggleVar<no_scope_inaccuracy_vis_vars::Enabled>("No-scope Inaccuracy Vis", ++controlId);
        toggleVar<spread_circle_vars::Enabled>("Draw Weapon Spread", ++controlId);
    });
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
        const ImGuiID iid = ImGui::GetItemID();
        const bool active = glowSubTab == i;
        const float r = motion(iid ^ 0x671cu, active ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
        ImGui::PopID();
        if (clicked && !active) {
            glowSubTab = i;
            // popovers belong to the other sub-tab's rows - close them
            state.popup.open = false;
            state.colorPickerOpen = false;
            state.multiSelectOpen = false;
            styleSelect.open = false;
        }
        d->AddRectFilled(p, p + ImVec2(pillWidth, pillHeight), mix(C(16, 16, 18, 0), C(37, 37, 41), r), pillHeight * 0.5f);
        d->AddRect(p, p + ImVec2(pillWidth, pillHeight), mix(C(30, 30, 33), g_accent, r), pillHeight * 0.5f);
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
        hueVar<FlashbangHue>("Flashbang", ++controlId);
        hueVar<HEGrenadeHue>("HE Grenade", ++controlId);
        hueVar<SmokeGrenadeHue>("Smoke Grenade", ++controlId);
        hueVar<MolotovHue>("Molotov / Incendiary", ++controlId);
    });
    addCard("BOMB & DEFUSE KIT", 6, [] {
        toggleVar<GlowDroppedBomb>("Glow Dropped Bomb", ++controlId);
        toggleVar<GlowTickingBomb>("Glow Ticking Bomb", ++controlId);
        toggleVar<GlowDefuseKits>("Glow Defuse Kits", ++controlId);
        hueVar<DroppedBombHue>("Dropped Bomb", ++controlId);
        hueVar<TickingBombHue>("Ticking Bomb", ++controlId);
        hueVar<DefuseKitHue>("Defuse Kit", ++controlId);
    });
    addCard("HOSTAGES", 2, [] {
        toggleVar<GlowHostages>("Glow Hostages", ++controlId);
        hueVar<HostageHue>("Hostage", ++controlId);
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
        hueVar<FlashbangHue>("Flashbang", ++controlId);
        hueVar<HEGrenadeHue>("HE Grenade", ++controlId);
        hueVar<SmokeGrenadeHue>("Smoke Grenade", ++controlId);
        hueVar<MolotovHue>("Molotov / Incendiary", ++controlId);
    });
    addCard("BOMB & DEFUSE KIT", 6, [] {
        toggleVar<GlowDroppedBomb>("Glow Dropped Bomb Model", ++controlId);
        toggleVar<GlowTickingBomb>("Glow Ticking Bomb Model", ++controlId);
        toggleVar<GlowDefuseKits>("Glow Defuse Kit Models", ++controlId);
        hueVar<DroppedBombHue>("Dropped Bomb", ++controlId);
        hueVar<TickingBombHue>("Ticking Bomb", ++controlId);
        hueVar<DefuseKitHue>("Defuse Kit", ++controlId);
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
    addCard("PLAYER LIST", 3, [] {
        toggleVar<PlayerListEnabled>("Player List", ++controlId);
        sliderVar<PlayerListOffsetX>("X Offset", ++controlId);
        sliderVar<PlayerListOffsetY>("Y Offset", ++controlId);
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
}

// --- movement (dedicated tab): automation + the edge/speed suite ---------------------

void pageMovement() noexcept
{
    addCard("AUTOMATION", 4, [] {
        toggleVar<BlockbotEnabled>("Blockbot", ++controlId);
        toggleVar<BunnyhopEnabled>("Bunnyhop", ++controlId);
        toggleVar<AutoStrafeEnabled>("Auto Strafe", ++controlId);
        toggleVar<TestStraferEnabled>("Test Strafer", ++controlId);
    });
    addCard("EDGE & SPEED", 6, [] {
        toggleVar<movement_vars::EdgeJump>("Edge Jump", ++controlId);
        toggleVar<movement_vars::EdgeStop>("Edge Stop", ++controlId);
        toggleVar<movement_vars::SlowWalk>("Slow Walk", ++controlId);
        sliderVar<movement_vars::SlowWalkSpeed>("Slow Walk Speed", ++controlId, "%");
        toggleVar<movement_vars::FastLadder>("Fast Ladder", ++controlId);
        toggleVar<movement_vars::JumpBug>("Jump Bug", ++controlId);
    });
}

void pageMisc() noexcept
{
    addCard("LOGGING", 4, [] {
        toggleVar<HitLogEnabled>("Log Hits", ++controlId);
        toggleVar<TeamDamageTrackerEnabled>("Team Damage Tracker", ++controlId);
        toggleVar<VoteRevealerEnabled>("Vote Revealer", ++controlId);
        toggleVar<CooldownRevealerEnabled>("Cooldown Revealer", ++controlId);
    });

    addCard("ACCOUNT", 4, [] {
        toggleVar<FakePrimeEnabled>("Fake Prime", ++controlId);
        toggleVar<FakeLevelEnabled>("Fake Level", ++controlId);
        sliderVar<FakeLevelValue>("Level", ++controlId);
        toggleVar<MatchAutoAcceptEnabled>("Match Auto Accept", ++controlId);
    });
    addCard("KILLSAY", 1, [] {
        toggleVar<KillsayEnabled>("Killsay From File", ++controlId);
    });
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
}

// --- inventory (skin changer) ---------------------------------------------------------
// Six category pills over one per-category card, plus a live knife preview card. Only the
// active category's selects exist per frame, so the 40+ dropdown rows never render at once
// (and the card area stays scannable instead of a wall of selects).

int inventoryCategory = 0;

constexpr const char* const kKnifeModels[] = {"None", "Bayonet", "Bowie Knife", "Butterfly Knife", "Classic Knife", "Falchion Knife", "Flip Knife", "Gut Knife", "Huntsman Knife", "Karambit", "Kukri Knife", "M9 Bayonet", "Navaja Knife", "Nomad Knife", "Paracord Knife", "Shadow Daggers", "Skeleton Knife", "Stiletto Knife", "Survival Knife", "Talon Knife", "Ursus Knife"};
constexpr const char* const kKnifeFinishes[] = {"None", "Fade", "Marble Fade", "Doppler", "Doppler Ruby", "Doppler Sapphire", "Doppler Black Pearl", "Tiger Tooth", "Damascus Steel", "Rust Coat", "Crimson Web"};

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
        const ImGuiID iid = ImGui::GetItemID();
        const bool active = inventoryCategory == i;
        const float r = motion(iid ^ 0x1acau, active ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
        ImGui::PopID();
        if (clicked && !active) {
            inventoryCategory = i;
            // popovers belong to the other category's rows - close them
            state.popup.open = false;
            state.colorPickerOpen = false;
            state.multiSelectOpen = false;
            styleSelect.open = false;
        }
        d->AddRectFilled(p, p + ImVec2(pillWidth, pillHeight), mix(C(16, 16, 18, 0), C(37, 37, 41), r), pillHeight * 0.5f);
        d->AddRect(p, p + ImVec2(pillWidth, pillHeight), mix(C(30, 30, 33), g_accent, r), pillHeight * 0.5f);
        const float labelWidth = ImGui::GetFont()->CalcTextSizeA(kTextControl, FLT_MAX, 0.0f, kNames[i]).x;
        textY(d, p.x + (pillWidth - labelWidth) * 0.5f, p.y, pillHeight, mix(C(145, 149, 159), C(226, 228, 235), r), kNames[i], kTextControl, nullptr);
    }
    // clearance below the pills so the card caption does not touch them
    columnYs[0] = columnYs[1] = s(4.0f) + pillHeight + s(30.0f);
}

void pageInventoryCategory(int category) noexcept
{
    using namespace skin_changer_vars;
    switch (category) {
    case 0:
        addCard("KNIVES", 2, [] {
            selectVar<KnifeModel>("Knife Model", kKnifeModels, 21, ++controlId);
            selectVar<KnifeSkin>("Knife Finish", kKnifeFinishes, 11, ++controlId);
        });
        break;
    case 1:
        addCard("PISTOLS", 10, [] {
            static const char* const cz75[] = {"None", "Army Sheen", "Copper Fiber", "Emerald Quartz", "Polymer", "Tread Plate", "The Fuschia Is Now", "Twist", "Poison Dart", "Chalice", "Emerald"};
            selectVar<CZ75AutoSkin>("CZ75-Auto", cz75, 11, ++controlId);
            static const char* const deagle[] = {"None", "Blaze", "Hypnotic", "Cobalt Disruption", "Bronze Deco", "Meteorite", "Night Heist", "Emerald Jörmungandr", "The Bronze", "Golden Koi", "Sunset Storm 壱"};
            selectVar<DesertEagleSkin>("Desert Eagle", deagle, 11, ++controlId);
            static const char* const dualBerettas[] = {"None", "Cobalt Quartz", "Heist", "Hemoglobin", "Emerald", "Anodized Navy", "Cartel", "Stained", "Flora Carnivora", "Twin Turbo", "Dualing Dragons"};
            selectVar<DualBerettasSkin>("Dual Berettas", dualBerettas, 11, ++controlId);
            static const char* const fiveSeven[] = {"None", "Berries And Cherries", "Copper Galaxy", "Silver Quartz", "Anodized Gunmetal", "Fowl Play", "Heat Treated", "Scumbria", "Angry Mob", "Violent Daimyo", "Fairy Tale"};
            selectVar<FiveSeveNSkin>("Five-SeveN", fiveSeven, 11, ++controlId);
            static const char* const glock18[] = {"None", "Fade", "Dragon Tattoo", "Steel Disruption", "Moonrise", "High Beam", "Twilight Galaxy", "Reactor", "Nuclear Garden", "Brass", "Bunsen Burner"};
            selectVar<Glock18Skin>("Glock-18", glock18, 11, ++controlId);
            static const char* const p2000[] = {"None", "Amber Fade", "Space Race", "Panther Camo", "Chainmail", "Dispatch", "Ocean Foam", "Imperial", "Scorpion", "Silver", "Acid Etched"};
            selectVar<P2000Skin>("P2000", p2000, 11, ++controlId);
            static const char* const p250[] = {"None", "Nevermore", "Digital Architect", "Steel Disruption", "Undertow", "Ripple", "Dark Filigree", "Metallic DDPAT", "Cartel", "Valence", "Verdigris"};
            selectVar<P250Skin>("P250", p250, 11, ++controlId);
            static const char* const r8[] = {"None", "Amber Fade", "Fade", "Blaze", "Phoenix Marker", "Leafhopper", "Reboot", "Survivalist", "Skull Crusher", "Banana Cannon", "Bone Forged"};
            selectVar<R8RevolverSkin>("R8 Revolver", r8, 11, ++controlId);
            static const char* const tec9[] = {"None", "Red Quartz", "Titanium Bit", "Ossified", "Re-Entry", "Ice Cap", "Blue Titanium", "Brass", "Cut Out", "Isaac", "Avalanche"};
            selectVar<Tec9Skin>("Tec-9", tec9, 11, ++controlId);
            static const char* const usps[] = {"None", "Serum", "Stainless", "Dark Water", "Purple DDPAT", "Target Acquired", "Orange Anolis", "Caiman", "Business Class", "Black Lotus", "Cortex"};
            selectVar<USPSSkin>("USP-S", usps, 11, ++controlId);
        });
        break;
    case 2:
        addCard("SMGS", 7, [] {
            static const char* const mac10[] = {"None", "Fade", "Amber Fade", "Last Dive", "Gold Brick", "Copper Borre", "Aloha", "Lapis Gator", "Malachite", "Oceanic", "Nuclear Garden"};
            selectVar<MAC10Skin>("MAC-10", mac10, 11, ++controlId);
            static const char* const mp5sd[] = {"None", "Co-Processor", "Desert Strike", "Liquidation", "Condition Zero", "Acid Wash", "Agent", "Phosphor", "Necro Jr.", "Oxide Oasis", "Gauss"};
            selectVar<MP5SDSkin>("MP5-SD", mp5sd, 11, ++controlId);
            static const char* const mp7[] = {"None", "Fade", "Motherboard", "Vault Heist", "Ocean Foam", "Anodized Navy", "Armor Core", "Urban Hazard", "Special Delivery", "Abyssal Apparition", "Guerrilla"};
            selectVar<MP7Skin>("MP7", mp7, 11, ++controlId);
            static const char* const mp9[] = {"None", "Sand Scale", "Mount Fuji", "Pandora's Box", "Hypnotic", "Army Sheen", "Dark Age", "Music Box", "Bioleak", "Ruby Poison Dart", "Stained Glass"};
            selectVar<MP9Skin>("MP9", mp9, 11, ++controlId);
            static const char* const p90[] = {"None", "Ancient Earth", "Astral Jörmungandr", "Cold Blooded", "Tiger Pit", "Baroque Red", "Module", "Leather", "Death by Kitty", "Emerald Dragon", "Run and Hide"};
            selectVar<P90Skin>("P90", p90, 11, ++controlId);
            static const char* const ppBizon[] = {"None", "Breaker Box", "Carbon Fiber", "Cobalt Halftone", "Brass", "Rust Coat", "RMX", "Traitor", "Osiris", "High Roller", "Antique"};
            selectVar<PPBizonSkin>("PP-Bizon", ppBizon, 11, ++controlId);
            static const char* const ump45[] = {"None", "Mechanism", "Fade", "Blaze", "Moonrise", "Carbon Fiber", "Oscillator", "Grand Prix", "Metal Flowers", "Minotaur's Labyrinth", "Briefing"};
            selectVar<UMP45Skin>("UMP-45", ump45, 11, ++controlId);
        });
        break;
    case 3:
        addCard("HEAVY", 6, [] {
            static const char* const m249[] = {"None", "Aztec", "Magma", "Deep Relief", "Downtown", "Submerged", "System Lock", "Spectre", "O.S.I.P.R.", "Nebula Crusader", "Warbird"};
            selectVar<M249Skin>("M249", m249, 11, ++controlId);
            static const char* const mag7[] = {"None", "Carbon Fiber", "Chainmail", "Hard Water", "Sonar", "Navy Sheen", "Metallic DDPAT", "Silver", "SWAG-7", "Rust Coat", "Heaven Guard"};
            selectVar<MAG7Skin>("MAG-7", mag7, 11, ++controlId);
            static const char* const negev[] = {"None", "Army Sheen", "Man-o'-war", "Anodized Navy", "Bratatat", "Loudmouth", "Drop Me", "dev_texture", "Power Loader", "Prototype", "Desert-Strike"};
            selectVar<NegevSkin>("Negev", negev, 11, ++controlId);
            static const char* const nova[] = {"None", "Army Sheen", "Graphite", "Red Quartz", "Gila", "Caged Steel", "Baroque Orange", "Exo", "Rust Coat", "Antique", "Plume"};
            selectVar<NovaSkin>("Nova", nova, 11, ++controlId);
            static const char* const sawedOff[] = {"None", "Amber Fade", "Brake Light", "Copper", "Morris", "Highwayman", "Zander", "Rust Coat", "First Class", "Limelight", "Apocalypto"};
            selectVar<SawedOffSkin>("Sawed-Off", sawedOff, 11, ++controlId);
            static const char* const xm1014[] = {"None", "Ancient Lore", "Charter", "Frost Borre", "Elegant Vines", "Bone Machine", "Zombie Offensive", "Blue Steel", "Teclu Burner", "XOXO", "Scumbria"};
            selectVar<XM1014Skin>("XM1014", xm1014, 11, ++controlId);
        });
        break;
    case 4:
        addCard("RIFLES", 7, [] {
            static const char* const ak47[] = {"None", "The Outsiders", "Hydroponic", "Searing Rage", "AUTOEXEC", "Crane Flight", "Consequence of the Jinn", "The Oligarch", "Inheritance", "Cartel", "Phantom Disruptor"};
            selectVar<AK47Skin>("AK-47", ak47, 11, ++controlId);
            static const char* const aug[] = {"None", "Amber Fade", "Death by Puppy", "Ricochet", "Midnight Lily", "Random Access", "Surveillance", "Carved Jade", "Flame Jörmungandr", "Anodized Navy", "Hot Rod"};
            selectVar<AUGSkin>("AUG", aug, 11, ++controlId);
            static const char* const famas[] = {"None", "Faulty Wiring", "Neural Net", "Meltdown", "Styx", "Prime Conspiracy", "Dark Water", "Sergeant", "Valence", "Djinn", "Afterimage"};
            selectVar<FamasSkin>("FAMAS", famas, 11, ++controlId);
            static const char* const galil[] = {"None", "Amber Fade", "Aqua Terrace", "Blue Titanium", "Rainbow Spoon", "Cerberus", "Chatterbox", "Black Sand", "Sugar Rush", "Chromatic Aberration", "Destroyer"};
            selectVar<GalilARSkin>("Galil AR", galil, 11, ++controlId);
            static const char* const m4a1s[] = {"None", "Fade", "Moss Quartz", "Atomic Alloy", "Blue Phosphor", "Knight", "Dark Water", "Hot Rod", "Basilisk", "Guardian", "Master Piece"};
            selectVar<M4A1SSkin>("M4A1-S", m4a1s, 11, ++controlId);
            static const char* const m4a4[] = {"None", "Asiimov", "Daybreak", "Bullet Rain", "Mainframe", "Global Offensive", "Howl", "龙王 (Dragon King)", "Cyber Security", "Desolate Space", "Poly Mag"};
            selectVar<M4A4Skin>("M4A4", m4a4, 11, ++controlId);
            static const char* const sg553[] = {"None", "Desert Blossom", "Lush Ruins", "Hypnotic", "Army Sheen", "Anodized Navy", "Damascus Steel", "Traveler", "Aerial", "Atlas", "Hazard Pay"};
            selectVar<SG553Skin>("SG 553", sg553, 11, ++controlId);
        });
        break;
    case 5:
        addCard("SNIPER RIFLES", 4, [] {
            static const char* const awp[] = {"None", "Graphite", "Worm God", "Man-o'-war", "Fade", "PAW", "Lightning Strike", "Silk Tiger", "Black Box", "Ice Coaled", "LongDog"};
            selectVar<AWPSkin>("AWP", awp, 11, ++controlId);
            static const char* const g3sg1[] = {"None", "Ancient Ritual", "Murky", "Violet Murano", "Chronos", "Black Sand", "The Executioner", "Dream Glade", "Keeping Tabs", "High Seas", "Hunter"};
            selectVar<G3SG1Skin>("G3SG1", g3sg1, 11, ++controlId);
            static const char* const scar20[] = {"None", "Army Sheen", "Carbon Fiber", "Emerald", "Brass", "Grotto", "Blueprint", "Wild Berry", "Cardiac", "Assault", "Cyrex"};
            selectVar<SCAR20Skin>("SCAR-20", scar20, 11, ++controlId);
            static const char* const ssg08[] = {"None", "Acid Fade", "Carbon Fiber", "Threat Detected", "Dark Water", "Abyss", "Blood in the Water", "Parallax", "Death's Head", "Dragonfire", "Fever Dream"};
            selectVar<SSG08Skin>("SSG 08", ssg08, 11, ++controlId);
        });
        break;
    }
}

// The knife preview: the selected model + finish with a finish-derived color swatch (the colors
// are hashed from the finish name - a real paint image needs a VPK -> texture pipeline, this is
// the honest stand-in). Updates live as the Knives category selects change.
void pageInventoryPreview() noexcept
{
    using namespace skin_changer_vars;
    addCard("KNIFE PREVIEW", 3, [] {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const int model = static_cast<int>(ui_config::get<KnifeModel>());
        const int finish = static_cast<int>(ui_config::get<KnifeSkin>());
        const bool none = model <= 0 || finish <= 0 || model >= static_cast<int>(sizeof(kKnifeModels) / sizeof(kKnifeModels[0])) || finish >= static_cast<int>(sizeof(kKnifeFinishes) / sizeof(kKnifeFinishes[0]));

        // row 1: star + model name, large
        {
            const float rowY = card.origin.y + card.row * kRowHeight;
            if (card.row)
                d->AddLine(ImVec2(card.origin.x + s(12), rowY), ImVec2(card.origin.x + card.width - s(12), rowY), C(26, 26, 30));
            if (none) {
                textY(d, card.origin.x + s(13), rowY, kRowHeight, C(110, 114, 124), "pick a knife and a finish", kTextControl, nullptr);
            } else {
                textY(d, card.origin.x + s(13), rowY, kRowHeight, g_accent, kIconStar, s(14.0f), iconFont()); // star
                textY(d, card.origin.x + s(36), rowY, kRowHeight, C(226, 228, 235), kKnifeModels[model], kTextTitle, strongFont());
            }
            ++card.row;
        }
        // row 2: finish name in the accent
        {
            const float rowY = card.origin.y + card.row * kRowHeight;
            if (none) {
                textY(d, card.origin.x + s(13), rowY, kRowHeight, C(110, 114, 124), "nothing equipped", kTextControl, nullptr);
            } else {
                textY(d, card.origin.x + s(13), rowY, kRowHeight, g_accent, kKnifeFinishes[finish], kTextControl, nullptr);
                const char* tag = "FINISH";
                const float tagWidth = ImGui::GetFont()->CalcTextSizeA(kTextCaption, FLT_MAX, 0.0f, tag).x;
                textY(d, card.origin.x + card.width - s(13) - tagWidth, rowY, kRowHeight, C(91, 96, 108), tag, kTextCaption, nullptr);
            }
            ++card.row;
        }
        // row 3: finish swatch - deterministic two-hue gradient hashed from the finish name
        {
            const float rowY = card.origin.y + card.row * kRowHeight;
            float r1 = 0.45f, g1 = 0.45f, b1 = 0.48f, r2 = 0.25f, g2 = 0.25f, b2 = 0.28f;
            if (!none) {
                std::uint32_t hash = 2166136261u;
                for (const char* p = kKnifeFinishes[finish]; *p; ++p) {
                    hash ^= static_cast<std::uint8_t>(*p);
                    hash *= 16777619u;
                }
                ImGui::ColorConvertHSVtoRGB(static_cast<float>(hash & 0xFFu) / 255.0f, 0.62f, 1.0f, r1, g1, b1);
                ImGui::ColorConvertHSVtoRGB(static_cast<float>((hash >> 8) & 0xFFu) / 255.0f, 0.62f, 1.0f, r2, g2, b2);
            }
            const ImVec2 sw(card.origin.x + s(13), rowY + rowCentered(s(16)));
            const float swWidth = card.width - s(26);
            const ImU32 c1 = C(static_cast<int>(r1 * 255), static_cast<int>(g1 * 255), static_cast<int>(b1 * 255));
            const ImU32 c2 = C(static_cast<int>(r2 * 255), static_cast<int>(g2 * 255), static_cast<int>(b2 * 255));
            d->AddRectFilledMultiColor(sw, ImVec2(sw.x + swWidth, sw.y + s(16)), c1, c2, c2, c1);
            d->AddRect(sw, ImVec2(sw.x + swWidth, sw.y + s(16)), C(30, 30, 33), s(4));
            ++card.row;
        }
    });
}

void pageInventory() noexcept
{
    if (!searchIndexing) {
        inventoryPills(ImGui::GetWindowDrawList());
        controlId += inventoryCategory * 500; // per-category control-id ranges
        pageInventoryCategory(inventoryCategory);
        if (inventoryCategory == 0)
            pageInventoryPreview(); // the preview is knife-specific - knives tab only
        return;
    }

    // ghost render for the search index: lay out EVERY category exactly as it appears when
    // active (so a search hit scrolls to the right offset after opening its category)
    for (int category = 0; category < 6; ++category) {
        searchGlowSubTab = category; // reused as the generic sub-tab tag of the indexed row
        inventoryPills(ImGui::GetWindowDrawList());
        if (category)
            controlId += 500;
        pageInventoryCategory(category);
        if (category == 0)
            pageInventoryPreview();
        columnYs[0] = columnYs[1] = 0;
    }
}

// --- web radio (dedicated tab) -------------------------------------------------------
//
// TuneIn/RadioTime web radio, driven through RadioManager: the regional local-station list,
// free-text station search, and click-to-play. Fetches and playback run asynchronously on the
// HOST (steam-runtime-launch-client -> curl/ffplay, see RadioManager.h); this page only draws
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
    constexpr int kFixedRows = 4; // search / now playing / volume / results header
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
    d->AddRectFilled(p, p + ImVec2(width, height), C(16, 16, 18, 224), s(14.0f));
    d->AddRect(p, p + ImVec2(width, height), C(30, 30, 33), s(14.0f));

    card = CardContext{p + ImVec2(0, s(6.0f)), width, 0};

    auto pillButton = [&](int id, const char* label, ImVec2 pos, float buttonWidth, ImFont* font, float fontSize) {
        ImGui::PushID(id);
        const bool clicked = hit("##pill_btn", pos, ImVec2(buttonWidth, s(23)));
        const float hover = motion(ImGui::GetItemID() ^ 0x3a11u, ImGui::IsItemHovered() ? 1.0f : 0.0f);
        ImGui::PopID();
        d->AddRectFilled(pos, pos + ImVec2(buttonWidth, s(23)), mix(C(24, 24, 26), C(32, 32, 36), hover), s(5));
        d->AddRect(pos, pos + ImVec2(buttonWidth, s(23)), mix(C(30, 30, 33), g_accent, hover), s(5));
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
        ImGui::PushStyleColor(ImGuiCol_FrameBg, C(23, 23, 25));
        ImGui::PushStyleColor(ImGuiCol_Text, C(207, 209, 218));
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

    // Row 2: volume (standard slider primitive; applied to the next play).
    sliderVar<radio_vars::Volume>("Volume", ++controlId, "%");

    // Sections: persisted favorites (star toggles back off) and this session's recently played.
    auto sectionHeader = [&](const char* title, const char* right) {
        const float rowY = card.origin.y + card.row * kRowHeight;
        if (card.row)
            d->AddLine(ImVec2(card.origin.x + s(12), rowY), ImVec2(card.origin.x + width - s(12), rowY), C(26, 26, 30));
        textY(d, card.origin.x + s(13), rowY, kRowHeight, C(150, 154, 165), title, kTextControl, nullptr);
        if (right && right[0] != '\0') {
            const float rightWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, right).x;
            textY(d, card.origin.x + width - s(13) - rightWidth, rowY, kRowHeight, C(110, 114, 124), right, kTextSmall, nullptr);
        }
        ++card.row;
    };

    auto savedStationRow = [&](int idSalt, const char* id, const char* name, bool starred, bool playing) {
        const float rowY = card.origin.y + card.row * kRowHeight;
        d->AddLine(ImVec2(card.origin.x + s(12), rowY), ImVec2(card.origin.x + width - s(12), rowY), C(26, 26, 30));

        // star toggle at the right edge: on favorites rows it removes; on result rows it adds
        const float starX = p.x + width - s(13) - s(22);
        ImGui::PushID(idSalt);
        const bool starClicked = hit("##radio_star", ImVec2(starX, rowY + rowCentered(s(22))), ImVec2(s(22), s(22))) && !searchIndexing;
        const float starHover = motion(ImGui::GetItemID() ^ 0x5a27u, ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
        ImGui::PopID();
        if (starClicked)
            withRadio([&](auto&& radio) { radio.toggleFavorite(id, name); });

        // name area plays the station
        ImGui::PushID(idSalt + 10000);
        const bool clicked = hit("##radio_saved", ImVec2(p.x + s(4), rowY), ImVec2(width - s(8) - s(26), kRowHeight));
        const float hover = motion(ImGui::GetItemID() ^ 0xbe22u, ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
        ImGui::PopID();
        if (hover > 0.001f)
            d->AddRectFilled(ImVec2(p.x + s(4), rowY), ImVec2(p.x + width - s(4) - s(26), rowY + kRowHeight), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(14 * hover) << IM_COL32_A_SHIFT), s(6));

        textY(d, p.x + s(13), rowY, kRowHeight, starred ? g_accent : mix(C(110, 114, 124), C(190, 194, 204), starHover), kIconStar, s(12.0f), iconFont()); // star
        textY(d, p.x + s(38), rowY, kRowHeight, playing ? g_accent : C(207, 209, 218), name, kTextControl, nullptr);
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
            d->AddLine(ImVec2(card.origin.x + s(12), rowY), ImVec2(card.origin.x + width - s(12), rowY), C(26, 26, 30));
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
            d->AddLine(ImVec2(card.origin.x + s(12), rowY), ImVec2(card.origin.x + width - s(12), rowY), C(26, 26, 30));

            ImGui::PushID(4200 + i);
            const bool clicked = hit("##station", ImVec2(p.x + s(4), rowY), ImVec2(width - s(8) - s(26), kRowHeight));
            const float hover = motion(ImGui::GetItemID() ^ (0xbe21u + i), ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
            ImGui::PopID();
            if (hover > 0.001f)
                d->AddRectFilled(ImVec2(p.x + s(4), rowY), ImVec2(p.x + width - s(4) - s(26), rowY + kRowHeight), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(14 * hover) << IM_COL32_A_SHIFT), s(6));

            // favorite star at the right edge of the row
            {
                const float starX = p.x + width - s(13) - s(22);
                const bool fav = i < kStationSnap && stationFavSnap[i];
                ImGui::PushID(4400 + i);
                const bool starClicked = hit("##radio_star", ImVec2(starX, rowY + rowCentered(s(22))), ImVec2(s(22), s(22))) && !searchIndexing;
                const float starHover = motion(ImGui::GetItemID() ^ (0x5a27u + i), ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
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
            textY(d, p.x + s(nameX), rowY, kRowHeight, playing ? g_accent : C(207, 209, 218), st.text, kTextControl, nullptr);

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
        case Page::Misc: pageMisc(); break;
        }
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
    d->AddRectFilled(contentMin, contentMax, C(12, 12, 13, 250), s(14));

    // input field (autofocused on the open frame)
    ImGui::SetCursorScreenPos(contentMin + ImVec2(s(16), s(16)));
    ImGui::PushItemWidth(contentMax.x - contentMin.x - s(32));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, C(23, 23, 25));
    ImGui::PushStyleColor(ImGuiCol_Border, g_accent);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s(8), s(6)));
    if (ImGui::GetFrameCount() == searchOpenedFrame)
        ImGui::SetKeyboardFocusHere(0);
    ImGui::InputTextWithHint("##search_query", "search settings...", searchQuery, sizeof(searchQuery));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopItemWidth();

    // Input diagnostics: the field once filled with '?' - log the raw UTF-8 bytes we receive
    // (on change only) plus whether SDL text input is actually active, so a layout/encoding
    // problem is visible in the log instead of guessable only from rendered glyphs.
    static char lastLoggedQuery[sizeof(searchQuery)] = "";
    if (std::strcmp(searchQuery, lastLoggedQuery) != 0) {
        std::memcpy(lastLoggedQuery, searchQuery, sizeof(lastLoggedQuery));
        char hex[3 * sizeof(searchQuery)] = "";
        std::size_t hexIndex = 0;
        for (std::size_t i = 0; i < sizeof(searchQuery) && searchQuery[i]; ++i)
            hexIndex += static_cast<std::size_t>(std::snprintf(hex + hexIndex, sizeof(hex) - hexIndex, "%02X ", static_cast<unsigned char>(searchQuery[i])));
        const bool textInputActive = gui_sdl::functions.textInputActive && gui_sdl::functions.textInputActive(gui_sdl::window);
        gui_log::write("search query: [%s] bytes: %s(text input %s)", searchQuery, hex, textInputActive ? "active" : "INACTIVE");
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(b, b + ImVec2(kSidebarWidth, kShellHeight)) && ImGui::GetFrameCount() > searchOpenedFrame))
        searchOpen = false;

    if (!searchQuery[0]) {
        textY(d, contentMin.x + s(16), contentMin.y + s(58), s(24), C(110, 114, 124), "type to search across every page", kTextSmall, nullptr);
        return;
    }

    // results
    const float rowHeight = s(30.0f);
    const float listTop = contentMin.y + s(56.0f);
    int shown = 0;
    int firstMatch = -1;
    for (int i = 0; i < searchIndexCount && shown < 9; ++i) {
        if (!searchMatches(searchIndex[i].label, searchQuery))
            continue;

        if (firstMatch < 0)
            firstMatch = i;
        const ImVec2 rp(contentMin.x + s(12), listTop + shown * rowHeight);
        ImGui::PushID(7000 + i);
        const bool clicked = hit("##search_row", rp, ImVec2(contentMax.x - contentMin.x - s(24), rowHeight));
        const float hover = motion(ImGui::GetItemID() ^ 0x9e21u, ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
        ImGui::PopID();
        if (hover > 0.001f)
            d->AddRectFilled(rp, rp + ImVec2(contentMax.x - contentMin.x - s(24), rowHeight), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(22 * hover) << IM_COL32_A_SHIFT), s(8));
        textY(d, rp.x + s(10), rp.y, rowHeight, C(207, 209, 218), searchIndex[i].label, kTextControl, nullptr);
        const char* pageName = kPageNames[static_cast<int>(searchIndex[i].page)];
        const float pageNameWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, pageName).x;
        textY(d, rp.x + (contentMax.x - contentMin.x - s(24)) - pageNameWidth - s(10), rp.y, rowHeight, C(110, 114, 124), pageName, kTextSmall, nullptr);

        if (clicked) {
            const bool visualsPage = searchIndex[i].page >= Page::PlayerInfo && searchIndex[i].page <= Page::Sound;
            if (searchIndex[i].page == Page::Glow)
                glowSubTab = searchIndex[i].subTab;
            else if (searchIndex[i].page == Page::Inventory)
                inventoryCategory = searchIndex[i].subTab; // subTab doubles as the category tag
            changePage(searchIndex[i].page);
            if (visualsPage)
                state.visualsExpanded = true;
            scrollOffset = ImMax(0.0f, searchIndex[i].contentY - s(60.0f));
            scrollTarget = scrollOffset; // search jumps snap, no glide
            searchOpen = false;
        }
        ++shown;
    }

    if (shown == 0)
        textY(d, contentMin.x + s(16), listTop, rowHeight, C(110, 114, 124), "no matches", kTextSmall, nullptr);

    // enter jumps to the first match
    if ((ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) && firstMatch >= 0) {
        const bool visualsPage = searchIndex[firstMatch].page >= Page::PlayerInfo && searchIndex[firstMatch].page <= Page::Sound;
        if (searchIndex[firstMatch].page == Page::Glow)
            glowSubTab = searchIndex[firstMatch].subTab;
        else if (searchIndex[firstMatch].page == Page::Inventory)
            inventoryCategory = searchIndex[firstMatch].subTab;
        changePage(searchIndex[firstMatch].page);
        if (visualsPage)
            state.visualsExpanded = true;
        scrollOffset = ImMax(0.0f, searchIndex[firstMatch].contentY - s(60.0f));
        scrollTarget = scrollOffset; // search jumps snap, no glide
        searchOpen = false;
    }

    if (searchIndexCount >= kSearchIndexCap && shown < 9)
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
    d->AddRectFilled(base, base + ImVec2(kSidebarWidth, kShellHeight), C(16, 16, 18, 231), s(17.0f), ImDrawFlags_RoundCornersLeft);
    d->AddRectFilled(base + ImVec2(s(145), 0), base + ImVec2(kSidebarWidth, kShellHeight), C(16, 16, 18, 231));
    d->AddLine(base + ImVec2(kSidebarWidth, 0), base + ImVec2(kSidebarWidth, kShellHeight), C(30, 33, 43));

    d->AddRectFilled(base + ImVec2(s(15), s(11)), base + ImVec2(s(45), s(43)), C(22, 22, 25), s(7));
    text(d, base + ImVec2(s(21), s(18)), g_accent, "GS", kTextTitle, strongFont());
    text(d, base + ImVec2(s(53), s(14)), C(228, 230, 236), "Gamesense", kTextTitle, strongFont());
    text(d, base + ImVec2(s(53), s(33)), C(91, 96, 108), "Counter-Strike 2", s(8));
    d->AddLine(base + ImVec2(s(10), s(56)), base + ImVec2(s(147), s(56)), C(26, 26, 30));

    int id = 100;

    auto eyebrow = [&](const char* label) {
        text(d, base + ImVec2(s(16), y_nav), C(91, 96, 108), label, kTextCaption, nullptr);
        y_nav += s(20.0f);
    };

    auto nav = [&](const char* icon, const char* label, Page target, float indent = 0.0f) {
        const ImVec2 p = base + ImVec2(s(7.0f + indent), y_nav);
        ImGui::PushID(++id);
        const bool clicked = hit("##nav", p, ImVec2(s(140.0f - indent), s(30.0f)));
        const ImGuiID iid = ImGui::GetItemID();
        const bool selected = state.page == target;
        const float r = motion(iid ^ 0x7771u, selected ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
        ImGui::PopID();
        if (clicked)
            changePage(target);
        if (r > 0.001f)
            d->AddRectFilled(p, p + ImVec2(s(140.0f - indent), s(30.0f)), mix(C(16, 16, 18, 0), C(37, 37, 41), r), s(6));
        textY(d, p.x + s(10), p.y, s(30), mix(C(137, 142, 153), g_accent, r), icon, kTextIcon, iconFont());
        textY(d, p.x + s(31), p.y, s(30), mix(C(145, 149, 159), C(226, 228, 235), r), label, kTextBody, nullptr);
        y_nav += s(32.0f);
    };

    // The expanded Visuals sub-list (8 pages) can outgrow the rail space between the logo and
    // the account bar - clamp the nav content with a clip rect and wheel-scroll it instead of
    // letting Misc slide behind the Gamesense chip.
    const float expand = motion(ImGui::GetID("##visual_expand"), state.visualsExpanded ? 1.0f : 0.0f, 18.0f);
    const float navTop = base.y + s(58.0f);
    const float navBottom = base.y + kShellHeight - s(45.0f) - s(4.0f);
    float navMaxScroll = 0.0f;
    {
        const float contentEnd = base.y + s(64.0f)
            + s(20.0f) + 2 * s(32.0f) + 2.0f          // AIMBOT group
            + s(20.0f) + s(32.0f)                      // FEATURES header
            + 7 * s(32.0f) * expand + s(2.0f)          // expanded sub-list
            + s(20.0f) + 3 * s(32.0f);                 // OTHER group
        navMaxScroll = contentEnd > navBottom ? contentEnd - navBottom : 0.0f;
        if (navMaxScroll > 0.0f && ImGui::IsMouseHoveringRect(ImVec2(base.x, navTop), ImVec2(base.x + kSidebarWidth, navBottom)))
            navScroll -= ImGui::GetIO().MouseWheel * s(24.0f);
        navScroll = ImClamp(navScroll, 0.0f, navMaxScroll);
        if (navMaxScroll <= 0.0f)
            navScroll = 0.0f;
    }

    y_nav = s(64.0f) - navScroll;
    d->PushClipRect(ImVec2(base.x, navTop), ImVec2(base.x + kSidebarWidth, navBottom), true);
    eyebrow("AIMBOT");
    nav("\xEF\x81\x9B", "Rage", Page::Rage);   // crosshairs
    nav("\xEF\xA3\x8C", "Legit", Page::Legit); // mouse
    nav("\xEF\x95\x8B", "Movement", Page::Movement); // shoe-prints
    y_nav += 2.0f;

    eyebrow("FEATURES");
    // The group parent navigates to Player Info and toggles the sub-list.
    {
        const ImVec2 p = base + ImVec2(s(7), y_nav);
        ImGui::PushID(++id);
        const bool clicked = hit("##nav", p, ImVec2(s(140), s(30)));
        const ImGuiID iid = ImGui::GetItemID();
        const bool visuals = state.page >= Page::PlayerInfo && state.page <= Page::Sound;
        const float r = motion(iid ^ 0x7772u, visuals ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
        ImGui::PopID();
        if (clicked) {
            const bool wasVisuals = visuals;
            state.visualsExpanded = wasVisuals ? !state.visualsExpanded : true;
            changePage(Page::PlayerInfo);
        }
        if (r > 0.001f)
            d->AddRectFilled(p, p + ImVec2(s(140), s(30)), mix(C(16, 16, 18, 0), C(37, 37, 41), r), s(6));
        textY(d, p.x + s(10), p.y, s(30), mix(C(137, 142, 153), g_accent, r), "\xEF\x80\xBE", kTextIcon, iconFont()); // image
        textY(d, p.x + s(31), p.y, s(30), mix(C(145, 149, 159), C(226, 228, 235), r), "Visuals", kTextBody, nullptr);
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
    nav("\xEF\x80\x93", "Misc", Page::Misc);           // cog
    d->PopClipRect();

    // Scroll affordance for the rail: bottom fade + thin thumb whenever the sub-list overflows.
    if (navMaxScroll > 1.0f) {
        const ImU32 bgFade = C(16, 16, 18, 231);
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
    d->AddRectFilled(base + ImVec2(kSidebarWidth, 0), base + ImVec2(kShellWidth, kToolbarHeight), C(10, 10, 11, 225), s(17.0f), ImDrawFlags_RoundCornersTopRight);
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
            const ImVec2 defaultPos((display.x - kShellWidth) * 0.5f, (display.y - kShellHeight) * 0.5f);
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
    const ImGuiID iid = ImGui::GetItemID();
    const float response = motion(iid ^ 0x51e7u, (configPopoverOpen || ImGui::IsItemHovered()) ? 1.0f : 0.0f);
    ImGui::PopID();
    if (chipClicked) {
        configPopoverOpen = !configPopoverOpen;
        configPopoverOpenedFrame = ImGui::GetFrameCount();
    }
    d->AddRectFilled(profile, configChipMax, mix(C(16, 16, 18), C(23, 23, 25), response), s(6));
    d->AddRect(profile, configChipMax, mix(C(26, 26, 30), g_accent, response), s(6));
    textY(d, profile.x + s(12), profile.y, s(30), g_accent, "\xEF\x83\x87", kTextIcon, iconFont()); // save
    textY(d, profile.x + s(34), profile.y, s(30), C(184, 187, 197), ui_config::activeConfigNameForDisplay(), kTextControl, nullptr);
    chevron(d, profile + ImVec2(s(160), s(11)), C(130, 135, 146));

    // Global search button, left of Unload.
    const ImVec2 searchBtn = base + ImVec2(kShellWidth - s(78) - s(42), s(14));
    const ImVec2 searchSize = ImVec2(s(34), s(30));
    searchButtonMin = searchBtn;
    searchButtonMax = searchBtn + searchSize;
    ImGui::PushID(9002);
    const bool searchClicked = hit("##search", searchBtn, searchSize);
    const ImGuiID searchIid = ImGui::GetItemID();
    const float searchHover = motion(searchIid ^ 0x5eabu, (searchOpen || ImGui::IsItemHovered()) ? 1.0f : 0.0f);
    ImGui::PopID();
    if (searchClicked) {
        searchOpen = !searchOpen;
        searchOpenedFrame = ImGui::GetFrameCount();
        if (searchOpen)
            searchQuery[0] = '\0';
    }
    d->AddRectFilled(searchBtn, searchBtn + searchSize, mix(C(16, 16, 18), C(23, 23, 25), searchHover), s(6));
    d->AddRect(searchBtn, searchBtn + searchSize, mix(C(26, 26, 30), g_accent, searchHover), s(6));
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
    const ImGuiID uid = ImGui::GetItemID();
    const float unloadHover = motion(uid ^ 0x0ea1u, ImGui::IsItemHovered() ? 1.0f : 0.0f);
    ImGui::PopID();
    d->AddRectFilled(unload, unload + unloadSize, mix(C(30, 17, 20), C(120, 34, 40), unloadHover), s(6));
    d->AddRect(unload, unload + unloadSize, mix(C(50, 28, 32), C(190, 60, 66), unloadHover), s(6));
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
    const ImGuiID iid = ImGui::GetItemID();
    const float r = motion(iid ^ 0x1932u, state.profileOpen ? 1.0f : ImGui::IsItemHovered() ? 0.5f : 0.0f);
    ImGui::PopID();
    if (clicked) {
        state.profileOpen = !state.profileOpen;
        state.profileOpenedFrame = ImGui::GetFrameCount();
    }
    if (r > 0.001f)
        d->AddRectFilled(account, account + ImVec2(barWidth, s(38)), C(37, 37, 41, static_cast<int>(235 * r)), s(6));

    // avatar: user image from <Osiris dir>/avatar.png once uploaded, GS monogram fallback.
    const ImVec2 avatar = account + ImVec2(s(7), s(5));
    const float avatarRadius = s(14);
    const ImTextureID avatarTex = reinterpret_cast<ImTextureID>(VulkanHook::avatar_texture::query());
    if (avatarTex)
        d->AddImageRounded(avatarTex, avatar, avatar + ImVec2(avatarRadius * 2.0f, avatarRadius * 2.0f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), C(255, 255, 255, 255), avatarRadius);
    else {
        d->AddCircleFilled(avatar + ImVec2(avatarRadius, avatarRadius), avatarRadius, C(22, 22, 25));
        textY(d, avatar.x + s(5), avatar.y, s(28), g_accent, "GS", kTextControl, strongFont());
    }
    d->AddCircle(avatar + ImVec2(avatarRadius, avatarRadius), avatarRadius, g_accent, 0, s(2));
    text(d, account + ImVec2(s(43), s(4)), C(225, 227, 233), "Gamesense", kTextControl, nullptr);
    text(d, account + ImVec2(s(43), s(20)), C(111, 116, 128), "INSERT to toggle", kTextSmall, nullptr);
    chevron(d, account + ImVec2(barWidth - s(9), s(16)), C(181, 185, 195));
}

void profilePopover(ImDrawList* d, ImVec2 base) noexcept
{
    const float open = motion(ImGui::GetID("##profile_open"), state.profileOpen ? 1.0f : 0.0f, 17.0f, 0.0f);
    if (open < 0.002f)
        return;

    const float width = s(210.0f);
    const float rowHeight = s(30.0f);
    const float height = rowHeight * 14.0f + s(16.0f) + s(26.0f); // scale block, style, 3 theme colors, 6 glow rows, esp, about
    const ImVec2 p = base + ImVec2(s(7.0f), kShellHeight - s(45.0f) - height * open - s(6.0f));
    const ImVec2 size(width, height);
    recordPopupRect(PopupProfile, p, p + size);
    const int first = d->VtxBuffer.Size;
    softShadow(d, p, p + size, s(16.0f));
    d->AddRectFilled(p - ImVec2(s(4), s(2)), p + size + ImVec2(s(4), s(7)), C(0, 0, 0, 65), s(18));
    d->AddRectFilled(p, p + size, C(23, 23, 25, 245), s(16));
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
    const ImGuiID scaleSliderId = ImGui::GetItemID();
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
        d->AddRectFilled(pill, pill + ImVec2(s(44), s(21)), C(24, 24, 26), s(5));

        ImGui::PushID(kScaleEditId);
        if (editing) {
            ImGui::SetCursorScreenPos(pill + ImVec2(s(4), s(3)));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, C(30, 30, 33));
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
        const float shown = motion(scaleSliderId ^ 0x5ca1eu, (previewScale - 0.75f) / 1.25f, 16.0f, (menuScale - 0.75f) / 1.25f);
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
        const float hover = motion(ImGui::GetItemID() ^ 0x57e1u, ImGui::IsItemHovered() ? 1.0f : 0.0f);
        ImGui::PopID();
        d->AddRectFilled(sp, sp + ImVec2(s(72.0f), s(20.0f)), mix(C(24, 24, 26), C(32, 32, 36), hover), s(5));
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
            const float hover = motion(ImGui::GetItemID() ^ (0x6d10u + static_cast<unsigned>(id)), ImGui::IsItemHovered() ? 1.0f : 0.0f);
            ImGui::PopID();
            d->AddRectFilled(sp, sp + ImVec2(s(72.0f), s(20.0f)), mix(C(24, 24, 26), C(32, 32, 36), hover), s(5));
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
            const bool clicked = hitPopupRow("##glow_toggle", tp, ImVec2(s(29.0f), s(18.0f)), PopupProfile);
            const float r = motion(ImGui::GetItemID() ^ 0x6d13u, glowOn ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
            ImGui::PopID();
            if (r > 0.001f)
                d->AddRectFilled(tp, tp + ImVec2(s(29), s(18)), mix(C(27, 27, 29), g_buttonAccent, r), s(9));
            d->AddCircleFilled(tp + ImVec2(ImLerp(s(9), s(20), r), s(9)), s(7), mix(C(133, 133, 138), C(248, 249, 252), r));
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
            const bool clicked = hitPopupRow("##glow_rainbow", tp, ImVec2(s(29.0f), s(18.0f)), PopupProfile);
            const float r = motion(ImGui::GetItemID() ^ 0x6d14u, rainbow ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
            ImGui::PopID();
            if (r > 0.001f)
                d->AddRectFilled(tp, tp + ImVec2(s(29), s(18)), mix(C(27, 27, 29), g_buttonAccent, r), s(9));
            d->AddCircleFilled(tp + ImVec2(ImLerp(s(9), s(20), r), s(9)), s(7), mix(C(133, 133, 138), C(248, 249, 252), r));
            if (clicked)
                ui_config::set<MenuGlowRainbow>(!rainbow);
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
            const float shown = motion(ImGui::GetItemID() ^ 0x6d15u, (value - min) / (max - min), 16.0f, (value - min) / (max - min));
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
            const float shown = motion(ImGui::GetItemID() ^ 0x6d16u, (value - min) / (max - min), 16.0f, (value - min) / (max - min));
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
            ImGui::PushID(8718);
            const bool clicked = hitPopupRow("##glow_debug", tp, ImVec2(s(29.0f), s(18.0f)), PopupProfile);
            const float r = motion(ImGui::GetItemID() ^ 0x6d17u, debug ? 1.0f : ImGui::IsItemHovered() ? 0.48f : 0.0f);
            ImGui::PopID();
            if (r > 0.001f)
                d->AddRectFilled(tp, tp + ImVec2(s(29), s(18)), mix(C(27, 27, 29), g_buttonAccent, r), s(9));
            d->AddCircleFilled(tp + ImVec2(ImLerp(s(9), s(20), r), s(9)), s(7), mix(C(133, 133, 138), C(248, 249, 252), r));
            if (clicked)
                ui_config::set<MenuGlowDebug>(!debug);
        }
        y += rowHeight;
    }

    // ESP Scale: placeholder until ImGui-based ESP rendering exists.
    textY(d, p.x + s(14), y, rowHeight, C(92, 96, 107), "ESP Scale", kTextControl, nullptr);
    {
        const ImVec2 soonPill(p.x + width - s(66.0f), y + rowCentered(s(20.0f)));
        d->AddRectFilled(soonPill, soonPill + ImVec2(s(48.0f), s(20.0f)), C(24, 24, 26), s(5));
        textY(d, soonPill.x, soonPill.y, s(20.0f), C(110, 114, 124), "Soon", kTextSmall, nullptr);
    }
    y += rowHeight;

    textY(d, p.x + s(14), y, rowHeight, C(150, 154, 165), "Gamesense - neverlose UI port", kTextSmall, nullptr);

    const float eased = 1.0f - std::pow(1.0f - open, 3.0f);
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
    d->AddRectFilled(p, p + size, C(18, 18, 20, 240), s(16));
    d->AddRect(p, p + size, C(54, 54, 60, 205), s(16));
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
        const float hover = motion(ImGui::GetItemID() ^ (0x51e1u + i), ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
        ImGui::PopID();
        if (hover > 0.001f && i != activeIndex)
            d->AddRectFilled(rp, rp + ImVec2(width - s(8), s(28)), (g_accent & 0x00FFFFFFu) | (static_cast<ImU32>(25 * hover) << IM_COL32_A_SHIFT), s(8));
        const bool active = i == activeIndex;
        if (active)
            d->AddRectFilled(rp + ImVec2(s(2), s(5)), rp + ImVec2(s(5), s(23)), g_accent, s(3));
        textY(d, rp.x + s(12), rp.y, s(28), active ? C(224, 229, 243) : C(182, 185, 196), ui_config::listedConfigName(i), kTextControl, nullptr);
        if (clicked && !active)
            ui_config::switchToConfig(i);

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
            if (deleted && !ui_config::deleteConfig(i))
                gui_log::write("config: failed to delete '%s'", ui_config::listedConfigName(i));
        }
    }

    // actions: SAVE / RESTORE DEFAULTS
    float ay = p.y + searchHeight + listHeight + s(4.0f);
    auto actionButton = [&](int id, const char* label) {
        const ImVec2 bp = p + ImVec2(s(8), ay - p.y);
        ImGui::PushID(id);
        const bool clicked = hitPopupRow("##cfg_action", bp, ImVec2(width - s(16), s(26)), PopupConfig);
        const float hover = motion(ImGui::GetItemID() ^ (0xa1a1u + static_cast<unsigned>(id)), ImGui::IsItemHovered() ? 1.0f : 0.0f);
        ImGui::PopID();
        d->AddRectFilled(bp, bp + ImVec2(width - s(16), s(26)), mix(C(24, 24, 26), C(37, 37, 41), hover), s(6));
        textY(d, bp.x + s(10), bp.y, s(26), C(182, 185, 196), label, kTextControl, nullptr);
        ay += s(30.0f);
        return clicked;
    };
    if (actionButton(0, "SAVE"))
        ui_config::saveActive();
    if (actionButton(1, "DUPLICATE"))
        static_cast<void>(ui_config::duplicateActiveConfig()); // switches to "<active>_copy.cfg" on success
    if (actionButton(2, "RESTORE DEFAULTS"))
        ui_config::restoreDefaults();

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
        if (ui_config::createAndSwitchToConfig(newConfigName))
            newConfigName[0] = '\0';
    }
    ImGui::SameLine(0.0f, s(4));
    if (ImGui::Button("RENAME", ImVec2(s(70.0f), 0.0f))) {
        if (ui_config::renameActiveConfig(newConfigName))
            newConfigName[0] = '\0';
    }
    ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f)); // park the cursor; nothing flow-laid-out follows

    const float eased = 1.0f - std::pow(1.0f - open, 3.0f);
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
    const ImVec2 defaultPos((display.x - kShellWidth) * 0.5f, (display.y - kShellHeight) * 0.5f);
    const ImVec2 shellPos(defaultPos + menuOffset);

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

        softShadow(d, b, b + ImVec2(kShellWidth, kShellHeight), s(17.0f));

        d->AddRectFilled(b, b + ImVec2(kShellWidth, kShellHeight), C(12, 12, 13, 248), s(17.0f));
        d->AddRect(b, b + ImVec2(kShellWidth, kShellHeight), C(30, 30, 33), s(17.0f));

        sidebar(d, b);
        toolbar(d, b);
        accountBar(d, b);
        const int contentFirst = d->VtxBuffer.Size;

        // content region: clip + manual scroll
        const ImVec2 contentMin(b.x + kSidebarWidth + s(2), b.y + kToolbarHeight + s(24));
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
        case Page::Misc: pageMisc(); break;
        }

        const float contentHeight = ImMax(columnYs[0], columnYs[1]);
        const float visibleHeight = contentMax.y - contentMin.y;
        const float previousScrollOffset = scrollOffset;
        maxScroll = ImMax(0.0f, contentHeight - visibleHeight);
        if (ImGui::IsMouseHoveringRect(contentMin, contentMax) && ImGui::GetIO().MouseWheel != 0.0f)
            scrollTarget = ImClamp(scrollTarget - ImGui::GetIO().MouseWheel * 40.0f, 0.0f, maxScroll);
        if (scrollTarget > maxScroll)
            scrollTarget = maxScroll;
        // ease toward the target; settle exactly once close enough (avoids endless subpixel
        // text shimmer from a decaying-but-never-exact lerp)
        scrollOffset = ImLerp(scrollOffset, scrollTarget, 1.0f - std::exp(-14.0f * ImGui::GetIO().DeltaTime));
        if (std::fabs(scrollTarget - scrollOffset) < 0.3f)
            scrollOffset = scrollTarget;
        // the hand-drawn popups anchor to screen positions of the rows that opened them - once
        // the content scrolls those anchors are stale, so close them
        if (std::fabs(scrollOffset - previousScrollOffset) > 0.5f) {
            state.popup.open = false;
            state.colorPickerOpen = false;
            state.multiSelectOpen = false;
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
        // edge fade wherever more content hides beyond the edge, plus a thin position thumb on
        // the right edge while the page overflows.
        {
            const ImU32 bgFade = C(12, 12, 13, 246);
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
                const float thumbY = contentMin.y + s(4.0f) + t * (track - thumbHeight);
                d->AddRectFilled(ImVec2(contentMax.x - s(5.0f), thumbY), ImVec2(contentMax.x - s(2.0f), thumbY + thumbHeight), C(255, 255, 255, 26), s(2));
            }
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

        // game-anchored overlay: hitmarker moved to
        // neverlose::renderGameOverlay - they draw every frame, with the menu closed too.

        // reveal: scale + fade the whole shell - forward from the open moment, reversed while
        // the menu dismisses (GUI.cpp keeps render() alive past the alpha fade until this
        // lands). The transform is symmetric in reveal, so the same vertex pass serves both.
        {
            if (dismissActive) {
                reveal = ImLerp(reveal, 0.0f, 1.0f - std::exp(-9.0f * ImGui::GetIO().DeltaTime));
                if (reveal < 0.02f) {
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
            d->AddRectFilled(sp, sp + size, C(18, 18, 20, 240), s(12));
            d->AddRect(sp, sp + size, C(54, 54, 60, 205), s(12));
            for (int i = 0; i < count; ++i) {
                ImVec2 rp = sp + ImVec2(s(5), s(5) + i * s(28.0f));
                ImGui::PushID(8900 + i);
                const bool clicked = hitModal("##style_row", rp, ImVec2(size.x - s(10), s(28)));
                const float hover = motion(ImGui::GetItemID() ^ (0x77e1u + i), ImGui::IsItemHovered() ? 1.0f : 0.0f, 22.0f);
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
        colorPickerPopover(d);
        featureBindPopover(d);

        // rotate the popup rect snapshot: what the popups drew THIS frame gates page controls
        // NEXT frame (popupPrev holds the complete last-frame set; popupCur starts empty again)
        for (int i = 0; i < PopupKindCount; ++i) {
            popupPrev[i] = popupCur[i];
            popupCur[i].valid = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// Game-anchored overlay pass: runs EVERY frame from GUI::render (independent of menu alpha),
// because the hitmarker and the player list are gameplay HUD, not menu.
void drawPlayerListWindow() noexcept; // defined below
void drawBindsListWindow(float extraYOffset) noexcept; // defined below
void drawSpectatorListWindow(float& yOffsetForBindsList) noexcept; // defined below

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
    const ImVec2 shellPos((display.x - kShellWidth) * 0.5f + menuOffset.x, (display.y - kShellHeight) * 0.5f + menuOffset.y);
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
    drawBindsListWindow(bindsListOffset);
}

// --- spectator list (in-game HUD overlay) ------------------------------------------------
// The friend-client port: boxed list of who is watching the POV - ours when alive, the
// spectated player's when dead. Names come from SpectatorSnapshot (game thread collects,
// this pass draws); hidden while nobody is spectating.

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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, C(12, 12, 13, 248));
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
            textY(d, winPos.x + s(12), y, rowHeight, C(207, 209, 218), snap.names[i], kTextControl, nullptr);
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

void drawBindsListWindow(float extraYOffset) noexcept
{
    if (!ui_config::get<binds_list_vars::Enabled>())
        return;

    const BindListEntry entries[] = {
        {"Legit Aim", static_cast<int>(static_cast<legit_aimbot_vars::AimKey::ValueType::ValueType>(ui_config::get<legit_aimbot_vars::AimKey>()))},
        {"Triggerbot", static_cast<int>(static_cast<triggerbot_vars::HoldKey::ValueType::ValueType>(ui_config::get<triggerbot_vars::HoldKey>()))},
        {"Combat Panic", static_cast<int>(static_cast<panic_vars::Bind::ValueType::ValueType>(ui_config::get<panic_vars::Bind>()))},
    };

    constexpr ImGuiWindowFlags menuClosedFlags = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
    constexpr ImGuiWindowFlags menuOpenFlags = (ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_AlwaysAutoResize);

    const float displayWidth = ImGui::GetIO().DisplaySize.x;
    const float windowWidth = s(190.0f);
    // top-right, below the watermark band (the watermark itself is user-movable via Hud offsets)
    ImGui::SetNextWindowPos(ImVec2(displayWidth - windowWidth - s(12.0f), s(52.0f) + extraYOffset), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s(12), s(8)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, C(12, 12, 13, 248));
    ImGui::PushStyleColor(ImGuiCol_Border, C(30, 30, 33, 210));

    if (ImGui::Begin("Keybind list", nullptr, GUI::isMenuOpen() ? menuOpenFlags : menuClosedFlags)) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        const float winWidth = ImGui::GetWindowWidth();

        textY(d, winPos.x + s(12), winPos.y + s(6), s(16), C(137, 142, 153), "KEYBINDS", kTextCaption, nullptr);
        d->AddLine(ImVec2(winPos.x + s(3), winPos.y + s(26)), ImVec2(winPos.x + winWidth - s(3), winPos.y + s(26)), (g_accent & 0x00FFFFFFu) | (200u << IM_COL32_A_SHIFT), 1.2f);

        float y = winPos.y + s(32.0f);
        const float rowHeight = s(28.0f);
        const auto drawBindRow = [&](const char* label, int key) {
            textY(d, winPos.x + s(12), y, rowHeight, C(207, 209, 218), label, kTextControl, nullptr);

            const bool held = key != Bind::kOff && Bind::isDown(key);
            const float pillWidth = s(76.0f);
            const float pillHeight = s(19.0f);
            const ImVec2 pill(winPos.x + winWidth - s(12) - pillWidth, y + (rowHeight - pillHeight) * 0.5f);
            d->AddRectFilled(pill, pill + ImVec2(pillWidth, pillHeight), held ? g_buttonAccent : C(24, 24, 26), s(5));
            d->AddRect(pill, pill + ImVec2(pillWidth, pillHeight), held ? g_buttonAccent : C(30, 30, 33), s(5));
            const char* keyName = Bind::displayName(key);
            const float keyWidth = ImGui::GetFont()->CalcTextSizeA(kTextSmall, FLT_MAX, 0.0f, keyName).x;
            textY(d, pill.x + ImMax(s(6), (pillWidth - keyWidth) * 0.5f), pill.y, pillHeight, held ? C(18, 18, 20) : C(170, 173, 184), keyName, kTextSmall, nullptr);
            y += rowHeight;
        };

        for (const auto& entry : entries)
            drawBindRow(entry.label, entry.value);

        // feature binds (right-click binds): every registered entry with a key
        for (std::size_t i = 0; i < feature_binds::entryCount; ++i) {
            const auto& entry = feature_binds::entries[i];
            if (entry.key != Bind::kOff && entry.label)
                drawBindRow(entry.label, entry.key);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

// --- player list (FrameworkCS2 port) ---------------------------------------------------
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

    // Pinned to anchor + offsets every frame (the watermark's model): position truth lives in
    // the two sliders/config, never in a drag.
    const float posX = s(10.0f) + static_cast<float>(ui_config::get<PlayerListOffsetX>());
    const float posY = s(64.0f) + static_cast<float>(ui_config::get<PlayerListOffsetY>());
    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Always);

    // Same shell palette as the menu: dark panel, hairline borders, theme accent, muted text.
    // WindowBg/Border/CellPadding shape the window; the table colors mirror the shell's rows.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s(2), s(4)));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(s(9), s(4)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, C(12, 12, 13, 248));
    ImGui::PushStyleColor(ImGuiCol_Border, C(30, 30, 33, 210));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, C(16, 16, 18, 240));
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, C(38, 42, 54));
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, C(26, 26, 29));
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, C(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, C(255, 255, 255, 7));

    if (ImGui::Begin("Player list", nullptr, GUI::isMenuOpen() ? menuOpenFlags : menuClosedFlags)) {
        const auto snap = player_list::snapshot();

        // Muted header text; a theme-accent underline sits under the header row.
        ImGui::PushStyleColor(ImGuiCol_Text, C(137, 142, 153));
        if (ImGui::BeginTable("##player_list_rows", 8,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
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
// The user drops avatar.png (or .jpg) into the Osiris folder; it is decoded once on the present
// thread and handed to the Vulkan hook for upload (avatar_texture in VulkanHook.h). Anything
// missing or undecodable simply keeps the GS monogram fallback.

bool avatarLoadAttempted = false;

void loadAvatar() noexcept
{
    if (!ui_config::withContext([&](auto&& hookContext) {
        const auto* const directory = hookContext.osirisDirectoryPath().get();
        if (!directory)
            return;

        for (const char* name : {"avatar.png", "avatar.jpg"}) {
            char path[512];
            std::snprintf(path, sizeof(path), "%s/%s", directory, name);
            const int fd = LinuxPlatformApi::open(path, O_RDONLY);
            if (fd < 0)
                continue;

            struct stat st {};
            if (LinuxPlatformApi::fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 8 * 1024 * 1024) {
                LinuxPlatformApi::close(fd);
                continue;
            }
            auto* fileData = static_cast<std::uint8_t*>(std::malloc(static_cast<std::size_t>(st.st_size)));
            if (!fileData) {
                LinuxPlatformApi::close(fd);
                continue;
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
                continue;
            }

            int width = 0, height = 0;
            unsigned char* pixels = stbi_load_from_memory(fileData, static_cast<int>(totalRead), &width, &height, nullptr, 4);
            std::free(fileData);
            if (!pixels)
                continue;

            gui_log::write("avatar staged: %s (%dx%d)", name, width, height);
            VulkanHook::avatar_texture::request(pixels, width, height); // takes ownership
            return;
        }
    }))
        gui_log::write("avatar: config context unavailable (monogram stays)");
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

    // Avatar: one attempt per session. Missing file / decode failure keeps the GS monogram.
    if (!avatarLoadAttempted) {
        avatarLoadAttempted = true;
        loadAvatar();
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
