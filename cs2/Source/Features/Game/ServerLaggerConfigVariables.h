#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

#include <cstdint>

#include <GameClient/Bind.h>

namespace server_lagger_params
{
// Fully custom profile (replaces the friend's two fixed presets - both presets just saturate
// the same ~98KB/tick channel buffer, so the tunable dimensions are: messages per batch
// (per-message server parse/decode/relay cost) and audio bytes per message (bytes per
// message on the wire and through the server's decompressor)).
inline constexpr auto kMsgsPerBatch = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 65, .def = 65};
// Audio payload bytes per message, in KB (x1024). 15 max keeps the framed payload inside the
// 0x60-byte message object's protobuf limits and the 2-byte varint length encodings.
inline constexpr auto kAudioKB = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 15, .def = 1};
// Batches queued per tick (the channel's send buffer takes ~98KB/tick - beyond ~119 datagrams
// SendNetMessage refuses until the next frame flush, so counts above that just ride the
// backpressure ceiling; the friend's slider goes to 2000 for the same effect - the engine's
// backpressure is the real limit). u16 so the friend's Mode 3 counts fit the slider.
inline constexpr auto kAmount = RangeConstrainedVariableParams<std::uint16_t>{.min = 1, .max = 2000, .def = 14};
// Audio byte content selector. The engine's VoiceData parse VALIDATES the audio content (an
// all-0xFF payload fails the factory parse - live-verified 2026-09-13 - so there is no
// constant-run mode): zeros (and any compressible run) compress to almost nothing - pure
// parse/decode-call storm on the server; random and counter bytes are incompressible - every
// relayed voice byte costs the server's uplink for real (and any byte that survives the opus
// decoder plays as white noise for the listeners).
inline constexpr auto kPayloadMode = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 4, .def = 0};
// Lag-O-Meter HUD window offsets from its default anchor in the right-hand HUD stack, in
// screen pixels: +X moves LEFT of the anchor, +Y moves DOWN (drag direction). Negative values
// go the opposite way (above/right of the anchor) - 0 is NOT a barrier, it is just the default
// anchor. Float range so the window can live anywhere on modern screens; the mouse drag
// writes the same offsets.
inline constexpr auto kMeterOffsetRange = RangeConstrainedVariableParams<float>{.min = -4096.0f, .max = 4096.0f, .def = 0.0f};
// Loop freeze: ticks the lagger queues WITHOUT commit/transmit before releasing everything as
// one burst (the ~98KB/tick SendNetMessage backpressure is the natural queue ceiling). The
// friend UI's "Loop stop after" analog.
inline constexpr auto kFreezeTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 128, .def = 25};
// Profile preset index: 0 = Custom (sliders as-is), 1..k = the menu's kLaggerPreset table.
inline constexpr auto kPreset = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 3, .def = 0};
// Slow ramp (friend v2's anti-self-kick technique): start at 1 datagram/tick and climb +1
// every RampInterval seconds until Amount - slamming the max instantly was the self-overflow
// kick. Sweet spot per the friend: ~12 datagrams/tick for the relay-amplifier profile.
inline constexpr auto kRampInterval = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 10, .def = 2};
// Hold-key gate: Off = the Enabled checkbox alone; a bind requires the key held while enabled.
inline constexpr auto kLaggerBind = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};
// Pulse duty cycle: burst PulseOn ticks, rest PulseOff ticks - pauses let the channel drain so
// every burst actually lands (choked-channel bytes are wasted), and breaks the constant-rate
// signature.
inline constexpr auto kPulseOn = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 32, .def = 8};
inline constexpr auto kPulseOff = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 64, .def = 8};
}

