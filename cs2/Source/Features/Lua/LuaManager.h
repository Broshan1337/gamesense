#pragma once

// Neversnooze Lua scripting framework (LuaJIT 2.1, vendored in Source/ThirdParty/luajit).
//
// Scripts live in <home>/OsirisCS2/scripts/*.lua and are managed from the menu's Scripts tab.
//
// Crash-containment design (read before extending):
//  * One lua_State per script. Unloading is lua_close - nothing else to clean up.
//  * EVERY entry into a script goes through protectedCall() (Lua.cpp): errors are caught,
//    formatted with a Lua traceback into the script's lastError, and the script is
//    auto-disabled (stops receiving dispatches) until reloaded. A Lua-level error can
//    therefore never reach the game.
//  * Runaway scripts: every protected call installs a count debug hook - a script that burns
//    more than kInstructionBudget VM instructions inside one callback is aborted with an error
//    instead of freezing the frame loop. (Requires interpreter-only mode: the count hook never
//    fires in JIT-compiled code - see openSandbox in Lua.cpp.)
//  * Sandbox: os/io/package/require/dofile/loadfile are removed, debug is pruned to
//    debug.traceback, and load/loadstring only accept source chunks (a bytecode chunk is an
//    arbitrary-memory-corruption vector). ffi/jit/bit are exposed ON PURPOSE - FFI is the
//    power surface the user asked for; it can segfault, that is inherent to FFI.
//  * Threading: dispatches arrive from the game thread (game events, createmove) and the present
//    thread (paint + http polling). One mutex serializes every entry into every state, so a
//    state is never touched from two threads at once.
//  * SCRIPT-FACING BINDINGS LIVE IN Lua.cpp, compiled WITH unwind tables: with
//    LUAJIT_UNWIND_EXTERNAL (x64 requirement, see lj_err.c) LuaJIT errors unwind as foreign
//    exceptions THROUGH the lua_CFunction frames - those frames need .eh_frame entries and must
//    not be noexcept. The rest of the tree calls into the framework only from outside the
//    lua_pcall boundary, so the target-wide -fno-unwind-tables flags stay untouched there.
//  * Scripts must never write to /tmp/gamesense_gui.log (anomaly-only contract): client.log goes
//    through VerifyConsole (engine console), not gui_log.

#include <pthread.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>

#include <GameClient/KeyboardState.h>
#include <imgui.h>
#include <UI/ImGui/Neverlose/Neverlose.h> // neverlose::uiScale for the renderer.scale binding
#include <Utils/NsStr.h> // LuaApi.h is included inside namespace lua and cannot include headers itself

struct lua_State;

