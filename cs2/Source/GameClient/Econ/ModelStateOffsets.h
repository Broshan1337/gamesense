#pragma once

#include <cstdint>

#include <CS2/Classes/CModelState.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <Utils/FieldOffset.h>














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
