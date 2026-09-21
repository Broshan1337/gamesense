#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

#include <cstdint>

namespace userinfo_flood_params
{
// Setinfo commands queued per flood tick (round-robin over the selected fields).
inline constexpr auto kSendsPerTick = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 64, .def = 4};
// One flood tick every N game ticks (64 tps: def 4 x SendsPerTick 4 = 16 userinfo updates/s; 1 x 64 = max rate).
inline constexpr auto kEveryTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 32, .def = 1};
}

namespace userinfo_flood_vars
{

// USERINFO FLOOD (MC addon philosophy, item 3): floods SEVERAL userinfo convars per tick through
// the setinfo path - the server validates every field of every userinfo packet, so several
// fields in one tick multiply server-side validation work per packet. Every send changes the
// value (a repeated value coalesces into an engine warning instead of a server-side update).
//
// Heavy Values (the CS2 stringtable-churn analog, item 4 - RE note: CS2 has NO client->server
// stringtable message at all, and CNetworkStringTable::AddString is local-only; the one client
// action that makes the server rewrite + broadcast a stringtable row IS the userinfo change, so
// the churn rides the same packet): pumps the `name` field (the only STRING userinfo cvar) to
// ~155 bytes of zero-width filler - every row rewrite + delta broadcast carries max bytes while
// the scoreboard renders the name unchanged.
//
// Values are snapshotted at engage and restored (setinfo) when the flood is disabled or the
// Restore Values button fires. Fields self-verify at runtime: a missing cvar, a shifted
// CConVar layout or an unpatchable flag set drops that field (fail closed) and it is never
// sent. The `name` field stands down while the name animator owns the name.
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE(HeavyMode, bool, false);
// Direct net-message mode: instead of queueing `setinfo` console commands (whose engine-side
// "queue userinfo cvar changes -> send" machinery coalesces/throttles the churn so most of it
// never reaches the server - user-observed 2026-09-13), build genuine CNETMsg_SetConVar
// messages (record id 6, every engaged field bundled per message, fresh values) and hand them
// to the net channel through the GameClient/NetMessageFactory.h pipeline - the same proven
// path the Server Lagger uses for VoiceData. One message = one full server-side userinfo
// validation + userinfo-table row rewrite + delta broadcast. Restore still rides setinfo
// (restore correctness does not care about the rate).
CONFIG_VARIABLE(DirectMode, bool, false);
CONFIG_VARIABLE(FloodName, bool, false);
CONFIG_VARIABLE(FloodClutch, bool, false);
CONFIG_VARIABLE(FloodTeamColor, bool, false);
CONFIG_VARIABLE(FloodXhStyle, bool, false);
CONFIG_VARIABLE(FloodXhColor, bool, false);
CONFIG_VARIABLE(FloodXhSize, bool, false);
CONFIG_VARIABLE(FloodXhGap, bool, false);
CONFIG_VARIABLE(FloodXhThick, bool, false);
CONFIG_VARIABLE(FloodXhOutline, bool, false);
CONFIG_VARIABLE(FloodXhDot, bool, false);
CONFIG_VARIABLE(FloodXhAlpha, bool, false);
CONFIG_VARIABLE(FloodXhSniper, bool, false);
CONFIG_VARIABLE(FloodLoadout, bool, false);
CONFIG_VARIABLE(FloodTeamId, bool, false);
CONFIG_VARIABLE(FloodXhColorR, bool, false);
CONFIG_VARIABLE(FloodXhColorG, bool, false);
CONFIG_VARIABLE(FloodXhColorB, bool, false);
CONFIG_VARIABLE_RANGE(SendsPerTick, userinfo_flood_params::kSendsPerTick);
CONFIG_VARIABLE_RANGE(EveryTicks, userinfo_flood_params::kEveryTicks);

}