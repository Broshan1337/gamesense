#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <Utils/FieldOffset.h>

// Resolved once (via SchemaSystem) and cached for the lifetime of the process. m_nSubclassID
// is what the engine resolves a weapon's specific "subclass" (e.g. which knife type) from -
// see BaseWeapon::setSubclassID() and the knife changer experiment in SkinChanger.h.
struct EntitySubclassOffsets {
    explicit EntitySubclassOffsets(auto&& schemaSystem) noexcept
        : subclassID{resolve(schemaSystem, "m_nSubclassID")}
    {
    }

    FieldOffset<cs2::C_BaseEntity, cs2::C_BaseEntity::m_nSubclassID, std::int32_t> subclassID;

    [[nodiscard]] bool isFullyResolved() const noexcept
    {
        return static_cast<bool>(subclassID);
    }

private:
    [[nodiscard]] static std::int32_t resolve(auto&& schemaSystem, const char* fieldName) noexcept
    {
        return schemaSystem.getFieldOffset("C_BaseEntity", fieldName).value_or(0);
    }
};
