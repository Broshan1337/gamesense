#pragma once

// Neversneeze Lua scripting framework (LuaJIT 2.1, vendored in Source/ThirdParty/luajit).
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

#include <imgui.h>

struct lua_State;

namespace lua
{

inline constexpr int kMaxScripts = 16;
inline constexpr int kMaxHttpSlots = 8;
inline constexpr std::size_t kMaxScriptName = 128; // file name including ".lua"
inline constexpr std::size_t kMaxError = 384;
inline constexpr std::size_t kMaxScriptBytes = 256 * 1024;
inline constexpr std::size_t kMaxHttpBytes = 256 * 1024;
inline constexpr int kMaxGuiItems = 32;            // menu items one script may create via gui.*
inline constexpr std::size_t kMaxGuiLabel = 48;
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

// One menu control a script created with gui.checkbox / gui.slider. Values are mutated by the
// menu (present thread) and read by the script under the framework mutex; bool/int writes are
// the same benign single-word tearing class the menu already accepts on scripts[] elsewhere.
struct GuiItem {
    enum class Type : unsigned char { Checkbox, Slider };
    Type type = Type::Checkbox;
    char label[kMaxGuiLabel] = {};
    bool boolValue = false; // checkbox current value
    int intValue = 0;       // slider current value
    int minValue = 0;
    int maxValue = 100;
};

// Numeric fields passed to a script's game-event callbacks as an `event` table (see
// dispatchEvent). Keys must be static strings - they are only read during the dispatch.
struct EventArg {
    const char* key = nullptr;
    bool isNumber = false; // false = integer
    int intValue = 0;
    float numberValue = 0.0f;
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
    GuiItem guiItems[kMaxGuiItems];
    int guiItemCount = 0;
};

struct FileEntry {
    char name[kMaxScriptName];
    long size;
};

// ---- state (defined in Lua.cpp; guarded by `mutex` unless noted) ----

extern Script scripts[kMaxScripts];
extern HttpSlot httpSlots[kMaxHttpSlots];
extern char scriptsDirPath[512];
// pthread (not std::mutex): the release build links -nostdlib and the std mutex error path
// references std::__throw_system_error, which only libstdc++ provides.
extern pthread_mutex_t mutex;
// Set by dispatchPaint around paint callbacks; renderer.* bindings use it. Read only between
// the set and the reset inside dispatchPaint, so it needs no extra synchronization.
extern ImDrawList* paintDrawList;
// Set by the UI layer (EntryPoints finishInit) - the framework core must not depend on GUI.h
// so it stays unit-testable.
extern bool (*menuOpenQuery)() noexcept;

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
// Raw C_BaseEntity* for an entity index (nullptr = invalid/recycled index).
extern void* (*entityFromIndexQuery)(int entityIndex) noexcept;
// Runtime schema offset of a field by (declaring class, field) name, or -1 when unavailable.
extern int (*schemaFieldOffsetQuery)(const char* className, const char* fieldName) noexcept;
// Enumerates players that currently have a pawn; returns the entry count (0 = unavailable).
extern int (*playerListQuery)(PlayerListEntry* out, int max) noexcept;

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
// optional numeric fields handed to each callback as an `event` table ({ userid = 3, ... });
// strings are not supported yet (the game's GetString vtable slot is not verified in this
// tree - see GameEventFields.h).
void dispatchEvent(const char* eventName, const EventArg* args = nullptr, int argCount = 0) noexcept;
// drawList may be null: renderer.* calls inside the callbacks then error out instead of drawing.
void dispatchPaint(ImDrawList* drawList) noexcept;
void dispatchTick() noexcept;

} // namespace lua
