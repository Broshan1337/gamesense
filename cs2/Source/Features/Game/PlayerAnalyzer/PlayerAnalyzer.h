#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <Features/Game/PlayerAnalyzer/CheatOMeterState.h>
#include <Features/Game/PlayerAnalyzer/PlayerAnalyzerConfigVariables.h>
#include <GameClient/ChatPrinter.h>
#include <GameClient/EngineClientPointer.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerController.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/RetAddrSpoofer.h>
#include <Utils/VerifyConsole.h>

// CHEAT O METER analyzer. A purely local, opt-in suspicion analyzer: the user picks players in
// the menu, and for those ONLY this feature
//
//   * samples the networked eye angles (C_CSPlayerPawn::m_angEyeAngles) every game frame and
//     derives single-sample yaw velocity + snap counts (a rage flick delivers most of its turn
//     in one interpolated network update; legit flicks spread over many frames),
//   * accumulates shots (weapon_fire), hits and headshots (player_hurt attacker/hitgroup) from
//     the events the client already receives,
//   * folds those into a 0-100 suspicion score and publishes HUD rows for the panel.
//
// Everything is local observation - nothing is sent anywhere. Scores are HINTS, not verdicts:
// remote eye angles are interpolated at the sender's tickrate, so subtle legit smoothing mostly
// reads clean while blatant rage snapping does not. The raw columns are shown next to the score
// so the user can judge for themselves.
template <typename HookContext>
class PlayerAnalyzer {
public:
    explicit PlayerAnalyzer(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // Game thread, per frame (ViewRenderHook_onRenderStart, next to PlayerList::run).
    void run() noexcept
    {
        if (!GET_CONFIG_VAR(analyzer_vars::Enabled) || cheat_ometer::selectedCount() == 0) {
            cheat_ometer::publishHud(nullptr, 0);
            return;
        }

        const auto eyeOffset = hookContext.schemaSystem().getFieldOffset("C_CSPlayerPawn", "m_angEyeAngles");
        const auto nameOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_iszPlayerName");
        const auto steamIdOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_steamID");
        if (!eyeOffset.has_value() || *eyeOffset <= 0 || !nameOffset.has_value() || *nameOffset <= 0) {
            VerifyConsole::write(30.0f, "meter", "schema offsets unresolved - analyzer inactive");
            cheat_ometer::publishHud(nullptr, 0);
            return;
        }

        const auto now = hookContext.globalVars().curtime();
        if (!now.hasValue())
            return;

        cheat_ometer::HudRow rows[cheat_ometer::kMaxRows];
        int rowCount = 0;

        // CONTROLLER pass first: controllers exist for every connected player for the whole
        // session (dead or alive), so they anchor both selection pruning and per-slot identity.
        // The old pawn-first walk keyed "slot seen" on PAWN presence, which made the ghost
        // prune DESELECT selected players the moment they died - their checkbox (and HUD row)
        // vanished even though they were still in the game.
        bool controllerSeen[cheat_ometer::kMaxSlots]{};
        cs2::C_BaseEntity* controllers[cheat_ometer::kMaxSlots]{};
        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            if (!entityTypeInfo.template is<cs2::CCSPlayerController>())
                return;
            auto* const controllerEntity = static_cast<cs2::C_BaseEntity*>(entityIdentity.entity);
            // Key on the CONTROLLER's slot (entity index - 1) - the same numbering the picker
            // uses and the game events carry. The PAWN's entity index is a different number
            // space entirely (pawns land anywhere in the entity list, often past index 64).
            const auto slot = static_cast<int>(hookContext.template make<BaseEntity>(controllerEntity).handle().index().value) - 1;
            if (slot < 0 || slot >= cheat_ometer::kMaxSlots)
                return;
            controllers[slot] = controllerEntity;
            controllerSeen[slot] = true;
        });

        // prune GHOST selections: players who LEFT keep their slot marked selected (selection
        // is slot-keyed), inflating selectedCount until the picker cap blocks new picks
        // ("6 checked but 8/8" bug). A slot with NO CONTROLLER this frame is a departed player.
        // A missing PAWN is NOT a departure - dead players keep their controller while their
        // pawn may be gone, and they must stay selected (and listed).
        for (int slot = 0; slot < cheat_ometer::kMaxSlots; ++slot)
            if (!controllerSeen[slot] && cheat_ometer::isSelected(slot))
                cheat_ometer::setSelected(slot, false);

