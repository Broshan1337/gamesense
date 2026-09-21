#pragma once

#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct ClientPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<MainMenuPanelPointer, CodePattern{"E5 53 48 83 EC ? 48 8D 1D ? ? ? ? 48 8B 03 48 8B 78 ? 48 8B"}.add(9).abs()>()
            .template addPattern<HudPanelPointer, CodePattern{"05 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 40"}.add(1).abs()>()
            .template addPattern<GlobalVarsPointer, CodePattern{"8D ? ? ? ? ? 48 89 35 ? ? ? ? 48 89 ? ? C3"}.add(9).abs()>()
            .template addPattern<TransformTranslate3dVMT, CodePattern{"48 8D 0D ? ? ? ? 48 89 08 48 89 50 08 48 8B 53 10"}.add(3).abs()>()
            .template addPattern<TransformScale3dVMT, CodePattern{"48 8B 53 08 48 8D 0D ? ? ? ? F3 0F 10 43"}.add(7).abs()>()
            .template addPattern<WorldToProjectionMatrixPointer, CodePattern{"01 4C 8D 05 ? ? ? ? 4C 89 EE"}.add(4).abs()>()
            .template addPattern<ViewToProjectionMatrixPointer, CodePattern{"EE 48 8D 0D ? ? ? ? 48 8D 15 ? ? ? ? 48"}.add(4).abs()>()
            .template addPattern<ViewRenderPointer, CodePattern{"48 8D 05 ? ? ? ? 48 89 38 48 85"}.add(3).abs()>()
            .template addPattern<LocalPlayerControllerPointer, CodePattern{"48 83 3D ? ? ? ? ? 0F 95 C0 C3"}.add(3).abs(5)>()
            // Resolves the client's g_pGameEventManager global (0x4806390 as of the 2026-08-25
            // update, was 0x47E1F90 after 2026-08-20, qword_47E1990 before that) out of the
            // multi-event listener-registration function that loads it with `lea r13,[rip+disp]`
            // at +0x3C and immediately starts registering listeners through its vtable+0x20
            // ("round_start" among them).
            // NOTE: this function used to be a small single-listener shape (pre-2026-08-25 it was
            // matched via a `xor ecx,ecx` prologue twin pair), and before that it deliberately
            // matched TWO byte-identical functions. The 2026-08-25 update recompiled it into a
            // larger multi-listener registrar with shuffled register allocation, so the pattern
            // was re-derived from scratch: prologue + body fingerprints (struct-relative lea
            // displacements like [r12+0xB28] survive relocation), BOTH rip-relative disp32s AND
            // the internal call disp32 wildcarded, extended past the second lea through the
            // listener-count stores. add(63).abs() lands on the lea r13 disp32 -> the global.
            // Verified exactly-once via offline byte scan against the 2026-08-25 libclient.so;
            // resolves into .bss at 0x4806390.
            .template addPattern<GameEventManagerGlobalPointer, CodePattern{"55 48 89 E5 41 55 41 54 53 48 89 FB 48 83 EC 18 4C 8D 25 ? ? ? ? 49 8D 8C 24 28 0B 00 00 49 8D 84 24 58 0B 00 00 66 48 0F 6E C1 66 48 0F 3A 22 C0 01 0F 29 45 D0 E8 ? ? ? ? 4C 8D 2D ? ? ? ? 31 C9 C6 83 88 13 00 00 00 49 8D 44 24 10 66 0F 6F 45 D0 48 C7 83 8C 13 00 00 00 00 00"}.add(63).abs()>()
            // Chat printing. ChatPrint (sub_1FFDF30) is matched on its own prologue.
            //
            // The delegate global (qword_48AA498) is taken from its tiny getter, sub_1FFDDA0,
            // which is just `mov rax, [rip+disp]; ret`. Those 8 bytes are far too generic to
            // match on alone - the binary is full of identical getters - so the pattern deliberately
            // runs past the function into its 0xCC padding AND the next function's prologue, which
            // is what makes it unique. add(3) lands on the disp32 and abs() resolves it: verified
            // by hand, 0x1FFDDA7 (end of the instruction) + 0x028AC6F1 = 0x48AA498.
            .template addPattern<ChatPrintFunction, CodePattern{"55 48 89 E5 41 55 49 89 D5 41 54 41 89 F4 53 48 81 EC D8 10 00 00"}>()
            // ChangeTeammateColor's C++ handler (0x1DA7AB0 on the 2026-09-10 libclient.so).
            // Fingerprint = prologue + the static convar-ref setup (`lea rbx,[rip+...]`,
            // `movzx eax,[rip+...]` guard byte, test/je) + the two
            // `mov esi,-1 / mov rdi,rbx / call <convar-ref probe>` rounds and the
            // `mov r12d,[rax]` that loads the current cl_color before the third call.
            // All rip-relative disp32s and both call rel32s are wildcarded, so this survives
            // relocations; the esi=-1 chain is load-bearing - cutting after the je half matches
            // a second site (0x199A8A0), adding the second call round brings it to exactly one
            // (byte-scan verified against the 2026-09-10 build).
            .template addPattern<ChangeTeammateColorCycle, CodePattern{"55 48 89 E5 41 54 53 48 8D 1D ? ? ? ? 48 83 EC 10 0F B6 05 ? ? ? ? 84 C0 0F 84 ? ? ? ? BE FF FF FF FF 48 89 DF E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? BE FF FF FF FF 48 89 DF 44 8B 20 E8"}>()
            .template addPattern<HudChatDelegatePointer, CodePattern{"48 8B 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 55 48 89 E5 41 56"}.add(3).abs()>()
            // Recovers the CCSGOInput global from its constructor call site:
            //   mov rdi, r13 / call <...> / lea r15, [rip+disp] / mov rdi, r15 / call <ctor>
            // add(11) lands on that lea's disp32 and abs() resolves it. Verified unique across the
            // whole module by a byte scan, which matters because the two surrounding calls are
            // wildcarded and the rest is generic register movement.
            .template addPattern<CSGOInputPointer, CodePattern{"4C 89 EF E8 ? ? ? ? 4C 8D 3D ? ? ? ? 4C 89 FF E8"}.add(11).abs()>()
            // The ranking block, from the function that publishes "game/level" / "game/xppts".
            // It loads the block once and indexes it, so the `lea` plus the very next instruction -
            // `test byte ptr [rbx+10h], 4`, a flags check on the block itself - is enough to be
            // unique. add(3) lands on the lea's disp32.
            .template addPattern<PlayerRankingDataPointer, CodePattern{"48 8D 1D ? ? ? ? F6 43 10 04 0F 85"}.add(3).abs()>()
            // Both econ-account accessors come out of one anchor - the status function behind the
            // "elevated" / "awaiting_cooldown" strings:
            //   call <EconSystemAccessor> / mov rax,[rax+115868h] / test / jz /
            //   mov rdi,[rax+68h] / call <GameAccountClientAccessor> / mov rbx,rax / test / jz / call
            // The shorter form of this (stopping after the second call) matches TEN sites and they
            // do NOT all call the same thing - 0x1ed7f50's second call goes to a different accessor
            // entirely - so the trailing `mov rbx,rax; test; jz; call` is load-bearing, not padding.
            // It brings the match count to exactly one. Verified by byte scan.
            .template addPattern<EconSystemAccessor, CodePattern{"E8 ? ? ? ? 48 8B 80 68 58 11 00 48 85 C0 74 ? 48 8B 78 68 E8 ? ? ? ? 48 89 C3 48 85 C0 74 ? E8"}.add(1).abs()>()
            .template addPattern<GameAccountClientAccessor, CodePattern{"E8 ? ? ? ? 48 8B 80 68 58 11 00 48 85 C0 74 ? 48 8B 78 68 E8 ? ? ? ? 48 89 C3 48 85 C0 74 ? E8"}.add(22).abs()>()
            // Resolved from slot 6's own subtick-move GROW path, which is unique in the module:
            //   mov rdi,[r14+18h] / mov [rbp+..],r8 / call <new step> / lea rdi,[r14+18h] /
            //   mov rsi,rax / call <AddAllocated>
            // add(12) lands on the first call's rel32 and abs() resolves it - a call displacement
            // is encoded exactly like the rip-relative ones this already handles. Patterning the
            // allocator directly is not possible: its prologue is shared by every protobuf message
            // of the same size, and only the typeinfo operand distinguishes it.
            .template addPattern<CreateSubtickMoveStep, CodePattern{"49 8B 7E 18 4C 89 85 ? ? ? ? E8 ? ? ? ? 49 8D 7E 18 48 89 C6 E8"}.add(12).abs()>()
            .template addPattern<RepeatedPtrFieldAddAllocated, CodePattern{"55 53 48 89 F5 51 48 8B 47 10 48 89 FB 48 85 C0 74 07 8B 4F 0C 39 08 75 0D BE 01 00 00 00"}>()
            // sub_15DAB20 - the command-ring getter. Pattern lifted verbatim from the internal
            // reference base's own signature for it and re-verified against this build, where it
            // matches exactly one address. Matching on the prologue plus the `mov r12, <global>`
            // that loads the command manager is what makes it unique.
            .template addPattern<GetUserCmd, CodePattern{"55 48 89 E5 41 54 4C 8B 25 ? ? ? ? 53 89 F3"}>()
            // add(24) steps over `mov rax,[rsi+18h]; test rax,rax; jz; mov rax,[rax+48h];
            // cmp [rbp-120h],rax` to land exactly on the `0F 84` of the SteamID-match branch.
            .template addPattern<AbandonCooldownGate, CodePattern{"48 8B 46 18 48 85 C0 0F 84 ? ? ? ? 48 8B 40 48 48 39 85 ? ? ? ? 0F 84"}.add(24)>()
            .template addPattern<ManageGlowSceneObjectPointer, CodePattern{"55 66 48 0F 7E C8"}>()
            .template addPattern<SetSceneObjectAttributeFloat4, CodePattern{"55 66 0F 6E D6 48 89 E5 53 48"}>()
            .template addPattern<PointerToClientMode, CodePattern{"05 ? ? ? ? ? 89 ? 48 89 05 ? ? ? ? E8 ? ? ? ? ? 8B ? ? C9 C3"}.add(1).abs()>();
    }
};
