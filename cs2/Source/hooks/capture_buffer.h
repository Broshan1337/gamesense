// ============================================================================
// Scratch capture buffer — reproduces FVA's `unk_7FFBE8234C90`.
//
// FVA holds a single statically-allocated CBaseUserCmdPB instance and
// snapshots every outgoing local usercmd into it via Hook C
// (Clear + MergeFrom).  We do the same — one process-lifetime instance,
// no reader synchronisation.  Consumers just read the fields; the snapshot
// is refreshed each Hook C invocation (~128 Hz during gameplay).
// ============================================================================
#pragma once

#include "../schema/cbaseusercmdpb_layout.h"

namespace fva::capture_buffer
{

// The scratch instance — FVA equivalent of unk_7FFBE8234C90.  Its vtable
// is picked up at construction (default-init).
extern valve::pb::raw::CBaseUserCmdPB g_scratch;

// Hook C calls this after MergeFrom to signal a fresh capture.  Purely a
// diagnostic hook — increments an internal counter callers can poll to see
// whether Hook C is actually firing.
void publish();

// Read the current generation counter (# of Hook C snapshots since load).
unsigned long long generation() noexcept;

// Copy the CBaseUserCmdPB vtable pointer from `src` into g_scratch so that
// virtual method dispatch (ByteSizeLong et al.) works on scratch itself.
// Called once from Hook C on the first CBaseUserCmdPB match.  Idempotent
// after the first successful cache.
void cache_vtable_from(void* src) noexcept;

} // namespace fva::capture_buffer
