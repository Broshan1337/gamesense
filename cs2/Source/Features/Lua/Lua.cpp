// Lua scripting framework implementation - see LuaManager.h for the design contract.
//
// EVERYTHING script-facing lives in THIS translation unit because LuaJIT errors unwind as
// foreign exceptions through the lua_CFunction frames (LUAJIT_UNWIND_EXTERNAL on x64 - the
// x64 interpreter cannot use the internal unwinder). Those frames need .eh_frame entries, so
// this file is compiled with -funwind-tables/-fasynchronous-unwind-tables (see
// Source/CMakeLists.txt) even though the rest of the release target drops unwind tables.
// The target-wide -fno-exceptions flag stays in effect: nothing here throws C++ exceptions -
// luaL_error unwinds as a LuaJIT foreign exception, and our RAII is exception-free.

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <atomic>
#include <cstdarg>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

extern "C" {
#include <lualib.h>
#include <lauxlib.h>
#include <lua.h>
}

// Internal LuaJIT headers (vendored): needed to verify the stack-base dummy frame that the
// C API cannot see. See repairLuaStackBase below.
#include <lj_def.h>
#include <lj_obj.h>

#include <Features/Combat/Autowall/Autowall.h>
#include <Features/Game/NetLag.h>
#include <GameClient/Tracing/Tracing.h>
#include <GameClient/UserCmd.h>
#include <Platform/Linux/LinuxDynamicLibrary.h>
#include <UI/ImGui/GuiLog.h>
#include <Utils/CrashLogger.h>
#include <Utils/NsPaths.h>
#include <Utils/NsStr.h>
#include <Utils/VerifyConsole.h>

#include <ThirdParty/stb/stb_image.h>

#include "LuaManager.h"

namespace lua
{

Script scripts[kMaxScripts];
HttpSlot httpSlots[kMaxHttpSlots];
char scriptsDirPath[512] = {};
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
ImDrawList* paintDrawList = nullptr;
char httpBuffer[kMaxHttpBytes + 1];
bool (*menuOpenQuery)() noexcept = nullptr;
bool (*imguiContextQuery)() noexcept = nullptr;
int (*localPlayerIndexQuery)() noexcept = nullptr;
void* (*entityFromIndexQuery)(int) noexcept = nullptr;
int (*schemaFieldOffsetQuery)(const char*, const char*) noexcept = nullptr;
bool (*entityClassNameQuery)(int, char*, int) noexcept = nullptr;
int (*playerListQuery)(PlayerListEntry*, int) noexcept = nullptr;
int (*spectatorListQuery)(int, int*, int) noexcept = nullptr;
int (*entityListQuery)(const char*, int*, int) noexcept = nullptr;
bool (*entityOriginQuery)(int, float*) noexcept = nullptr;
void (*engineCommandQuery)(const char*) noexcept = nullptr;
bool (*cvarIntQuery)(const char*, int*) noexcept = nullptr;
bool (*cvarFloatQuery)(const char*, float*) noexcept = nullptr;
bool (*cvarFloatSetQuery)(const char*, float) noexcept = nullptr;
bool (*cvarBoolSetQuery)(const char*, bool) noexcept = nullptr;
int (*configEntryCountQuery)() noexcept = nullptr;
bool (*configEntryAtQuery)(int, char*, int, int*) noexcept = nullptr;
bool (*configGetQuery)(const char*, ConfigValue*) noexcept = nullptr;
bool (*configSetQuery)(const char*, const ConfigValue*) noexcept = nullptr;
void (*configSaveQuery)() noexcept = nullptr;
bool (*worldToScreenQuery)(float, float, float, float*, float*) noexcept = nullptr;
void (*luaTextureRequest)(int, const void*, int, int) noexcept = nullptr;
void* (*luaTextureQuery)(int) noexcept = nullptr;
void (*luaTextureRelease)(int) noexcept = nullptr;
std::atomic<int> dispatchThreadKind{0};
// The cs2::CUserCmd* of the tick currently being dispatched (game thread, set by dispatchTick's
// caller). Valid only for the duration of one dispatchTick call - the cmd.* bindings dereference
// it under the framework mutex exactly then. Raw void* on purpose: Lua.cpp is the one TU that may
// know the game command layout (through GameClient/UserCmd.h), LuaManager.h stays game-agnostic.
static void* tickUserCmd = nullptr;

// posix_spawn environment (unistd.h only declares it under feature macros - mirror RadioManager)
extern "C" char** environ;

// Defined in LuaApi.h (included below, inside namespace lua) - forward-declared for the toast
// queue and the imgui.* window dispatch.
double luaNow() noexcept;

// Toast queue (client.notify). Written under the framework mutex from any dispatch, drawn at the
// end of dispatchPaint on the present thread.
inline constexpr int kMaxToasts = 6;
inline constexpr std::size_t kMaxToastText = 160;
inline constexpr double kToastLifeSeconds = 4.0;
struct Toast {
    bool active = false;
    double when = 0.0;
    std::uint32_t color = 0;
    char text[kMaxToastText] = {};
};
static Toast toasts[kMaxToasts];
static int toastCursor = 0;

// Queues one toast (safe from any dispatch under the framework mutex). Oldest entry is reused
// when the queue wraps - the same policy drawToasts already applies for expiry.
static void queueToast(const char* text, std::uint32_t colorRgba) noexcept
{
    if (!text || !text[0])
        return;
    Toast& toast = toasts[toastCursor];
    toastCursor = (toastCursor + 1) % kMaxToasts;
    toast.active = true;
    toast.when = luaNow();
    toast.color = colorRgba;
    std::size_t i = 0;
    for (; text[i] != '\0' && i < kMaxToastText - 1; ++i)
        toast.text[i] = text[i] == '\n' ? ' ' : text[i];
    toast.text[i] = '\0';
}

// Red "script errored" toast - the user-facing surface for a crashed callback. The full
// traceback stays in Script::lastError (Scripts page); the toast just makes the failure
// visible wherever the user is.
static void queueScriptErrorToast(const Script& script, const char* message) noexcept
{
    char toastText[kMaxToastText];
    char shortName[40];
    const char* dot = std::strchr(script.name, '.');
    const std::size_t stemLength = dot ? static_cast<std::size_t>(dot - script.name) : std::strlen(script.name);
    const std::size_t nameLength = stemLength < sizeof(shortName) - 1 ? stemLength : sizeof(shortName) - 1;
    std::memcpy(shortName, script.name, nameLength);
    shortName[nameLength] = '\0';
    std::snprintf(toastText, sizeof(toastText), "script error [%s]: %s", shortName, message ? message : "unknown error");
    queueToast(toastText, IM_COL32(232, 96, 96, 255));
}

// The menu page gui.* items are being created on during the current script load
// (gui.page(); -1 = the script's own Scripts-page sub-tab). Consumed by every gui item
// creation and reset in load().
static int pendingItemPage = static_cast<int>(ScriptPage::Subtab);
// True only while dispatchMenuWindows runs "menu" callbacks (present thread, ImGui context
// alive): the imgui.* bindings hard-error outside it, so they can never touch ImGui from a
// game-thread dispatch or a unit test (the test binary has no ImGui context at all).
static bool imguiMenuActive = false;
// Begin/End stack safety net: a Lua error mid-window would otherwise leave ImGui's window
// stack unbalanced (EndFrame asserts on that). dispatchMenuWindows force-closes the leftovers
// after every script's callbacks.
static int imguiWindowDepth = 0;
// renderer.push_clip/pop_clip balance counter (paint callbacks only; dispatchPaint resets it
// every frame so an errored callback cannot leave a dangling clip rect in the draw list).
static int paintClipDepth = 0;

// posix_spawn helper used by http.get: the game runs inside the Steam Linux Runtime container,
// /tmp is shared with the host, and the host's curl resolves DNS + TLS (same launch path the
// web radio uses). Overridable for unit tests (the test env cannot spawn launch-client).
pid_t (*spawnHostShellQuery)(const char* script) noexcept = nullptr;

[[nodiscard]] static pid_t spawnHostShell(const char* script) noexcept
{
    if (spawnHostShellQuery)
        return spawnHostShellQuery(script);
    constexpr const char* kLaunchClientPath = "/usr/bin/steam-runtime-launch-client";
    char* const argv[] = {
        const_cast<char*>("steam-runtime-launch-client"),
        const_cast<char*>("--host"),
        const_cast<char*>("--"),
        const_cast<char*>("sh"),
        const_cast<char*>("-c"),
        const_cast<char*>(script),
        nullptr,
    };
    pid_t pid{};
    if (::posix_spawn(&pid, kLaunchClientPath, nullptr, nullptr, argv, environ) == 0)
        return pid;
    return 0;
}

static void pollHttp() noexcept;
static void unloadScriptLocked(int index) noexcept; // mutex must already be held

// ---- gui.* state persistence (sidecar files next to the scripts) ----
//
// Values a script's menu items hold are persisted per script in <scriptsDir>/<name>.gui (a
// tiny line format, NOT the order-sensitive config schema - same sidecar decision the radio
// favorites and feature binds made). Loaded once per script load into `pendingGuiDefaults`;
// each gui.checkbox/gui.slider creation then applies its saved value by label. Declared here,
// ABOVE the LuaApi.h include, because the gui bindings consume these helpers.

struct PendingGuiValue {
    char label[kMaxGuiLabel] = {};
    // Item kind this default belongs to: 'c' checkbox, 's' slider, 'd' dropdown, 'k' color,
    // 'b' keybind, 'f' float slider, 't' text input, 'i' imgui input_text, 'K' imgui color_edit.
    // Lookups match label AND kind, so a gui.* item and an imgui widget sharing a label do
    // not steal each other's saved values.
    char kind = 0;
    bool boolValue = false;
    int intValue = 0;
    int minValue = 0;
    int maxValue = 100;
    float floatValue = 0.0f;
    float floatMin = 0.0f;
    float floatMax = 1.0f;
    std::uint32_t colorValue = 0xFFFFFFFF;
    char text[kMaxImguiText] = {};
};
static PendingGuiValue pendingGuiDefaults[kMaxGuiItems];
static int pendingGuiDefaultCount = 0;
static char pendingGuiOwner[kMaxScriptName] = {};

static void guiStatePath(char* out, std::size_t outSize, const char* name) noexcept
{
    const std::size_t dirLength = std::strlen(scriptsDirPath);
    const std::size_t nameLength = std::strlen(name);
    if (dirLength + 1 + nameLength + sizeof(".gui") > outSize) {
        out[0] = '\0';
        return;
    }
    std::memcpy(out, scriptsDirPath, dirLength);
    out[dirLength] = '/';
    std::memcpy(out + dirLength + 1, name, nameLength + 1);
    std::memcpy(out + dirLength + 1 + nameLength, ".gui", sizeof(".gui"));
}

static void loadGuiDefaults(const char* name) noexcept
{
    pendingGuiDefaultCount = 0;
    std::strncpy(pendingGuiOwner, name, kMaxScriptName - 1);
    pendingGuiOwner[kMaxScriptName - 1] = '\0';

    char path[648];
    guiStatePath(path, sizeof(path), name);
    if (!path[0])
        return;
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0)
        return; // no saved state yet - items keep their script-provided defaults

