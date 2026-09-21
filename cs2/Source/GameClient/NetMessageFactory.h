#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Constants/DllNames.h>
#include <GameClient/NetworkMessagesPointer.h>

// NetMessageFactory - the shared "send a real net message through the game's own net channel"
// machinery, extracted from the Server Lagger (2026-09-13) so any feature can ride it.
//
// Chain per send (every Windows vcall number re-derived and verified on Linux - NEVER trust the
// friend's Windows numbers 1:1, MSVC<->Itanium slots are per-class shifted):
//   NetworkMessagesVersion001 (libnetworksystem CreateInterface) -> CNetworkMessages manager
//   findRecord(typeId) [manager slot 30] -> record; info(record) [slot 12] = record+0x10;
//   binding = *(info+8); create [binding slot 6] -> 0x60-byte message object;
//   parse via ReadFromBuffer [manager slot 4] from a bit_read_t wrapping
//   varint(payloadSize) + payload + 4 zero slack bytes;
//   CNetChan::SendNetMessage [slot 40] (channel, message, -1) queues the message bits;
//   commit [slot 43] (no args) seals all 3 stream buffers' write cursors;
//   SendData [slot 42] (this, name, nullptr) builds + transmits a datagram from committed data.
//   Message clone = slot 5 (Itanium copy ctor; slot 4 is a getter here), destroy = slot 1
//   (Itanium D2 deleting dtor - slot 0 D1 would leak).
//
// Consumers: ServerLagger (CCLCMsg_VoiceData, record 22), UserInfoFlood direct mode
// (CNETMsg_SetConVar, record 6 - the client->server userinfo cvar update, schema verified
// against the embedded protobuf descriptors: CNETMsg_SetConVar { optional CMsg_CVars
// convars = 1; } / CMsg_CVars { repeated CVar cvars = 1; } / CVar { name = 1; value = 2; }).
//
// Every object's vtable is validated against the owning module before any slot is called
// (NetworkMessagesPointer / NetworkGameClientPointer). Failures return null/false - features
// latch fail closed and log; no wild vtable calls ever.
namespace net_messages
{

// bit_read_t: the engine's S1-heritage bit reader, layout friend-verified against the Linux
// ReadFromBuffer disassembly (data_bits @ +0xC, current_bit @ +0x10, overflow @ +0x20).
struct BitRead {
    const void* data;
    std::int32_t dataBytes;
    std::int32_t dataBits;
    std::int32_t currentBit;
    std::uint32_t reserved;
    const char* debugName;
    bool overflow;
    bool initialized;
    bool dwordSafe;
    std::uint8_t tail[5];
};
static_assert(sizeof(BitRead) == 0x28);

// Serialize-size sanity ceiling: a healthy parsed payload serializes to at most this many bytes.
// Anything bigger means the parse corrupted a length field (the 2026-09-12 233MB serialize wedge).
inline constexpr std::uint32_t kMaxSaneSerializeBytes = 32 * 1024;

// Verified Linux slot indexes (byte offsets = slot * 8).
inline constexpr std::size_t kSlotFindRecord = 30 * 8;      // CNetworkMessages: registry walk by message type id
inline constexpr std::size_t kSlotGetInfo = 12 * 8;         // CNetworkMessages: info = record+0x10
inline constexpr std::size_t kSlotReadFromBuffer = 4 * 8;   // CNetworkMessages: parse bit_read_t into message
inline constexpr std::size_t kSlotBindingCreate = 6 * 8;    // CProtobufBinding: new 0x60-byte message
inline constexpr std::size_t kSlotMessageClone = 5 * 8;     // CNetMessagePB copy ctor
inline constexpr std::size_t kSlotMessageDestroy = 1 * 8;   // Itanium D2 deleting dtor
inline constexpr std::size_t kSlotChannelSend = 40 * 8;     // CNetChan::SendNetMessage(msg, flag)
inline constexpr std::size_t kSlotChannelCommit = 43 * 8;   // CNetChan: commit all 3 stream buffers
inline constexpr std::size_t kSlotChannelSendData = 42 * 8; // CNetChan: build+send datagram from committed
inline constexpr std::size_t kSlotChannelReady = 47 * 8;    // CNetChan: send-window check
inline constexpr std::size_t kSlotClientGetChannel = 41 * 8;// CNetworkGameClient::GetChannel(slot)

// CNetworkGameClient tick field (verified: GetTick vtable slot 5 body reads [this+0x388]).
inline constexpr std::uintptr_t kClientTickFieldOffset = 0x388;

using FindRecordFn = void* (*)(void* manager, int messageId);
using GetInfoFn = void* (*)(void* manager, void* record);
using ReadFromBufferFn = bool (*)(void* manager, BitRead* reader, void* message);
using CreateMessageFn = void* (*)(void* binding);
using CloneMessageFn = void* (*)(void* message);
using DestroyMessageFn = void (*)(void* message);
using SendNetMessageFn = bool (*)(void* channel, void* message, std::int8_t flag);
using CommitChannelFn = void (*)(void* channel);
using SendDataFn = std::int32_t (*)(void* channel, const void* descriptor, void* argument);
using ChannelReadyFn = bool (*)(void* channel);
using GetChannelFn = void* (*)(void* networkClient, int slot);

[[nodiscard]] inline void* vtableSlot(void* object, std::size_t byteOffset) noexcept
{
    void* vtable = *reinterpret_cast<void**>(object);
    return *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(vtable) + byteOffset);
}

