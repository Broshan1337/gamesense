#pragma once

#include <tuple>

#include "ActiveWeaponAmmo/PlayerActiveWeaponAmmoPanel.h"
#include "PlayerAnalyzerTag/PlayerAnalyzerTagPanel.h"
#include "PlayerHealth/PlayerHealthPanel.h"
#include "PlayerPositionArrow/PlayerPositionArrowPanel.h"
#include "PlayerWeaponIcon/PlayerWeaponIconPanel.h"

// The Osiris state icons panel was removed 2026-10-13: our ESP flags (drawn
// next to the ESP box) replace it; the active weapon icon + ammo panels stay.
template <typename HookContext>
using PlayerInfoPanelTypes = std::tuple<
    PlayerPositionArrowPanel<HookContext>,
    PlayerHealthPanel<HookContext>,
    PlayerWeaponIconPanel<HookContext>,
    PlayerActiveWeaponAmmoPanel<HookContext>,
    PlayerAnalyzerTagPanel<HookContext>>;
