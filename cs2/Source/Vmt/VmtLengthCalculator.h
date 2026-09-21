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

    [[nodiscard]] VmtLength operator()(const std::uintptr_t* vmt) const noexcept
    {
        std::size_t length = 0;
        while (isVmtEntry(vmt + length))
            ++length;
        return VmtLength{ length };
    }

private:
    [[nodiscard]] bool isVmtEntry(const std::uintptr_t* pointer) const noexcept
    {
        return vmtSection.contains(std::uintptr_t(pointer), sizeof(std::uintptr_t)) && codeSection.contains(*pointer);
    }

    MemorySection codeSection;
    MemorySection vmtSection;
};

// The scan above counts only CONTIGUOUS .text entries - and multi-inheritance classes pack
// secondary-base vtable headers (offset-to-top / typeinfo) into the same region, so the scan can
// truncate far below the highest slot the game actually indexes (measured on CCSGOInput: the
// scan yields ~2 while CreateMove lives at slot 26 - every hook beyond the truncation used to be
// an out-of-bounds pool write, silently "working"). The game reads vtables POSITIONALLY, so the
// replacement copy must cover the highest hooked slot: pass maxHookedSlot + 1.
// Not constexpr: the calculator's scan reads runtime section data. (Kept inline: the constexpr
// it replaces was the only thing making this header definition linkable from several TUs.)
[[nodiscard]] inline VmtLength vmtCopyLength(const VmtLengthCalculator& calculator, const std::uintptr_t* vmt, std::size_t minSlots) noexcept
{
    const auto scanned = calculator(vmt);
    return static_cast<std::size_t>(scanned) >= minSlots ? scanned : VmtLength{ minSlots };
}
