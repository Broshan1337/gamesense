#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/IEngineClient.h>
#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>

// Resolves the live CNetworkMessages singleton (the net-message factory) via the exported
// CreateInterface entry point of libnetworksystem.so - the same mechanism as
// EngineClientPointer.h, with both the CreateInterface symbol and the returned object's vtable
// validated against NETWORKSYSTEM_DLL.
//
// This factory owns every registered net message type; the ServerLagger walks its registry
// (find record by type id -> info -> protobuf binding) to create a genuine CCLCMsg_VoiceData
// instance, which is why the resolved object's vtable must be validated before any vtable slot
// is called through it. Failures are not cached: resolution can only fail while libnetworksystem
// is not yet loaded, which is self-correcting.
struct NetworkMessagesPointer {
    NetworkMessagesPointer() noexcept
        : pointer{cachedPointer ? cachedPointer : (cachedPointer = resolve())}
    {
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return pointer != nullptr;
    }

    [[nodiscard]] void* get() const noexcept
    {
        return pointer;
    }

    // Whether an address sits inside libnetworksystem's vmt range - used to validate the net
    // channel object's vtable before the lagger calls through it (CNetChan lives in the same
    // module as this factory). The range is cached once: getVmtSection() open()s and mmap()s
    // the module file, so it must never run per tick.
    [[nodiscard]] static bool vtableInNetworkSystemModule(std::uintptr_t vtable) noexcept
    {
        static std::uintptr_t cachedStart = 0;
        static std::uintptr_t cachedEnd = 0;
        if (cachedEnd == 0) {
            const DynamicLibrary networkSystem{cs2::NETWORKSYSTEM_DLL};
            const auto raw = networkSystem.getVmtSection().raw();
            if (!raw.empty() && raw.data()) {
                cachedStart = reinterpret_cast<std::uintptr_t>(raw.data());
                cachedEnd = cachedStart + raw.size();
            }
        }
        return cachedEnd != 0 && vtable >= cachedStart && vtable < cachedEnd;
    }

private:
    [[nodiscard]] static void* resolve() noexcept
    {
        const DynamicLibrary networkSystem{cs2::NETWORKSYSTEM_DLL};

        using CreateInterfaceFn = void* (*)(const char* name, int* returnCode);
        const auto createInterface = networkSystem.getFunctionAddress("CreateInterface").as<CreateInterfaceFn>();
        if (!createInterface)
            return nullptr;

        void* const messages = createInterface("NetworkMessagesVersion001", nullptr);
        if (!messages)
            return nullptr;

        // A real CNetworkMessages' vtable is compiled into libnetworksystem itself - same
        // fail-safe validation the other interface pointers in this project use.
        void* vtable = nullptr;
        std::memcpy(&vtable, messages, sizeof(vtable));
        if (!networkSystem.getVmtSection().contains(std::uintptr_t(vtable)))
            return nullptr;

        return messages;
    }

    // Constant-initialised, trivially destructible: no __cxa_guard / atexit registration, which
    // matters because this project links -nostdlib.
    inline static void* cachedPointer{nullptr};

    void* pointer{nullptr};
};