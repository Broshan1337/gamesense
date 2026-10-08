#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CSource2Client.h>
#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>













struct Source2ClientPointer {
    Source2ClientPointer() noexcept
        : pointer{resolve()}
    {
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return pointer != nullptr;
    }

    [[nodiscard]] cs2::CSource2Client* get() const noexcept
    {
        return pointer;
    }

private:
    [[nodiscard]] static cs2::CSource2Client* resolve() noexcept
    {
        const DynamicLibrary clientDLL{cs2::CLIENT_DLL};

        using CreateInterfaceFn = void*(*)(const char* name, int* returnCode);
        const auto createInterface = clientDLL.getFunctionAddress("CreateInterface").as<CreateInterfaceFn>();
        if (!createInterface)
            return nullptr;

        void* const source2Client = createInterface("Source2Client002", nullptr);
        if (!source2Client)
            return nullptr;

        
        void* vtable = nullptr;
        std::memcpy(&vtable, source2Client, sizeof(vtable));
        if (!clientDLL.getVmtSection().contains(std::uintptr_t(vtable)))
            return nullptr;

        return static_cast<cs2::CSource2Client*>(source2Client);
    }

    cs2::CSource2Client* pointer{nullptr};
};
