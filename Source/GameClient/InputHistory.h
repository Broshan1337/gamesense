#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>

// Appends entries to the wire-side CSGOUserCmdPB.input_history repeated field.
//
// Where SubtickMoves.h grows base->subtick_moves, this is the sibling wrapper for the field that
// carries per-entry view angles to the server: cmd+40 is a RepeatedPtrFieldBase shaped exactly
// like the SubtickMoves one (see cs2::CUserCmd::InputHistory for how every anchor reconciles with
// constants this tree already trusted - kInputHistorySizeOffset, SubtickShotWriter's +52/+56 pair).
//
// The reference implementation (FORFUTURETESTS/mytest, a reconstruction of the obfuscated FVA
// plugin) burns hundreds of lines on this same job and learned its lessons in live crashes:
//   * Reuse allocated-but-unused spare slots FIRST - skipping them desynchronises the field's
//     allocated count from reality (SubtickMoves has the identical reuseSpareStep rule).
//   * Growth past total_size belongs to the game's own AddAllocated, never to hand-rolled Rep
//     surgery; the mytest port's manual grow path leaked Reps on every map change before it was
//     replaced by exactly the call we resolve as RepeatedPtrFieldAddAllocated.
//
// Deliberately NOT a HookContext template: allocation needs only the already-resolved game
// helper passed in explicitly, so every decision here is unit-testable against crafted buffers.
class InputHistory {
public:
    using AddAllocatedFn = void* (*)(void* field, void* element);

    explicit InputHistory(cs2::CUserCmd* cmd) noexcept
        : field{fieldOf(cmd)}
    {
    }

    [[nodiscard]] static std::byte* fieldOf(cs2::CUserCmd* cmd) noexcept
    {
        if (!cmd)
            return nullptr;
        return reinterpret_cast<std::byte*>(cmd) + cs2::CUserCmd::InputHistory::kFieldOffset;
    }

    // Elements currently visible to the server. A negative or oversized read means the offsets
    // are stale - treated as "unknown", which disables everything downstream.
    [[nodiscard]] int currentSize() const noexcept
    {
        if (!field)
            return -1;
        int size{};
        std::memcpy(&size, field + fieldRelative(cs2::CUserCmd::InputHistory::kCurrentSizeOffset), sizeof(size));
        return size;
    }

    // Sanity gate run before ANY write. This sits directly on the input path of every tick;
    // mytest's history shows a half-validated rep pointer corrupts networksystem minutes later,
    // so anything outside the protobuf invariants refuses loudly instead.
    //
    // One legitimate state beyond "populated": a freshly rebuilt-empty field (current = total =
    // rep = 0) is what this build's slot 6 leaves behind, and the game's own AddAllocated grows
    // such a field from nothing - that is exactly the virgin state SubtickMoves never sees but
    // this wrapper must.
    [[nodiscard]] bool looksValid() const noexcept
    {
        const int current = currentSize();
        if (current < 0 || current > cs2::CUserCmd::kMaxInputHistoryEntries)
            return false;

        int total{};
        std::memcpy(&total, field + fieldRelative(cs2::CUserCmd::InputHistory::kTotalSizeOffset), sizeof(total));
        if (total < 0 || total > kSaneTotalLimit || current > total)
            return false;

        std::byte* rep = nullptr;
        std::memcpy(&rep, field + fieldRelative(cs2::CUserCmd::InputHistory::kRepOffset), sizeof(rep));
        if (!rep)
            return current == 0 && total == 0; // virgin field - growable via the game helper
        if ((reinterpret_cast<std::uintptr_t>(rep) & 0x7) != 0)
            return false;

        int allocated{};
        std::memcpy(&allocated, rep + cs2::CUserCmd::InputHistory::kRepAllocatedSizeOffset, sizeof(allocated));
        if (allocated < current || allocated > total)
            return false;

        // Every live element pointer must be sane before we trust the slot math below. Reading
        // element pointers from an array [current..allocated) would also be fine, but this only
        // walks what the server could see anyway.
        for (int i = 0; i < current; ++i) {
            std::byte* entry = nullptr;
            std::memcpy(&entry, rep + cs2::CUserCmd::InputHistory::kRepElementsOffset + static_cast<std::ptrdiff_t>(i) * sizeof(entry), sizeof(entry));
            if (!entry)
                return false;
        }
        return true;
    }

    // Slots available WITHOUT any growth or game call: the region [current, allocated) holds
    // owned-but-invisible elements the game handed back last command - overwriting there is the
    // exact mechanics the engine itself performs between commands.
    [[nodiscard]] int spareSlots() const noexcept
    {
        if (!looksValid())
            return 0;

        std::byte* rep = nullptr;
        std::memcpy(&rep, field + fieldRelative(cs2::CUserCmd::InputHistory::kRepOffset), sizeof(rep));
        if (!rep)
            return 0;
        int allocated{}, current{};
        std::memcpy(&allocated, rep + cs2::CUserCmd::InputHistory::kRepAllocatedSizeOffset, sizeof(allocated));
        std::memcpy(&current, field + fieldRelative(cs2::CUserCmd::InputHistory::kCurrentSizeOffset), sizeof(current));
        return allocated > current ? allocated - current : 0;
    }

    // How many chain entries still fit under the fixed-buffer ceiling the game enforces.
    [[nodiscard]] int freeSlots() const noexcept
    {
        if (!looksValid())
            return 0;
        return cs2::CUserCmd::kMaxInputHistoryEntries - currentSize();
    }