    char buffer[4096];
    long total = 0;
    for (;;) {
        const ssize_t bytes = ::read(fd, buffer + total, static_cast<std::size_t>(sizeof(buffer) - 1 - total));
        if (bytes <= 0)
            break;
        total += bytes;
        if (total >= static_cast<long>(sizeof(buffer) - 1))
            break;
    }
    ::close(fd);
    buffer[total] = '\0';

    // Lines: "c\t<label>\t<0|1>" (checkbox) / "s\t<label>\t<value>\t<min>\t<max>" (slider) /
    // "d\t<label>\t<index>" (dropdown - options come from the script itself) /
    // "k\t<label>\t<r>,<g>,<b>,<a>" (color picker, 0-255 channels) /
    // "b\t<label>\t<int>" (keybind Bind value) /
    // "f\t<label>\t<value>\t<min>\t<max>" (float slider) /
    // "t\t<label>\t<text>" (text input) /
    // "i\t<label>\t<text>" (imgui input_text, keyed by widget label) /
    // "K\t<label>\t<r>,<g>,<b>,<a>" (imgui color_edit).
    // Unknown line kinds are ignored so older builds skip newer state and vice versa.
    for (char* line = buffer; line && pendingGuiDefaultCount < kMaxGuiItems;) {
        char* next = std::strchr(line, '\n');
        if (next)
            *next++ = '\0';
        const char lineKind = (line[0] != '\0' && line[1] == '\t') ? line[0] : 0;
        if (lineKind == 'c' || lineKind == 's' || lineKind == 'd' || lineKind == 'k'
            || lineKind == 'b' || lineKind == 'f' || lineKind == 't' || lineKind == 'i' || lineKind == 'K') {
            char* label = line + 2;
            char* rest = std::strchr(label, '\t');
            if (rest) {
                *rest++ = '\0';
                const std::size_t labelLength = std::strlen(label);
                if (labelLength > 0 && labelLength < kMaxGuiLabel) {
                    PendingGuiValue parsed{};
                    parsed.kind = lineKind;
                    std::strncpy(parsed.label, label, kMaxGuiLabel - 1);
                    if (lineKind == 'c') {
                        parsed.boolValue = rest[0] == '1';
                        pendingGuiDefaults[pendingGuiDefaultCount++] = parsed;
                    } else if (lineKind == 'd' || lineKind == 'b') {
                        parsed.intValue = std::atoi(rest);
                        pendingGuiDefaults[pendingGuiDefaultCount++] = parsed;
                    } else if (lineKind == 'k' || lineKind == 'K') {
                        unsigned r = 255, g = 255, b = 255, a = 255;
                        if (std::sscanf(rest, "%u,%u,%u,%u", &r, &g, &b, &a) == 4 && r <= 255 && g <= 255 && b <= 255 && a <= 255) {
                            parsed.colorValue = (r << 24) | (g << 16) | (b << 8) | a;
                            pendingGuiDefaults[pendingGuiDefaultCount++] = parsed;
                        }
                    } else if (lineKind == 's') {
                        // sliders: value, min, max (ints)
                        char* valueEnd = std::strchr(rest, '\t');
                        if (valueEnd) {
                            *valueEnd++ = '\0';
                            char* minEnd = std::strchr(valueEnd, '\t');
                            if (minEnd) {
                                *minEnd++ = '\0';
                                parsed.intValue = std::atoi(rest);
                                parsed.minValue = std::atoi(valueEnd);
                                parsed.maxValue = std::atoi(minEnd);
                                pendingGuiDefaults[pendingGuiDefaultCount++] = parsed;
                            }
                        }
                    } else if (lineKind == 'f') {
                        // float sliders: value, min, max
                        char* valueEnd = std::strchr(rest, '\t');
                        if (valueEnd) {
                            *valueEnd++ = '\0';
                            char* minEnd = std::strchr(valueEnd, '\t');
                            if (minEnd) {
                                *minEnd++ = '\0';
                                parsed.floatValue = static_cast<float>(std::atof(rest));
                                parsed.floatMin = static_cast<float>(std::atof(valueEnd));
                                parsed.floatMax = static_cast<float>(std::atof(minEnd));
                                pendingGuiDefaults[pendingGuiDefaultCount++] = parsed;
                            }
                        }
                    } else if (lineKind == 't' || lineKind == 'i') {
                        std::strncpy(parsed.text, rest, kMaxImguiText - 1);
                        pendingGuiDefaults[pendingGuiDefaultCount++] = parsed;
                    }
                }
            }
        }
        line = next;
    }
}

