#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>

// Resolves the live CNetworkGameClient (the game's net session) via the engine2 global that
// cs2-dumper records as dwNetworkGameClient (libengine2.so + 0xA2E380 on build 14181 - a raw
// RVA, so every use validates the object before anything is called through it: the object's
// own vtable must sit inside libengine2's vmt range and the tick field must read as a plausible
// tick count). The ServerLagger is the first consumer.
//
// The object is heap-allocated by the engine once a server connection exists and FREED on
// disconnect - which is why nothing here caches the client pointer: every construction re-reads
// the global and revalidates, so a stale object can never be dereferenced. Only the immutable
// module base + vmt range are cached once (getVmtSection() open()s and mmap()s the module file,
// so it must never run per tick).
//
// Layout facts verified against the live process + disassembly (updated 2026-10-06, build 11087116):
//   client tick field  = +0x3A8 (vtable slot 5 body: mov eax, [rdi+0x3A8]; was +0x388 pre-5GB-update)
//   channel array      = +0xF0, 24-byte stride, GetChannel(slot) = vtable slot 41
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

    // Fail-closed sanity on a live client object: its vtable must point back into libengine2's
    // vmt range (the same fail-safe the other interface pointers in this project use) and the
    // tick field must read as a plausible tick count (guards against a stale/garbage RVA after
    // a game update - a shifted .data qword would read some unrelated non-null value).
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
            reinterpret_cast<std::uintptr_t>(client) + 0x3A8);
        return tick >= 0 && tick < (1 << 24);
    }

    // Module base + vmt range, cached once (both are immutable after load).
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

    // libengine2.so build 14181 (2026-09-10 update, cs2-dumper output). RE-DERIVE on any CS2
    // update: valid() fails closed on drift instead of calling through garbage.
    static constexpr std::uintptr_t kNetworkGameClientRva = 0xA2E380;

    void* pointer{nullptr};
};