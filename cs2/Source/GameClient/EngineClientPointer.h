#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/IEngineClient.h>
#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>


















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

        
        
        
        void* vtable = nullptr;
        std::memcpy(&vtable, engineClient, sizeof(vtable));
        if (!engineDLL.getVmtSection().contains(std::uintptr_t(vtable)))
            return nullptr;

        return static_cast<cs2::IEngineClient*>(engineClient);
    }

    
    
    inline static cs2::IEngineClient* cachedPointer{nullptr};

    cs2::IEngineClient* pointer{nullptr};
};
