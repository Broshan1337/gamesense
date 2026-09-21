// ============================================================================
// Hook B — CBaseClientState::FireLevelInitEvent.
//
// Reproduces FVA `sub_7FFBE80C8E68` (client.dll+0xAFDFB0 target).
//
// FVA's handler runs a one-shot lazy resolver at first invocation: it
// XOR-decodes three IDA-style patterns, sig-scans client.dll for each,
// derives a RIP-relative slot via `mov rax, [rax]` post-processing, and
// caches the resulting pointers in qword_7FFBE823A980/A988/A990.  Live-
// verified 2026-07-10:
//    qword_7FFBE823A980 = 0x04DCF5150090
//    qword_7FFBE823A988 = 0x04DCB5FA0000
//    qword_7FFBE823A990 = 0x04DCFAA00500
// All three are arena/heap pointers created during level load — game-state
// handles FVA uses inside its Hook A mutator.
//
// Our reconstruction does NOT (yet) consume those three globals — Hook A's
// mutation body is still Phase 4b work.  So Hook B is a THIN passthrough:
// it installs an E9 so the FVA E9 slot on client.dll+0xAFDFB0 is claimed by
// us if FVA is not injected, and just forwards to the original.  When the
// Phase 4b mutator body actually needs the resolved pointers we can pull
// them in without churning the install path.
// ============================================================================
#pragma once

namespace fva::hooks
{

bool install_level_init_hook();
void uninstall_level_init_hook();

} // namespace fva::hooks
