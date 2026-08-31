#pragma once

#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct LinuxPlatformApi {
    static void* dlopen(const char* file, int mode) noexcept;
    static void* dlsym(void* handle, const char* name) noexcept;
    static int dlclose(void* handle) noexcept;
    static int dlinfo(void* handle, int request, void* info) noexcept;
    static int dladdr(const void* address, Dl_info* info) noexcept;

    static int nanosleep(const struct timespec* duration, struct timespec* remaining) noexcept;
    static int pthreadCreate(pthread_t* thread, const pthread_attr_t* attributes, void* (*startRoutine)(void*), void* argument) noexcept;
    static int pthreadDetach(pthread_t thread) noexcept;

    static int open(const char* pathname, int flags, mode_t mode = 0777) noexcept;
    static int unlink(const char* pathname) noexcept;
    static int rename(const char* oldPath, const char* newPath) noexcept;
    static ssize_t pread(int fd, void* buf, size_t count, off_t offset) noexcept;
    static ssize_t write(int fd, const void* buf, size_t count) noexcept;
    static int close(int fd) noexcept;
    static int fstat(int fd, struct stat* buf) noexcept;

    // Directory listing (for the config file dropdown). readDir returns the entry's d_name, or
    // nullptr when the directory is exhausted ("." and ".." included - callers filter).
    static void* openDir(const char* pathname) noexcept;
    static const char* readDir(void* dirHandle) noexcept;
    static void closeDir(void* dirHandle) noexcept;

    static void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) noexcept;
    static int munmap(void* addr, size_t length) noexcept;
    static int mprotect(void* addr, size_t length, int prot) noexcept;

    static void debugBreak() noexcept;

    static char* getenv(const char* name) noexcept;
};
