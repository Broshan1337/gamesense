// ============================================================================
// scratch → wire-bytes serialiser.
//
// Reproduces FVA `sub_7FFBE80C989C` (ScratchToString) using CS2's own
// SerializePartialToArray implementation from client.dll rather than
// carrying a private protobuf copy.
//
// Uses:
//   - vtable[+0x38] on scratch  → ByteSizeLong (virtual, always present)
//   - a sig-scanned direct pointer to client.dll's
//     `MessageLite::SerializePartialToArray` implementation
//     (RVA 0x1189A60 on legacy depot 2347770, resolved via anchor
//     "48 83 EC 78 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 60 44 0F B6 0D
//      ? ? ? ? 49 63 C0 4C 8D 44 24 20"; live-verified 2026-07-10 as unique)
//
// The scratch instance is filled by Hook C on every outgoing SerializeToArray
// call; its vtable is cached from the source cmd on the first match — see
// capture_buffer::cache_vtable_from.  Hook A then calls serialize() to
// obtain the byte string used to regenerate base->move_crc.
// ============================================================================
#pragma once

#include <string>

namespace fva::scratch_serialize
{

// One-shot init — sig-scan client.dll for SerializePartialToArray.
// Returns true if the direct serializer address was resolved.
bool init();

// Serialize fva::capture_buffer::g_scratch into `out`.
// Returns false if either the vtable isn't cached yet (no Hook C fire has
// happened) or init() wasn't successful.
bool serialize(std::string& out);

// Diagnostic — is init() done and vtable cached?  Used by Hook A to skip
// the move_crc rewrite gracefully during the first tick before Hook C has
// fired.
[[nodiscard]] bool ready() noexcept;

// FVA 1:1 — return CS2's ArenaStringPtr::Set (equivalent to FVA
// sub_7FFBE81027F0).  Signature: void __fastcall(ArenaStringPtr*, std::string*, Arena*).
// Nullptr if init() failed to resolve the target.  Used by Hook A to write
// serialized bytes into pb->move_crc through the arena-aware path.
void* get_arena_string_set() noexcept;

} // namespace fva::scratch_serialize
