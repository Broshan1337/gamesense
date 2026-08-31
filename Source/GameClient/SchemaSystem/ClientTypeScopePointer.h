#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <link.h>

#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>

// Entity classes (C_EconEntity, C_BaseEntity, ...) live in client.dll's own per-module
// CSchemaSystemTypeScope, not the "!GlobalTypes" aggregate scope (see GlobalTypeScopePointer
// in SchemaSystemPatternTypes.h) - classes only get promoted to the global scope when
// explicitly marked MGlobalTypeScope, which most entity classes are not.
//
// HISTORY (2026-08-14): an earlier version of this file assumed InstallSchemaBindings's own
// 2nd parameter (cached into a persistent client.dll-local global at a fixed +0x40 LEA inside
// that function) WAS the module's CSchemaSystemTypeScope*. That crashed the game repeatedly
// (6 times across several hardening attempts - see project memory for the full history).
// Static analysis of InstallSchemaBindings's actual caller (decompiled via IDA) proved this
// premise wrong: InstallSchemaBindings is a generic per-interface registration callback every
// module implements, and libclient.so's implementation only reacts when the interface name is
// "SchemaSystem_001" - i.e. it grabs a handle to the *global CSchemaSystem interface itself*,
// not a per-module scope. The pointer at the cached global is a real, valid CSchemaSystem*
// (which is exactly why it kept passing opcode/vtable-range validation despite being useless
// for FindDeclaredClassOrEnum - it's a different, real object).
//
// THE ACTUAL MECHANISM (confirmed by decompiling a genuine call site in libclient.so at file
// vaddr ~0xcaa0b0, which loads this same cached CSchemaSystem*, reads its vtable, and calls
// slot 12 (byte offset 0x60) with a constant string argument decoded from the binary as
// literally "libclient.so" - the platform-native module name, matching cs2::CLIENT_DLL):
//   CSchemaSystemTypeScope* CSchemaSystem::FindTypeScopeForName(this, const char* name, size_t* outLen)
// Disassembly of that function (libschemasystem.so vaddr 0x43120) matches this signature: it
// hashes/bounds the name string, looks it up in an internal table at this+0x208, and on a hit
// returns array[foundIndex] from this+0x1f8 (an array of per-module scope pointers) - exactly
// the shape of a name-keyed scope registry, not a scope object itself.
struct ClientTypeScopePointer {
    ClientTypeScopePointer() noexcept
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

private:
    struct ModuleRange {
        std::uintptr_t base = 0;
        std::uintptr_t end = 0;

        [[nodiscard]] bool contains(const void* ptr) const noexcept
        {
            const auto addr = reinterpret_cast<std::uintptr_t>(ptr);
            return base != 0 && addr >= base && addr < end;
        }
    };

    [[nodiscard]] static ModuleRange getModuleRange(const char* nameSubstring) noexcept
    {
        struct Query {
            const char* nameSubstring;
            ModuleRange range;
        } query{nameSubstring, {}};

        dl_iterate_phdr([](dl_phdr_info* info, std::size_t, void* data) noexcept -> int {
            auto& query = *static_cast<Query*>(data);
            if (!info->dlpi_name || !std::strstr(info->dlpi_name, query.nameSubstring))
                return 0;
            for (int i = 0; i < info->dlpi_phnum; ++i) {
                const auto& phdr = info->dlpi_phdr[i];
                if (phdr.p_type != PT_LOAD)
                    continue;
                const auto segBase = info->dlpi_addr + phdr.p_vaddr;
                const auto segEnd = segBase + phdr.p_memsz;
                if (query.range.base == 0 || segBase < query.range.base)
                    query.range.base = segBase;
                if (segEnd > query.range.end)
                    query.range.end = segEnd;
            }
            return 0;
        }, &query);

        return query.range;
    }

    // Resolves the same cached CSchemaSystem* as before - this part was always correct, only
    // how the result gets used was wrong. See file header for the full explanation.
    [[nodiscard]] static void* resolveSchemaSystemInterface() noexcept
    {
        const auto installSchemaBindings = DynamicLibrary{cs2::CLIENT_DLL}.getFunctionAddress("InstallSchemaBindings").as<std::byte*>();
        if (!installSchemaBindings)
            return nullptr;

        constexpr std::ptrdiff_t kLeaOffset = 0x40;
        constexpr std::ptrdiff_t kLeaInstructionLength = 7;
        const auto leaInstruction = installSchemaBindings + kLeaOffset;

        const auto opcodeByte0 = static_cast<std::uint8_t>(leaInstruction[0]);
        const auto opcodeByte1 = static_cast<std::uint8_t>(leaInstruction[1]);
        const auto modrmByte = static_cast<std::uint8_t>(leaInstruction[2]);
        if (opcodeByte0 != 0x48 || opcodeByte1 != 0x8D || (modrmByte & 0xC7) != 0x05)
            return nullptr;

        std::int32_t displacement = 0;
        std::memcpy(&displacement, leaInstruction + 3, sizeof(displacement));
        const auto globalAddress = leaInstruction + kLeaInstructionLength + displacement;

        void* schemaSystem = nullptr;
        std::memcpy(&schemaSystem, globalAddress, sizeof(schemaSystem));
        if (!schemaSystem)
            return nullptr;

        // A real CSchemaSystem's vtable is compiled into libschemasystem.so.
        void* vtable = nullptr;
        std::memcpy(&vtable, schemaSystem, sizeof(vtable));
        if (!getModuleRange("libschemasystem.so").contains(vtable))
            return nullptr;

        return schemaSystem;
    }

    [[nodiscard]] static void* resolve() noexcept
    {
        void* const schemaSystem = resolveSchemaSystemInterface();
        if (!schemaSystem)
            return nullptr;

        void* vtable = nullptr;
        std::memcpy(&vtable, schemaSystem, sizeof(vtable));

        // FindTypeScopeForName is vtable slot 12 (byte offset 12*8 = 0x60), confirmed via
        // decompile of both this vtable slot's own implementation and a genuine call site.
        constexpr std::size_t kFindTypeScopeForNameSlotOffset = 0x60;
        void* findTypeScopeForName = nullptr;
        std::memcpy(&findTypeScopeForName, static_cast<std::byte*>(vtable) + kFindTypeScopeForNameSlotOffset, sizeof(findTypeScopeForName));
        if (!findTypeScopeForName)
            return nullptr;

        // The function pointer itself must live in libschemasystem.so's code before we call
        // through it - same defensive standard as everywhere else this session.
        if (!getModuleRange("libschemasystem.so").contains(findTypeScopeForName))
            return nullptr;

        using FindTypeScopeForNameFn = void*(*)(void* thisPtr, const char* name, void* outLen);
        const auto call = reinterpret_cast<FindTypeScopeForNameFn>(findTypeScopeForName);
        void* const typeScope = call(schemaSystem, cs2::CLIENT_DLL, nullptr);
        if (!typeScope)
            return nullptr;

        // Validate the returned CSchemaSystemTypeScope the same way the old (wrong) pointer
        // was validated, since that check is legitimate regardless of how the pointer was
        // obtained: a real scope's vtable lives in libschemasystem.so too.
        void* typeScopeVtable = nullptr;
        std::memcpy(&typeScopeVtable, typeScope, sizeof(typeScopeVtable));
        if (!getModuleRange("libschemasystem.so").contains(typeScopeVtable))
            return nullptr;

        return typeScope;
    }

    void* pointer{nullptr};
};
