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
    
    
    
    using RegenerateSkin = void(C_CSWeaponBase* thisptr, bool forceHighRes);
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    using ResolveSubclassData = std::int64_t(C_CSWeaponBase* thisptr);
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    using UpdateWeaponData = std::int64_t(C_CSWeaponBase* thisptr, unsigned int changeType);
    
    
    
    
    
    
    
    
    using UpdateSubclass = std::int64_t(C_CSWeaponBase* thisptr, void* subclassSpawnData);
    
    
    
    
    
    
    
    using SetModel = std::int64_t(C_CSWeaponBase* thisptr, const char* modelPath);
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    using UpdateCompositeMaterial = std::int64_t(void* compositeMaterialOwner, bool dispatchOwnerRebuild);

    
    
    
    
    
    
    static constexpr int kCompositeMaterialOwnerOffset = 1928;
};

}
