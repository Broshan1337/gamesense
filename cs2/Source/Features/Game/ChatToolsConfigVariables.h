#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

// Chat tools (Misc > CHAT / CHAT SPAM / CHAT FUN / PRANKS): everything here is NETWORKED through
// the engine console (`say`, `playerchatwheel`, the `name` userinfo convar, `cl_hud_color`) -
// everyone on the server sees the messages/names. Text lives in sidecar files next to the
// configs (the schema has no string type): chat_name.txt / chat_spam.txt / chatwheel.txt /
// chat_names.txt / chat_intel.txt, one line each.
namespace chat_params
{
// fake intel push-to-talk key (0 = off)
inline constexpr auto kIntelBindParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};
// radio phrase 1-based index into the wheel table (ChatTools.h kRadioPhrases; GoGoGo = 6)
inline constexpr auto kRadioPhraseParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 21, .def = 6};
}

namespace chat_vars
{

// lines per spam burst before the cooldown kicks in
inline constexpr auto kSpamCountParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 30, .def = 10};
// delay between two spam lines, 100ms units (2 = 200ms .. 100 = 10s) - servers kick faster
// floods, so the floor is deliberately not "one per tick"
inline constexpr auto kSpamIntervalParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 2, .max = 100, .def = 10};
// delay between two chatwheel (radio) lines, 100ms units - the server rate-limits fast
// bursts (~3 quick tries then blocked), but a slow cadence plays ALL GAME: slide to 150+
// (15s+) for sustainable spam. 255 = 25.5s.
inline constexpr auto kWheelIntervalParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 10, .max = 255, .def = 150};
// delay between two map pings (impulse 201), 100ms units - pings provably stack per-message
// server-side buckets (no shared token pool with chat), so a fast cadence just keeps working
// until the bucket rate itself trips. Floor 2 = 5 pings/sec.
inline constexpr auto kPingIntervalParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 2, .max = 100, .def = 10};
// teammate color cycle steps per second (max 64 = every game tick)
inline constexpr auto kColorCycleSpeedParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 64, .def = 64};
// seconds between name rotations (floor 5s - name changes are rate-checked serverside)
inline constexpr auto kNameCycleIntervalParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 5, .max = 120, .def = 30};
// ban theater: seconds between the fake-ban name going live and the `retry` reconnect
// (the real "X left the game" line that sells the illusion)
inline constexpr auto kTheaterDelayParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 3, .max = 30, .def = 8};

CONFIG_VARIABLE(SpamEnabled, bool, false);
CONFIG_VARIABLE_RANGE(SpamCount, kSpamCountParams);
CONFIG_VARIABLE_RANGE(SpamInterval, kSpamIntervalParams);

CONFIG_VARIABLE(WheelEnabled, bool, false);
CONFIG_VARIABLE_RANGE(RadioPhrase, chat_params::kRadioPhraseParams);
CONFIG_VARIABLE_RANGE(WheelInterval, kWheelIntervalParams);

CONFIG_VARIABLE(PingSpamEnabled, bool, false);
CONFIG_VARIABLE_RANGE(PingInterval, kPingIntervalParams);

CONFIG_VARIABLE(HudColorCycle, bool, false);
CONFIG_VARIABLE_RANGE(HudColorCycleSpeed, kColorCycleSpeedParams);

CONFIG_VARIABLE(NameCycleEnabled, bool, false);
CONFIG_VARIABLE_RANGE(NameCycleInterval, kNameCycleIntervalParams);

CONFIG_VARIABLE_RANGE(TheaterDelay, kTheaterDelayParams);

// Fake intel: while enabled the persona becomes "Server [VALVe]" and the bound key sends the
// chat_intel.txt line (throttled) - team/enemy intel that looks official.
CONFIG_VARIABLE(IntelEnabled, bool, false);
CONFIG_VARIABLE_RANGE(IntelBind, chat_params::kIntelBindParams);

// Killstreak radio: on your 3rd/5th/8th/12th kill without dying, fire a chatwheel voice line
// (real radio voice, everyone hears it). Resets on your death.
CONFIG_VARIABLE(StreakRadioEnabled, bool, false);

// Live badge: local "LIVE n" pill while someone spectates you + a one-shot `say` when a new
// spectator joins (the networked half - the badge itself is local-only by physics).
CONFIG_VARIABLE(LiveBadgeEnabled, bool, false);

// Fake kick: on the bound key, a ~180-frame burst of CmdKeyValues("InvalidSteamLogon",
// reason) makes the SERVER disconnect us with that reason code (everyone on the server sees
// the kick line). The value IS the ENetworkDisconnectReason code - the full list lives in
// the kick-reason dropdown entries (ChatTools.h kKickReasonEntries).
inline constexpr auto kKickReasonParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 255, .def = 163};
CONFIG_VARIABLE_RANGE(KickReason, kKickReasonParams);

CONFIG_VARIABLE_RANGE(KickKey, chat_params::kIntelBindParams);

// Name steal/apply: after Steam confirms the persona rename, run `retry` (reconnect to the last
// server) so the server picks up the new name - CS2 servers read the name at CONNECT and ignore
// mid-match `name` convar changes. Off = the name applies on your next natural reconnect.
CONFIG_VARIABLE(NameForceReconnect, bool, false);

}