static void saveGuiState(const Script& script) noexcept
{
    char path[648];
    guiStatePath(path, sizeof(path), script.name);
    if (!path[0])
        return;
    const int fd = ::open(path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0)
        return;
    char line[256];
    auto writeLine = [fd](const char* text, int length) {
        if (length <= 0)
            return;
        std::size_t written = 0;
        while (written < static_cast<std::size_t>(length)) {
            const ssize_t bytes = ::write(fd, text + written, static_cast<std::size_t>(length) - written);
            if (bytes <= 0)
                break;
            written += static_cast<std::size_t>(bytes);
        }
    };
    for (int i = 0; i < script.guiItemCount; ++i) {
        const GuiItem& item = script.guiItems[i];
        int length = 0;
        if (item.type == GuiItem::Type::Checkbox)
            length = std::snprintf(line, sizeof(line), "c\t%s\t%d\n", item.label, item.boolValue ? 1 : 0);
        else if (item.type == GuiItem::Type::Dropdown)
            length = std::snprintf(line, sizeof(line), "d\t%s\t%d\n", item.label, item.intValue);
        else if (item.type == GuiItem::Type::Color)
            length = std::snprintf(line, sizeof(line), "k\t%s\t%u,%u,%u,%u\n", item.label,
                (item.colorValue >> 24) & 255, (item.colorValue >> 16) & 255, (item.colorValue >> 8) & 255, item.colorValue & 255);
        else if (item.type == GuiItem::Type::Keybind)
            length = std::snprintf(line, sizeof(line), "b\t%s\t%d\n", item.label, item.intValue);
        else if (item.type == GuiItem::Type::FloatSlider)
            length = std::snprintf(line, sizeof(line), "f\t%s\t%.6g\t%.6g\t%.6g\n", item.label,
                static_cast<double>(item.floatValue), static_cast<double>(item.floatMin), static_cast<double>(item.floatMax));
        else if (item.type == GuiItem::Type::Text || item.type == GuiItem::Type::Divider)
            length = item.type == GuiItem::Type::Text
                ? std::snprintf(line, sizeof(line), "t\t%s\t%s\n", item.label, item.textValue)
                : 0; // dividers carry no value
        else
            length = std::snprintf(line, sizeof(line), "s\t%s\t%d\t%d\t%d\n", item.label, item.intValue, item.minValue, item.maxValue);
        writeLine(line, length);
    }
    // imgui.* per-frame widget state (input_text/color_edit keyed by widget label) persists in
    // the same sidecar so script windows survive reloads like gui.* items do.
    for (int i = 0; i < kMaxImguiWidgets; ++i) {
        const ImguiWidgetState& widget = script.imguiWidgets[i];
        if (widget.label[0] == '\0' || (widget.kind != 'i' && widget.kind != 'K'))
            continue;
        int length = 0;
        if (widget.kind == 'i') {
            char clean[kMaxImguiText];
            std::size_t out = 0;
            for (const char* p = widget.text; *p != '\0' && out < sizeof(clean) - 1; ++p)
                clean[out++] = (*p == '\t' || *p == '\n' || *p == '\r') ? ' ' : *p;
            clean[out] = '\0';
            length = std::snprintf(line, sizeof(line), "i\t%s\t%s\n", widget.label, clean);
        } else {
            const auto channel = [&widget](int k) {
                int v = static_cast<int>(widget.color[k] * 255.0f + 0.5f);
                return v < 0 ? 0 : (v > 255 ? 255 : v);
            };
            length = std::snprintf(line, sizeof(line), "K\t%s\t%d,%d,%d,%d\n", widget.label,
                channel(0), channel(1), channel(2), channel(3));
        }
        writeLine(line, length);
    }
    ::close(fd);
}

// Returns the saved value for `label`+`kind` from the currently-loading script's sidecar, if
// any. Kind-qualified so a gui.* item and an imgui widget sharing a label stay independent.
static const PendingGuiValue* findPendingGuiDefault(const char* scriptName, const char* label, char kind) noexcept
{
    if (std::strcmp(pendingGuiOwner, scriptName) != 0)
        return nullptr;
    for (int i = 0; i < pendingGuiDefaultCount; ++i) {
        if (pendingGuiDefaults[i].kind == kind && std::strcmp(pendingGuiDefaults[i].label, label) == 0)
            return &pendingGuiDefaults[i];
    }
    return nullptr;
}

// ---- steamid64 helpers (steam.* bindings; see LuaManager.h) ----

void formatSteamId64(std::uint64_t sid, char* out, std::size_t outSize) noexcept
{
    std::snprintf(out, outSize, "%llu", static_cast<unsigned long long>(sid));
}

bool parseSteamId64(const char* text, std::uint64_t* out) noexcept
{
    if (!text || !out || text[0] < '1' || text[0] > '9')
        return false; // 0, signs, whitespace and empty strings are not valid ids
    std::uint64_t value = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9')
            return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(*p - '0');
        if (value > (0xFFFFFFFFFFFFFFFFULL - digit) / 10ULL)
            return false; // overflow
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

// Releases ONE http slot (failed setup path) - unlike killPendingHttpFor, the script's other
// in-flight requests stay untouched. Must be called with the slot's script mutex held.
static void discardHttpSlot(lua_State* L, HttpSlot* slot) noexcept
{
    if (slot->pid > 0) {
        ::kill(slot->pid, SIGKILL);
        ::waitpid(slot->pid, nullptr, 0);
    }
    char configPath[ns_paths::kMaxPath];
    char bodyPath[ns_paths::kMaxPath];
    std::snprintf(configPath, sizeof(configPath), "%s/ns_lua_http_%d.cfg", ns_paths::root(), static_cast<int>(slot - httpSlots));
    std::snprintf(bodyPath, sizeof(bodyPath), "%s/ns_lua_http_%d.body", ns_paths::root(), static_cast<int>(slot - httpSlots));
    ::unlink(configPath);
    ::unlink(bodyPath);
    ::unlink(slot->outPath);
    if (slot->callbackRef != -1 && L)
        luaL_unref(L, LUA_REGISTRYINDEX, slot->callbackRef);
    *slot = HttpSlot{};
}

// ---- map name (client.get_map_name; see LuaManager.h setCurrentMapName) ----
//
// Captured once per game_newmap on the game thread (EntryPoints), read from any dispatch
// thread through the binding. Own dedicated mutex: the game thread writes it OUTSIDE the
// framework mutex (immediately before dispatchEvent locks it), so reusing `mutex` would
// create a lock/unlock/relock window for no benefit.
inline constexpr std::size_t kMaxMapName = 64;
static char mapNameBuffer[kMaxMapName] = {};
static pthread_mutex_t mapNameMutex = PTHREAD_MUTEX_INITIALIZER;

void setCurrentMapName(const char* name) noexcept
{
    if (!name || name[0] == '\0')
        return;
    pthread_mutex_lock(&mapNameMutex);
    std::size_t i = 0;
    for (; name[i] != '\0' && i < kMaxMapName - 1; ++i)
        mapNameBuffer[i] = name[i];
    mapNameBuffer[i] = '\0';
    pthread_mutex_unlock(&mapNameMutex);
}

// Copies the current map name for the binding (defined below in LuaApi.h, which is included
// after this point). False = no map captured yet (never in a game since injection).
static bool copyMapName(char* out, std::size_t cap) noexcept
{
    pthread_mutex_lock(&mapNameMutex);
    const bool ok = mapNameBuffer[0] != '\0';
    if (ok)
        std::snprintf(out, cap, "%s", mapNameBuffer);
    pthread_mutex_unlock(&mapNameMutex);
    return ok;
}

// ---- script-facing bindings (LuaApi.h is included INSIDE namespace lua on purpose) ----
#include "LuaApi.h"

// ---- core helpers ----

static void copyError(Script& script, const char* message) noexcept
{
    if (!message)
        message = "(unknown lua error)";
    std::size_t i = 0;
    for (; message[i] != '\0' && i < kMaxError - 1; ++i)
        script.lastError[i] = message[i] == '\n' ? ' ' : message[i];
    script.lastError[i] = '\0';
}

// Foreign exceptions unwind through this frame - keep the FDE, never make it noexcept.
static void budgetHook(lua_State* L, lua_Debug*)
{
    luaL_error(L, "instruction budget exceeded");
}

// Error handler sitting under the called function: turns the error object into a traceback.
static int errorHandler(lua_State* L)
{
    const char* message = lua_tostring(L, 1);
    luaL_traceback(L, L, message ? message : "(non-string error object)", 1);
    return 1;
}

// --- stack-base dummy frame guard ---
//
// stack_init() lays the very bottom of every Lua stack out as: stack[0] = the thread TValue
// ("dummy frame function", read by curr_func() whenever no Lua frame is active - which is
// exactly how our dispatch loops run), stack[1] = nil (the FR2 frame-link slot). LuaJIT never
// writes either again. A C binding that pops BELOW the stack base (unbalanced
// push/pop in some l_* helper) makes later pushes land in these slots; the next dispatch
// then decodes the "current function" from the garbage (observed 4x on 2026-09-10: int64 -3
// in the slot -> non-canonical pointer -> #GP inside lua_pushcclosure, si_code 0x80).
//
// Verify + restore at every dispatch entry, so whatever binding does this degrades to a
// one-line anomaly in the gui log instead of taking the game down. The breadcrumb (0x360
// corrupted / 0x361 repaired) attributes the frame to the crash dump if it still fires.
// Forensics counter: incremented on every stack-base repair. Read by the unit-test
// reproduction of the multi-death stack corruption and by triage.
std::atomic<int> stackBaseRepairCountForTesting{0};
// Test seam: return false to silence the repair log (the UnitTests environment cannot
// exercise gui_log's LinuxPlatformApi mocks from this path). Production: null = always log.
bool (*stackBaseRepairLogQuery)() noexcept = nullptr;

static void repairLuaStackBase(lua_State* L) noexcept
{
    static int logBudget = 16; // a per-tick underflow would otherwise spam the anomaly log
    const TValue* stack = mref(L->stack, TValue);
    if (!stack)
        return;
    const uint64_t expectedThread = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(L)) | (static_cast<uint64_t>(LJ_TTHREAD) << 47);
    if (stack[0].gcr.gcptr64 != expectedThread || stack[1].it64 != -1) {
        CrashLogger::trace(0x360);
        stackBaseRepairCountForTesting.fetch_add(1, std::memory_order_relaxed);
        if (logBudget > 0 && (!stackBaseRepairLogQuery || stackBaseRepairLogQuery())) {
            --logBudget;
            // BOTH slots + geometry: the logged "thread slot" value keeps coming out VALID
            // (3x now), so the failing check is actually stack[1] - the writer lands ONE value
            // below the frame. Logging what that value IS (string TValue / int / raw garbage)
            // plus the base/top offsets identifies the writer on the next occurrence.
            gui_log::write("[lua] stack-base dummy frame corrupted, repaired (slot0=0x%llx slot1=0x%llx base=+%d top=+%d maxstack=%p)",
                static_cast<unsigned long long>(stack[0].gcr.gcptr64),
                static_cast<unsigned long long>(stack[1].it64),
                static_cast<int>(reinterpret_cast<const char*>(L->base) - reinterpret_cast<const char*>(stack)),
                static_cast<int>(reinterpret_cast<const char*>(L->top) - reinterpret_cast<const char*>(stack)),
                static_cast<const void*>(mref(L->maxstack, void)));
        }
        setgcVraw(const_cast<TValue*>(&stack[0]), obj2gco(L), LJ_TTHREAD);
        setnilV(const_cast<TValue*>(&stack[1]));
        CrashLogger::trace(0x361);
    }
}

