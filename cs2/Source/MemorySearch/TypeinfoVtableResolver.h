#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include <Platform/Linux/LinuxPlatformApi.h>
#include <Utils/MemorySection.h>











namespace typeinfo_vtable
{

struct VtableCandidate {
    std::uintptr_t addressPoint{};
    std::uintptr_t firstFunction{};
};

[[nodiscard]] inline bool findQwords(const MemorySection& section, std::uintptr_t value, void (*sink)(std::uintptr_t, void*), void* userdata) noexcept
{
    const auto bytes = section.raw();
    const auto sectionBase = reinterpret_cast<std::uintptr_t>(bytes.data());
    bool found = false;
    if (bytes.size() < sizeof(std::uintptr_t))
        return false;
    for (std::size_t i = 0; i + sizeof(std::uintptr_t) <= bytes.size(); i += alignof(std::uintptr_t)) {
        std::uintptr_t candidate{};
        std::memcpy(&candidate, bytes.data() + i, sizeof(candidate));
        if (candidate == value) {
            sink(sectionBase + i, userdata);
            found = true;
        }
    }
    return found;
}

struct CollectFirst {
    std::uintptr_t* out;
    int remaining;
};

[[nodiscard]] inline int countAndCollect(const MemorySection& section, std::uintptr_t value, std::uintptr_t* out, int maxOut) noexcept
{
    const auto bytes = section.raw();
    const auto sectionBase = reinterpret_cast<std::uintptr_t>(bytes.data());
    int count = 0;
    if (bytes.size() < sizeof(std::uintptr_t))
        return 0;
    for (std::size_t i = 0; i + sizeof(std::uintptr_t) <= bytes.size(); i += alignof(std::uintptr_t)) {
        std::uintptr_t candidate{};
        std::memcpy(&candidate, bytes.data() + i, sizeof(candidate));
        if (candidate != value)
            continue;
        if (count < maxOut)
            out[count] = sectionBase + i;
        ++count;
    }
    return count;
}




[[nodiscard]] inline std::optional<VtableCandidate> findPrimaryVtable(const MemorySection& rodata, const MemorySection& dataRelRo, const MemorySection& text, const char* className) noexcept
{
    if (!className)
        return {};

    
    char nameBuf[64];
    const auto nameLen = std::strlen(className);
    if (nameLen == 0 || nameLen >= sizeof(nameBuf) - 4)
        return {};
    if (nameLen < 10) {
        nameBuf[0] = static_cast<char>('0' + nameLen);
        std::memcpy(nameBuf + 1, className, nameLen);
        nameBuf[1 + nameLen] = '\0';
    } else {
        const auto tens = static_cast<char>('0' + nameLen / 10);
        const auto ones = static_cast<char>('0' + nameLen % 10);
        nameBuf[0] = tens;
        nameBuf[1] = ones;
        std::memcpy(nameBuf + 2, className, nameLen);
        nameBuf[2 + nameLen] = '\0';
    }
    const auto nameBufLen = (nameLen < 10 ? 1 : 2) + nameLen;

    const auto rodataBytes = rodata.raw();
    std::optional<std::uintptr_t> nameAddress;
    for (std::size_t i = 0; i + nameBufLen + 1 <= rodataBytes.size(); ++i) {
        if (std::memcmp(rodataBytes.data() + i, nameBuf, nameBufLen + 1) == 0) {
            nameAddress = reinterpret_cast<std::uintptr_t>(rodataBytes.data()) + i;
            break;
        }
    }
    if (!nameAddress)
        return {};

    
    std::uintptr_t ztiNameSlots[4]{};
    const auto ztiNameSlotCount = countAndCollect(dataRelRo, *nameAddress, ztiNameSlots, 4);
    if (ztiNameSlotCount == 0)
        return {};

    
    for (int z = 0; z < ztiNameSlotCount && z < 4; ++z) {
        const auto zti = ztiNameSlots[z] - sizeof(void*);
        std::uintptr_t typeInfoSlots[8]{};
        const auto slotCount = countAndCollect(dataRelRo, zti, typeInfoSlots, 8);
        for (int s = 0; s < slotCount && s < 8; ++s) {
            const auto addressPoint = typeInfoSlots[s] + sizeof(void*);
            if (!dataRelRo.contains(addressPoint, sizeof(void*)))
                continue;
            std::uintptr_t firstFunction{};
            std::memcpy(&firstFunction, reinterpret_cast<const void*>(addressPoint), sizeof(firstFunction));
            if (text.contains(firstFunction)) {
                
                
                return VtableCandidate{addressPoint, firstFunction};
            }
        }
    }
    return {};
}

}




class VTableSlotPatch {
public:
    [[nodiscard]] bool install(std::uintptr_t slotAddress, std::uintptr_t replacement) noexcept
    {
        if (active || !slotAddress)
            return false;
        std::memcpy(&originalValue_, reinterpret_cast<const void*>(slotAddress), sizeof(originalValue_));
        if (originalValue_ == replacement)
            return true; 
        if (!writeSlot(slotAddress, replacement))
            return false;
        active = true;
        slotAddress_ = slotAddress;
        return true;
    }

    void restore() noexcept
    {
        if (!active)
            return;
        writeSlot(slotAddress_, originalValue_);
        active = false;
        slotAddress_ = 0;
    }

