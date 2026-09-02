#pragma once

#include <cstddef>

#include <dlfcn.h>
#include <pthread.h>
#include <time.h>

#include "LinuxPlatformApi.h"

// Unmaps this library from the game process, so that injecting it again into the SAME running game
// works.
//
// Without this step the unload command tore down everything EXCEPT the library itself: hooks were
// restored and the global context destroyed, but the .so stayed mapped with the injector's
// reference still held. A second injection then did nothing at all, and not because anything was
// corrupted - dlopen() finds the library already loaded, bumps its reference count and hands back
// the existing handle WITHOUT re-running .init_array. That constructor is this library's only entry
// point (there is no exported init symbol and no FINI_ARRAY), so nothing ran and the game looked
// untouched. Matching is by SONAME, which is baked into the ELF as `libMangoHud.so`, so copying the
// file somewhere else under a different name does not sidestep it either.
//
// Unmapping a library from inside itself is only safe if two things hold, and both are load-bearing
// here:
//
//   1. Nothing of ours may be RUNNING when the unmap happens. The unload is triggered from inside
//      our own render hook, so the unmap is handed to a worker thread that waits first. By then the
//      render thread has long since returned into the game, and any scene-object-updater job that
//      happened to be in flight on a worker thread has finished. Every hook is restored before this
//      is called, so no game thread can enter our code again in the meantime.
//
//   2. Nothing of ours may run AFTER the unmap. dlclose() is the last thing that can ever execute
//      on our behalf: the instruction following the call would be fetched from memory dlclose has
//      just unmapped, which is an immediate SIGSEGV rather than a subtle bug. So the worker's final
//      act is a guaranteed tail call - the compiler emits our epilogue and then a bare `jmp`, which
//      leaves the stack exactly as glibc's start_thread set it up, so dlclose returns straight into
//      start_thread and not one byte of ours is ever touched again.
//
// [[gnu::musttail]] is what makes rule 2 enforceable instead of hopeful: if the tail call is ever
// lost - by adding cleanup after it, by a signature change, by building without optimisation - it
// is a compile error, not a crash at unload time in front of the user.
class LinuxSelfUnload {
public:
    // Append-only diagnostics for the unload path. /tmp is shared between the Steam runtime
    // container and the host shell, so failures here are visible from outside the game. Every
    // stage of the unload logs; if the library ever stays mapped, this file says which stage
    // failed (or that the worker never ran at all). Best-effort: failures to log are ignored.
    static void log(const char* message) noexcept
    {
        // O_WRONLY | O_APPEND | O_CREAT, 0644 - numeric because fcntl flags under -nostdlib.
        const auto fd = LinuxPlatformApi::open("/tmp/libMangoHud_unload.log", 0x441, 0644);
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

    // Schedules the unmap and returns immediately. The caller is expected to return out of our code
    // promptly, which it does - this is called at the very end of the render hook.
    //
    // Returns false if the unmap could not be scheduled, in which case the library simply stays
    // resident with all its hooks removed, which is exactly the old behaviour. Nothing is left in a
    // half-torn-down state either way.
    static bool unmapSelf() noexcept
    {
        log("unmap: entered");
        // dladdr on one of our own functions gives the library's path exactly as the loader
        // recorded it, so this works whatever the injector called the file or wherever it put it.
        Dl_info info{};
        if (LinuxPlatformApi::dladdr(reinterpret_cast<const void*>(&unloadWorker), &info) == 0 || info.dli_fname == nullptr) {
            log("unmap: FAILED - dladdr could not resolve our own module");
            return false;
        }

        // RTLD_NOLOAD turns this into "give me a handle to the already-loaded copy" rather than a
        // load. It still takes a reference, which is deliberate: it is held until the very end so
        // that nothing else can drop the count to zero and unmap us early, and the worker gives it
        // back as its second-to-last act.
        void* const selfHandle = LinuxPlatformApi::dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_NOW);
        if (selfHandle == nullptr) {
            log("unmap: FAILED - RTLD_NOLOAD dlopen returned null");
            return false;
        }

        // The tail call MUST land in libc's own dlclose. Going through LinuxPlatformApi::dlclose
        // would tail-jump into our wrapper, which then calls libc and returns into itself - into
        // the memory that was just unmapped. Asking the dynamic linker for the symbol guarantees
        // the real implementation rather than any stub living inside this library.
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

        // Detached because nobody can ever join it: by the time it finishes, the code that would
        // have done the joining no longer exists.
        LinuxPlatformApi::pthreadDetach(worker);
        log("unmap: worker scheduled, unmapping in 500ms");
        return true;
    }

private:
    // Typed to return void* rather than dlclose's actual int purely so the tail call's return type
    // matches this thread routine's. Both come back in rax and the value is discarded - the thread
    // is detached, so its result is never collected.
    using RawDlclose = void* (*)(void*) noexcept;

    static void* unloadWorker(void* selfHandle) noexcept
    {
        log("worker: entered, sleeping 500ms");

        const timespec delay{0, kUnmapDelayNanoseconds};
        LinuxPlatformApi::nanosleep(&delay, nullptr);

        // Give back the reference unmapSelf() took. The injector still holds its own, so this does
        // not unmap anything and it is safe to keep executing afterwards.
        log("worker: own reference dropped");
        LinuxPlatformApi::dlclose(selfHandle);

        // ...and this drops the last one. Nothing may follow it. See rule 2 above.
        log("worker: dropping the injector reference - unmap happens inside this call");
        [[gnu::musttail]] return rawDlclose(selfHandle);
    }

    // Half a second: around thirty frames at 60fps, so the render thread has left our hook by an
    // enormous margin, while still being far too short for anyone to notice the unload lagging.
    static constexpr long kUnmapDelayNanoseconds = 500 * 1000 * 1000;

    // Constant-initialised and trivially destructible, so no __cxa_guard under -nostdlib. Set
    // before the worker is created, and only ever read by it.
    inline static RawDlclose rawDlclose{nullptr};
};
