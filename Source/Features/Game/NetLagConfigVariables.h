#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

#include <cstdint>

namespace net_lag_params
{
// All range knobs are uint8 - the config schema's range branch supports uint8/float only.
inline constexpr auto kChokeKeyBind = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};
inline constexpr auto kChokeTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 16, .def = 4};
inline constexpr auto kBlipCount = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 8, .def = 2};
inline constexpr auto kDupCount = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 4, .def = 0};
inline constexpr auto kDelayMs = RangeConstrainedVariableParams<std::uint8_t>{.min = 5, .max = 100, .def = 50};
// Flood burst: extra copies of every passing game datagram (the MMCrasher DupPercent
// equivalent). 0 = off. Meant to probe the server-side countermeasure stack: the engine2
// per-source rate limiter ("Traffic from %s was blocked for exceeding rate limits"), the SNS
// connection-level RateLimit_Recv_* config and the CQ command queue.
inline constexpr auto kFloodBurstCount = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 32, .def = 0};
inline constexpr auto kFloodKeyBind = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};
// Connectionless flood (the MMEvent successor): bursts of 0xFFFFFFFF-prefixed datagrams aimed at
// the game server, exercising the engine2 connectionless rate limiter directly (engine2
// 0x508800 dispatcher -> 0x557400 check -> 0x556B40 per-IP accounting). The limiter counts
// packets BEFORE command dispatch, so any connectionless-shaped payload reaches it. Fired per
// CreateMove tick while held (or continuously with the key Off) - 64 ticks/sec x count.
// WARNING: the server may netban your address on top of rate-blocking; own server only.
inline constexpr auto kConnlessFloodCount = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 64, .def = 0};
inline constexpr auto kConnlessKeyBind = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};
}

// NET LAG (own-server experiment, ported from the Harpoon reference's lag kit):
// datagram-level manipulation of the game's outgoing network traffic, implemented as a GOT hook
// over the sendto/sendmsg imports of libsteamnetworkingsockets.so (ALL CS2 transport, including
// community-server UDP, flows through that module's libc imports - see the 2026-09-07 netlag
// feasibility memory). Everything is applied per-datagram:
//
//   Choke / fakelag - withhold outgoing datagrams for ChokeTicks consecutive sends, then release.
//   The game believes it sent (we return success), so from the server's side this looks exactly
//   like real packet loss and from the client's side like S1-style fakelag. The server's CQ
//   (command queue) system is the countermeasure being studied: run "cq_logging 1" on the server
//   and watch bloat/starve classification while this is on.
//
//   Blips - zero-length datagrams sent the moment a choke window completes (the Harpoon
//   keep-alive trick: empty packets exercise the path without carrying input).
//
//   Dup - every passing datagram is additionally transmitted DupCount extra times (rate-limiter
//   pressure / duplicate-tolerance probe).
//
//   Delay - datagrams are buffered for DelayMs milliseconds and flushed later (fake ping).
//
// Hold-key choke: ChokeKeyBind (Bind.h encoding, 0 = Off) engages choke while held; with
// FakelagAlways the choke runs continuously while the master toggle is on. Key state is polled
// on the game thread (SDL state is not thread-safe) and published to the network thread through
// atomics - the send path itself only ever reads lock-free state.
namespace net_lag_vars
{
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE(FakelagAlways, bool, false);
CONFIG_VARIABLE_RANGE(ChokeKeyBind, net_lag_params::kChokeKeyBind);
CONFIG_VARIABLE_RANGE(ChokeTicks, net_lag_params::kChokeTicks);
CONFIG_VARIABLE_RANGE(BlipCount, net_lag_params::kBlipCount);
CONFIG_VARIABLE_RANGE(DupCount, net_lag_params::kDupCount);
CONFIG_VARIABLE(DelayEnabled, bool, false);
CONFIG_VARIABLE_RANGE(DelayMs, net_lag_params::kDelayMs);
CONFIG_VARIABLE(StatsEnabled, bool, false);
CONFIG_VARIABLE_RANGE(FloodBurstCount, net_lag_params::kFloodBurstCount);
// Hold-to-flood. Off = flood runs continuously while Enabled and FloodBurstCount > 0.
CONFIG_VARIABLE_RANGE(FloodKeyBind, net_lag_params::kFloodKeyBind);
CONFIG_VARIABLE_RANGE(ConnlessFloodCount, net_lag_params::kConnlessFloodCount);
// Hold-to-fire (per CreateMove tick). Off = fires every tick while Enabled and Count > 0.
CONFIG_VARIABLE_RANGE(ConnlessKeyBind, net_lag_params::kConnlessKeyBind);
}
