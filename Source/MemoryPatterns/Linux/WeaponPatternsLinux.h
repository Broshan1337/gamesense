#pragma once

#include <MemoryPatterns/PatternTypes/WeaponPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct WeaponPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<OffsetToClipAmmo, CodePattern{"74 ? 8B 87 ? ? ? ? C3"}.add(4).read()>()
            .template addPattern<OffsetToWeaponSceneObjectUpdaterHandle, CodePattern{"48 89 83 ? ? ? ? BE ? ? ? ? 48 89 DF"}.add(3).read()>()
            .template addPattern<PointerToGetInaccuracyFunction, CodePattern{"55 48 89 E5 41 57 41 56 49 89 ? 41 55 49 89 ? 41 54 53 48 89 FB 48 83 EC ? E8"}>()
            .template addPattern<PointerToGetSpreadFunction, CodePattern{"55 48 89 E5 48 83 EC ? 48 63"}>()
            // sub_1ADCAA0 - the per-shot spread SEED generator (see WeaponPatternTypes.h for the C
            // signature and RE trail). Anchors on the distinctive prologue
            // (push rbp / mov eax,edx / mov rbp,rsp / push rbx / sub rsp,0xD8) followed by
            // `movss xmm0,[rsi]` (loading the first angle) and two comiss-against-constant range
            // clamps; the RIP-relative displacements of those constant loads are wildcarded since
            // they move on every recompile. Verified exactly 1 match in the current libclient.so via
            // the IDA find_bytes tool (not a guessed prefix).
            .template addPattern<PointerToSpreadSeedFunction, CodePattern{"55 89 D0 48 89 E5 53 48 81 EC D8 00 00 00 F3 0F 10 06 0F 2F 05 ? ? ? ? F3 0F 10 1D ? ? ? ? 72 ? 0F 2F D8"}>()
            // sub_1ADCC00 - the per-shot bullet CONE generator (see WeaponPatternTypes.h). The
            // prologue's register moves are self-fingerprinting: mov r14d,ecx (seed) / mov r12d,edi
            // (itemDef) / mov rbx,r8 (&outX) / sub rsp,0x58 / mov [rbp],esi (numBullets) /
            // mov esi,-1 / mov [rbp],di (the int16 itemDef store), ending at the `lea rdi,[rip+...]`
            // whose displacement is wildcarded. Verified exactly 1 match in the current libclient.so
            // via the IDA find_bytes tool.
            .template addPattern<PointerToCalculateSpreadFunction, CodePattern{"55 48 89 E5 41 57 41 56 41 89 CE 41 55 41 54 41 89 FC 53 4C 89 C3 48 83 EC 58 89 75 A8 BE FF FF FF FF 66 89 7D A0 48 8D 3D ? ? ? ?"}>()
            // sub_14537D0 - UpdateAccuracyPenalty (see WeaponPatternTypes.h). Anchors on the prologue
            // (push rbp / mov rbp,rsp / push r13 / push r12 / push rbx / mov rbx,rdi / sub rsp,0x18)
            // through `call <owner-getter> / test rax,rax / jz / mov rdi,rax / mov r12,rax / call`,
            // with the three relative call/jump displacements wildcarded. Verified exactly 1 match in
            // the current libclient.so via the IDA find_bytes tool.
            .template addPattern<PointerToUpdateAccuracyPenaltyFunction, CodePattern{"55 48 89 E5 41 55 41 54 53 48 89 FB 48 83 EC 18 E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 89 C7 49 89 C4 E8"}>()
            // sub_14D31A0 - the aim-punch getter (see WeaponPatternTypes.h). Anchors on the prologue
            // (push rbp / mov rbp,rsp / push r14 / push r13 / push r12 / push rbx / mov rbx,rdi /
            // add rsp,-0x80) through the `cmp dword[rip+disp], -1` init-flag check, its jz, a call, and
            // `mov esi,[rbx+0x48]` (the read of m_predictableBaseTick that fingerprints this as the
            // aim-punch getter). RIP displacement, jz and call targets wildcarded. Verified exactly 1
            // match in the current libclient.so via the IDA find_bytes tool.
            .template addPattern<PointerToGetAimPunchFunction, CodePattern{"55 48 89 E5 41 56 41 55 41 54 53 48 89 FB 48 83 C4 80 83 3D ? ? ? ? FF 0F 84 ? ? ? ? E8 ? ? ? ? 8B 73 48"}>()
            // Prologue of the function bound to the "regenerate_weapon_skins" ConCommand
            // (confirmed via xref from the string + IDA decompilation, not signature-guessed).
            // Uniqueness across the whole client.so has not been exhaustively confirmed yet -
            // r2's /x search is too slow over this binary in this environment to finish a full
            // scan (same known limitation as two of the schema-system patterns).
            .template addPattern<PointerToRegenerateWeaponSkin, CodePattern{"55 48 89 E5 41 57 41 56 41 55 41 54 41 89 F4 BE FF FF FF FF 53 48 89 FB 48 8D 3D ? ? ? ? 48 81 EC 28 03 00 00 E8 ? ? ? ? 48 85 C0 0F 84"}>()
            // C_EconItemView::SetAttributeValueByName(this, const char* attributeName, float
            // value) - the real internal attribute-write path. Found via string xref to
            // "set item texture wear"/"prefab"/"seed" (the same attribute names the
            // Andromeda-CS2-Base reference uses), then confirmed by decompile: this is a thin
            // stub - it forwards to an update-existing-or-add-new-attribute routine on
            // &item->m_AttributeList (this+0x1110, byte-identical to this project's own
            // schema-resolved EconItemAttributeOffsets::attributeList offset - strong
            // independent confirmation this is the right function on the right object), then
            // unconditionally invalidates a cached object at this+0x1108 that a raw write into
            // m_Attributes never touches. The `48 8D BF 10 11 00 00` mid-pattern span (the
            // literal +0x1110 LEA) makes this pattern effectively self-fingerprinting - very
            // unlikely to collide with anything else in the binary.
            .template addPattern<PointerToSetAttributeValueByName, CodePattern{"55 48 89 E5 53 48 89 FB 48 8D BF 10 11 00 00 48 83 EC 08 E8 ? ? ? ? 48 89 DF 48 8B 5D F8 C9 E9 ? ? ? ?"}>()
            // sub_D9A070 - called from the entity subclass-resolution routine (sub_DDE400)
            // right after it computes/refreshes m_nSubclassID; resolves and caches the schema
            // subclass data for whatever index the entity currently reports. Found via string
            // xref to the "...requires %s subclass data, but it doesn't have any. DELETED."
            // error message, then confirmed by decompile: it reads the same field offset (1264)
            // that independently schema-resolves to m_nSubclassID (see EntitySubclassOffsets.h)
            // - strong independent confirmation this is the right function on the right object.
            // Verified unique across the whole binary via an independent Python byte scan.
            .template addPattern<PointerToResolveSubclassData, CodePattern{"55 48 89 E5 41 57 49 89 FF 41 56 41 55 41 54 53 48 81 EC 48 01 00 00 48 8B 07 FF 90 18 06 00 00 89 C3 41 8B"}>()
            // The real UpdateWeaponData(). Found via string xref to "Re-creating weapon hudmodel
            // due to vdata subclass model change." - see the type alias comment (C_CSWeaponBase.h)
            // for the full RE trail, including the later discovery (via a full decompile of the
            // whole function, not just the changeType==1 branch) that this unconditionally
            // tail-calls the real model-refresh logic regardless of changeType. Anchors on the
            // function's own entry point, so this pattern didn't need updating when that later
            // discovery only revealed more of the function body past what the pattern covers.
            // Verified unique across the whole binary via independent Python byte scan (a
            // shorter 16-byte prefix was NOT unique - 5 matches - had to extend to 24 bytes, a
            // clean instruction boundary right after the `cmp esi, 1 / je` that gates the
            // changeType==1-specific behavior).
            .template addPattern<PointerToUpdateWeaponData, CodePattern{"55 48 89 E5 41 55 41 54 41 89 F4 53 48 89 FB 48 83 EC 08 83 FE 01 74 18"}>()
            // The real UpdateSubclass() (sub_DDE400), confirmed via the user's own IDA
            // analysis. See the type alias comment (C_CSWeaponBase.h) for the full trail.
            // Verified unique via independent Python byte scan - a shorter prefix collided with
            // a sibling function sharing the same prologue shape; had to extend to include the
            // literal call-target bytes at the end (not wildcarded, unlike most other patterns
            // here, specifically because wildcarding them reintroduced the collision with that
            // sibling - the two functions differ only in which function they call at this spot).
            // sub_DDE400 -> 0xDE4D00 as of the 2026-08-25 update. See the type alias comment
            // (C_CSWeaponBase.h) for the full trail.
            // Verified unique via offline Python byte scan. The 2026-08-25 update produced TWO
            // semantically-identical copies of this function (0xDE4D00 and 0xDE5040) that differ
            // ONLY in their relocatable disp32s - no body byte disambiguates them anymore (the
            // old literal-call-byte trick died with that update). They also still share the
            // prologue shape with an unrelated sibling at 0x1691990. The anchor is now the
            // 0xCC padding BEFORE the function: this copy is preceded by >= 8 bytes of padding,
            // its twin by only 4. Pattern = 8x CC + prologue + first call (disp wildcarded) +
            // `mov rdi,rbx` + second call's opcode byte, which excludes the 0x1691990 sibling
            // (its post-call bytes differ). Semantics of both twins re-verified by disassembly:
            // identity bit-0x10 gate at [rax+30h], ResolveSubclassData call when open (now
            // 0xDA0970), m_pSubclassVData read at [rbx+4F8], tail dispatch through vtable+30.
            //
            // CRITICAL: the match lands at the START of the 0xCC padding run, NOT on the
            // function - .add(8) skips the anchor bytes so the stored pointer is the real
            // entry point. Without it the resolved "function" is int3 padding and calling
            // it SIGTRAPs the game (crash 2026-08-25: PC at libclient+0xDE4CF9/0xDE4CF8,
            // via SkinChanger -> BaseWeapon::updateSubclass on every map load).
            .template addPattern<PointerToUpdateSubclass, CodePattern{"CC CC CC CC CC CC CC CC 55 48 89 E5 41 57 41 56 41 55 41 54 49 89 F4 53 48 89 FB 48 81 EC A8 00 00 00 E8 ? ? ? ? 48 89 DF E8"}.add(8)>()
            // sub_1FD2EA0 - see the type alias comment (WeaponPatternTypes.h) for what it does
            // and the full RE trail. The "lazy singleton getter" prologue shape is shared by ~10
            // sibling accessors, so it must be disambiguated. The ORIGINAL fix kept the embedded
            // RIP-relative displacement (this accessor's own singleton global) literal - but that
            // displacement changes on every game recompile, and the 2026-08-20 update duly broke
            // it. Disambiguate on the function BODY instead: the bytes after `test rax,rax` are
            // this function's own (null-checks then `movzx esi,[rbx+0x10C2]` = the item-def index,
            // then a tail call), and are unique with the displacement wildcarded. Verified 1 match
            // in the new binary via Python byte scan; body unchanged across the update, only the
            // displacement moved.
            .template addPattern<PointerToGetItemDefinitionByIndex, CodePattern{"55 48 89 E5 41 54 53 48 89 FB 48 83 EC 10 48 8B 05 ? ? ? ? 48 85 C0 74 26 48 8B 78 08 48 85 FF 74 75 0F B7 B3 C2 10 00 00"}>()
            // sub_15D4A50 - candidate SetModel(). Found via a hardcoded-literal-path caller
            // (sub_142D250, passes "models/inventory_items/dogtags.vmdl"). See the type alias
            // comment (C_CSWeaponBase.h) for the full trail and the HexRays-rendering caveat.
            // The original pattern kept the `lea rax,[rip+disp]` displacement literal; that
            // displacement moved in the 2026-08-20 update and broke it. Disambiguate on the body
            // instead - wildcard the lea displacement and extend through `mov rdi,[rax] /
            // mov rax,[rdi] / call [rax+0x68]`, which is unique (1 match) with the displacement
            // wildcarded. Verified against the new binary via Python byte scan.
            .template addPattern<PointerToSetModel, CodePattern{"55 48 89 E5 53 48 89 FB 48 83 EC 08 48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 50 68"}>()
            // sub_1FD4AE0 - see the type alias comment (WeaponPatternTypes.h) for what it does
            // and the full RE trail. Generated and confirmed unique against the whole binary
            // directly by the IDA Pro MCP signature tool (make_signature_for_function), not a
            // manual scan this time - the two RIP-relative-displacement operands (the vtable
            // call offset and the paint-kit singleton global) are wildcarded, matching this
            // project's usual convention of only keeping literal bytes when disambiguation
            // specifically requires it.
            .template addPattern<PointerToGetPaintKitDefinition, CodePattern{"55 48 89 E5 53 48 83 EC 18 48 8B 07 FF 50 ? 89 C6 48 8B 05 ? ? ? ? 48 85 C0 74 ? 48 8B 78 ? 48 8B 5D"}>()
            // sub_40F1590 - the real UpdateCompositeMaterial. See the type alias comment
            // (C_CSWeaponBase.h) for what it does and the full RE trail, including why the
            // game's own regenerate_weapon_skins ConCommand handler is the evidence that this
            // is the correct call to pair with RegenerateSkin. Address re-verified in a fresh
            // IDA session before generating this (not trusted from an older session's notes).
            // Generated by the IDA Pro MCP signature tool (make_signature_for_function) and
            // independently re-verified unique via a separate find_bytes call - exactly 1 match,
            // at 0x40f1590. No wildcards were needed: the prologue's register-save sequence plus
            // the `89 75` tail is already unique without any relocatable operands in it.
            .template addPattern<PointerToUpdateCompositeMaterial, CodePattern{"55 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 53 48 83 EC 28 89 75"}>();
    }
};
