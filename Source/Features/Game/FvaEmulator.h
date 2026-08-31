#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/CUserCmd.h>
#include <Features/Game/FvaConfigVariables.h>
#include <GameClient/GlobalVars.h>
#include <GameClient/InputHistory.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/SpreadPrediction/SpreadSolver.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <Utils/FvaMath.h>
#include <Utils/Optional.h>
#include <Utils/RetAddrSpoofer.h>
#include <Utils/CrashLogger.h>
#include <Utils/StringBuilder.h>
#include <Utils/VerifyConsole.h>

// The FVA view-angle emulator - port of FORFUTURETESTS/mytest's Section-J emitter to this tree's
// Linux-resolved plumbing.
//
// What it does on the wire: while enabled, every tick with real rotation grows input_history by a
// chain of CSGOInputHistoryEntryPB entries whose embedded CMsgQAngle values walk from the
// reference cache (what the server was last told) toward the angles the real command carries,
// spread across subtick fractions of THIS tick. The base message's own viewangles are left
// untouched, so local prediction, rendering and move_crc all keep describing reality; only the
// per-entry history - what anti-cheat rewind logic replays when validating a shot - grows a
// synthetic smooth rotation chain. A view stops being a single angle and becomes a scheduled
// sequence of absolute angles across the tick.
//
// TWO PHASES, and the split is measured, not stylistic: this build's slot 6 (BuildUserCmd)
// rebuilds input_history AFTER the CreateMove hook returns - the tree's own Aimbot notes proved
// it ("anything written at CreateMove is gone") and our first live run confirmed it (chains
// published at CreateMove, wire check read 0 at WriteMoveCrc). So CreateMove only STAGES fully
// built entries; the WriteMoveCrc hook - where edits provably reach the wire - publishes them
// into the freshly rebuilt-empty field. Staged entries are heap clones (this build's field is
// heap-backed: its arena word is NULL), so slot 6's rebuild never sees or clears them.
//
// Target selection (matching the reconstruction's semantics):
//   * default    - chain endpoint is the command's REAL angles each tick ("self echo": the
//                  reference cache follows the mouse; near-zero turns publish nothing at all).
//                  LegitAimbot and Rcs move the real view, so they feed this mode automatically
//                  and need no arming of their own.
//   * zero-origin experiment - FvaZeroOriginSpoof pins the chain start at (0,0) every tick,
//                  reproducing the literal nonsense-delta behaviour of the decompiled original.
//   * external   - fva::setTargetAngle() arms an explicit endpoint. This is the integration
//                  point for the aim-assist experiments that motivated this port: the Aimbot
//                  arms it for every staged silent shot. The endpoint is written INTO THE
//                  HISTORY CHAIN ONLY, never into the base message, so local view and wire
//                  history can disagree on purpose while armed.
//
// Safety model (each gate exists because mytest hit it live and paid for it in delayed crashes):
//   * every entry/qangle is a clone made through the game's own generated-protobuf vtable slots
//     (New(Arena*) = vtable[3], MergeFrom = vtable[6] on this build - slot-verified against the
//     on-disk CMsgQAngle vtable's disassembly, because the Windows reconstruction's slot[2]=New
//     turned out to be a per-platform divergence) from a LIVE instance this command already owns
//     -> correct vtable and correct allocator forever, no signature/RVA manifest to rot;
//   * clones are allocated directly in the input_history field's arena, so publishing on the
//     fast path is arena-consistent by construction (mytest had to bolt on a cross-arena copy
//     pass because its qangle::New ran with NULL first);
//   * publishing beyond pre-allocation rides the resolved game AddAllocated helper with a
//     post-check that refuses mid-chain instead of trusting drifted resolvers;
//   * structural validation refuses stale or half-initialised rep layouts outright.
//
// Parity ledger against FORFUTURETESTS/mytest (the clean-room FVA reconstruction):
//   PORTED   - Section-J emitter, reference-cache write-back, zero-origin experiment mode,
//              external target API (fva::setTargetAngle, armed by the Aimbot on staged shots),
//              per-shot seed restamp (through the already-resolved seed function, not a Windows
//              byte signature), analysis-cvar suppression, firing gate (opt-in), level-change
//              reset via tick-count regression, isvalveds_check (m_bIsValveDS forced to false -
//              offset anchored to the cs2-dumper dump of THIS Linux build: 0x9C vs the Windows
//              0xA4, proving the deferral-until-dumped call was right; see
//              cs2::C_CSGameRules::kIsValveDsOffset and Features/Game/IsValveDsSpoof.h).
//   PORTED   (cont.) - the engine2 stable-gameplay gate as a local-pawn presence check. The
//              original audit wrongly marked this "covered" by the hook architecture; the live
//              map-load SIGSEGV proved otherwise: hooks DO run during transitions, the input
//              protobufs ARE concurrently torn down there, and only a gate keeps the emitter
//              out of that window. Same lesson mytest paid for with their de_mirage repro.
//   COVERED  - Hook A (CSGOInput vtable swap), Hook B (FrameStageNotify), console exec, convar
//              registry access, pattern scanning, logging - all pre-existing here in stronger
//              form, and every resolved call goes through the return-address spoofer.
//              client_input (GetSlotInput/GetCmdBySequence): mytest needed those resolvers
//              because its detour re-fetched the command through the input chain; this tree's
//              VMT hook signature hands the command in directly, so the resolvers have no scope.
//   OBSOLETE - Hook C + scratch mirror + capture buffer + move_crc rewrite: mytest mutated the
//              protobuf AFTER crc computation in some paths and needed a re-serialization loop
//              (plus ArenaStringPtr::Set) to repair it. Every mutation in this tree happens
//              before slot 7 recomputes the crc over the final pb, so there is nothing to repair.
//   VESTIGIAL - animation_hook (ShouldUpdateSequences): PROVEN dead code in mytest itself -
//              hook_manager.cpp documents that FVA's H4 slot was never populated ("stub/
//              vestigial/reserved"), and its install is commented out. Not ported for the same
//              reason it is disabled there.
//   DEFERRED - fire_flag_probe proper (the hidden firing byte): FVA resolves it via a hard-coded
//              client RVA + schema-hash walk, both live-RE artifacts. The schema dump contains
//              no such input-state field (input classes are not schema classes), so the honest
//              substitute remains the attack-button gate. Upgrading needs a live session with
//              the game_state accessor - the one place a byte-level live RE pass still helps.

