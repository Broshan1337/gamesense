









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




static void* tickUserCmd = nullptr;


extern "C" char** environ;



double luaNow() noexcept;



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




static int pendingItemPage = static_cast<int>(ScriptPage::Subtab);



static bool imguiMenuActive = false;



static int imguiWindowDepth = 0;


static int paintClipDepth = 0;




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
static void unloadScriptLocked(int index) noexcept; 









struct PendingGuiValue {
    char label[kMaxGuiLabel] = {};
    
    
    
    
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
        return; 

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
                : 0; 
        else
            length = std::snprintf(line, sizeof(line), "s\t%s\t%d\t%d\t%d\n", item.label, item.intValue, item.minValue, item.maxValue);
        writeLine(line, length);
    }
    
    
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



void formatSteamId64(std::uint64_t sid, char* out, std::size_t outSize) noexcept
{
    std::snprintf(out, outSize, "%llu", static_cast<unsigned long long>(sid));
}

bool parseSteamId64(const char* text, std::uint64_t* out) noexcept
{
    if (!text || !out || text[0] < '1' || text[0] > '9')
        return false; 
    std::uint64_t value = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9')
            return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(*p - '0');
        if (value > (0xFFFFFFFFFFFFFFFFULL - digit) / 10ULL)
            return false; 
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}



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



static bool copyMapName(char* out, std::size_t cap) noexcept
{
    pthread_mutex_lock(&mapNameMutex);
    const bool ok = mapNameBuffer[0] != '\0';
    if (ok)
        std::snprintf(out, cap, "%s", mapNameBuffer);
    pthread_mutex_unlock(&mapNameMutex);
    return ok;
}


#include "LuaApi.h"



static void copyError(Script& script, const char* message) noexcept
{
    if (!message)
        message = "(unknown lua error)";
    std::size_t i = 0;
    for (; message[i] != '\0' && i < kMaxError - 1; ++i)
        script.lastError[i] = message[i] == '\n' ? ' ' : message[i];
    script.lastError[i] = '\0';
}


static void budgetHook(lua_State* L, lua_Debug*)
{
    luaL_error(L, "instruction budget exceeded");
}


static int errorHandler(lua_State* L)
{
    const char* message = lua_tostring(L, 1);
    luaL_traceback(L, L, message ? message : "(non-string error object)", 1);
    return 1;
}
















std::atomic<int> stackBaseRepairCountForTesting{0};


bool (*stackBaseRepairLogQuery)() noexcept = nullptr;

static void repairLuaStackBase(lua_State* L) noexcept
{
    static int logBudget = 16; 
    const TValue* stack = mref(L->stack, TValue);
    if (!stack)
        return;
    const uint64_t expectedThread = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(L)) | (static_cast<uint64_t>(LJ_TTHREAD) << 47);
    if (stack[0].gcr.gcptr64 != expectedThread || stack[1].it64 != -1) {
        CrashLogger::trace(0x360);
        stackBaseRepairCountForTesting.fetch_add(1, std::memory_order_relaxed);
        if (logBudget > 0 && (!stackBaseRepairLogQuery || stackBaseRepairLogQuery())) {
            --logBudget;
            
            
            
            
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










static bool protectedCall(Script& script, int nargs) noexcept
{
    lua_State* L = script.L;
    const int before = lua_gettop(L);
    lua_pushcfunction(L, errorHandler);
    lua_insert(L, -(nargs + 2)); 
    lua_sethook(L, budgetHook, LUA_MASKCOUNT, kInstructionBudget);
    const int status = lua_pcall(L, nargs, 0, -(nargs + 2));
    lua_sethook(L, nullptr, 0, 0);
    const int after = lua_gettop(L); 
    const int expected = before - nargs + (status != 0 ? 1 : 0);
    if (after != expected) {
        lua_settop(L, expected);
        static int logBudget = 16; 
        if (logBudget > 0) {
            --logBudget;
            gui_log::write("[lua] unbalanced protectedCall in %s (%s): stack level moved %d -> %d, reset",
                script.name, script.tabLabel[0] ? script.tabLabel : "-", before, after);
        }
    }
    if (status == 0) {
        lua_pop(L, 1); 
        return true;
    }
    const char* errorObject = lua_tostring(L, -1);
    copyError(script, errorObject);
    script.errored = true;
    queueScriptErrorToast(script, script.lastError); 
    lua_pop(L, 2); 
    return false;
}



static void preloadGlobal(lua_State* L, const char* name) noexcept
{
    lua_getglobal(L, "require");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_pushstring(L, name);
    if (lua_pcall(L, 1, 1, 0) == 0)
        lua_setglobal(L, name); 
    else
        lua_pop(L, 1); 
}


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

    
    
    
    
    
    
    
    lua_getglobal(L, "jit");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "off");
        if (lua_isfunction(L, -1))
            lua_call(L, 0, 0); 
    }
    lua_pop(L, 1);

    
    
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

    
    
    lua_getglobal(L, "debug");
    if (lua_istable(L, -1)) {
        lua_newtable(L);
        lua_getfield(L, -2, "traceback");
        lua_setfield(L, -2, "traceback");
        lua_setglobal(L, "debug");
    }
    lua_pop(L, 1);
}



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
    
    return i > 4 && i <= kMaxScriptName - 1 && name[i - 4] == '.' && name[i - 3] == 'l' && name[i - 2] == 'u' && name[i - 1] == 'a';
}