        for (int slot = 0; slot < cheat_ometer::kMaxSlots; ++slot) {
            auto* const controllerEntity = controllers[slot];
            if (!controllerEntity)
                continue;

            auto& s = cheat_ometer::stats[slot];
            if (!cheat_ometer::isSelected(slot)) {
                // deselected - drop the window so a re-select starts a fresh scan
                if (s.seen)
                    cheat_ometer::resetStats(slot);
                continue;
            }
            if (rowCount >= cheat_ometer::kMaxRows)
                break;

            // A different player taking over the slot must not inherit the old scan window.
            const auto controllerHandle = hookContext.template make<BaseEntity>(controllerEntity).handle().value;
            if (s.controllerHandle != 0 && s.controllerHandle != controllerHandle)
                cheat_ometer::resetStats(slot);
            s.controllerHandle = controllerHandle;

            const auto controllerBytes = reinterpret_cast<const std::byte*>(controllerEntity);
            // voice-tap attribution key: the controller's SteamID64 (matches CSVCMsg_VoiceData.xuid)
            if (steamIdOffset.has_value() && *steamIdOffset > 0)
                std::memcpy(&s.steamId, controllerBytes + *steamIdOffset, sizeof(s.steamId));
            copySanitized(s.name, reinterpret_cast<const char*>(controllerBytes + *nameOffset));

            auto&& controller = hookContext.template make<PlayerController>(static_cast<cs2::CCSPlayerController*>(controllerEntity));
            if (controller == hookContext.localPlayerController())
                continue; // the local player is never scanned

            // The pawn resolves THROUGH the controller's m_hPlayerPawn handle. While dead, the
            // pawn's angles stop updating (sample() skips them and re-baselines for respawn);
            // if the pawn is missing entirely the row below is still published from the stats,
            // so the panel never flickers.
            auto&& pawn = controller.pawn();
            if (auto* const pawnEntity = static_cast<cs2::C_BaseEntity*>(pawn))
                sample(s, hookContext.template make<PlayerPawn>(static_cast<cs2::C_CSPlayerPawn*>(pawnEntity)), *eyeOffset, now.value());

            if (!s.seen) {
                s.seen = true;
                s.startTime = now.value();
            }
            s.score = computeScore(s, now.value());
            rows[rowCount] = buildRow(s, slot, controller, now.value());
            ++rowCount;
            maybeCallout(s, now.value());
        }

