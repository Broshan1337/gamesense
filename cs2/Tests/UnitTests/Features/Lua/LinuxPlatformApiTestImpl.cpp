


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

#include <dirent.h>
ssize_t LinuxPlatformApi::pread(int fd, void* buf, size_t count, off_t offset) noexcept { return ::pread(fd, buf, count, offset); }
int LinuxPlatformApi::unlink(const char* path) noexcept { return ::unlink(path); }
void* LinuxPlatformApi::openDir(const char* path) noexcept { return ::opendir(path); }
const char* LinuxPlatformApi::readDir(void* dir) noexcept {
    const auto* entry = ::readdir(static_cast<DIR*>(dir)); return entry ? entry->d_name : nullptr;
}
void LinuxPlatformApi::closeDir(void* dir) noexcept { ::closedir(static_cast<DIR*>(dir)); }
