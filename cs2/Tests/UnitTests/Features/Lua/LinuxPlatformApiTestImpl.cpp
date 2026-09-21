// Provides the LinuxPlatformApi methods the Lua framework's gui_log::write needs. The dlopen
// family is intentionally absent - SharedObjectTests.cpp mocks it; the game build gets the
// real definitions from dllmain.cpp including LinuxPlatformApiImpl.h.
#include <Platform/Linux/LinuxPlatformApi.h>

#include <cstdio>
#include <sys/syscall.h>
#include <unistd.h>

ssize_t LinuxPlatformApi::write(int fd, const void* buf, size_t count) noexcept
{
    return ::write(fd, buf, count);
}

int LinuxPlatformApi::rename(const char* oldPath, const char* newPath) noexcept
{
    return ::rename(oldPath, newPath);
}

int LinuxPlatformApi::processId() noexcept
{
    return ::getpid();
}

int LinuxPlatformApi::threadId() noexcept
{
    return static_cast<int>(::syscall(SYS_gettid));
}
