#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <CS2/Constants/DllNames.h>
#include <Features/Game/ServerLaggerConfigVariables.h>
#include <GameClient/Entities/PlayerController.h>
#include <GameClient/NetworkGameClientPointer.h>
#include <GameClient/NetMessageFactory.h>
#include <GameClient/Bind.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Random.h>
#include <Utils/VerifyConsole.h>

#include <atomic>

// SERVER LAGGER - friend-source port (the "server_lagger_profile_t" snippet), every Windows vcall
// re-derived and verified against the Linux binaries on 2026-09-12. The Linux slot map differs
// from the friend's Windows numbers in exactly three places, all measured against the live
// process and the on-disk modules:
//   - message clone = vtable slot 5 (the Itanium copy ctor; the friend's Windows slot 4 is a
//     get-binding getter here)
//   - message destroy = vtable slot 1 (Itanium D2 deleting dtor, frees the 0x60 block; slot 0 is
//     the complete-object dtor D1 and would LEAK)
//   - channel SendNetMessage = vtable slot 40 (the only CNetChan slot that makes a virtual call
//     through the message object)
// Everything else matched the friend's numbers 1:1 and was verified by disassembly + live-object
// reads (manager registry walk, binding ctor, CNetChan transmit/ready).
//
// Mechanism: build a genuine CCLCMsg_VoiceData (message type 22) through the game's OWN net
// message factory (NetworkMessagesVersion001 -> record 22 -> binding -> new message), parse a
// hand-built protobuf body into it via ReadFromBuffer (audio wrapper {format=2, empty sub-field,
// N zero audio bytes}, fresh random xuid, current tick), then clone it messagesPerDatagram times
// per datagram and Transmit up to `amount` datagrams per tick through the game's net channel.
// Every datagram is engine-framed and encrypted like real voice; the server decompresses +
// validates each message and relays it to every other player.
//
// All state is namespace statics (hookContext.make<> builds temporaries per call). Any resolution
// failure latches the feature off for the session with a [lagger] console line - fail closed, no
// wild vtable calls ever.
namespace server_lagger
{

// The live profile: user-tuned instead of the friend's two fixed presets. The audio payload
// bytes per message = AudioKB x 1024, capped below the 0x60-byte message object's protobuf
// limits; the datagram ceiling is the friend's Mode-Two maximum (119).
struct ServerLaggerProfile {
    std::uint32_t messagesPerDatagram;
    int maximumDatagramsPerTick;
    std::size_t packetOffsetsPerMessage;
};

// bit_read_t + the verified vtable slot map + the whole create/parse/send/commit/transmit
// machinery live in GameClient/NetMessageFactory.h (shared with the UserInfoFlood direct mode,
// which sends CNETMsg_SetConVar through the same channel pipeline).

// Payload upper bound: 10-byte prefix + 16320 audio bytes + 0x11 + xuid(8) + 0x18 + varint(tick).
inline constexpr std::size_t kMaxPayloadBytes = 10 + 16320 + 1 + 8 + 1 + 8;
inline constexpr std::size_t kFramedMax = kMaxPayloadBytes + 8;
// Serialize-size sanity ceiling lives in net_messages (kMaxSaneSerializeBytes) - shared with the
// userinfo direct mode. The channel's unreliable-flush limit and the back-off below are lagger
// specific: the lagger is the only feature pushing 784K-bit flushes.
inline constexpr std::uint32_t kChannelPayloadBitsLimit = 262144;
// After a refused flush the queued data is not dropped - stop flooding briefly so the buffer
// can drain instead of accumulating forever.
inline constexpr int kFlushCooldownTicks = 128;

// Live flood stats for the menu's Lag-O-Meter row (game thread writes, present thread reads -
// relaxed atomics, display only; the menu computes per-second rates from its own snapshots).
inline std::atomic<std::uint64_t> statsOfferedBytes{0};
inline std::atomic<std::uint64_t> statsRefusedEvents{0};
inline std::atomic<std::uint64_t> statsTxBytes{0}; // ACTUALLY transmitted (transmit's return)

// "Time since last tick" (Lag-O-Meter row): the game thread samples the connection's tick
// counter every CreateMove; the meter shows how long it has been stalled. A server choking on
// the flood stops sending snapshots - the gap grows.
inline std::atomic<long long> lastTickAdvanceMs{0};
inline std::atomic<int> lastSeenTick{-1};

// Set by the menu's "Probe NetChan" button (present thread), serviced + printed on the game
// thread inside run() - works while the lagger is disabled (chat pendingApply pattern).
inline std::atomic<int> probeRequest{0};

inline long long monotonicMs() noexcept
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

// Streamed entropy for the payload content modes (advances once per payload build).
inline std::uint64_t xuidEntropy{0};

inline std::uint8_t payloadBytes[kMaxPayloadBytes]{};
inline std::size_t payloadSize = 0;
inline std::uint8_t framedBytes[kFramedMax]{};

// Fills the audio region per PayloadMode (region = the bytes covered by the prefix's length
// field; the protobuf framing around it is untouched). NOTE: the engine's VoiceData parse
// VALIDATES the audio content (live-verified 2026-09-13: an all-0xFF payload fails the factory
// parse with "parse failed" while the framing bytes are provably correct - the client-side (and
// by extension the server-side) voice decoder rejects an all-0xFF opus packet). Zeros compress
// to ~nothing at the relay = pure parse/decode-call storm; random/counter bytes are
// incompressible = every relayed voice byte costs the server's uplink for real (and whatever
// survives the opus decoder plays as white noise to the listeners).
// Payload-region fillers. CRITICAL CONSTRAINT (live-probed 2026-09-14): the offsets region is
// protobuf field 8 = packed repeated uint32 - the server parses it as a RUN OF VARINTS. Bytes
// >= 0x80 chain into multi-byte varints that overrun the submessage length -> the whole
// CMsgVoiceAudio fails the server's parse and is dropped silently (that is why the original
// full-range Random/Counter modes "were not accepted": ~50%+ of their byte stream malformed
// the packed field). Every filler below therefore keeps each byte a VALID varint (<= 0x7F),
// or uses proper multi-byte encodings, while maximizing entropy within that constraint.
inline void fillVoiceAudio(std::uint32_t mode, std::size_t audioOffset, std::size_t audioBytes) noexcept
{
    switch (mode) {
    case 1: {
        // Rand7: incompressible random, masked to 7 bits so every byte is a valid 1-byte varint
        // (7/8 of full random's entropy, zero parse risk)
        static std::uint64_t state{0};
        for (std::size_t i = 0; i < audioBytes; ++i) {
            if ((i & 7u) == 0) {
                std::uint64_t x = state ^ (xuidEntropy += 0x9E3779B97F4A7C15ull);
                x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
                x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
                state = x ^ (x >> 31);
            }
            payloadBytes[audioOffset + i] = static_cast<std::uint8_t>((state >> ((i & 7u) * 8u)) & 0x7Fu);
        }
        break;
    }
    case 2:
        // Count7: deterministic counter masked to 7 bits (valid varints, incompressible)
        for (std::size_t i = 0; i < audioBytes; ++i)
            payloadBytes[audioOffset + i] = static_cast<std::uint8_t>((i + xuidEntropy) & 0x7Fu);
        break;
    case 4: {
        // Varint Mix: proper 2-byte varints encoding 14-bit values (0..16383) - doubles the
        // per-offset value range over Rand7 while staying protobuf-perfect
        static std::uint64_t state4{0};
        for (std::size_t i = 0; i + 1 < audioBytes; i += 2) {
            if ((i & 14u) == 0) {
                std::uint64_t x = state4 ^ (xuidEntropy += 0x9E3779B97F4A7C15ull);
                x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
                state4 = x ^ (x >> 31);
            }
            const std::uint16_t value = static_cast<std::uint16_t>((state4 >> ((i & 14u) * 4u)) & 0x3FFFu);
            payloadBytes[audioOffset + i] = static_cast<std::uint8_t>((value & 0x7Fu) | 0x80u);
            payloadBytes[audioOffset + i + 1] = static_cast<std::uint8_t>(value >> 7u);
        }
        if (audioBytes & 1u)
            payloadBytes[audioOffset + audioBytes - 1] = 0;
        break;
    }
    case 3: {
        // Static mode: full-range random bytes, but they ride the OPAQUE voice_data field
        // (bytes are always protobuf-valid there) - see makeVoicePayload's Static branch
        static std::uint64_t state3{0};
        for (std::size_t i = 0; i < audioBytes; ++i) {
            if ((i & 7u) == 0) {
                std::uint64_t x = state3 ^ (xuidEntropy += 0x9E3779B97F4A7C15ull);
                x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
                state3 = x ^ (x >> 31);
            }
            payloadBytes[audioOffset + i] = static_cast<std::uint8_t>((state3 >> ((i & 7u) * 8u)) & 0xFFu);
        }
        break;
    }
    case 0:
    default:
        break; // zeros: the buffer is static-zeroed, keep it that way
    }
}

// Builds the protobuf body for the selected mode. Two envelope shapes:
//   modes 0/1/2/4: inner {format=2, empty voice_data, 0x42 <offs varint junk>} - the junk rides
//                  the packed packet_offsets field as valid varints (see fillVoiceAudio).
//   mode 3 (Static): inner {format=2, voice_data=<noiseLen random bytes>} - the junk rides the
//                  OPAQUE bytes field (protobuf-valid by construction), giving the relay real
//                  bandwidth to carry AND the receivers' opus decoders garbage to chew on.
// Then a fresh random xuid (field 2, fixed64) and the current tick (field 3, varint).
inline void makeVoicePayload(const ServerLaggerProfile& profile, std::uint64_t xuid, std::uint32_t tick, std::uint32_t payloadMode, bool numPackets) noexcept
{
    const std::size_t offs = profile.packetOffsetsPerMessage;

    if (payloadMode == 3) {
        // ---- Static envelope: junk in voice_data (opaque bytes field) ----
        const std::size_t noiseLen = offs; // 1024..15360 -> 2-byte varint range
        const std::size_t innerLen = 2 + 3 + noiseLen; // 08 02 | 12 <2-byte varint> | noise
        payloadBytes[0] = 0x0A;
        payloadBytes[1] = static_cast<std::uint8_t>((innerLen & 0x7F) | 0x80);
        payloadBytes[2] = static_cast<std::uint8_t>(innerLen >> 7u);
        payloadBytes[3] = 0x08;
        payloadBytes[4] = 0x02;
        payloadBytes[5] = 0x12;
        payloadBytes[6] = static_cast<std::uint8_t>((noiseLen & 0x7F) | 0x80);
        payloadBytes[7] = static_cast<std::uint8_t>(noiseLen >> 7u);
        fillVoiceAudio(3, 8, noiseLen);
        std::size_t offset = 8 + noiseLen;
        payloadBytes[offset++] = 0x11;
        for (std::size_t byte = 0; byte < sizeof(xuid); ++byte)
            payloadBytes[offset++] = static_cast<std::uint8_t>(xuid >> (byte * 8u));
        payloadBytes[offset++] = 0x18;
        do {
            std::uint8_t encoded = static_cast<std::uint8_t>(tick & 0x7Fu);
            tick >>= 7u;
            if (tick)
                encoded |= 0x80u;
            payloadBytes[offset++] = encoded;
        } while (tick);
        payloadSize = offset;
        return;
    }

    // ---- offsets-region envelope (modes 0/1/2/4) ----
    // DECODE STORM: num_packets (field 7, tag 0x38) set to the offsets count makes every
    // RECEIVING client iterate that many "packets" per message - decode work moves from
    // server-only to the whole lobby. Offsets count: modes 0/1/2 = 1 byte/offset; mode 4 = 2.
    const std::size_t offsetsCount = payloadMode == 4 ? offs / 2 : offs;
    const std::size_t npOverhead = numPackets ? 1 + (offsetsCount >= 128 ? 1 : 0) + (offsetsCount >= 16384 ? 1 : 0) : 0;
    const std::size_t audioPayloadBytes = 2 + 2 + 3 + npOverhead + offs;
    std::uint8_t prefix[13] = {
        0x0A,
        static_cast<std::uint8_t>((audioPayloadBytes & 0x7F) | 0x80),
        static_cast<std::uint8_t>(audioPayloadBytes >> 7u),
        0x08,
        0x02,
        0x12,
        0x00,
    };
    std::size_t prefixSize = 7;
    if (numPackets) {
        prefix[prefixSize++] = 0x38; // num_packets, field 7 varint
        prefix[prefixSize++] = static_cast<std::uint8_t>((offsetsCount & 0x7F) | 0x80);
        prefix[prefixSize++] = static_cast<std::uint8_t>(offsetsCount >> 7u);
    }
    prefix[prefixSize++] = 0x42;
    prefix[prefixSize++] = static_cast<std::uint8_t>((offs & 0x7F) | 0x80);
    prefix[prefixSize++] = static_cast<std::uint8_t>(offs >> 7u);

    std::memcpy(payloadBytes, prefix, prefixSize);
    std::size_t offset = prefixSize + offs; // audio filled below
    fillVoiceAudio(payloadMode, prefixSize, offs);
    payloadBytes[offset++] = 0x11;
    for (std::size_t byte = 0; byte < sizeof(xuid); ++byte)
        payloadBytes[offset++] = static_cast<std::uint8_t>(xuid >> (byte * 8u));
    payloadBytes[offset++] = 0x18;
    do {
        std::uint8_t encoded = static_cast<std::uint8_t>(tick & 0x7Fu);
        tick >>= 7u;
        if (tick)
            encoded |= 0x80u;
        payloadBytes[offset++] = encoded;
    } while (tick);
    payloadSize = offset;
}

[[nodiscard]] inline std::uint64_t nextXuid() noexcept
{
    static std::uint64_t state{0};
    if (state == 0) {
        const auto seed = static_cast<std::uint32_t>(Random::floating(0.0f, 1.0f) * static_cast<float>(0x7FFFFFFF));
        std::uint64_t z = (static_cast<std::uint64_t>(seed) << 1) | 1ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        state = z ^ (z >> 31);
        if (state == 0)
            state = 0x9E3779B97F4A7C15ull;
    }
    std::uint64_t x = state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

template <typename HookContext>
class ServerLagger {
public:
    explicit ServerLagger(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] ServerLaggerProfile selectedProfile() const noexcept
    {
        return {static_cast<std::uint32_t>(GET_CONFIG_VAR(server_lagger_vars::MsgsPerBatch)),
                2000 /* datagram attempts/tick - beyond ~119 the channel backpressure refuses; the extra attempts just ride the ceiling (friend Mode 3 goes to 2000) */,
                static_cast<std::size_t>(GET_CONFIG_VAR(server_lagger_vars::AudioKB)) * 1024u};
    }

    void run() noexcept
    {
        // Tick-gap sampler + probe service run EVERY CreateMove, independent of the enabled
        // gate and the flush cooldown (the meter's freeze indicator must tick when idle, and
        // the probe button must answer while the lagger is off).
        sampleServerTick();
        if (probeRequest.exchange(0, std::memory_order_acq_rel) != 0)
            runProbe();

        if (flushCooldownTicks > 0) {
            --flushCooldownTicks;
            return;
        }

        // MAP-SWITCH SESSION GATE (the NameAnimator crash class): during transitions the local
        // pawn is destroyed BEFORE the network channel is torn down - the channel object sits
        // gutted-but-not-freed and bursting into it faults inside engine2 (live crash: DM map
        // switch with the lagger on, pc libengine2, fault 0x0). The pawn always exists while
        // connected (alive OR dead in DM), so null = transition = stand down + reset.
        if (!hookContext.localPlayerController().pawn()) {
            resetRuntime();
            transmitStrikes = 0;
            freezeTicksLeft = 0;
            releasePending = false;
            wasLaggerActive = false;
            autoStopLatched = false;
            rampLevel = 1;
            pulseCountdown = 0;
            pulsePhaseOn = true;
            VerifyConsole::write(4.0f, "lagger", "idle: no live session (map transition or not in a match)");
            return;
        }

        if (!GET_CONFIG_VAR(server_lagger_vars::Enabled)) {
            resetRuntime();
            transmitStrikes = 0;
            freezeTicksLeft = 0;
            releasePending = false;
            wasLaggerActive = false;
            autoStopLatched = false;
            rampLevel = 1;
            return;
        }

        // Friend v2's keybind gate: Enabled checkbox + hold-key both required (Off = checkbox
        // alone). Evaluated every tick so releasing the key stops mid-flood instantly.
        if (const auto laggerKey = GET_CONFIG_VAR(server_lagger_vars::LaggerKey);
            laggerKey != Bind::kOff && !Bind::isDown(laggerKey)) {
            transmitStrikes = 0;
            freezeTicksLeft = 0;
            releasePending = false;
            wasLaggerActive = false;
            autoStopLatched = false;
            rampLevel = 1;
            pulseCountdown = 0;
            pulsePhaseOn = true;
            VerifyConsole::write(4.0f, "lagger", "idle: Lagger Key set but not held");
            return;
        }

        // Every stand-down below used to be silent (0 KB/s + 0 refused on the meter with no
        // explanation - the 2026-09-13 "second game sends nothing" report). Each now logs its
        // reason through the shared per-tag throttle (VerifyConsole), so an idle lagger always
        // says why in the console.
        const NetworkGameClientPointer clientPointer{};
        if (!clientPointer) {
            resetRuntime();
            VerifyConsole::write(4.0f, "lagger", "idle: no network client (not connected, or the client global drifted)");
            return;
        }
        void* const networkClient = clientPointer.get();

        const auto currentTick = net_messages::networkClientTick(networkClient);
        // One batch per game tick per client pointer (the friend's runtime guard).
        // massive backwards tick jump = map change (new map starts near tick 0) - the channel
        // is being rebuilt; stand down for a tick instead of bursting into it
        if (runtime.networkClient == networkClient && runtime.tick >= 0 && currentTick + 100000 < runtime.tick) {
            resetRuntime();
            freezeTicksLeft = 0;
            releasePending = false;
            return;
        }
        if (runtime.networkClient == networkClient && runtime.tick == currentTick)
            return;

        // From here down: the send path. Runtime bookkeeping happens even on stand-down ticks
        // below (pulse rest ticks still advance the tick guard), so the per-tick dedup guard
        // above must be the LAST silent gate.

        runtime.networkClient = networkClient;
        runtime.tick = currentTick;

        // The whole send region runs under the crash guard: map transitions gut the network
        // objects in orders that VARY (pawn destroyed before the channel on one path, after it
        // on another - both observed live), and vcalls into the nulled channel internals fault
        // (libengine2+0x58820d, twice). A fault here bounces to a clean stand-down instead of
        // killing the game.
        if (!crash_guard::arm()) {
            resetRuntime();
            transmitStrikes = 0;
            freezeTicksLeft = 0;
            releasePending = false;
            wasLaggerActive = false;
            rampLevel = 1;
            VerifyConsole::write(4.0f, "lagger", "network transition fault guarded - standing down");
            return;
        }

        // one-shot fail-closed chain resolution (manager/record-22/binding)
        if (!ensureResolved()) {
            crash_guard::disarm();
            return;
        }

        void* const channel = net_messages::getClientChannel(networkClient);
        if (!channel) {
            crash_guard::disarm();
            VerifyConsole::write(4.0f, "lagger", "idle: no net channel (not fully connected)");
            return;
        }

        // AutoStop latched stand-down (2 consecutive transmit refusals = the overflow-flag state
        // that ends in the self-kick; riding it down disconnects us). Cleared by the disable /
        // key-release gates above or the session gates - re-toggling re-arms.
        if (autoStopLatched) {
            crash_guard::disarm();
            return;
        }

        // PULSE duty cycle: burst PulseOn server ticks, rest PulseOff. Phase derives from the
        // server tick so a client hitch cannot skip a burst; rest ticks return with the runtime
        // bookkeeping above already done.
        if (GET_CONFIG_VAR(server_lagger_vars::PulseMode)) {
            const unsigned on = GET_CONFIG_VAR(server_lagger_vars::PulseOn);
            const unsigned off = GET_CONFIG_VAR(server_lagger_vars::PulseOff);
            const unsigned period = on + off;
            if (period != 0 && static_cast<unsigned>(currentTick) % period >= on) {
                crash_guard::disarm();
                return;
            }
        }

        const auto profile = selectedProfile();

        // Lobby walk (only when a lobby-aware vector needs it): player count for POPULATION
        // SCALE, foreign SteamIDs for MISATTRIBUTE (the flood rides their identity).
        unsigned lobbyPlayers = 0;
        std::uint64_t foreignSteamIds[16];
        int foreignCount = 0;
        if (GET_CONFIG_VAR(server_lagger_vars::PopulationScale) || GET_CONFIG_VAR(server_lagger_vars::Misattribute)) {
            const auto steamIdOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_steamID");
            if (steamIdOffset.has_value() && *steamIdOffset > 0) {
                hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
                    const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
                    if (!entityTypeInfo.template is<cs2::CCSPlayerController>())
                        return;
                    auto* const controllerEntity = static_cast<cs2::C_BaseEntity*>(entityIdentity.entity);
                    ++lobbyPlayers;
                    if (foreignCount < static_cast<int>(sizeof(foreignSteamIds) / sizeof(foreignSteamIds[0]))
                        && hookContext.template make<PlayerController>(static_cast<cs2::CCSPlayerController*>(entityIdentity.entity)) != hookContext.localPlayerController()) {
                        std::memcpy(&foreignSteamIds[foreignCount++], reinterpret_cast<const std::byte*>(controllerEntity) + *steamIdOffset, sizeof(std::uint64_t));
                    }
                });
            }
        }

        // SLOW RAMP: arm at 1 on the active rising edge, +1 every RampInterval seconds up to the
        // tick's datagram budget (slamming the max instantly self-overflows/kicks - the friend's
        // sweet spot ~12).
        const long long now = monotonicMs();
        if (!wasLaggerActive) {
            wasLaggerActive = true;
            if (GET_CONFIG_VAR(server_lagger_vars::SlowRamp)) {
                rampLevel = 1;
                rampNextStepMs = now + static_cast<long long>(GET_CONFIG_VAR(server_lagger_vars::RampInterval)) * 1000;
            } else {
                rampLevel = -1; // no ramp
            }
        }

        // datagrams this tick: Amount (clamped to the profile ceiling) x population scale.
        int datagrams = clampAmount(profile);
        if (GET_CONFIG_VAR(server_lagger_vars::PopulationScale) && lobbyPlayers > 0) {
            const unsigned scale = lobbyPlayers / 8 < 1 ? 1 : lobbyPlayers / 8 > 3 ? 3 : lobbyPlayers / 8;
            datagrams *= static_cast<int>(scale);
        }
        if (GET_CONFIG_VAR(server_lagger_vars::SlowRamp) && rampLevel >= 0) {
            if (rampLevel < datagrams && now >= rampNextStepMs) {
                ++rampLevel;
                rampNextStepMs = now + static_cast<long long>(GET_CONFIG_VAR(server_lagger_vars::RampInterval)) * 1000;
            }
            if (rampLevel < datagrams)
                datagrams = rampLevel;
        }

        // LOOP FREEZE: queue batches WITHOUT commit/transmit for FreezeTicks ticks, then release
        // the whole queued backlog as one commit+transmit burst (choke-then-snap; the friend's
        // "loop freeze"). The channel's send buffer absorbs ~one tick of the profile per design -
        // refusals during the freeze are the expected partial-batch case.
        bool queueOnly = false;
        bool releaseNow = true;
        if (GET_CONFIG_VAR(server_lagger_vars::LoopFreeze)) {
            if (freezeTicksLeft <= 0)
                freezeTicksLeft = GET_CONFIG_VAR(server_lagger_vars::FreezeTicks);
            queueOnly = true;
            releaseNow = --freezeTicksLeft <= 0;
            if (releaseNow)
                freezeTicksLeft = 0;
        }

        // Payload vector: the configured mode, or MIX = rotate all 5 shapes per tick (no single
        // server-side defense tunes the whole flood out).
        unsigned payloadMode = GET_CONFIG_VAR(server_lagger_vars::PayloadMode);
        if (GET_CONFIG_VAR(server_lagger_vars::MixMode))
            payloadMode = static_cast<unsigned>(nextXuid() >> 24) % 5;

        // Voice xuid: our own random value, or MISATTRIBUTE = a foreign player's SteamID64
        // (rotates per tick; the relay + their clients see the flood as THEIR voice).
        std::uint64_t xuid = nextXuid();
        if (GET_CONFIG_VAR(server_lagger_vars::Misattribute) && foreignCount > 0)
            xuid = foreignSteamIds[static_cast<unsigned>(currentTick) % static_cast<unsigned>(foreignCount)];

        makeVoicePayload(profile, xuid, static_cast<std::uint32_t>(currentTick), payloadMode,
                         GET_CONFIG_VAR(server_lagger_vars::NumPackets));

        // ONE template message per tick, cloned per queue (the friend's prototype/clone flow).
        void* const prototype = makeVoiceMessage();
        if (!prototype) {
            crash_guard::disarm();
            VerifyConsole::write(4.0f, "lagger", "parse failed: %s", net_messages::lastMakeMessageFailure ? net_messages::lastMakeMessageFailure : "unknown");
            return;
        }

        const unsigned msgsPerDatagram = GET_CONFIG_VAR(server_lagger_vars::DatagramMode) ? 1u
            : profile.messagesPerDatagram; // DATAGRAM MODE: many tiny datagrams = per-datagram
                                           // server parse cost instead of relay amplification

        unsigned batches = 0;
        unsigned messagesQueued = 0;
        unsigned refusedThisTick = 0;
        for (int datagram = 0; datagram < datagrams; ++datagram) {
            // CanPacket mid-burst re-check (slot 47): the channel can choke between datagrams;
            // queuing past it costs clone/send/destroy for locally-dropped bytes.
            if (datagram > 0 && !net_messages::channelReady(channel)) {
                ++refusedThisTick;
                break;
            }
            unsigned batch = 0;
            for (unsigned message = 0; message < msgsPerDatagram; ++message) {
                void* const clone = net_messages::cloneMessage(prototype);
                if (!clone) {
                    net_messages::destroyMessage(prototype);
                    crash_guard::disarm();
                    VerifyConsole::write(4.0f, "lagger", "clone failed - standing down");
                    return;
                }
                if (net_messages::sendNetMessage(channel, clone)) {
                    ++batch;
                    ++messagesQueued;
                    statsOfferedBytes.fetch_add(payloadSize, std::memory_order_relaxed);
                } else {
                    ++refusedThisTick; // buffer backpressure - expected at profile ceilings
                }
                net_messages::destroyMessage(clone);
            }
            // PARTIAL-BATCH RULE: commit runs even when the send refused mid-batch - skipping it
            // strands queued bits in the pending cursor ("0 batches, N queued" bug).
            if (batch == 0 && queueOnly)
                continue; // frozen ticks with a fully refused batch: nothing to seal
            if (!queueOnly || releaseNow) {
                // the release tick seals EVERYTHING queued (this tick's + the backlog) - commit
                // once, then drain up to a few datagrams while the transmit keeps accepting.
                net_messages::commitChannel(channel);
                std::int32_t transmitted = 0;
                for (int burst = 0; burst < (queueOnly ? 4 : 1); ++burst) {
                    transmitted = net_messages::transmitChannelEx(channel, "Server Lagger");
                    if (transmitted > 0) {
                        ++batches;
                        statsTxBytes.fetch_add(static_cast<std::uint64_t>(transmitted), std::memory_order_relaxed);
                        transmitStrikes = 0;
                    } else {
                        statsRefusedEvents.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                }
                if (transmitted <= 0) {
                    // refused transmit: the queued data STAYS queued - back off so the buffer
                    // drains instead of accumulating forever.
                    ++transmitStrikes;
                    flushCooldownTicks = kFlushCooldownTicks;
                    if (GET_CONFIG_VAR(server_lagger_vars::AutoStop) && transmitStrikes >= kAutoStopStrikes) {
                        autoStopLatched = true;
                        transmitStrikes = 0;
                        VerifyConsole::write(6.0f, "lagger", "AUTO STOP: %d consecutive transmit refusals (overflow flag) - re-toggle to go again", kAutoStopStrikes);
                    }
                    break;
                }
            }
        }
        net_messages::destroyMessage(prototype);

        if (batches > 0 || refusedThisTick > 0)
            VerifyConsole::write(2.0f, "lagger", "%u batches (x%u msgs), %u voice msgs queued, %u refused",
                                 batches, msgsPerDatagram, messagesQueued, refusedThisTick);
        crash_guard::disarm();
    }

    void onUnload() const noexcept
    {
        resolved = false;
        resolutionFailed = false;
        flushCooldownTicks = 0;
        oversizeLogged = false;
        backpressureLogged = false;
        transmitStrikes = 0;
        freezeTicksLeft = 0;
        releasePending = false;
        wasLaggerActive = false;
        autoStopLatched = false;
        rampLevel = 1;
        pulseCountdown = 0;
        pulsePhaseOn = true;
        resetRuntime();
    }

private:
    struct Runtime {
        void* networkClient = nullptr;
        int tick = -1;
    };

    // Auto stop: consecutive datagram-transmit refusals before standing down (the overflow-flag
    // state that precedes the NETWORK_DISCONNECT_OVERFLOW self-kick).
    static constexpr int kAutoStopStrikes = 2;

    // Every CreateMove: sample the connection tick for the meter's freeze indicator. Two benign
    // racing stores - display only.
    void sampleServerTick() noexcept
    {
        const NetworkGameClientPointer clientPointer{};
        if (!clientPointer) {
            lastSeenTick.store(-1, std::memory_order_relaxed);
            return;
        }
        const int tick = net_messages::networkClientTick(clientPointer.get());
        if (tick != lastSeenTick.load(std::memory_order_relaxed)) {
            lastSeenTick.store(tick, std::memory_order_relaxed);
            lastTickAdvanceMs.store(monotonicMs(), std::memory_order_relaxed);
        }
    }

    // "Probe NetChan" button: dump the whole resolution chain + channel state in one line,
    // available while the lagger is disabled. Reads only - no wild vtable calls.
    void runProbe() noexcept
    {
        // the probe walks the same risky objects (GetChannel vcall + channel field reads)
        if (!crash_guard::arm()) {
            VerifyConsole::write(4.0f, "lagger", "probe: network transition fault guarded");
            return;
        }
        const NetworkMessagesPointer messagesPointer{};
        const NetworkGameClientPointer clientPointer{};
        void* channel = nullptr;
        bool ready = false;
        unsigned overflowFlag = 0;
        if (clientPointer && clientPointer.valid()) {
            channel = net_messages::getClientChannel(clientPointer.get());
            if (channel) {
                ready = net_messages::channelReady(channel);
                // CNetChan+0x8CE1 = the overflow flag slot 42 (transmit) gates on - the state
                // whose set-state ends in the "Overflow error" disconnect (verified by the
                // slot-48/42 disassembly + the reproduced overflow kick). The member moved
                // +0x27/+0x28 in the CNetChan struct with the 2026-10-06 update (was 0x8CBA
                // on dce58989, 0x70DA on build 14181).
                overflowFlag = *reinterpret_cast<const volatile std::uint8_t*>(static_cast<const std::uint8_t*>(channel) + 0x8CE1);
            }
        }
        const char* const binding = resolved ? "ok" : (resolutionFailed ? "FAIL(latched)" : "unresolved");
        // "unresolved" = the one-shot resolution chain has not RUN yet - it only runs once the
        // whole send path engages (enabled + in a live session + key held), so a probe pressed
        // before that says nothing about record-22 health. pawn/key state below names the gate
        // that is holding the send path back.
        const bool pawnPresent = hookContext.localPlayerController().pawn() != nullptr;
        const auto laggerKey = GET_CONFIG_VAR(server_lagger_vars::LaggerKey);
        VerifyConsole::write(0.0f, "lagger",
                             "probe: mgr=%s rec22=%s client=%s chan=%p ready=%d pawn=%d key=%s held=%d overflow=%u transmitStrikes=%d freeze=%d enabled=%d",
                             messagesPointer ? "ok" : "FAIL",
                             binding,
                             (clientPointer && clientPointer.valid()) ? "ok" : "FAIL",
                             channel, ready ? 1 : 0, pawnPresent ? 1 : 0,
                             laggerKey == Bind::kOff ? "off" : Bind::displayName(laggerKey),
                             (laggerKey != Bind::kOff && Bind::isDown(laggerKey)) ? 1 : 0,
                             overflowFlag, transmitStrikes, freezeTicksLeft,
                             GET_CONFIG_VAR(server_lagger_vars::Enabled) ? 1 : 0);
        // DISARM: an armed-but-returned bounce point is a dangling sigjmp target - a later
        // unrelated fault on this thread would longjmp into the returned frame (UB).
        crash_guard::disarm();
    }

    void resetRuntime() const noexcept
    {
        runtime = Runtime{};
    }

    [[nodiscard]] int clampAmount(const ServerLaggerProfile& profile) const noexcept
    {
        const auto configured = static_cast<int>(GET_CONFIG_VAR(server_lagger_vars::Amount));
        const int max = profile.maximumDatagramsPerTick;
        int value = configured < 1 ? 1 : configured;
        if (value > max)
            value = max;
        return value;
    }

    // One-shot chain resolution with module validation on every object; any failure latches the
    // feature off for the session (fail closed - a wrong vtable slot is a crash, never a no-op).
    [[nodiscard]] bool ensureResolved() noexcept
    {
        if (resolutionFailed)
            return false;
        if (resolved)
            return true;

        const NetworkMessagesPointer messagesPointer{};
        if (!messagesPointer) {
            VerifyConsole::write(0.0f, "lagger", "NetworkMessagesVersion001 unavailable - fail closed");
            resolutionFailed = true;
            return false;
        }

        const NetworkGameClientPointer clientPointer{};
        if (!clientPointer || !clientPointer.valid()) {
            VerifyConsole::write(0.0f, "lagger", "network game client invalid - fail closed");
            resolutionFailed = true;
            return false;
        }

        if (!net_messages::resolveMessageBinding(22 /* CCLCMsg_VoiceData */)) {
            VerifyConsole::write(0.0f, "lagger", "voice message record not registered - fail closed");
            resolutionFailed = true;
            return false;
        }

        resolved = true;
        VerifyConsole::write(6.0f, "lagger", "engaged: %u msgs/datagram x up to %d datagrams/tick, %zu-byte audio",
                             selectedProfile().messagesPerDatagram, selectedProfile().maximumDatagramsPerTick,
                             selectedProfile().packetOffsetsPerMessage);
        return true;
    }

    [[nodiscard]] void* makeVoiceMessage() noexcept
    {
        return net_messages::makeMessage(22, payloadBytes, payloadSize, "Server Lagger", framedBytes, sizeof(framedBytes));
    }

    inline static Runtime runtime{};
    inline static bool resolved = false;
    inline static bool resolutionFailed = false;
    inline static int flushCooldownTicks = 0;
    inline static bool oversizeLogged = false;
    inline static bool backpressureLogged = false;
    inline static int transmitStrikes = 0;
    inline static int freezeTicksLeft = 0;
    inline static bool releasePending = false;
    inline static bool wasLaggerActive = false;
    inline static bool autoStopLatched = false;
    inline static int rampLevel = 1;
    inline static long long rampNextStepMs = 0;
    inline static int pulseCountdown = 0;
    inline static bool pulsePhaseOn = true;

    HookContext& hookContext;
};

} // namespace server_lagger