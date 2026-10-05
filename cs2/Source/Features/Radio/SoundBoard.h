#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>

#include <CS2/Classes/IGameEventManager2.h>
#include <CS2/Constants/DllNames.h>
#include <CS2/Constants/TeamNumberConstants.h>
#include <Features/Radio/RadioConfigVariables.h>
#include <Features/Radio/SoundBoardConfigVariables.h>
#include <GameClient/Bind.h>
#include <Platform/Linux/LinuxDynamicLibrary.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <GameClient/NetworkGameClientPointer.h>
#include <GameClient/NetMessageFactory.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/NsPaths.h>
#include <Utils/VerifyConsole.h>

// SOUND BOARD - in-process voice injection (replaces the host-script airhorn pipeline; the
// ns_mic_board.sh pactl/ffmpeg/fifo pipeline is GONE - see project_soundboard_voice_injection).
//
// Design: a wav clip is decoded to 48kHz s16le mono PCM, encoded to opus with the GAME'S OWN
// libopus (dlsym'd from libsoundsystem.so - exported), and sent as paced CCLCMsg_VoiceData
// messages through the game's net channel via GameClient/NetMessageFactory.h - the exact
// pipeline the Server Lagger uses for its voice flood, at normal voice cadence (~50 20ms
// chunks/s). The server relays the frames to every listener like real voice; the voice gate
// (+voicerecord) is irrelevant - injected messages transmit on their own.
//
// What this deletes vs the old pipeline: host spawns (present-thread freeze hiccups), the
// PulseWire virtual-source + capture device switching (the _dl_close_worker crash class), the
// fifo EOF spin hazard, the silence keeper, the scripts. Zero host traffic while idle.
//
// Thread rules: run() = CreateMove (game thread) - pacing, encoding, sending, keybind, wav load,
// clip scan. onGameEvent = game thread (event triggers). The menu reads the clip-list statics.
//
// Opus frames: 20ms = 960 samples. Pacing: CreateMove ticks at 64/s add 15.625ms each; a chunk
// leaves when the accumulator reaches 20ms -> exactly realtime (1.28 ticks/chunk).
namespace soundboard
{

// 30s of 48kHz s16le mono PCM.
inline constexpr std::size_t kMaxClipSamples = 48000 * 30;
inline constexpr std::size_t kMaxClips = 64;
inline constexpr int kSamplesPerFrame = 960; // 20ms @ 48kHz
inline constexpr std::size_t kMaxClipName = 96;
inline constexpr std::size_t kMaxPayload = 1024;
inline constexpr std::size_t kFramedMax = kMaxPayload + 12;
inline std::uint8_t framed[kFramedMax]{};

// --- decoded clip + playback state (namespace statics: hookContext.make builds temporaries) --
inline std::int16_t clipPcm[kMaxClipSamples]{};
inline std::size_t clipSamples = 0;      // decoded length in samples
inline bool playing = false;
inline std::size_t playPos = 0;          // sample offset of the next chunk
inline float pacingAcc = 0.0f;           // ms accumulated vs 20ms frames
inline char currentClip[kMaxClipName]{};

inline pthread_mutex_t namesMutex = PTHREAD_MUTEX_INITIALIZER;
inline char clipNames[kMaxClips][kMaxClipName]{};
inline const char* clipNamePtrs[kMaxClips]{};
inline int clipCount = 0;
inline float clipScanAcc = 0.0f;         // seconds since the last scan
inline bool firstBloodUsed = false;      // per-round first-blood trigger latch

// opus (dlsym'd once per session; null = unavailable -> fail closed)
inline void* opusEncoder = nullptr;
inline bool opusFailed = false;

using EncoderCreateFn = void* (*)(std::int32_t, std::int32_t, std::int32_t, std::int32_t*);
using EncodeFn = std::int32_t (*)(void*, const std::int16_t*, std::int32_t, std::uint8_t*, std::int32_t);
using CtlFn = std::int32_t (*)(void*, std::int32_t, ...);

inline constexpr int kOpusApplicationVoip = 2048;
inline constexpr int kOpusSetBitrate = 4002;

inline EncodeFn opusEncode = nullptr;

// Debug dumps: the pre-encode PCM and the post-encode opus frames of every clip, so the actual
// soundboard output can be played/decoded OFFLINE (paplay the raw; the frames decode with libopus).
// Files: /tmp/ns_board_pcm_debug.raw (s16le 48k mono), /tmp/ns_board_opus_debug.frames
// ([u16 len][frame] framing). Each chunk re-opens/appends/closes: the game's own fd management
// closes arbitrary fd NUMBERS (one landed on our persistent dump fd mid-clip once - the dump
// stayed 0 bytes with mode 000 from an omitted open() mode argument), so nothing long-lived.
// Appends to a diagnostics file inside the exchange root (NsPaths.h); `name` is relative.
// Reopened per call on purpose (comment below) - and callers pass plain file NAMES, so the
// debug dumps follow the exchange root everywhere.
inline void debugAppend(const char* name, const void* data, std::size_t length) noexcept
{
    char path[ns_paths::kMaxPath];
    if (!ns_paths::join(path, sizeof(path), name))
        return;
    const int fd = ::open(path, 0x401 /* O_WRONLY|O_APPEND */ | 0100 /* O_CREAT */, 0644);
    if (fd < 0)
        return;
    static_cast<void>(::write(fd, data, length));
    ::close(fd);
}

[[nodiscard]] inline std::int32_t netTick() noexcept
{
    const NetworkGameClientPointer clientPointer{};
    return clientPointer ? net_messages::networkClientTick(clientPointer.get()) : 0;
}

// Builds the CMsgVoiceAudio/CCLCMsg_VoiceData envelope around one opus frame.
// THE SCHEMA (decoded from the embedded netmessages.proto descriptor in libengine2 -
// the friend-source payload had the audio in field 8 = packet_offsets and an EMPTY
// voice_data, which is why it made lag but never played audio):
//   CMsgVoiceAudio { optional enum format = 1; optional bytes voice_data = 2;
//                   optional int32 sample_rate = 5; repeated int32 packet_offsets = 8; }
//   CCLCMsg_VoiceData { optional CMsgVoiceAudio audio = 1; optional fixed64 xuid = 2; optional int32 tick = 3; }
[[nodiscard]] inline std::size_t makeVoiceEnvelope(const std::uint8_t* frame, std::size_t frameLength,
                                                   std::uint32_t tick, std::uint64_t xuid, std::uint32_t voiceFormat,
                                                   std::uint8_t* out, std::size_t outCap) noexcept
{
    // CMsgVoiceAudio: format=2 (08 02) + voice_data=<opus> (12 <varint len>) + sample_rate=48000 (28 <varint>)
    std::uint8_t audio[512];
    std::size_t audioLength = 0;
    audio[audioLength++] = 0x08;
    net_messages::appendVarint(audio, audioLength, voiceFormat);
    audio[audioLength++] = 0x12;
    net_messages::appendVarint(audio, audioLength, static_cast<std::uint32_t>(frameLength));
    std::memcpy(audio + audioLength, frame, frameLength);
    audioLength += frameLength;
    audio[audioLength++] = 0x28; // sample_rate = 48000
    net_messages::appendVarint(audio, audioLength, 48000);
    audio[audioLength++] = 0x38; // num_packets = 1
    audio[audioLength++] = 0x01;
    audio[audioLength++] = 0x40; // packet_offsets = [0]
    audio[audioLength++] = 0x00;

    std::size_t offset = 0;
    out[offset++] = 0x0A; // audio = 1, length-delimited
    net_messages::appendVarint(out, offset, static_cast<std::uint32_t>(audioLength));
    std::memcpy(out + offset, audio, audioLength);
    offset += audioLength;

    out[offset++] = 0x11; // xuid, fixed64: the LOCAL player's SteamID (the server may filter
    // voice by sender identity - a zero xuid is the prime "nobody hears it" suspect)
    for (int byte = 0; byte < 8; ++byte)
        out[offset++] = static_cast<std::uint8_t>(xuid >> (byte * 8u));
    out[offset++] = 0x18; // tick
    net_messages::appendVarint(out, offset, tick);
    return offset <= outCap ? offset : 0;
}

// Loads a wav (16-bit PCM RIFF) and converts to 48kHz s16le mono into clipPcm. Stereo mixes
// down; other sample rates get nearest-frame resampling. Returns false on any parse failure -
// fail closed, never send garbage.
[[nodiscard]] inline bool loadWav(const char* path) noexcept
{
    const int fd = ::open(path, 0 /* O_RDONLY */);
    if (fd < 0)
        return false;

    std::uint8_t header[12];
    if (::read(fd, header, sizeof(header)) != sizeof(header) || std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) {
        ::close(fd);
        return false;
    }

    std::uint16_t channels = 1;
    std::uint32_t sampleRate = 48000;
    bool haveData = false, haveFmt = false;

    std::uint8_t chunk[8];
    while (::read(fd, chunk, sizeof(chunk)) == sizeof(chunk)) {
        const std::uint32_t chunkSize = static_cast<std::uint32_t>(chunk[4]) | (static_cast<std::uint32_t>(chunk[5]) << 8u)
            | (static_cast<std::uint32_t>(chunk[6]) << 16u) | (static_cast<std::uint32_t>(chunk[7]) << 16u << 8u);
        const off_t skip = static_cast<off_t>(chunkSize) + (chunkSize & 1);

        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            std::uint8_t fmt[16];
            if (::read(fd, fmt, sizeof(fmt)) != sizeof(fmt)) { ::close(fd); return false; }
            const std::uint16_t format = static_cast<std::uint16_t>(fmt[0]) | (static_cast<std::uint16_t>(fmt[1]) << 8u);
            channels = static_cast<std::uint16_t>(fmt[2]) | (static_cast<std::uint16_t>(fmt[3]) << 8u);
            sampleRate = static_cast<std::uint32_t>(fmt[4]) | (static_cast<std::uint32_t>(fmt[5]) << 8u)
                | (static_cast<std::uint32_t>(fmt[6]) << 16u) | (static_cast<std::uint32_t>(fmt[7]) << 16u << 8u);
            const std::uint16_t bits = static_cast<std::uint16_t>(fmt[14]) | (static_cast<std::uint16_t>(fmt[15]) << 8u);
            if (format != 1 || bits != 16 || channels < 1 || channels > 2 || sampleRate < 8000) { ::close(fd); return false; }
            haveFmt = true;
            if (skip > static_cast<off_t>(16) && ::lseek(fd, skip - 16, SEEK_CUR) < 0) { ::close(fd); return false; }
            continue;
        }

        if (std::memcmp(chunk, "data", 4) == 0 && haveFmt) {
            const std::size_t frameBytes = static_cast<std::size_t>(channels) * 2u;
            if (frameBytes == 0) { ::close(fd); return false; }

            // read the data chunk into a scratch buffer (capped at 30s @ 48k stereo)
            static std::uint8_t raw[48000 * 30 * 4];
            const std::size_t toRead = chunkSize < sizeof(raw) ? chunkSize : sizeof(raw);
            if (toRead < frameBytes || ::read(fd, raw, static_cast<ssize_t>(toRead)) != static_cast<ssize_t>(toRead)) { ::close(fd); return false; }
            const std::size_t sourceFrames = toRead / frameBytes;

            // nearest-frame resample to 48kHz + stereo -> mono, into clipPcm
            std::size_t out = 0;
            for (std::size_t outIndex = 0; out < kMaxClipSamples; ++outIndex) {
                const std::size_t sourceIndex = static_cast<std::size_t>(static_cast<double>(outIndex) * static_cast<double>(sampleRate) / 48000.0);
                if (sourceIndex >= sourceFrames)
                    break;
                const std::uint8_t* frame = raw + sourceIndex * frameBytes;
                const std::int16_t left = static_cast<std::int16_t>(static_cast<std::uint16_t>(frame[0]) | (static_cast<std::uint16_t>(frame[1]) << 8u));
                if (channels == 2) {
                    const std::int16_t right = static_cast<std::int16_t>(static_cast<std::uint16_t>(frame[2]) | (static_cast<std::uint16_t>(frame[3]) << 8u));
                    clipPcm[out++] = static_cast<std::int16_t>((static_cast<std::int32_t>(left) + static_cast<std::int32_t>(right)) >> 1);
                } else {
                    clipPcm[out++] = left;
                }
            }
            clipSamples = out;
            ::close(fd);
            return out > 0;
        }

        if (::lseek(fd, static_cast<off_t>(chunkSize) + (chunkSize & 1), SEEK_CUR) < 0) { ::close(fd); return false; }
    }
    ::close(fd);
    return false;
}

