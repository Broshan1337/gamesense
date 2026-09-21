#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <imgui.h>

#include <CS2/Classes/IMemAlloc.h>
#include <GameClient/DLLs/Tier0Dll.h>
#include <Platform/Macros/FunctionAttributes.h>
#include <Utils/RetAddrSpoofer.h>

// Bridges ImGui's allocations onto CS2's IMemAlloc (tier0 `g_pMemAlloc`).
//
// ImGui core (with IMGUI_DISABLE_DEFAULT_ALLOCATORS, set on the imgui CMake target) allocates
// exclusively through the two function pointers handed to ImGui::SetAllocatorFunctions, so
// routing them here keeps every menu allocation inside the game's own heap allocator - no libc
// malloc footprint appears for our UI, matching how the rest of this library gets memory from
// the game. The bridge must be installed BEFORE ImGui::CreateContext(); GUI::init owns that
// ordering and treats a false return as "menu unavailable this session" (fail closed).
//
// The IMemAlloc vtable slots are NOT hardcoded. tier0 exports thin wrappers that tail-jump
// through the interface vtable, and the jump displacement IS the slot byte offset:
//
//   MemAlloc_AllocFunc:  ... ff 60 10   jmp *0x10(%rax)   -> Alloc = slot 0x10
//   MemAlloc_FreeFunc:   ... ff 60 20   jmp *0x20(%rax)   -> Free  = slot 0x20
//
// (verified against the current libtier0.so; GetSize is the same trick at 0x90). Reading the
// displacement out of the wrapper's code at runtime keeps the bridge correct across tier0
// reshuffles. This is the tier0-side twin of the libclient call-site pattern the existing
// MemAlloc<HookContext> uses (MemoryPatterns/Linux/MemAllocPatternsLinux.h resolves the same
// 0x10 slot from `call qword [rax + disp]` sites); either can serve as a cross-check of the
// other during in-game verification.
//
// All resolved game calls route through RetAddrSpoofer (project invariant), mirroring
// MemAlloc::allocate(). RetAddrSpoofer::init() runs before GUI::init, so the gadget is always
// resolved by the time an allocation can happen - and if it ever were not, invoke() degrades to
// an unspoofed call rather than failing.
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

    // Raw allocation through the bridged IMemAlloc - for handing ImGui-owned buffers (fonts)
    // memory that ImGui can later free through the same allocator.
    [[nodiscard]] static void* allocateRaw(std::size_t size) noexcept
    {
        return allocate(size, nullptr);
    }

    static void deallocateRaw(void* memory) noexcept
    {
        deallocate(memory, nullptr);
    }

    // Extracts the `jmp *[vtable + displacement]` displacement from a tier0 wrapper's code
    // bytes. The displacement is the byte offset of the wrapped method's slot inside the
    // IMemAlloc vtable. Returns 0 when no jump-through-vtable shape is recognized. Exposed for
    // unit tests (ImGuiMemAllocBridgeTests).
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

    // Resolved slot offsets (byte offsets into the IMemAlloc vtable), public for diagnostics.
    inline static constinit std::size_t allocSlotOffset{0};
    inline static constinit std::size_t freeSlotOffset{0};

private:
    // tier0's wrappers are 19-28 bytes; scanning the first 32 covers every shape seen so far
    // (disp8 `ff 60 XX` today, disp32 `ff a0 XX XX XX XX` kept for forward compatibility).
    [[nodiscard]] static std::size_t slotFromWrapper(const Tier0Dll& tier0, const char* wrapperName) noexcept
    {
        const auto wrapper = tier0.getFunctionAddress(wrapperName);
        if (!wrapper)
            return 0;

        std::byte code[32];
        std::memcpy(code, wrapper.as<const void*>(), sizeof(code));
        return slotFromCodeBytes(code, sizeof(code));
    }

    [[NOINLINE]] static void* allocate(std::size_t size, void* /*user data*/) noexcept
    {
        if (!thisptr || !*thisptr)
            return nullptr;

        const auto fn = *reinterpret_cast<cs2::IMemAlloc::Alloc**>(reinterpret_cast<std::uintptr_t>((*thisptr)->vmt) + allocSlotOffset);
        if (!fn)
            return nullptr;

        return RetAddrSpoofer::spoof(fn)(*thisptr, size);
    }

    [[NOINLINE]] static void deallocate(void* memory, void* /*user data*/) noexcept
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
