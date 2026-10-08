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
    
    
    
    static constexpr std::size_t kMaxSlots = 512;
    static constexpr std::size_t kMaxConsecutiveNonCode = 3;
};














[[nodiscard]] inline VmtLength vmtCopyLength(const VmtLengthCalculator& calculator, const std::uintptr_t* vmt, std::size_t minSlots) noexcept
{
    const auto scanned = calculator(vmt);
    return static_cast<std::size_t>(scanned) >= minSlots ? scanned : VmtLength{ minSlots };
}
