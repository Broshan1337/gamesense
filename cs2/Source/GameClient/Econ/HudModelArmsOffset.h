#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <Utils/FieldOffset.h>






struct HudModelArmsOffset {
    explicit HudModelArmsOffset(auto&& schemaSystem) noexcept
        : hudModelArms{resolve(schemaSystem)}
    {
    }

    FieldOffset<cs2::C_CSPlayerPawn, cs2::C_CSPlayerPawn::m_hHudModelArms, std::int32_t> hudModelArms;

    [[nodiscard]] bool isFullyResolved() const noexcept
    {
        return static_cast<bool>(hudModelArms);
    }

private:
    [[nodiscard]] static std::int32_t resolve(auto&& schemaSystem) noexcept
    {
        return schemaSystem.getFieldOffset("C_CSPlayerPawn", "m_hHudModelArms").value_or(0);
    }
};
