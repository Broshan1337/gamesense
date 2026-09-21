#pragma once

#include <cstdint>

#include <CS2/Classes/SceneObjectUpdaterHandle_t.h>
#include "C_EconEntity.h"

namespace cs2
{

struct C_CSWeaponBase : C_EconEntity {
    using m_iClip1 = std::int32_t;
    using sceneObjectUpdaterHandle = SceneObjectUpdaterHandle_t*;
    using GetInaccuracy = float(C_CSWeaponBase* thisptr, float* movementInaccuracy, float* airSpeedInaccuracy);
    using GetSpread = float(C_CSWeaponBase* thisptr);
    // The real per-weapon composite-material regenerator (what "regenerate_weapon_skins"
    // is bound to natively) - reads paint kit/seed/wear off the item view and rebuilds
    // the .vcompmat. forceHighRes mirrors the console command's own a2 argument.
    using RegenerateSkin = void(C_CSWeaponBase* thisptr, bool forceHighRes);
    // sub_D9A070 - THE ONLY WRITER of m_pSubclassVData (weapon + 1272), and the fix for the
    // knife holding-animation bug that took this project many sessions to find.
    //
    // It resolves the schema subclass data for whatever index the entity currently reports:
    //     m_pSubclassVData = sub_F17310(registry, classIndex, m_nSubclassID);
    // and, if that registry lookup misses, falls back to building VData from the entity's own
    // class name instead (which for a knife yields the GENERIC "weapon_knife" data - worth
    // knowing, because that failure mode looks identical to "nothing happened" unless you
    // compare the VData pointer, not just its name).
    //
    // CRITICAL, and the whole reason this must be called directly rather than relying on
    // UpdateSubclass(): sub_DDE400 (UpdateSubclass) only reaches this function when
    // `(*(uint8*)(entityIdentity + 48) & 0x10) == 0`. That bit is already set on any entity
    // which resolved its subclass data at spawn - i.e. every weapon we ever touch - so
    // UpdateSubclass() silently skips re-resolution and merely re-dispatches the STALE VData's
    // vtable+6. Calling this directly bypasses the gate.
    //
    // This is also why, for sessions, the skin changer only ever produced a correct grip on a
    // brand-new entity (a map rejoin, or a fresh console `give`): those entities had not
    // resolved subclass data yet, so the gate was still open. It was never about deploy timing.
    using ResolveSubclassData = std::int64_t(C_CSWeaponBase* thisptr);
    // The real UpdateWeaponData() - confirmed via the user's friend, who pointed at the exact
    // "Re-creating weapon hudmodel due to vdata subclass model change." string as living inside
    // it. Full decompile this session (sub_1494B00) shows it's not just a HUD-icon cleanup
    // helper as first assumed: everything gated on changeType==1 (compare a cached vdata
    // reference at thisptr+9712 against the currently-resolved one; if they differ, destroy the
    // stale first-person hudmodel sub-entity so it rebuilds fresh) sits inside an `if`, but the
    // function UNCONDITIONALLY tail-calls a second function (sub_15D4990) afterward regardless
    // of changeType's value. That second function re-derives the weapon's currently-desired
    // model via a virtual call (vtable+2624) and, if it differs from what's currently set,
    // dispatches into the SAME internal function (sub_15D4740) our own SetModel chain already
    // reaches. So this is the game's own "did my VData-driven model change? then fix it"
    // auto-refresh - the real reason a raw skin/subclass write needs a follow-up call at all.
    // changeType==1 additionally does the stale-hudmodel cleanup (only meaningful for a genuine
    // subclass swap, i.e. knives); changeType==0 skips that but still runs the unconditional
    // model-refresh check, so it's safe to call on any weapon regardless of whether its subclass
    // actually changed. Found registered as a stored function pointer in ~48 separate per-class
    // callback-table entries in .data.rel.ro (not a single call site) - consistent with being a
    // shared per-class handler invoked by a common dispatcher, not tied to one weapon type.
    using UpdateWeaponData = std::int64_t(C_CSWeaponBase* thisptr, unsigned int changeType);
    // EXPERIMENTAL - the real UpdateSubclass(), confirmed by the user's own IDA analysis
    // (matches sub_DDE400: computes/reuses the subclass hash, calls ResolveSubclassData, then -
    // the part we had never called before - dispatches into the resolved subclass data object's
    // own vtable+48 method, passing through the second argument (real callers pass KV3 spawn
    // data with a "subclass_name" field; we pass nullptr since our own m_nSubclassID write
    // already means the hash-parsing branch that would dereference this argument is skipped).
    // That final vtable dispatch is the one new piece of the real mechanism this call reaches
    // that resolveSubclassData()/updateWeaponData() alone did not.
    using UpdateSubclass = std::int64_t(C_CSWeaponBase* thisptr, void* subclassSpawnData);
    // EXPERIMENTAL - candidate SetModel(), found via a hardcoded-string caller
    // (sub_142D250 calls it with a literal "models/inventory_items/dogtags.vmdl" path). Its own
    // decompile appears to take only one argument, but that's very likely a HexRays rendering
    // limitation on indirect/vtable calls, not the true prototype - the function falls straight
    // into a vtable call without touching its second argument register first, so the model-path
    // string passed by the caller should still reach that indirect call intact even though the
    // decompiled C doesn't show it explicitly.
    using SetModel = std::int64_t(C_CSWeaponBase* thisptr, const char* modelPath);
    // sub_40F1590 - the real Linux equivalent of the reference implementation's
    // UpdateCompositeMaterial(CCompositeMaterialOwner*, bool). NOT a C_CSWeaponBase method: it
    // takes the weapon's embedded CCompositeMaterialOwner sub-object, which lives at
    // kCompositeMaterialOwnerOffset bytes into the weapon (see below). Declared here anyway to
    // keep it next to RegenerateSkin, the call it always precedes.
    //
    // What it does (confirmed by full decompile against a friend-supplied Windows decompile of
    // the same function, and re-verified in a fresh IDA session before wiring): flushes the
    // owner's pending composite-material job queue (array at owner+704, count at owner+696),
    // submitting each queued job through the composite-material system singleton's own
    // vtable+24; then walks a second array (owner+680, count at owner+672) releasing a refcount
    // on each entry; then zeroes both counts. Finally - only if that second array was non-empty
    // AND the bool argument is true AND no rebuild is already in flight (owner+720) - dispatches
    // through the owner's OWN vtable slot 4, which is explicitly guarded against the base class's
    // default no-op, so that last step is a safe no-op unless a derived class overrides it.
    //
    // Why this is the right call and not a guess: the game's own "regenerate_weapon_skins"
    // ConCommand handler (sub_1467B10) does exactly, and only, this per weapon:
    //     sub_40F1590(weapon + 1928, true);   // this function
    //     sub_1466900(weapon, forceHighRes);  // RegenerateSkin, below
    // - byte-for-byte the same two-call sequence the reference implementation performs
    // (UpdateCompositeMaterial(composite) then UpdateSkin(true)). This project previously only
    // ever reached this function indirectly, via RegenerateSkin's own internal conditional
    // fallback branch, so the flush never happened on the path we actually use.
    using UpdateCompositeMaterial = std::int64_t(void* compositeMaterialOwner, bool dispatchOwnerRebuild);

    // Byte offset of the embedded CCompositeMaterialOwner sub-object within C_CSWeaponBase.
    // Taken directly from the game's own call site (sub_1467B10: `sub_40F1590(weapon + 1928, 1)`)
    // rather than derived, and independently corroborated by RegenerateSkin's own internal call
    // to the same function at the same offset (rendered as `a1 + 241` in qwords = 1928 bytes).
    // Note this differs from the reference implementation's Windows offset (0x608 / 1544) - the
    // two builds lay C_CSWeaponBase out differently; the FUNCTION is the same, the offset is not.
    static constexpr int kCompositeMaterialOwnerOffset = 1928;
};

}
