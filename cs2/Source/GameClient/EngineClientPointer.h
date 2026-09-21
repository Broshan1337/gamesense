#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/IEngineClient.h>
#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>

// Resolves the live CEngineClient singleton via the exported CreateInterface entry point - the
// same mechanism as GameEventManagerPointer.h and Source2ClientPointer.h, with one difference
// that matters: this interface lives in libengine2.so, not libclient.so, so both the module the
// CreateInterface symbol is taken from AND the module the returned object's vtable is validated
// against are ENGINE_DLL.
// Unlike the other interface pointers in this project, this one is resolved from a HOT path -
// once per hit registered, inside game-event dispatch on the main thread. resolve() is far too
// expensive to run there: getVmtSection() open()s libengine2.so, mmap()s the whole file, and
// linearly strcmp()s every ELF section header before unmapping it again. Doing that per hit was
// the cause of the hit sound being audibly late and intermittently dropping entirely.
//
// The interface is a process-lifetime singleton, so it's cached after the first successful
// resolve. Failures are deliberately NOT cached: the only way resolve() can fail is being called
// before libengine2.so is loaded, which is self-correcting, and retrying costs nothing because a
// hit cannot be registered before the engine exists in the first place.
struct EngineClientPointer {
    EngineClientPointer() noexcept
        : pointer{cachedPointer ? cachedPointer : (cachedPointer = resolve())}
    {
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return pointer != nullptr;
    }

    [[nodiscard]] cs2::IEngineClient* get() const noexcept
    {
        return pointer;
    }

private:
    [[nodiscard]] static cs2::IEngineClient* resolve() noexcept
    {
        const DynamicLibrary engineDLL{cs2::ENGINE_DLL};

        using CreateInterfaceFn = void*(*)(const char* name, int* returnCode);
        const auto createInterface = engineDLL.getFunctionAddress("CreateInterface").as<CreateInterfaceFn>();
        if (!createInterface)
            return nullptr;

        void* const engineClient = createInterface("Source2EngineToClient001", nullptr);
        if (!engineClient)
            return nullptr;

        // A real CEngineClient's vtable is compiled into libengine2.so itself - same fail-safe
        // validation the other interface pointers in this project use. Catches a wrong or
        // renamed interface returning something unexpected before we ever call through it.
        void* vtable = nullptr;
        std::memcpy(&vtable, engineClient, sizeof(vtable));
        if (!engineDLL.getVmtSection().contains(std::uintptr_t(vtable)))
            return nullptr;

        return static_cast<cs2::IEngineClient*>(engineClient);
    }

    // Constant-initialised, trivially destructible: no __cxa_guard / atexit registration, which
    // matters because this project links -nostdlib.
    inline static cs2::IEngineClient* cachedPointer{nullptr};

    cs2::IEngineClient* pointer{nullptr};
};