        cheat_ometer::publishHud(rows, rowCount);
    }

    // Game thread, FireEventClientSide hook (next to TeamDamageTracker's handler).
    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event)
            return;

        // Per-match reset, same boundaries TeamDamageTracker uses. Selection is deliberately
        // kept: the picker list is live and slots persist across halves.
        if (game_events::is(event, "begin_new_match") || game_events::is(event, "warmup_end") || game_events::is(event, "cs_win_panel_match")) {
            cheat_ometer::resetAllStats();
            return;
        }

        if (!GET_CONFIG_VAR(analyzer_vars::Enabled) || cheat_ometer::selectedCount() == 0)
            return;

        if (game_events::is(event, "weapon_fire")) {
            const auto shooterSlot = game_events::entityForKey(event, "userid");
            if (shooterSlot >= 0 && shooterSlot < cheat_ometer::kMaxSlots && cheat_ometer::isSelected(static_cast<int>(shooterSlot))) {
                auto& shooterStats = cheat_ometer::stats[shooterSlot];
                ++shooterStats.shots;
                // view punch poisons rotation deltas for a few tenths after every shot
                if (const auto curtime = hookContext.globalVars().curtime(); curtime.hasValue())
                    shooterStats.lastShotTime = curtime.value();
            }
        } else if (game_events::is(event, "player_hurt")) {
            const auto attackerSlot = game_events::entityForKey(event, "attacker");
            if (attackerSlot < 0 || attackerSlot >= cheat_ometer::kMaxSlots || !cheat_ometer::isSelected(static_cast<int>(attackerSlot)))
                return;
            auto& s = cheat_ometer::stats[attackerSlot];
            if (!s.seen)
                return; // counters only count within a scan window
            ++s.hits;
            // CS2 hitgroups: 1 = head
            if (game_events::intForKey(event, "hitgroup", 0) == 1)
                ++s.headshots;
        }
    }

    // Chat callout: when a scanned player's score crosses the configured threshold, announce
    // them with the live stats. Local mode prints into our own chat feed (ChatPrinter); team
    // mode sends a real `say` broadcast (the lobby reads it - Killsay's sanitized pipe).
    // Cooldowns: 25s minimum per player, and a re-announce only when the score climbed by 15
    // since the last one - a static score never spams.
    void maybeCallout(cheat_ometer::Stats& s, float now) noexcept
    {
        if (!GET_CONFIG_VAR(analyzer_vars::Callout))
            return;
        const auto threshold = static_cast<int>(GET_CONFIG_VAR(analyzer_vars::CalloutThreshold));
        if (s.score < threshold)
            return;
        if (now - s.lastCalloutTime < 25.0f && s.score < s.lastCalloutScore + 15)
            return;

        s.lastCalloutTime = now;
        s.lastCalloutScore = s.score;

        char stats[96];
        if (s.voiceStrikes > 0)
            std::snprintf(stats, sizeof(stats), "%d deg/s, snap %d/min, voice strikes %d", static_cast<int>(s.peakSpeed), s.snaps, s.voiceStrikes);
        else
            std::snprintf(stats, sizeof(stats), "%d deg/s, snap %d/min, %d hits", static_cast<int>(s.peakSpeed), s.snaps, s.hits);

        char message[192];
        std::snprintf(message, sizeof(message), "[CHEAT-O-METER] %s is most likely a cheater! Score %d (%s)",
                      s.name, s.score, stats);

        if (GET_CONFIG_VAR(analyzer_vars::CalloutTeamChat)) {
            sayThroughConsole(message);
        } else {
            hookContext.template make<ChatPrinter>().print(message);
        }
    }

    // Killsay's sanitized `say` pipe: engine ExecuteClientCommand with quote/semicolon/control
    // bytes stripped (player names are attacker-controlled bytes).
    void sayThroughConsole(const char* text) const noexcept
    {
        const EngineClientPointer engineClient{};
        auto* const engine = engineClient.get();
        if (!engine)
            return;
        const auto vtable = *reinterpret_cast<void* const* const*>(engine);
        if (!vtable)
            return;
        const auto executeCommand = vtable[cs2::IEngineClient::kExecuteClientCommandVtableSlot];
        if (!executeCommand)
            return;

        char command[264]{"say "};
        std::size_t writeIndex = 4;
        for (std::size_t i = 0; text[i] != '\0' && writeIndex < sizeof(command) - 1 && i < 130; ++i) {
            const auto c = text[i];
            if (c == '"' || c == ';' || static_cast<unsigned char>(c) < 0x20)
                continue;
            command[writeIndex++] = c;
        }
        command[writeIndex] = '\0';

        RetAddrSpoofer::spoof(reinterpret_cast<cs2::IEngineClient::ExecuteClientCommand*>(executeCommand))(engine, 0, command, 1);
    }

