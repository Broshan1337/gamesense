#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

// Player Analyzer ("CHEAT O METER", Misc > PLAYER ANALYZER): a purely local suspicion
// analyzer. The user multi-selects players to scan; for those ONLY, the feature samples the
// networked eye angles (C_CSPlayerPawn::m_angEyeAngles - the client receives everyone's aim
// angles for animation), counts single-frame angle jumps (snaps), and accumulates per-player
// accuracy/headshot counters from the player_hurt/weapon_fire events the client already gets.
// Nothing is sent to the server and no other player can see any of it.
namespace analyzer_params
{
// a single-sample yaw jump (degrees) counted as an aimbot-style snap. Remote eye angles are
// interpolated, so legit flicks spread over several frames stay below it while most of a
// 90-degree rage flick lands in one frame.
inline constexpr auto kSnapThresholdParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 5, .max = 90, .def = 25};
// chat callout score threshold (0-100)
inline constexpr auto kCalloutThreshold = RangeConstrainedVariableParams<std::uint8_t>{.min = 10, .max = 100, .def = 50};
}

namespace analyzer_vars
{

// master switch: sampling + the CHEAT O METER HUD panel
CONFIG_VARIABLE(Enabled, bool, false);
// in-world tag on the info panel of every scanned player (score + verdict word)
CONFIG_VARIABLE(EspTag, bool, true);
CONFIG_VARIABLE_RANGE(SnapThreshold, analyzer_params::kSnapThresholdParams);
// voice receive tap calibration probe (VoiceTapHook): dumps raw CSVCMsg_VoiceData objects
// ([vtap] console lines) so the parsed-field offsets are measured, not guessed
CONFIG_VARIABLE(VoiceProbe, bool, false);
// voice forensics: weird-voice wire packets (non-opus format, degenerate fields - the
// shared-ESP smuggler signature) append to <configDir>/voice_captures.log with the matched
// player's name, and count as CHEAT O METER voice strikes (score component)
CONFIG_VARIABLE(VoiceLog, bool, false);

// Chat callout: when a scanned player's score crosses the threshold, announce them with the
// live stats. Local = our own chat feed only (ChatPrinter); Team Chat = a real `say` broadcast
// (the lobby sees it - and so does the accused). Re-announces only when the score climbs.
CONFIG_VARIABLE(Callout, bool, false);
CONFIG_VARIABLE(CalloutTeamChat, bool, false);
// score threshold for the callout (the score components saturate: 50 = clearly suspicious)
CONFIG_VARIABLE_RANGE(CalloutThreshold, analyzer_params::kCalloutThreshold);

}