inline void appendVarint(std::uint8_t* out, std::size_t& written, std::uint32_t value) noexcept
{
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7Fu);
        value >>= 7u;
        if (value)
            byte |= 0x80u;
        out[written++] = byte;
    } while (value);
}

[[nodiscard]] inline std::size_t varintLength(std::uint32_t value) noexcept
{
    std::size_t length = 1;
    while (value >= 0x80u) {
        value >>= 7u;
        ++length;
    }
    return length;
}

// The protobuf sub-object lives at message+0x30 (embedded). Its vtable slot 8 (byte 0x40) is
// ByteSize - the same call the engine's own serialize path makes before malloc'ing the output.
[[nodiscard]] inline std::uint32_t protoByteSize(void* message) noexcept
{
    if (!message)
        return 0;
    void* proto = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(message) + 0x30);
    void* protoVtable = *reinterpret_cast<void**>(proto);
    if (!protoVtable)
        return 0;
    const auto byteSizeFn = *reinterpret_cast<std::uint32_t (**)(void*)>(reinterpret_cast<std::uintptr_t>(protoVtable) + 0x40);
    return byteSizeFn ? byteSizeFn(proto) : 0;
}

inline void destroyMessage(void* message) noexcept
{
    if (!message)
        return;
    const auto destroyFn = reinterpret_cast<DestroyMessageFn>(vtableSlot(message, kSlotMessageDestroy));
    if (destroyFn)
        destroyFn(message);
}

// Resolves manager -> record(typeId) -> info -> binding. Returns null on any failure (the
// caller latches fail closed). No caching: the lagger already walks the registry per send and
// it is a hash lookup, not a scan.
[[nodiscard]] inline void* resolveMessageBinding(int recordId) noexcept
{
    const NetworkMessagesPointer messagesPointer{};
    if (!messagesPointer)
        return nullptr;
    void* const manager = messagesPointer.get();

    const auto findRecord = reinterpret_cast<FindRecordFn>(vtableSlot(manager, kSlotFindRecord));
    void* const record = findRecord ? findRecord(manager, recordId) : nullptr;
    if (!record)
        return nullptr;

    const auto getInfo = reinterpret_cast<GetInfoFn>(vtableSlot(manager, kSlotGetInfo));
    void* const info = getInfo ? getInfo(manager, record) : nullptr;
    if (!info)
        return nullptr;

    return *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(info) + 8);
}

