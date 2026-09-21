// ============================================================================
// scratch → wire-bytes serialiser — implementation.
// ============================================================================
#include "scratch_serialize.h"
#include "capture_buffer.h"
#include "../core/signature_scanner.h"
#include "../schema/cbaseusercmdpb_layout.h"
#include "../version_manifest.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

namespace fva::scratch_serialize
{

namespace {

// void __fastcall(void* this, uint8_t* buffer, unsigned int size)
using SerializePartialFn = void(__fastcall*)(void*, std::uint8_t*, unsigned int);

// size_t __fastcall(const void* this) — vtable slot at +0x38
using ByteSizeLongFn = std::size_t(__fastcall*)(const void*);

std::atomic<SerializePartialFn> g_serialize_partial{nullptr};

// Anchor for CS2's MessageLite::SerializePartialToArray implementation.
// Depot 24134959 prologue (new hotpatch-friendly entry):
//   mov [rsp+18h], rbx               ; 48 89 5C 24 18
//   push rbp, rsi, rdi               ; 55 56 57
//   sub rsp, 90h                     ; 48 81 EC 90 00 00 00
//   mov rax, [rip + __security_cookie] ; 48 8B 05 ? ? ? ?
//   xor rax, rsp                     ; 48 33 C4
constexpr std::string_view k_sig_serialize_partial =
    "48 89 5C 24 18 55 56 57 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4";

} // namespace

// CS2 ArenaStringPtr::Set — see version_manifest.h for details.
using ArenaStringSetFn = void(__fastcall*)(void*, const void*, void*);
std::atomic<ArenaStringSetFn> g_arena_string_set{nullptr};

constexpr std::string_view k_sig_arena_string_set =
    "48 89 5C 24 20 57 48 83 EC 30 49 8B C0 48 8B DA 48 8B F9 48 8B 09 F6 C1 03";

bool init()
{
    HMODULE client = ::GetModuleHandleW(fva::version::module_names::client_dll);
    if (!client) return false;

    namespace bf = fva::version::build_fingerprint;
    auto* client_base = reinterpret_cast<std::uint8_t*>(client);

    // Try sig-scan first, then fall back to manifest RVA.  Validate the
    // fingerprint AFTER each attempt — if sig-scan returns a hit that fails
    // the check (matched a different function with similar prologue on this
    // depot), retry via known RVA rather than giving up.
    auto try_target = [&](std::uint8_t* p) -> bool {
        if (!p) return false;
        if (std::memcmp(p, bf::kSerializePartialPrologue.data(),
                          bf::kSerializePartialPrologue.size()) != 0)
            return false;
        g_serialize_partial.store(reinterpret_cast<SerializePartialFn>(p),
                                     std::memory_order_release);
        return true;
    };

    const bool serialize_ok =
        try_target(static_cast<std::uint8_t*>(
            fva::scanner::find_in_module(client, k_sig_serialize_partial))) ||
        try_target(client_base +
                   fva::version::known_rva::serialize_partial_to_array);
    if (!serialize_ok) return false;

    // Resolve ArenaStringPtr::Set (for FVA 1:1 move_crc write path).
    // Prefer sig-scan; fall back to manifest RVA.  Both routes verified
    // against the arena-tag-check prologue.
    if (auto* p = fva::scanner::find_in_module(client, k_sig_arena_string_set)) {
        g_arena_string_set.store(reinterpret_cast<ArenaStringSetFn>(p),
                                    std::memory_order_release);
        std::printf("[fva_recon] arena_string_set @ %p (sig-scan)\n", p);
    } else {
        auto* fallback = client_base +
                          fva::version::known_rva::arena_string_ptr_set;
        g_arena_string_set.store(reinterpret_cast<ArenaStringSetFn>(fallback),
                                    std::memory_order_release);
        std::printf("[fva_recon] arena_string_set @ %p (RVA fallback)\n",
                     fallback);
    }
    return true;
}

// Public accessor for the resolved ArenaStringPtr::Set — used by
// create_move_hook to write into pb->move_crc the FVA way instead of
// std::string::assign.
void* get_arena_string_set() noexcept
{
    return reinterpret_cast<void*>(
        g_arena_string_set.load(std::memory_order_acquire));
}

bool serialize(std::string& out)
{
    auto& scratch = fva::capture_buffer::g_scratch;
    if (!scratch.vtable) return false;

    auto ser = g_serialize_partial.load(std::memory_order_acquire);
    if (!ser) return false;

    // ByteSizeLong via vtable[+0x38] (index 7 for 8-byte slots).
    auto vtbl = reinterpret_cast<void**>(scratch.vtable);
    auto bytesize_fn = reinterpret_cast<ByteSizeLongFn>(
        vtbl[fva::version::protobuf::vtbl_bytesize_long / sizeof(void*)]);

    const std::size_t size = bytesize_fn(&scratch);
    if (!size || size > (1u << 30)) return false;

    out.resize(size);
    ser(&scratch,
         reinterpret_cast<std::uint8_t*>(out.data()),
         static_cast<unsigned int>(size));
    return true;
}

bool ready() noexcept
{
    return g_serialize_partial.load(std::memory_order_acquire) != nullptr &&
           fva::capture_buffer::g_scratch.vtable != nullptr;
}

} // namespace fva::scratch_serialize
