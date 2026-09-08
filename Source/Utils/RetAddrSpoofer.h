#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include <MemorySearch/BytePattern.h>
#include <MemorySearch/HybridPatternFinder.h>
#include <Platform/Macros/IsCompiler.h>
#include <Platform/Macros/IsPlatform.h>
#include <Platform/SimpleMessageBox.h>
#include <Utils/StringBuilder.h>

namespace RetAddrSpoofer
{

// Return-address spoofer (signature-agnostic, naked-asm design).
//
// Goal: call a game function so that, WHILE it executes, the return address sitting on the stack
// points inside libclient.so instead of into our module's mapped memory. Stack walkers / tracebacks that
// sample the thread mid-call therefore see a game module address, not ours.
//
// Mechanism (no self-modifying code, nothing signature-specific):
//   invoke<Ret, Args...>(method, args...) tail-calls a single hand-written trampoline
//   (osirisSpoofEntry) typed as Ret(void* method, Args...). Because `method` is prepended as an
//   extra leading argument, the compiler shifts every INTEGER/pointer argument up by one integer
//   register (method->rdi, arg0->rsi, arg1->rdx, ...). Float args are unaffected (they go to xmm).
//   The trampoline shifts the integer registers back down by one (rsi->rdi, rdx->rsi, ...), which
//   exactly reconstructs the target's own register state for ANY mix of int/float args, then:
//       pop real_return                    ; the address in OUR module we must come back to
//       push real_return ; push pad ; push gadget   ; (16-byte aligned; gadget on top)
//       jmp method
//   The target sees its return address = `gadget`, a `pop rcx; ret` sequence INSIDE libclient.so.
//   When the target returns it lands on the gadget (a libclient address -> the spoof), which pops
//   the pad and returns to `real_return` with rax/rdx/xmm0 (the target's return value) untouched.
//
// Reentrancy / threads: all state lives on the stack, so nested and concurrent spoofed calls are
// fine. No per-signature codegen, no mprotect, no instruction scanning -> it cannot be broken by a
// signature the self-test never exercised (the failure mode of the old self-modifying trampoline).
//
// Restrictions:
//   - x86-64 System V ABI, GCC/Clang. Other targets transparently fall back to a plain call.
//   - Supports up to 5 integer/pointer arguments (method + 5 = 6 integer registers) plus up to 8
//     float arguments. A call with more integer args than fit in registers would need stack-arg
//     relocation the trampoline does not do, so invoke() falls back to a plain call when
//     sizeof...(Args) > 5 (conservative: never spoofs a call it cannot forward correctly).
//   - The gadget must be resolved by init() before the first spoofed invoke().

inline void* leaveRet = nullptr; // resolved gadget (`pop rcx; ret`); named leaveRet for compatibility
inline bool verified = false;    // set by init(): true once a spoofed self-test round-tripped

#if IS_LINUX() && defined(GCC)
#define OSIRIS_HAS_RETADDR_SPOOFER 1

namespace detail
{
// Gadget slot the trampoline reads. Storage is defined ONCE inside the file-scope asm block below
// (weak + zero-initialized); this extern declaration gives C++ code typed access to it. Defining
// the slot as a C++ inline variable with an asm name proved unreliable: nothing guaranteed the
// symbol the assembler references actually gets emitted, leading to undefined-reference link
// errors when more than one translation unit included this header.
extern "C" {
[[gnu::visibility("hidden")]] extern void* osirisSpoofGadget;
}

extern "C" void osirisSpoofEntry(); // defined in the file-scope asm block below

__attribute__((noinline)) inline int selfTestTarget(int a, int b) noexcept
{
    return a * 10 + b;
}
} // namespace detail

// The trampoline. `.weak` + `.hidden` so including this header in many TUs is fine (the linker keeps
// one copy) and the symbol is not exported. See the header comment for the register-shuffle rationale.
__asm__(
    ".bss\n"
    ".weak osirisSpoofGadget\n"
    ".hidden osirisSpoofGadget\n"
    ".type osirisSpoofGadget, @object\n"
    ".size osirisSpoofGadget, 8\n"
    ".align 8\n"
    "osirisSpoofGadget:\n"
    "    .zero 8\n"
    ".text\n"
    ".weak osirisSpoofEntry\n"
    ".hidden osirisSpoofEntry\n"
    ".type osirisSpoofEntry, @function\n"
    "osirisSpoofEntry:\n"
    "    popq %rax\n"          // rax = real return address (into our caller)
    "    movq %rdi, %r10\n"    // r10 = method (before we overwrite rdi)
    "    movq %rsi, %rdi\n"    // shift integer args down one register: arg0
    "    movq %rdx, %rsi\n"    //   arg1
    "    movq %rcx, %rdx\n"    //   arg2
    "    movq %r8,  %rcx\n"    //   arg3
    "    movq %r9,  %r8\n"     //   arg4  (r9 left as-is; target won't read a 6th int arg)
    "    pushq %rax\n"         // [real_return]  (highest of the three)
    "    pushq %rax\n"         // [pad]          (consumed by the gadget's `pop rcx`)
    "    movq osirisSpoofGadget@GOTPCREL(%rip), %r11\n"
    "    movq (%r11), %r11\n"  // r11 = gadget address (`pop rcx; ret` in libclient.so)
    "    pushq %r11\n"         // [gadget]       (top: the return address the target sees)
    "    jmpq *%r10\n"         // enter target (rsp%16==8); it returns via gadget -> pad -> real_return
);
#endif

template <typename Ret = void, typename... Args>
Ret invoke(void* method, Args... args) noexcept
{
#ifdef OSIRIS_HAS_RETADDR_SPOOFER
    if constexpr (sizeof...(Args) <= 5) {
        if (detail::osirisSpoofGadget) {
            auto* const shim = reinterpret_cast<Ret (*)(void*, Args...)>(reinterpret_cast<void*>(&detail::osirisSpoofEntry));
            return shim(method, args...);
        }
    }
#endif
    // Spoofer unavailable (unsupported target, gadget not resolved, or too many integer args):
    // degrade to a normal, unspoofed call.
    return reinterpret_cast<Ret (*)(Args...)>(method)(args...);
}

// True only for a plain (non C-variadic) function type Ret(Args...). C-variadic functions
// (Ret(Args..., ...), e.g. the chat printf) can't be forwarded through a template, so callers leave
// them as raw pointers.
template <typename T>
inline constexpr bool isSpoofableFunction = false;
template <typename Ret, typename... Args>
inline constexpr bool isSpoofableFunction<Ret(Args...)> = true;

// A callable that wraps a game function pointer and routes every call through invoke(), so a stack
// walk from inside the callee sees a return address in the game module. This is the choke point that
// gives velocity-style parity WITHOUT editing call sites: AllMemoryPatternSearchResults::get()
// returns one of these for any spoofable function-pointer pattern, and existing `fn(args)` / `if
// (fn)` code keeps working. Trivially copyable; holds one pointer. Deliberately NO implicit
// conversion back to the raw pointer, so a call can never silently bypass the spoof - use
// rawAddress() for the rare identity/anchor check (e.g. reading a byte at the function's address).
template <typename Signature>
struct SpoofedInvoker;

template <typename Ret, typename... Args>
struct SpoofedInvoker<Ret(Args...)> {
    Ret (*target)(Args...);

