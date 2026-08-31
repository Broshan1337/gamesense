#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <Utils/FieldOffset.h>

// Resolves C_CSPlayerPawn::m_hHudModelArms - the local player's first-person "arms" entity,
// which owns the actual rendered viewmodel weapon clones as scene-node children (see
// GetHudWeapon in SkinChanger.h). Confirmed as a real schema field via a direct string search
// of libclient.so ("m_hHudModelArms" - present verbatim), same confirmation standard as every
// other schema field this project resolves this way.
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
