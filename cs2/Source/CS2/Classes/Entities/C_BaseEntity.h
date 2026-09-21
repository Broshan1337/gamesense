#pragma once

#include <cstdint>

#include <CS2/Classes/CBodyComponent.h>
#include <CS2/Classes/EntitySystem/CEntityHandle.h>
#include "CEntityInstance.h"

namespace cs2
{

struct CBodyComponent;
struct CEntitySubclassVDataBase;
struct CGameSceneNode;
struct CRenderComponent;
struct Vector;

struct C_BaseEntity : CEntityInstance {
    using m_pGameSceneNode = CGameSceneNode*;
    using m_iHealth = std::int32_t;
    using m_lifeState = std::uint8_t;
    using m_iTeamNum = std::uint8_t;
    using m_pSubclassVData = CEntitySubclassVDataBase*;
    using m_pRenderComponent = CRenderComponent*;
    using m_hOwnerEntity = CEntityHandle;
    // Standard Source 2 field, confirmed via a schema-registration function that references
    // both this field name and the literal string "C_BaseEntity". Used here to reach the
    // entity's CModelState (mesh group mask) - see ModelStateOffsets.h for the full RE trail.
    using m_CBodyComponent = CBodyComponent*;
    // CUtlStringToken in the real schema (a single uint32 hash, m_nHashCode) - modeled here
    // as a plain uint32 since that's all FieldOffset needs for pointer arithmetic. Drives
    // which weapon "subclass" (e.g. specific knife type) the engine resolves this entity as -
    // see EntitySubclassOffsets.h.
    using m_nSubclassID = std::uint32_t;
    using GetAbsOrigin = Vector*(C_BaseEntity* thisptr);
};

}
