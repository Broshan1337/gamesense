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
    
    
    
    using m_CBodyComponent = CBodyComponent*;
    
    
    
    
    using m_nSubclassID = std::uint32_t;
    using GetAbsOrigin = Vector*(C_BaseEntity* thisptr);
};

}
