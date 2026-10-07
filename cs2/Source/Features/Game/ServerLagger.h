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

























namespace server_lagger
{




struct ServerLaggerProfile {
    std::uint32_t messagesPerDatagram;
    int maximumDatagramsPerTick;
    std::size_t packetOffsetsPerMessage;
};






inline constexpr std::size_t kMaxPayloadBytes = 10 + 16320 + 1 + 8 + 1 + 8;
inline constexpr std::size_t kFramedMax = kMaxPayloadBytes + 8;



inline constexpr std::uint32_t kChannelPayloadBitsLimit = 262144;


inline constexpr int kFlushCooldownTicks = 128;



inline std::atomic<std::uint64_t> statsOfferedBytes{0};
inline std::atomic<std::uint64_t> statsRefusedEvents{0};
inline std::atomic<std::uint64_t> statsTxBytes{0}; 




inline std::atomic<long long> lastTickAdvanceMs{0};
inline std::atomic<int> lastSeenTick{-1};



inline std::atomic<int> probeRequest{0};

inline long long monotonicMs() noexcept
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}


inline std::uint64_t xuidEntropy{0};

inline std::uint8_t payloadBytes[kMaxPayloadBytes]{};
inline std::size_t payloadSize = 0;
inline std::uint8_t framedBytes[kFramedMax]{};
















inline void fillVoiceAudio(std::uint32_t mode, std::size_t audioOffset, std::size_t audioBytes) noexcept
{
    switch (mode) {
    case 1: {
        
        
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
        
        for (std::size_t i = 0; i < audioBytes; ++i)
            payloadBytes[audioOffset + i] = static_cast<std::uint8_t>((i + xuidEntropy) & 0x7Fu);
        break;
    case 4: {
        
        
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
        break; 
    }
}








inline void makeVoicePayload(const ServerLaggerProfile& profile, std::uint64_t xuid, std::uint32_t tick, std::uint32_t payloadMode, bool numPackets) noexcept
{
    const std::size_t offs = profile.packetOffsetsPerMessage;

    if (payloadMode == 3) {
        
        const std::size_t noiseLen = offs; 
        const std::size_t innerLen = 2 + 3 + noiseLen; 
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
        prefix[prefixSize++] = 0x38; 
        prefix[prefixSize++] = static_cast<std::uint8_t>((offsetsCount & 0x7F) | 0x80);
        prefix[prefixSize++] = static_cast<std::uint8_t>(offsetsCount >> 7u);
    }
    prefix[prefixSize++] = 0x42;
    prefix[prefixSize++] = static_cast<std::uint8_t>((offs & 0x7F) | 0x80);
    prefix[prefixSize++] = static_cast<std::uint8_t>(offs >> 7u);

    std::memcpy(payloadBytes, prefix, prefixSize);
    std::size_t offset = prefixSize + offs; 
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
                2000 ,
                static_cast<std::size_t>(GET_CONFIG_VAR(server_lagger_vars::AudioKB)) * 1024u};
    }

    void run() noexcept
    {
        
        
        
        sampleServerTick();
        if (probeRequest.exchange(0, std::memory_order_acq_rel) != 0)
            runProbe();

        if (flushCooldownTicks > 0) {
            --flushCooldownTicks;
            return;
        }

        
        
        
        
        
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

        
        
        
        
        const NetworkGameClientPointer clientPointer{};
        if (!clientPointer) {
            resetRuntime();
            VerifyConsole::write(4.0f, "lagger", "idle: no network client (not connected, or the client global drifted)");
            return;
        }
        void* const networkClient = clientPointer.get();

        const auto currentTick = net_messages::networkClientTick(networkClient);
        
        
        
        if (runtime.networkClient == networkClient && runtime.tick >= 0 && currentTick + 100000 < runtime.tick) {
            resetRuntime();
            freezeTicksLeft = 0;
            releasePending = false;
            return;
        }
        if (runtime.networkClient == networkClient && runtime.tick == currentTick)
            return;

        
        
        

        runtime.networkClient = networkClient;
        runtime.tick = currentTick;

        
        
        
        
        
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

        
        
        
        if (autoStopLatched) {
            crash_guard::disarm();
            return;
        }

        
        
        
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

        
        
        
        const long long now = monotonicMs();
        if (!wasLaggerActive) {
            wasLaggerActive = true;
            if (GET_CONFIG_VAR(server_lagger_vars::SlowRamp)) {
                rampLevel = 1;
                rampNextStepMs = now + static_cast<long long>(GET_CONFIG_VAR(server_lagger_vars::RampInterval)) * 1000;
            } else {
                rampLevel = -1; 
            }
        }

        
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

        
        
        unsigned payloadMode = GET_CONFIG_VAR(server_lagger_vars::PayloadMode);
        if (GET_CONFIG_VAR(server_lagger_vars::MixMode))
            payloadMode = static_cast<unsigned>(nextXuid() >> 24) % 5;

        
        
        std::uint64_t xuid = nextXuid();
        if (GET_CONFIG_VAR(server_lagger_vars::Misattribute) && foreignCount > 0)
            xuid = foreignSteamIds[static_cast<unsigned>(currentTick) % static_cast<unsigned>(foreignCount)];

        makeVoicePayload(profile, xuid, static_cast<std::uint32_t>(currentTick), payloadMode,
                         GET_CONFIG_VAR(server_lagger_vars::NumPackets));

        
        void* const prototype = makeVoiceMessage();
        if (!prototype) {
            crash_guard::disarm();
            VerifyConsole::write(4.0f, "lagger", "parse failed: %s", net_messages::lastMakeMessageFailure ? net_messages::lastMakeMessageFailure : "unknown");
            return;
        }

        const unsigned msgsPerDatagram = GET_CONFIG_VAR(server_lagger_vars::DatagramMode) ? 1u
            : profile.messagesPerDatagram; 
                                           

        unsigned batches = 0;
        unsigned messagesQueued = 0;
        unsigned refusedThisTick = 0;
        for (int datagram = 0; datagram < datagrams; ++datagram) {
            
            
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
                    ++refusedThisTick; 
                }
                net_messages::destroyMessage(clone);
            }
            
            
            if (batch == 0 && queueOnly)
                continue; 
            if (!queueOnly || releaseNow) {
                
                
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

    
    
    static constexpr int kAutoStopStrikes = 2;

    
    
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

    
    
    void runProbe() noexcept
    {
        
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
                
                
                
                overflowFlag = *reinterpret_cast<const volatile std::uint8_t*>(static_cast<const std::uint8_t*>(channel) + 0x70DA);
            }
        }
        const char* const binding = resolved ? "ok" : (resolutionFailed ? "FAIL(latched)" : "unresolved");
        
        
        
        
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

        if (!net_messages::resolveMessageBinding(22 )) {
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

} 