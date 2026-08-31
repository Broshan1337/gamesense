#pragma once

#include <cstdint>

#include <CS2/Classes/CModelState.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <Utils/FieldOffset.h>

// Resolves the chain used to reach a weapon's live CModelState (for SetMeshGroupMask-style
// legacy-paint-kit fixes): C_BaseEntity::m_CBodyComponent (schema-resolved) locates the
// CBodyComponent; from there a virtual call through its own vtable+112 (identified empirically,
// not by name - two independent decompiled callers, sub_D273B0 and sub_17EA710, both reach the
// exact same object type through this slot before touching model-state fields directly) yields
// the CModelState itself. m_MeshGroupMask's offset (520) is resolved the normal safe way too.
struct ModelStateOffsets {
    explicit ModelStateOffsets(auto&& schemaSystem) noexcept
        : bodyComponent{resolve(schemaSystem, "C_BaseEntity", "m_CBodyComponent")}
        , meshGroupMask{resolve(schemaSystem, "CModelState", "m_MeshGroupMask")}
    {
    }

    FieldOffset<cs2::C_BaseEntity, cs2::C_BaseEntity::m_CBodyComponent, std::int32_t> bodyComponent;
    FieldOffset<cs2::CModelState, cs2::CModelState::m_MeshGroupMask, std::int32_t> meshGroupMask;

    [[nodiscard]] bool isFullyResolved() const noexcept
    {
        return bodyComponent && meshGroupMask;
    }

private:
    [[nodiscard]] static std::int32_t resolve(auto&& schemaSystem, const char* className, const char* fieldName) noexcept
    {
        return schemaSystem.getFieldOffset(className, fieldName).value_or(0);
    }
};
