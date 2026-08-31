// ============================================================================
// input_scratch_mirror.h
//
// Зеркалит live CUserCmd (buttons + view-angles) в две отдельные scratch-
// копии, аллоцированные CS2 protobuf ареной.  Мотивация — matched-vtable
// scratch pair (CInButtonStatePB + CMsgQAngle), которые Phase E может
// re-serialize и записать move_crc для валидации server-side без touchа
// оригинального live CUserCmd.
//
// Что аллоцируется (через CS2's own protobuf T::New по RVA + prologue
// fingerprint, тот же паттерн что protobuf_alloc.cpp — иначе vtable будет
// не тот и downstream serializers ломаются):
//
//   CInButtonStatePB scratch  ←  buttons  из user_cmd + 0x60/0x68/0x70
//                                (buttonstate1 / buttonstate2 / buttonstate3)
//                                has_bits |= 0x1 | 0x2 | 0x4
//   CMsgQAngle       scratch  ←  view-angles  из user_cmd->base->viewangles
//                                pitch (x) + yaw (y) только, roll игнорится
//                                has_bits |= 0x1 | 0x2
//
// Wire-observability: scratch не читается прямо в wire.  Phase E's
// scratch_serialize производит финальный write в move_crc, что делает
// scratch state косвенно observable через CRC-mismatch если сервер сравнивает.
// ============================================================================
#pragma once

#include "../schema/cbaseusercmdpb_layout.h"

namespace fva::hooks::input_scratch_mirror
{

// Sig-scan (via RVA + prologue fingerprint, matching protobuf_alloc.cpp's
// style) client.dll for CInButtonStatePB::New and CMsgQAngle::New.  Returns
// false on fingerprint drift; apply() becomes a silent no-op in that case.
bool init();

[[nodiscard]] bool ready() noexcept;

// True iff init() had to fall through to a plain HeapAlloc backing store
// for at least one of the caches — the CS2 build drifted such that
// neither the manifest RVA nor sig-scan hit CInButtonStatePB::New /
// CMsgQAngle::New.  self_check reads client.dll RVAs which may point at
// unmapped bytes on such a build; dllmain uses this to guard self_check
// even though ready() is true.
[[nodiscard]] bool used_heap_fallback() noexcept;

// Mirror the live CUserCmd (Hook A's rbx) into the two aux caches.  Safe to
// call every tick; safe to call before init() (no-op); safe to call with
// raw_cmd == nullptr (no-op).
void apply(const valve::pb::raw::CUserCmd* raw_cmd) noexcept;

// Read-only accessors — expose the caches for downstream stitching.  Return
// nullptr until the first successful apply() has lazy-allocated them.
[[nodiscard]] const valve::pb::raw::CInButtonStatePB* buttons_cache() noexcept;
[[nodiscard]] const valve::pb::raw::CMsgQAngle*       angles_cache()  noexcept;

} // namespace fva::hooks::input_scratch_mirror
