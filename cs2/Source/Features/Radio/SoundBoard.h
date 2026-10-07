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




















namespace soundboard
{


inline constexpr std::size_t kMaxClipSamples = 48000 * 30;
inline constexpr std::size_t kMaxClips = 64;
inline constexpr int kSamplesPerFrame = 960; 
inline constexpr std::size_t kMaxClipName = 96;
inline constexpr std::size_t kMaxPayload = 1024;
inline constexpr std::size_t kFramedMax = kMaxPayload + 12;
inline std::uint8_t framed[kFramedMax]{};


inline std::int16_t clipPcm[kMaxClipSamples]{};
inline std::size_t clipSamples = 0;      
inline bool playing = false;
inline std::size_t playPos = 0;          
inline float pacingAcc = 0.0f;           
inline char currentClip[kMaxClipName]{};

inline pthread_mutex_t namesMutex = PTHREAD_MUTEX_INITIALIZER;
inline char clipNames[kMaxClips][kMaxClipName]{};
inline const char* clipNamePtrs[kMaxClips]{};
inline int clipCount = 0;
inline float clipScanAcc = 0.0f;         
inline bool firstBloodUsed = false;      


inline void* opusEncoder = nullptr;
inline bool opusFailed = false;

using EncoderCreateFn = void* (*)(std::int32_t, std::int32_t, std::int32_t, std::int32_t*);
using EncodeFn = std::int32_t (*)(void*, const std::int16_t*, std::int32_t, std::uint8_t*, std::int32_t);
using CtlFn = std::int32_t (*)(void*, std::int32_t, ...);

inline constexpr int kOpusApplicationVoip = 2048;
inline constexpr int kOpusSetBitrate = 4002;

inline EncodeFn opusEncode = nullptr;










inline void debugAppend(const char* name, const void* data, std::size_t length) noexcept
{
    char path[ns_paths::kMaxPath];
    if (!ns_paths::join(path, sizeof(path), name))
        return;
    const int fd = ::open(path, 0x401  | 0100 , 0644);
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








[[nodiscard]] inline std::size_t makeVoiceEnvelope(const std::uint8_t* frame, std::size_t frameLength,
                                                   std::uint32_t tick, std::uint64_t xuid, std::uint32_t voiceFormat,
                                                   std::uint8_t* out, std::size_t outCap) noexcept
{
    
    std::uint8_t audio[512];
    std::size_t audioLength = 0;
    audio[audioLength++] = 0x08;
    net_messages::appendVarint(audio, audioLength, voiceFormat);
    audio[audioLength++] = 0x12;
    net_messages::appendVarint(audio, audioLength, static_cast<std::uint32_t>(frameLength));
    std::memcpy(audio + audioLength, frame, frameLength);
    audioLength += frameLength;
    audio[audioLength++] = 0x28; 
    net_messages::appendVarint(audio, audioLength, 48000);
    audio[audioLength++] = 0x38; 
    audio[audioLength++] = 0x01;
    audio[audioLength++] = 0x40; 
    audio[audioLength++] = 0x00;

    std::size_t offset = 0;
    out[offset++] = 0x0A; 
    net_messages::appendVarint(out, offset, static_cast<std::uint32_t>(audioLength));
    std::memcpy(out + offset, audio, audioLength);
    offset += audioLength;

    out[offset++] = 0x11; 
    
    for (int byte = 0; byte < 8; ++byte)
        out[offset++] = static_cast<std::uint8_t>(xuid >> (byte * 8u));
    out[offset++] = 0x18; 
    net_messages::appendVarint(out, offset, tick);
    return offset <= outCap ? offset : 0;
}




[[nodiscard]] inline bool loadWav(const char* path) noexcept
{
    const int fd = ::open(path, 0 );
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

            
            static std::uint8_t raw[48000 * 30 * 4];
            const std::size_t toRead = chunkSize < sizeof(raw) ? chunkSize : sizeof(raw);
            if (toRead < frameBytes || ::read(fd, raw, static_cast<ssize_t>(toRead)) != static_cast<ssize_t>(toRead)) { ::close(fd); return false; }
            const std::size_t sourceFrames = toRead / frameBytes;

            
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
    ctl(encoder, kOpusSetBitrate, 64000); 
    opusEncoder = encoder;
}



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

} 


namespace soundboard
{

template <typename HookContext>
class SoundBoard {
public:
    explicit SoundBoard(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
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

        clipScanAcc += 15.625f; 
        if (clipScanAcc >= 1000.0f) {
            clipScanAcc = 0.0f;
            scanClips(hookContext);
        }

        
        const int key = GET_CONFIG_VAR(soundboard_vars::SoundKeyBind);
        const bool down = Bind::isDown(key);
        if (down && !keyWasDown && key != Bind::kOff)
            startPlayback();
        keyWasDown = down;

        if (!playing)
            return;

        
        pacingAcc += 15.625f;
        if (pacingAcc < 20.0f)
            return;
        pacingAcc -= 20.0f;

        sendChunk();
    }

    
    void onGameEvent(cs2::IGameEvent* event) noexcept
    {
        if (!event || !GET_CONFIG_VAR(radio_vars::AirhornEnabled))
            return;

        if (game_events::is(event, "round_end")) {
            firstBloodUsed = false; 
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
            
            return;
        }
        playing = true;
        playPos = 0;
        pacingAcc = 20.0f; 
        chunksSent = 0;
    }

    
    void sendChunk() noexcept
    {
        const NetworkGameClientPointer clientPointer{};
        if (!clientPointer || !clientPointer.valid()) {
            playing = false;
            return;
        }
        void* const channel = net_messages::getClientChannel(clientPointer.get());
        if (!channel || !net_messages::channelReady(channel))
            return; 

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
        std::memcpy(frame, clipPcm + playPos, take * sizeof(std::int16_t)); 
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
            return; 
        ++chunksSent;
        if (chunksSent == 1)
            VerifyConsole::write(2.0f, "board", "streaming clip (%d-byte opus chunks)", (int)payloadSize);
        net_messages::commitChannel(channel);
        net_messages::transmitChannel(channel, "Sound Board");
    }

    
    
    
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

} 