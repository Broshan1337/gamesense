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

// Resolves field byte offsets by (class name, field name) at runtime via CS2's own
// reflection system in libschemasystem.so, instead of a per-field signature scan.
// See PatternTypes/SchemaSystemPatternTypes.h for where the underlying function
// pointers come from.
template <typename HookContext>
class SchemaSystem {
public:
    explicit SchemaSystem(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // TEMPORARY diagnostic - reports whether the underlying byte-pattern-resolved function
    // pointers themselves are non-null, to tell "patterns didn't match this build at all"
    // apart from "patterns matched but this specific class/field lookup failed".
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

        // DISABLED (2026-08-14): even a mapped-range-checked read of the candidate client.dll
        // scope crashed the game (5th crash in a row tracing back to this one pointer) - most
        // likely a TOCTOU race, not a logic bug in the guard: this all happens during a map
        // load transition, exactly when the schema system is most likely to be asynchronously
        // tearing down/rebuilding scope objects on another thread, so "checked mapped, then
        // read" isn't actually safe here. Not touching this pointer's target memory in any way
        // from the live process anymore - only printing the raw numeric value, which requires
        // no dereference at all and cannot fault. Re-derive/verify this pointer via static
        // analysis of the on-disk binary (r2/IDA, no injection) before probing it live again.
        std::fprintf(outFile, "\n=== candidate client.dll scope (NOT used for lookups, NOT dereferenced) ===\nvalue = %p\n", hookContext.clientTypeScope().get());
    }

    // TEMPORARY diagnostic - reads /proc/self/maps (this process's own mappings - safe, no
    // ptrace/other-process access needed since we're injected in-process) to check a range is
    // actually mapped+readable before touching it. Learned the hard way: "the first N bytes of
    // a pointer read fine" does NOT imply a larger region past it is safe - walking off the end
    // of a smaller-than-assumed allocation into an unmapped page crashes just as hard as
    // dereferencing outright garbage. Every read of an *unverified* pointer (anything not
    // already known-good, like the byte-pattern-resolved GlobalTypeScopePointer) must go
    // through this first.
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

    // TEMPORARY diagnostic - inspects a candidate CSchemaSystemTypeScope's own vtable slot
    // (devirtualization check at [[scope]+0xd8], per FindDeclaredClassOrEnum's own logic) and
    // brute-force walks its embedded hashtable (scope+0x4e68, 256 buckets, 9 qwords/bucket -
    // matches the decompiled "9*bucketIndex+2509/2510" indexing). Every dereference of the
    // candidate `scope` pointer (and anything reached through it) is gated by isRangeMapped()
    // first - this pointer's validity is exactly what's being investigated, so nothing about
    // it can be assumed safe to touch.
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
                // Re-checked every iteration, not just the bucket head - a node reached via
                // the "next" chain is just as unverified as the head itself.
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
                        // Speculative: try reading a name string at data+8 (matches this
                        // codebase's CSchemaClassFieldData layout convention: name at +8).
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

    // TEMPORARY diagnostic - lists every field name+offset the iterator actually sees on a
    // class, so field-name drift (stale reference offsets) can be told apart from a broken
    // resolver. Remove once field names for the econ/attribute chain are confirmed.
    void dumpFields(const char* className, FILE* outFile) const noexcept
    {
        const auto classBinding = findDeclaredClassOrEnum(className);
        std::fprintf(outFile, "class %s: %s\n", className, classBinding ? "FOUND" : "NOT FOUND");
        if (!classBinding)
            return;

        // TEMPORARY - classBinding is now a confirmed-valid, safely-dereferenced pointer (the
        // scope resolution crash saga is over), so dumping its own raw bytes carries no new
        // risk. Checking whether offset 0 looks like a vtable pointer (the field-iterator
        // functions turned out to walk base classes, not members - see if classBinding has its
        // own virtual methods for a real field-by-name lookup instead).
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

    // One-shot health probe for the live schema-query path: walks the SAME chain as
    // getFieldOffset and reports the stage where it stops, so a silently-empty snapshot
    // (2026-10-04: player list / match state starved with zero visible errors) can be told
    // apart between "scope walk broken", "iterator fns missing" and "field-name drift".
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
    // The iterator's "current node" pointer (stored at iterator+0xB0, same field HasNext/
    // GetOffset read) has TWO independent string pointers on it: node+8 is the field's *type*
    // name (what Current() surfaces, by design meant for CSchemaClassFieldData-style type
    // introspection), and node+0 is the field's own declared identifier (e.g.
    // "m_AttributeManager") - confirmed via decompiling schema_detailed_class_layout's real
    // per-field name formatter (sub_75030 in that session's IDA numbering), which builds a
    // "ClassName::fieldName" display string via `**(iterator+0xB0)` - a double dereference of
    // the SAME node pointer Current() only single-dereferences. Reads directly out of our own
    // iterator buffer, no additional resolved function pointer needed.
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
    // Matches the kind value CSchemaSystem's own "schema_detailed_class_layout"
    // console command uses when iterating a class's fields.
    static constexpr std::int32_t kFieldIterationKind = 4;
    // The engine's field iterator is an opaque object accessed only through offsets up
    // to +0xB0/+0xB8 in the functions we call - this is a generously sized stack buffer
    // for it, not a modeled struct.
    static constexpr std::size_t kIteratorStorageSize = 256;

    // Entity classes live in client.dll's own per-module type scope, not the "!GlobalTypes"
    // aggregate (classes are only promoted there when explicitly marked MGlobalTypeScope,
    // confirmed via CS2's own "Promoting unresolved type '%s' to global scope" diagnostic
    // string - most entity classes aren't).
    //
    // DISABLED (2026-08-14): the client-scope path crashed the game 5 times in a row across
    // several hardening attempts (see project memory for the full history). Re-verified via
    // pure STATIC analysis (objdump against the on-disk libclient.so, no injection) that
    // ClientTypeScopePointer's `+0x40` offset assumption is byte-for-byte correct - that was
    // never the bug. Current theory: the crashes are a timing race, not a wrong pointer - our
    // render hook queries this scope on literally the first frame a weapon becomes valid, which
    // is right at/during map load, and the engine may still be asynchronously populating this
    // scope's hashtable on another thread at that exact moment (recall from earlier RE:
    // sub_1467b10 gates on async in-flight/pending-load counters, so this engine does have real
    // cross-thread load activity around this exact subsystem during level transitions). Even a
    // mapped-range-checked *read* crashed, which rules out "wrong/garbage pointer" as the sole
    // explanation and points at a genuine TOCTOU race instead.
    //
    // Mitigation: kMinMapTimeBeforeClientScope gates any use of the client scope behind enough
    // in-game time since map start for that population to have long since settled, using the
    // existing curtime() the codebase already exposes. Still leave kUseClientScope=false until
    // this has had one deliberate, informed live retest - flip it only for that specific test.
    static constexpr bool kUseClientScope = true;

    // Was 15.0f, on the TOCTOU theory described above. Round 8 disproved that theory and round 9
    // fixed the real cause (a CSchemaSystem* passed where a CSchemaSystemTypeScope* was wanted), so
    // the long wait was protecting against nothing. Now a small "is the client actually running
    // yet" gate, shared with the skin changer's own gate so the two cannot drift apart.
    // See SchemaReadiness.h.
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
