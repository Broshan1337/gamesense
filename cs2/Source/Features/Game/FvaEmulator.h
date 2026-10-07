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
} 

namespace fva
{



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

} 

template <typename HookContext>
class FvaEmulator {
public:
    explicit FvaEmulator(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    
    void onCreateMove(cs2::CUserCmd* cmd) noexcept
    {
        if (!GET_CONFIG_VAR(FvaEnabled))
            return;

        
        
        
        
        
        
        if (!hookContext.activeLocalPlayerPawn())
            return;

        
        
        
        
        auto&& cvars = hookContext.template make<CvarSystem>();
        
        (void)cvars.forceBoolConVar("cl_showusercmd", false);
        (void)cvars.forceBoolConVar("cl_pred_print_every_cmd", false);

        UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        
        
        
        
        if (const cs2::GlobalVars* globalVars = hookContext.globalVars().globalVars) {
            if (globalVars->tickCount < lastSeenTickCount)
                resetForLevelChange();
            lastSeenTickCount = globalVars->tickCount;
        }

        
        
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
            
            
            
            fromPitch = 0.0f;
            fromYaw = 0.0f;
        }
        if (targetArmedNow) {
            toPitch = fva_target::floatOf(fva_target::pitchBits.load(std::memory_order_relaxed));
            toYaw = fva_target::floatOf(fva_target::yawBits.load(std::memory_order_relaxed));
        }

        
        
        referencePitch = currentPitch;
        referenceYaw = currentYaw;

        if (!zeroOrigin && !targetArmedNow && !hasMeaningfulRotation(fromPitch, toPitch, fromYaw, toYaw))
            return; 

        stage(history, cmd, fromPitch, fromYaw, toPitch, toYaw);

        if (targetArmedNow)
            fva::clearTargetAngle(); 
    }

    
    
    
    
    
    
