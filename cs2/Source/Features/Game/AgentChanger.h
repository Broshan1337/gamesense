#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Econ/ItemDefDatabase.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <GameClient/PawnSettle.h>
#include <Utils/CrashLogger.h>
#include <Features/Game/AgentChangerConfigVariables.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <HookContext/HookContextMacros.h>
#include <MemoryPatterns/PatternTypes/WeaponPatternTypes.h>

















namespace agent_changer
{


struct ModelState {
    std::uint64_t modelHandle;
    std::uint64_t modelNameSymbol;
};

struct PawnOffsets {
    std::uint32_t teamNum = 0;
    std::uint32_t gameSceneNode = 0;
    std::uint32_t collision = 0;
    std::uint32_t weaponServices = 0;
    std::uint32_t ownerEntity = 0;
    std::uint32_t modelState = 0;
    std::uint32_t vecMins = 0;
    std::uint32_t vecMaxs = 0;
    std::uint32_t myWeapons = 0;
};

[[nodiscard]] inline PawnOffsets resolveOffsets(auto&& schemaSystem) noexcept
{
    PawnOffsets o;
    o.teamNum = schemaSystem.getFieldOffset("C_BaseEntity", "m_iTeamNum").value_or(0);
    o.gameSceneNode = schemaSystem.getFieldOffset("C_BaseEntity", "m_pGameSceneNode").value_or(0);
    o.collision = schemaSystem.getFieldOffset("C_BaseModelEntity", "m_Collision").value_or(0);
    o.weaponServices = schemaSystem.getFieldOffset("C_BasePlayerPawn", "m_pWeaponServices").value_or(0);
    o.ownerEntity = schemaSystem.getFieldOffset("C_BaseEntity", "m_hOwnerEntity").value_or(0);
    o.modelState = schemaSystem.getFieldOffset("CSkeletonInstance", "m_modelState").value_or(0);
    o.vecMins = schemaSystem.getFieldOffset("CCollisionProperty", "m_vecMins").value_or(0);
    o.vecMaxs = schemaSystem.getFieldOffset("CCollisionProperty", "m_vecMaxs").value_or(0);
    o.myWeapons = schemaSystem.getFieldOffset("CPlayer_WeaponServices", "m_hMyWeapons").value_or(0);
    return o;
}

[[nodiscard]] inline bool offsetsValid(const PawnOffsets& o) noexcept
{
    return o.teamNum && o.gameSceneNode && o.collision && o.weaponServices && o.ownerEntity
        && o.modelState && o.vecMins && o.vecMaxs && o.myWeapons;
}

template <typename T>
void writeAt(unsigned char* base, std::uint32_t offset, const T& value) noexcept
{
    std::memcpy(base + offset, &value, sizeof(T));
}

template <typename T>
T readAt(const unsigned char* base, std::uint32_t offset) noexcept
{
    T value;
    std::memcpy(&value, base + offset, sizeof(T));
    return value;
}

} 

