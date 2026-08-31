#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/C_EconEntity.h>
#include <Utils/FieldOffset.h>

// Resolved once (via SchemaSystem) and cached for the lifetime of the process instead of
// being looked up by name on every access.
struct EconEntityOffsets {
    explicit EconEntityOffsets(auto&& schemaSystem) noexcept
        : fallbackPaintKit{resolve(schemaSystem, "m_nFallbackPaintKit")}
        , fallbackSeed{resolve(schemaSystem, "m_nFallbackSeed")}
        , fallbackWear{resolve(schemaSystem, "m_flFallbackWear")}
        , fallbackStatTrak{resolve(schemaSystem, "m_nFallbackStatTrak")}
    {
    }

    FieldOffset<cs2::C_EconEntity, cs2::C_EconEntity::m_nFallbackPaintKit, std::int32_t> fallbackPaintKit;
    FieldOffset<cs2::C_EconEntity, cs2::C_EconEntity::m_nFallbackSeed, std::int32_t> fallbackSeed;
    FieldOffset<cs2::C_EconEntity, cs2::C_EconEntity::m_flFallbackWear, std::int32_t> fallbackWear;
    FieldOffset<cs2::C_EconEntity, cs2::C_EconEntity::m_nFallbackStatTrak, std::int32_t> fallbackStatTrak;

    // Whether this attempt produced a complete answer. A resolve made before the schema system is
    // ready returns 0 for every field, and 0 is already treated as "no offset" everywhere else
    // (FieldOffset::of refuses to dereference it), so "all non-zero" is the same standard the rest
    // of the code applies - not a new one. HookContext uses this to re-resolve instead of caching a
    // failed attempt for the lifetime of the process.
    [[nodiscard]] bool isFullyResolved() const noexcept
    {
        return fallbackPaintKit && fallbackSeed && fallbackWear && fallbackStatTrak;
    }

private:
    [[nodiscard]] static std::int32_t resolve(auto&& schemaSystem, const char* fieldName) noexcept
    {
        return schemaSystem.getFieldOffset("C_EconEntity", fieldName).value_or(0);
    }
};
