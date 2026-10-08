#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <link.h>

#include <CS2/Constants/DllNames.h>
#include <Platform/DynamicLibrary.h>



























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

        
        
        constexpr std::size_t kFindTypeScopeForNameSlotOffset = 0x60;
        void* findTypeScopeForName = nullptr;
        std::memcpy(&findTypeScopeForName, static_cast<std::byte*>(vtable) + kFindTypeScopeForNameSlotOffset, sizeof(findTypeScopeForName));
        if (!findTypeScopeForName)
            return nullptr;

        
        
        if (!getModuleRange("libschemasystem.so").contains(findTypeScopeForName))
            return nullptr;

        using FindTypeScopeForNameFn = void*(*)(void* thisPtr, const char* name, void* outLen);
        const auto call = reinterpret_cast<FindTypeScopeForNameFn>(findTypeScopeForName);
        void* const typeScope = call(schemaSystem, cs2::CLIENT_DLL, nullptr);
        if (!typeScope)
            return nullptr;

        
        
        
        void* typeScopeVtable = nullptr;
        std::memcpy(&typeScopeVtable, typeScope, sizeof(typeScopeVtable));
        if (!getModuleRange("libschemasystem.so").contains(typeScopeVtable))
            return nullptr;

        return typeScope;
    }

    void* pointer{nullptr};
};
