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
#include <Utils/NsStr.h>
#include <Utils/StringBuilder.h>

namespace RetAddrSpoofer
{

































inline void* leaveRet = nullptr; 
inline bool verified = false;    

#if IS_LINUX() && defined(GCC)
#define OSIRIS_HAS_RETADDR_SPOOFER 1

namespace detail
{





extern "C" {
[[gnu::visibility("hidden")]] extern void* retAddrSpoofGadget;
}

extern "C" void retAddrSpoofEntry(); 

__attribute__((noinline)) inline int selfTestTarget(int a, int b) noexcept
{
    return a * 10 + b;
}
} 



__asm__(
    ".bss\n"
    ".weak retAddrSpoofGadget\n"
    ".hidden retAddrSpoofGadget\n"
    ".type retAddrSpoofGadget, @object\n"
    ".size retAddrSpoofGadget, 8\n"
    ".align 8\n"
    "retAddrSpoofGadget:\n"
    "    .zero 8\n"
    ".text\n"
    ".weak retAddrSpoofEntry\n"
    ".hidden retAddrSpoofEntry\n"
    ".type retAddrSpoofEntry, @function\n"
    "retAddrSpoofEntry:\n"
    "    popq %rax\n"          
    "    movq %rdi, %r10\n"    
    "    movq %rsi, %rdi\n"    
    "    movq %rdx, %rsi\n"    
    "    movq %rcx, %rdx\n"    
    "    movq %r8,  %rcx\n"    
    "    movq %r9,  %r8\n"     
    "    pushq %rax\n"         
    "    pushq %rax\n"         
    "    movq retAddrSpoofGadget@GOTPCREL(%rip), %r11\n"
    "    movq (%r11), %r11\n"  
    "    pushq %r11\n"         
    "    jmpq *%r10\n"         
);
#endif

template <typename Ret = void, typename... Args>
Ret invoke(void* method, Args... args) noexcept
{
#ifdef OSIRIS_HAS_RETADDR_SPOOFER
    if constexpr (sizeof...(Args) <= 5) {
        if (detail::retAddrSpoofGadget) {
            auto* const shim = reinterpret_cast<Ret (*)(void*, Args...)>(reinterpret_cast<void*>(&detail::retAddrSpoofEntry));
            return shim(method, args...);
        }
    }
#endif
    
    
    return reinterpret_cast<Ret (*)(Args...)>(method)(args...);
}




template <typename T>
inline constexpr bool isSpoofableFunction = false;
template <typename Ret, typename... Args>
inline constexpr bool isSpoofableFunction<Ret(Args...)> = true;








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
} 




template <typename FunctionPointer>
    requires detail::FunctionSignature<FunctionPointer>::spoofable
[[nodiscard]] constexpr auto spoof(FunctionPointer target) noexcept
{
    return SpoofedInvoker<typename detail::FunctionSignature<FunctionPointer>::Type>{target};
}




template <typename T>
[[nodiscard]] auto wrapSpoofed(T raw) noexcept
{
    if constexpr (detail::FunctionSignature<T>::spoofable)
        return SpoofedInvoker<typename detail::FunctionSignature<T>::Type>{raw};
    else
        return raw;
}




inline void init(std::span<const std::byte> executableSection) noexcept
{
#ifdef OSIRIS_HAS_RETADDR_SPOOFER
    static constexpr std::array gadgetBytes{ '\x59', '\xC3' }; 

    HybridPatternFinder finder{executableSection, BytePattern{std::string_view{gadgetBytes.data(), gadgetBytes.size()}}};
    if (const auto* gadget = finder.findNextOccurrence()) {
        detail::retAddrSpoofGadget = const_cast<std::byte*>(gadget);
        leaveRet = detail::retAddrSpoofGadget;
    } else {
        StringBuilderStorage<200> storage;
        auto builder = storage.builder();
        builder.put("Return address spoofer: couldn't find a 'pop rcx; ret' gadget in the client's "
                    "executable section, spoofed invocations are disabled.");
        NS_STR(spoofBrand, "Neversnooze");
        SimpleMessageBox{}.showWarning(spoofBrand, builder.cstring());
        return;
    }

    
    volatile int a = 12, b = 34;
    const int expected = detail::selfTestTarget(a, b);
    verified = (invoke<int>(reinterpret_cast<void*>(detail::selfTestTarget), a, b) == expected);

    
    
    if (!verified) {
        detail::retAddrSpoofGadget = nullptr;
        leaveRet = nullptr;
    }
#endif
}

} 
