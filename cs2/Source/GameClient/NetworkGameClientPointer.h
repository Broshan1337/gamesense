#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>
















struct NetworkGameClientPointer {
    NetworkGameClientPointer() noexcept
        : pointer{resolve()}
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

    
    
    
    
    [[nodiscard]] bool valid() const noexcept
    {
        return validate(pointer);
    }

private:
    [[nodiscard]] static bool validate(void* client) noexcept
    {
        if (!client)
            return false;
        void* vtable = nullptr;
        std::memcpy(&vtable, client, sizeof(vtable));
        if (!engineVmtContains(std::uintptr_t(vtable)))
            return false;
        const auto tick = *reinterpret_cast<const volatile std::int32_t*>(
            reinterpret_cast<std::uintptr_t>(client) + 0x388);
        return tick >= 0 && tick < (1 << 24);
    }

    
    [[nodiscard]] static bool engineVmtContains(std::uintptr_t address) noexcept
    {
        static std::uintptr_t cachedStart = 0;
        static std::uintptr_t cachedEnd = 0;
        if (cachedEnd == 0) {
            const DynamicLibrary engineDLL{cs2::ENGINE_DLL};
            const auto vmt = engineDLL.getVmtSection();
            const auto raw = vmt.raw();
            if (!raw.empty() && raw.data()) {
                cachedStart = reinterpret_cast<std::uintptr_t>(raw.data());
                cachedEnd = cachedStart + raw.size();
            }
        }
        return cachedEnd != 0 && address >= cachedStart && address < cachedEnd;
    }

    [[nodiscard]] static void* resolve() noexcept
    {
        const DynamicLibrary engineDLL{cs2::ENGINE_DLL};
        const auto linkMap = engineDLL.getLinkMap();
        if (!linkMap || !linkMap->l_addr)
            return nullptr;
        const auto base = reinterpret_cast<std::uintptr_t>(linkMap->l_addr);

        void* client = nullptr;
        std::memcpy(&client, reinterpret_cast<const void*>(base + kNetworkGameClientRva), sizeof(client));
        if (!validate(client))
            return nullptr;
        return client;
    }

    
    
    static constexpr std::uintptr_t kNetworkGameClientRva = 0xA2E380;

    void* pointer{nullptr};
};