#pragma once

#include "Macros/IsPlatform.h"

#if IS_LINUX()

#include "Linux/LinuxSelfUnload.h"

using SelfUnload = LinuxSelfUnload;

#else







struct SelfUnload {
    static bool unmapSelf() noexcept
    {
        return false;
    }

    static void log(const char*) noexcept {}
};

#endif
