#pragma once

// Native-config bridge for the Lua config.* API.
//
// Included ONLY from EntryPoints.h (which owns the HookContext machinery) - never from
// Lua.cpp, so the framework core stays unit-testable with all config queries null.
//
// The config schema is template-driven C++ with no runtime names, but the SAME schema walk
// the .cfg save/load uses (ConfigSchema::performConversion) visits every var with its
// dotted path. These walkers record/apply through that walk, so config.get/set/list address
// vars by path ("Combat.Triggerbot.Enabled") with zero per-var glue and zero drift: a var
// added to the schema is automatically addressable.
//
// config.set carries config-file-load semantics: range-clamped/saturated by the schema's own
// setters, change handlers NOT invoked, autosave scheduled + changeEpoch bumped (the menu's
// dirty dot tracks it like any menu click).

#include <cstdio>
#include <cstring>
#include <type_traits>

#include <Config/ConfigSchema.h>
#include <UI/ImGui/UiConfig.h>
#include <Utils/ColorUtils.h>

#include <Features/Lua/LuaManager.h>

namespace lua_config_bridge
{

inline constexpr int kMaxWalkDepth = 16;

struct PathStack {
    char path[lua::kMaxConfigPath] = {};
    int prefixLength[kMaxWalkDepth] = {};
    int depth = 0;

    void push(const char* name) noexcept
    {
        if (depth >= kMaxWalkDepth || !name)
            return;
        prefixLength[depth] = static_cast<int>(std::strlen(path));
        std::size_t used = static_cast<std::size_t>(prefixLength[depth]);
        if (used > 0 && used + 1 < sizeof(path)) {
            path[used++] = '.';
            path[used] = '\0';
        }
        for (const char* p = name; *p != '\0' && used + 1 < sizeof(path); ++p)
            path[used++] = *p;
        path[used] = '\0';
        ++depth;
    }

    void pop() noexcept
    {
        if (depth <= 0)
            return;
        path[prefixLength[--depth]] = '\0';
    }
};

inline void appendKeyName(char* out, std::size_t outSize, const char8_t* key) noexcept
{
    std::size_t i = 0;
    for (; key[i] != u8'\0' && i + 1 < outSize; ++i)
        out[i] = static_cast<char>(key[i]);
    out[i] = '\0';
}

// ---- count + list ----

struct CountWalk {
    PathStack stack;
    int count = 0;

    void beginRoot() noexcept {}
    std::size_t endRoot() noexcept { return 0; }
    void beginObject(const char8_t* key) noexcept
    {
        char name[64];
        appendKeyName(name, sizeof(name), key);
        stack.push(name);
    }
    void endObject() noexcept { stack.pop(); }
    void boolean(const char8_t*, auto&&, auto&&) noexcept { ++count; }
    void uint(const char8_t*, auto&&, auto&&) noexcept { ++count; }
    void floating(const char8_t*, auto&&, auto&&) noexcept { ++count; }
};

struct ListWalk {
    PathStack stack;
    int wantIndex = 0;
    int cursor = 0;
    char* outPath = nullptr;
    int pathCap = 0;
    int* outKind = nullptr;
    bool done = false;

    void beginRoot() noexcept {}
    std::size_t endRoot() noexcept { return 0; }
    void beginObject(const char8_t* key) noexcept
    {
        char name[64];
        appendKeyName(name, sizeof(name), key);
        stack.push(name);
    }
    void endObject() noexcept { stack.pop(); }
    void boolean(const char8_t* key, auto&&, auto&&) noexcept
    {
        record(static_cast<int>(lua::ConfigValueKind::Bool), key);
    }
    void uint(const char8_t* key, auto&&, auto&& valueGetter) noexcept
    {
        using GetterType = decltype(valueGetter());
        constexpr bool isColor = std::is_same_v<GetterType, color::Rgba>;
        record(static_cast<int>(isColor ? lua::ConfigValueKind::Color : lua::ConfigValueKind::Uint), key);
    }
    void floating(const char8_t* key, auto&&, auto&&) noexcept
    {
        record(static_cast<int>(lua::ConfigValueKind::Float), key);
    }

    void record(int kind, const char8_t* key) noexcept
    {
        if (done)
            return;
        if (cursor++ != wantIndex)
            return;
        done = true;
        char name[64];
        if (key)
            appendKeyName(name, sizeof(name), key);
        else
            name[0] = '\0';
        if (name[0] != '\0')
            stack.push(name);
        std::snprintf(outPath, static_cast<std::size_t>(pathCap), "%s", stack.path);
        if (name[0] != '\0')
            stack.pop();
        if (outKind)
            *outKind = kind;
    }
};

// ---- get ----

struct GetWalk {
    PathStack stack;
    const char* wantPath = nullptr;
    lua::ConfigValue* out = nullptr;
    bool found = false;

    void beginRoot() noexcept {}
    std::size_t endRoot() noexcept { return 0; }
    void beginObject(const char8_t* key) noexcept
    {
        char name[64];
        appendKeyName(name, sizeof(name), key);
        stack.push(name);
    }
    void endObject() noexcept { stack.pop(); }

    bool atLeaf(const char8_t* key) noexcept
    {
        if (found || !wantPath || !out)
            return false;
        if (key) {
            char name[64];
            appendKeyName(name, sizeof(name), key);
            stack.push(name);
            const bool match = std::strcmp(stack.path, wantPath) == 0;
            stack.pop();
            return match;
        }
        return std::strcmp(stack.path, wantPath) == 0;
    }