// Shared target state for the FVA view-angle emulator. Lives OUTSIDE the class template because
// aim-assist features will arm targets through these free functions and must not drag
// HookContext/GlobalContext includes along just to reach an atomic.
//
// Armed state is consumed by the next published chain ("one-shot" like the reference): the value
// describes where the NEXT completed rotation chain should END as far as input_history claims,
// never where the real view goes. atomic<float> would work here, but the bit-cast keeps every
// store lock-free by construction the way PanicKey keeps its latched edge - cheap and obvious.
namespace fva_target
{
inline std::atomic<bool> armed{false};
inline std::atomic<std::uint32_t> pitchBits{};
inline std::atomic<std::uint32_t> yawBits{};

[[nodiscard]] inline std::uint32_t bitCasted(float value) noexcept
{
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

[[nodiscard]] inline float floatOf(std::uint32_t bits) noexcept
{
    float value{};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
} // namespace fva_target

namespace fva
{

// Arms the next tick's chain endpoint. The endpoint lives in the history chain ONLY - rendering,
// local prediction and the base message keep describing reality.
inline void setTargetAngle(float pitch, float yaw) noexcept
{
    fva_target::pitchBits.store(fva_target::bitCasted(pitch), std::memory_order_relaxed);
    fva_target::yawBits.store(fva_target::bitCasted(yaw), std::memory_order_relaxed);
    fva_target::armed.store(true, std::memory_order_release);
}

inline void clearTargetAngle() noexcept
{
    fva_target::armed.store(false, std::memory_order_release);
}

} // namespace fva

template <typename HookContext>
class FvaEmulator {
public:
    explicit FvaEmulator(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // Runs after the original CreateMove inside the CSGOInput slot 26 detour, at the end of the
    // feature chain: every angle-affecting feature above has finished, so the command carries the
    // angles that are actually being sent - exactly what the chains should interpolate towards.
    void onCreateMove(cs2::CUserCmd* cmd) noexcept
    {
        if (!GET_CONFIG_VAR(FvaEnabled))
            return;

        // STABLE-GAMEPLAY GATE - the port of the reconstruction's engine2!IsInGame && IsConnected
        // fix, and it exists because this crash class was reproduced here: SIGSEGV inside
        // libclient while a map was loading. During map transitions the input protobufs are torn
        // down by networksystem on ANOTHER thread, which means no point-in-time validation of
        // them is safe - the only safe move is to not touch the command's protobuf memory at all
        // until a local pawn exists again (the in-game signal this tree has without new patterns).
        if (!hookContext.activeLocalPlayerPawn())
            return;

        // Analysis-cvar suppression - FVA's Hook A zeroed these two dev cvars' value bytes every
        // tick because both dump exactly the fields the chains rewrite (cl_showusercmd prints the
        // serialized usercmd protobuf; cl_pred_print_every_cmd traces every prediction command).
        // Written through resolved value pointers (CvarSystem::forceBoolConVar), never offsets.
        auto&& cvars = hookContext.template make<CvarSystem>();
        // false = cvar value pointer not resolved yet; suppression retried next tick by design.
        (void)cvars.forceBoolConVar("cl_showusercmd", false);
        (void)cvars.forceBoolConVar("cl_pred_print_every_cmd", false);

        UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        // Level restart detection without a new hook: the game's tick count regresses across
        // map changes. A stale reference cache (or an armed target from the previous level)
        // would otherwise span the load boundary, so everything resets exactly like the
        // reconstruction's LevelShutdown handler did.
        if (const cs2::GlobalVars* globalVars = hookContext.globalVars().globalVars) {
            if (globalVars->tickCount < lastSeenTickCount)
                resetForLevelChange();
            lastSeenTickCount = globalVars->tickCount;
        }

        // Opt-in firing gate: chains only while the primary attack is held - the wire shape of
        // the original's firing-flag phase, before it switched to always-running self echo.
        if (GET_CONFIG_VAR(FvaChainOnFireOnly) && !userCmd.isButtonDown(cs2::CCSGOInput::Buttons::kAttack))
            return;

        InputHistory history{cmd};
        if (!history.looksValid()) {
            VerifyConsole::write(5.0f, "[fva]", "input_history layout refused\n");
            return;
        }

        const Optional<float> yaw{userCmd.viewYaw()};
        const Optional<float> pitch{userCmd.viewPitch()};
        if (!yaw.hasValue() || !pitch.hasValue()) {
            referenceValid = false;
            return;
        }

        const float currentPitch{pitch.value()}, currentYaw{yaw.value()};

        // Map-change / teleport guard: anything further than half a circle against our own cache
        // snaps the cache instead of interpolating through fantasy angles around the globe.
        if (!referenceValid || fva_math::isDiscontinuity(referenceYaw, currentYaw)) {
            referenceValid = true;
            referencePitch = currentPitch;
            referenceYaw = currentYaw;
            return;
        }

        float fromPitch = referencePitch, fromYaw = referenceYaw;
        float toPitch = currentPitch, toYaw = currentYaw;

        const bool zeroOrigin = GET_CONFIG_VAR(FvaZeroOriginSpoof);
        const bool targetArmedNow = fva_target::armed.load(std::memory_order_acquire);
        if (zeroOrigin) {
            // The decompiled original's literal behaviour: reference pinned at (0,0), making even
            // idle ticks describe a full flight from zero to wherever you look. Kept behind its
            // own experiment switch because it is trivially detectable server-side.
            fromPitch = 0.0f;
            fromYaw = 0.0f;
        }
        if (targetArmedNow) {
            toPitch = fva_target::floatOf(fva_target::pitchBits.load(std::memory_order_relaxed));
            toYaw = fva_target::floatOf(fva_target::yawBits.load(std::memory_order_relaxed));
        }

        // Reference-cache write-back happens EVERY tick regardless of emission - the behaviour
        // the reconstruction verified against the original's cache handling.
        referencePitch = currentPitch;
        referenceYaw = currentYaw;

        if (!zeroOrigin && !targetArmedNow && !hasMeaningfulRotation(fromPitch, toPitch, fromYaw, toYaw))
            return; // Self echo with no turn: publishing empty chains buys nothing.

        stage(history, cmd, fromPitch, fromYaw, toPitch, toYaw);

        if (targetArmedNow)
            fva::clearTargetAngle(); // One-shot consumption, mirroring the reference contract.
    }

    // Slot 7 (WriteMoveCrc), pre-original - EARLY position, before the shot writer. Only active
    // in the Silent Shots experiment: publishing here lets SubtickShotWriter see our chains and
    // take the per-entry redirect (shot claimed at the aim angle through input_history while the
    // base viewangles stay at the real view). On servers that resolve shots from base angles -
    // like this build's default matchmaking - this combination misses; the late position is the
    // proven-hitting default.
    void onWriteMoveCrcEarly(cs2::CUserCmd* cmd) noexcept
    {
        if (!GET_CONFIG_VAR(FvaSilentShots))
            return; // late position owns publication in the default composition
        publishStaged(cmd);
    }

    // Slot 7 (WriteMoveCrc), pre-original - LATE position, after every other feature. The
    // default composition: each feature sees the field exactly as slot 6 left it, and the chains
    // publish once nothing else can touch them.
    void onWriteMoveCrcLate(cs2::CUserCmd* cmd) noexcept
    {
        if (GET_CONFIG_VAR(FvaSilentShots)) {
            stagedCount = 0; // already published (or dropped) at the early position
            return;
        }
        publishStaged(cmd);
    }

private:
    // The command object here is the one that goes on the wire, and this build's slot 6 has by
    // now rebuilt input_history empty - which is precisely why the chains are PUBLISHED here
    // instead of at CreateMove. Same command object as the staging pass, so the staged heap
    // clones are still alive and owned by nobody else.
    void publishStaged(cs2::CUserCmd* cmd) noexcept
    {
        if (!GET_CONFIG_VAR(FvaEnabled))
            return;

        if (stagedCount == 0)
            return;

        // Same stable-gameplay reasoning as the CreateMove gate: if the pawn vanished between
        // the hooks (disconnect mid-tick), the pb may be mid-teardown - drop instead of touch.
        if (!hookContext.activeLocalPlayerPawn()) {
            stagedCount = 0;
            return;
        }

        InputHistory history{cmd};
        if (!history.looksValid()) {
            VerifyConsole::write(5.0f, "[fva]", "slot-7 field refused, dropping staged chains\n");
            stagedCount = 0;
            return;
        }

        auto&& patternSearchResults = hookContext.patternSearchResults();
        const auto addAllocated = patternSearchResults.template get<RepeatedPtrFieldAddAllocated>();

        CrashLogger::trace(0xF80);
        int published = 0;
        for (int i = 0; i < stagedCount; ++i) {
            CrashLogger::trace(0xF90 | static_cast<std::uint64_t>(i));
            const auto publishResult = history.publish(stagedEntries[static_cast<std::size_t>(i)], addAllocated);
            CrashLogger::trace(0xFA0 | (static_cast<std::uint64_t>(publishResult) << 8) | static_cast<std::uint64_t>(i));
            if (publishResult == InputHistory::PublishResult::Refused) {
                VerifyConsole::write(5.0f, "[fva]", "slot-7 publish refused mid-chain\n");
                stagedCount = 0;
                return;
            }
            ++published;
        }
        stagedCount = 0;

        if (published == 0)
            return;

        // The reconstruction's final step was calling CS2's ComputeRandomSeed to make each
        // rewritten command carry an engine-valid seed. On Linux that helper is not something we
        // sig-hunt: this tree already resolves the SAME derivation - per-shot seed =
        // hash(round(pitch,.5deg), round(yaw,.5deg), tick) inside libclient - and SpreadSolver
        // exposes it directly. Recomputing from the chain ENDPOINT keeps server-side RNG rewinds
        // aligned with what input_history now claims. Runs HERE because slot 6 resets the base
        // message's seed field along with everything else.
        if (GET_CONFIG_VAR(FvaReseedChains)) {
            const auto tickBase = hookContext.localPlayerController().tickBase();
            if (tickBase.hasValue() && tickBase.value() > 0) {
                auto solver = hookContext.template make<SpreadSolver>();
                const auto derivedSeed = solver.seed(typename SpreadSolver<HookContext>::Angles{stagedToPitch, stagedToYaw, 0.0f}, tickBase.value());
                if (derivedSeed.hasValue())
                    UserCmd{cmd}.setRandomSeed(static_cast<int>(derivedSeed.value()));
            }
        }

        VerifyConsole::write(2.0f, "[fva]", "wire: published %d chains, input_history carries %d entries\n", published, history.currentSize());
    }

    [[nodiscard]] static bool hasMeaningfulRotation(float fromPitch, float toPitch, float fromYaw, float toYaw) noexcept
    {
        constexpr float epsilonDegrees = 0.001f;
        return fva_math::foldedMagnitude(fva_math::fold180(toPitch - fromPitch)) > epsilonDegrees
            || fva_math::foldedMagnitude(fva_math::fold180(toYaw - fromYaw)) > epsilonDegrees;
    }

    [[nodiscard]] static std::byte* readPointer(const void* at) noexcept
    {
        std::byte* pointer{nullptr};
        std::memcpy(&pointer, at, sizeof(pointer));
        return pointer;
    }

    // Clones a live embedded protobuf message into `arena` using the two slots that the on-disk
    // vtable disassembly verified for THIS build: vtable[3] = New(Arena*) (allocates sizeof(T),
    // stamps vtable + arena, zeroes has-bits and fields) and vtable[6] = MergeFrom. NOTE the
    // cross-platform divergence from the Windows reconstruction (which used vtable[2] for New):
    // slot indices are NOT portable across Valve's platform protobuf builds. Every step validates,
    // because calling the wrong slot on the wrong build corrupts memory instead of returning null:
    //   * fresh pointer must differ from the source AND carry the SAME vtable qword;
    //   * its stamped arena word must untangle to exactly the arena we asked for (null-is-null).
    [[nodiscard]] static std::byte* cloneMessage(const std::byte* source, void* arena) noexcept
    {
        void* const sourceVtable = readPointer(source);
        if (!sourceVtable)
            return nullptr;

        using NewInArenaFn = void* (*)(const void*, void*);

        // vtable[3] holds New(Arena*) on THIS build - established by disassembling the real
        // CMsgQAngle vtable (libclient+0x4357670, typeinfo "10CMsgQAngle"): slot[3] allocates
        // sizeof(CMsgQAngle)=0x28 bytes, stamps vtable + arena at +8 and zeroes +0x10..+0x20
        // (has-bits + x/y/z, re-validating our field offsets). mytest's Windows RE put New at
        // vtable[2] (+0x10) - a cross-platform vtable divergence: their +0x30 MergeFrom matches
        // this build's slot[6], but slot[2] here is a Valve-custom arena helper that unconditionally
        // dereferences its argument. Two crashes taught this in sequence: calling slot[2]'s ADDRESS
        // instead of its content (pc == fault at vtable+0x10), then calling slot[2] itself with the
        // field's NULL arena word (fault 0x0 inside libclient+0x21366a6's `mov (%rdi),%rax`).
        const void* const newFunction = *reinterpret_cast<void* const*>(reinterpret_cast<const std::byte*>(sourceVtable) + 3 * sizeof(void*));
        if (!newFunction)
            return nullptr; // null slot = not the class we assume; refuse instead of call
        CrashLogger::trace(0xC10);
        CrashLogger::trace(reinterpret_cast<std::uint64_t>(newFunction));
        CrashLogger::trace(reinterpret_cast<std::uint64_t>(arena));
        CrashLogger::trace(reinterpret_cast<std::uint64_t>(source));
        std::byte* fresh = static_cast<std::byte*>(reinterpret_cast<NewInArenaFn>(newFunction)(source, arena));
        CrashLogger::trace(0xC12);
        if (!fresh || fresh == source)
            return nullptr;

        if (readPointer(fresh) != sourceVtable)
            return nullptr; // Wrong-slot landing or a different generation entirely.

        if (arena && effectiveArenaWord(fresh) != reinterpret_cast<std::uintptr_t>(arena))
            return nullptr;

        using MergeFromFn = void (*)(void*, const void*);
        const void* const mergeFunction = *reinterpret_cast<void* const*>(reinterpret_cast<const std::byte*>(sourceVtable) + 6 * sizeof(void*)); // vtable+0x30
        if (!mergeFunction)
            return nullptr;
        CrashLogger::trace(0xC13);
        CrashLogger::trace(reinterpret_cast<std::uint64_t>(mergeFunction));
        CrashLogger::trace(reinterpret_cast<std::uint64_t>(fresh));
        reinterpret_cast<MergeFromFn>(mergeFunction)(fresh, source);
        CrashLogger::trace(0xC14);
        return fresh;
    }

    // Protobuf stamps every generated message's arena field at +8 as a TAGGED word:
    //   bit 1 - "no arena" tag (effective 0); bit 0 - indirection (word is a pointer-to-pointer);
    //   everything else is the raw pointer. Reproduced from the reconstruction's untag routine -
    // byte-for-byte identical arithmetic keeps wire-level assumptions in one family.
    [[nodiscard]] static std::uintptr_t effectiveArenaWord(const std::byte* message) noexcept
    {
        std::uintptr_t raw{};
        std::memcpy(&raw, message + 8, sizeof(raw));
        if (raw & 0b10)
            return 0;
        raw &= ~std::uintptr_t{0b11};
        if (raw & 0b01)
            std::memcpy(&raw, reinterpret_cast<const void*>(raw), sizeof(raw));
        return raw;
    }

    // SYNTHESIS - the entry is built from scratch instead of cloned. The reason is measured:
    // on this build the elements the game fills into input_history at CreateMove are NOT
    // CSGOInputHistoryEntryPB (their vtable fails the identity anchor, and slot[3] reads as
    // null - the `call 0` crash), so there is no trustworthy template to clone. The entry
    // vtable's New thunk DISCARDS its `this` argument (verified: `mov rsi,rdi; jmp RealNew`),
    // so a fresh 120-byte entry can be constructed from the vtable anchor alone: New stamps
    // the real vtable, zeroes has-bits and every field, and applies the -1 defaults.
    [[nodiscard]] static std::byte* entryNew(void* arena) noexcept
    {
        const auto entryVtable = reinterpret_cast<const std::byte*>(CrashLogger::clientModule.base + kEntryVtableRva);
        const void* const newFunction = *reinterpret_cast<void* const*>(entryVtable + 3 * sizeof(void*));
        if (!newFunction)
            return nullptr;

        using NewInArenaFn = void* (*)(const void*, void*);
        std::byte* fresh = static_cast<std::byte*>(reinterpret_cast<NewInArenaFn>(newFunction)(nullptr, arena));
        if (!fresh || readPointer(fresh) != entryVtable)
            return nullptr; // constructor drift - refuse rather than publish a wrong-shaped entry
        return fresh;
    }

    // Field writes on a freshly synthesized entry. Field 1 on this class is the view_angles
    // CMsgQAngle (has-bit 0x1, child at +0x18 - unchanged from the 14177 RE and re-confirmed by
    // the 14178 Clear()); the tick-context scalars sit at +0x60..+0x6c under bits
    // 0x200/0x400/0x800/0x1000 (the appended 14178 fields live beyond them at +0x70/+0x74).
    [[nodiscard]] static std::byte* prepareSyntheticEntry(void* arena, std::byte* qangle,
                                                          std::uint32_t renderTickCount) noexcept
    {
        std::byte* entry = entryNew(arena);
        if (!entry)
            return nullptr;

        std::memcpy(entry + cs2::CUserCmd::InputHistory::kEntryViewAnglesOffset, &qangle, sizeof(qangle));

        std::uint32_t hasBits{};
        std::memcpy(&hasBits, entry + cs2::CUserCmd::InputHistory::kEntryHasBitsOffset, sizeof(hasBits));
        hasBits |= cs2::CUserCmd::InputHistory::kEmittedEntryHasBits;
        std::memcpy(entry + cs2::CUserCmd::InputHistory::kEntryHasBitsOffset, &hasBits, sizeof(hasBits));

        std::memcpy(entry + cs2::CUserCmd::InputHistory::kEntryRenderTickCountOffset, &renderTickCount, sizeof(renderTickCount));
        return entry;
    }

    // Builds the chain entries against the CreateMove-time field state (which carries the game's
    // own entries to clone from) but does NOT publish: the clones are heap objects the slot 6
    // rebuild never touches, held in static staging until the WriteMoveCrc hook publishes them.
    void stage(InputHistory& history, cs2::CUserCmd* cmd, float fromPitch, float fromYaw, float toPitch, float toYaw) noexcept
    {
        const void* const templateViewAngles = readPointer(userCmdBase(cmd) + cs2::CUserCmd::BaseMessage::kViewAnglesOffset);
        if (!templateViewAngles)
            return;

        void* const arena = readPointer(reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::InputHistory::kFieldOffset);

        // The slot-7 field arrives freshly rebuilt-empty, so the ceiling that matters is the
        // fixed buffer maximum, not this tick's CreateMove-time occupancy.
        const int entryCount = fva_math::plannedEntries(
            static_cast<int>(GET_CONFIG_VAR(fva_vars::Substeps)), cs2::CUserCmd::kMaxInputHistoryEntries);
        if (entryCount <= 0)
            return;

        // Per-tick context: prefer a real game entry's render_tick_count when the field holds
        // genuine CSGOInputHistoryEntryPB elements; otherwise carry the last value we saw (and
        // before the first one, zeros - the server currently ignores this field anyway).
        std::uint32_t renderTickCount{lastCarriedRenderTickCount};
        if (const std::byte* newestLive = history.entryAt(history.currentSize() - 1)) {
            if (readPointer(newestLive) == reinterpret_cast<const void*>(CrashLogger::clientModule.base + kEntryVtableRva)) {
                std::memcpy(&renderTickCount, newestLive + cs2::CUserCmd::InputHistory::kEntryRenderTickCountOffset, sizeof(renderTickCount));
                lastCarriedRenderTickCount = renderTickCount;
            }
        }

        // Vtable identity guard: the template must be a REAL CMsgQAngle, verified against the
        // on-disk vtable address. Catches freed/mid-teardown memory, wrong-class children, and
        // any layout drift - everything that used to reach a call through a garbage pointer.
        const void* const liveQangleVtable = readPointer(templateViewAngles);
        const auto expectedQangleVtable = CrashLogger::clientModule.base + kCMsgQangleVtableRva;
        if (liveQangleVtable != reinterpret_cast<const void*>(expectedQangleVtable)) {
            VerifyConsole::write(10.0f, "[fva]", "qangle vtable mismatch (got +0x%llx), refusing tick\n",
                                 liveQangleVtable ? static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(liveQangleVtable) - CrashLogger::clientModule.base) : 0ull);
            return;
        }

        using QAngleOffsets = cs2::CUserCmd::BaseMessage::ViewAngles;
        CrashLogger::trace(0xE00);

        stagedCount = 0;
        for (int i = 0; i < entryCount; ++i) {
            if (stagedCount >= kMaxStagedEntries)
                break;
            CrashLogger::trace(0xE10 | static_cast<std::uint64_t>(i));
            // No qangle clone means no further chain: cloning needs a live instance of the type
            // and this command's own viewangles sub-message is the only supplier we trust.
            std::byte* qangle = cloneMessage(reinterpret_cast<const std::byte*>(templateViewAngles), arena);
            CrashLogger::trace(0xE20 | static_cast<std::uint64_t>(i));
            if (!qangle)
                break;

            const float interpolatedPitch = fva_math::interpolated(fromPitch, toPitch, i, entryCount);
            const float interpolatedYaw = fva_math::interpolated(fromYaw, toYaw, i, entryCount);
            std::memcpy(qangle + QAngleOffsets::kPitchOffset, &interpolatedPitch, sizeof(interpolatedPitch));
            std::memcpy(qangle + QAngleOffsets::kYawOffset, &interpolatedYaw, sizeof(interpolatedYaw));

            // Presence bits: yaw/pitch MUST be marked (a present sub-message claiming no angle
            // would serialize as nothing at all); roll keeps whatever the template carried.
            std::uint32_t qangleBits{};
            std::memcpy(&qangleBits, qangle + cs2::CUserCmd::BaseMessage::kHasBitsOffset, sizeof(qangleBits));
            qangleBits |= 0x3; // x | y
            std::memcpy(qangle + cs2::CUserCmd::BaseMessage::kHasBitsOffset, &qangleBits, sizeof(qangleBits));

            CrashLogger::trace(0xE30 | static_cast<std::uint64_t>(i));
            std::byte* entry = prepareSyntheticEntry(arena, qangle, renderTickCount);
            CrashLogger::trace(0xE40 | static_cast<std::uint64_t>(i));
            if (!entry)
                break;

            const float when = fva_math::whenAt(i, entryCount);
            std::memcpy(entry + cs2::CUserCmd::InputHistory::kEntryRenderTickFractionOffset, &when, sizeof(when));

            CrashLogger::trace(0xE50 | static_cast<std::uint64_t>(i));
            stagedEntries[static_cast<std::size_t>(stagedCount)] = entry;
            ++stagedCount;
        }

        if (stagedCount == 0)
            return;

        stagedToPitch = toPitch;
        stagedToYaw = toYaw;
    }

    [[nodiscard]] static std::byte* userCmdBase(cs2::CUserCmd* cmd) noexcept
    {
        if (!cmd)
            return nullptr;
        std::byte* base{nullptr};
        std::memcpy(&base, reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::kBaseMessageOffset, sizeof(base));
        return base;
    }

    HookContext& hookContext;

    void resetForLevelChange() noexcept
    {
        referenceValid = false;
        stagedCount = 0;             // never publish chains staged on the previous level
        fva::clearTargetAngle();
        VerifyConsole::write(5.0f, "[fva]", "level change: chain state reset\n");
    }

    // Runtime identity anchors for the two cloned classes, taken from the on-disk vtables of
    // THIS build (14178): CMsgQAngle's primary vtable and CSGOInputHistoryEntryPB's primary
    // vtable. A live object whose first qword differs from these is NOT the class we think it
    // is, and calling its slot[3] is how the last crash happened (slot read as 0 -> call 0).
    // Build-specific by design - re-dump after game updates, same rule as kIsValveDsOffset.
    static constexpr std::uintptr_t kCMsgQangleVtableRva = 0x4357670;
    static constexpr std::uintptr_t kEntryVtableRva = 0x4380aa0;

    // Cross-tick state, polled once per user command on the game thread - the storage shape
    // PanicKey established. Statics are per-template-instantiation, and there is exactly one
    // instantiation in this project (HookContext<GlobalContext>), matching the existing pipeline.
    inline static bool referenceValid{false};
    inline static float referencePitch{}, referenceYaw{};
    inline static int lastSeenTickCount{};
    inline static std::uint32_t lastCarriedRenderTickCount{};

    // Cross-hook staging (CreateMove builds -> WriteMoveCrc publishes). Entries are heap clones
    // owned by nobody until published; a tick whose WriteMoveCrc never runs simply leaks them to
    // the process heap (a few KB, rare) rather than corrupting anything - the next staging pass
    // replaces the slots.
    static constexpr int kMaxStagedEntries = 16;
    inline static std::byte* stagedEntries[kMaxStagedEntries]{};
    inline static int stagedCount{0};
    inline static float stagedToPitch{}, stagedToYaw{};
};