// Ensures the opus encoder exists (dlsym'd from the game's own libsoundsystem). Fail closed:
// any unavailable symbol or ctl failure latches opusFailed for the session.
inline void ensureOpus() noexcept
{
    if (opusEncoder || opusFailed)
        return;
    const LinuxDynamicLibrary soundSystem{cs2::SOUNDSYSTEM_DLL};
    const auto create = soundSystem.getFunctionAddress("opus_encoder_create").as<EncoderCreateFn>();
    const auto ctl = soundSystem.getFunctionAddress("opus_encoder_ctl").as<CtlFn>();
    opusEncode = soundSystem.getFunctionAddress("opus_encode").as<EncodeFn>();
    if (!create || !ctl || !opusEncode) {
        opusFailed = true;
        gui_log::write("soundboard: opus API unavailable in libsoundsystem - soundboard disabled");
        return;
    }
    std::int32_t error = 0;
    void* encoder = create(48000, 1, kOpusApplicationVoip, &error);
    if (!encoder || error != 0) {
        opusFailed = true;
        gui_log::write("radio: opus_encoder_create failed (%d) - soundboard disabled", error);
        return;
    }
    ctl(encoder, kOpusSetBitrate, 64000); // normal voice bitrate; server relays frames as-is
    opusEncoder = encoder;
}