// Calls the function (plus nargs arguments) currently on top of the stack of `script`.
// On error: records the traceback in the script and marks it errored (auto-disabled).
// The stack-balance guard lives HERE (not only in runCallbacks) so EVERY dispatch path is
// covered: pollHttp's http-callback invocation, drainPendingCalls' timer callbacks and any
// future caller. Live-verified 2026-09-11: a paint-thread callback (dead_comedian http body)
// pushed string TValues BELOW the stack base repeatedly (17 repairs, dummy frame corrupted)
// and then wedged the VM in an FF-fallback C cycle (hookcount consumed 6 of 50M - C cycles are
// invisible to the count hook), freezing the game. Balance snapping here bounds the damage and
// attributes the offender by script name.
static bool protectedCall(Script& script, int nargs) noexcept
{
    lua_State* L = script.L;
    const int before = lua_gettop(L);
    lua_pushcfunction(L, errorHandler);
    lua_insert(L, -(nargs + 2)); // handler below function + args
    lua_sethook(L, budgetHook, LUA_MASKCOUNT, kInstructionBudget);
    const int status = lua_pcall(L, nargs, 0, -(nargs + 2));
    lua_sethook(L, nullptr, 0, 0);
    const int after = lua_gettop(L); // post-pcall: handler on top (+ error object on failure)
    const int expected = before - nargs + (status != 0 ? 1 : 0);
    if (after != expected) {
        lua_settop(L, expected);
        static int logBudget = 16; // per-tick offenders would otherwise spam the anomaly log
        if (logBudget > 0) {
            --logBudget;
            gui_log::write("[lua] unbalanced protectedCall in %s (%s): stack level moved %d -> %d, reset",
                script.name, script.tabLabel[0] ? script.tabLabel : "-", before, after);
        }
    }
    if (status == 0) {
        lua_pop(L, 1); // the error handler
        return true;
    }
    const char* errorObject = lua_tostring(L, -1);
    copyError(script, errorObject);
    script.errored = true;
    queueScriptErrorToast(script, script.lastError); // visible failure, wherever the user is
    lua_pop(L, 2); // error object + error handler
    return false;
}

// ---- sandbox ----

static void preloadGlobal(lua_State* L, const char* name) noexcept
{
    lua_getglobal(L, "require");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_pushstring(L, name);
    if (lua_pcall(L, 1, 1, 0) == 0)
        lua_setglobal(L, name); // pops the module, sets the global
    else
        lua_pop(L, 1); // error object
}

// load/loadstring replacement: source chunks only (first byte ESC = precompiled bytecode).
static int safeLoad(lua_State* L)
{
    if (lua_type(L, 1) == LUA_TSTRING) {
        std::size_t length = 0;
        const char* chunk = lua_tolstring(L, 1, &length);
        if (length > 0 && chunk[0] == 27)
            return luaL_error(L, "bytecode chunks are not allowed");
    } else if (lua_type(L, 1) != LUA_TFUNCTION) {
        return luaL_error(L, "bad argument #1 to 'load' (string or function expected)");
    }
    lua_pushliteral(L, "__ns_original_load");
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, 1);
    return 1;
}

static void openSandbox(lua_State* L) noexcept
{
    luaL_openlibs(L);

    // Interpreter-only mode, for two real reasons measured this session:
    //  1. The instruction-budget hook NEVER fires in JIT-compiled code (count hooks are
    //     interpreter-only, and LuaJIT happily compiles `while true do end` into an infinite
    //     machine-code loop) - with the JIT on, a runaway script freezes the game.
    //  2. The JIT's mcode allocator mmaps RWX pages - an unnecessary executable-memory artifact
    //     inside a VAC-monitored process. Interpreter-only states create none.
    // Script hot paths run at plain-Lua speed, which is plenty for paint/HUD work.
    lua_getglobal(L, "jit");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "off");
        if (lua_isfunction(L, -1))
            lua_call(L, 0, 0); // jit.off() - this state compiles nothing
    }
    lua_pop(L, 1);

    // Materialize the power-user libraries as plain globals, then strip require/package -
    // scripts use `ffi`, `bit`, `jit` directly and have no business pulling in more modules.
    preloadGlobal(L, "ffi");
    preloadGlobal(L, "bit");
    preloadGlobal(L, "jit");

    lua_pushliteral(L, "__ns_original_load");
    lua_getglobal(L, "load");
    lua_rawset(L, LUA_REGISTRYINDEX);
    lua_pushcfunction(L, safeLoad);
    lua_setglobal(L, "load");
    lua_pushcfunction(L, safeLoad);
    lua_setglobal(L, "loadstring");

    lua_pushnil(L); lua_setglobal(L, "os");
    lua_pushnil(L); lua_setglobal(L, "io");
    lua_pushnil(L); lua_setglobal(L, "package");
    lua_pushnil(L); lua_setglobal(L, "require");
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");

    // debug: keep only debug.traceback (used for error reporting); the rest is an
    // introspection/upvalue-tampering surface with zero legitimate script use.
    lua_getglobal(L, "debug");
    if (lua_istable(L, -1)) {
        lua_newtable(L);
        lua_getfield(L, -2, "traceback");
        lua_setfield(L, -2, "traceback");
        lua_setglobal(L, "debug");
    }
    lua_pop(L, 1);
}

