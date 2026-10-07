#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

#include <CS2/Classes/CSchemaSystem.h>
#include <GameClient/SchemaSystem/ClientTypeScopePointer.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <MemoryPatterns/PatternTypes/SchemaSystemPatternTypes.h>





template <typename HookContext>
class SchemaSystem {
public:
    explicit SchemaSystem(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    
    void dumpPatternStatus(FILE* outFile) const noexcept
    {
        const auto mapTime = hookContext.globalVars().curtime();
        std::fprintf(outFile, "curtime=%.1f (client scope gate opens at %.1f)\n", mapTime.hasValue() ? mapTime.value() : -1.0f, kMinMapTimeBeforeClientScope);
        std::fprintf(outFile, "GlobalTypeScopePointer=%p\n", hookContext.patternSearchResults().template get<GlobalTypeScopePointer>());
        std::fprintf(outFile, "PointerToSchemaFindDeclaredClassOrEnum=%p\n", reinterpret_cast<void*>(hookContext.patternSearchResults().template get<PointerToSchemaFindDeclaredClassOrEnum>()));
        std::fprintf(outFile, "PointerToSchemaBeginFieldIterator=%p\n", reinterpret_cast<void*>(hookContext.patternSearchResults().template get<PointerToSchemaBeginFieldIterator>()));
        std::fprintf(outFile, "PointerToSchemaFieldIteratorHasNext=%p\n", reinterpret_cast<void*>(hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorHasNext>()));
        std::fprintf(outFile, "PointerToSchemaFieldIteratorCurrent=%p\n", reinterpret_cast<void*>(hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorCurrent>()));
        std::fprintf(outFile, "PointerToSchemaFieldIteratorNext=%p\n", reinterpret_cast<void*>(hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorNext>()));
        std::fprintf(outFile, "PointerToSchemaFieldIteratorOffset=%p\n", reinterpret_cast<void*>(hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorOffset>()));

        dumpRawBytes(outFile, "GlobalTypeScopePointer target", hookContext.patternSearchResults().template get<GlobalTypeScopePointer>());
        dumpRawBytes(outFile, "FindDeclaredClassOrEnum code", reinterpret_cast<void*>(hookContext.patternSearchResults().template get<PointerToSchemaFindDeclaredClassOrEnum>()));

        std::fprintf(outFile, "\n=== !GlobalTypes scope ===\n");
        dumpScopeHashtable(outFile, hookContext.patternSearchResults().template get<GlobalTypeScopePointer>());

        
        
        
        
        
        
        
        
        
        std::fprintf(outFile, "\n=== candidate client.dll scope (NOT used for lookups, NOT dereferenced) ===\nvalue = %p\n", hookContext.clientTypeScope().get());
    }

    
    
    
    
    
    
    
    
    [[nodiscard]] static bool isRangeMapped(const void* address, std::size_t length) noexcept
    {
        if (!address)
            return false;
        FILE* maps = std::fopen("/proc/self/maps", "r");
        if (!maps)
            return false;
        const auto start = reinterpret_cast<std::uintptr_t>(address);
        const auto end = start + length;
        bool found = false;
        char line[512];
        while (std::fgets(line, sizeof(line), maps)) {
            unsigned long long rangeStart = 0, rangeEnd = 0;
            if (std::sscanf(line, "%llx-%llx", &rangeStart, &rangeEnd) == 2) {
                if (start >= rangeStart && end <= rangeEnd) {
                    found = true;
                    break;
                }
            }
        }
        std::fclose(maps);
        return found;
    }

    static void dumpRawBytes(FILE* outFile, const char* label, const void* address) noexcept
    {
        std::fprintf(outFile, "%s @ %p:", label, address);
        if (!address) {
            std::fprintf(outFile, " (null)\n");
            return;
        }
        if (!isRangeMapped(address, 24)) {
            std::fprintf(outFile, " (unmapped, skipped)\n");
            return;
        }
        const auto* bytes = static_cast<const unsigned char*>(address);
        for (int i = 0; i < 24; ++i)
            std::fprintf(outFile, " %02x", bytes[i]);
        std::fprintf(outFile, "\n");
    }

    
    
    
    
    
    
    
    static void dumpScopeHashtable(FILE* outFile, const void* scope) noexcept
    {
        if (!scope) {
            std::fprintf(outFile, "(null scope)\n");
            return;
        }
        if (!isRangeMapped(scope, 8)) {
            std::fprintf(outFile, "scope=%p (unmapped, skipped entirely)\n", scope);
            return;
        }
        const auto scopeBytes = static_cast<const std::byte*>(scope);
        void* vtable = nullptr;
        std::memcpy(&vtable, scopeBytes, sizeof(vtable));
        std::fprintf(outFile, "scope=%p vtable ptr = %p\n", scope, vtable);
        if (vtable && isRangeMapped(vtable, 0xd8 + 8)) {
            void* slot27 = nullptr;
            std::memcpy(&slot27, static_cast<const std::byte*>(vtable) + 0xd8, sizeof(slot27));
            std::fprintf(outFile, "scope vtable[0xd8] (devirt check target) = %p\n", slot27);
        } else if (vtable) {
            std::fprintf(outFile, "scope vtable ptr %p looks unmapped past +0xd8 - not a real vtable, skipping rest of this scope\n", vtable);
            return;
        }

        if (!isRangeMapped(scopeBytes + 0x108, 8) || !isRangeMapped(scopeBytes + 0x4e68, 256 * 9 * 8)) {
            std::fprintf(outFile, "scope+0x108 or scope+0x4e68..+0x9668 (hashtable) not fully mapped - this isn't a real CSchemaSystemTypeScope, skipping\n");
            return;
        }
        dumpRawBytes(outFile, "scope+0x4e68 (hashtable start)", scopeBytes + 0x4e68);
        dumpRawBytes(outFile, "scope+0x108 (fallback module ptr)", scopeBytes + 0x108);

        int populatedBuckets = 0;
        int totalNodes = 0;
        for (int bucket = 0; bucket < 256; ++bucket) {
            void* node = nullptr;
            std::memcpy(&node, scopeBytes + 0x4e68 + bucket * 9 * 8, sizeof(node));
            if (!node)
                continue;
            ++populatedBuckets;
            int nodesInBucket = 0;
            while (node && nodesInBucket < 64) {
                
                
                if (!isRangeMapped(node, 24)) {
                    std::fprintf(outFile, "bucket[%d] node=%p (unmapped mid-chain, stopping)\n", bucket, node);
                    break;
                }
                ++nodesInBucket;
                ++totalNodes;
                if (totalNodes <= 20) {
                    std::uint32_t storedHash = 0;
                    void* dataPtr = nullptr;
                    std::memcpy(&storedHash, node, sizeof(storedHash));
                    std::memcpy(&dataPtr, static_cast<std::byte*>(node) + 16, sizeof(dataPtr));
                    std::fprintf(outFile, "bucket[%d] node hash=%08x data=%p", bucket, storedHash, dataPtr);
                    if (dataPtr && isRangeMapped(dataPtr, 16)) {
                        
                        
                        void* maybeNamePtr = nullptr;
                        std::memcpy(&maybeNamePtr, static_cast<std::byte*>(dataPtr) + 8, sizeof(maybeNamePtr));
                        std::fprintf(outFile, " nameAt+8=%p", maybeNamePtr);
                        if (maybeNamePtr && isRangeMapped(maybeNamePtr, 63)) {
                            char nameBuf[64] = {};
                            std::memcpy(nameBuf, maybeNamePtr, sizeof(nameBuf) - 1);
                            std::fprintf(outFile, " \"%s\"", nameBuf);
                        }
                    }
                    std::fprintf(outFile, "\n");
                }
                void* next = nullptr;
                std::memcpy(&next, static_cast<std::byte*>(node) + 8, sizeof(next));
                node = next;
            }
        }
        std::fprintf(outFile, "populated buckets: %d / 256, total nodes seen: %d (printed up to 20)\n", populatedBuckets, totalNodes);
    }

    
    
    
    void dumpFields(const char* className, FILE* outFile) const noexcept
    {
        const auto classBinding = findDeclaredClassOrEnum(className);
        std::fprintf(outFile, "class %s: %s\n", className, classBinding ? "FOUND" : "NOT FOUND");
        if (!classBinding)
            return;

        
        
        
        
        
        dumpRawBytes(outFile, "classBinding raw bytes", classBinding);
        {
            void* possibleVtable = nullptr;
            std::memcpy(&possibleVtable, classBinding, sizeof(possibleVtable));
            std::fprintf(outFile, "classBinding+0 (possible vtable) = %p\n", possibleVtable);
            if (possibleVtable)
                dumpRawBytes(outFile, "classBinding vtable[0..2]", possibleVtable);
        }

        const auto beginIterator = hookContext.patternSearchResults().template get<PointerToSchemaBeginFieldIterator>();
        const auto hasNext = hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorHasNext>();
        const auto current = hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorCurrent>();
        const auto next = hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorNext>();
        const auto offset = hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorOffset>();
        if (!beginIterator || !hasNext || !current || !next || !offset) {
            std::fprintf(outFile, "  (iterator function pointers missing)\n");
            return;
        }

        alignas(8) std::array<std::byte, kIteratorStorageSize> iterator{};
        beginIterator(iterator.data(), classBinding, kFieldIterationKind);
        int count = 0;
        while (hasNext(iterator.data())) {
            const auto realName = currentFieldName(iterator.data());
            const auto field = static_cast<cs2::CSchemaClassFieldData*>(current(iterator.data()));
            const auto typeName = (field && field->name) ? field->name : "?";
            std::fprintf(outFile, "  [%d] %s : %s @ %d\n", count, realName ? realName : "(unnamed)", typeName, offset(iterator.data()));
            ++count;
            next(iterator.data());
        }
        std::fprintf(outFile, "  total fields: %d\n", count);
    }

    
    
    
    
    [[nodiscard]] const char* diagnoseFieldLookup(const char* className, const char* fieldName) const noexcept
    {
        const auto classBinding = findDeclaredClassOrEnum(className);
        if (!classBinding)
            return "classBinding NOT FOUND (scope walk broken)";
        const auto beginIterator = hookContext.patternSearchResults().template get<PointerToSchemaBeginFieldIterator>();
        const auto hasNext = hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorHasNext>();
        const auto current = hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorCurrent>();
        const auto next = hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorNext>();
        const auto offset = hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorOffset>();
        if (!beginIterator || !hasNext || !current || !next || !offset)
            return "iterator function pointers missing";
        alignas(8) std::array<std::byte, kIteratorStorageSize> iterator{};
        beginIterator(iterator.data(), classBinding, kFieldIterationKind);
        int seen = 0;
        while (hasNext(iterator.data())) {
            if (const auto realName = currentFieldName(iterator.data()); realName && std::strcmp(realName, fieldName) == 0)
                return "ok";
            ++seen;
            next(iterator.data());
        }
        static thread_local char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "field NOT FOUND (%d fields seen)", seen);
        return buffer;
    }