private:
    void sample(cheat_ometer::Stats& s, auto&& pawn, int eyeOffset, float now) const noexcept
    {
        auto* const pawnEntity = static_cast<cs2::C_BaseEntity*>(pawn.baseEntity());
        if (!pawnEntity)
            return;

        // QAngle {pitch, yaw, roll}; yaw is index 1. A dead pawn's angles stop updating - skip
        // so the dt math cannot churn on frozen angles.
        const auto health = pawn.health();
        if (health.hasValue() && health.value() <= 0) {
            s.sampled = false; // force a fresh baseline on respawn
            return;
        }

        float eye[3];
        std::memcpy(eye, reinterpret_cast<const std::byte*>(pawnEntity) + eyeOffset, sizeof(eye));
        const float yaw = eye[1];

        if (!s.sampled) {
            s.lastYaw = yaw;
            s.lastSampleTime = now;
            s.sampled = true;
            return;
        }

        float dt = now - s.lastSampleTime;
        if (dt <= 0.0f || dt > 0.25f) {
            // backwards/ stalled clock (map change, hitch) - re-baseline, never score it
            s.lastYaw = yaw;
            s.lastSampleTime = now;
            return;
        }

        float dyaw = yaw - s.lastYaw;
        while (dyaw > 180.0f)
            dyaw -= 360.0f;
        while (dyaw < -180.0f)
            dyaw += 360.0f;

        const float speed = std::fabs(dyaw) / dt;
        if (speed > s.peakSpeed)
            s.peakSpeed = speed;

        const float snapThreshold = static_cast<float>(GET_CONFIG_VAR(analyzer_vars::SnapThreshold));
        if (std::fabs(dyaw) >= snapThreshold)
            ++s.snaps;

        // ---- sensitivity quantum vector ------------------------------------------------
        // A legit rotation delta = integer mouse counts x 0.022 x sensitivity. Feed the rolling
        // ring (skipping the poison cases Emily flagged: view punch right after shots, and
        // respawn/teleport angle SETS), re-fit the quantum periodically, count off-grid deltas.
        const float ady = std::fabs(dyaw);
        if (ady > 45.0f) {
            // angle SET (respawn/teleport/spawn angles), not mouse movement - discard the fit
            s.yawDeltaCount = 0;
            s.yawDeltaHead = 0;
            s.sensQuantum = 0.0f;
            s.sensFitError = 1.0f;
            s.sensSamples = 0;
        } else if (ady > 0.001f && now - s.lastShotTime > 0.6f) {
            s.yawDeltas[s.yawDeltaHead] = ady;
            s.yawDeltaHead = (s.yawDeltaHead + 1) % (sizeof(s.yawDeltas) / sizeof(s.yawDeltas[0]));
            s.yawDeltaCount = std::min(s.yawDeltaCount + 1, static_cast<int>(sizeof(s.yawDeltas) / sizeof(s.yawDeltas[0])));
            ++s.sensSamples;
            if (s.sensSamples % 32 == 0)
                fitQuantum(s);
        }

        s.lastYaw = yaw;
        s.lastSampleTime = now;
    }

    // Fit the rotation quantum: the smallest observed delta is k x q for some integer k, so try
    // smallestDelta / m for m = 1..8 and score each candidate by the mean distance of all
    // deltas to the nearest multiple. The winner's fit error self-reports confidence: real
    // mouse-driven rotations land on the grid; interpolated/smeared data never does (and we
    // never strike on a bad fit). sensitivity = quantum / 0.022 (m_yaw/m_pitch degrees/count).
    void fitQuantum(cheat_ometer::Stats& s) const noexcept
    {
        float d[sizeof(s.yawDeltas) / sizeof(s.yawDeltas[0])];
        int n = 0;
        for (int i = 0; i < s.yawDeltaCount; ++i) {
            const float v = s.yawDeltas[i];
            if (v > 0.005f)
                d[n++] = v;
        }
        if (n < 24) {
            s.sensQuantum = 0.0f;
            return;
        }
        std::sort(d, d + n);
        const float d0 = d[0];
        float bestQ = 0.0f, bestRel = 1e9f;
        for (int m = 1; m <= 8 && d0 / m > 0.0002f; ++m) {
            const float q = d0 / static_cast<float>(m);
            double err = 0.0;
            for (int i = 0; i < n; ++i) {
                float r = std::fmod(d[i], q);
                if (r > q * 0.5f)
                    r = q - r;
                err += r;
            }
            const float rel = static_cast<float>(err / n) / q;
            if (rel < bestRel) {
                bestRel = rel;
                bestQ = q;
            }
        }
        s.sensQuantum = bestQ;
        s.sensFitError = bestRel;

        // off-grid rotations = angle-writing cheats. Only on a CONFIDENT fit (mean residual
        // under 2% of the quantum), only for meaningful deltas, one strike per fit round.
        if (bestRel < 0.02f && bestQ > 0.002f) {
            int mismatches = 0;
            for (int i = 0; i < n; ++i) {
                if (d[i] < 0.05f)
                    continue;
                float r = std::fmod(d[i], bestQ);
                if (r > bestQ * 0.5f)
                    r = bestQ - r;
                if (r > bestQ * 0.12f)
                    ++mismatches;
            }
            if (mismatches > 0)
                ++s.aimStrikes;
        }
    }

    // Weighted sum of the components that have enough data; weights renormalize over what is
    // available so a fresh scan does not read clean just because counters are still empty.
    [[nodiscard]] static int computeScore(const cheat_ometer::Stats& s, float now) noexcept
    {
        const float elapsed = std::max(now - s.startTime, 1.0f);

        // aim speed: log scale 1500 deg/s -> 0, ~30000 deg/s -> 100
        float speedScore = 0.0f;
        if (s.peakSpeed > 1500.0f)
            speedScore = std::clamp((std::log10(s.peakSpeed) - 3.176f) / (4.477f - 3.176f), 0.0f, 1.0f);

        // snaps: 0/min -> 0, 10+/min -> 100
        const float snapsPerMin = std::clamp(static_cast<float>(s.snaps) * 60.0f / elapsed, 0.0f, 1000.0f);
        const float snapScore = std::clamp(snapsPerMin / 10.0f, 0.0f, 1.0f);

        float total = speedScore * 0.35f + snapScore * 0.25f;
        float weights = 0.60f;

        // accuracy: needs a real sample; 45% -> 0, 75%+ -> 100
        if (s.shots >= 10) {
            const float acc = static_cast<float>(s.hits) / static_cast<float>(s.shots);
            total += std::clamp((acc - 0.45f) / 0.30f, 0.0f, 1.0f) * 0.25f;
            weights += 0.25f;
        }

        // headshot rate among hits: 30% -> 0, 70%+ -> 100
        if (s.hits >= 5) {
            const float hs = static_cast<float>(s.headshots) / static_cast<float>(s.hits);
            total += std::clamp((hs - 0.30f) / 0.40f, 0.0f, 1.0f) * 0.15f;
            weights += 0.15f;
        }

        // weird-voice wire strikes: 1 = suspicious, 3+ = the wire itself is not voice
        // (ESP-share payloads smuggled into CMsgVoiceAudio). Saturates at 3.
        if (s.voiceStrikes > 0) {
            total += std::clamp(static_cast<float>(s.voiceStrikes) / 3.0f, 0.0f, 1.0f) * 0.30f;
            weights += 0.30f;
        }

        // rotation-quantum strikes: deltas that don't sit on the player's own sensitivity grid
        // (anti-aim / angle-writing aimbots). Saturates at 4 strikes.
        if (s.aimStrikes > 0) {
            total += std::clamp(static_cast<float>(s.aimStrikes) / 4.0f, 0.0f, 1.0f) * 0.25f;
            weights += 0.25f;
        }

        return static_cast<int>(std::clamp(total / weights, 0.0f, 1.0f) * 100.0f + 0.5f);
    }

    [[nodiscard]] static cheat_ometer::HudRow buildRow(const cheat_ometer::Stats& s, int slot, auto&& controller, float now) noexcept
    {
        cheat_ometer::HudRow row;
        std::memcpy(row.name, s.name, sizeof(row.name));
        row.slot = slot;
        // The pawn's team is authoritative while the pawn is resolvable; the controller's own
        // team (C_BaseEntity field) covers the pawn-missing case so dead players keep their row.
        auto&& pawn = controller.pawn();
        if (auto* const pawnEntity = static_cast<cs2::C_BaseEntity*>(pawn))
            row.team = static_cast<int>(pawn.teamNumber());
        else
            row.team = static_cast<int>(controller.teamNumber());
        row.score = s.score;
        row.peakSpeed = static_cast<int>(s.peakSpeed);
        const float elapsed = std::max(now - s.startTime, 1.0f);
        row.snapsPerMin = static_cast<int>(static_cast<float>(s.snaps) * 60.0f / elapsed + 0.5f);
        if (s.shots >= 10)
            row.accPct = static_cast<int>(static_cast<float>(s.hits) / static_cast<float>(s.shots) * 100.0f + 0.5f);
        if (s.hits >= 5)
            row.hsPct = static_cast<int>(static_cast<float>(s.headshots) / static_cast<float>(s.hits) * 100.0f + 0.5f);
        row.voiceStrikes = s.voiceStrikes;
        // sensitivity readout: only when the quantum fit is confident
        if (s.sensQuantum > 0.0f && s.sensFitError < 0.02f) {
            row.sensitivity = s.sensQuantum / 0.022f;
            row.sensFitError = s.sensFitError;
        }
        row.aimStrikes = s.aimStrikes;
        row.shots = s.shots;
        row.elapsed = static_cast<int>(elapsed);
        return row;
    }

    static void copySanitized(char* destination, const char* name) noexcept
    {
        if (!name || name[0] == '\0')
            name = "?";
        std::size_t i = 0;
        for (; name[i] != '\0' && i < cheat_ometer::kMaxNameLen - 1; ++i) {
            const auto c = static_cast<unsigned char>(name[i]);
            if (c < 0x20 && c != '\t')
                break;
            destination[i] = static_cast<char>(c);
        }
        if (i == 0)
            destination[i++] = '?';
        destination[i] = '\0';
    }

    HookContext& hookContext;
};
