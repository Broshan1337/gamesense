#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <Config/ConfigVariable.h>
#include <CS2/Constants/DllNames.h>
#include <Features/Game/PlayerAnalyzer/CheatOMeterState.h>
#include <Features/Game/PlayerAnalyzer/PlayerAnalyzerConfigVariables.h>
#include <GameClient/NetMessageFactory.h>
#include <GameClient/NetworkMessagesPointer.h>
#include <Platform/DynamicLibrary.h>
#include <Utils/SpinLock.h>
#include <Utils/StatusReport.h>
#include <Utils/VerifyConsole.h>
#include <Vmt/VmtLengthCalculator.h>
#include <Vmt/VmtSwapper.h>




















namespace voice_tap
{

using net_messages::BitRead;
using net_messages::ReadFromBufferFn;

inline constexpr std::uintptr_t kDataRelRoShAddr = 0x42D5920;      
inline constexpr std::uintptr_t kVoiceVtablePrimaryRva = 0x43E9108; 
inline constexpr std::uintptr_t kVoiceVtableProtoRva = 0x43E9148;   
inline constexpr std::size_t kVoiceMessageSize = 0x70;
inline constexpr std::size_t kRingSlots = 8;



alignas(VmtSwapper) inline unsigned char swapperStorage[sizeof(VmtSwapper)];
inline VmtSwapper& swapper()
{
    return *reinterpret_cast<VmtSwapper*>(swapperStorage);
}
inline std::atomic<bool> swapperConstructed{false};
inline std::atomic<bool> enabledMirror{false};
inline std::atomic<unsigned> dropCount{0};
inline std::atomic<bool> seenVoice{false};

struct StagedPacket {
    std::uintptr_t vtable{};
    std::uintptr_t messagePtr{};
    std::uint32_t readerBytes{};
    std::uint32_t wireLen{};
    std::uint8_t bytes[kVoiceMessageSize]{};
    std::uint8_t wire[384]{}; 
};

inline SpinLock ringLock;
struct Ring {
    StagedPacket slots[kRingSlots]{};
    int writeIndex = 0;
};
inline Ring ring;


inline std::uintptr_t voiceVtablePrimary = 0;
inline std::uintptr_t voiceVtableProto = 0;

inline ReadFromBufferFn originalReadFromBuffer = nullptr;

inline bool hookedReadFromBuffer(void* manager, net_messages::BitRead* reader, void* message) noexcept
{
    
    
    
    
    
    
    
    
    
    const std::uint8_t* wireStart = nullptr;
    std::uint32_t wireLen = 0;
    const bool probeActive = enabledMirror.load(std::memory_order_acquire);
    if (probeActive && reader && reader->data && reader->currentBit >= 0) {
        const int bytePos = reader->currentBit >> 3;
        const int remaining = reader->dataBytes - bytePos;
        if (remaining > 0) {
            wireStart = static_cast<const std::uint8_t*>(reader->data) + bytePos;
            wireLen = static_cast<std::uint32_t>(remaining > 384 ? 384 : remaining);
        }
    }

    const bool ok = originalReadFromBuffer(manager, reader, message);
    if (ok && message && probeActive) {
        const auto vtable = *reinterpret_cast<std::uintptr_t*>(message);
        if (vtable == voiceVtablePrimary || vtable == voiceVtableProto) {
            StagedPacket staged;
            staged.vtable = vtable;
            staged.messagePtr = reinterpret_cast<std::uintptr_t>(message);
            staged.readerBytes = reader ? static_cast<std::uint32_t>(reader->dataBits) : 0;
            staged.wireLen = wireLen;
            if (wireStart)
                std::memcpy(staged.wire, wireStart, wireLen);
            std::memcpy(staged.bytes, message, kVoiceMessageSize);

            const std::lock_guard guard{ringLock};
            ring.slots[ring.writeIndex] = staged;
            ring.writeIndex = (ring.writeIndex + 1) % kRingSlots;
            seenVoice.store(true, std::memory_order_release);
        }
    }
    return ok;
}

[[nodiscard]] inline bool install() noexcept
{
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    StatusReport::record("VoiceTap: manager vtable reshuffled (09-25), parse slot unverified - tap disabled", false);
    return false;

    NetworkMessagesPointer managerPointer;
    if (!managerPointer) {
        StatusReport::record("VoiceTap: CNetworkMessages not resolved", false);
        return false;
    }

    void* manager = managerPointer.get();
    
    
    auto* objectVmtSlot = reinterpret_cast<std::uintptr_t**>(manager);
    auto* vmt = *objectVmtSlot;
    if (!NetworkMessagesPointer::vtableInNetworkSystemModule(reinterpret_cast<std::uintptr_t>(vmt))) {
        StatusReport::record("VoiceTap: manager vtable outside libnetworksystem - fail closed", false);
        return false;
    }

    
    
    originalReadFromBuffer = reinterpret_cast<ReadFromBufferFn>(vmt[4]);
    if (!originalReadFromBuffer) {
        StatusReport::record("VoiceTap: ReadFromBuffer slot null - fail closed", false);
        return false;
    }

    
    const DynamicLibrary clientDll{cs2::CLIENT_DLL};
    const auto dataRelRo = clientDll.getVmtSection(); 
    if (dataRelRo.raw().empty() || !dataRelRo.raw().data()) {
        StatusReport::record("VoiceTap: libclient .data.rel.ro unresolved", false);
        return false;
    }
    const auto sectionBase = reinterpret_cast<std::uintptr_t>(dataRelRo.raw().data());
    voiceVtablePrimary = sectionBase + (kVoiceVtablePrimaryRva - kDataRelRoShAddr);
    voiceVtableProto = sectionBase + (kVoiceVtableProtoRva - kDataRelRoShAddr);

    const DynamicLibrary networkSystemDll{cs2::NETWORKSYSTEM_DLL};
    const VmtLengthCalculator calculator{networkSystemDll.getCodeSection(), networkSystemDll.getVmtSection()};
    if (!swapperConstructed.load(std::memory_order_acquire)) {
        new (swapperStorage) VmtSwapper{};
        swapperConstructed.store(true, std::memory_order_release);
    }
    
    
    
    
    
    
    
    if (!swapper().install(calculator, *objectVmtSlot, 128)) {
        StatusReport::record("VoiceTap: VMT install failed", false);
        return false;
    }
    
    static_cast<void>(swapper().hook(4, &hookedReadFromBuffer));
    StatusReport::record("VoiceTap: ReadFromBuffer hooked (voice filter armed)", true);
    return true;
}

inline void uninstall() noexcept
{
    if (!swapperConstructed.load(std::memory_order_acquire) || !swapper().wasEverInstalled())
        return;
    NetworkMessagesPointer managerPointer;
    if (!managerPointer)
        return;
    swapper().uninstall(*reinterpret_cast<std::uintptr_t**>(managerPointer.get()));
}



template <typename HookContext>
inline void refreshEnabled(HookContext& hookContext) noexcept
{
    enabledMirror.store(GET_CONFIG_VAR(analyzer_vars::VoiceProbe) || GET_CONFIG_VAR(analyzer_vars::VoiceLog), std::memory_order_release);
}




struct VoiceWireView {
    bool ok = false;
    bool hasAudio = false;
    unsigned format = 2;     
    bool hasSampleRate = false;
    unsigned sampleRate = 0;
    unsigned sequenceBytes = 0;
    unsigned sectionNumber = 0;
    unsigned numPackets = 0;
    unsigned long long xuid = 0;
    unsigned tick = 0;
    int entity = -1;
    unsigned voiceDataLen = 0;
    const std::uint8_t* voiceData = nullptr;
};

[[nodiscard]] inline bool readWireVarint(const std::uint8_t* data, std::uint32_t len, std::uint32_t& pos, unsigned& out) noexcept
{
    unsigned value = 0;
    unsigned shift = 0;
    while (pos < len && shift < 35) {
        const std::uint8_t byte = data[pos++];
        value |= static_cast<unsigned>(byte & 0x7Fu) << shift;
        if (!(byte & 0x80u)) {
            out = value;
            return true;
        }
        shift += 7;
    }
    return false;
}

[[nodiscard]] inline VoiceWireView parseVoiceWire(const std::uint8_t* data, std::uint32_t len) noexcept
{
    VoiceWireView view;
    std::uint32_t pos = 0;
    
    unsigned declared = 0;
    if (!readWireVarint(data, len, pos, declared))
        return view;
    std::uint32_t end = pos + declared;
    if (end > len)
        end = len; 
    while (pos < end) {
        const std::uint8_t key = data[pos++];
        switch (key) {
        case 0x0A: { 
            unsigned subLen = 0;
            if (!readWireVarint(data, len, pos, subLen) || pos + subLen > end)
                return view;
            view.hasAudio = true;
            const std::uint32_t subEnd = pos + subLen;
            std::uint32_t sub = pos;
            while (sub < subEnd) {
                const std::uint8_t audioKey = data[sub++];
                switch (audioKey & 7u) {
                case 0: { 
                    unsigned v = 0;
                    if (!readWireVarint(data, subEnd, sub, v))
                        return view;
                    if (audioKey == 0x08) view.format = v;
                    else if (audioKey == 0x1A) view.sequenceBytes = v;
                    else if (audioKey == 0x20) view.sectionNumber = v;
                    else if (audioKey == 0x28) { view.sampleRate = v; view.hasSampleRate = true; }
                    else if (audioKey == 0x38) view.numPackets = v;
                    break;
                }
                case 2: { 
                    unsigned l = 0;
                    if (!readWireVarint(data, subEnd, sub, l) || sub + l > subEnd)
                        return view;
                    if (audioKey == 0x12) {
                        view.voiceData = data + sub;
                        view.voiceDataLen = l;
                    }
                    sub += l;
                    break;
                }
                case 5: sub += 4; break; 
                default: return view;    
                }
            }
            pos = subEnd;
            break;
        }
        case 0x21: 
            if (pos + 8 > end)
                return view;
            std::memcpy(&view.xuid, data + pos, 8);
            pos += 8;
            break;
        case 0x40: { 
            unsigned v = 0;
            if (!readWireVarint(data, len, pos, v))
                return view;
            view.entity = static_cast<int>(v);
            break;
        }
        case 0x30: { 
            unsigned v = 0;
            if (!readWireVarint(data, len, pos, v))
                return view;
            view.tick = v;
            break;
        }
        case 0x10: 
        case 0x18: 
        case 0x28: 
        case 0x38: 
        case 0x48: { 
            unsigned v = 0;
            if (!readWireVarint(data, len, pos, v))
                return view;
            break;
        }
        default:
            return view; 
        }
    }
    view.ok = true;
    return view;
}



[[nodiscard]] inline bool classifyVoiceWire(const VoiceWireView& view, char (&tags)[64]) noexcept
{
    tags[0] = '\0';
    if (!view.ok)
        std::strncat(tags, "PARSE ", sizeof(tags) - std::strlen(tags) - 1);
    if (!view.hasAudio)
        std::strncat(tags, "NOAUDIO ", sizeof(tags) - std::strlen(tags) - 1);
    if (view.hasAudio && view.format != 2)
        std::strncat(tags, "FMT ", sizeof(tags) - std::strlen(tags) - 1);
    if (view.hasSampleRate && view.sampleRate != 48000)
        std::strncat(tags, "SR ", sizeof(tags) - std::strlen(tags) - 1);
    if (view.xuid == 0)
        std::strncat(tags, "XUID0 ", sizeof(tags) - std::strlen(tags) - 1);
    if (view.hasAudio && view.voiceDataLen < 16)
        std::strncat(tags, "TINY ", sizeof(tags) - std::strlen(tags) - 1);
    if (view.hasAudio && view.voiceDataLen > 4000)
        std::strncat(tags, "HUGE ", sizeof(tags) - std::strlen(tags) - 1);
    if (view.hasAudio && view.voiceDataLen > 0 && view.sequenceBytes == 0)
        std::strncat(tags, "SEQ0 ", sizeof(tags) - std::strlen(tags) - 1);
    if (view.hasAudio && view.numPackets > 4)
        std::strncat(tags, "NPK ", sizeof(tags) - std::strlen(tags) - 1);
    return tags[0] != '\0';
}




template <typename HookContext>
inline void drainProbe(HookContext& hookContext) noexcept
{
    refreshEnabled(hookContext);
    const bool fileLog = GET_CONFIG_VAR(analyzer_vars::VoiceLog);

    StagedPacket local[kRingSlots];
    int count = 0;
    {
        const std::lock_guard guard{ringLock};
        
        for (int i = 0; i < static_cast<int>(kRingSlots); ++i) {
            const int idx = (ring.writeIndex + i) % kRingSlots;
            const auto& slot = ring.slots[idx];
            if (slot.vtable == 0)
                continue;
            local[count++] = slot;
        }
        for (int i = 0; i < static_cast<int>(kRingSlots); ++i)
            ring.slots[i].vtable = 0;
    }

    const unsigned dropped = dropCount.exchange(0);
    if (dropped)
        VerifyConsole::write(5.0f, "vtap", "ring overflow, dropped %u", dropped);

    
    const char* configDir = nullptr;
    if (fileLog) {
        const auto& directoryPath = hookContext.configState().pathToConfigDirectory;
        if (directoryPath)
            configDir = reinterpret_cast<const char*>(directoryPath.get());
    }

    for (int i = 0; i < count; ++i) {
        const auto& p = local[i];
        const bool primary = p.vtable == voiceVtablePrimary;

        
        
        const VoiceWireView view = parseVoiceWire(p.wire, p.wireLen);
        char tags[64];
        const bool weird = classifyVoiceWire(view, tags);

        if (fileLog && weird && configDir) {
            
            const char* matchedName = cheat_ometer::addVoiceStrike(view.xuid);

            char capturePath[512];
            std::snprintf(capturePath, sizeof(capturePath), "%s/voice_captures.log", configDir);
            if (FILE* file = std::fopen(capturePath, "a")) {
                std::fprintf(file, "weird voice packet | tags=%s| xuid=%llu entity=%d tick=%u fmt=%u sr=%u seq=%u sect=%u npk=%u vlen=%u player=%s\n",
                             tags,
                             static_cast<unsigned long long>(view.xuid), view.entity, view.tick,
                             view.format, view.sampleRate, view.sequenceBytes, view.sectionNumber,
                             view.numPackets, view.voiceDataLen,
                             matchedName ? matchedName : "?");
                
                if (view.voiceData && view.voiceDataLen > 0) {
                    const unsigned hexLen = view.voiceDataLen > 48 ? 48 : view.voiceDataLen;
                    std::fprintf(file, "  vdata[%u]:", view.voiceDataLen);
                    for (unsigned b = 0; b < hexLen; ++b)
                        std::fprintf(file, " %02X", view.voiceData[b]);
                    std::fprintf(file, "%s\n", view.voiceDataLen > 48 ? " ..." : "");
                }
                
                if (!view.ok && p.wireLen > 0) {
                    std::fprintf(file, "  wire[%u]:", p.wireLen);
                    for (unsigned b = 0; b < p.wireLen; ++b)
                        std::fprintf(file, " %02X", p.wire[b]);
                    std::fprintf(file, "\n");
                }
                std::fclose(file);
            }
            VerifyConsole::write(2.0f, "vtap", "weird voice: tags=%s xuid=%llu player=%s (logged)", tags,
                                 static_cast<unsigned long long>(view.xuid), matchedName ? matchedName : "?");
        }

        if (!GET_CONFIG_VAR(analyzer_vars::VoiceProbe))
            continue;
        
        VerifyConsole::write(0.0f, "vtap", "%s msg=%p bits=%u%s",
            primary ? "primary" : "proto", reinterpret_cast<void*>(p.messagePtr), p.readerBytes,
            weird ? " WEIRD" : "");
        for (int row = 0; row < static_cast<int>(kVoiceMessageSize); row += 16) {
            char hex[3 * 16 + 1];
            static_assert(sizeof(hex) == 49);
            for (int c = 0; c < 16; ++c)
                std::snprintf(hex + c * 3, 4, "%02X ", p.bytes[row + c]);
            hex[48] = '\0';
            VerifyConsole::write(0.0f, "vtap", "+%02X: %s", row, hex);
        }
    }
}

}