// Scans <configDir>/sounds/*.wav into the clip list (game thread, 1s throttle - readdir is
// cheap; the menu reads the resulting statics on the present thread).
template <typename HookContext>
inline void scanClips(HookContext& hookContext) noexcept
{
    const auto& directoryPath = hookContext.configState().pathToConfigDirectory;
    if (!directoryPath)
        return;
    const char* dir = reinterpret_cast<const char*>(directoryPath.get());

    char soundsDir[kMaxClipName + 32];
    std::snprintf(soundsDir, sizeof(soundsDir), "%s/sounds", dir);

    DIR* d = ::opendir(soundsDir);
    if (!d)
        return;

    char fresh[kMaxClips][kMaxClipName]{};
    int freshCount = 0;
    while (const dirent* entry = ::readdir(d)) {
        if (freshCount >= kMaxClips)
            break;
        const std::size_t len = std::strlen(entry->d_name);
        if (len < 5 || len >= kMaxClipName)
            continue;
        if (std::memcmp(entry->d_name + len - 4, ".wav", 4) != 0)
            continue;
        std::memcpy(fresh[freshCount], entry->d_name, len + 1);
        ++freshCount;
    }
    ::closedir(d);

    pthread_mutex_lock(&namesMutex);
    for (int i = 0; i < freshCount; ++i) {
        std::memcpy(clipNames[i], fresh[i], kMaxClipName);
        clipNamePtrs[i] = clipNames[i];
    }
    clipCount = freshCount;
    pthread_mutex_unlock(&namesMutex);
}

} // namespace soundboard

