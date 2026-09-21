#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <GameClient/Bind.h>
#include <UI/ImGui/UiConfig.h>

// Right-click keybinds for toggleable features.
//
// Any toggle in the menu that is REGISTERED here can be right-clicked to open a small bind popup:
// capture a key (the same Bind encoding the keybind rows use), pick Toggle or Hold mode, unbind.
// The runtime application is the point: binds work with the menu CLOSED, every rendered frame -
//
//   Hold   - the feature's config var is forced ON while the key is down and restored on release
//            (restoring only what the bind itself changed, so a feature the user turned on
//            manually is not stomped),
//   Toggle - a key PRESS edge flips the config var.
//
// Features read their config vars every tick anyway (GET_CONFIG_VAR), so a bind is just a
// remote-controlled config write - no feature code changes, and the menu toggle reflects the
// bind state for free.
//
// Registry design: a fixed pool of entries, each identifying a config var by a stable
// compile-time id (FNV-1a over the pretty-function name of a template instantiated with the
// config var type - same binary = same id, which is what the sidecar file keys on). Everything
// (registration from the menu, popup edits, runtime application, the binds-list HUD read) runs
// on the Vulkan present thread inside the Neverlose render pass, so there is no locking.
// Persistence: <configDir>/feature_binds.txt, one "<id-hex> <key> <holdMode>" per line - the
// same sidecar pattern as radio_favorites.txt / killsay.txt, deliberately NOT the schema-based
// config (the config schema is an order-sensitive fixed-key format; a dynamic id-keyed map does
// not belong in it).
namespace feature_binds
{

struct Entry {
    std::uint64_t id{};        // stable per-config-var identity (idFor<ConfigVar>())
    const char* label{};       // display name (bind popup title + keybind list HUD)
    bool (*get)() = nullptr;   // type-erased ui_config::get<ConfigVar>()
    bool (*set)(bool) = nullptr; // type-erased ui_config::set<ConfigVar>()
    int key = Bind::kOff;
    bool holdMode = false;     // false = toggle mode
    bool lastKeyDown = false;  // edge-detection state for the applied bind
    bool holdRestore = false;  // value to restore when a hold ends
};

inline constexpr std::size_t kMaxEntries = 128;
inline Entry entries[kMaxEntries]{};
inline std::size_t entryCount = 0;
inline bool loadAttempted = false;

namespace detail {

// Stable identity for a config var type. __PRETTY_FUNCTION__ of a template instantiation
// embeds the full type name ("aimbot_vars::Enabled" etc.) and is compile-time constant with
// the same compiler/flags - exactly the stability the sidecar file keys on.
template <typename ConfigVar>
consteval std::uint64_t idFromString() noexcept
{
    const char* s = __PRETTY_FUNCTION__;
    std::uint64_t hash = 0xcbf29ce484222325ull;
    while (*s) {
        hash ^= static_cast<unsigned char>(*s++);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

}

template <typename ConfigVar>
[[nodiscard]] consteval std::uint64_t idFor() noexcept
{
    return detail::idFromString<ConfigVar>();
}

template <typename ConfigVar>
[[nodiscard]] bool typeGetter() noexcept
{
    return static_cast<bool>(ui_config::get<ConfigVar>());
}

template <typename ConfigVar>
bool typeSetter(bool value) noexcept
{
    return ui_config::set<ConfigVar>(typename ConfigVar::ValueType{value});
}

// Registers a bindable toggle. Called from the menu's once-per-session registration list;
// re-registration of the same var is a no-op (the stored key/mode survive menu re-renders).
template <typename ConfigVar>
void registerToggle(const char* label) noexcept
{
    const auto id = idFor<ConfigVar>();
    for (std::size_t i = 0; i < entryCount; ++i) {
        if (entries[i].id == id) {
            entries[i].label = label; // keep the label fresh from the menu source
            return;
        }
    }
    if (entryCount >= kMaxEntries)
        return;
    auto& entry = entries[entryCount++];
    entry.id = id;
    entry.label = label;
    entry.get = &typeGetter<ConfigVar>;
    entry.set = &typeSetter<ConfigVar>;
}

template <typename ConfigVar>
[[nodiscard]] Entry* entryFor() noexcept
{
    const auto id = idFor<ConfigVar>();
    for (std::size_t i = 0; i < entryCount; ++i) {
        if (entries[i].id == id)
            return &entries[i];
    }
    return nullptr;
}

[[nodiscard]] inline Entry* entryById(std::uint64_t id) noexcept
{
    for (std::size_t i = 0; i < entryCount; ++i) {
        if (entries[i].id == id)
            return &entries[i];
    }
    return nullptr;
}

// --- persistence (<configDir>/feature_binds.txt) ---------------------------------------

[[nodiscard]] inline bool filePath(char (&path)[512]) noexcept
{
    const char* dir = nullptr;
    static_cast<void>(ui_config::withContext([&](auto&& hookContext) {
        const auto& directoryPath = hookContext.configState().pathToConfigDirectory;
        if (directoryPath)
            dir = reinterpret_cast<const char*>(directoryPath.get());
    }));
    if (!dir)
        return false;

    std::size_t length = 0;
    while (dir[length] != '\0' && length + 1 < sizeof(path) - sizeof("/feature_binds.txt"))
        ++length;
    std::memcpy(path, dir, length);
    std::memcpy(path + length, "/feature_binds.txt", sizeof("/feature_binds.txt"));
    return true;
}

// "<id-hex> <key> <holdMode>" per line, '#' comments and blank lines skipped - matching the
// radio-favorites sidecar format. Manual hex/dec parsing: the file is fully controlled by us.
inline void load() noexcept
{
    char path[512];
    if (!filePath(path))
        return;

    const int fd = ::open(path, O_RDONLY);
    if (fd < 0)
        return; // no file yet - every bind simply stays Off

    char buffer[4096];
    const auto readBytes = ::pread(fd, buffer, sizeof(buffer) - 1, 0);
    ::close(fd);
    if (readBytes <= 0)
        return;
    buffer[readBytes] = '\0';

    std::size_t offset = 0;
    while (offset < static_cast<std::size_t>(readBytes)) {
        std::size_t lineLength = 0;
        while (buffer[offset + lineLength] != '\0' && buffer[offset + lineLength] != '\n')
            ++lineLength;
        const auto next = offset + lineLength + 1;

        if (lineLength > 0 && buffer[offset] != '#') {
            std::uint64_t id = 0;
            std::size_t i = 0;
            bool validId = lineLength > 2;
            while (i < lineLength && buffer[offset + i] != ' ') {
                const char c = buffer[offset + i];
                int digit = -1;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                if (digit < 0) { validId = false; break; }
                id = id * 16 + static_cast<std::uint64_t>(digit);
                ++i;
            }
            if (validId && i < lineLength) {
                int key = 0;
                bool holdMode = false;
                int fields = 0;
                ++i;
                while (i < lineLength && fields < 2) {
                    int value = 0;
                    bool valid = false;
                    while (i < lineLength && buffer[offset + i] != ' ') {
                        if (buffer[offset + i] < '0' || buffer[offset + i] > '9') { valid = false; break; }
                        value = value * 10 + (buffer[offset + i] - '0');
                        valid = true;
                        ++i;
                    }
                    if (!valid)
                        break;
                    if (fields == 0)
                        key = value;
                    else
                        holdMode = value != 0;
                    ++fields;
                    while (i < lineLength && buffer[offset + i] == ' ')
                        ++i;
                }
                if (fields == 2) {
                    if (auto* entry = entryById(id)) {
                        entry->key = key;
                        entry->holdMode = holdMode;
                        entry->lastKeyDown = false;
                    }
                }
            }
        }
        offset = next;
    }
}

inline void save() noexcept
{
    char path[512];
    if (!filePath(path))
        return;

    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;

    char line[64];
    for (std::size_t i = 0; i < entryCount; ++i) {
        if (entries[i].id == 0)
            continue;
        const int length = std::snprintf(line, sizeof(line), "%llx %d %d\n",
                                         static_cast<unsigned long long>(entries[i].id),
                                         entries[i].key, entries[i].holdMode ? 1 : 0);
        if (length > 0)
            static_cast<void>(::write(fd, line, static_cast<std::size_t>(length)));
    }
    ::close(fd);
}

// --- runtime application (every rendered frame, menu closed or not) ---------------------

inline void apply() noexcept
{
    if (!loadAttempted) {
        loadAttempted = true;
        load();
    }

    for (std::size_t i = 0; i < entryCount; ++i) {
        auto& entry = entries[i];
        if (entry.key == Bind::kOff || !entry.get || !entry.set) {
            entry.lastKeyDown = false;
            continue;
        }

        const bool down = Bind::isDown(entry.key);
        if (entry.holdMode) {
            if (down && !entry.lastKeyDown) {
                entry.holdRestore = entry.get();
                if (!entry.holdRestore)
                    entry.set(true);
            } else if (!down && entry.lastKeyDown) {
                // Restore only what the hold itself changed: a feature the user (or another
                // bind) enabled during the hold is left alone.
                if (!entry.holdRestore)
                    entry.set(false);
            }
        } else if (down && !entry.lastKeyDown) {
            entry.set(!entry.get());
        }
        entry.lastKeyDown = down;
    }
}

}