static void buildScriptPath(char* out, std::size_t outSize, const char* name) noexcept
{
    
    
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
                slot.processDone = true; 
            } else {
                continue;
            }
        }
        if (!slot.processDone)
            continue;

        
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
                
                
                
                
                
                
                if (hasBody)
                    lua_pushlstring(L, httpBuffer, static_cast<std::size_t>(bodySize));
                else
                    lua_pushnil(L); 
                protectedCall(script, 1);
            } else {
                lua_pop(L, 1); 
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
        if (*p == '?') { 
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






static void runCallbacks(Script& script, int arrayIndex, const EventArg* args = nullptr, int argCount = 0) noexcept
{
    const int count = static_cast<int>(lua_objlen(script.L, arrayIndex));
    for (int k = 1; k <= count; ++k) {
        
        
        
        
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
            static int logBudget = 16; 
            if (logBudget > 0) {
                --logBudget;
                gui_log::write("[lua] unbalanced callback in %s (%s): stack level moved %d -> %d, reset",
                    script.name, script.tabLabel[0] ? script.tabLabel : "-", before, after);
            }
        }
        if (script.errored)
            break; 
    }
}



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
    lua_rawget(L, -2); 
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return false;
    }
    return true;
}



void init() noexcept
{
    const char* home = ::getenv("HOME");
    if (!home)
        return;
    NS_STR(configDir, "OsirisCS2");
    std::snprintf(scriptsDirPath, sizeof(scriptsDirPath), "%s/%s/scripts", home, (const char*)configDir);
    ::mkdir(scriptsDirPath, 0777); 
}





static bool preloadLibs(lua_State* L) noexcept
{
    char libDir[600];
    std::snprintf(libDir, sizeof(libDir), "%s/lib", scriptsDirPath);
    DIR* dir = ::opendir(libDir);
    if (!dir)
        return true; 
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
            lua_setglobal(L, globalName); 
        else
            lua_pop(L, 1); 
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
        unloadScriptLocked(existing); 

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
    loadGuiDefaults(name); 

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
    pendingItemPage = static_cast<int>(ScriptPage::Subtab); 
    return protectedCall(script, 0); 
}







static void unloadScriptLocked(int index) noexcept
{
    Script& script = scripts[index];
    if (script.L) {
        
        
        
        
        if (script.hasUnload && !script.errored) {
            dispatchThreadKind.store(0, std::memory_order_relaxed);
            if (fetchCallbackArray(script, "unload")) {
                runCallbacks(script, -1);
                lua_pop(script.L, 2); 
            }
        }
        saveGuiState(script); 
        lua_close(script.L);
        script.L = nullptr;
    }
    
    
    releaseScriptTextures(index);
    
    
    
    net_region::clearBlockedIps();
    killPendingHttpFor(index);
    if (std::strcmp(pendingGuiOwner, script.name) == 0)
        pendingGuiDefaultCount = 0; 
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
        return false; 
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

    
    dispatchThreadKind.store(1, std::memory_order_relaxed);
    for (int i = 0; i < kMaxScripts; ++i) {
        Script& script = scripts[i];
        if (!script.L || script.errored)
            continue;
        if (fetchCallbackArray(script, eventName)) {
            runCallbacks(script, -1, args, argCount);
            lua_pop(script.L, 2); 
        }
    }
    dispatchThreadKind.store(0, std::memory_order_relaxed);
}




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
                const int tableIndex = lua_gettop(L); 
                nargs = static_cast<int>(lua_objlen(L, tableIndex));
                for (int a = 1; a <= nargs; ++a)
                    lua_rawgeti(L, tableIndex, a); 
                lua_remove(L, tableIndex); 
            }
            luaL_unref(L, LUA_REGISTRYINDEX, call.functionRef);
            if (call.argsRef != -1 && call.argsRef != LUA_REFNIL)
                luaL_unref(L, LUA_REGISTRYINDEX, call.argsRef);
            protectedCall(script, nargs);
            if (script.errored)
                break;
            
        }
    }
}



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
    paintClipDepth = 0; 
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






void dispatchMenuWindows() noexcept
{
    if (!scriptsDirPath[0])
        return;
    
    
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
        script.imguiWidgetCount = 0; 
        if (fetchCallbackArray(script, "menu")) {
            runCallbacks(script, -1);
            lua_pop(script.L, 2);
        }
        
        
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
{    
    
    
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

} 
