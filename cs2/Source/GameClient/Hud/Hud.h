#pragma once

#include <CS2/Constants/PanelIDs.h>

#include <Utils/CrashLogger.h>
#include <Utils/Lvalue.h>

#include "DeathNotices.h"

template <typename Context>
struct Hud {
    explicit Hud(Context context) noexcept
        : context{context}
    {
    }

    [[nodiscard]] decltype(auto) deathNotices() noexcept
    {
        return context.deathNoticesPanelHandle().getOrInit(findVisibleDeathNoticesPanel()).template as<DeathNotices>();
    }

    [[nodiscard]] decltype(auto) timerTextPanel() noexcept
    {
        return context.timerTextPanelHandle().getOrInit(findTimerTextPanel());
    }

    [[nodiscard]] decltype(auto) getHudReticle() noexcept
    {
        // The 5GB 2026-09-25 update dropped 'HudInWorld' from its parent's children array
        // (hidden panels leave the array; they keep the parent pointer), so the old
        // CSGOHud -> HudInWorld -> HudReticle findChildInLayoutFile walk returned null
        // forever and every in-world ESP panel died silently. HudReticle has exactly one
        // instance per session - look it up directly in the engine's slot array instead.
        // 0x37F = the direct slot-array lookup returned null (diagnostic).
        auto&& reticle = context.findUniquePanelById(cs2::panel_id::HudReticle);
        if (!static_cast<bool>(reticle))
            CrashLogger::trace(0x37F);
        return utils::lvalue<decltype(reticle)>(reticle);
    }

    // The full-screen HUD root - panels parented here span the whole screen, so corner alignment
    // means actual screen corners (HudReticle is only a mid-screen strip).
    [[nodiscard]] decltype(auto) rootPanel() noexcept
    {
        return context.panel();
    }

    [[nodiscard]] decltype(auto) scoreAndTimeAndBomb() noexcept
    {
        return context.scoreAndTimeAndBombPanelHandle().getOrInit(findScoreAndTimeAndBombPanel());
    }

    [[nodiscard]] decltype(auto) bombStatus() noexcept
    {
        return context.bombStatusPanelHandle().getOrInit(findBombStatusPanel());
    }

    [[nodiscard]] decltype(auto) hudTeamCounter() noexcept
    {
        return context.panel().findChildInLayoutFile(cs2::panel_id::HudTeamCounter);
    }

    [[nodiscard]] decltype(auto) bombPlantedPanel() noexcept
    {
        return context.bombPlantedPanelHandle().getOrInit(findBombPlantedPanel());
    }

private:
    [[nodiscard]] decltype(auto) hudDeathNotice() noexcept
    {
        return context.panel().findChildInLayoutFile(cs2::panel_id::HudDeathNotice);
    }

    [[nodiscard]] auto findVisibleDeathNoticesPanel() noexcept
    {
        return [this] { 
            return hudDeathNotice().findChildInLayoutFile(cs2::panel_id::VisibleNotices);
        };
    }

    [[nodiscard]] auto findBombStatusPanel() noexcept
    {
        return [this] {
            context.resetBombStatusVisibility();
            return scoreAndTimeAndBomb().findChildInLayoutFile(cs2::panel_id::BombStatus);
        };
    }

    [[nodiscard]] auto findBombPlantedPanel() noexcept
    {
        return [this] {
            return bombStatus().findChildInLayoutFile(cs2::panel_id::BombPlanted);
        };
    }

    [[nodiscard]] auto findScoreAndTimeAndBombPanel() noexcept
    {
        return [this] {
            // The 5GB 2026-09-25 panel-tree reorg moved 'ScoreAndTimeAndBomb' under a new
            // intermediate 'TeamCounter' panel and OUT of the visible children arrays
            // (live-verified: hidden panels keep a parent pointer but leave the array) -
            // the old HudTeamCounter.findChildInLayoutFile walk returned null forever.
            // Unique per-session HUD container: look it up in the engine's slot array.
            return context.findUniquePanelById(cs2::panel_id::ScoreAndTimeAndBomb);
        };
    }

    [[nodiscard]] auto findTimerTextPanel() noexcept
    {
        return [this] {
            return scoreAndTimeAndBomb().findChildInLayoutFile(cs2::panel_id::TimerText);
        };
    }

    Context context;
};
