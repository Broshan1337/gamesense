#pragma once

#include <dirent.h>
#include <dlfcn.h>
#include <link.h>
#include <signal.h>
#include <sys/mman.h>
#include <stdlib.h>

#include "LinuxPlatformApi.h"

void* LinuxPlatformApi::dlopen(const char* file, int mode) noexcept
{
    return ::dlopen(file, mode);
}

void* LinuxPlatformApi::dlsym(void* handle, const char* name) noexcept
{
    return ::dlsym(handle, name);
}

int LinuxPlatformApi::dlclose(void* handle) noexcept
{
    return ::dlclose(handle);
}

int LinuxPlatformApi::dlinfo(void* handle, int request, void* info) noexcept
{
    return ::dlinfo(handle, request, info);
}

int LinuxPlatformApi::dladdr(const void* address, Dl_info* info) noexcept
{
    return ::dladdr(address, info);
}

int LinuxPlatformApi::nanosleep(const struct timespec* duration, struct timespec* remaining) noexcept
{
    return ::nanosleep(duration, remaining);
}

int LinuxPlatformApi::pthreadCreate(pthread_t* thread, const pthread_attr_t* attributes, void* (*startRoutine)(void*), void* argument) noexcept
{
    return ::pthread_create(thread, attributes, startRoutine, argument);
}

int LinuxPlatformApi::pthreadDetach(pthread_t thread) noexcept
{
    return ::pthread_detach(thread);
}

int LinuxPlatformApi::open(const char* pathname, int flags, mode_t mode) noexcept
{
    return ::open(pathname, flags, mode);
}

ssize_t LinuxPlatformApi::pread(int fd, void* buf, size_t count, off_t offset) noexcept
{
    return ::pread(fd, buf, count, offset);
}

ssize_t LinuxPlatformApi::write(int fd, const void* buf, size_t count) noexcept
{
    return ::write(fd, buf, count);
}

int LinuxPlatformApi::unlink(const char* pathname) noexcept
{
    return ::unlink(pathname);
}

int LinuxPlatformApi::rename(const char* oldPath, const char* newPath) noexcept
{
    return ::rename(oldPath, newPath);
}

int LinuxPlatformApi::close(int fd) noexcept
{
    return ::close(fd);
}

int LinuxPlatformApi::fstat(int fd, struct stat* buf) noexcept
{
    return ::fstat(fd, buf);
}

void* LinuxPlatformApi::openDir(const char* pathname) noexcept
{
    return ::opendir(pathname);
}

const char* LinuxPlatformApi::readDir(void* dirHandle) noexcept
{
    if (const auto* entry = ::readdir(static_cast<DIR*>(dirHandle)))
        return entry->d_name;
    return nullptr;
}

void LinuxPlatformApi::closeDir(void* dirHandle) noexcept
{
    ::closedir(static_cast<DIR*>(dirHandle));
}

void* LinuxPlatformApi::mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) noexcept
{
    return ::mmap(addr, length, prot, flags, fd, offset);
}

int LinuxPlatformApi::munmap(void* addr, size_t length) noexcept
{
    return ::munmap(addr, length);
}

int LinuxPlatformApi::mprotect(void* addr, size_t length, int prot) noexcept
{
    return ::mprotect(addr, length, prot);
}

void LinuxPlatformApi::debugBreak() noexcept
{
    (void)raise(SIGTRAP);
}

char* LinuxPlatformApi::getenv(const char* name) noexcept
{
    return ::getenv(name);
}
