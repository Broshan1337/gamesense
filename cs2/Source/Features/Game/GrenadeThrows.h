#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Vector.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/GameSceneNode.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <GameClient/Tracing/Tracing.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/UiConfig.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

// Saved grenade throws (2026-10-15, deadlocked's design): record your
// position + view angles + weapon while holding a live grenade; later, with
// the same grenade out near a saved spot, aim at the recorded angles so the
// throw lands where the recorded one did. Deadlocked's ui/grenades.rs keeps
// a per-map JSON list; this is the flat-file port (the feature_binds.txt
// pattern): one "<map> <defIndex> <x> <y> <z> <pitch> <yaw>" line per throw.
namespace grenade_throws
{

inline constexpr int kMaxThrows = 128;

struct Throw {
    char map[32];
    int weaponDefIndex;
    cs2::Vector position;
    float pitch;
    float yaw;
    bool valid;
};

inline Throw throws[kMaxThrows]{};
inline int throwCount = 0;

inline void defaultPath(char (&path)[512]) noexcept
{
    char dir[512]{};
    static_cast<void>(ui_config::withContext([&](auto&& hookContext) {
        const auto& directoryPath = hookContext.configState().pathToConfigDirectory;
        if (directoryPath) {
            for (std::size_t i = 0; i < sizeof(dir) - 1 && reinterpret_cast<const char*>(directoryPath.get())[i] != '\0'; ++i)
                dir[i] = reinterpret_cast<const char*>(directoryPath.get())[i];
        }
    }));
    if (!dir[0])
        return;
    std::size_t length = 0;
    while (dir[length] != '\0' && length + 1 < sizeof(path) - sizeof("/grenade_throws.txt"))
        ++length;
    if (dir[length] != '\0')
        return;
    std::memcpy(path, dir, length);
    std::memcpy(path + length, "/grenade_throws.txt", sizeof("/grenade_throws.txt"));
}

inline void load() noexcept
{
    char path[512]{};
    defaultPath(path);
    if (!path[0])
        return;
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0)
        return;

    char buffer[4096];
    const auto bytes = ::read(fd, buffer, sizeof(buffer) - 1);
    ::close(fd);
    if (bytes <= 0)
        return;
    buffer[bytes] = '\0';

    throwCount = 0;
    char* line = buffer;
    while (line && *line && throwCount < kMaxThrows) {
        char* next = std::strchr(line, '\n');
        if (next)
            *next = '\0';

        auto& entry = throws[throwCount];
        float x = 0.0f, y = 0.0f, z = 0.0f, pitch = 0.0f, yaw = 0.0f;
        int defIndex = 0;
        char map[32]{};
        if (std::sscanf(line, "%31s %d %f %f %f %f %f",
                map, &defIndex, &x, &y, &z, &pitch, &yaw) == 7) {
            std::memcpy(entry.map, map, sizeof(entry.map));
            entry.map[sizeof(entry.map) - 1] = '\0';
            entry.weaponDefIndex = defIndex;
            entry.position = cs2::Vector{x, y, z};
            entry.pitch = pitch;
            entry.yaw = yaw;
            entry.valid = true;
            ++throwCount;
        }
        line = next ? next + 1 : nullptr;
    }
}

inline void save() noexcept
{
    char path[512]{};
    defaultPath(path);
    if (!path[0])
        return;
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return;

    for (int i = 0; i < throwCount; ++i) {
        const auto& entry = throws[i];
        if (!entry.valid)
            continue;
        char line[192];
        const int length = std::snprintf(line, sizeof(line), "%s %d %.2f %.2f %.2f %.2f %.2f\n",
            entry.map, entry.weaponDefIndex,
            entry.position.x, entry.position.y, entry.position.z,
            entry.pitch, entry.yaw);
        if (length > 0)
            static_cast<void>(::write(fd, line, static_cast<std::size_t>(length)));
    }
    ::close(fd);
}

// Current map name (the Lua map-name buffer contract: set by the game-event
// path, empty before the first round).
inline char currentMap[32]{};

inline void setCurrentMap(const char* name) noexcept
{
    if (!name || !name[0])
        return;
    std::snprintf(currentMap, sizeof(currentMap), "%s", name);
}

inline void recordCurrent(const cs2::Vector& position, float pitch, float yaw, int weaponDefIndex) noexcept
{
    if (throwCount >= kMaxThrows || !currentMap[0])
        return;
    auto& entry = throws[throwCount];
    std::memcpy(entry.map, currentMap, sizeof(entry.map));
    entry.weaponDefIndex = weaponDefIndex;
    entry.position = position;
    entry.pitch = pitch;
    entry.yaw = yaw;
    entry.valid = true;
    ++throwCount;
    save();
}

inline void removeAt(int index) noexcept
{
    if (index < 0 || index >= throwCount)
        return;
    std::memmove(&throws[index], &throws[index + 1],
        static_cast<std::size_t>(throwCount - index - 1) * sizeof(Throw));
    --throwCount;
    save();
}

} // namespace grenade_throws