    [[nodiscard]] bool isActive() const noexcept { return active; }
    [[nodiscard]] std::uintptr_t original() const noexcept { return originalValue_; }

private:
    static bool writeSlot(std::uintptr_t slotAddress, std::uintptr_t value) noexcept
    {
        const auto pageStart = slotAddress & ~std::uintptr_t{0xFFF};
        if (LinuxPlatformApi::mprotect(reinterpret_cast<void*>(pageStart), 0x1000, PROT_READ | PROT_WRITE) != 0)
            return false;
        std::memcpy(reinterpret_cast<void*>(slotAddress), &value, sizeof(value));
        return LinuxPlatformApi::mprotect(reinterpret_cast<void*>(pageStart), 0x1000, PROT_READ) == 0;
    }

    std::uintptr_t originalValue_{0};
    std::uintptr_t slotAddress_{0};
    bool active{false};
};





class SingleBytePatch {
public:
    static constexpr std::uint8_t kRetOpcode = 0xC3;

    [[nodiscard]] bool install(std::uintptr_t functionAddress) noexcept
    {
        if (active || !functionAddress)
            return false;
        std::memcpy(&originalByte_, reinterpret_cast<const void*>(functionAddress), 1);
        if (originalByte_ == kRetOpcode)
            return true;
        if (!writeByte(functionAddress, kRetOpcode))
            return false;
        active = true;
        functionAddress_ = functionAddress;
        return true;
    }

    void restore() noexcept
    {
        if (!active)
            return;
        writeByte(functionAddress_, originalByte_);
        active = false;
        functionAddress_ = 0;
    }

    [[nodiscard]] bool isActive() const noexcept { return active; }

private:
    static bool writeByte(std::uintptr_t address, std::uint8_t value) noexcept
    {
        const auto pageStart = address & ~std::uintptr_t{0xFFF};
        
        
        
        
        if (LinuxPlatformApi::mprotect(reinterpret_cast<void*>(pageStart), 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
            return false;
        std::memcpy(reinterpret_cast<void*>(address), &value, 1);
        return LinuxPlatformApi::mprotect(reinterpret_cast<void*>(pageStart), 0x1000, PROT_READ | PROT_EXEC) == 0;
    }

    std::uint8_t originalByte_{0};
    std::uintptr_t functionAddress_{0};
    bool active{false};
};





namespace string_xref
{

[[nodiscard]] inline std::optional<std::uintptr_t> findFunctionStart(const MemorySection& literals, const MemorySection& text, const char* literal) noexcept
{
    if (!literal)
        return {};
    const auto literalLen = std::strlen(literal);

    const auto literalBytes = literals.raw();
    std::optional<std::uintptr_t> stringAddress;
    int occurrences = 0;
    for (std::size_t i = 0; i + literalLen + 1 <= literalBytes.size(); ++i) {
        if (std::memcmp(literalBytes.data() + i, literal, literalLen) == 0 && literalBytes[i + literalLen] == std::byte{0}) {
            stringAddress = reinterpret_cast<std::uintptr_t>(literalBytes.data()) + i;
            if (++occurrences > 1)
                break;
        }
    }
    if (!stringAddress || occurrences != 1)
        return {};

    
    const auto textBytes = text.raw();
    std::optional<std::uintptr_t> instructionStart;
    int leaSites = 0;
    for (std::size_t i = 0; i + 7 <= textBytes.size(); ++i) {
        const auto b0 = std::to_integer<unsigned char>(textBytes[i]);
        if (b0 != 0x48 && b0 != 0x4C)
            continue;
        const auto b1 = std::to_integer<unsigned char>(textBytes[i + 1]);
        const auto b2 = std::to_integer<unsigned char>(textBytes[i + 2]);
        if (b1 != 0x8D || (b2 & 0xC7) != 0x05)
            continue;
        std::int32_t displacement{};
        std::memcpy(&displacement, textBytes.data() + i + 3, sizeof(displacement));
        const auto nextInstruction = reinterpret_cast<std::uintptr_t>(textBytes.data()) + i + 7;
        if (static_cast<std::uintptr_t>(static_cast<std::int64_t>(nextInstruction) + displacement) == *stringAddress) {
            instructionStart = reinterpret_cast<std::uintptr_t>(textBytes.data()) + i;
            if (++leaSites > 1)
                break;
        }
    }
    if (!instructionStart || leaSites != 1)
        return {};

    
    constexpr std::size_t kMaxWalkback = 0x800;
    const auto offsetInText = text.offsetOf(*instructionStart);
    if (offsetInText < kMaxWalkback)
        return {};
    const auto walkRegion = textBytes.data() + offsetInText - kMaxWalkback;
    std::optional<std::size_t> prologueOffset;
    for (std::size_t back = 0; back + 4 <= kMaxWalkback; ++back) {
        const auto idx = kMaxWalkback - 4 - back; 
        if (std::memcmp(walkRegion + idx, "\x55\x48\x89\xE5", 4) == 0) {
            prologueOffset = idx;
            break;
        }
    }
    if (!prologueOffset)
        return {};
    return reinterpret_cast<std::uintptr_t>(textBytes.data()) + offsetInText - kMaxWalkback + *prologueOffset;
}

}