    void boolean(const char8_t* key, auto&&, auto&& valueGetter) noexcept
    {
        if (!atLeaf(key))
            return;
        found = true;
        out->kind = lua::ConfigValueKind::Bool;
        out->boolValue = static_cast<bool>(valueGetter());
    }

    void uint(const char8_t* key, auto&&, auto&& valueGetter) noexcept
    {
        if (!atLeaf(key))
            return;
        found = true;
        using GetterType = decltype(valueGetter());
        if constexpr (std::is_same_v<GetterType, color::Rgba>) {
            const color::Rgba color = valueGetter();
            out->kind = lua::ConfigValueKind::Color;
            out->color[0] = color.r();
            out->color[1] = color.g();
            out->color[2] = color.b();
            out->color[3] = color.a();
        } else {
            out->kind = lua::ConfigValueKind::Uint;
            out->uintValue = static_cast<unsigned long long>(valueGetter());
        }
    }

    void floating(const char8_t* key, auto&&, auto&& valueGetter) noexcept
    {
        if (!atLeaf(key))
            return;
        found = true;
        out->kind = lua::ConfigValueKind::Float;
        out->floatValue = static_cast<double>(valueGetter());
    }
};

// ---- set ----

struct SetWalk {
    PathStack stack;
    const char* wantPath = nullptr;
    const lua::ConfigValue* value = nullptr;
    bool applied = false;

    void beginRoot() noexcept {}
    std::size_t endRoot() noexcept { return 0; }
    void beginObject(const char8_t* key) noexcept
    {
        char name[64];
        appendKeyName(name, sizeof(name), key);
        stack.push(name);
    }
    void endObject() noexcept { stack.pop(); }

    bool atLeaf(const char8_t* key) noexcept
    {
        if (applied || !wantPath || !value)
            return false;
        if (key) {
            char name[64];
            appendKeyName(name, sizeof(name), key);
            stack.push(name);
            const bool match = std::strcmp(stack.path, wantPath) == 0;
            stack.pop();
            return match;
        }
        return std::strcmp(stack.path, wantPath) == 0;
    }

    void boolean(const char8_t* key, auto&& valueSetter, auto&&) noexcept
    {
        if (!atLeaf(key))
            return;
        if (value->kind == lua::ConfigValueKind::Bool) {
            valueSetter(value->boolValue);
            applied = true;
        } else if (value->kind == lua::ConfigValueKind::Uint && value->uintValue <= 1) {
            valueSetter(value->uintValue != 0);
            applied = true;
        }
    }

    void uint(const char8_t* key, auto&& valueSetter, auto&& valueGetter) noexcept
    {
        if (!atLeaf(key))
            return;
        using GetterType = decltype(valueGetter());
        if constexpr (std::is_same_v<GetterType, color::Rgba>) {
            if (value->kind != lua::ConfigValueKind::Color)
                return;
            valueSetter(color::Rgba{static_cast<std::uint8_t>(value->color[0]),
                static_cast<std::uint8_t>(value->color[1]),
                static_cast<std::uint8_t>(value->color[2]),
                static_cast<std::uint8_t>(value->color[3])});
            applied = true;
        } else {
            if (value->kind != lua::ConfigValueKind::Uint)
                return;
            valueSetter(value->uintValue);
            applied = true;
        }
    }

    void floating(const char8_t* key, auto&& valueSetter, auto&&) noexcept
    {
        if (!atLeaf(key))
            return;
        if (value->kind != lua::ConfigValueKind::Float)
            return;
        valueSetter(static_cast<float>(value->floatValue));
        applied = true;
    }
};

template <typename HookContext>
int entryCount(HookContext& hookContext) noexcept
{
    CountWalk walk;
    ConfigSchema<HookContext> schema{hookContext};
    (void)schema.performConversion(walk);
    return walk.count;
}

template <typename HookContext>
bool entryAt(HookContext& hookContext, int index, char* outPath, int pathCap, int* outKind) noexcept
{
    if (!outPath || pathCap <= 0 || index < 0)
        return false;
    outPath[0] = '\0';
    ListWalk walk;
    walk.wantIndex = index;
    walk.outPath = outPath;
    walk.pathCap = pathCap;
    walk.outKind = outKind;
    ConfigSchema<HookContext> schema{hookContext};
    (void)schema.performConversion(walk);
    return walk.done && outPath[0] != '\0';
}

template <typename HookContext>
bool configGet(HookContext& hookContext, const char* path, lua::ConfigValue* out) noexcept
{
    if (!path || !out || path[0] == '\0')
        return false;
    GetWalk walk;
    walk.wantPath = path;
    walk.out = out;
    ConfigSchema<HookContext> schema{hookContext};
    (void)schema.performConversion(walk);
    return walk.found;
}

template <typename HookContext>
bool configSet(HookContext& hookContext, const char* path, const lua::ConfigValue* value) noexcept
{
    if (!path || !value || path[0] == '\0')
        return false;
    SetWalk walk;
    walk.wantPath = path;
    walk.value = value;
    ConfigSchema<HookContext> schema{hookContext};
    (void)schema.performConversion(walk);
    if (!walk.applied)
        return false;
    hookContext.config().saveActive(); // schedule the autosave, like any menu edit
    ui_config::changeEpoch.fetch_add(1, std::memory_order_relaxed); // menu dirty dot
    return true;
}

} // namespace lua_config_bridge
