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

// VoiceTapHook - the receive-side tap for the CHEAT O METER voice revealer.
//
// CNetworkMessages::ReadFromBuffer (manager vtable slot 4 - the same byte-exact slot the
// lagger uses to parse its crafted payloads, NetMessageFactory.h) is the universal parse:
// EVERY network message that arrives on the connection is parsed through it, and so is
// everything we send ourselves (transparent: the hook runs the original first and only
// inspects the parsed object afterwards).
//
// Filtering: CSVCMsg_VoiceData (record 47) objects are recognized by their vtable pointer.
// The class has two libclient vtables (RE 2026-09-14, build 14181): the CNetMessagePB primary
// at +0x43E9108 and the embedded protobuf sub-object at +0x43E9148 (the protobuf part lives at
// message+0x30, per the class's own GetProto slot `lea rax, [rdi+0x30]; ret`). Both RVAs are
// .data.rel.ro entries; at runtime they resolve through the section base. The probe accepts
// EITHER and logs which one sat at object+0, so the field-offset calibration is evidence-based.
//
// Network-thread discipline: the hook body only memcpys into a staging ring under a spinlock
// and reads an atomic enable mirror - no console writes, no config reads off the game thread
// (the radio present-thread console crash class). The game thread drains the ring from
// ViewRenderHook_onRenderStart and refreshes the mirror from the config var.
namespace voice_tap
{

using net_messages::BitRead;
using net_messages::ReadFromBufferFn;

inline constexpr std::uintptr_t kDataRelRoShAddr = 0x42D5920;      // libclient .data.rel.ro sh_addr (build 14181)
inline constexpr std::uintptr_t kVoiceVtablePrimaryRva = 0x43E9108; // CNetMessagePB<47, CSVCMsg_VoiceData>
inline constexpr std::uintptr_t kVoiceVtableProtoRva = 0x43E9148;   // embedded protobuf sub-object vtable
inline constexpr std::size_t kVoiceMessageSize = 0x70;
inline constexpr std::size_t kRingSlots = 8;

// Placement storage: -nostdlib has no __cxa_atexit/__dso_handle, so an inline global with a
// non-trivial destructor (VmtSwapper holds an optional) cannot be a plain inline object.
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
    std::uint8_t wire[384]{}; // raw wire snapshot at the reader's entry position
};

inline SpinLock ringLock;
struct Ring {
    StagedPacket slots[kRingSlots]{};
    int writeIndex = 0;
};
inline Ring ring;

// Resolved once at install: the two candidate voice vtable addresses in the live process.
inline std::uintptr_t voiceVtablePrimary = 0;
inline std::uintptr_t voiceVtableProto = 0;

inline ReadFromBufferFn originalReadFromBuffer = nullptr;

