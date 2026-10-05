#pragma once

#include <cstdint>

#include <CS2/Classes/CEconItemView.h>
#include <CS2/Classes/Entities/C_CSWeaponBase.h>
#include <CS2/Classes/Vector.h>
#include <Utils/FieldOffset.h>
#include <Utils/StrongTypeAlias.h>

template <typename FieldType, typename OffsetType>
using WeaponOffset = FieldOffset<cs2::C_CSWeaponBase, FieldType, OffsetType>;

STRONG_TYPE_ALIAS(OffsetToClipAmmo, WeaponOffset<cs2::C_CSWeaponBase::m_iClip1, std::int32_t>);
STRONG_TYPE_ALIAS(OffsetToWeaponSceneObjectUpdaterHandle, WeaponOffset<cs2::C_CSWeaponBase::sceneObjectUpdaterHandle, std::int32_t>);
STRONG_TYPE_ALIAS(PointerToGetInaccuracyFunction, cs2::C_CSWeaponBase::GetInaccuracy*);
STRONG_TYPE_ALIAS(PointerToGetSpreadFunction, cs2::C_CSWeaponBase::GetSpread*);
STRONG_TYPE_ALIAS(PointerToRegenerateWeaponSkin, cs2::C_CSWeaponBase::RegenerateSkin*);
STRONG_TYPE_ALIAS(PointerToResolveSubclassData, cs2::C_CSWeaponBase::ResolveSubclassData*);
STRONG_TYPE_ALIAS(PointerToUpdateWeaponData, cs2::C_CSWeaponBase::UpdateWeaponData*);
STRONG_TYPE_ALIAS(PointerToUpdateSubclass, cs2::C_CSWeaponBase::UpdateSubclass*);
STRONG_TYPE_ALIAS(PointerToSetModel, cs2::C_CSWeaponBase::SetModel*);
STRONG_TYPE_ALIAS(PointerToUpdateCompositeMaterial, cs2::C_CSWeaponBase::UpdateCompositeMaterial*);

// sub_1FD2EA0 - resolves the item schema's item-definition-by-index hashtable (lazily
// initializing the singleton if needed) and looks up the entry for whatever 16-bit index is
// read from thisLike+4290. Not tied to any real class - the only field it ever touches on its
// argument is that one index at a fixed offset, so any suitably-sized zeroed buffer with the
// desired index written at +4290 works as the argument. Returns a CEconItemDefinition*-like
// pointer whose "model_player" field lives at +328 (confirmed via the KV3 item-definition
// parsing function, sub_1F85DD0 - see project notes).
using GetItemDefinitionByIndexFn = void*(void* indexHolder);
STRONG_TYPE_ALIAS(PointerToGetItemDefinitionByIndex, GetItemDefinitionByIndexFn*);
STRONG_TYPE_ALIAS(PointerToSetAttributeValueByName, cs2::CEconItemView::SetAttributeValueByName*);

// sub_1FD4AE0 - the real per-equipped-paint-kit data accessor, found by tracing
// RegenerateWeaponSkin's (sub_1466900) own vtable+2400 item-view call before it builds a
// "weapons/paints/legacy/<name>.vcompmat" path from the result. Given a CEconItemView*
// (embedded, no dereference needed to obtain it), calls the item view's own vtable+32 virtual
// method to read its currently-equipped paint-kit index, then looks that index up in a
// red-black-tree-backed paint-kit-definition singleton (lazily built on first call) and
// returns the resolved definition pointer, or null if none is equipped/found. The RB-tree
// lookup itself is sub_1F2F1A0 (tail-called from here) - its node layout (count@752,
// flags@756, data@760, root@768, 32-byte nodes: {left:4, right:4, ..., key:4 @+16, ...,
// value:8 @+24}) matches exactly what was inferred earlier this session from the tree-insert
// function, now independently confirmed via this find function's own decompile. The returned
// definition's use_legacy_model bool lives at offset 174 (confirmed via the KV3 paint-kit
// parser, sub_1F2B190) - this is the real per-paint-kit "is legacy" check that
// SkinChangerData::isLegacyPaintKit()'s earlier guess was standing in for.
using GetPaintKitDefinitionFn = void*(void* itemView);
STRONG_TYPE_ALIAS(PointerToGetPaintKitDefinition, GetPaintKitDefinitionFn*);

// sub_1ADCAA0 - CS2's per-shot spread-SEED generator (velocity-cs2 calls it "get_tick_view_angles").
// Reverse-engineered (and confirmed against velocity-cs2) as hash(round(pitch, 0.5deg),
// round(yaw, 0.5deg), tick) -> the 32-bit seed the game feeds its ran1 stream to build that shot's
// bullet cone. The first argument is a pawn pointer at the real call site (sub_145EBA0, the weapon
// Fire) but the function body IGNORES it - pass nullptr. `angles` points at {pitch, yaw, roll}; only
// pitch and yaw are read (roll does not affect the seed, which is exactly what makes the roll-based
// spread correction in SpreadSolver possible). Verified: exactly one match across libclient.so.
using SpreadSeedFn = std::uint32_t(void* pawn, const cs2::Vector* angles, int tick);
STRONG_TYPE_ALIAS(PointerToSpreadSeedFunction, SpreadSeedFn*);