// Diagnostic: the stage string of the LAST makeMessage failure (null = the last call
// succeeded). Game-thread only (all consumers are); the features log it in their idle
// breadcrumbs so a mid-session resolution/parse drift names its stage instead of failing
// silently.
inline const char* lastMakeMessageFailure = nullptr;

// Creates a message object for `recordId` and parses `payload` into it. The framed form fed to
// ReadFromBuffer is varint(payloadSize) + payload + 4 zero slack bytes (the friend-source
// framing, live-verified). Returns null on any failure (binding/create/parse/overflow/serialize
// gate; lastMakeMessageFailure then names the stage) - a non-null return MUST be released with
// destroyMessage().
[[nodiscard]] inline void* makeMessage(int recordId, const void* payload, std::size_t payloadSize,
                                       const char* debugName, std::uint8_t* framedScratch, std::size_t scratchSize) noexcept
{
    void* const binding = resolveMessageBinding(recordId);
    if (!binding) {
        lastMakeMessageFailure = "record/binding unresolved";
        return nullptr;
    }

    const auto createFn = reinterpret_cast<CreateMessageFn>(vtableSlot(binding, kSlotBindingCreate));
    void* const message = createFn ? createFn(binding) : nullptr;
    if (!message) {
        lastMakeMessageFailure = "binding create failed";
        return nullptr;
    }

    const NetworkMessagesPointer messagesPointer{};
    if (!messagesPointer) {
        destroyMessage(message);
        lastMakeMessageFailure = "message factory unavailable";
        return nullptr;
    }

    std::size_t written = 0;
    appendVarint(framedScratch, written, static_cast<std::uint32_t>(payloadSize));
    if (written + payloadSize + 4 > scratchSize) {
        destroyMessage(message);
        lastMakeMessageFailure = "payload does not fit scratch";
        return nullptr;
    }
    std::memcpy(framedScratch + written, payload, payloadSize);
    written += payloadSize;
    const std::size_t logicalSize = written; // varint + payload exactly
    std::memset(framedScratch + written, 0, 4);
    written += 4;

    BitRead reader{
        framedScratch,
        static_cast<std::int32_t>(logicalSize),
        static_cast<std::int32_t>(logicalSize * 8),
        0,
        0,
        debugName,
        false,
        true,
        true,
        {},
    };

    const auto readFromBuffer = reinterpret_cast<ReadFromBufferFn>(vtableSlot(messagesPointer.get(), kSlotReadFromBuffer));
    const bool parsed = readFromBuffer && readFromBuffer(messagesPointer.get(), &reader, message) && !reader.overflow;
    if (!parsed) {
        destroyMessage(message);
        lastMakeMessageFailure = reader.overflow ? "parse overflow" : "parse failed";
        return nullptr;
    }

    const auto byteSize = protoByteSize(message);
    if (byteSize == 0 || byteSize > kMaxSaneSerializeBytes) {
        destroyMessage(message);
        lastMakeMessageFailure = "serialize size gate";
        return nullptr;
    }
    lastMakeMessageFailure = nullptr;
    return message;
}

// Validates + returns the client's CNetChan for slot 0, or null (not connected / stale object).
[[nodiscard]] inline void* getClientChannel(void* networkClient) noexcept
{
    if (!networkClient)
        return nullptr;
    const auto getChannelFn = reinterpret_cast<GetChannelFn>(vtableSlot(networkClient, kSlotClientGetChannel));
    void* const channel = getChannelFn ? getChannelFn(networkClient, 0) : nullptr;
    if (!channel || !NetworkMessagesPointer::vtableInNetworkSystemModule(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(channel))))
        return nullptr;
    return channel;
}

[[nodiscard]] inline bool channelReady(void* channel) noexcept
{
    const auto readyFn = reinterpret_cast<ChannelReadyFn>(vtableSlot(channel, kSlotChannelReady));
    return readyFn && readyFn(channel);
}

