#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/C_EconEntity.h>
#include <Utils/FieldOffset.h>



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