// ---- name validation / path helpers ----

bool validScriptName(const char* name) noexcept
{
    if (!name || name[0] == '\0')
        return false;
    std::size_t i = 0;
    for (; name[i] != '\0'; ++i) {
        const char c = name[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '_' || c == '-' || c == ' ' || c == '.' || c == '(' || c == ')';
        if (!ok)
            return false;
        if (c == '.' && name[i + 1] == '.')
            return false;
    }
    // must end in ".lua"
    return i > 4 && i <= kMaxScriptName - 1 && name[i - 4] == '.' && name[i - 3] == 'l' && name[i - 2] == 'u' && name[i - 1] == 'a';
}

static void buildScriptPath(char* out, std::size_t outSize, const char* name) noexcept
{
    // Manual assembly (not snprintf): callers pass raw buffers of assorted sizes, and
    // -Wformat-truncation cannot be convinced otherwise.
    const std::size_t dirLength = std::strlen(scriptsDirPath);
    const std::size_t nameLength = std::strlen(name);
    if (dirLength + 1 + nameLength + 1 > outSize) {
        out[0] = '\0';
        return;
    }
    std::memcpy(out, scriptsDirPath, dirLength);
    out[dirLength] = '/';
    std::memcpy(out + dirLength + 1, name, nameLength + 1);
}

int loadedIndex(const char* name) noexcept
{
    for (int i = 0; i < kMaxScripts; ++i) {
        if (scripts[i].L && std::strcmp(scripts[i].name, name) == 0)
            return i;
    }
    return -1;
}

// ---- http plumbing (used by the bindings and the paint dispatcher) ----

static void killPendingHttpFor(int scriptIndex) noexcept
{
    for (auto& slot : httpSlots) {
        if (!slot.active || slot.scriptIndex != scriptIndex)
            continue;
        if (slot.pid > 0) {
            ::kill(slot.pid, SIGKILL);
            ::waitpid(slot.pid, nullptr, 0);
        }
        ::unlink(slot.outPath);
        if (slot.callbackRef != -1 && scripts[scriptIndex].L)
            luaL_unref(scripts[scriptIndex].L, LUA_REGISTRYINDEX, slot.callbackRef);
        slot = HttpSlot{};
    }
}

// Present thread, called from dispatchPaint with the mutex held.
static void pollHttp() noexcept
{
    for (int i = 0; i < kMaxHttpSlots; ++i) {
        HttpSlot& slot = httpSlots[i];
        if (!slot.active)
            continue;

        if (slot.pid > 0) {
            int status;
            if (::waitpid(slot.pid, &status, WNOHANG) == slot.pid) {
                slot.pid = 0;
                slot.processDone = true; // the shell (curl && mv) finished; the file is final
            } else {
                continue;
            }
        }
        if (!slot.processDone)
            continue;

        // The owning script must still be alive and healthy to receive the response.
        Script& script = scripts[slot.scriptIndex];
        const bool deliverable = script.L && !script.errored;

        long bodySize = 0;
        bool hasBody = false;
        if (deliverable) {
            const int fd = ::open(slot.outPath, O_RDONLY);
            if (fd >= 0) {
                for (;;) {
                    const ssize_t bytes = ::read(fd, httpBuffer + bodySize, kMaxHttpBytes - bodySize);
                    if (bytes <= 0)
                        break;
                    bodySize += bytes;
                    if (bodySize >= static_cast<long>(kMaxHttpBytes))
                        break;
                }
                ::close(fd);
                httpBuffer[bodySize] = '\0';
                hasBody = true;
            }
        }
        ::unlink(slot.outPath);

        if (deliverable) {
            lua_State* L = script.L;
            lua_rawgeti(L, LUA_REGISTRYINDEX, slot.callbackRef);
            if (lua_isfunction(L, -1)) {
                // protectedCall consumes the callback + the body (pcall pops func+args,
                // the error handler is popped inside) - NOTHING extra to pop after it.
                // The old unconditional lua_pop(L, 1) here over-popped ONE value on this
                // branch (only the dead-ref branch needs a pop) - that one-slot underflow
                // per delivered response was the stack-base corruption writer (frozen
                // re-dispatch + SIGSEGV while dying, 2026-09-11/12).
                if (hasBody)
                    lua_pushlstring(L, httpBuffer, static_cast<std::size_t>(bodySize));
                else
                    lua_pushnil(L); // request failed (offline / timeout / response missing)
                protectedCall(script, 1);
            } else {
                lua_pop(L, 1); // dead ref - the rawgeti pushed garbage
            }
        }
        if (slot.callbackRef != -1 && script.L)
            luaL_unref(script.L, LUA_REGISTRYINDEX, slot.callbackRef);
        slot = HttpSlot{};
    }
}

int parseIdaPattern(const char* pattern, PatternByte* out, int maxBytes) noexcept
{
    int byteCount = 0;
    for (const char* p = pattern; *p != '\0';) {
        if (*p == ' ') {
            ++p;
            continue;
        }
        if (byteCount >= maxBytes)
            return 0;
        if (*p == '?') { // "??", "?" - any run of '?' acts as a wildcard byte
            out[byteCount].wildcard = true;
            out[byteCount].value = 0;
            ++byteCount;
            ++p;
            if (*p == '?')
                ++p;
            continue;
        }
        int hex = 0;
        int digits = 0;
        while (digits < 2 && *p != '\0' && *p != ' ') {
            const char c = *p;
            int value;
            if (c >= '0' && c <= '9')
                value = c - '0';
            else if (c >= 'a' && c <= 'f')
                value = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                value = c - 'A' + 10;
            else
                return 0;
            hex = hex * 16 + value;
            ++digits;
            ++p;
        }
        if (digits == 0)
            return 0;
        out[byteCount].wildcard = false;
        out[byteCount].value = static_cast<unsigned char>(hex);
        ++byteCount;
    }
    return byteCount;
}

const unsigned char* scanMemoryPattern(const unsigned char* data, std::size_t size,
    const PatternByte* bytes, int byteCount) noexcept
{
    if (!data || byteCount <= 0 || byteCount > kMaxPatternBytes)
        return nullptr;
    for (std::size_t i = 0; i + static_cast<std::size_t>(byteCount) <= size; ++i) {
        int k = 0;
        while (k < byteCount && (bytes[k].wildcard || data[i + k] == bytes[k].value))
            ++k;
        if (k == byteCount)
            return data + i;
    }
    return nullptr;
}

// ---- dispatch (shared iteration body) ----

// Runs every callback registered under the array on top of the script's stack. args/argCount
// (optional) are pushed as a single `event` table before each callback. Pushes and pops its
// own temporaries.
static void runCallbacks(Script& script, int arrayIndex, const EventArg* args = nullptr, int argCount = 0) noexcept
{
    const int count = static_cast<int>(lua_objlen(script.L, arrayIndex));
    for (int k = 1; k <= count; ++k) {
        // Stack-balance guard: every callback must come back to the level the loop found it at
        // (array + fn [+ event table] pushed, pcall pops everything again). A binding that pops
        // more than it pushed leaves top BELOW base - the next push then stomps the stack-base
        // dummy frame (see repairLuaStackBase). Snap back + attribute before that can happen.
        const int before = lua_gettop(script.L);
        lua_rawgeti(script.L, arrayIndex, k);
        if (argCount > 0) {
            lua_createtable(script.L, 0, argCount);
            for (int a = 0; a < argCount; ++a) {
                lua_pushstring(script.L, args[a].key);
                if (args[a].isString)
                    lua_pushstring(script.L, args[a].strValue);
                else if (args[a].isNumber)
                    lua_pushnumber(script.L, args[a].numberValue);
                else
                    lua_pushinteger(script.L, args[a].intValue);
                lua_rawset(script.L, -3);
            }
        }
        protectedCall(script, argCount > 0 ? 1 : 0);
        const int after = lua_gettop(script.L);
        if (after != before) {
            lua_settop(script.L, before);
            static int logBudget = 16; // per-tick offenders would otherwise spam the anomaly log
            if (logBudget > 0) {
                --logBudget;
                gui_log::write("[lua] unbalanced callback in %s (%s): stack level moved %d -> %d, reset",
                    script.name, script.tabLabel[0] ? script.tabLabel : "-", before, after);
            }
        }
        if (script.errored)
            break; // auto-disabled mid-event; stop calling into it
    }
}

// Fetches callbacks[eventName]; leaves the stack as [callbacks table, array] with the array on
// top. Returns false when absent.
static bool fetchCallbackArray(Script& script, const char* eventName) noexcept
{
    lua_State* L = script.L;
    lua_pushliteral(L, "__ns_callbacks");
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_pushstring(L, eventName);
    lua_rawget(L, -2); // callbacks[eventName]
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return false;
    }
    return true;
}

