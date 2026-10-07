#include "Tf2Log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace ns_tf2 {

namespace {

int g_fd = -1;




char logPath[320] = "/tmp/gamesense_tf2.log";
bool logPathResolved = false;

void resolveLogPath()
{
    if (logPathResolved)
        return;
    logPathResolved = true;
    const char *home = ::getenv("HOME");
    if (!home || !home[0])
        return;
    char candidate[320];
    const int dirLen = snprintf(candidate, sizeof(candidate), "%s/OsirisCS2", home);
    if (dirLen <= 0 || size_t(dirLen) + 32 >= sizeof(candidate))
        return; 
    if (::mkdir(candidate, 0777) != 0 && errno != EEXIST)
        return;
    char logsDir[320];
    if (snprintf(logsDir, sizeof(logsDir), "%s/logs", candidate) <= 0)
        return;
    if (::mkdir(logsDir, 0777) != 0 && errno != EEXIST)
        return;
    if (snprintf(candidate, sizeof(candidate), "%s/gamesense_tf2.log", logsDir) <= 0)
        return;
    std::memcpy(logPath, candidate, sizeof(logPath));
}

void writeAll(const char *data, size_t len)
{
    while (len > 0) {
        const ssize_t written = ::write(g_fd, data, len);
        if (written <= 0)
            return;
        data += written;
        len -= size_t(written);
    }
}

} 

void logInit()
{
    if (g_fd >= 0)
        return;
    resolveLogPath();
    g_fd = ::open(logPath, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (g_fd < 0)
        return;
    char header[96];
    time_t now = ::time(nullptr);
    tm tmv{};
    localtime_r(&now, &tmv);
    const int len = snprintf(header, sizeof(header), "[%02d:%02d:%02d] tf2 module: log opened (pid %d)\n",
                             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, int(getpid()));
    if (len > 0)
        writeAll(header, size_t(len));
}

void log(const char *fmt, ...)
{
    if (g_fd < 0)
        return;

    char stackBuf[512];
    va_list args;
    va_start(args, fmt);
    const int len = vsnprintf(stackBuf, sizeof(stackBuf), fmt, args);
    va_end(args);
    if (len <= 0)
        return;

    time_t now = ::time(nullptr);
    tm tmv{};
    localtime_r(&now, &tmv);
    char stamped[560];
    int total = snprintf(stamped, sizeof(stamped), "[%02d:%02d:%02d] ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    if (total > 0 && size_t(total) < sizeof(stamped)) {
        const int copied = snprintf(stamped + total, sizeof(stamped) - size_t(total), "%s\n", stackBuf);
        if (copied > 0)
            writeAll(stamped, size_t(total + copied));
    }
}

void logShutdown()
{
    if (g_fd < 0)
        return;
    ::close(g_fd);
    g_fd = -1;
}

} 