    [[nodiscard]] std::optional<std::int32_t> getFieldOffset(const char* className, const char* fieldName) const noexcept
    {
        const auto classBinding = findDeclaredClassOrEnum(className);
        const auto beginIterator = hookContext.patternSearchResults().template get<PointerToSchemaBeginFieldIterator>();
        const auto hasNext = hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorHasNext>();
        const auto current = hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorCurrent>();
        const auto next = hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorNext>();
        const auto offset = hookContext.patternSearchResults().template get<PointerToSchemaFieldIteratorOffset>();

        if (!classBinding || !beginIterator || !hasNext || !current || !next || !offset)
            return {};

        alignas(8) std::array<std::byte, kIteratorStorageSize> iterator{};
        beginIterator(iterator.data(), classBinding, kFieldIterationKind);

        while (hasNext(iterator.data())) {
            if (const auto realName = currentFieldName(iterator.data()); realName && std::strcmp(realName, fieldName) == 0)
                return offset(iterator.data());
            next(iterator.data());
        }
        return {};
    }

private:
    
    
    
    
    
    
    
    
    
    [[nodiscard]] static const char* currentFieldName(const void* iteratorData) noexcept
    {
        const auto* const bytes = static_cast<const std::byte*>(iteratorData);
        void* node = nullptr;
        std::memcpy(&node, bytes + 0xb0, sizeof(node));
        if (!node)
            return nullptr;
        const char* name = nullptr;
        std::memcpy(&name, node, sizeof(name));
        return name;
    }
    
    
    static constexpr std::int32_t kFieldIterationKind = 4;
    
    
    
    static constexpr std::size_t kIteratorStorageSize = 256;

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    static constexpr bool kUseClientScope = true;

    
    
    
    
    
    static constexpr float kMinMapTimeBeforeClientScope = schema_readiness::kMinMapTime;

    [[nodiscard]] void* findDeclaredClassOrEnum(const char* name) const noexcept
    {
        const auto find = hookContext.patternSearchResults().template get<PointerToSchemaFindDeclaredClassOrEnum>();
        if (!find)
            return nullptr;

        if constexpr (kUseClientScope) {
            const auto mapTime = hookContext.globalVars().curtime();
            if (mapTime.hasValue() && mapTime.value() >= kMinMapTimeBeforeClientScope) {
                if (const auto& clientScope = hookContext.clientTypeScope(); clientScope)
                    if (const auto result = find(clientScope.get(), name))
                        return result;
            }
        }

        if (const auto scope = hookContext.patternSearchResults().template get<GlobalTypeScopePointer>())
            return find(scope, name);

        return nullptr;
    }

    HookContext& hookContext;
};