// Clones a parsed message (Itanium copy ctor, slot 5 - slot 4 is a getter on Linux; the friend's
// Windows slot 4 must never be ported). Returns null on failure; the clone is the caller's to
// destroy with destroyMessage().
[[nodiscard]] inline void* cloneMessage(void* message) noexcept
{
    if (!message)
        return nullptr;
    const auto cloneFn = reinterpret_cast<CloneMessageFn>(vtableSlot(message, kSlotMessageClone));
    return cloneFn ? cloneFn(message) : nullptr;
}

// Queues a message object into the channel's send buffer. Returns false when refused (buffer
// full backpressure) - the message is still the caller's to destroy.
inline bool sendNetMessage(void* channel, void* message) noexcept
{
    const auto sendFn = reinterpret_cast<SendNetMessageFn>(vtableSlot(channel, kSlotChannelSend));
    return sendFn && sendFn(channel, message, -1);
}

// Seals the queued message bits into the outgoing stream (call after a batch of sends).
inline void commitChannel(void* channel) noexcept
{
    const auto commitFn = reinterpret_cast<CommitChannelFn>(vtableSlot(channel, kSlotChannelCommit));
    if (commitFn)
        commitFn(channel);
}

// Builds + transmits a datagram from committed data. Returns the transmitted byte count
// (0 = refused: channel overflow flag / flow limit) - refused data STAYS queued, so callers
// must back off. The byte count is the friend-lagger's "did the bytes actually leave" probe.
inline std::int32_t transmitChannelEx(void* channel, const char* debugName) noexcept
{
    const auto sendDataFn = reinterpret_cast<SendDataFn>(vtableSlot(channel, kSlotChannelSendData));
    return sendDataFn ? sendDataFn(channel, debugName, nullptr) : 0;
}

inline bool transmitChannel(void* channel, const char* debugName) noexcept
{
    return transmitChannelEx(channel, debugName) != 0;
}

[[nodiscard]] inline std::int32_t networkClientTick(void* networkClient) noexcept
{
    return *reinterpret_cast<const volatile std::int32_t*>(
        reinterpret_cast<std::uintptr_t>(networkClient) + kClientTickFieldOffset);
}

// --- CNETMsg_SetConVar payload builder -----------------------------------------------------
// CNETMsg_SetConVar { optional CMsg_CVars convars = 1; }
// CMsg_CVars { repeated CVar cvars = 1; }  CVar { optional string name = 1; optional string value = 2; }
// (schema verified against the embedded protobuf descriptors in libengine2: CNETMsg_SetConVar
// blob @0x2b20a2, CMsg_CVars blob @0x2b1e85 - 2026-09-13).
inline constexpr int kSetConVarMessageId = 6; // NET group id (N13CNetMessagePBILi6E17CNETMsg_SetConVar in the RTTI catalog)

struct SetConVarEntry {
    const char* name;
    const char* value;
};

