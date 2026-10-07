#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Vector.h>
#include <Features/Combat/MovementFix.h>
#include <GameClient/SpreadPrediction/SpreadSolver.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Optional.h>
#include <Utils/VerifyConsole.h>




























template <typename HookContext>
class SubtickShotWriter {
public:
    explicit SubtickShotWriter(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    [[nodiscard]] bool run(cs2::CUserCmd* cmd, auto&& localPawn,
              float aimPitch, float aimYaw,
              float punchPitch, float punchYaw,
              float backtrackSimTime,
              bool compensateSpread,
              int* redirectedEntry = nullptr,
              const typename SpreadSolver<HookContext>::Angles* precomputedCorrection = nullptr) const noexcept
    {
        if (redirectedEntry)
            *redirectedEntry = -1;

        const bool backtracking = backtrackSimTime > 0.0f;

        auto* const cmdBytes = reinterpret_cast<std::byte*>(cmd);

        int size{};
        std::memcpy(&size, cmdBytes + cs2::CUserCmd::kInputHistorySizeOffset, sizeof(size));
        if (size <= 0 || size > cs2::CUserCmd::kMaxInputHistoryEntries) {
            bool claimedLands = false;
            int claimedEntry = -1;
            if (size <= 0
                && claimRecycledHistoryEntry(cmdBytes, localPawn, aimPitch, aimYaw, punchPitch, punchYaw,
                                             backtrackSimTime, compensateSpread, backtracking, claimedLands, claimedEntry,
                                             precomputedCorrection)) {
                if (redirectedEntry)
                    *redirectedEntry = claimedEntry;
                return claimedLands;
            }

            
            
            
            
            
            
            
            std::byte* probeRep = nullptr;
            std::memcpy(&probeRep, cmdBytes + kInputHistoryRepOffset, sizeof(probeRep));
            int totalSize{};
            std::memcpy(&totalSize, cmdBytes + kInputHistoryTotalSizeOffset, sizeof(totalSize));
            int allocatedSize = -1;
            if (probeRep)
                std::memcpy(&allocatedSize, probeRep, sizeof(allocatedSize));
            VerifyConsole::write(1.0f, "hist", "empty path (base-angle fallback): current=%d total=%d rep=%p allocated=%d", size, totalSize, static_cast<void*>(probeRep), allocatedSize);

            return writeIntoBaseViewangles(localPawn, cmd, aimPitch, aimYaw, punchPitch, punchYaw, compensateSpread, precomputedCorrection);
        }

        std::byte* rep = nullptr;
        std::memcpy(&rep, cmdBytes + kInputHistoryRepOffset, sizeof(rep));
        if (!rep)
            return writeIntoBaseViewangles(localPawn, cmd, aimPitch, aimYaw, punchPitch, punchYaw, compensateSpread, precomputedCorrection);

        
        
        
        int attackIndex{};
        std::memcpy(&attackIndex, cmdBytes + cs2::CUserCmd::kAttack1StartHistoryIndexOffset, sizeof(attackIndex));
        const int index = (attackIndex >= 0 && attackIndex < size) ? attackIndex : size - 1;

        std::byte* entry = nullptr;
        std::memcpy(&entry, rep + kRepElementsOffset + static_cast<std::ptrdiff_t>(index) * static_cast<std::ptrdiff_t>(sizeof(entry)), sizeof(entry));
        if (!entry)
            return !compensateSpread;

        std::byte* viewAngles = nullptr;
        std::memcpy(&viewAngles, entry + kHistoryViewAnglesOffset, sizeof(viewAngles));
        if (!viewAngles)
            return !compensateSpread;

        
        
        
        
        VerifyConsole::write(1.0f, "hist", "entry path: redirecting entry %d of %d (silent shot)", index, size);

        if (redirectedEntry)
            *redirectedEntry = index;

        return writeShotIntoEntry(entry, viewAngles, localPawn, aimPitch, aimYaw, punchPitch, punchYaw,
                                  backtrackSimTime, compensateSpread, backtracking, false,
                                  precomputedCorrection);
    }

private:
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    [[nodiscard]] bool claimRecycledHistoryEntry(std::byte* cmdBytes, auto&& localPawn,
                                                 float aimPitch, float aimYaw,
                                                 float punchPitch, float punchYaw,
                                                 float backtrackSimTime, bool compensateSpread,
                                                 bool backtracking, bool& lands, int& entryIndex,
                                                 const typename SpreadSolver<HookContext>::Angles* precomputedCorrection) const noexcept
    {
        std::byte* rep = nullptr;
        std::memcpy(&rep, cmdBytes + kInputHistoryRepOffset, sizeof(rep));
        if (!rep)
            return false;

        int allocatedSize{};
        std::memcpy(&allocatedSize, rep, sizeof(allocatedSize));
        if (allocatedSize <= 0)
            return false;

        std::byte* entry = nullptr;
        std::memcpy(&entry, rep + kRepElementsOffset, sizeof(entry));
        if (!entry)
            return false;

        std::byte* viewAngles = nullptr;
        std::memcpy(&viewAngles, entry + kHistoryViewAnglesOffset, sizeof(viewAngles));
        if (!viewAngles)
            return false;

        
        
        const int newSize = 1;
        std::memcpy(cmdBytes + cs2::CUserCmd::kInputHistorySizeOffset, &newSize, sizeof(newSize));

        std::uint32_t hasBits{};
        std::memcpy(&hasBits, entry + kHasBitsOffset, sizeof(hasBits));
        hasBits |= kViewAnglesEntryHasBit;
        std::memcpy(entry + kHasBitsOffset, &hasBits, sizeof(hasBits));

        std::uint32_t vaHasBits{};
        std::memcpy(&vaHasBits, viewAngles + kSubMessageHasBitsOffset, sizeof(vaHasBits));
        vaHasBits |= kViewAnglesSubMessageHasBits;
        std::memcpy(viewAngles + kSubMessageHasBitsOffset, &vaHasBits, sizeof(vaHasBits));

        VerifyConsole::write(1.0f, "hist", "fast path: recycled entry 0 (fully silent shot)");

        entryIndex = 0;
        lands = writeShotIntoEntry(entry, viewAngles, localPawn, aimPitch, aimYaw, punchPitch, punchYaw,
                                   backtrackSimTime, compensateSpread, backtracking, true,
                                   precomputedCorrection);
        return true;
    }

    
    
    
    
    [[nodiscard]] bool writeShotIntoEntry(std::byte* entry, std::byte* viewAngles, auto&& localPawn,
                            float aimPitch, float aimYaw, float punchPitch, float punchYaw,
                            float backtrackSimTime, bool compensateSpread, bool backtracking,
                            bool recycledEntry,
                            const typename SpreadSolver<HookContext>::Angles* precomputedCorrection = nullptr) const noexcept
    {
        
        
        
        int tickBase{};
        if (const auto baseTick = hookContext.localPlayerController().tickBase(); baseTick.hasValue())
            tickBase = baseTick.value();
        const int stampTick = backtracking
            ? static_cast<int>(backtrackSimTime / kTickInterval) + 1
            : tickBase;

        
        
        Optional<typename SpreadSolver<HookContext>::Angles> corrected;
        if (compensateSpread && stampTick > 0) {
            if (precomputedCorrection && !backtracking) {
                corrected = *precomputedCorrection;
            } else {
                auto solver = hookContext.template make<SpreadSolver>();
                if (const auto params = solver.weaponParams(localPawn.getActiveWeapon()); params.hasValue())
                    corrected = solver.findSpreadCorrection(typename SpreadSolver<HookContext>::Angles{aimPitch, aimYaw, 0.0f}, stampTick, params.value());
            }
        }
        const bool lands = !compensateSpread || corrected.hasValue();

        
        
        
        
        const float pitch = (corrected.hasValue() ? corrected.value().pitch : aimPitch) - punchPitch;
        const float yaw = (corrected.hasValue() ? corrected.value().yaw : aimYaw) - punchYaw;

        std::memcpy(viewAngles + cs2::CUserCmd::BaseMessage::ViewAngles::kPitchOffset, &pitch, sizeof(pitch));
        std::memcpy(viewAngles + cs2::CUserCmd::BaseMessage::ViewAngles::kYawOffset, &yaw, sizeof(yaw));

        
        
        
        
        
        if (corrected.hasValue() || recycledEntry) {
            const float roll = corrected.hasValue() ? corrected.value().roll : 0.0f;
            std::memcpy(viewAngles + cs2::CUserCmd::BaseMessage::ViewAngles::kRollOffset, &roll, sizeof(roll));
        }

        
        
        
        if (backtracking) {
            stampBacktrackTick(entry, backtrackSimTime);
        } else if (stampTick > 0) {
            stampLiveTick(entry, stampTick);
        }

        zeroInterpolationInfo(entry);
        return lands;
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    [[nodiscard]] bool writeIntoBaseViewangles(auto&& localPawn, cs2::CUserCmd* cmd, float aimPitch, float aimYaw,
                                               float punchPitch, float punchYaw, bool compensateSpread,
                                               const typename SpreadSolver<HookContext>::Angles* precomputedCorrection = nullptr) const noexcept
    {
        int tickBase{};
        if (const auto baseTick = hookContext.localPlayerController().tickBase(); baseTick.hasValue())
            tickBase = baseTick.value();

        Optional<typename SpreadSolver<HookContext>::Angles> corrected;
        if (compensateSpread && tickBase > 0) {
            if (precomputedCorrection) {
                corrected = *precomputedCorrection;
            } else {
                auto solver = hookContext.template make<SpreadSolver>();
                if (const auto params = solver.weaponParams(localPawn.getActiveWeapon()); params.hasValue())
                    corrected = solver.findSpreadCorrection(typename SpreadSolver<HookContext>::Angles{aimPitch, aimYaw, 0.0f}, tickBase, params.value());
            }
        }
        const bool lands = !compensateSpread || corrected.hasValue();
        const float pitch = (corrected.hasValue() ? corrected.value().pitch : aimPitch) - punchPitch;
        const float yaw = (corrected.hasValue() ? corrected.value().yaw : aimYaw) - punchYaw;

        const UserCmd userCmd{cmd};
        if (!userCmd)
            return lands;

        
        
        movement_fix::setViewAngles(userCmd, pitch, yaw);

        
        
        
        
        
        return lands;
    }

    
    
    
    static void stampLiveTick(std::byte* entry, int tick) noexcept
    {
        setTickFields(entry, tick + 1, 0.0f, tick, 0.0f);
    }

    
    
    static void stampBacktrackTick(std::byte* entry, float simulationTime) noexcept
    {
        const float t = simulationTime / kTickInterval;
        const int tick = static_cast<int>(t);
        const float fraction = t - static_cast<float>(tick);
        const int tickPlusOne = tick + 1;
        setTickFields(entry, tickPlusOne, 0.0f, tickPlusOne, fraction);
    }

    static void setTickFields(std::byte* entry, int renderTick, float renderFrac,
                              int playerTick, float playerFrac) noexcept
    {
        std::uint32_t hasBits{};
        std::memcpy(&hasBits, entry + kHasBitsOffset, sizeof(hasBits));
        hasBits |= kTickHasBits;
        std::memcpy(entry + kHasBitsOffset, &hasBits, sizeof(hasBits));

        std::memcpy(entry + kRenderTickCountOffset, &renderTick, sizeof(renderTick));
        std::memcpy(entry + kRenderTickFractionOffset, &renderFrac, sizeof(renderFrac));
        std::memcpy(entry + kPlayerTickCountOffset, &playerTick, sizeof(playerTick));
        std::memcpy(entry + kPlayerTickFractionOffset, &playerFrac, sizeof(playerFrac));
    }

    
    
    
    static void zeroInterpolationInfo(std::byte* entry) noexcept
    {
        std::uint32_t hasBits{};
        std::memcpy(&hasBits, entry + kHasBitsOffset, sizeof(hasBits));

        if (hasBits & kClInterpEntryHasBit)
            zeroClInterp(entry + kClInterpPtrOffset);
        if (hasBits & kSvInterp0EntryHasBit)
            zeroSvInterp(entry + kSvInterp0PtrOffset);
        if (hasBits & kSvInterp1EntryHasBit)
            zeroSvInterp(entry + kSvInterp1PtrOffset);
    }

    
    static void zeroClInterp(std::byte* ptrField) noexcept
    {
        std::byte* interp = loadPointer(ptrField);
        if (!interp)
            return;
        setSubMessageFloat(interp, kInterpFracOffset, kInterpFracHasBit, 0.0f);
    }

    
    
    
    static void zeroSvInterp(std::byte* ptrField) noexcept
    {
        std::byte* interp = loadPointer(ptrField);
        if (!interp)
            return;
        setSubMessageFloat(interp, kInterpFracOffset, kInterpFracHasBit, 0.0f);
        setSubMessageInt(interp, kInterpSrcTickOffset, kInterpSrcTickHasBit, -1);
        setSubMessageInt(interp, kInterpDstTickOffset, kInterpDstTickHasBit, -1);
    }

    [[nodiscard]] static std::byte* loadPointer(std::byte* at) noexcept
    {
        std::byte* pointer = nullptr;
        std::memcpy(&pointer, at, sizeof(pointer));
        return pointer;
    }

    
    
    static void setSubMessageFloat(std::byte* msg, int offset, std::uint32_t hasBit, float value) noexcept
    {
        std::memcpy(msg + offset, &value, sizeof(value));
        orSubMessageHasBits(msg, hasBit);
    }

    static void setSubMessageInt(std::byte* msg, int offset, std::uint32_t hasBit, std::int32_t value) noexcept
    {
        std::memcpy(msg + offset, &value, sizeof(value));
        orSubMessageHasBits(msg, hasBit);
    }

    static void orSubMessageHasBits(std::byte* msg, std::uint32_t bits) noexcept
    {
        std::uint32_t hasBits{};
        std::memcpy(&hasBits, msg + kSubMessageHasBitsOffset, sizeof(hasBits));
        hasBits |= bits;
        std::memcpy(msg + kSubMessageHasBitsOffset, &hasBits, sizeof(hasBits));
    }

    
    
    
    
    static constexpr std::ptrdiff_t kInputHistoryRepOffset = 56;
    static constexpr std::ptrdiff_t kInputHistoryTotalSizeOffset = 52; 
    static constexpr std::ptrdiff_t kRepElementsOffset = 8;

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    static constexpr std::ptrdiff_t kHistoryViewAnglesOffset = 0x18;
    static constexpr std::ptrdiff_t kClInterpPtrOffset = 0x20;
    static constexpr std::ptrdiff_t kSvInterp0PtrOffset = 0x28;
    static constexpr std::ptrdiff_t kSvInterp1PtrOffset = 0x30;

    static constexpr std::uint32_t kViewAnglesEntryHasBit = 0x1;
    static constexpr std::uint32_t kClInterpEntryHasBit = 0x2;
    static constexpr std::uint32_t kSvInterp0EntryHasBit = 0x4;
    static constexpr std::uint32_t kSvInterp1EntryHasBit = 0x8;

    static constexpr std::ptrdiff_t kHasBitsOffset = 16;
    static constexpr std::uint32_t kRenderTickCountHasBit = 0x200;
    static constexpr std::uint32_t kRenderTickFractionHasBit = 0x400;
    static constexpr std::uint32_t kPlayerTickCountHasBit = 0x800;
    static constexpr std::uint32_t kPlayerTickFractionHasBit = 0x1000;
    static constexpr std::uint32_t kTickHasBits = kRenderTickCountHasBit | kRenderTickFractionHasBit
        | kPlayerTickCountHasBit | kPlayerTickFractionHasBit;

    static constexpr std::ptrdiff_t kRenderTickCountOffset = 96;
    static constexpr std::ptrdiff_t kRenderTickFractionOffset = 100;
    static constexpr std::ptrdiff_t kPlayerTickCountOffset = 104;
    static constexpr std::ptrdiff_t kPlayerTickFractionOffset = 108;

    
    static constexpr std::ptrdiff_t kSubMessageHasBitsOffset = 0x10;
    static constexpr std::ptrdiff_t kInterpFracOffset = 0x18;
    static constexpr std::ptrdiff_t kInterpSrcTickOffset = 0x1c;
    static constexpr std::ptrdiff_t kInterpDstTickOffset = 0x20;
    static constexpr std::uint32_t kInterpFracHasBit = 0x1;
    static constexpr std::uint32_t kInterpSrcTickHasBit = 0x2;
    static constexpr std::uint32_t kInterpDstTickHasBit = 0x4;

    
    
    static constexpr std::uint32_t kViewAnglesSubMessageHasBits = 0x1 | 0x2 | 0x4;

    static constexpr float kTickInterval = 0.015625f; 

    HookContext& hookContext;
};
