#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/IEngineClient.h>
#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>











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

        
        
        void* vtable = nullptr;
        std::memcpy(&vtable, messages, sizeof(vtable));
        if (!networkSystem.getVmtSection().contains(std::uintptr_t(vtable)))
            return nullptr;

        return messages;
    }

    
    
    inline static void* cachedPointer{nullptr};

    void* pointer{nullptr};
};