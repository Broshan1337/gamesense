#pragma once

#include <MemoryPatterns/PatternTypes/SchemaSystemPatternTypes.h>
#include <MemorySearch/CodePattern.h>

// Signatures below were derived from a manual disassembly / decompilation pass against
// libschemasystem.so (see commit message / PR description for the reverse engineering
// notes). They target internal, non-exported functions, so unlike most patterns in this
// codebase they cannot be cross-checked against a symbol name - re-verify against a real
// build before trusting them blindly on an update.
struct SchemaSystemPatterns {
    [[nodiscard]] static consteval auto addSchemaSystemPatterns(auto schemaSystemPatterns) noexcept
    {
        return schemaSystemPatterns
            // Anchored on CSchemaSystem's "find binding for console command" helper, which loads
            // the address of the always-present global CSchemaSystemTypeScope via a RIP-relative LEA.
            // Extended past the original 30-byte prologue-only signature (2026-08-13 CS2 update
            // introduced at least one other function sharing that same generic 26-byte prologue
            // shape elsewhere in libschemasystem.so, causing a false-positive collision) to include
            // the immediately following call + branch + second (redundant) LEA of the same global,
            // which is unique to this function specifically - confirmed by tracing forward from the
            // still-valid PointerToSchemaFindDeclaredClassOrEnum pattern's only caller.
            .template addPattern<GlobalTypeScopePointer, CodePattern{"55 48 89 E5 41 57 41 56 49 89 F6 41 55 49 89 FD 41 54 53 48 83 EC 18 48 8D 3D ? ? ? ? 48 89 55 C8 E8 ? ? ? ? 48 85 C0 0F 85 ? ? ? ? 48 8D 3D ? ? ? ? 4C"}.add(26).abs()>()
            // bool/void* FindDeclaredClassOrEnum(CSchemaSystemTypeScope* scope, const char* name)
            //
            // 2026-08-20 update: the original 30-byte prologue-only signature now matches TWO
            // byte-identical sibling functions in libschemasystem.so - FindDeclaredClass (0x3ba40)
            // and FindDeclaredEnum (0x3bd40), 0x300 apart, each called once by a common dispatcher
            // (0x64430) that tries class-then-enum against the global scope. The twins are
            // identical for their first 0x31 bytes and first diverge at the call displacement into
            // their SHARED strlen helper (DC for class, D9 for enum) - the same class of collision
            // as BeginFieldIterator below. Extended with exact bytes through that divergence byte:
            // the linear scan then finds FindDeclaredClass first AND unambiguously, which is the
            // one this codebase needs (getFieldOffset/dumpFields only ever pass class names and
            // feed the result to BeginFieldIterator, documented as taking CSchemaClassBinding).
            // The two `je` displacements stay wildcarded - they are position-dependent. If a future
            // update moves the shared helper by >= 0x100 the DC anchor shifts and this pattern
            // degrades to not-found (logged), never to a wrong-function match.
            // 2026-09-23 update (1.41.8.2): exactly the case the note above predicted - the
            // shared strlen helper moved again, so the E8-disp anchor bytes rotated
            // (DC -> D9 low byte at twin +0x30..0x31; enum twin now D6). Same fix shape:
            // extended exact bytes through the first divergence between the recompiled
            // twins (verified 0x31 identical bytes, class twin calls 0x39180 = strlen
            // helper, enum twin disp low byte D6). The two `je` displacements stay
            // wildcarded - position-dependent.
            .template addPattern<PointerToSchemaFindDeclaredClassOrEnum, CodePattern{"55 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 53 48 83 EC 08 48 85 F6 0F 84 ? ? ? ? 45 31 E4 80 3E 00 48 89 F3 0F 84 ? ? ? ? 48 89 F7 E8 CC D9"}>()
            // void BeginFieldIterator(FieldIterator* outIter, CSchemaClassBinding* classBinding, int32_t kind)
            //
            // The original 31-byte version of this pattern (just the shared "movabs rax,0x80...08;
            // push rbp; ..." prologue) matched TWO byte-identical functions in libschemasystem.so:
            // this one, and a base-class-hierarchy walker sitting ~0x160 bytes earlier that happens
            // to share the exact same prologue shape (same class of collision as the
            // GlobalTypeScopePointer/FindDeclaredClassOrEnum duplicate-function issues elsewhere in
            // this project - see project memory). Our linear-scan matcher found the wrong (earlier)
            // one every time, which meant HasNext/Current/Next/GetOffset were always being fed an
            // iterator that BeginFieldIterator itself never actually populated - specifically, the
            // wrong function never writes iterator+0xB0 (which HasNext reads), so iteration always
            // silently reported zero fields, for every class, regardless of kind. Confirmed via a
            // byte-for-byte diff between the two candidates: identical for the first 51 bytes, then
            // diverge at a `jne` displacement (the two functions' jump targets differ since they're
            // at different addresses) - extended the pattern past that point (exact bytes, no
            // wildcard) to uniquely anchor on the real one.
            .template addPattern<PointerToSchemaBeginFieldIterator, CodePattern{"48 B8 08 00 00 00 00 00 00 80 55 48 89 E5 41 56 4C 8D 77 18 41 55 49 89 F5 41 54 41 89 D4 53 48 89 FB 48 C7 47 08 00 00 00 00 48 89 47 10 83 E7 07 0F 85 71 01 00 00 66 0F EF C0 4C"}>()
            // bool FieldIterator::HasNext()
            .template addPattern<PointerToSchemaFieldIteratorHasNext, CodePattern{"48 83 BF B0 00 00 00 00 0F 95 C0 C3"}>()
            // SchemaFieldData* FieldIterator::Current()
            .template addPattern<PointerToSchemaFieldIteratorCurrent, CodePattern{"48 8B 87 B0 00 00 00 48 85 C0 74 04 48 8B 40 08 C3"}>()
            // void FieldIterator::Next()
            .template addPattern<PointerToSchemaFieldIteratorNext, CodePattern{"48 83 BF B0 00 00 00 00 48 89 F8 8B 97 A8 00 00 00 74 3D 85 D2 78 3D 8B"}>()
            // int32_t FieldIterator::GetOffset()
            .template addPattern<PointerToSchemaFieldIteratorOffset, CodePattern{"48 8B 97 B0 00 00 00 8B 87 AC 00 00 00 03 42 10 C3"}>();
    }
};