// ---- public API ----

void init() noexcept
{
    const char* home = ::getenv("HOME");
    if (!home)
        return;
    NS_STR(configDir, "OsirisCS2");
    std::snprintf(scriptsDirPath, sizeof(scriptsDirPath), "%s/%s/scripts", home, (const char*)configDir);
    ::mkdir(scriptsDirPath, 0777); // exists -> EEXIST, harmless
}

// Executes <scriptsDir>/lib/*.lua into the state before the script body - the sandbox's require
// replacement: shared helper libraries (json, base64, vec3, easing are vendored there) run once
// per script load and expose their globals to the script. A lib error fails the script load with
// the lib name in the error text.
static bool preloadLibs(lua_State* L) noexcept
{
    char libDir[600];
    std::snprintf(libDir, sizeof(libDir), "%s/lib", scriptsDirPath);
    DIR* dir = ::opendir(libDir);
    if (!dir)
        return true; // no lib dir - nothing to preload
    constexpr int kMaxLibs = 16;
    char paths[kMaxLibs][768];
    int count = 0;
    while (count < kMaxLibs) {
        const dirent* entry = ::readdir(dir);
        if (!entry)
            break;
        const std::size_t length = std::strlen(entry->d_name);
        if (length <= 4 || std::strcmp(entry->d_name + length - 4, ".lua") != 0)
            continue;
        std::snprintf(paths[count], sizeof(paths[count]), "%s/%s", libDir, entry->d_name);
        ++count;
    }
    ::closedir(dir);
    // Lexicographic order (nested dirs not supported; a flat "00_json.lua" style prefix keeps
    // ordering explicit if it ever matters).
    for (int i = 1; i < count; ++i) {
        for (int j = i; j > 0 && std::strcmp(paths[j - 1], paths[j]) > 0; --j) {
            char swap[768];
            std::memcpy(swap, paths[j - 1], sizeof(swap));
            std::memcpy(paths[j - 1], paths[j], sizeof(swap));
            std::memcpy(paths[j], swap, sizeof(swap));
        }
    }
    for (int i = 0; i < count; ++i) {
        if (luaL_loadfile(L, paths[i]) != 0 || lua_pcall(L, 0, 1, 0) != 0) {
            const char* error = lua_tostring(L, -1);
            lua_pop(L, 1);
            char message[896];
            std::snprintf(message, sizeof(message), "lib preload failed (%s): %s", paths[i], error ? error : "unknown lib error");
            lua_pushlstring(L, message, std::strlen(message));
            return false;
        }
        // The lib's return value (a table) becomes a global named after the file stem
        // ("<stem>.lua" -> `<stem>`), so `json.lua` exposes itself as `json`. A lib that wants
        // several globals (vector.lua ships vec3 AND the standalone `angle` table) sets the
        // extra ones itself as globals during its chunk.
        const char* fileName = std::strrchr(paths[i], '/');
        fileName = fileName ? fileName + 1 : paths[i];
        char globalName[64];
        std::size_t nameLength = 0;
        for (const char* p = fileName; *p != '\0' && *p != '.' && nameLength < sizeof(globalName) - 1; ++p) {
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')
                globalName[nameLength++] = *p;
        }
        globalName[nameLength] = '\0';
        if (nameLength > 0)
            lua_setglobal(L, globalName); // pops the lib's return value (a table or nothing)
        else
            lua_pop(L, 1); // no meaningful name - drop the return value
    }
    return true;
}

