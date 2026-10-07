#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <imgui.h>

#include <CS2/Classes/IMemAlloc.h>
#include <GameClient/DLLs/Tier0Dll.h>
#include <Platform/Macros/FunctionAttributes.h>
#include <Utils/RetAddrSpoofer.h>



























class ImGuiMemAllocBridge {
public:
    [[nodiscard]] static bool install() noexcept
    {
        const Tier0Dll tier0;
        thisptr = tier0.memAlloc();
        allocSlotOffset = slotFromWrapper(tier0, "MemAlloc_AllocFunc");
        freeSlotOffset = slotFromWrapper(tier0, "MemAlloc_FreeFunc");
        if (!thisptr || !allocSlotOffset || !freeSlotOffset)
            return false;

        ImGui::SetAllocatorFunctions(&allocate, &deallocate);
        return true;
    }

    
    
    [[nodiscard]] static void* allocateRaw(std::size_t size) noexcept
    {
        return allocate(size, nullptr);
    }

    static void deallocateRaw(void* memory) noexcept
    {
        deallocate(memory, nullptr);
    }

    
    
    
    
    [[nodiscard]] static std::size_t slotFromCodeBytes(const std::byte* code, std::size_t size) noexcept
    {
        for (std::size_t i = 0; i + 2 < size; ++i) {
            const auto b0 = static_cast<unsigned char>(code[i]);
            const auto b1 = static_cast<unsigned char>(code[i + 1]);
            if (b0 != 0xFF || (b1 != 0x60 && b1 != 0xA0))
                continue;

            if (b1 == 0x60)
                return static_cast<unsigned char>(code[i + 2]);
            if (i + 5 < size) {
                std::uint32_t disp = 0;
                std::memcpy(&disp, &code[i + 2], sizeof(disp));
                return disp;
            }
        }
        return 0;
    }

    
    inline static constinit std::size_t allocSlotOffset{0};
    inline static constinit std::size_t freeSlotOffset{0};

private:
    
    
    [[nodiscard]] static std::size_t slotFromWrapper(const Tier0Dll& tier0, const char* wrapperName) noexcept
    {
        const auto wrapper = tier0.getFunctionAddress(wrapperName);
        if (!wrapper)
            return 0;

        std::byte code[32];
        std::memcpy(code, wrapper.as<const void*>(), sizeof(code));
        return slotFromCodeBytes(code, sizeof(code));
    }

    [[NOINLINE]] static void* allocate(std::size_t size, void* ) noexcept
    {
        if (!thisptr || !*thisptr)
            return nullptr;

        const auto fn = *reinterpret_cast<cs2::IMemAlloc::Alloc**>(reinterpret_cast<std::uintptr_t>((*thisptr)->vmt) + allocSlotOffset);
        if (!fn)
            return nullptr;

        return RetAddrSpoofer::spoof(fn)(*thisptr, size);
    }

    [[NOINLINE]] static void deallocate(void* memory, void* ) noexcept
    {
        if (!memory || !thisptr || !*thisptr)
            return;

        const auto fn = *reinterpret_cast<cs2::IMemAlloc::Free**>(reinterpret_cast<std::uintptr_t>((*thisptr)->vmt) + freeSlotOffset);
        if (!fn)
            return;

        RetAddrSpoofer::spoof(fn)(*thisptr, memory);
    }

    inline static constinit cs2::IMemAlloc** thisptr{nullptr};
};
