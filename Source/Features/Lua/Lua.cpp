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
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

extern "C" {
#include <lualib.h>
#include <lauxlib.h>
#include <lua.h>
}

#include <Platform/Linux/LinuxDynamicLibrary.h>
#include <Utils/VerifyConsole.h>

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

// posix_spawn environment (unistd.h only declares it under feature macros - mirror RadioManager)
extern "C" char** environ;

// posix_spawn helper used by http.get: the game runs inside the Steam Linux Runtime container,
// /tmp is shared with the host, and the host's curl resolves DNS + TLS (same launch path the
// web radio uses).
[[nodiscard]] static pid_t spawnHostShell(const char* script) noexcept
{
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

// Calls the function (plus nargs arguments) currently on top of the stack of `script`.
// On error: records the traceback in the script and marks it errored (auto-disabled).
static bool protectedCall(Script& script, int nargs) noexcept
{
    lua_State* L = script.L;
    lua_pushcfunction(L, errorHandler);
    lua_insert(L, -(nargs + 2)); // handler below function + args
    lua_sethook(L, budgetHook, LUA_MASKCOUNT, kInstructionBudget);
    const int status = lua_pcall(L, nargs, 0, -(nargs + 2));
    lua_sethook(L, nullptr, 0, 0);
    if (status == 0) {
        lua_pop(L, 1); // the error handler
        return true;
    }
    copyError(script, lua_tostring(L, -1));
    script.errored = true;
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
                if (hasBody)
                    lua_pushlstring(L, httpBuffer, static_cast<std::size_t>(bodySize));
                else
                    lua_pushnil(L); // request failed (offline / timeout / response missing)
                protectedCall(script, 1);
            }
            lua_pop(L, 1); // callback
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

// Runs every callback registered under the string key pushed on top of the script's stack
// (already fetched from the callbacks table). Pushes and pops its own temporaries.
static void runCallbacks(Script& script, int arrayIndex) noexcept
{
    const int count = static_cast<int>(lua_objlen(script.L, arrayIndex));
    for (int k = 1; k <= count; ++k) {
        lua_rawgeti(script.L, arrayIndex, k);
        protectedCall(script, 0);
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
    std::snprintf(scriptsDirPath, sizeof(scriptsDirPath), "%s/OsirisCS2/scripts", home);
    ::mkdir(scriptsDirPath, 0777); // exists -> EEXIST, harmless
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
        unloadScript(existing);

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

    if (luaL_loadfile(L, path) != 0) {
        copyError(script, lua_tostring(L, -1));
        script.errored = true;
        lua_pop(L, 1);
        return false;
    }
    return protectedCall(script, 0); // runs the chunk (top-level), registering its callbacks
}

void unloadScript(int index) noexcept
{
    Script& script = scripts[index];
    if (script.L) {
        lua_close(script.L);
        script.L = nullptr;
    }
    killPendingHttpFor(index);
    script = Script{};
}

void unloadAll() noexcept
{
    for (int i = 0; i < kMaxScripts; ++i) {
        if (scripts[i].L)
            unloadScript(i);
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
        "-- Neversneeze script\n\n"
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

void dispatchEvent(const char* eventName) noexcept
{
    if (!eventName || !scriptsDirPath[0])
        return;
    pthread_mutex_lock(&mutex);
    struct MutexUnlock {
        pthread_mutex_t& m;
        ~MutexUnlock() { pthread_mutex_unlock(&m); }
    } mutexUnlock{mutex};

    for (int i = 0; i < kMaxScripts; ++i) {
        Script& script = scripts[i];
        if (!script.L || script.errored)
            continue;
        if (fetchCallbackArray(script, eventName)) {
            runCallbacks(script, -1);
            lua_pop(script.L, 2); // array + callbacks table
        }
    }
}

void dispatchPaint(ImDrawList* drawList) noexcept
{
    pthread_mutex_lock(&mutex);
    struct MutexUnlock {
        pthread_mutex_t& m;
        ~MutexUnlock() { pthread_mutex_unlock(&m); }
    } mutexUnlock{mutex};

    paintDrawList = drawList;
    for (int i = 0; i < kMaxScripts; ++i) {
        Script& script = scripts[i];
        if (!script.L || script.errored || !script.hasPaint)
            continue;
        if (fetchCallbackArray(script, "paint")) {
            runCallbacks(script, -1);
            lua_pop(script.L, 2);
        }
    }
    paintDrawList = nullptr;
    pollHttp();
}

void dispatchTick() noexcept
{
    // Per-tick path (game thread): identical to dispatchEvent("createmove"), gated on the
    // cheap hasTick flag so scripts without a tick callback cost nothing.
    if (!scriptsDirPath[0])
        return;
    pthread_mutex_lock(&mutex);
    struct MutexUnlock {
        pthread_mutex_t& m;
        ~MutexUnlock() { pthread_mutex_unlock(&m); }
    } mutexUnlock{mutex};

    for (int i = 0; i < kMaxScripts; ++i) {
        Script& script = scripts[i];
        if (!script.L || script.errored || !script.hasTick)
            continue;
        if (fetchCallbackArray(script, "createmove")) {
            runCallbacks(script, -1);
            lua_pop(script.L, 2);
        }
    }
}

} // namespace lua
