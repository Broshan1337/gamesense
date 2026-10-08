#pragma once

#include <ctime>
#include <cstdio>
#include <cstdarg>

#include <Platform/Linux/LinuxPlatformApi.h>

#include <Utils/NsPaths.h>
#include <Utils/NsStr.h>












namespace gui_log
{

inline bool rotated{false};


inline void write(const char* fmt, ...) noexcept
{
    if (!rotated) {
        rotated = true;
        char rotFrom[ns_paths::kMaxPath];
        char rotTo[ns_paths::kMaxPath];
        if (ns_paths::joinLog(rotFrom, sizeof(rotFrom), "gamesense_gui.log")
            && ns_paths::joinLog(rotTo, sizeof(rotTo), "gamesense_gui.log.old"))
            (void)LinuxPlatformApi::rename(rotFrom, rotTo);
    }

    
    char logPath[ns_paths::kMaxPath];
    if (!ns_paths::joinLog(logPath, sizeof(logPath), "gamesense_gui.log"))
        return;
    const auto fd = LinuxPlatformApi::open(logPath, 0x441, 0644);
    if (fd < 0)
        return;

    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);

    char line[320];
    int length = std::snprintf(line, sizeof(line), "[%ld.%03ld] [P%d:T%d] ", ts.tv_sec, ts.tv_nsec / 1'000'000,
                               LinuxPlatformApi::processId(), LinuxPlatformApi::threadId());
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