namespace lua
{

inline constexpr int kMaxScripts = 16;
inline constexpr int kMaxHttpSlots = 8;
inline constexpr std::size_t kMaxScriptName = 128; // file name including ".lua"
inline constexpr std::size_t kMaxError = 384;
inline constexpr std::size_t kMaxScriptBytes = 256 * 1024;
inline constexpr std::size_t kMaxHttpBytes = 256 * 1024;
inline constexpr int kMaxGuiItems = 96;            // menu items one script may create via gui.*
inline constexpr std::size_t kMaxGuiLabel = 48;
inline constexpr int kMaxGuiOptions = 64;          // options per gui.dropdown (48 SDR regions + spare)
inline constexpr int kMaxGuiText = 64;             // characters per gui.text_input value
inline constexpr int kMaxEventString = 64;         // characters per string field in an event table
inline constexpr int kMaxPendingCalls = 32;        // client.delay_call queue per script
inline constexpr int kMaxScriptTextures = 8;       // renderer.load_image slots per script
inline constexpr int kMaxImguiWidgets = 48;        // imgui.* per-frame state slots per script
inline constexpr std::size_t kMaxImguiText = 128;  // input_text buffer size per imgui.* widget
inline constexpr std::size_t kMaxImguiTitle = 64;  // imgui.begin window title length
// VM instructions one callback may burn before being aborted. Generous on purpose: this is not
// a frame-time limiter, it is a "while true do end does not freeze the game" guard.
inline constexpr int kInstructionBudget = 50'000'000;

struct HttpSlot {
    bool active = false;
    bool processDone = false;
    pid_t pid = 0;
    int scriptIndex = -1;
    int callbackRef = -1; // LUA_REGISTRYINDEX ref to the response callback
    char outPath[64] = {};
};

// Pages a gui.* item can live on. -1 (kScriptSubtab) = the script's own sub-tab on the Scripts
// page; otherwise the item renders in a script-owned card at the bottom of the matching menu
// page. The ORDER MUST MATCH the Page enum in Neverlose.cpp (static_assert there) and
// kScriptPageNames below.
enum class ScriptPage : int
{
    Subtab = -1,
    Rage = 0,
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
inline constexpr int kScriptPageCount = 13; // ScriptPage values 0..12 (Subtab excluded)
inline constexpr const char* kScriptPageNames[kScriptPageCount] = {
    "Rage", "Legit", "Movement", "Player Info", "Glow",
    "Viewmodel", "Effects", "Hud", "Sound", "Inventory", "Radio", "Scripts", "Misc"
};

// One menu control a script created with gui.checkbox / gui.slider / gui.dropdown. Values are
// mutated by the menu (present thread) and read by the script under the framework mutex; bool/int
// writes are the same benign single-word tearing class the menu already accepts on scripts[]
// elsewhere. Dropdown options are copied into optionStorage at creation and optionPtrs points at
// them, so the popup layer can keep rendering across frames while the script stays loaded.
struct GuiItem {
    enum class Type : unsigned char { Checkbox, Slider, Dropdown, Divider, Color, Keybind, FloatSlider, Text };
    Type type = Type::Checkbox;
    char label[kMaxGuiLabel] = {};
    bool boolValue = false; // checkbox current value
    int intValue = 0;       // slider current value / dropdown selected index / keybind Bind value
    int minValue = 0;
    int maxValue = 100;
    float floatValue = 0.0f; // float slider current value
    float floatMin = 0.0f;
    float floatMax = 1.0f;
    std::uint32_t colorValue = 0xFFFFFFFF; // color picker current value, 0xRRGGBBAA
    char textValue[kMaxGuiText] = {};      // text input current value
    int page = static_cast<int>(ScriptPage::Subtab); // where this item renders (see ScriptPage)
    // dropdown-only state
    int optionCount = 0;
    char optionStorage[kMaxGuiOptions][kMaxGuiLabel] = {};
    const char* optionPtrs[kMaxGuiOptions] = {};
};

// Persistent state for one imgui.* widget call inside a script's "menu" callback. Widgets are
// matched to slots by call ORDER within the frame (the same contract ImGui itself uses for
// item ids): a script must call its imgui.* widgets in the same order every frame for input
// state (input_text buffers, color_edit values) to stay stable.
struct ImguiWidgetState {
    char text[kMaxImguiText] = {};
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // Persistence key: the widget label currently owning this slot (slots are matched by call
    // order, so a reorder re-keys the slot). kind = 'i' (input_text) or 'K' (color_edit),
    // matching the sidecar line kinds. Empty label = slot never used this load.
    char label[kMaxGuiLabel] = {};
    char kind = 0;
};

// One queued client.delay_call: the callback function + an args table (both registry refs),
// firing when the monotonic clock passes `when`.
struct PendingCall {
    double when = 0.0;
    int functionRef = -1;
    int argsRef = -1; // LUA_REFNIL when the call has no arguments
};

// Numeric AND string fields passed to a script's game-event callbacks as an `event` table
// (see dispatchEvent). Keys must be static strings - they are only read during the dispatch.
// String values are copied into strValue at build time (the game event is recycled after the
// hook returns), capped at kMaxEventString characters.
struct EventArg {
    const char* key = nullptr;
    bool isNumber = false; // false = integer (ignored when isString is true)
    int intValue = 0;
    float numberValue = 0.0f;
    bool isString = false;
    char strValue[kMaxEventString] = {};
};

// One player (controller + pawn pair) enumerated by the entity bridge. Entity indices, not slots.
struct PlayerListEntry {
    int controllerIndex = 0; // entity index of the CCSPlayerController
    int pawnIndex = 0;       // entity index of the C_CSPlayerPawn
};

struct Script {
    char name[kMaxScriptName] = {};
    lua_State* L = nullptr;
    bool errored = false;
    char lastError[kMaxError] = {};
    bool hasPaint = false; // fast gates for the per-frame / per-tick dispatch loops
    bool hasTick = false;
    bool hasMenu = false;  // registered a "menu" callback (imgui.* windows)
    bool hasUnload = false; // registered an "unload" callback (fired from unloadScriptLocked)
    GuiItem guiItems[kMaxGuiItems];
    int guiItemCount = 0;
    // The Scripts-page subtab this script's controls render under. Set once via gui.tab(name);
    // empty = derive the label from the file name.
    char tabLabel[kMaxGuiLabel] = {};
    // imgui.* per-frame widget state (menu dispatch only; reset at each dispatchMenuWindows).
    ImguiWidgetState imguiWidgets[kMaxImguiWidgets];
    int imguiWidgetCount = 0;
    PendingCall pendingCalls[kMaxPendingCalls];
    int pendingCallCount = 0;
    int textureSlots[kMaxScriptTextures] = {}; // renderer.load_image slot ids, 0 = unused
};

struct FileEntry {
    char name[kMaxScriptName];
    long size;
};

// ---- state (defined in Lua.cpp; guarded by `mutex` unless noted) ----

extern Script scripts[kMaxScripts];
extern HttpSlot httpSlots[kMaxHttpSlots];
extern char scriptsDirPath[512];
// Forensics/test surface: number of stack-base repairs since init (see repairLuaStackBase).
extern std::atomic<int> stackBaseRepairCountForTesting;
// Test seam for the repair log: null/false = silent. Production: null = log.
extern bool (*stackBaseRepairLogQuery)() noexcept;
// pthread (not std::mutex): the release build links -nostdlib and the std mutex error path
// references std::__throw_system_error, which only libstdc++ provides.
extern pthread_mutex_t mutex;
// Set by dispatchPaint around paint callbacks; renderer.* bindings use it. Read only between
// the set and the reset inside dispatchPaint, so it needs no extra synchronization.
extern ImDrawList* paintDrawList;
// Overridable http.get spawn (tests: the unit env cannot spawn launch-client). Null = real.
extern pid_t (*spawnHostShellQuery)(const char* script) noexcept;
// Set by the UI layer (EntryPoints finishInit) - the framework core must not depend on GUI.h
// so it stays unit-testable.
extern bool (*menuOpenQuery)() noexcept;
// True while an ImGui context/frame exists (menu layer). Gates dispatchMenuWindows: without it
// (unit tests) "menu" callbacks are never fired, so imgui.* bindings can never touch a missing
// context. Null in tests -> the menu dispatch no-ops.
extern bool (*imguiContextQuery)() noexcept;

// ---- entity/schema bridges (also set by EntryPoints finishInit) ----
//
// The pattern-resolved entity list and the schema system live behind HookContext, which the
// framework core deliberately does not link. The UI/entry layer instead installs these query
// functions (each builds a HookContext per call, exactly like ui_config::withContext) and the
// bindings below only see plain data. All of them must tolerate being called on the game
// thread (createmove/event dispatch) AND the present thread (paint), and must return their
// "unavailable" value while the context is missing or shutting down. In the unit tests they
// are null: the entity.* bindings then return nil, never crash.

// Entity index of the local CCSPlayerController, or 0 when unavailable.
extern int (*localPlayerIndexQuery)() noexcept;
// renderer.load_image bridge into the Vulkan texture pool (VulkanHook::lua_texture). Installed in
// EntryPoints finishInit; null in unit tests - the image bindings then fail softly (nil/false),
// never crash.
extern void (*luaTextureRequest)(int index, const void* pixelsRgba, int width, int height) noexcept;
extern void* (*luaTextureQuery)(int index) noexcept;
extern void (*luaTextureRelease)(int index) noexcept;
// Raw C_BaseEntity* for an entity index (nullptr = invalid/recycled index).
extern void* (*entityFromIndexQuery)(int entityIndex) noexcept;
// Fills `out` with entity indices whose networkable identity class exactly matches `className`
// (the game's own class map lookup, so names are the schema ones - "C_PlantedC4", "C_Inferno",
// "C_WeaponTaser", ...). Returns the count, or -1 when the class name is unknown.
extern int (*entityListQuery)(const char* className, int* outIndices, int max) noexcept;
// World origin of an entity through the game's own GetAbsOrigin (game scene node path - the
// schema's m_vecAbsOrigin lives on CGameSceneNode behind a pointer, out of lua's reach). False
// = unavailable. Installed in EntryPoints finishInit; null in unit tests.
extern bool (*entityOriginQuery)(int entityIndex, float* out) noexcept;
// Runtime schema offset of a field by (declaring class, field) name, or -1 when unavailable.
extern int (*schemaFieldOffsetQuery)(const char* className, const char* fieldName) noexcept;
// Most-derived schema class name of an entity ("C_CSPlayerPawn" etc.) through the game's own
// entity-class map (reverse lookup, no new RE). False = invalid index / context unavailable.
extern bool (*entityClassNameQuery)(int entityIndex, char* outName, int nameCap) noexcept;
// Enumerates players that currently have a pawn; returns the entry count (0 = unavailable).
extern int (*playerListQuery)(PlayerListEntry* out, int max) noexcept;
// Controller entity indices of every player whose pawn is ACTIVELY spectating (observer mode
// != 0) the pawn with entity index `targetPawnIndex`. Returns the count, or 0 when unavailable
// (schema offsets unresolvable / context missing). The m_pObserverServices pointer field cannot
// be followed from Lua (get_prop reads int32s), so the walk lives here.
extern int (*spectatorListQuery)(int targetPawnIndex, int* outControllerIndices, int max) noexcept;
// Runs a console command through the engine's client command buffer (queued, next frame).
// Game thread only - the caller (client.exec binding) enforces the callback context.
extern void (*engineCommandQuery)(const char* command) noexcept;
// API v2 bridges (installed in EntryPoints finishInit, null in unit tests - bindings then
// return nil/false, never crash):
// Reads a runtime int32/float32 cvar by name. False = not found / wrong type / unavailable.
extern bool (*cvarIntQuery)(const char* name, int* out) noexcept;
extern bool (*cvarFloatQuery)(const char* name, float* out) noexcept;
// Writes through a runtime cvar's resolved value pointer (forceFloatConVar/forceBoolConVar).
extern bool (*cvarFloatSetQuery)(const char* name, float value) noexcept;
extern bool (*cvarBoolSetQuery)(const char* name, bool value) noexcept;
// World -> normalized device coordinates through the per-frame worldToProjection matrix.
// False = off screen (w <= 0) / matrix unresolved. Pixels are derived in the binding.
extern bool (*worldToScreenQuery)(float x, float y, float z, float* ndcX, float* ndcY) noexcept;
// Which thread is currently dispatching Lua callbacks: 0 = none, 1 = game thread
// (createmove / game events), 2 = present thread (paint). Set around the dispatch loops.
extern std::atomic<int> dispatchThreadKind;

// ---- native config bridges (installed in EntryPoints finishInit) ----
//
// The config schema is template-driven C++ (ConfigVariableTypes + ConfigSchema walk), so Lua
// cannot name a var directly. These queries walk the same schema the .cfg save/load uses and
// address vars by their dotted path ("Combat.Triggerbot.Enabled"). config.set carries the
// exact semantics of a config-file load (range-clamped, change handlers NOT invoked,
// autosave scheduled) - never partial writes: an unknown path or a kind mismatch fails
// without touching anything. Null in unit tests - the config.* bindings then fail softly.
enum class ConfigValueKind : int { Bool = 'b', Uint = 'u', Float = 'f', Color = 'c' };
struct ConfigValue {
    ConfigValueKind kind = ConfigValueKind::Uint;
    bool boolValue = false;
    unsigned long long uintValue = 0;
    double floatValue = 0.0;
    unsigned char color[4] = {}; // 0-255 RGBA
};
inline constexpr int kMaxConfigPath = 128;
// Number of addressable config entries (placeholder keys included).
extern int (*configEntryCountQuery)() noexcept;
// The index-th entry: "Combat.Triggerbot.Enabled"-style path + its ConfigValueKind as int.
// False = index out of range / context unavailable.
extern bool (*configEntryAtQuery)(int index, char* outPath, int pathCap, int* outKind) noexcept;
// Reads one entry by path. False = unknown path / context unavailable (out untouched).
extern bool (*configGetQuery)(const char* path, ConfigValue* out) noexcept;
// Writes one entry by path (kinds must match, except Bool also accepts Uint 0/1). False =
// unknown path / kind mismatch / context unavailable (nothing written).
extern bool (*configSetQuery)(const char* path, const ConfigValue* value) noexcept;
// Flushes the active config to disk now (the navbar SAVE button pipeline).
extern void (*configSaveQuery)() noexcept;

// ---- lifecycle / file IO (called from the menu thread or the render thread) ----

void init() noexcept;
bool load(const char* name) noexcept;      // false = load failed (slot may still hold an errored script)
void unloadScript(int index) noexcept;
void unloadAll() noexcept;
int listFiles(FileEntry* out, int max) noexcept;
bool readScript(const char* name, char* buffer, std::size_t bufferSize, long* outSize = nullptr) noexcept;
bool writeScript(const char* name, const char* content, std::size_t length) noexcept;
bool createScript(const char* name) noexcept;
bool deleteScript(const char* name) noexcept;
int loadedIndex(const char* name) noexcept;
bool validScriptName(const char* name) noexcept;

// ---- steam id helpers (used by the steam.* bindings) ----

// SteamID64s (7656119...) exceed 2^53, so they cross the Lua boundary as STRINGS - a double
// round-trip silently corrupts the low bits. format/parse are strict decimal (no sign, no
// whitespace, no trailing garbage, no overflow, 0 is not a valid id) and are unit-tested.
void formatSteamId64(std::uint64_t sid, char* out, std::size_t outSize) noexcept;
bool parseSteamId64(const char* text, std::uint64_t* out) noexcept;

// ---- map name (client.get_map_name) ----

// Called from the game-event hook (game thread) when game_newmap fires - copies the map name
// ("de_mirage" style) into the framework's map-name buffer. Thread-safe against the
// client.get_map_name binding (present/game thread) through a dedicated mutex.
void setCurrentMapName(const char* name) noexcept;

// ---- IDA-style runtime pattern helpers (unit-tested directly; used by memory.pattern_scan) ----

struct PatternByte {
    bool wildcard;
    unsigned char value;
};
inline constexpr int kMaxPatternBytes = 64;

// Parses "48 8B 05 ?? ??" (also lowercase hex, "?" wildcards). Returns the byte count, or 0 on
// invalid input.
int parseIdaPattern(const char* pattern, PatternByte* out, int maxBytes) noexcept;

// Scans [data, data+size) for the pattern; returns the match address or nullptr.
const unsigned char* scanMemoryPattern(const unsigned char* data, std::size_t size,
    const PatternByte* bytes, int byteCount) noexcept;

// ---- event dispatch ----

// eventName may be nullptr (event without a name) - the dispatcher no-ops. args/argCount are
// optional int/float/string fields handed to each callback as an `event` table
// ({ userid = 3, weapon = "ak47", ... }); events without a wired shape pass no table.
void dispatchEvent(const char* eventName, const EventArg* args = nullptr, int argCount = 0) noexcept;
// drawList may be null: renderer.* calls inside the callbacks then error out instead of drawing.
void dispatchPaint(ImDrawList* drawList) noexcept;
// Menu-layer dispatch (present thread, menu open): fires every script's "menu" callback, which
// may call imgui.* to build script-owned ImGui windows. Called from the menu shell render AFTER
// the shell + editor windows, so script windows float above the menu. Also enqueues the script
// error toasts (drawn by dispatchPaint).
void dispatchMenuWindows() noexcept;
// Page name -> ScriptPage int, or -2 when the name is unknown ("", "Scripts", "Visuals" style
// names accepted case-insensitively). Unit-tested.
int scriptPageFromName(const char* name) noexcept;
// Per-tick (game thread) dispatch. `userCmd` is the cs2::CUserCmd* the tick's CreateMove produced
// (the cmd.* bindings read/write it; it is only valid for the duration of this call). Pass
// nullptr (or use the default) when no command is available - cmd.* bindings then no-op/nil.
void dispatchTick(void* userCmd = nullptr) noexcept;

} // namespace lua
