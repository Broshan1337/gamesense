#pragma once

#include <ctime>
#include <cstdio>
#include <cstdarg>

#include <Platform/Linux/LinuxPlatformApi.h>

// Append-only diagnostics file for the ImGui UI bootstrap (same pattern as
// LinuxSelfUnload::log): /tmp/gamesense_gui.log. The engine console is not always ready when
// the early menu/Vulkan-hook stages run, and StatusReport only dumps once - this file answers
// "how far did the chain get" from outside the game. Best-effort: open+write+close per line,
// failures ignored. Temporary verification scaffolding for the UI migration.
namespace gui_log
{

inline void write(const char* fmt, ...) noexcept
{
    // O_WRONLY | O_APPEND | O_CREAT, 0644 - numeric because fcntl flags under -nostdlib.
    const auto fd = LinuxPlatformApi::open("/tmp/gamesense_gui.log", 0x441, 0644);
    if (fd < 0)
        return;

    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);

    char line[320];
    int length = std::snprintf(line, sizeof(line), "[%ld.%03ld] ", ts.tv_sec, ts.tv_nsec / 1'000'000);
    if (length > 0 && length < static_cast<int>(sizeof(line))) {
        va_list args;
        va_start(args, fmt);
        const int written = std::vsnprintf(line + length, sizeof(line) - length, fmt, args);
        va_end(args);
        if (written > 0)
            length += written;
        if (length < static_cast<int>(sizeof(line)) - 1) {
            line[length++] = '\n';
            LinuxPlatformApi::write(fd, line, static_cast<std::size_t>(length));
        }
    }

    LinuxPlatformApi::close(fd);
}

}
