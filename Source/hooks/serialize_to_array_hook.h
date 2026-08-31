// ============================================================================
// Hook C — MessageLite::SerializeToArray chokepoint (CBaseUserCmdPB filter).
//
// Reproduces the FVA handler `sub_7FFBE80C9B9C` (client.dll+0x1189930 target).
//
// Semantics (verified via IDA decomp 2026-07-10):
//   1. Once-init: cache the original trampoline pointer via TLS-guarded
//      once-init (analog of FVA's dword_7FFBE823CC08 / qword_7FFBE823CBE0).
//   2. Read `this->GetTypeName()` via vtable[+0x78].
//   3. Compare strlen+memcmp against XOR-decoded expected name.  On the
//      current build the encoded pair decodes to "CBaseUserCmdPB".
//   4. If (typename matches) AND (this != &scratch):
//        Clear(scratch);
//        MergeFrom(scratch, this);      // scratch := deep-copy of this
//      This is CAPTURE-ONLY — no mutation of `this` at this hook.
//   5. Always tail-call the original serializer.
//
// The wire bytes on-network are whatever CS2 built plus any mutation Hook A
// applied.  Hook C's purpose is to give other FVA subsystems a stable
// snapshot of the outgoing usercmd (via the scratch instance).
// ============================================================================
#pragma once

#include <cstdint>

namespace valve::pb::raw { struct CBaseUserCmdPB; }

namespace fva::hooks
{

bool install_serialize_to_array_hook();
void uninstall_serialize_to_array_hook();

// Hook body — actual entry-point of the FVA detour rewritten in C++.
// Called by hook trampoline once install_serialize_to_array_hook() succeeds.
#if defined(_WIN32)
char __fastcall hook_body(void* self, void* stream, int max_size);
#else
char hook_body(void* self, void* stream, int max_size);
#endif

} // namespace fva::hooks
