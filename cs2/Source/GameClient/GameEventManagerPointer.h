#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/IGameEventManager2.h>
#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>

// Resolves the live CGameEventManager singleton (behind the IGameEventManager2 interface) via
// the exported CreateInterface(name, status) entry point - same mechanism as
// Source2ClientPointer.h. "GAMEEVENTSMANAGER002" is the classic, still-present Source engine
// interface name for this exact system, confirmed as a real string in this binary.
struct GameEventManagerPointer {
    GameEventManagerPointer() noexcept
        : pointer{resolve()}
    {
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return pointer != nullptr;
    }

    [[nodiscard]] cs2::IGameEventManager2* get() const noexcept
    {
        return pointer;
    }

private:
    [[nodiscard]] static cs2::IGameEventManager2* resolve() noexcept
    {
        const DynamicLibrary clientDLL{cs2::CLIENT_DLL};

        using CreateInterfaceFn = void*(*)(const char* name, int* returnCode);
        const auto createInterface = clientDLL.getFunctionAddress("CreateInterface").as<CreateInterfaceFn>();
        if (!createInterface)
            return nullptr;

        void* const gameEventManager = createInterface("GAMEEVENTSMANAGER002", nullptr);
        if (!gameEventManager)
            return nullptr;

        // A real CGameEventManager's vtable is compiled into libclient.so itself.
        void* vtable = nullptr;
        std::memcpy(&vtable, gameEventManager, sizeof(vtable));
        if (!clientDLL.getVmtSection().contains(std::uintptr_t(vtable)))
            return nullptr;

        return static_cast<cs2::IGameEventManager2*>(gameEventManager);
    }

    cs2::IGameEventManager2* pointer{nullptr};
};
