#pragma once

#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct ClientPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<MainMenuPanelPointer, CodePattern{"E5 53 48 83 EC ? 48 8D 1D ? ? ? ? 48 8B 03 48 8B 78 ? 48 8B"}.add(9).abs()>()
            // ⚠️ HUD ROOT - NO libclient GLOBAL EXISTS ON THE 2026-09-26 5GB BUILD (build
            // 68f386a6): the wrapper-global concept is DEAD, re-verified live on the running
            // game after the user challenged the removal ("patterns move, they don't vanish").
            // Exhaustive scan across ALL writable ranges of ALL loaded modules (libclient
            // .bss/.data + every other module + their anon .bss, 121 writable ranges) for
            // EVERY level of the live panel tree ('CSGOHud' root CUIPanel + its CPanel2D,
            // 'Hud', 'HudInWorld', 'HudReticle', the CCSGO_HudReticle element instance):
            // ZERO references. The game moved the indirection INTO the panorama engine
            // object: UiEnginePointer (still exactly-once, global 0x4B9BDD8) -> [engine+0x218]
            // = the global CUIPanel slot array (entries at +0x10, stride 0x20: {CUIPanel*,
            // u64 serial}; live: 'CSGOHud' = entry 3, round-trip [cpanel2d+8] == panel OK).
            // The reticle element itself = CCSGO_HudReticle, a CPanel2D DERIVATIVE
            // (primary vtable = libclient 0x45398a0, uiPanel@+8, CSGO_HudElement secondary
            // interface @+0x20 with the "CCSGO_HudReticle" name @+0x50) - it does not have
            // its own wrapper global either. HudContext::panel() resolves the root by
            // walking the engine slot list; every deref there is gated on a cached
            // /proc/self/maps region table, so garbage list/panel values (the 18:39 crash:
            // engine+0x218 valid but the list pointer itself garbage) degrade to a null
            // panel instead of a SEGV. Do NOT re-add a HudPanelPointer pattern to THIS
            // pool - the Windows pool keeps its own (different binary).
            .template addPattern<GlobalVarsPointer, CodePattern{"8D ? ? ? ? ? 48 89 35 ? ? ? ? 48 89 ? ? C3"}.add(9).abs()>()
            .template addPattern<TransformTranslate3dVMT, CodePattern{"48 8D 0D ? ? ? ? 48 89 08 48 89 50 08 48 8B 53 10"}.add(3).abs()>()
            .template addPattern<TransformScale3dVMT, CodePattern{"48 8B 53 08 48 8D 0D ? ? ? ? F3 0F 10 43"}.add(7).abs()>()
            .template addPattern<WorldToProjectionMatrixPointer, CodePattern{"01 4C 8D 05 ? ? ? ? 4C 89 EE"}.add(4).abs()>()
            .template addPattern<ViewToProjectionMatrixPointer, CodePattern{"EE 48 8D 0D ? ? ? ? 48 8D 15 ? ? ? ? 48"}.add(4).abs()>()
            .template addPattern<ViewRenderPointer, CodePattern{"48 8D 05 ? ? ? ? 48 89 38 48 85"}.add(3).abs()>()
            .template addPattern<LocalPlayerControllerPointer, CodePattern{"48 83 3D ? ? ? ? ? 0F 95 C0 C3"}.add(3).abs(5)>()
            // The client's CGameEventManager. Two facts drive this anchor (re-verified on
            // 1.41.8.9, 2026-10-06):
            //  1. It is NOT reachable via CreateInterface("GAMEEVENTSMANAGER002"): libclient's
            //     interface list has exactly 8 static InterfaceReg nodes (Source2ClientConfig001,
            //     EmptyWorldService001_Client, GameClientExports001, Source2Client002,
            //     ClientToolsInfo_001, Source2ClientPrediction001, Source2ClientUI001,
            //     LegacyGameUI001 - walked from the exported CreateInterface's registry head,
            //     0x49ED120 on this build) and the event manager is placed into the game-system
            //     registry instead. The "GAMEEVENTSMANAGER002" string in .rodata has zero code
            //     references. (The old comment that claimed the resolved global WAS
            //     g_pGameEventManager at 0x492F258/0x4931AD8 was wrong end to end: that global is
            //     the Source2EngineToClient001 interface instance - its only writer is
            //     `lea rdi,"Source2EngineToClient001"; call rbx; mov [global],rax` - hooking it
            //     VMT-swapped the engine-client interface, which is where the 09-26 crash wave
            //     came from.)
            //  2. It is NOT a pointer global either: the client placement-constructs the object
            //     in place at a fixed .bss address (RTTI-confirmed _ZTI17CGameEventManager ->
            //     vtable 0x44D8470, ctor 0x1719860 on 1.41.8.9). This pattern resolves THE
            //     OBJECT's address (the type is IGameEventManager2*, not **).
            //
            // Anchor = the placement-construction idiom: lea r15,[object]; lea rdi,[factory
            // node]; call <game-system registrar>; mov rdi,r15; call <CGameEventManager::ctor>;
            // mov rsi,r15; mov rdx,r12; lea rdi,[dtor node]. The tail past the ctor call is
            // load-bearing: ClientModeCSNormal is built by the same idiom a page earlier, and
            // only the GEM site continues mov rdx,r12 (the clientmode site goes mov rdx,rbx /
            // xor r15d,r15d). add(3).abs() lands on the first lea's disp32 -> the placement
            // address. Object 0x4918300 on 1.41.8.9; verified exactly-once.
            .template addPattern<GameEventManagerGlobalPointer, CodePattern{"4C 8D 3D ? ? ? ? 48 8D 3D ? ? ? ? E8 ? ? ? ? 4C 89 FF E8 ? ? ? ? 4C 89 FE 4C 89 E2 48 8D 3D"}.add(3).abs()>()
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
            // 2026-09-23 1.41.8.2: handler recompiled - the old `je` rel32 after `test al,al`
            // became a rel8 (74 xx), `mov r12d,[rax+0x58]` (cl_color ref deref) moved BEFORE the
            // first notify call, and the tail-call chain shuffled. Re-derived from the cvar
            // registration parade: the "ChangeTeammateColor" name string's registration site
            // passes this callback (lea rsi = &fn, rdi = &cl_color cvar). Verified exactly-once.
            .template addPattern<ChangeTeammateColorCycle, CodePattern{"55 48 89 E5 41 54 53 48 8D 1D ? ? ? ? 48 83 EC 10 0F B6 05 ? ? ? ? 84 C0 74 ? 48 8B 05 ? ? ? ? BE FF FF FF FF 48 89 DF 44 8B 60 58 E8"}>()
            // 2026-09-23 1.41.8.2: the getter template now has TWO byte-identical twins; the
            // chat delegate is the one whose FOLLOWING function keeps its arg in r12 (the
            // comms-ban printer at 0x1AE6E70 calls this getter then ChatPrint at 0x2098E30).
            // The trailing `49 89` disambiguates. Verified exactly-once.
            .template addPattern<HudChatDelegatePointer, CodePattern{"48 8B 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 55 48 89 E5 41 56 49 89"}.add(3).abs()>()
            // Recovers the CCSGOInput global from its constructor call site:
            //   mov rdi, r13 / call <...> / lea r15, [rip+disp] / mov rdi, r15 / call <ctor>
            // add(11) lands on that lea's disp32 and abs() resolves it. Verified unique across the
            // whole module by a byte scan, which matters because the two surrounding calls are
            // wildcarded and the rest is generic register movement.
            .template addPattern<CSGOInputPointer, CodePattern{"4C 89 EF E8 ? ? ? ? 4C 8D 3D ? ? ? ? 4C 89 FF E8"}.add(11).abs()>()
            // The ranking block (0x494AB60 on 2026-09-23, was 0x4843720), from the function that
            // publishes "game/level" / "game/xppts". Same anchor as before - lea + the block-flags
            // test `test byte ptr [rbx+10h], 4` - the conditional jump flipped polarity
            // (0F 85 jne -> 0F 84 je) in this build. add(3) lands on the lea's disp32.
            .template addPattern<PlayerRankingDataPointer, CodePattern{"48 8D 1D ? ? ? ? F6 43 10 04 0F 84"}.add(3).abs()>()
            // Both econ-account accessors come out of one anchor - the status function behind the
            // "elevated" / "awaiting_cooldown" strings (GetElevatedState, registered under that
            // name): call <EconSystemAccessor> / mov rax,[rax+0x12C848] / test / jz /
            // mov rdi,[rax+68h] / call <GameAccountClientAccessor> / mov rbx,rax / test / jz / call
            // 2026-09-23 1.41.8.2: the econ-system field offset moved 0x115868 -> 0x12C848
            // (recompiled struct); the trailing `mov rbx,rax; test; jz; call` stays load-bearing
            // (shorter forms match a decoy twin). Verified exactly-once by wildcard scan.
            .template addPattern<EconSystemAccessor, CodePattern{"E8 ? ? ? ? 48 8B 80 48 C8 12 00 48 85 C0 74 ? 48 8B 78 68 E8 ? ? ? ? 48 89 C3 48 85 C0 74 ? E8"}.add(1).abs()>()
            .template addPattern<GameAccountClientAccessor, CodePattern{"E8 ? ? ? ? 48 8B 80 48 C8 12 00 48 85 C0 74 ? 48 8B 78 68 E8 ? ? ? ? 48 89 C3 48 85 C0 74 ? E8"}.add(22).abs()>()
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
            // 2026-09-23 1.41.8.2: both conditional jumps in the gate collapsed rel32 -> rel8
            // (744F / 7456); add(20) lands on the 74 opcode of the SteamID-match je.
            // add(20) steps over `mov rax,[rsi+18h]; test rax,rax; je(rel8); mov rax,[rax+48h];
            // cmp [rbp-120h],rax` to land exactly on the `je` of the SteamID-match branch.
            .template addPattern<AbandonCooldownGate, CodePattern{"48 8B 46 18 48 85 C0 74 ? 48 8B 40 48 48 39 85 ? ? ? ? 74"}.add(20)>()
            .template addPattern<ManageGlowSceneObjectPointer, CodePattern{"55 66 48 0F 7E C8"}>()
            .template addPattern<SetSceneObjectAttributeFloat4, CodePattern{"55 66 0F 6E D6 48 89 E5 53 48"}>()
            .template addPattern<PointerToClientMode, CodePattern{"05 ? ? ? ? ? 89 ? 48 89 05 ? ? ? ? E8 ? ? ? ? ? 8B ? ? C9 C3"}.add(1).abs()>();
    }
};