// Writes the wire form of a SetConVar message carrying all `entries`. Returns the byte count,
// or 0 when the entries do not fit `outCap` (or count/outCap degenerate).
[[nodiscard]] inline std::size_t makeSetConVarPayload(const SetConVarEntry* entries, int count,
                                                      std::uint8_t* out, std::size_t outCap) noexcept
{
    if (!entries || count <= 0 || !out || outCap == 0)
        return 0;

    std::size_t cvarsLength = 0;
    for (int i = 0; i < count; ++i) {
        const std::size_t nameLength = entries[i].name ? std::strlen(entries[i].name) : 0;
        const std::size_t valueLength = entries[i].value ? std::strlen(entries[i].value) : 0;
        if (nameLength == 0)
            return 0;
        // entry = 0x0A varint(len(name)) name 0x12 varint(len(value)) value
        const std::size_t entryLength = 1 + varintLength(static_cast<std::uint32_t>(nameLength)) + nameLength
            + 1 + varintLength(static_cast<std::uint32_t>(valueLength)) + valueLength;
        // repeated field header = 0x0A varint(entryLength)
        cvarsLength += 1 + varintLength(static_cast<std::uint32_t>(entryLength)) + entryLength;
    }

    const std::size_t totalLength = 1 + varintLength(static_cast<std::uint32_t>(cvarsLength)) + cvarsLength;
    if (totalLength > outCap)
        return 0;

    std::size_t written = 0;
    out[written++] = 0x0A; // convars = 1, length-delimited
    appendVarint(out, written, static_cast<std::uint32_t>(cvarsLength));
    for (int i = 0; i < count; ++i) {
        const auto* name = reinterpret_cast<const std::uint8_t*>(entries[i].name);
        const auto* value = reinterpret_cast<const std::uint8_t*>(entries[i].value);
        const std::size_t nameLength = entries[i].name ? std::strlen(entries[i].name) : 0;
        const std::size_t valueLength = entries[i].value ? std::strlen(entries[i].value) : 0;
        const std::size_t entryLength = 1 + varintLength(static_cast<std::uint32_t>(nameLength)) + nameLength
            + 1 + varintLength(static_cast<std::uint32_t>(valueLength)) + valueLength;

        out[written++] = 0x0A; // cvars = 1, length-delimited
        appendVarint(out, written, static_cast<std::uint32_t>(entryLength));
        out[written++] = 0x0A; // name = 1
        appendVarint(out, written, static_cast<std::uint32_t>(nameLength));
        std::memcpy(out + written, name, nameLength);
        written += nameLength;
        out[written++] = 0x12; // value = 2
        appendVarint(out, written, static_cast<std::uint32_t>(valueLength));
        std::memcpy(out + written, value, valueLength);
        written += valueLength;
    }
    return written;
}

// Sends ONE SetConVar message carrying a single cvar entry through the client's net channel:
// queue (slot 40) -> commit (43) -> transmit (42), so the update leaves as its own datagram the
// moment it is queued. This is the direct rename/update path for latency-sensitive churn (the
// name animator's per-frame renames) - it bypasses the engine's own userinfo-change machinery,
// which coalesces console-driven churn. On failure *failReason (if requested) names the stage;
// callers log throttled and fall back to the console path so a direct failure can never take
// the feature down with it. Value is trusted to fit 512 bytes (animator frames are <= 160).
[[nodiscard]] inline bool sendSingleSetConVar(void* networkClient, const char* cvarName, const char* value, const char* debugName, const char** failReason = nullptr) noexcept
{
    void* const channel = getClientChannel(networkClient);
    if (!channel) {
        if (failReason)
            *failReason = "no net channel";
        return false;
    }
    if (!channelReady(channel)) {
        if (failReason)
            *failReason = "send window closed";
        return false;
    }

    const SetConVarEntry entry{cvarName, value};
    std::uint8_t payload[512];
    const std::size_t payloadSize = makeSetConVarPayload(&entry, 1, payload, sizeof(payload));
    if (payloadSize == 0) {
        if (failReason)
            *failReason = "value too long";
        return false;
    }

    std::uint8_t framed[528];
    void* const message = makeMessage(kSetConVarMessageId, payload, payloadSize, debugName, framed, sizeof(framed));
    if (!message) {
        if (failReason)
            *failReason = lastMakeMessageFailure ? lastMakeMessageFailure : "factory failed";
        return false;
    }
    const bool accepted = sendNetMessage(channel, message);
    destroyMessage(message);
    if (!accepted) {
        if (failReason)
            *failReason = "channel refused message";
        return false;
    }

    commitChannel(channel);
    if (!transmitChannel(channel, debugName)) {
        if (failReason)
            *failReason = "datagram transmit refused";
        return false;
    }
    return true;
}

} // namespace net_messages
