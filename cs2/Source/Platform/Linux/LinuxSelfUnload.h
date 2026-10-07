#pragma once

#include <cstddef>

#include <dlfcn.h>
#include <pthread.h>
#include <time.h>

#include "LinuxPlatformApi.h"

#include <Utils/NsPaths.h>
































class LinuxSelfUnload {
public:
    
    
    
    
    static void log(const char* message) noexcept
    {
        
        char logPath[ns_paths::kMaxPath];
        if (!ns_paths::joinLog(logPath, sizeof(logPath), "libMangoHud_unload.log"))
            return;
        const auto fd = LinuxPlatformApi::open(logPath, 0x441, 0644);
        if (fd < 0)
            return;
        std::size_t length = 0;
        while (message[length] != '\0')
            ++length;
        if (length > 0)
            LinuxPlatformApi::write(fd, message, length);
        LinuxPlatformApi::write(fd, "\n", 1);
        LinuxPlatformApi::close(fd);
    }

    
    
    
    
    
    
    static bool unmapSelf() noexcept
    {
        log("unmap: entered");
        
        
        Dl_info info{};
        if (LinuxPlatformApi::dladdr(reinterpret_cast<const void*>(&unloadWorker), &info) == 0 || info.dli_fname == nullptr) {
            log("unmap: FAILED - dladdr could not resolve our own module");
            return false;
        }

        
        
        
        
        void* const selfHandle = LinuxPlatformApi::dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_NOW);
        if (selfHandle == nullptr) {
            log("unmap: FAILED - RTLD_NOLOAD dlopen returned null");
            return false;
        }

        
        
        
        
        rawDlclose = reinterpret_cast<RawDlclose>(LinuxPlatformApi::dlsym(RTLD_DEFAULT, "dlclose"));
        if (rawDlclose == nullptr) {
            log("unmap: FAILED - dlsym could not resolve libc dlclose");
            LinuxPlatformApi::dlclose(selfHandle);
            return false;
        }

        pthread_t worker{};
        if (LinuxPlatformApi::pthreadCreate(&worker, nullptr, &unloadWorker, selfHandle) != 0) {
            log("unmap: FAILED - pthread_create for the unmap worker failed");
            LinuxPlatformApi::dlclose(selfHandle);
            return false;
        }

        
        
        LinuxPlatformApi::pthreadDetach(worker);
        log("unmap: worker scheduled, unmapping in 500ms");
        return true;
    }

private:
    
    
    
    using RawDlclose = void* (*)(void*) noexcept;

    static void* unloadWorker(void* selfHandle) noexcept
    {
        log("worker: entered, sleeping 500ms");

        const timespec delay{0, kUnmapDelayNanoseconds};
        LinuxPlatformApi::nanosleep(&delay, nullptr);

        
        
        log("worker: own reference dropped");
        LinuxPlatformApi::dlclose(selfHandle);

        
        log("worker: dropping the injector reference - unmap happens inside this call");
        
        
        
        
        
        
#if defined(__clang__)
        [[clang::musttail]] return rawDlclose(selfHandle);
#else
        [[gnu::musttail]] return rawDlclose(selfHandle);
#endif
    }

    
    
    static constexpr long kUnmapDelayNanoseconds = 500 * 1000 * 1000;

    
    
    inline static RawDlclose rawDlclose{nullptr};
};
