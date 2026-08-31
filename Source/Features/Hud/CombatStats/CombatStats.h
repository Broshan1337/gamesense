#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

#include <CS2/Classes/IGameEventManager2.h>
#include <Features/Hud/CombatStats/CombatStatsParams.h>
#include <Features/Hud/CombatStats/CombatStatsState.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <GameClient/Panorama/PanoramaLabel.h>
#include <GameClient/Panorama/PanoramaUiEngine.h>
#include <GameClient/PlayerSlotLookup.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/StringBuilder.h>

// Hits/misses counting + a small event feed. Purely local bookkeeping over events the client
// already receives; nothing is sent anywhere.
//
//   shots = weapon_fire events by the local player - one event per TRIGGER PULL, which matches
//           the intended semantics exactly: every shot counts, wherever it lands (bullet_impact
//           was tried first and is wrong for this: it only fires when the bullet actually hits a
//           surface, so skybox/near-miss shots were never counted as misses)
//   hits  = player_hurt events by the local player against an ENEMY (friendly damage and world
//           damage excluded - those are not "hits" in any useful sense)
//   misses = shots - hits (saturating; converges once both events of a shot arrive)
//
// Feed lines (newest first, consecutive misses coalesce into "missed xN"):
//   [gs] hit <name> for <dmg> dmg [head]   /   [gs] missed xN   /   [gs] killed <name>
template <typename HookContext>
class CombatStats {
public:
    explicit CombatStats(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event)
            return;

        // Same reset boundaries TeamDamageTracker uses - session stats are per-match, or a
        // "HITS 400" from last night's session would still be on screen tomorrow.
        if (game_events::is(event, "begin_new_match") || game_events::is(event, "warmup_end") || game_events::is(event, "cs_win_panel_match")) {
            shotsFired = 0;
            hitsLanded = 0;
            missStreak = 0;
            for (auto& line : lineTexts)
                line[0] = '\0';
            return;
        }

        if (game_events::is(event, "weapon_fire")) {
            if (game_events::localPlayerIsSlot(hookContext, game_events::entityForKey(event, "userid")))
                ++shotsFired;
            return;
        }

        if (game_events::is(event, "player_hurt")) {
            onPlayerHurt(event);
            return;
        }

        if (game_events::is(event, "player_death"))
            onPlayerDeath(event);
    }

    void run() const noexcept
    {
        const double now = monotonicSeconds();

        auto&& counterPanel = uiEngine().getPanelFromHandle(state().hitsPanelHandle);
        if (!counterPanel) {
            if (now - lastCreateAttempt < 1.0)
                return;
            lastCreateAttempt = now;
            createPanels();
            return;
        }

        if (now - lastTextUpdate < 0.15)
            return;
        lastTextUpdate = now;

        updateCounterLabels();
        updateFeedLabels();
    }

    void onUnload() const noexcept
    {
        hookContext.template make<PanoramaUiEngine>().deletePanelByHandle(state().counterBoxPanelHandle);
        hookContext.template make<PanoramaUiEngine>().deletePanelByHandle(state().feedBoxPanelHandle);
    }