inline bool hookedReadFromBuffer(void* manager, net_messages::BitRead* reader, void* message) noexcept
{
    // Wire snapshot BEFORE the original consumes the reader: the entry position is the start of
    // this message (varint payload length + protobuf body) - parseable without any parsed-object
    // offsets, using the CMsgVoiceAudio wire schema the soundboard already builds.
    //
    // GATED on the enable mirror (hardening 2026-09-24): the game update reshuffled this
    // manager's vtable (slot 1 is no longer the old binding getter - RegisterFieldChange-
    // CallbackPriority lives there now), so slot 4's argument semantics MUST NOT be trusted
    // blindly. When the probe is off the hook must be a pure, touch-nothing pass-through:
    // dereferencing reader->data on a reshuffled slot's args would be a wild read.
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
    // FAIL-CLOSED (2026-09-25 game update): the manager (INetworkMessageInternal) vtable
    // reshuffled for the THIRD time in a row (09-23, 09-24, 09-25) and slot 4 is no longer
    // the parse. Live evidence (18:44 loader-session crash): hooking the new slot 4 and
    // truncating its return to bool wild-called the game into
    // libnetworksystem RegisterFieldChangeCallbackPriority (its assert fires -> dialog +
    // null-write + ud2 abort), because the new slot 4 is an unrelated method whose return
    // is not a bool. A hook on an unverified slot is a loaded gun: fail closed until the
    // parse slot is re-derived AND verified (the lagger's crafted-parse flow, once its own
    // stale manager slots are re-derived, is the natural live verifier - it calls the same
    // slot with a crafted bit reader + message).
    //
    // 09-25 static layout (libnetworksystem manager vtable @0x48b080, 40 primary slots +
    // secondary-base composite to ~slot 54):
    //   slot 1 = RegisterFieldChangeCallbackPriority (0x2b22c0 - the assert fn)
    //   slot 7 = PARSE entry (0x2b4bc0: varint msg-id read + dword-pair decode off the bit
    //            reader, tail-calls slot 5) - 4th arg in rcx is forwarded, a 3-arg hook
    //            would clobber it
    //   slot 8 = clone-message forwarder (tail-jumps msg->vtable[5] = the 0x70-alloc copy
    //            ctor; slot 5 is NOT the parse)
    //   slot 9 = destroy-message forwarder (msg->vtable[1] = Itanium D0)
    //   message-class layout UNCHANGED vs 09-12: 0=D1, 1=D0(delete), 2/3=GetProto(lea
    //   [rdi+0x30]), 4=getter, 5=clone(0x70 alloc+copy), 6=returns the netmsg id (0x2f
    //   for voice)
    //   voice wrapper vtables moved: libclient .data.rel.ro primary 0x44e6ed0 (off_to_top
    //   0), embedded proto sub-object 0x44e6f20 (off_to_top -0x30 = msg+0x30) - replaces
    //   the stale build-14181 RVAs 0x43E9108/0x43E9148 + sh_addr 0x42D5920 below
    StatusReport::record("VoiceTap: manager vtable reshuffled (09-25), parse slot unverified - tap disabled", false);
    return false;

    NetworkMessagesPointer managerPointer;
    if (!managerPointer) {
        StatusReport::record("VoiceTap: CNetworkMessages not resolved", false);
        return false;
    }

    void* manager = managerPointer.get();
    // The OBJECT's vtable slot must be the swapped lvalue (install writes the replacement
    // pointer through it) - passing a local copy left the hook silently inert.
    auto* objectVmtSlot = reinterpret_cast<std::uintptr_t**>(manager);
    auto* vmt = *objectVmtSlot;
    if (!NetworkMessagesPointer::vtableInNetworkSystemModule(reinterpret_cast<std::uintptr_t>(vmt))) {
        StatusReport::record("VoiceTap: manager vtable outside libnetworksystem - fail closed", false);
        return false;
    }

    // Save the original BEFORE the slot swap: a receive-thread call between the swap and the
    // assignment would otherwise call through a null original.
    originalReadFromBuffer = reinterpret_cast<ReadFromBufferFn>(vmt[4]);
    if (!originalReadFromBuffer) {
        StatusReport::record("VoiceTap: ReadFromBuffer slot null - fail closed", false);
        return false;
    }

    // Voice vtable addresses from libclient's .data.rel.ro section base (RVA - sh_addr).
    const DynamicLibrary clientDll{cs2::CLIENT_DLL};
    const auto dataRelRo = clientDll.getVmtSection(); // .data.rel.ro
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
    // minSlots 128 (hardening 2026-09-24 crash fix): the manager is INetworkMessageInternal
    // and the game dispatches it POSITIONALLY well beyond the hooked slot - a live crash showed
    // [vptr+0x2B0] (slot 86). The 2026-09-23 game build additionally put a NULL at slot 0 of
    // this vtable, which made the old stop-at-first-null scan compute length 0 (floored to 5)
    // -> every dispatch beyond slot 4 read out of bounds of the pool allocation. The fixed
    // null-tolerant scan yields 102; 128 is the floor so a future reshuffle cannot truncate
    // below the observed dispatch range again.
    if (!swapper().install(calculator, *objectVmtSlot, 128)) {
        StatusReport::record("VoiceTap: VMT install failed", false);
        return false;
    }
    // write the replacement into the copied vmt (the original was saved before the swap)
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

// Game thread: refresh the enable mirror from the config var. Call every frame (cheap atomic
// store) so the network-thread hook never touches the config system.
template <typename HookContext>
inline void refreshEnabled(HookContext& hookContext) noexcept
{
    enabledMirror.store(GET_CONFIG_VAR(analyzer_vars::VoiceProbe) || GET_CONFIG_VAR(analyzer_vars::VoiceLog), std::memory_order_release);
}

// ---- CSVCMsg_VoiceData wire parsing (schema parsed from the embedded descriptor 2026-09-14;
// the same envelope the soundboard BUILDS, so the field encodings are trusted) ----------------

struct VoiceWireView {
    bool ok = false;
    bool hasAudio = false;
    unsigned format = 2;     // VoiceDataFormat_t: STEAM=0 / ENGINE=1 / OPUS=2
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
    // leading varint = the message payload length (the ReadFromBuffer framing)
    unsigned declared = 0;
    if (!readWireVarint(data, len, pos, declared))
        return view;
    std::uint32_t end = pos + declared;
    if (end > len)
        end = len; // clamp: never parse past the snapshot
    while (pos < end) {
        const std::uint8_t key = data[pos++];
        switch (key) {
        case 0x0A: { // audio submessage
            unsigned subLen = 0;
            if (!readWireVarint(data, len, pos, subLen) || pos + subLen > end)
                return view;
            view.hasAudio = true;
            const std::uint32_t subEnd = pos + subLen;
            std::uint32_t sub = pos;
            while (sub < subEnd) {
                const std::uint8_t audioKey = data[sub++];
                switch (audioKey & 7u) {
                case 0: { // varint
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
                case 2: { // length-delimited
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
                case 5: sub += 4; break; // fixed32 (voice_level)
                default: return view;    // unknown wire type - stop trusting the parse
                }
            }
            pos = subEnd;
            break;
        }
        case 0x21: // xuid, fixed64
            if (pos + 8 > end)
                return view;
            std::memcpy(&view.xuid, data + pos, 8);
            pos += 8;
            break;
        case 0x40: { // entity, varint
            unsigned v = 0;
            if (!readWireVarint(data, len, pos, v))
                return view;
            view.entity = static_cast<int>(v);
            break;
        }
        case 0x30: { // tick, varint
            unsigned v = 0;
            if (!readWireVarint(data, len, pos, v))
                return view;
            view.tick = v;
            break;
        }
        case 0x10: // client_deprecated, varint
        case 0x18: // proximity, varint
        case 0x28: // audible_mask, varint
        case 0x38: // passthrough, varint
        case 0x48: { // caster, varint
            unsigned v = 0;
            if (!readWireVarint(data, len, pos, v))
                return view;
            break;
        }
        default:
            return view; // unknown field - stop trusting the parse
        }
    }
    view.ok = true;
    return view;
}

// Signature-free weirdness classifier: legit CS2 voice = OPUS format, 48kHz, plausible opus
// frame sizes, one packet per message. Shared-ESP smugglers break one of those.
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

// Game thread: drain staged packets - [vtap] hex probe lines when Voice Probe is on, and the
// weird-packet forensics log (configDir/voice_captures.log) + CHEAT O METER voice strikes when
// Voice Log is on.
template <typename HookContext>
inline void drainProbe(HookContext& hookContext) noexcept
{
    refreshEnabled(hookContext);
    const bool fileLog = GET_CONFIG_VAR(analyzer_vars::VoiceLog);

    StagedPacket local[kRingSlots];
    int count = 0;
    {
        const std::lock_guard guard{ringLock};
        // snapshot in ring order, oldest first
        for (int i = 0; i < kRingSlots; ++i) {
            const int idx = (ring.writeIndex + i) % kRingSlots;
            const auto& slot = ring.slots[idx];
            if (slot.vtable == 0)
                continue;
            local[count++] = slot;
        }
        for (int i = 0; i < kRingSlots; ++i)
            ring.slots[i].vtable = 0;
    }

    const unsigned dropped = dropCount.exchange(0);
    if (dropped)
        VerifyConsole::write(5.0f, "vtap", "ring overflow, dropped %u", dropped);

    // forensics file handle: opened once per drain when Voice Log is on
    const char* configDir = nullptr;
    if (fileLog) {
        const auto& directoryPath = hookContext.configState().pathToConfigDirectory;
        if (directoryPath)
            configDir = reinterpret_cast<const char*>(directoryPath.get());
    }

    for (int i = 0; i < count; ++i) {
        const auto& p = local[i];
        const bool primary = p.vtable == voiceVtablePrimary;

        // parse the wire snapshot + classify BEFORE the hex spam (the classifier decides the
        // file log; the hex lines are the calibration aid)
        const VoiceWireView view = parseVoiceWire(p.wire, p.wireLen);
        char tags[64];
        const bool weird = classifyVoiceWire(view, tags);

        if (fileLog && weird && configDir) {
            // CHEAT O METER attribution: weird xuid = a SteamID64 -> strike the matching player
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
                // head of the voice payload: enough for offline opus/sig analysis
                if (view.voiceData && view.voiceDataLen > 0) {
                    const unsigned hexLen = view.voiceDataLen > 48 ? 48 : view.voiceDataLen;
                    std::fprintf(file, "  vdata[%u]:", view.voiceDataLen);
                    for (unsigned b = 0; b < hexLen; ++b)
                        std::fprintf(file, " %02X", view.voiceData[b]);
                    std::fprintf(file, "%s\n", view.voiceDataLen > 48 ? " ..." : "");
                }
                // full wire snapshot for packets that failed the parse entirely
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
        // one hex line per 16 bytes keeps lines readable; the interesting region is +0x08..+0x70
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