    [[nodiscard]] explicit operator bool() const noexcept { return target != nullptr; }
    [[nodiscard]] const void* rawAddress() const noexcept { return reinterpret_cast<const void*>(target); }

    Ret operator()(Args... args) const noexcept
    {
        return invoke<Ret>(reinterpret_cast<void*>(target), args...);
    }
};

namespace detail
{
template <typename T>
struct FunctionSignature {
    static constexpr bool spoofable = false;
};
template <typename Ret, typename... Args>
struct FunctionSignature<Ret (*)(Args...)> {
    using Type = Ret(Args...);
    static constexpr bool spoofable = isSpoofableFunction<Ret(Args...)>;
};
} // namespace detail

// Builds a SpoofedInvoker for a raw game-function pointer: `spoof(fn)(args...)`. Use this for
// calls that bypass the pattern layer (vtable-slot lookups, hook originals). Only valid for plain
// (non-variadic) function pointers; everything else is a compile error at this boundary.
template <typename FunctionPointer>
    requires detail::FunctionSignature<FunctionPointer>::spoofable
[[nodiscard]] constexpr auto spoof(FunctionPointer target) noexcept
{
    return SpoofedInvoker<typename detail::FunctionSignature<FunctionPointer>::Type>{target};
}

// Wraps a raw pattern-search result: a spoofable function pointer becomes a SpoofedInvoker (every
// call routed through the spoofer); anything else (data pointers, offsets, C-variadic functions) is
// returned unchanged.
template <typename T>
[[nodiscard]] auto wrapSpoofed(T raw) noexcept
{
    if constexpr (detail::FunctionSignature<T>::spoofable)
        return SpoofedInvoker<typename detail::FunctionSignature<T>::Type>{raw};
    else
        return raw;
}

// Resolves the `pop rcx; ret` gadget from libclient.so's executable section and verifies the whole
// path with a round-trip through invoke(). Call it once the client library is loaded; afterwards
// invoke() is fully usable.
inline void init(std::span<const std::byte> executableSection) noexcept
{
#ifdef OSIRIS_HAS_RETADDR_SPOOFER
    static constexpr std::array gadgetBytes{ '\x59', '\xC3' }; // pop rcx; ret

    HybridPatternFinder finder{executableSection, BytePattern{std::string_view{gadgetBytes.data(), gadgetBytes.size()}}};
    if (const auto* gadget = finder.findNextOccurrence()) {
        detail::osirisSpoofGadget = const_cast<std::byte*>(gadget);
        leaveRet = detail::osirisSpoofGadget;
    } else {
        StringBuilderStorage<200> storage;
        auto builder = storage.builder();
        builder.put("Return address spoofer: couldn't find a 'pop rcx; ret' gadget in the client's "
                    "executable section, spoofed invocations are disabled.");
        SimpleMessageBox{}.showWarning("Neversnooze", builder.cstring());
        return;
    }

    // End-to-end self test: a spoofed call must come back into our frame with the correct value.
    volatile int a = 12, b = 34;
    const int expected = detail::selfTestTarget(a, b);
    verified = (invoke<int>(reinterpret_cast<void*>(detail::selfTestTarget), a, b) == expected);

    // If the round-trip did not come back intact the mechanism is unusable on this machine -
    // disable it so every invoke() degrades to a plain call instead of corrupting execution.
    if (!verified) {
        detail::osirisSpoofGadget = nullptr;
        leaveRet = nullptr;
    }
#endif
}

} // namespace RetAddrSpoofer
