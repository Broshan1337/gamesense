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
    
    
    
    // ABI (decoded from the 2026-10-07 crash + the game's own call sites at libclient
    // 0x15d38b7/0x15d39e6 on build 11087116): bool(weapon, outSkinData*) - rdi = the weapon,
    // rsi = a pointer to a 32-byte OUTPUT struct the success path fills with two aligned
    // 16-byte stores ([out]/[out+0x10], movaps => the buffer MUST be 16-byte aligned). The
    // out-pointer is NEVER read, only written, and only on the success path (the early-out
    // path returns false without touching it) - any aligned 32-byte buffer works, no
    // initialization needed. The game's own callers pass a stack buffer pre-initialized from
    // a runtime-built default table (BSS at 0x49f98e0 - not a static file constant).
    // (The old "forceHighRes mirrors the console command's own a2 argument" story was the
    // ConCommand-ITERATOR era reading - the iterator takes a bool in dil; this per-weapon fn
    // does not. Passing false here = null out-pointer = the 10-07 movaps [r12] crash with
    // r12 = 0 once the classifier chain actually let SkinChanger run.)
    using RegenerateSkin = bool(C_CSWeaponBase* thisptr, void* outSkinData);
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    using ResolveSubclassData = std::int64_t(C_CSWeaponBase* thisptr);
    using UpdateSkin = void(C_CSWeaponBase* thisptr, int changeFlag);
    using UpdateCompositeMaterialSet = void(void* destOwner, void* srcOwner, bool dispatch);
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    using UpdateWeaponData = std::int64_t(C_CSWeaponBase* thisptr, unsigned int changeType);
    
    
    
    
    
    
    
    
    using UpdateSubclass = std::int64_t(C_CSWeaponBase* thisptr, void* subclassSpawnData);
    
    
    
    
    
    
    
    using SetModel = std::int64_t(C_CSWeaponBase* thisptr, const char* modelPath);
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    using UpdateCompositeMaterial = std::int64_t(void* compositeMaterialOwner, bool dispatchOwnerRebuild);

    
    
    
    
    
    
    static constexpr int kCompositeMaterialOwnerOffset = 1936;
};

}
