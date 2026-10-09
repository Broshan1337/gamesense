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
STRONG_TYPE_ALIAS(PointerToUpdateSkin, cs2::C_CSWeaponBase::UpdateSkin*);
STRONG_TYPE_ALIAS(PointerToUpdateCompositeMaterialSet, cs2::C_CSWeaponBase::UpdateCompositeMaterialSet*);








using GetItemDefinitionByIndexFn = void*(void* indexHolder);
STRONG_TYPE_ALIAS(PointerToGetItemDefinitionByIndex, GetItemDefinitionByIndexFn*);
STRONG_TYPE_ALIAS(PointerToSetAttributeValueByName, cs2::CEconItemView::SetAttributeValueByName*);















using GetPaintKitDefinitionFn = void*(void* itemView);
STRONG_TYPE_ALIAS(PointerToGetPaintKitDefinition, GetPaintKitDefinitionFn*);








using SpreadSeedFn = std::uint32_t(void* pawn, const cs2::Vector* angles, int tick);
STRONG_TYPE_ALIAS(PointerToSpreadSeedFunction, SpreadSeedFn*);








// Legacy ABI retained for pattern identification only. Do not invoke: the
// engine replaced it with a shot-context API on 2026-09-27. See SpreadSolver.
using CalculateSpreadFn = void(std::int16_t itemDefinitionIndex, int numBullets, int mode, std::uint32_t seed, float inaccuracy, float spread, float recoilIndex, float* outX, float* outY);
STRONG_TYPE_ALIAS(PointerToCalculateSpreadFunction, CalculateSpreadFn*);









using UpdateAccuracyPenaltyFn = void(void* weapon);
STRONG_TYPE_ALIAS(PointerToUpdateAccuracyPenaltyFunction, UpdateAccuracyPenaltyFn*);



struct AimPunchAngles {
    float pitch;
    float yaw;
    float roll;
};




















struct PunchAccumulator {
    std::int32_t tick;
    float fraction;
};
// Linux SysV: RSI points to the query tick/fraction, EDX supplies the interpolation flag.
using GetAimPunchFn = AimPunchAngles(void* aimPunchServices, const PunchAccumulator* sampleTime, bool allowExtrapolation);
STRONG_TYPE_ALIAS(PointerToGetAimPunchFunction, GetAimPunchFn*);