// sub_1ADCC00 - CS2's per-shot bullet-CONE generator (velocity-cs2 "weapon_calculate_spread"). Runs
// FX_FireBullets' spread path for a given seed and writes the tangent-plane deflection (*outX, *outY)
// added to the aim's forward vector: bullet_dir = (forward + left*outX + up*outY).normalized(). Must
// be called with seed+1 (the game does its own ++seed right before calling this). SysV register
// mapping, confirmed by disasm: rdi=itemDefinitionIndex(int16), esi=numBullets, edx=mode(0),
// ecx=seed, r8=&outX, r9=&outY, xmm0=inaccuracy, xmm1=spread, xmm2=recoilIndex. Verified: exactly one
// match across libclient.so.
using CalculateSpreadFn = void(std::int16_t itemDefinitionIndex, int numBullets, int mode, std::uint32_t seed, float inaccuracy, float spread, float recoilIndex, float* outX, float* outY);
STRONG_TYPE_ALIAS(PointerToCalculateSpreadFunction, CalculateSpreadFn*);

// sub_14537D0 - CS2's UpdateAccuracyPenalty (velocity-cs2 "weapon_update_accuracy"). Exponentially
// decays the weapon's m_fAccuracyPenalty (0x28A8) and m_flRecoilIndex (0x28B8) toward the current
// tick and stamps the last-update tick, so GetInaccuracy/GetSpread read a value that is CURRENT
// rather than stale from the last game update. Must be called on the weapon right before reading its
// inaccuracy/spread for a prediction, or the predicted cone lags the real one (badly for fast-firing
// spray weapons). Idempotent within a tick (a second call the same tick is a no-op), so calling it in
// our CreateMove hook does not desync from the game's own call at fire. Single argument: the weapon.
// Verified exactly one match across libclient.so.
using UpdateAccuracyPenaltyFn = void(void* weapon);
STRONG_TYPE_ALIAS(PointerToUpdateAccuracyPenaltyFunction, UpdateAccuracyPenaltyFn*);

// Aim punch (recoil kick) as {pitch, yaw, roll} degrees. Under SysV a 12-byte all-float struct
// returns in xmm0 (pitch, yaw) + xmm1 (roll) - exactly how sub_14D31A0 returns it.
struct AimPunchAngles {
    float pitch;
    float yaw;
    float roll;
};

// sub_14D31A0 - CS2's aim-punch getter (velocity-cs2 "get_aim_punch"). Given a
// CCSPlayer_AimPunchServices*, interpolates the PREDICTABLE (m_predictableBaseTick/InterpAmount/
// Angle/AngleVel at 0x48/0x4C/0x50/0x5C) and UNPREDICTABLE (0xA0/0xA4) base punch angles to the
// CURRENT tick and returns their sum - the live aim punch the game adds to the shoot angle (confirmed:
// a caller `addps`es this straight into the view angle before firing). The aimbot subtracts it so a
// spray stays on target as recoil kicks the view. Reads only - safe from the input hook. Only pitch/
// yaw are used (roll is ~0 for aim punch).
//
// 2026-09-27 SIGNATURE CORRECTION (the mid-match SEGV class, RE'd from build 68f386a6): the
// 5GB update changed the signature. The current function takes (this, PunchAccumulator* in
// RSI, float in XMM0): it builds a local {int tick, float angle} pair and ADDS it into the
// CALLER-SUPPLIED accumulator via 0x2F072D0 - which reads [rsi]/[rsi+4] UNCONDITIONALLY (no
// null guard) - before interpolating the history (0x15173A0). The old "(double reserved,
// float rollInput)" contract belonged to the pre-update function; calling the new one with
// garbage in rsi fed an unmapped/garbage pair into the accumulator, the interpolation index
// collapsed to cvttss2si's indefinite value (0x80000000 = INT_MIN - what cvttss2si returns
// for NaN/overflow) and the history walk faulted at [history + INT_MIN*12] - both mid-match
// crashes faulted at exactly array - 0x600000000, the INT_MIN*12 offset, verified to the byte.
// A standalone read passes a ZEROED accumulator = zero extra accumulation = the base punch.
struct PunchAccumulator {
    std::int32_t tick;
    float angle;
};
using GetAimPunchFn = AimPunchAngles(void* aimPunchServices, PunchAccumulator* accumulator, float interpInput);
STRONG_TYPE_ALIAS(PointerToGetAimPunchFunction, GetAimPunchFn*);
