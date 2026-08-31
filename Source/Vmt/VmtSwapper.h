#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "VmtCopy.h"
#include "VmtLengthCalculator.h"

#include <Utils/GenericFunctionPointer.h>
#include <Utils/StatusReport.h>

class VmtSwapper {
public:
    [[nodiscard]] bool wasEverInstalled() const noexcept
    {
        return vmtCopy.has_value();
    }

    [[nodiscard]] bool isInstalled(const std::uintptr_t* vmt) const noexcept
    {
        assert(wasEverInstalled());
        return vmt == vmtCopy->getReplacementVmt();
    }

    // minSlots = highest hooked slot + 1 (see vmtCopyLength: the length scan alone truncates on
    // multi-inheritance composite vtables, and the game indexes the table positionally).
    bool install(const VmtLengthCalculator& vmtLengthCalculator, std::uintptr_t*& vmt, std::size_t minSlots) noexcept
    {
        const auto justInitialized = initializeVmtCopy(vmtLengthCalculator, vmt, minSlots);
        if (const auto replacementVmt = vmtCopy->getReplacementVmt())
            vmt = replacementVmt;
        return justInitialized;
    }

    void uninstall(std::uintptr_t*& vmt) const noexcept
    {
        assert(wasEverInstalled());
        vmt = vmtCopy->getOriginalVmt();
    }

    [[nodiscard]] GenericFunctionPointer hook(std::size_t index, GenericFunctionPointer replacementFunction) const noexcept
    {
        assert(wasEverInstalled());
        if (const auto replacementVmt = vmtCopy->getReplacementVmt()) {
            // Belt-and-braces against a minSlots mistake: better a loud disabled hook than a
            // pool-corrupting write. The original is returned as null so the failure is visible
            // in a crash log instead of silently calling a slot we never copied.
            if (index >= vmtCopy->getLength()) [[unlikely]] {
                StatusReport::record("VmtSwapper: hook slot beyond copy length - hook disabled", false);
                return GenericFunctionPointer{static_cast<void(*)()>(nullptr)};
            }
            replacementVmt[index] = std::uintptr_t(static_cast<void(*)()>(replacementFunction));
        }
        return reinterpret_cast<void(*)()>(vmtCopy->getOriginalVmt()[index]);
    }

private:
    [[nodiscard]] bool initializeVmtCopy(const VmtLengthCalculator& vmtLengthCalculator, std::uintptr_t* vmt, std::size_t minSlots) noexcept
    {
        if (!vmtCopy.has_value()) {
            vmtCopy.emplace(vmt, vmtCopyLength(vmtLengthCalculator, vmt, minSlots));
            return true;
        }
        return false;
    }

    std::optional<VmtCopy> vmtCopy;
};