// The feature class itself (hookContext.make<soundboard::SoundBoard>()).
namespace soundboard
{

template <typename HookContext>
class SoundBoard {
public:
    explicit SoundBoard(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // Game thread, CreateMove. Keybind edge detection, clip scan, pacing, sending.
    void run() noexcept
    {
        if (!GET_CONFIG_VAR(radio_vars::AirhornEnabled)) {
            playing = false;
            playPos = 0;
            clipScanAcc = 0.0f;
            return;
        }
        ensureOpus();
        if (opusFailed)
            return;

        clipScanAcc += 15.625f; // ms per 64tps tick
        if (clipScanAcc >= 1000.0f) {
            clipScanAcc = 0.0f;
            scanClips(hookContext);
        }

        // Sound Key: edge-triggered, plays the selected clip
        const int key = GET_CONFIG_VAR(soundboard_vars::SoundKeyBind);
        const bool down = Bind::isDown(key);
        if (down && !keyWasDown && key != Bind::kOff)
            startPlayback();
        keyWasDown = down;

        if (!playing)
            return;

        // pacing: 15.625ms per 64tps tick, chunk per 20ms
        pacingAcc += 15.625f;
        if (pacingAcc < 20.0f)
            return;
        pacingAcc -= 20.0f;

        sendChunk();
    }

    // Game-event triggers - same rules the old airhorn honored (radio_vars toggles).
    void onGameEvent(cs2::IGameEvent* event) noexcept
    {
        if (!event || !GET_CONFIG_VAR(radio_vars::AirhornEnabled))
            return;

        if (game_events::is(event, "round_end")) {
            firstBloodUsed = false; // a new round means a new first blood
            const int winner = game_events::intForKey(event, "winner", -1);
            if (!GET_CONFIG_VAR(radio_vars::AirhornRoundWin))
                return;
            const auto localTeam = hookContext.localPlayerController().pawn().template as<PlayerPawn>().teamNumber();
            if ((winner == cs2::TEAM_TERRORIST && localTeam == TeamNumber::TT)
                || (winner == cs2::TEAM_CT && localTeam == TeamNumber::CT))
                startPlayback();
            return;
        }

        if (!game_events::is(event, "player_death"))
            return;
        if (!game_events::localPlayerIsAttacker(hookContext, event))
            return;
        if (playing)
            return;
        if (GET_CONFIG_VAR(radio_vars::AirhornFirstBlood) && !firstBloodUsed) {
            firstBloodUsed = true;
            startPlayback();
            return;
        }
        if (GET_CONFIG_VAR(radio_vars::AirhornHeadshot) && game_events::intForKey(event, "headshot", 0) != 0)
            startPlayback();
    }

    void onUnload() const noexcept
    {
        playing = false;
    }

private:
    // Resolves the selected clip from the scanned list and arms playback. Called on the game
    // thread only.
    void startPlayback() noexcept
    {
        if (opusFailed)
            return;
        resolveLocalXuid();

        char name[kMaxClipName]{};
        pthread_mutex_lock(&namesMutex);
        const int index = GET_CONFIG_VAR(soundboard_vars::ClipIndex);
        if (clipCount == 0 || index < 0 || index >= clipCount) {
            pthread_mutex_unlock(&namesMutex);
            return;
        }
        std::memcpy(name, clipNames[index], sizeof(name));
        pthread_mutex_unlock(&namesMutex);

        const auto& directoryPath = hookContext.configState().pathToConfigDirectory;
        if (!directoryPath)
            return;
        const char* dir = reinterpret_cast<const char*>(directoryPath.get());
        char path[kMaxClipName + 32];
        std::snprintf(path, sizeof(path), "%s/sounds/%s", dir, name);

        if (!loadWav(path)) {
            VerifyConsole::write(4.0f, "board", "clip %s failed to load (16-bit PCM wav only)", name);
            return;
        }
        if (!opusEncoder) {
            // ensureOpus ran on the last run(); a failed create latches opusFailed - silent here
            return;
        }
        playing = true;
        playPos = 0;
        pacingAcc = 20.0f; // send the first chunk on the next tick
        chunksSent = 0;
    }

    // Sends ONE 20ms chunk of the clip as a paced CCLCMsg_VoiceData.
    void sendChunk() noexcept
    {
        const NetworkGameClientPointer clientPointer{};
        if (!clientPointer || !clientPointer.valid()) {
            playing = false;
            return;
        }
        void* const channel = net_messages::getClientChannel(clientPointer.get());
        if (!channel || !net_messages::channelReady(channel))
            return; // not connected: hold the position, resume when the channel returns

        if (!opusEncoder) {
            playing = false;
            return;
        }

        std::int16_t frame[kSamplesPerFrame]{};
        const std::size_t remaining = clipSamples > playPos ? clipSamples - playPos : 0;
        const std::size_t take = remaining < static_cast<std::size_t>(kSamplesPerFrame) ? remaining : static_cast<std::size_t>(kSamplesPerFrame);
        if (take == 0) {
            playing = false;
            VerifyConsole::write(2.0f, "board", "clip finished (%u chunks sent)", chunksSent);
            return;
        }
        std::memcpy(frame, clipPcm + playPos, take * sizeof(std::int16_t)); // tail stays zero-padded
        playPos += take;
        debugAppend("ns_board_pcm_debug.raw", frame, take * sizeof(std::int16_t));

        std::uint8_t opusFrame[400];
        const std::int32_t frameLength = opusEncode(opusEncoder, frame, kSamplesPerFrame, opusFrame, sizeof(opusFrame));
        if (frameLength <= 0) {
            playing = false;
            return;
        }
        const std::uint16_t frameSize = static_cast<std::uint16_t>(frameLength);
        debugAppend("ns_board_opus_debug.frames", &frameSize, sizeof(frameSize));
        debugAppend("ns_board_opus_debug.frames", opusFrame, static_cast<std::size_t>(frameLength));

        std::uint8_t payload[1024];
        const std::size_t payloadSize = makeVoiceEnvelope(opusFrame, static_cast<std::size_t>(frameLength),
                                                          static_cast<std::uint32_t>(netTick()), xuid,
                                                          static_cast<std::uint32_t>(GET_CONFIG_VAR(soundboard_vars::VoiceFormat)),
                                                          payload, sizeof(payload));
        if (payloadSize == 0)
            return;

        void* const message = net_messages::makeMessage(22, payload, payloadSize, "Sound Board", framed, sizeof(framed));
        if (!message) {
            VerifyConsole::write(4.0f, "board", "send failed: %s",
                                 net_messages::lastMakeMessageFailure ? net_messages::lastMakeMessageFailure : "unknown");
            playing = false;
            return;
        }
        const bool accepted = net_messages::sendNetMessage(channel, message);
        net_messages::destroyMessage(message);
        if (!accepted)
            return; // buffer full: drop the chunk, keep playing (realtime pacing continues)
        ++chunksSent;
        if (chunksSent == 1)
            VerifyConsole::write(2.0f, "board", "streaming clip (%d-byte opus chunks)", (int)payloadSize);
        net_messages::commitChannel(channel);
        net_messages::transmitChannel(channel, "Sound Board");
    }

    // The local player's SteamID64 for the voice xuid field, resolved once per session through
    // the schema (CCSPlayerController.m_steamID). A zero xuid is the prime "server drops our
    // voice" suspect - the server may filter relayed voice by sender identity.
    void resolveLocalXuid() noexcept
    {
        if (xuidResolved)
            return;
        xuidResolved = true;
        const auto offset = hookContext.schemaSystem().getFieldOffset("CCSPlayerController", "m_steamID");
        if (!offset.has_value()) {
            VerifyConsole::write(4.0f, "board", "xuid: m_steamID offset unavailable - using zeros");
            return;
        }
        const auto controllerPointer = hookContext.patternSearchResults().template get<LocalPlayerControllerPointer>();
        if (!controllerPointer || !*controllerPointer) {
            VerifyConsole::write(4.0f, "board", "xuid: no local player controller - using zeros");
            return;
        }
        xuid = *reinterpret_cast<const volatile std::uint64_t*>(
            reinterpret_cast<std::uintptr_t>(*controllerPointer) + static_cast<std::uintptr_t>(offset.value()));
        VerifyConsole::write(4.0f, "board", "xuid: %llu", static_cast<unsigned long long>(xuid));
    }

    HookContext& hookContext;
    bool keyWasDown = false;
    bool xuidResolved = false;
    std::uint64_t xuid = 0;
    std::uint32_t chunksSent = 0;
};

} // namespace soundboard