namespace server_lagger_vars
{

// SERVER LAGGER (friend-source port, Linux slot-verified 2026-09-12): floods CCLCMsg_VoiceData
// (message type 22) through the game's OWN net channel - the message factory builds a real
// CCLCMsg_VoiceData from a hand-built protobuf, so every datagram is engine-framed/encrypted and
// indistinguishable from real voice. The server decompresses + validates each message AND relays
// voice to every other player: per-packet validation work multiplied by messages x batches per
// tick. One batch per game tick (client pointer + tick guard). Every anchor is verified before
// first use and the feature fails closed on any drift.
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(MsgsPerBatch, server_lagger_params::kMsgsPerBatch);
CONFIG_VARIABLE_RANGE(AudioKB, server_lagger_params::kAudioKB);
CONFIG_VARIABLE_RANGE(Amount, server_lagger_params::kAmount);
CONFIG_VARIABLE_RANGE(PayloadMode, server_lagger_params::kPayloadMode);

// Lag-O-Meter: the live flood-throughput window (offered KB/s + refused/s), rendered as a
// keybind-list-style HUD overlay instead of a menu row - visible while flooding without the
// menu open. Off by default.
CONFIG_VARIABLE(MeterEnabled, bool, false);
CONFIG_VARIABLE_RANGE(MeterOffsetX, server_lagger_params::kMeterOffsetRange);
CONFIG_VARIABLE_RANGE(MeterOffsetY, server_lagger_params::kMeterOffsetRange);

// Everything below appended LAST (order-sensitive parser; the keys above existed in already-
// shipped config files and must not move).

// Profile preset selector (0 = Custom = the sliders as-is; 1+ writes the sliders - see the
// kLaggerPreset table in the menu). A dropdown QoL on top of the sliders, not a replacement.
CONFIG_VARIABLE_RANGE(Preset, server_lagger_params::kPreset);
// Datagram mode: each message leaves as its OWN datagram (commit+transmit per message,
// MsgsPerBatch forced to 1) - the friend's "Datagrams: N" cost profile: many tiny datagrams
// = per-datagram engine framing/parse work on the server instead of relay-bandwidth amplification.
CONFIG_VARIABLE(DatagramMode, bool, false);
// Loop freeze: queue without commit/transmit for FreezeTicks ticks, then release the whole
// backlog as one burst (choke-then-snap; the friend's "Loop freeze").
CONFIG_VARIABLE(LoopFreeze, bool, false);
CONFIG_VARIABLE_RANGE(FreezeTicks, server_lagger_params::kFreezeTicks);
// Auto stop: after 2 consecutive datagram-transmit refusals (the overflow-flag state that ends
// in the NETWORK_DISCONNECT_OVERFLOW self-kick), disable the lagger instead of riding it down.
CONFIG_VARIABLE(AutoStop, bool, false);

// Everything below appended LAST (order-sensitive parser) - friend v2 parity batch.
CONFIG_VARIABLE(SlowRamp, bool, false);
CONFIG_VARIABLE_RANGE(RampInterval, server_lagger_params::kRampInterval);
CONFIG_VARIABLE_RANGE(LaggerKey, server_lagger_params::kLaggerBind);
// DECODE STORM: offsets envelope sets num_packets (field 7) to the offsets count - receivers
// iterate that many "packets" per message, moving decode work from server-only to every client.
CONFIG_VARIABLE(NumPackets, bool, false);

// Friend-v2 parity batch 2 (appended LAST, order-sensitive parser).
CONFIG_VARIABLE(PulseMode, bool, false);
CONFIG_VARIABLE_RANGE(PulseOn, server_lagger_params::kPulseOn);
CONFIG_VARIABLE_RANGE(PulseOff, server_lagger_params::kPulseOff);
// Mix: rotate the payload vector (all 5 shapes) per tick - no single defense tunes it out.
CONFIG_VARIABLE(MixMode, bool, false);
// Population scale: relay amplification scales with receivers - auto-scale datagrams by lobby
// size (1x at 8 players, up to 3x in full lobbies).
CONFIG_VARIABLE(PopulationScale, bool, false);
// Misattribute: write ANOTHER player's SteamID64 into the voice xuid - the flood is attributed
// to them (their clients see it as their voice). Evil. Your call, it is a toggle.
CONFIG_VARIABLE(Misattribute, bool, false);

}