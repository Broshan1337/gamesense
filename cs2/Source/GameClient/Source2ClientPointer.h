#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CSource2Client.h>
#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>

// Resolves the live CSource2Client singleton via the exported CreateInterface(name, status)
// entry point - the same classic Source engine interface-registry mechanism confirmed working
// for the schema system (see ClientTypeScopePointer.h): CreateInterface walks a linked list of
// {vtable, name, next} nodes registered by the module, strcmp's the requested name, and calls
// the matched node's own factory function with no arguments. "Source2Client002" is
// CSource2Client's real registered interface name - found as a literal string, cross-confirmed
// via two separate per-module "interfaces I depend on" registration tables that both reference
// it by name (both otherwise-unrelated functions list the exact same 3-4 interface name
// strings, ruling out an incidental/unrelated match). Unlike ClientTypeScopePointer's target
// (a per-name-keyed scope lookup), a module's registered CreateInterface factories are
// lazily-constructed-once singletons - safe to resolve a single time and treat as stable for
// the process's whole lifetime, no need to re-resolve per frame.
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

        // A real CSource2Client's vtable is compiled into libclient.so itself.
        void* vtable = nullptr;
        std::memcpy(&vtable, source2Client, sizeof(vtable));
        if (!clientDLL.getVmtSection().contains(std::uintptr_t(vtable)))
            return nullptr;

        return static_cast<cs2::CSource2Client*>(source2Client);
    }

    cs2::CSource2Client* pointer{nullptr};
};
