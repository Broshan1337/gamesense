#pragma once

#include "Macros/IsPlatform.h"

#if IS_LINUX()

#include "Linux/LinuxSelfUnload.h"

using SelfUnload = LinuxSelfUnload;

#else

// Windows keeps the previous behaviour: the unload command restores every hook and destroys the
// global context, but leaves the module mapped. The equivalent there is CreateThread on
// FreeLibraryAndExitThread, which is a genuinely different mechanism to the Linux one rather than a
// rename of it - and this Windows tree deliberately imports nothing, reaching the few APIs it uses
// through the PEB loader and direct syscalls, so it is not a change worth making untested. Left as
// an explicit no-op so the call site reads the same on both platforms.
struct SelfUnload {
    static bool unmapSelf() noexcept
    {
        return false;
    }

    static void log(const char*) noexcept {}
};

#endif