bool load(const char* name) noexcept
{
    pthread_mutex_lock(&mutex);
    struct MutexUnlock {
        pthread_mutex_t& m;
        ~MutexUnlock() { pthread_mutex_unlock(&m); }
    } mutexUnlock{mutex};

    if (!scriptsDirPath[0] || !validScriptName(name))
        return false;

    const int existing = loadedIndex(name);
    if (existing >= 0)
        unloadScriptLocked(existing); // mutex already held here

    int slot = -1;
    for (int i = 0; i < kMaxScripts; ++i) {
        if (!scripts[i].L) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return false;

    char path[640];
    buildScriptPath(path, sizeof(path), name);

    lua_State* L = luaL_newstate();
    if (!L)
        return false;
    openSandbox(L);
    registerApi(L, slot);

    Script& script = scripts[slot];
    script = Script{};
    std::strncpy(script.name, name, kMaxScriptName - 1);
    script.L = L;
    loadGuiDefaults(name); // gui.* items created by the chunk below restore their saved values

    if (!preloadLibs(L)) {
        copyError(script, lua_tostring(L, -1));
        script.errored = true;
        queueScriptErrorToast(script, script.lastError);
        lua_pop(L, 1);
        return false;
    }

    if (luaL_loadfile(L, path) != 0) {
        copyError(script, lua_tostring(L, -1));
        script.errored = true;
        queueScriptErrorToast(script, script.lastError);
        lua_pop(L, 1);
        return false;
    }
    pendingItemPage = static_cast<int>(ScriptPage::Subtab); // gui.page() state is per-load
    return protectedCall(script, 0); // runs the chunk (top-level), registering its callbacks
}

// State teardown, mutex HELD. Called from the menu (present thread) AND from load()'s
// reload path (which already holds the lock), so the public wrappers below take the mutex
// around this. Without the lock an UNLOAD click in the Scripts > MANAGE page lua_close()'d
// the state on the present thread while dispatchTick ran that script's createmove callbacks
// on the game thread under the mutex - the freed stack then fed garbage into the next tick's
// curr_func() read (#GP in lua_pushcclosure).
static void unloadScriptLocked(int index) noexcept
{
    Script& script = scripts[index];
    if (script.L) {
        // "unload" callbacks run first so the script can release what it owns (net block
        // lists are cleared below regardless). Fired with no dispatch thread active, so the
        // game-thread-only bindings correctly refuse - unload runs on the menu thread.
        // Errors here only mark the (already closing) script; nothing else can run after.
        if (script.hasUnload && !script.errored) {
            dispatchThreadKind.store(0, std::memory_order_relaxed);
            if (fetchCallbackArray(script, "unload")) {
                runCallbacks(script, -1);
                lua_pop(script.L, 2); // array + callbacks table
            }
        }
        saveGuiState(script); // persist gui.* + imgui.* values across reloads/unloads
        lua_close(script.L);
        script.L = nullptr;
    }
    // Script-owned renderer textures go back to the pool (the GPU state itself is retired by
    // VulkanHook on the present path; the slot is reusable immediately).
    releaseScriptTextures(index);
    // A region-filter block list is script-owned state published into the datagram hook - a
    // dead script can no longer manage it, so it must not survive the unload. (A reloading
    // script re-applies its list on the next tick.)
    net_region::clearBlockedIps();
    killPendingHttpFor(index);
    if (std::strcmp(pendingGuiOwner, script.name) == 0)
        pendingGuiDefaultCount = 0; // no stale defaults for a different script
    script = Script{};
}

void unloadScript(int index) noexcept
{
    pthread_mutex_lock(&mutex);
    struct MutexUnlock {
        pthread_mutex_t& m;
        ~MutexUnlock() { pthread_mutex_unlock(&m); }
    } mutexUnlock{mutex};
    unloadScriptLocked(index);
}

void unloadAll() noexcept
{
    pthread_mutex_lock(&mutex);
    struct MutexUnlock {
        pthread_mutex_t& m;
        ~MutexUnlock() { pthread_mutex_unlock(&m); }
    } mutexUnlock{mutex};
    for (int i = 0; i < kMaxScripts; ++i) {
        if (scripts[i].L)
            unloadScriptLocked(i);
    }
}

int listFiles(FileEntry* out, int max) noexcept
{
    if (!scriptsDirPath[0])
        return 0;
    DIR* dir = ::opendir(scriptsDirPath);
    if (!dir)
        return 0;
    int count = 0;
    while (count < max) {
        const dirent* entry = ::readdir(dir);
        if (!entry)
            break;
        const std::size_t length = std::strlen(entry->d_name);
        if (length <= 4 || length >= kMaxScriptName || std::strcmp(entry->d_name + length - 4, ".lua") != 0)
            continue;
        struct stat st{};
        char path[768];
        std::snprintf(path, sizeof(path), "%s/%s", scriptsDirPath, entry->d_name);
        if (::stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        std::strncpy(out[count].name, entry->d_name, kMaxScriptName - 1);
        out[count].name[kMaxScriptName - 1] = '\0';
        out[count].size = static_cast<long>(st.st_size);
        ++count;
    }
    ::closedir(dir);
    return count;
}

bool readScript(const char* name, char* buffer, std::size_t bufferSize, long* outSize) noexcept
{
    if (!scriptsDirPath[0] || !validScriptName(name))
        return false;
    char path[640];
    buildScriptPath(path, sizeof(path), name);
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0)
        return false;
    long total = 0;
    for (;;) {
        const ssize_t bytes = ::read(fd, buffer + total, bufferSize - 1 - total);
        if (bytes <= 0)
            break;
        total += bytes;
        if (static_cast<std::size_t>(total) >= bufferSize - 1)
            break;
    }
    ::close(fd);
    buffer[total] = '\0';
    if (outSize)
        *outSize = total;
    return true;
}

bool writeScript(const char* name, const char* content, std::size_t length) noexcept
{
    if (!scriptsDirPath[0] || !validScriptName(name) || length > kMaxScriptBytes)
        return false;
    char path[640];
    buildScriptPath(path, sizeof(path), name);
    char tempPath[648];
    std::snprintf(tempPath, sizeof(tempPath), "%s.new", path);
    const int fd = ::open(tempPath, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0)
        return false;
    std::size_t written = 0;
    while (written < length) {
        const ssize_t bytes = ::write(fd, content + written, length - written);
        if (bytes <= 0) {
            ::close(fd);
            ::unlink(tempPath);
            return false;
        }
        written += static_cast<std::size_t>(bytes);
    }
    ::close(fd);
    if (::rename(tempPath, path) != 0) {
        ::unlink(tempPath);
        return false;
    }
    return true;
}

bool createScript(const char* name) noexcept
{
    static constexpr char kTemplate[] =
        "-- script template\n\n"
        "client.set_event_callback(\"paint\", function()\n"
        "    renderer.text(20, 200, \"hello from lua\", 255, 255, 255, 255)\n"
        "end)\n";
    if (!validScriptName(name))
        return false;
    char path[640];
    buildScriptPath(path, sizeof(path), name);
    const int fd = ::open(path, O_CREAT | O_EXCL | O_WRONLY, 0666);
    if (fd < 0)
        return false; // already exists
    const std::size_t length = sizeof(kTemplate) - 1;
    const ssize_t written = ::write(fd, kTemplate, length);
    ::close(fd);
    return written == static_cast<ssize_t>(length);
}

bool deleteScript(const char* name) noexcept
{
    if (!scriptsDirPath[0] || !validScriptName(name))
        return false;
    const int index = loadedIndex(name);
    if (index >= 0)
        unloadScript(index);
    char path[640];
    buildScriptPath(path, sizeof(path), name);
    return ::unlink(path) == 0;
}

void dispatchEvent(const char* eventName, const EventArg* args, int argCount) noexcept
{
    if (!eventName || !scriptsDirPath[0])
        return;
    if (argCount < 0)
        argCount = 0;
    pthread_mutex_lock(&mutex);
    struct MutexUnlock {
        pthread_mutex_t& m;
        ~MutexUnlock() { pthread_mutex_unlock(&m); }
    } mutexUnlock{mutex};

    for (int i = 0; i < kMaxScripts; ++i)
        if (scripts[i].L)
            repairLuaStackBase(scripts[i].L);

    // Game events fire on the game thread - unlock client.exec while we are here.
    dispatchThreadKind.store(1, std::memory_order_relaxed);
    for (int i = 0; i < kMaxScripts; ++i) {
        Script& script = scripts[i];
        if (!script.L || script.errored)
            continue;
        if (fetchCallbackArray(script, eventName)) {
            runCallbacks(script, -1, args, argCount);
            lua_pop(script.L, 2); // array + callbacks table
        }
    }
    dispatchThreadKind.store(0, std::memory_order_relaxed);
}

// Fires every client.delay_call whose deadline passed. Mutex held; runs on whichever dispatch
// (paint or tick) reaches the deadline first - a delay queued from paint fires on paint, one from
// createmove on a later tick.
static void drainPendingCalls() noexcept
{
    const double now = luaNow();
    for (int i = 0; i < kMaxScripts; ++i) {
        Script& script = scripts[i];
        if (!script.L || script.errored || script.pendingCallCount == 0)
            continue;
        int k = 0;
        while (k < script.pendingCallCount) {
            if (script.pendingCalls[k].when > now) {
                ++k;
                continue;
            }
            const PendingCall call = script.pendingCalls[k];
            script.pendingCalls[k] = script.pendingCalls[--script.pendingCallCount];
            script.pendingCalls[script.pendingCallCount] = PendingCall{};

            lua_State* L = script.L;
            lua_rawgeti(L, LUA_REGISTRYINDEX, call.functionRef);
            int nargs = 0;
            if (call.argsRef != -1 && call.argsRef != LUA_REFNIL) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, call.argsRef);
                const int tableIndex = lua_gettop(L); // absolute index - negative indices shift as we push
                nargs = static_cast<int>(lua_objlen(L, tableIndex));
                for (int a = 1; a <= nargs; ++a)
                    lua_rawgeti(L, tableIndex, a); // push args in order above the table
                lua_remove(L, tableIndex); // drop the table, args remain
            }
            luaL_unref(L, LUA_REGISTRYINDEX, call.functionRef);
            if (call.argsRef != -1 && call.argsRef != LUA_REFNIL)
                luaL_unref(L, LUA_REGISTRYINDEX, call.argsRef);
            protectedCall(script, nargs);
            if (script.errored)
                break;
            // do not ++k: the last pending call was swapped into slot k
        }
    }
}

