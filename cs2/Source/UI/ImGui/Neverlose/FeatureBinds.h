#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <type_traits>
#include "FeatureBindState.h"
#include "FeatureBindRecord.h"

#include <fcntl.h>
#include <unistd.h>

#include <GameClient/Bind.h>
#include <Config/ConfigOverrideState.h>
#include <UI/ImGui/UiConfig.h>
#include <Utils/InRange.h>

namespace feature_binds
{

inline constexpr std::size_t kMaxEntries = 256;
inline Entry entries[kMaxEntries]{};
inline std::size_t entryCount = 0;
inline bool loadAttempted = false;

inline void restoreAll() noexcept
{
    for (std::size_t i = 0; i < entryCount; ++i)
        entries[i].restore();
}

template <typename ConfigVar>
void registerSavedBase(Entry& entry) noexcept
{
    constexpr auto index = ConfigVariableTypes::indexOf<ConfigVar>();
    static_assert(index < config_overrides::kCapacity);
    config_overrides::registerValue(index, &entry.active, &entry.restoreValue);
    config_overrides::restoreBeforeLoad = &restoreAll;
}

namespace detail {

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
[[nodiscard]] double typeGetter() noexcept
{
    return static_cast<double>(ui_config::get<ConfigVar>());
}

template <typename ConfigVar>
bool typeSetter(double value) noexcept
{
    return ui_config::set<ConfigVar>(typename ConfigVar::ValueType{value != 0.0});
}

template <typename ConfigVar>
void registerToggle(const char* label) noexcept
{
    const auto id = idFor<ConfigVar>();
    for (std::size_t i = 0; i < entryCount; ++i) {
        if (entries[i].id == id) {
            entries[i].label = label; 
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
    registerSavedBase<ConfigVar>(entry);
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

template <typename ConfigVar>
bool numericSetter(double value) noexcept
{
    using Range = typename ConfigVar::ValueType;
    if constexpr (IsRangeConstrained<Range>::value) {
        using Number = typename Range::ValueType;
        if (!std::isfinite(value))
            return false;
        value = std::clamp(value, static_cast<double>(Range::kMin), static_cast<double>(Range::kMax));
        if constexpr (std::is_integral_v<Number>)
            value = std::round(value);
        return ui_config::set<ConfigVar>(Range{static_cast<Number>(value)});
    } else {
        // Plain-type var (no InRange bounds): coerce straight to the value type.
        using Number = Range;
        if (!std::isfinite(value))
            return false;
        if constexpr (std::is_integral_v<Number>)
            value = std::round(value);
        return ui_config::set<ConfigVar>(static_cast<Number>(value));
    }
}

template < typename ConfigVar >
void registerNumber( const char* label ) noexcept
{
    if ( auto* existing = entryFor< ConfigVar >( ) ) {
        existing->label = label;
        return;
    }
    if ( entryCount >= kMaxEntries )
        return;
    using Range = typename ConfigVar::ValueType;
    auto& entry = entries[ entryCount++ ];
    entry.id = idFor< ConfigVar >( );
    entry.label = label;
    entry.get = &typeGetter< ConfigVar >;
    entry.set = &numericSetter< ConfigVar >;
    entry.numeric = true;
    // Range-constrained vars carry their own bounds; plain-type vars (e.g. the
    // skin changer's uint16 StatTrak/Wear/Seed) get the slider's own bounds.
    if constexpr ( IsRangeConstrained< Range >::value ) {
        entry.integral = std::is_integral_v< typename Range::ValueType >;
        entry.minimum = static_cast< double >( Range::kMin );
        entry.maximum = static_cast< double >( Range::kMax );
    } else {
        entry.integral = std::is_integral_v< Range >;
        entry.minimum = 0.0;
        entry.maximum = 0.0;
    }
    entry.boundValue = entry.get( );
    registerSavedBase< ConfigVar >( entry );
}

[[nodiscard]] inline Entry* entryById(std::uint64_t id) noexcept
{
    for (std::size_t i = 0; i < entryCount; ++i) {
        if (entries[i].id == id)
            return &entries[i];
    }
    return nullptr;
}

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
    if (dir[length] != '\0')
        return false;
    std::memcpy(path, dir, length);
    std::memcpy(path + length, "/feature_binds.txt", sizeof("/feature_binds.txt"));
    return true;
}

inline void load() noexcept
{
    char path[512];
    if (!filePath(path))
        return;

    const int fd = ::open(path, O_RDONLY);
    if (fd < 0)
        return; 

    char buffer[32768];
    const auto readBytes = ::pread(fd, buffer, sizeof(buffer) - 1, 0);
    ::close(fd);
    if (readBytes <= 0)
        return;
    buffer[readBytes] = '\0';

    char* line = buffer;
    while (line) {
        char* next = std::strchr(line, '\n');
        if (next)
            *next++ = '\0';
        Record record;
        if (parseRecord(line, record, Bind::kLast)) {
            if (auto* entry = entryById(record.id)) {
                entry->restore();
                entry->key = record.key;
                entry->holdMode = record.holdMode;
                if (record.hasValue)
                    entry->setBoundValue(record.value);
            }
        }
        line = next;
    }
}

inline void save() noexcept
{
    char path[512];
    if (!filePath(path))
        return;

    char temporary[528];
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    const int fd = ::open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return;

    bool complete = true;
    char line[96];
    for (std::size_t i = 0; i < entryCount && complete; ++i) {
        if (entries[i].id == 0)
            continue;
        const int length = std::snprintf(line, sizeof(line), "%llx %d %d %.17g\n",
            static_cast<unsigned long long>(entries[i].id), entries[i].key,
            entries[i].holdMode ? 1 : 0, entries[i].boundValue);
        if (length <= 0 || length >= static_cast<int>(sizeof(line))) {
            complete = false;
            break;
        }
        int written = 0;
        while (written < length) {
            const auto result = ::write(fd, line + written, static_cast<std::size_t>(length - written));
            if (result <= 0) {
                complete = false;
                break;
            }
            written += static_cast<int>(result);
        }
    }
    if (::close(fd) != 0)
        complete = false;
    if (!complete || LinuxPlatformApi::rename(temporary, path) != 0)
        static_cast<void>(LinuxPlatformApi::unlink(temporary));
}

inline void apply() noexcept
{
    if (!loadAttempted) {
        loadAttempted = true;
        load();
    }

    for (std::size_t i = 0; i < entryCount; ++i)
        entries[i].update(Bind::isDown(entries[i].key));
}

}