private:
    void onPlayerHurt(cs2::IGameEvent* event) const noexcept
    {
        if (!game_events::localPlayerIsAttacker(hookContext, event))
            return;

        const auto victimSlot = game_events::entityForKey(event, "userid");
        auto&& lookup = hookContext.template make<PlayerSlotLookup>();
        if (!lookup.isValidSlot(victimSlot))
            return;

        auto&& victimPawn = lookup.pawnBySlot(victimSlot);
        if (!victimPawn || victimPawn.teamNumber() == localTeamNumber())
            return;   // friendly fire / self-damage is not a "hit" in any useful sense

        ++hitsLanded;

        const auto damage = game_events::intForKey(event, "dmg_health");
        const auto hitgroup = game_events::intForKey(event, "hitgroup");
        char line[96];
        {
            StringBuilder builder{line};
            builder.put('[', 'g', 's', ']', ' ', 'h', 'i', 't', ' ', lookup.nameBySlot(victimSlot), ' ', 'f', 'o', 'r', ' ', damage, ' ', 'd', 'm', 'g');
            if (hitgroup == 1)
                builder.put(' ', '[', 'h', 'e', 'a', 'd', ']');
            pushFeedLine(builder.cstring());
        }
    }

    void onPlayerDeath(cs2::IGameEvent* event) const noexcept
    {
        if (!game_events::localPlayerIsAttacker(hookContext, event))
            return;

        const auto victimSlot = game_events::entityForKey(event, "userid");
        auto&& lookup = hookContext.template make<PlayerSlotLookup>();
        if (!lookup.isValidSlot(victimSlot))
            return;

        char line[96];
        StringBuilder builder{line};
        builder.put('[', 'g', 's', ']', ' ', 'k', 'i', 'l', 'l', 'e', 'd', ' ', lookup.nameBySlot(victimSlot));
        pushFeedLine(builder.cstring());
    }

    // The victim's team is only meaningful compared against OURS - the local pawn's team number
    // read is the same one the triggerbot/aimbot targeting uses.
    [[nodiscard]] auto localTeamNumber() const noexcept
    {
        return hookContext.activeLocalPlayerPawn().teamNumber();
    }

    void pushFeedLine(const char* line) const noexcept
    {
        // Coalesce consecutive misses: "[gs] missed" -> "[gs] missed x2" -> ... instead of a
        // line per bullet of a missed spray.
        if (std::strncmp(line, "[gs] missed", 11) == 0) {
            if (missStreak > 0) {
                char rebuilt[32];
                StringBuilder builder{rebuilt};
                builder.put('[', 'g', 's', ']', ' ', 'm', 'i', 's', 's', 'e', 'd', ' ', 'x', ++missStreak);
                std::memcpy(lineTexts[0], rebuilt, sizeof(rebuilt));
                return;
            }
            ++missStreak;
        } else {
            missStreak = 0;
        }

        // Newest first: shift the ring down, drop the oldest past kFeedLines.
        constexpr std::size_t kFeedLines = sizeof(lineTexts) / sizeof(lineTexts[0]);
        for (std::size_t i = kFeedLines - 1; i > 0; --i)
            std::memcpy(lineTexts[i], lineTexts[i - 1], sizeof(lineTexts[0]));
        std::strncpy(lineTexts[0], line, sizeof(lineTexts[0]) - 1);
        lineTexts[0][sizeof(lineTexts[0]) - 1] = '\0';
    }

    void updateCounterLabels() const noexcept
    {
        using namespace combat_stats_params;

        // cstring() everywhere: StringBuilder does NOT null-terminate on put() - passing the raw
        // buffer would let stale bytes from the previous label leak into the text (that is where
        // the mysterious "50%S 7" came from).
        const auto misses = shotsFired >= hitsLanded ? shotsFired - hitsLanded : 0u;
        char buffer[32];

        StringBuilder hitsBuilder{buffer};
        hitsBuilder.put('H', 'I', 'T', 'S', ' ', hitsLanded);
        uiEngine().getPanelFromHandle(state().hitsPanelHandle).clientPanel().template as<PanoramaLabel>().setText(hitsBuilder.cstring());

        StringBuilder missBuilder{buffer};
        missBuilder.put('M', 'I', 'S', 'S', ' ', misses);
        uiEngine().getPanelFromHandle(state().missesPanelHandle).clientPanel().template as<PanoramaLabel>().setText(missBuilder.cstring());

        // Hit ratio over shots taken; "--" until the first shot so it never reads as 0% skill.
        if (auto&& ratioPanel = uiEngine().getPanelFromHandle(state().ratioPanelHandle)) {
            StringBuilder ratioBuilder{buffer};
            ratioBuilder.put('A', 'C', 'C', ' ');
            if (shotsFired == 0)
                ratioBuilder.put('-', '-');
            else
                ratioBuilder.put(static_cast<int>((static_cast<std::uint64_t>(hitsLanded) * 100) / shotsFired));
            ratioBuilder.put('%');
            ratioPanel.clientPanel().template as<PanoramaLabel>().setText(ratioBuilder.cstring());
        }
    }

    void updateFeedLabels() const noexcept
    {
        constexpr std::size_t kFeedLines = sizeof(lineTexts) / sizeof(lineTexts[0]);
        for (std::size_t i = 0; i < kFeedLines; ++i) {
            auto&& panel = uiEngine().getPanelFromHandle(state().feedLineHandles[i]);
            if (!panel)
                continue;
            panel.clientPanel().template as<PanoramaLabel>().setText(lineTexts[i]);
        }
    }

    void createPanels() const noexcept
    {
        using namespace combat_stats_params;

        // Hits/misses box above the status chips
        auto&& counterBox = hookContext.panelFactory().createPanel(hookContext.hud().rootPanel()).uiPanel();
        if (counterBox) {
            counterBox.setFlowChildren(cs2::k_EFlowRight);
            counterBox.setBackgroundColor(kBoxColor);
            counterBox.setBorderRadius(kBoxBorderRadius);
            counterBox.setAlign(kBottomAlignment);
            counterBox.setMargin(kCounterBoxMargin);
            state().counterBoxPanelHandle = counterBox.getHandle();

            auto&& hitsLabel = hookContext.panelFactory().createLabelPanel(counterBox).uiPanel();
            if (hitsLabel) {
                hitsLabel.setFont(kFont);
                hitsLabel.setColor(kTextColor);
                hitsLabel.setMargin(kRowMargin);
                state().hitsPanelHandle = hitsLabel.getHandle();
            }
            auto&& missesLabel = hookContext.panelFactory().createLabelPanel(counterBox).uiPanel();
            if (missesLabel) {
                missesLabel.setFont(kFont);
                missesLabel.setColor(kMissColor);
                missesLabel.setMargin(kRowMargin);
                state().missesPanelHandle = missesLabel.getHandle();
            }
            auto&& ratioLabel = hookContext.panelFactory().createLabelPanel(counterBox).uiPanel();
            if (ratioLabel) {
                ratioLabel.setFont(kFont);
                ratioLabel.setColor(kTextColor);
                ratioLabel.setMargin(kRowMargin);
                state().ratioPanelHandle = ratioLabel.getHandle();
            }
        }

        // Event feed under the radar
        auto&& feedBox = hookContext.panelFactory().createPanel(hookContext.hud().rootPanel()).uiPanel();
        if (feedBox) {
            feedBox.setFlowChildren(cs2::k_EFlowDown);
            feedBox.setAlign(kTopAlignment);
            feedBox.setMargin(kFeedBoxMargin);
            state().feedBoxPanelHandle = feedBox.getHandle();

            for (std::size_t i = 0; i < sizeof(lineTexts) / sizeof(lineTexts[0]); ++i) {
                auto&& line = hookContext.panelFactory().createLabelPanel(feedBox).uiPanel();
                if (!line)
                    continue;
                line.setFont(kFont);
                line.setColor(kTextColor);
                line.setMargin(kFeedLineMargin);
                state().feedLineHandles[i] = line.getHandle();
            }
        }
    }

    [[nodiscard]] auto& state() const noexcept
    {
        return hookContext.featuresStates().hudFeaturesStates.combatStatsState;
    }

    [[nodiscard]] decltype(auto) uiEngine() const noexcept
    {
        return hookContext.template make<PanoramaUiEngine>();
    }

    [[nodiscard]] static double monotonicSeconds() noexcept
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1.0e-9;
    }

    // Process-lifetime counters + feed ring (feature objects are rebuilt per call).
    inline static std::uint32_t shotsFired{0};
    inline static std::uint32_t hitsLanded{0};
    inline static int missStreak{0};
    inline static char lineTexts[5][96]{};
    inline static double lastTextUpdate{-1.0e9};
    inline static double lastCreateAttempt{-1.0e9};

    HookContext& hookContext;
};
