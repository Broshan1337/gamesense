#pragma once

#include <algorithm>
#include <cstdint>
#include <cstddef>

#include <MemoryAllocation/UniquePtr.h>
#include <Platform/TypeInfoPrecedingVmt.h>
#include "VmtLength.h"

class VmtCopy {
public:
    VmtCopy(std::uintptr_t* vmt, VmtLength length) noexcept
        : originalVmt{ vmt }
        , replacementVmtWithTypeInfo{ mem::makeUniqueForOverwrite<std::uintptr_t[]>(static_cast<std::size_t>(length) + platform::lengthOfTypeInfoPrecedingVmt) }
    {
        // Zero first: if the computed length turns out shorter than a later-hooked slot, that slot
        // must be a deterministic nullptr call (SIGSEGV) - never uninitialized garbage (silent
        // wild jumps). Slots within the copied range are overwritten below anyway.
        if (replacementVmtWithTypeInfo)
            std::fill_n(replacementVmtWithTypeInfo.get(), lengthWithTypeInfo(), std::uintptr_t{0});
        copyOriginalVmt();
    }

    [[nodiscard]] std::uintptr_t* getReplacementVmt() const noexcept
    {
        if (replacementVmtWithTypeInfo) [[likely]]
            return replacementVmtWithTypeInfo.get() + platform::lengthOfTypeInfoPrecedingVmt;
        return nullptr;
    }

    [[nodiscard]] std::uintptr_t* getOriginalVmt() const noexcept
    {
        return originalVmt;
    }

    // Number of valid FUNCTION entries in this vtable copy (excludes the type-info prefix).
    [[nodiscard]] std::size_t getLength() const noexcept
    {
        return lengthWithTypeInfo() - platform::lengthOfTypeInfoPrecedingVmt;
    }

private:
    void copyOriginalVmt() const noexcept
    {
        if (replacementVmtWithTypeInfo) [[likely]]
            std::copy_n(originalVmt - platform::lengthOfTypeInfoPrecedingVmt, lengthWithTypeInfo(), replacementVmtWithTypeInfo.get());
    }

    [[nodiscard]] std::size_t lengthWithTypeInfo() const noexcept
    {
        return replacementVmtWithTypeInfo.get_deleter().getNumberOfElements();
    }

    std::uintptr_t* originalVmt;
    UniquePtr<std::uintptr_t[]> replacementVmtWithTypeInfo;
};