    // The view_angles pointer of entry `index`, or null when out of range / unset. Used both to
    // validate a candidate vtable source and to carry per-tick context over between entries.
    [[nodiscard]] std::byte* entryAt(int index) const noexcept
    {
        if (!looksValid() || index < 0 || index >= currentSize())
            return nullptr;

        std::byte* rep = nullptr;
        std::memcpy(&rep, field + fieldRelative(cs2::CUserCmd::InputHistory::kRepOffset), sizeof(rep));
        std::byte* entry = nullptr;
        std::memcpy(&entry, rep + cs2::CUserCmd::InputHistory::kRepElementsOffset + static_cast<std::ptrdiff_t>(index) * sizeof(entry), sizeof(entry));
        return entry;
    }

    // Publishes a fully-stamped entry built BY THE CALLER (the caller owns arena agreement -
    // cloned allocations must have been made in this field's arena, see FvaEmulator.h).
    //
    // Branch selection reproduces the reference decompile of RepeatedPtrField::AddAllocated
    // exactly, including its ORDER, because the branches interact:
    //   * spares beside us            -> conserving swap-append (moved element keeps ownership);
    //   * rep exhausted, arenaless    -> invisible-slot recycle, counts apart deliberately;
    //   * anything else               -> ONLY the game's own reserve+append helper, post-checked.
    enum class PublishResult { Refused, PublishedFastPath, PublishedSlotRecycle, PublishedByGame };

    template <typename AddAllocated>
    [[nodiscard]] PublishResult publish(std::byte* entry, AddAllocated&& addAllocated) noexcept
    {
        if (!entry || static_cast<bool>(addAllocated) == false || !looksValid())
            return PublishResult::Refused;

        // Hard ceiling first: the wire field mirrors a fixed 32-entry circular buffer on this
        // build, so appending past that count would desynchronise the local mirror no matter how
        // valid the Rep surgery looks.
        std::byte* rep = nullptr;
        std::memcpy(&rep, field + fieldRelative(cs2::CUserCmd::InputHistory::kRepOffset), sizeof(rep));

        int current{}, total{}, allocated{};
        std::memcpy(&current, field + fieldRelative(cs2::CUserCmd::InputHistory::kCurrentSizeOffset), sizeof(current));
        std::memcpy(&total, field + fieldRelative(cs2::CUserCmd::InputHistory::kTotalSizeOffset), sizeof(total));
        std::memcpy(&allocated, rep + cs2::CUserCmd::InputHistory::kRepAllocatedSizeOffset, sizeof(allocated));

        if (current >= cs2::CUserCmd::kMaxInputHistoryEntries)
            return PublishResult::Refused;

        if (rep) {
            // Branch B of the reference decompile (sub_7FFBE80CB348): spares exist beside us -
            // conserving swap-append keeps every owned message accounted for.
            if (current != total && allocated != total) {
                if (current < allocated) {
                    std::byte* moved = nullptr;
                    std::memcpy(&moved, rep + cs2::CUserCmd::InputHistory::kRepElementsOffset + static_cast<std::ptrdiff_t>(current) * sizeof(moved), sizeof(moved));
                    std::memcpy(rep + cs2::CUserCmd::InputHistory::kRepElementsOffset + static_cast<std::ptrdiff_t>(allocated) * sizeof(moved), &moved, sizeof(moved));
                }
                std::memcpy(rep + cs2::CUserCmd::InputHistory::kRepElementsOffset + static_cast<std::ptrdiff_t>(current) * sizeof(entry), &entry, sizeof(entry));
                const int grown = current + 1;
                std::memcpy(field + fieldRelative(cs2::CUserCmd::InputHistory::kCurrentSizeOffset), &grown, sizeof(grown));
                ++allocated;
                std::memcpy(rep + cs2::CUserCmd::InputHistory::kRepAllocatedSizeOffset, &allocated, sizeof(allocated));
                return PublishResult::PublishedFastPath;
            }

            // Branch C - slot recycle on arena-less fields: nothing grows, the invisible
            // element underneath us gets overwritten (the reference leaks its destructor call
            // deliberately; the arena reclaims it).
            if (current != total && allocated == total) {
                void* const fieldArena = readArena();
                if (fieldArena == nullptr) {
                    std::memcpy(rep + cs2::CUserCmd::InputHistory::kRepElementsOffset + static_cast<std::ptrdiff_t>(current) * sizeof(entry), &entry, sizeof(entry));
                    const int grown = current + 1;
                    std::memcpy(field + fieldRelative(cs2::CUserCmd::InputHistory::kCurrentSizeOffset), &grown, sizeof(grown));
                    return PublishResult::PublishedSlotRecycle;
                }
            }
            // Branch A - rep missing OR fully consumed: only the game's own reserve+append may
            // proceed, never hand-rolled growth.
        }

        const auto result = addAllocated(field, entry);
        if (!result)
            return PublishResult::Refused;

        // Trust but verify - the game helper must have grown the visible count by one; anything
        // else means the resolver drifted onto a different function and the caller disarms.
        int after{};
        std::memcpy(&after, field + fieldRelative(cs2::CUserCmd::InputHistory::kCurrentSizeOffset), sizeof(after));
        return after == current + 1 ? PublishResult::PublishedByGame : PublishResult::Refused;
    }

private:
    [[nodiscard]] void* readArena() const noexcept
    {
        void* arena{nullptr};
        std::memcpy(&arena, field, sizeof(arena));
        return arena;
    }
    [[nodiscard]] static constexpr std::ptrdiff_t fieldRelative(std::ptrdiff_t absoluteCommandOffset) noexcept
    {
        return absoluteCommandOffset - cs2::CUserCmd::InputHistory::kFieldOffset;
    }

    static constexpr int kSaneTotalLimit = cs2::CUserCmd::kMaxInputHistoryEntries * 4;

    std::byte* field;
};