    void onWriteMoveCrcEarly(cs2::CUserCmd* cmd) noexcept
    {
        if (!GET_CONFIG_VAR(FvaSilentShots))
            return; 
        publishStaged(cmd);
    }

    
    
    
    void onWriteMoveCrcLate(cs2::CUserCmd* cmd) noexcept
    {
        if (GET_CONFIG_VAR(FvaSilentShots)) {
            stagedCount = 0; 
            return;
        }
        publishStaged(cmd);
    }

private:
    
    
    
    
    void publishStaged(cs2::CUserCmd* cmd) noexcept
    {
        if (!GET_CONFIG_VAR(FvaEnabled))
            return;

        if (stagedCount == 0)
            return;

        
        
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

    
    
    
    
    
    
    
    
    [[nodiscard]] static std::byte* cloneMessage(const std::byte* source, void* arena) noexcept
    {
        void* const sourceVtable = readPointer(source);
        if (!sourceVtable)
            return nullptr;

        using NewInArenaFn = void* (*)(const void*, void*);

        
        
        
        
        
        
        
        
        
        const void* const newFunction = *reinterpret_cast<void* const*>(reinterpret_cast<const std::byte*>(sourceVtable) + 3 * sizeof(void*));
        if (!newFunction)
            return nullptr; 
        CrashLogger::trace(0xC10);
        CrashLogger::trace(reinterpret_cast<std::uint64_t>(newFunction));
        CrashLogger::trace(reinterpret_cast<std::uint64_t>(arena));
        CrashLogger::trace(reinterpret_cast<std::uint64_t>(source));
        std::byte* fresh = static_cast<std::byte*>(reinterpret_cast<NewInArenaFn>(const_cast<void*>(newFunction))(source, arena));
        CrashLogger::trace(0xC12);
        if (!fresh || fresh == source)
            return nullptr;

        if (readPointer(fresh) != sourceVtable)
            return nullptr; 

        if (arena && effectiveArenaWord(fresh) != reinterpret_cast<std::uintptr_t>(arena))
            return nullptr;

        using MergeFromFn = void (*)(void*, const void*);
        const void* const mergeFunction = *reinterpret_cast<void* const*>(reinterpret_cast<const std::byte*>(sourceVtable) + 6 * sizeof(void*)); 
        if (!mergeFunction)
            return nullptr;
        CrashLogger::trace(0xC13);
        CrashLogger::trace(reinterpret_cast<std::uint64_t>(mergeFunction));
        CrashLogger::trace(reinterpret_cast<std::uint64_t>(fresh));
        reinterpret_cast<MergeFromFn>(const_cast<void*>(mergeFunction))(fresh, source);
        CrashLogger::trace(0xC14);
        return fresh;
    }

    
    
    
    
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

    
    
    
    
    
    
    
    [[nodiscard]] static std::byte* entryNew(void* arena) noexcept
    {
        const auto entryVtable = reinterpret_cast<const std::byte*>(CrashLogger::clientModule.base + kEntryVtableRva);
        const void* const newFunction = *reinterpret_cast<void* const*>(entryVtable + 3 * sizeof(void*));
        if (!newFunction)
            return nullptr;

        using NewInArenaFn = void* (*)(const void*, void*);
        std::byte* fresh = static_cast<std::byte*>(reinterpret_cast<NewInArenaFn>(const_cast<void*>(newFunction))(nullptr, arena));
        if (!fresh || readPointer(fresh) != entryVtable)
            return nullptr; 
        return fresh;
    }

    
    
    
    
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

    
    
    
    void stage(InputHistory& history, cs2::CUserCmd* cmd, float fromPitch, float fromYaw, float toPitch, float toYaw) noexcept
    {
        const void* const templateViewAngles = readPointer(userCmdBase(cmd) + cs2::CUserCmd::BaseMessage::kViewAnglesOffset);
        if (!templateViewAngles)
            return;

        void* const arena = readPointer(reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::InputHistory::kFieldOffset);

        
        
        const int entryCount = fva_math::plannedEntries(
            static_cast<int>(GET_CONFIG_VAR(fva_vars::Substeps)), cs2::CUserCmd::kMaxInputHistoryEntries);
        if (entryCount <= 0)
            return;

        
        
        
        std::uint32_t renderTickCount{lastCarriedRenderTickCount};
        if (const std::byte* newestLive = history.entryAt(history.currentSize() - 1)) {
            if (readPointer(newestLive) == reinterpret_cast<const void*>(CrashLogger::clientModule.base + kEntryVtableRva)) {
                std::memcpy(&renderTickCount, newestLive + cs2::CUserCmd::InputHistory::kEntryRenderTickCountOffset, sizeof(renderTickCount));
                lastCarriedRenderTickCount = renderTickCount;
            }
        }

        
        
        
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
            
            
            std::byte* qangle = cloneMessage(reinterpret_cast<const std::byte*>(templateViewAngles), arena);
            CrashLogger::trace(0xE20 | static_cast<std::uint64_t>(i));
            if (!qangle)
                break;

            const float interpolatedPitch = fva_math::interpolated(fromPitch, toPitch, i, entryCount);
            const float interpolatedYaw = fva_math::interpolated(fromYaw, toYaw, i, entryCount);
            std::memcpy(qangle + QAngleOffsets::kPitchOffset, &interpolatedPitch, sizeof(interpolatedPitch));
            std::memcpy(qangle + QAngleOffsets::kYawOffset, &interpolatedYaw, sizeof(interpolatedYaw));

            
            
            std::uint32_t qangleBits{};
            std::memcpy(&qangleBits, qangle + cs2::CUserCmd::BaseMessage::kHasBitsOffset, sizeof(qangleBits));
            qangleBits |= 0x3; 
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
        stagedCount = 0;             
        fva::clearTargetAngle();
        VerifyConsole::write(5.0f, "[fva]", "level change: chain state reset\n");
    }

    
    
    
    
    
    static constexpr std::uintptr_t kCMsgQangleVtableRva = 0x4357670;
    static constexpr std::uintptr_t kEntryVtableRva = 0x4380aa0;

    
    
    
    inline static bool referenceValid{false};
    inline static float referencePitch{}, referenceYaw{};
    inline static int lastSeenTickCount{};
    inline static std::uint32_t lastCarriedRenderTickCount{};

    
    
    
    
    static constexpr int kMaxStagedEntries = 16;
    inline static std::byte* stagedEntries[kMaxStagedEntries]{};
    inline static int stagedCount{0};
    inline static float stagedToPitch{}, stagedToYaw{};
};
