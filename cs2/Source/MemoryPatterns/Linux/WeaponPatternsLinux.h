#pragma once

#include <MemoryPatterns/PatternTypes/WeaponPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct WeaponPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<OffsetToClipAmmo, CodePattern{"74 ? 8B 87 ? ? ? ? C3"}.add(4).read()>()
            .template addPattern<OffsetToWeaponSceneObjectUpdaterHandle, CodePattern{"48 89 83 ? ? ? ? BE ? ? ? ? 48 89 DF"}.add(3).read()>()
            // 2026-09-23 1.41.8.2: GetInaccuracy moved to 0x148EF40 (vtable slot 0x5C8) and
            // recompiled (r13=rdx, r12=rsi explicitly, stack 0x48); the leading E8 is the
            // interval-per-tick getter as before. Verified exactly-once.
            .template addPattern<PointerToGetInaccuracyFunction, CodePattern{"55 48 89 E5 41 57 41 56 41 55 49 89 D5 41 54 49 89 F4 53 48 89 FB 48 83 EC 48 E8"}>()
            // 2026-09-23 1.41.8.2: GetSpread moved to 0x1490590 (vtable slot 0x5E8) and
            // recompiled from the prologue+movsxd shape into a weapon-id dispatch
            // (movzx eax,[rdi+0x2838]; cmp 0x321 first). A byte-identical twin exists in the
            // same dispatch family, so the first cmp is load-bearing. Verified exactly-once.
            // GetSpread reads m_flSpread[weaponMode] from weapon VData. The former
            // item-definition switch was a void weapon action, not a float getter.
            .template addPattern<PointerToGetSpreadFunction, CodePattern{"48 63 87 90 28 00 00 48 8B 97 F8 04 00 00 83 F8 01 76 0D F3 0F 10 82 50 07 00 00"}>()
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
            // 2026-09-23 1.41.8.2: CalculateSpread moved to 0x1B49880 (found as the call that
            // follows the spread-seed fn at the fire path, 0x14A21AF seed -> 0x14A2254 cone)
            // and recompiled (r15=rdx, r13=r8, r12=rcx, ebx=edi, stack 0x458, args spilled to
            // rbp immediately). Verified exactly-once.
            .template addPattern<PointerToCalculateSpreadFunction, CodePattern{"55 48 89 E5 41 57 49 89 D7 41 56 41 55 4D 89 C5 41 54 49 89 CC 53 89 FB 48 81 EC 58 04 00 00 8B 45 40"}>()
            // sub_14537D0 - UpdateAccuracyPenalty (see WeaponPatternTypes.h). Anchors on the prologue
            // (push rbp / mov rbp,rsp / push r13 / push r12 / push rbx / mov rbx,rdi / sub rsp,0x18)
            // through `call <owner-getter> / test rax,rax / jz / mov rdi,rax / mov r12,rax / call`,
            // with the three relative call/jump displacements wildcarded. Verified exactly 1 match in
            // the current libclient.so via the IDA find_bytes tool.
            // 2026-09-23 1.41.8.2: UpdateAccuracyPenalty recompiled - r13 push dropped, the
            // stack frame shrank to 0x10 and the field offsets moved (m_fAccuracyPenalty
            // 0x2680 -> 0x28A8, m_flRecoilIndex 0x2690 -> 0x28B8, last-update 0x28AC - all
            // schema-confirmed, verified in the decay math at 0x14947C0..0x1494814). The
            // prologue + globals-call + test/je chain stays the anchor; verified exactly-once.
            .template addPattern<PointerToUpdateAccuracyPenaltyFunction, CodePattern{"55 48 89 E5 41 54 53 48 89 FB 48 83 EC 10 E8 ? ? ? ? 48 85 C0 0F 84"}>()
            // Read-only getter: interpolates predictable and unpredictable punch and adds them.
            .template addPattern<PointerToGetAimPunchFunction, CodePattern{"55 48 8D 4F 5C 48 89 E5 41 55 44 0F B6 EA 41 54 48 8D 57 50 49 89 F4 45 89 E9 53 4D 89 E0 48 89 FB"}>()
            // C_CSWeaponBase::RegenerateSkin(this /*rdi*/, bool forceHighRes /*sil*/) - the
            // PER-WEAPON fn. 2026-09-27 (build dce58989) CORRECTION of a latent 09-23
            // re-anchor bug (THE "skins broken" root): the 09-23 anchor followed the
            // regenerate_weapon_skins ConCommand thunk into the ConCommand HANDLER BODY - an
            // iterator taking only a bool in dil which regenerates EVERY weapon in the list
            // (it calls the per-weapon fn itself per element). Our regenerate(baseWeapon,
            // false) therefore ran that iterator since 09-23: the weapon pointer's LOW BYTE
            // was read as forceHighRes (heap addresses = almost always nonzero = the
            // "forceHighRes=true" death-SEGV class documented in BaseWeapon.h) and every skin
            // apply regenerated the whole weapon list. The per-weapon fn on dce58989 =
            // 0x1491950, verified by its own semantic chain (vdata getter 0xd8c540 ->
            // readiness virtuals [+0x4e8]/[+0xaf0] -> devirt check vtable+0xC50 ->
            // m_pSubclassVData [this+0x4f8] -> [+0x520] compare = the exact documented +1312
            // field) and by the game's own skin-update call sites (0x15d32b7/0x15d33e6).
            // Forged with pattern_forge.py forge --direct (literal prologue, exactly-once,
            // match == target; the fn is referenced only by call rel32, which the default
            // rip-ref forge does not see).
            // 2026-10-04: a rip-ref re-anchor briefly replaced this with a pattern landing
            // mid-function at 0x1466900 (calling it jumps into another fn's body); reverted
            // to the direct prologue after re-verifying the full semantic chain on the new
            // build (entry 0x1491950, vdata getter 0xd8c540, [+0x4e8]/[+0xaf0], vtable+0xC50,
            // [this+0x4f8] -> [+0x520]).
            .template addPattern<PointerToRegenerateWeaponSkin, CodePattern{"55 48 89 E5 41 57 41 56 49 89 FE 41 55 41 54 49 89 F4 53 48 81 EC 88 02 00 00"}>()
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
            // 2026-09-23 1.41.8.2: ResolveSubclassData moved to 0x1C660A0 (recompiled - r6d
            // args, stack 0x68, the vtable+0x618 GetSubclassID call at 0x1C6616B survives
            // inside). Verified exactly-once.
            .template addPattern<PointerToResolveSubclassData, CodePattern{"55 48 89 E5 41 57 41 56 41 89 D6 41 55 41 54 41 89 F4 53 48 89 FB 48 83 EC 68 E8"}>()
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
            // 2026-09-23 1.41.8.2: UpdateWeaponData moved to 0x14D78D0 - same prologue+tail
            // (cmp esi,1; je +0x18) as before; verified exactly-once.
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