template <typename HookContext>
class AgentChanger {
public:
    explicit AgentChanger(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() noexcept
    {
        
        
        
        
        
        
        
        if (const auto mapTime = hookContext.globalVars().curtime();
            !mapTime.hasValue() || mapTime.value() < schema_readiness::kMinMapTime)
            return;

        if (auto&& localPawn = hookContext.activeLocalPlayerPawn()) {
            if (!localPawn.isAlive().value_or(false))
                return;
            
            
            
            
            
            
            
            
            if (!pawn_settle::ready(localPawn.rawPawn()))
                return;
            applyToPawn(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity()));
        }
    }

private:
    void applyToPawn(cs2::C_BaseEntity* pawn) noexcept
    {
        const auto schema = hookContext.schemaSystem();
        const auto offsets = agent_changer::resolveOffsets(schema);
        if (!agent_changer::offsetsValid(offsets))
            return;

        auto* const pawnBytes = reinterpret_cast<unsigned char*>(pawn);
        const auto team = agent_changer::readAt<std::uint8_t>(pawnBytes, offsets.teamNum);
        if (team != 2 && team != 3)
            return;

        auto* const sceneNode = agent_changer::readAt<unsigned char*>(pawnBytes, offsets.gameSceneNode);
        if (!sceneNode)
            return;
        auto* const modelState = sceneNode + offsets.modelState;

        const auto def = GET_CONFIG_VAR(agent_changer_vars::AgentDef);
        const cs2::ItemDefEntry* agent = nullptr;
        if (def)
            agent = cs2::itemDefById(cs2::kAgentItems, static_cast<int>(sizeof(cs2::kAgentItems) / sizeof(cs2::kAgentItems[0])), def);

        const auto handle = agent_changer::readAt<std::uint64_t>(modelState, kModelHandleOffset);

        
        if (trackedPawn != pawn) {
            trackedPawn = pawn;
            savedHandle = 0;
            savedNameSymbol = 0;
            appliedDef = 0;
        }

        if (!agent || !agent->model[0]) {
            if (savedHandle != 0) {
                
                
                agent_changer::writeAt(modelState, kModelHandleOffset, savedHandle);
                agent_changer::writeAt(modelState, kModelNameOffset, savedNameSymbol);
                savedHandle = 0;
                savedNameSymbol = 0;
                appliedDef = 0;
            }
            return;
        }

        if (appliedDef == def && handle == appliedHandle)
            return; 

        if (savedHandle == 0) {
            savedHandle = handle;
            savedNameSymbol = agent_changer::readAt<std::uint64_t>(modelState, kModelNameOffset);
        }

        const auto setModel = hookContext.patternSearchResults().template get<PointerToSetModel>();
        if (!setModel)
            return;
        CrashLogger::trace(0x350); 
        setModel(reinterpret_cast<cs2::C_CSWeaponBase*>(pawn), agent->model);
        CrashLogger::trace(0x351);

        
        
        alignas(4) static constexpr float kMins[3] = {-16.0f, -16.0f, 0.0f};
        alignas(4) static constexpr float kMaxs[3] = {16.0f, 16.0f, 72.0f};
        auto* const collision = pawnBytes + offsets.collision;
        agent_changer::writeAt(collision, offsets.vecMins, kMins);
        agent_changer::writeAt(collision, offsets.vecMaxs, kMaxs);

        cycleWeaponOwners(pawn, offsets);

        appliedHandle = agent_changer::readAt<std::uint64_t>(modelState, kModelHandleOffset);
        appliedDef = def;
    }

    
    
    void cycleWeaponOwners(cs2::C_BaseEntity* pawn, const agent_changer::PawnOffsets& offsets) noexcept
    {
        auto* const pawnBytes = reinterpret_cast<unsigned char*>(pawn);
        auto* const weaponServices = agent_changer::readAt<unsigned char*>(pawnBytes, offsets.weaponServices);
        if (!weaponServices)
            return;
        const auto count = agent_changer::readAt<std::int32_t>(weaponServices, offsets.myWeapons);
        auto* const data = agent_changer::readAt<unsigned char*>(weaponServices, offsets.myWeapons + 8);
        if (count <= 0 || !data)
            return;

        auto&& entitySystem = hookContext.template make<EntitySystem>();
        for (int i = 0; i < count; ++i) {
            std::uint32_t handle{};
            std::memcpy(&handle, data + i * sizeof(std::uint32_t), sizeof(handle));
            auto* const weapon = entitySystem.getEntityFromHandle(cs2::CEntityHandle{handle});
            if (!weapon)
                continue;
            auto* const weaponBytes = reinterpret_cast<unsigned char*>(weapon);
            const auto saved = agent_changer::readAt<std::uint32_t>(weaponBytes, offsets.ownerEntity);
            agent_changer::writeAt(weaponBytes, offsets.ownerEntity, 0xffffffffu);
            agent_changer::writeAt(weaponBytes, offsets.ownerEntity, saved);
        }
    }

    HookContext& hookContext;
    cs2::C_BaseEntity* trackedPawn = nullptr;
    std::uint64_t savedHandle = 0;
    std::uint64_t savedNameSymbol = 0;
    std::uint16_t appliedDef = 0;
    std::uint64_t appliedHandle = 0;

    static constexpr std::uint32_t kModelHandleOffset = 0xA0; 
    static constexpr std::uint32_t kModelNameOffset = 0xA8;   
};