// Draws the toast queue (top-center stack with fade-in/out) after the script callbacks. Mutex
// held, present thread, paintDrawList valid.
static void drawToasts(ImDrawList* drawList) noexcept
{
    const double now = luaNow();
    const float displayWidth = ImGui::GetIO().DisplaySize.x;
    ImFont* font = ImGui::GetIO().Fonts->Fonts[0];
    if (!font)
        return;

    int row = 0;
    for (int i = 0; i < kMaxToasts; ++i) {
        const Toast& toast = toasts[i];
        if (!toast.active)
            continue;
        const double age = now - toast.when;
        if (age > kToastLifeSeconds) {
            toasts[i].active = false;
            continue;
        }
        float alpha = 1.0f;
        if (age < 0.2)
            alpha = static_cast<float>(age / 0.2);
        else if (age > kToastLifeSeconds - 0.6)
            alpha = static_cast<float>((kToastLifeSeconds - age) / 0.6);

        const ImVec2 size = font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, toast.text);
        const float boxWidth = size.x + 24.0f;
        const float boxHeight = size.y + 12.0f;
        const float y = 90.0f + row * 34.0f;
        const float slide = (1.0f - alpha) * -10.0f;
        const ImU32 bg = IM_COL32(16, 16, 18, static_cast<int>(215 * alpha));
        const ImU32 border = IM_COL32(70, 74, 84, static_cast<int>(200 * alpha));
        drawList->AddRectFilled(ImVec2((displayWidth - boxWidth) * 0.5f, y + slide),
            ImVec2((displayWidth + boxWidth) * 0.5f, y + boxHeight + slide), bg, 8.0f);
        drawList->AddRect(ImVec2((displayWidth - boxWidth) * 0.5f, y + slide),
            ImVec2((displayWidth + boxWidth) * 0.5f, y + boxHeight + slide), border, 8.0f);
        drawList->AddText(font, 13.0f,
            ImVec2((displayWidth - size.x) * 0.5f, y + 6.0f + slide),
            (toast.color & 0x00FFFFFF) | (static_cast<ImU32>(255 * alpha) << 24), toast.text);
        ++row;
    }
}

void dispatchPaint(ImDrawList* drawList) noexcept
{
    pthread_mutex_lock(&mutex);
    struct MutexUnlock {
        pthread_mutex_t& m;
        ~MutexUnlock() { pthread_mutex_unlock(&m); }
    } mutexUnlock{mutex};

    for (int i = 0; i < kMaxScripts; ++i)
        if (scripts[i].L)
            repairLuaStackBase(scripts[i].L);

    paintDrawList = drawList;
    paintClipDepth = 0; // a callback that errored mid-clip must not poison the next frame
    dispatchThreadKind.store(2, std::memory_order_relaxed);
    for (int i = 0; i < kMaxScripts; ++i) {
        Script& script = scripts[i];
        if (!script.L || script.errored || !script.hasPaint)
            continue;
        if (fetchCallbackArray(script, "paint")) {
            runCallbacks(script, -1);
            lua_pop(script.L, 2);
        }
    }
    // A callback that errored (or forgot) mid push_clip must not leave a dangling clip rect
    // in this frame's draw list - unwind on the same list the clips were pushed to.
    if (drawList) {
        while (paintClipDepth > 0) {
            drawList->PopClipRect();
            --paintClipDepth;
        }
        drawToasts(drawList);
    }
    paintDrawList = nullptr;
    dispatchThreadKind.store(0, std::memory_order_relaxed);
    drainPendingCalls();
    pollHttp();
}

// Present thread, called from the menu shell render while the menu is open (ImGui context
// alive): fires every script's "menu" callback - the surface imgui.* bindings require. After
// EACH script's callbacks the ImGui window/style stacks are force-balanced, because a Lua
// error inside a window (protectedCall catches it, but the C++ Begin() would stay open) would
// trip ImGui's stack assertions on the next EndFrame.
void dispatchMenuWindows() noexcept
{
    if (!scriptsDirPath[0])
        return;
    // No live ImGui context (unit tests, shutdown): firing "menu" callbacks would let the
    // imgui.* bindings call ImGui with no context - skip the dispatch entirely.
    if (!imguiContextQuery || !imguiContextQuery())
        return;
    pthread_mutex_lock(&mutex);
    struct MutexUnlock {
        pthread_mutex_t& m;
        ~MutexUnlock() { pthread_mutex_unlock(&m); }
    } mutexUnlock{mutex};

    for (int i = 0; i < kMaxScripts; ++i)
        if (scripts[i].L)
            repairLuaStackBase(scripts[i].L);

    dispatchThreadKind.store(2, std::memory_order_relaxed);
    imguiMenuActive = true;
    for (int i = 0; i < kMaxScripts; ++i) {
        Script& script = scripts[i];
        if (!script.L || script.errored || !script.hasMenu)
            continue;
        script.imguiWidgetCount = 0; // per-frame widget slots restart every dispatch
        if (fetchCallbackArray(script, "menu")) {
            runCallbacks(script, -1);
            lua_pop(script.L, 2);
        }
        // Safety net: the callback may have errored (or been sloppy) with windows open.
        // imgui.begin pushes 2 style colors + 2 style vars per window BEFORE Begin().
        if (imguiWindowDepth > 0) {
            const int leaked = imguiWindowDepth;
            imguiWindowDepth = 0;
            for (int w = 0; w < leaked; ++w)
                ImGui::End();
            ImGui::PopStyleColor(2 * leaked);
            ImGui::PopStyleVar(2 * leaked);
            gui_log::write("[lua] imgui window left open by %s - force-closed (%d)", script.name, leaked);
        }
        script.imguiWidgetCount = 0;
    }
    imguiMenuActive = false;
    dispatchThreadKind.store(0, std::memory_order_relaxed);
}

int scriptPageFromName(const char* name) noexcept
{
    if (!name || !name[0])
        return -2;
    for (int i = 0; i < kScriptPageCount; ++i) {
        std::size_t k = 0;
        for (; kScriptPageNames[i][k] != '\0' && name[k] != '\0'; ++k) {
            const char a = kScriptPageNames[i][k] >= 'A' && kScriptPageNames[i][k] <= 'Z'
                ? static_cast<char>(kScriptPageNames[i][k] - 'A' + 'a') : kScriptPageNames[i][k];
            const char b = name[k] >= 'A' && name[k] <= 'Z' ? static_cast<char>(name[k] - 'A' + 'a') : name[k];
            if (a != b)
                break;
        }
        if (kScriptPageNames[i][k] == '\0' && name[k] == '\0')
            return i;
    }
    // common alias: the nav rail calls the Glow page "Visuals"
    {
        std::size_t k = 0;
        for (; "visuals"[k] != '\0' && name[k] != '\0'; ++k) {
            const char b = name[k] >= 'A' && name[k] <= 'Z' ? static_cast<char>(name[k] - 'A' + 'a') : name[k];
            if ("visuals"[k] != b)
                break;
        }
        if ("visuals"[k] == '\0' && name[k] == '\0')
            return static_cast<int>(ScriptPage::Glow);
    }
    return -2;
}

void dispatchTick(void* userCmd) noexcept
{    // Per-tick path (game thread): identical to dispatchEvent("createmove"), gated on the
    // cheap hasTick flag so scripts without a tick callback cost nothing. `userCmd` is the
    // tick's cs2::CUserCmd* - valid only for the duration of this call.
    if (!scriptsDirPath[0])
        return;
    pthread_mutex_lock(&mutex);
    struct MutexUnlock {
        pthread_mutex_t& m;
        ~MutexUnlock() { pthread_mutex_unlock(&m); }
    } mutexUnlock{mutex};

    for (int i = 0; i < kMaxScripts; ++i)
        if (scripts[i].L)
            repairLuaStackBase(scripts[i].L);

    tickUserCmd = userCmd;
    dispatchThreadKind.store(1, std::memory_order_relaxed);
    for (int i = 0; i < kMaxScripts; ++i) {
        Script& script = scripts[i];
        if (!script.L || script.errored || !script.hasTick)
            continue;
        if (fetchCallbackArray(script, "createmove")) {
            runCallbacks(script, -1);
            lua_pop(script.L, 2);
        }
    }
    tickUserCmd = nullptr;
    dispatchThreadKind.store(0, std::memory_order_relaxed);
    drainPendingCalls();
}

} // namespace lua
