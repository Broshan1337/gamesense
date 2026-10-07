#pragma once

#include <cstdint>

#include <CS2/Classes/CModelState.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <Utils/FieldOffset.h>

// Resolves the chain used to reach a weapon's live CModelState (for SetMeshGroupMask-style
// legacy-paint-kit fixes): C_BaseEntity::m_CBodyComponent (schema-resolved) locates the
// CBodyComponentSkeletonInstance; from there the FULLY SCHEMA-RESOLVED embedded chain
// (m_skeletonInstance -> m_modelState) reaches the CModelState. 2026-09-27: this replaced a
// virtual call through the component's vtable+112 ("slot 14, identified empirically") - the
// 5GB update reshuffled that vtable (the fourth reshuffled vtable of the update), slot 14
// became a static-pointer getter returning 0x46798A0, and writing m_MeshGroupMask (+0x248)
// through it clobbered the CBaseAnimGraph_API registry (entry-3 fn slot -> qword 2 -> the
// game's map-load walk called it -> the rip=2 crashes; caught with a gdb HW watchpoint).
// Schema chain (fresh dumper): m_skeletonInstance @ +0x80, m_modelState @ +0x140,
// m_MeshGroupMask @ +0x208 (520 decimal; an earlier revision of this comment said +0x248 - a
// hex typo, the runtime value was always schema-resolved so behavior was never affected),
// m_hModel @ +0xA0.
struct ModelStateOffsets {
    explicit ModelStateOffsets(auto&& schemaSystem) noexcept
        : bodyComponent{resolve(schemaSystem, "C_BaseEntity", "m_CBodyComponent")}
        , skeletonInstance{resolve(schemaSystem, "CBodyComponentSkeletonInstance", "m_skeletonInstance")}
        , modelState{resolve(schemaSystem, "CSkeletonInstance", "m_modelState")}
        , meshGroupMask{resolve(schemaSystem, "CModelState", "m_MeshGroupMask")}
        , modelHandle{resolve(schemaSystem, "CModelState", "m_hModel")}
    {
    }

    FieldOffset<cs2::C_BaseEntity, cs2::C_BaseEntity::m_CBodyComponent, std::int32_t> bodyComponent;
    FieldOffset<cs2::CBodyComponentSkeletonInstance, cs2::CBodyComponentSkeletonInstance::m_skeletonInstance, std::int32_t> skeletonInstance;
    FieldOffset<cs2::CSkeletonInstance, cs2::CSkeletonInstance::m_modelState, std::int32_t> modelState;
    FieldOffset<cs2::CModelState, cs2::CModelState::m_MeshGroupMask, std::int32_t> meshGroupMask;
    FieldOffset<cs2::CModelState, cs2::CModelState::m_hModel, std::int32_t> modelHandle;

    [[nodiscard]] bool isFullyResolved() const noexcept
    {
        return bodyComponent && skeletonInstance && modelState && meshGroupMask;
    }

private:
    [[nodiscard]] static std::int32_t resolve(auto&& schemaSystem, const char* className, const char* fieldName) noexcept
    {
        return schemaSystem.getFieldOffset(className, fieldName).value_or(0);
    }
};
