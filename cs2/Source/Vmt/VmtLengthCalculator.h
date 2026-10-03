#pragma once

#include <cstddef>
#include <cstdint>

#include "VmtLength.h"
#include <Utils/MemorySection.h>

struct VmtLengthCalculator {
    explicit VmtLengthCalculator(MemorySection codeSection, MemorySection vmtSection)
        : codeSection{ codeSection }, vmtSection{ vmtSection }
    {
    }

    // NULL/HEADER-TOLERANT SCAN (hardening 2026-09-24 - the CNetworkMessages injection crash):
    // the old scan stopped at the FIRST non-code entry. The 1.41.8.x+ game builds place NULL
    // slots and secondary-base headers (offset-to-top / typeinfo pairs) INSIDE the composite
    // vtable region, and the game indexes the table POSITIONALLY far past them - measured live:
    // CNetworkMessages' manager is dispatched at [vtable+0x2B0] (slot 86) while the scan
    // truncated at slot 40 (a NULL at slot 40). The VmtCopy clone then covered only 40 slots
    // and every manager dispatch beyond it read OUT OF THE POOL ALLOCATION (wild call -> the
    // libnetworksystem+0x2AFBF0 null-copy crash).
    //
    // New semantics: walk up to kMaxSlots; NULL slots and header pairs are SKIPPED (they are
    // part of the positional layout), the length = LAST code entry index + 1. The scan stops
    // after kMaxConsecutiveNonCode consecutive non-code entries (the inter-class gap) or at
    // the vmt section end. The copy stays byte-faithful over the whole span, so dispatch
    // through the clone is identical to the unhooked table for every slot the game indexes.
    [[nodiscard]] VmtLength operator()(const std::uintptr_t* vmt) const noexcept
    {
        std::size_t length = 0;
        std::size_t consecutiveNonCode = 0;
        for (std::size_t i = 0; i < kMaxSlots; ++i) {
            if (!vmtSection.contains(std::uintptr_t(vmt + i), sizeof(std::uintptr_t)))
                break;
            if (codeSection.contains(vmt[i])) {
                length = i + 1;
                consecutiveNonCode = 0;
            } else if (++consecutiveNonCode >= kMaxConsecutiveNonCode) {
                break;
            }
        }
        return VmtLength{ length };
    }

    MemorySection codeSection;
    MemorySection vmtSection;

private:
    // Composite vtables of the net/UI classes measure 100-150 slots live; 512 is a hard cap
    // (4KB clone from the module pool - trivial). 3 consecutive non-code entries = the gap
    // between classes' composite regions (a secondary-base header pair is only 2).
    static constexpr std::size_t kMaxSlots = 512;
    static constexpr std::size_t kMaxConsecutiveNonCode = 3;
};

// The scan (2026-09-24 rework) is NULL/HEADER-TOLERANT: it walks the vmt section and counts up
// to the LAST code entry, skipping null slots and secondary-base headers (offset-to-top /
// typeinfo pairs) instead of truncating at the first non-code entry. Rationale, paid for in
// live crashes: the game reads vtables POSITIONALLY well past "logical" table ends and past
// nulls (measured on CCSGOInput: scan ~2 while CreateMove lives at slot 26; and on
// INetworkMessageInternal the 2026-09-23 build put a NULL at slot 0 - the old scan computed 0,
// the clone floored to 5 slots, and the game's slot-86 dispatch read ~648 bytes out of bounds
// of the pool allocation -> wild call -> SIGSEGV). A run of 3 consecutive non-code entries ends
// the scan (real end-of-composite heuristic); the copy stays byte-faithful over the whole span,
// so dispatch through the clone is identical to the unhooked table for every slot it covers.
// minSlots remains the floor guarantee for the highest hooked slot: pass maxHookedSlot + 1.
// Not constexpr: the calculator's scan reads runtime section data. (Kept inline: the constexpr
// it replaces was the only thing making this header definition linkable from several TUs.)
[[nodiscard]] inline VmtLength vmtCopyLength(const VmtLengthCalculator& calculator, const std::uintptr_t* vmt, std::size_t minSlots) noexcept
{
    const auto scanned = calculator(vmt);
    return static_cast<std::size_t>(scanned) >= minSlots ? scanned : VmtLength{ minSlots };
}
