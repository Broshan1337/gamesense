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

// Local agent model changer. Port of the reference agents changer (velocity agents.cpp /
// FrameworkCS2 ModelChanger): while the local player is alive, swap the pawn's model to the
// configured agent via the game's own SetModel (PointerToSetModel - the same generic
// C_BaseModelEntity::SetModel the knife impersonation uses; works on pawns too, called on the
// game thread through the return-address spoofer like every other spoofed pattern call).
//
// Per the reference's known visual follow-ups after a model swap:
//  - the collision bounds are rewritten to the standard player hull (agents share it), and
//  - the owned weapons' m_hOwnerEntity handles are cycled (write 0xFFFFFFFF, write back) so
//    the weapon entities re-attach to the new model and don't disappear.
// The previous model (m_hModel resource handle + m_ModelName symbol, both u64 in CModelState)
// is captured before the first swap so "None" can write it back (full restore also happens
// naturally on respawn, where the game re-runs its own model selection).
//
// Runs on the GAME thread (CreateMove hook) - the same thread-safety rule as the inventory
// changer: never mutate live game objects from the present thread.
namespace agent_changer
{

// CModelState field offsets (schema-stable; resolved via the schema system per call).
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

} // namespace agent_changer

template <typename HookContext>
class AgentChanger {
public:
    explicit AgentChanger(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() noexcept
    {
        // MAP-TRANSITION SESSION GATE (AGENTS.md rule 0, crash 2026-09-19 23:28): during a map
        // load the pawn is rebuilt while the engine's model system is still spinning up - our
        // SetModel on the fresh pawn raced the model-load machinery and the loading thread died
        // on a null virtual call (engine2, mov rax,[rdi=0]; call [rax+0x140]). The alive-pawn
        // check alone is not a session gate: the pawn exists and reads "alive" long before the
        // scene is safe. Same idiom as NameAnimator/SkinChanger/ServerLagger: no curtime below
        // kMinMapTime = transition window = stand down.
        if (const auto mapTime = hookContext.globalVars().curtime();
            !mapTime.hasValue() || mapTime.value() < schema_readiness::kMinMapTime)
            return;

        if (auto&& localPawn = hookContext.activeLocalPlayerPawn()) {
            if (!localPawn.isAlive().value_or(false))
                return;
            // PAWN-SETTLE GATE (2026-09-27, crashes 02:14/10:12 - both map-load, both right
            // after the fresh pawn spawned): the curtime gate above is BLIND during the join
            // window (old map's large curtime until the GlobalVars swap - the 09-12 lesson),
            // so SetModel still fired on a pawn whose model/animgraph state was mid-build
            // (the same race as the documented 09-19 23:28 crash). CLOCK_MONOTONIC identity
            // settle: the fresh pawn must be the local pawn for pawn_settle::kSettleNs
            // before any model swap. Mid-round respawns re-arm the window too (rule 0:
            // don't re-fire on a pawn identity change until the session gate passes).
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

        // Track pawn identity: respawn/model resets by the game invalidate the capture.
        if (trackedPawn != pawn) {
            trackedPawn = pawn;
            savedHandle = 0;
            savedNameSymbol = 0;
            appliedDef = 0;
        }

        if (!agent || !agent->model[0]) {
            if (savedHandle != 0) {
                // Selection cleared: write the captured original model back (a respawn also
                // restores it through the game's own model selection).
                agent_changer::writeAt(modelState, kModelHandleOffset, savedHandle);
                agent_changer::writeAt(modelState, kModelNameOffset, savedNameSymbol);
                savedHandle = 0;
                savedNameSymbol = 0;
                appliedDef = 0;
            }
            return;
        }

        if (appliedDef == def && handle == appliedHandle)
            return; // already wearing it (and no respawn since)

        if (savedHandle == 0) {
            savedHandle = handle;
            savedNameSymbol = agent_changer::readAt<std::uint64_t>(modelState, kModelNameOffset);
        }

        const auto setModel = hookContext.patternSearchResults().template get<PointerToSetModel>();
        if (!setModel)
            return;
        CrashLogger::trace(0x350); // agent SetModel about to fire (0x351 = done)
        setModel(reinterpret_cast<cs2::C_CSWeaponBase*>(pawn), agent->model);
        CrashLogger::trace(0x351);

        // Standard player hull - the reference rewrites it after every swap so a shorter/taller
        // agent model can't leave stale collision behind.
        alignas(4) static constexpr float kMins[3] = {-16.0f, -16.0f, 0.0f};
        alignas(4) static constexpr float kMaxs[3] = {16.0f, 16.0f, 72.0f};
        auto* const collision = pawnBytes + offsets.collision;
        agent_changer::writeAt(collision, offsets.vecMins, kMins);
        agent_changer::writeAt(collision, offsets.vecMaxs, kMaxs);

        cycleWeaponOwners(pawn, offsets);

        appliedHandle = agent_changer::readAt<std::uint64_t>(modelState, kModelHandleOffset);
        appliedDef = def;
    }

    // Write 0xFFFFFFFF into every owned weapon's m_hOwnerEntity and immediately restore it -
    // forces the weapon entities to re-attach to the swapped model (they otherwise vanish).
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

    static constexpr std::uint32_t kModelHandleOffset = 0xA0; // CModelState::m_hModel
    static constexpr std::uint32_t kModelNameOffset = 0xA8;   // CModelState::m_ModelName
};
