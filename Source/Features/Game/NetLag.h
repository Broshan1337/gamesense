#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <link.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <Features/Game/NetLagConfigVariables.h>
#include <GameClient/Bind.h>
#include <HookContext/HookContextMacros.h>
#include <Platform/Linux/LinuxDynamicLibrary.h>
#include <Platform/Linux/LinuxPlatformApi.h>

// Shared state between the game thread (config polling) and the network thread(s) that run the
// hooked sendto/sendmsg. Atomics only - the send path must never block on game-side locks.
namespace net_lag
{
inline std::atomic<bool> chokeEngaged{false};
inline std::atomic<bool> masterEnabled{false};
inline std::atomic<std::uint32_t> chokeWindow{4};
inline std::atomic<std::uint32_t> blipCount{2};
inline std::atomic<std::uint32_t> dupCount{0};
inline std::atomic<bool> floodEngaged{false};
inline std::atomic<std::uint32_t> floodCount{0};
inline std::atomic<bool> connlessFloodEngaged{false};
inline std::atomic<std::uint32_t> connlessFloodCount{0};
inline std::atomic<bool> delayEnabled{false};
inline std::atomic<std::uint32_t> delayMs{50};
inline std::atomic<bool> statsEnabled{false};

inline std::atomic<std::uint64_t> statSends{0};
inline std::atomic<std::uint64_t> statPassed{0};
inline std::atomic<std::uint64_t> statDropped{0};
// Choke-window position (datagrams withheld since the last release). Network-thread only.
inline std::atomic<std::uint32_t> windowCounter{0};
inline std::atomic<std::uint64_t> statDuped{0};
inline std::atomic<std::uint64_t> statFlooded{0};
inline std::atomic<std::uint64_t> statConnless{0};
inline std::atomic<std::uint64_t> statRaw{0};
inline std::atomic<std::uint64_t> statBlips{0};
inline std::atomic<std::uint64_t> statDelayed{0};
inline std::atomic<std::uint64_t> statFlushed{0};
inline std::atomic<std::uint64_t> statOverflow{0};
}

// Region selector (script-driven, see server_region.lua): a set of relay IPv4s whose datagrams
// are silently swallowed in the hook. Blocking a region's relays makes its SDR pings fail, so
// matchmaking excludes it and only the allowed region gets matched - the same trick the
// firewall-based server pickers use, but in-process, reversible and root-free.
// Reader/writer protocol (the send path must never block): seqlock over the whole list. The
// writer (Lua, rare) bumps the generation odd, rewrites, bumps even. A reader that sees an odd
// or changed generation SKIPS filtering that datagram instead of retrying - a handful of
// datagrams passing through during a list swap is harmless, a stall in the send path is not.
namespace net_region
{
inline constexpr std::size_t kMaxBlockedIps = 512;

inline std::atomic<std::uint64_t> statRegionBlocked{0};
inline std::atomic<std::uint32_t> seqlockGen{0};   // even = stable, odd = being rewritten
inline std::atomic<std::uint32_t> blockedCount{0};
// Host-byte-order IPv4s, sorted ascending at publish time (binary search on the read path).
inline std::uint32_t blockedIps[kMaxBlockedIps];

inline void publishBlockedIps(std::uint32_t* ips, std::size_t count) noexcept
{
    if (count > kMaxBlockedIps)
        count = kMaxBlockedIps;
    // Insertion sort - lists are tiny (a region block is ~200 entries) and rewrites are rare.
    for (std::size_t i = 1; i < count; ++i) {
        const std::uint32_t key = ips[i];
        std::size_t j = i;
        while (j > 0 && ips[j - 1] > key) {
            ips[j] = ips[j - 1];
            --j;
        }
        ips[j] = key;
    }
    seqlockGen.fetch_add(1, std::memory_order_relaxed);   // odd - rewrite in progress
    std::atomic_thread_fence(std::memory_order_seq_cst);
    for (std::size_t i = 0; i < count; ++i)
        blockedIps[i] = ips[i];
    std::atomic_thread_fence(std::memory_order_seq_cst);
    blockedCount.store(static_cast<std::uint32_t>(count), std::memory_order_relaxed);
    // The even bump must be release-ordered (seq_cst): a reader that loads the even generation
    // with acquire then also sees the data + count writes above.
    seqlockGen.fetch_add(1, std::memory_order_seq_cst);   // even - stable again
}

inline void clearBlockedIps() noexcept
{
    seqlockGen.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    blockedCount.store(0, std::memory_order_relaxed);
    seqlockGen.fetch_add(1, std::memory_order_seq_cst);
}

// Binary search over the published list; false on any torn read (see protocol above).
[[nodiscard]] inline bool isBlocked(std::uint32_t ip) noexcept
{
    const std::uint32_t generation = seqlockGen.load(std::memory_order_acquire);
    if (generation & 1)
        return false;
    const std::uint32_t count = blockedCount.load(std::memory_order_relaxed);
    if (count == 0 || count > kMaxBlockedIps)
        return false;
    std::size_t low = 0, high = count;
    while (low < high) {
        const std::size_t mid = (low + high) / 2;
        if (blockedIps[mid] < ip)
            low = mid + 1;
        else
            high = mid;
    }
    const bool found = low < count && blockedIps[low] == ip;
    if (seqlockGen.load(std::memory_order_acquire) != generation)
        return false;   // list rewritten under us - skip filtering this datagram
    return found;
}

// 0 for non-IPv4 destinations (relay data is v4-only; v6 traffic always passes).
[[nodiscard]] inline std::uint32_t destIpv4(const sockaddr* to) noexcept
{
    if (!to || to->sa_family != AF_INET)
        return 0;
    const auto* sin = reinterpret_cast<const sockaddr_in*>(to);
    return ntohl(sin->sin_addr.s_addr);
}
}

// Datagram-level network lag: a GOT/PLT hook over the sendto/sendmsg imports of
// libsteamnetworkingsockets.so. Every datagram the game transmits (community-server UDP, SDR
// relay traffic, voice) leaves through that module's libc imports, so patching its GOT entries
// gives the same NET_SendPacket-level control the Windows reference gets from engine hooks -
// see (drop = choke), duplicate (dup), withhold-and-release-later (delay) and inject
// (zero-size blips).
namespace netlag_hook
{
// NOTE: everything here is deliberately inline (no anonymous namespace) - this header is
// included from BOTH the main TU (EntryPoints/dllmain) and the Lua TU, and the hook state
// (originals, ring, captured endpoint) must be one shared copy per process.

constexpr std::size_t kMaxDatagram = 1400;   // SNS MTU-sized payloads; larger datagrams pass untouched
constexpr std::size_t kRingSlots = 128;
constexpr const char* kStatsPath = "/tmp/ns_netlag_stats.txt";

using SendToFn = ssize_t(*)(int, const void*, std::size_t, int, const sockaddr*, socklen_t);
using SendMsgFn = ssize_t(*)(int, const msghdr*, int);

inline SendToFn originalSendTo = nullptr;
inline SendMsgFn originalSendMsg = nullptr;
inline void** gotSendTo = nullptr;
inline void** gotSendMsg = nullptr;
inline void* savedSendTo = nullptr;
inline void* savedSendMsg = nullptr;
inline bool installed = false;

struct PendingPacket {
    int fd{};
    int flags{};
    sockaddr_storage addr{};
    socklen_t addrLen{};
    std::size_t len{};
    std::uint64_t deadlineMs{};
    unsigned char data[kMaxDatagram]{};
};

inline PendingPacket ring[kRingSlots];
inline bool slotUsed[kRingSlots];
inline std::size_t ringCursor = 0;
inline pthread_mutex_t ringMutex = PTHREAD_MUTEX_INITIALIZER;

// The game server's endpoint, learned from observed traffic: the hook sees every passing game
// datagram's destination, and while connected the consistent one is the server. Guarded by the
// ring mutex (sends are ~100s/sec - lock cost is irrelevant). The flood sends FROM here so the
// server's per-IP limiter counts the address the game itself uses.
struct ServerEndpoint {
    bool valid{};
    int fd{};
    sockaddr_storage addr{};
    socklen_t addrLen{};
};
inline ServerEndpoint serverEndpoint;

inline void captureServerEndpoint(int fd, const sockaddr* to, socklen_t toLen) noexcept
{
    pthread_mutex_lock(&ringMutex);
    serverEndpoint.valid = true;
    serverEndpoint.fd = fd;
    std::memcpy(&serverEndpoint.addr, to, toLen);
    serverEndpoint.addrLen = toLen;
    pthread_mutex_unlock(&ringMutex);
}

// Region filter decision (see net_region above). True = swallow this datagram. The match we are
// already in is never blocked - relays can only be pinned while matchmaking, not enforced
// mid-game - so the captured server endpoint always passes.
[[nodiscard]] inline bool regionShouldBlock(const sockaddr* to) noexcept
{
    const std::uint32_t ip = net_region::destIpv4(to);
    if (ip == 0)
        return false;
    if (!net_region::isBlocked(ip))
        return false;
    pthread_mutex_lock(&ringMutex);
    const ServerEndpoint endpoint = serverEndpoint;
    pthread_mutex_unlock(&ringMutex);
    if (endpoint.valid && endpoint.addr.ss_family == AF_INET) {
        const auto* sin = reinterpret_cast<const sockaddr_in*>(&endpoint.addr);
        if (ntohl(sin->sin_addr.s_addr) == ip)
            return false;
    }
    return true;
}

// Builds connectionless-shaped datagrams (0xFFFFFFFF magic + command byte) and sends them
// straight through the ORIGINAL sendto - bypassing our own GOT hook, so the flood neither
// recurses into the dup/flood logic nor pollutes the traffic counters. Command bytes cycle
// 'q'/'k'/'!' (the three commands the engine2 dispatcher dispatches on).
inline void sendConnlessBurst(std::uint32_t count) noexcept
{
    if (count == 0 || !originalSendTo)
        return;
    pthread_mutex_lock(&ringMutex);
    const ServerEndpoint endpoint = serverEndpoint;
    pthread_mutex_unlock(&ringMutex);
    if (!endpoint.valid)
        return;

    unsigned char packet[12];
    std::memset(packet, 0xFF, 4);
    static constexpr char kCommands[] = {'q', 'k', '!'};
    for (std::uint32_t i = 0; i < count; ++i) {
        packet[4] = kCommands[i % (sizeof(kCommands) / sizeof(kCommands[0]))];
        const ssize_t sent = originalSendTo(endpoint.fd, packet, sizeof(packet), 0,
            reinterpret_cast<const sockaddr*>(&endpoint.addr), endpoint.addrLen);
        if (sent >= 0)
            net_lag::statConnless.fetch_add(1, std::memory_order_relaxed);
    }
}

// ---- script-facing bridge (the Lua `net` library rides on these two) ----

// Copy of the captured game-server endpoint for callers outside the hook.
[[nodiscard]] inline bool getServerEndpoint(int* outFd, sockaddr_storage* outAddr, socklen_t* outAddrLen) noexcept
{
    pthread_mutex_lock(&ringMutex);
    const bool valid = serverEndpoint.valid;
    if (valid) {
        *outFd = serverEndpoint.fd;
        *outAddr = serverEndpoint.addr;
        *outAddrLen = serverEndpoint.addrLen;
    }
    pthread_mutex_unlock(&ringMutex);
    return valid;
}

// Sends `count` copies of an arbitrary payload to the captured game-server endpoint through the
// ORIGINAL sendto (bypasses our GOT hook: no recursion, no dup/flood amplification, no statSends
// pollution). Hard caps: 256 packets per call, 1400-byte payload - a script can therefore move
// at most 64 ticks * 256 * 1400B/sec, bounded by its own tick callback. Returns packets sent.
inline int sendRawToServer(const unsigned char* data, std::size_t len, std::uint32_t count) noexcept
{
    if (!data || len == 0 || len > kMaxDatagram || count == 0)
        return 0;
    if (count > 256)
        count = 256;
    if (!originalSendTo || !installed)
        return 0;
    int fd = -1;
    sockaddr_storage addr{};
    socklen_t addrLen = 0;
    if (!getServerEndpoint(&fd, &addr, &addrLen))
        return 0;

    int sent = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (originalSendTo(fd, data, len, 0, reinterpret_cast<const sockaddr*>(&addr), addrLen) >= 0)
            ++sent;
    }
    net_lag::statRaw.fetch_add(sent, std::memory_order_relaxed);
    return sent;
}

[[nodiscard]] inline std::uint64_t monotonicMs() noexcept
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000 + static_cast<std::uint64_t>(ts.tv_nsec) / 1000000;
}

// Only touch UDP-family destinations with a remote address - that is the game's server traffic.
// Connected-socket sends, AF_UNIX control traffic and oversize payloads pass straight through.
[[nodiscard]] inline bool isGameDatagram(const void* addr, std::size_t len) noexcept
{
    if (!addr || len == 0 || len > kMaxDatagram)
        return false;
    const auto family = static_cast<const sockaddr*>(addr)->sa_family;
    return family == AF_INET || family == AF_INET6;
}

inline void writeStats() noexcept
{
    char line[384];
    std::snprintf(line, sizeof(line),
        "sends=%llu passed=%llu dropped=%llu duped=%llu flooded=%llu connless=%llu raw=%llu blips=%llu delayed=%llu flushed=%llu overflow=%llu regionblocked=%llu engaged=%d\n",
        static_cast<unsigned long long>(net_lag::statSends.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(net_lag::statPassed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(net_lag::statDropped.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(net_lag::statDuped.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(net_lag::statFlooded.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(net_lag::statConnless.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(net_lag::statRaw.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(net_lag::statBlips.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(net_lag::statDelayed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(net_lag::statFlushed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(net_lag::statOverflow.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(net_region::statRegionBlocked.load(std::memory_order_relaxed)),
        net_lag::chokeEngaged.load(std::memory_order_relaxed) ? 1 : 0);
    if (const int fd = LinuxPlatformApi::open(kStatsPath, O_WRONLY | O_CREAT | O_TRUNC, 0644); fd >= 0) {
        static_cast<void>(LinuxPlatformApi::write(fd, line, std::strlen(line)));
        static_cast<void>(LinuxPlatformApi::close(fd));
    }
}

// Releases every buffered datagram whose delay has elapsed (or all of them when flushAll).
// Sends go through the saved originals, never back through the GOT, so no re-entry.
inline void flushDue(bool flushAll) noexcept
{
    const std::uint64_t now = monotonicMs();
    pthread_mutex_lock(&ringMutex);
    for (std::size_t i = 0; i < kRingSlots; ++i) {
        if (!slotUsed[i])
            continue;
        if (!flushAll && ring[i].deadlineMs > now)
            continue;
        if (originalSendTo)
            static_cast<void>(originalSendTo(ring[i].fd, ring[i].data, ring[i].len, ring[i].flags,
                reinterpret_cast<const sockaddr*>(&ring[i].addr), ring[i].addrLen));
        slotUsed[i] = false;
        net_lag::statFlushed.fetch_add(1, std::memory_order_relaxed);
    }
    pthread_mutex_unlock(&ringMutex);
}

[[nodiscard]] inline bool enqueueDatagram(int fd, const unsigned char* data, std::size_t len, int flags,
    const sockaddr* to, socklen_t toLen) noexcept
{
    const std::uint64_t deadline = monotonicMs() + net_lag::delayMs.load(std::memory_order_relaxed);
    pthread_mutex_lock(&ringMutex);
    std::size_t slot = kRingSlots;
    for (std::size_t i = 0; i < kRingSlots; ++i) {
        const std::size_t index = (ringCursor + i) % kRingSlots;
        if (!slotUsed[index]) {
            slot = index;
            break;
        }
    }
    if (slot == kRingSlots) {
        // Full: sacrifice the oldest (cursor position) - the count of sacrifices is part of the
        // experiment's data, not an error to hide.
        slot = ringCursor;
        net_lag::statOverflow.fetch_add(1, std::memory_order_relaxed);
    }
    ringCursor = (slot + 1) % kRingSlots;

    PendingPacket& packet = ring[slot];
    packet.fd = fd;
    packet.flags = flags;
    std::memcpy(&packet.addr, to, toLen);
    packet.addrLen = toLen;
    packet.len = len;
    packet.deadlineMs = deadline;
    std::memcpy(packet.data, data, len);
    slotUsed[slot] = true;
    pthread_mutex_unlock(&ringMutex);
    net_lag::statDelayed.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// Choke window: drop while fewer than `window` datagrams were withheld since the last release.
// Window state is only ever touched through fetch_add, so concurrent network threads degrade to
// an approximate window instead of corrupting it. The call that completes the window releases
// the blips (zero-length keep-alive datagrams, the Harpoon trick) and passes.
[[nodiscard]] inline bool shouldDrop() noexcept
{
    const auto window = net_lag::chokeWindow.load(std::memory_order_relaxed);
    if (window == 0)
        return false;
    const auto previous = net_lag::windowCounter.fetch_add(1, std::memory_order_relaxed);
    if (previous + 1 < window)
        return true;
    net_lag::windowCounter.store(0, std::memory_order_relaxed);   // window completed; caller releases blips
    return false;
}

inline void sendBlips(int fd, const sockaddr* to, socklen_t toLen, int flags) noexcept
{
    const auto count = net_lag::blipCount.load(std::memory_order_relaxed);
    if (count == 0 || !originalSendTo)
        return;
    static const char dummy = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        static_cast<void>(originalSendTo(fd, &dummy, 0, flags, to, toLen));
    }
    net_lag::statBlips.fetch_add(count, std::memory_order_relaxed);
}

inline void sendDuplicates(int fd, const void* buf, std::size_t len, int flags, const sockaddr* to, socklen_t toLen) noexcept
{
    const auto extra = net_lag::dupCount.load(std::memory_order_relaxed);
    if (extra == 0 || !originalSendTo)
        return;
    for (std::uint32_t i = 0; i < extra; ++i) {
        static_cast<void>(originalSendTo(fd, buf, len, flags, to, toLen));
    }
    net_lag::statDuped.fetch_add(extra, std::memory_order_relaxed);
}

// Flood burst (the MMCrasher DupPercent equivalent): extra copies of every PASSING game
// datagram. Key-gated or continuous via floodEngaged; volume set by floodCount. Valid-duplicate
// flooding is what the engine2 per-source rate limiter, the SNS RateLimit_Recv_* config and the
// CQ command queue all classify - that classification is the experiment.
inline void sendFloodBurst(int fd, const void* buf, std::size_t len, int flags, const sockaddr* to, socklen_t toLen) noexcept
{
    const auto count = net_lag::floodCount.load(std::memory_order_relaxed);
    if (count == 0 || !net_lag::floodEngaged.load(std::memory_order_relaxed) || !originalSendTo)
        return;
    for (std::uint32_t i = 0; i < count; ++i) {
        static_cast<void>(originalSendTo(fd, buf, len, flags, to, toLen));
    }
    net_lag::statFlooded.fetch_add(count, std::memory_order_relaxed);
}

inline ssize_t hookSendTo(int fd, const void* buf, std::size_t len, int flags, const sockaddr* to, socklen_t toLen) noexcept
{
    net_lag::statSends.fetch_add(1, std::memory_order_relaxed);
    flushDue(false);

    const bool udpGame = isGameDatagram(to, len);

    // Region filter first: blocked-relay datagrams vanish before any lag logic sees them.
    if (udpGame && regionShouldBlock(to)) {
        net_region::statRegionBlocked.fetch_add(1, std::memory_order_relaxed);
        return static_cast<ssize_t>(len);   // silently swallowed - the game reads this as a dead relay
    }

    const bool engaged = net_lag::chokeEngaged.load(std::memory_order_relaxed)
        || net_lag::delayEnabled.load(std::memory_order_relaxed);
    const bool gameTraffic = engaged && udpGame;

    if (gameTraffic && net_lag::chokeEngaged.load(std::memory_order_relaxed)) {
        if (shouldDrop()) {
            net_lag::statDropped.fetch_add(1, std::memory_order_relaxed);
            return static_cast<ssize_t>(len);   // withheld; the game believes it was sent
        }
        sendBlips(fd, to, toLen, flags);        // window completed with this datagram
    }

    if (gameTraffic && net_lag::delayEnabled.load(std::memory_order_relaxed)) {
        static_cast<void>(enqueueDatagram(fd, static_cast<const unsigned char*>(buf), len, flags, to, toLen));
        return static_cast<ssize_t>(len);
    }

    net_lag::statPassed.fetch_add(1, std::memory_order_relaxed);
    if (!originalSendTo)
        return -1;
    const ssize_t result = originalSendTo(fd, buf, len, flags, to, toLen);
    if (result > 0 && udpGame) {
        if (len > 4)
            captureServerEndpoint(fd, to, toLen);
        if (net_lag::masterEnabled.load(std::memory_order_relaxed)) {
            sendDuplicates(fd, buf, len, flags, to, toLen);
            sendFloodBurst(fd, buf, len, flags, to, toLen);
        }
    }
    return result;
}

inline ssize_t hookSendMsg(int fd, const msghdr* msg, int flags) noexcept
{
    net_lag::statSends.fetch_add(1, std::memory_order_relaxed);
    flushDue(false);

    // Only the simple UDP shape (remote address, single iovec, no ancillary data) is handled;
    // anything else passes through untouched rather than being mis-reconstructed.
    if (msg && msg->msg_name && msg->msg_iov && msg->msg_iovlen >= 1 && !msg->msg_control
        && isGameDatagram(msg->msg_name, msg->msg_iov[0].iov_len)) {
        if (regionShouldBlock(static_cast<const sockaddr*>(msg->msg_name))) {
            net_region::statRegionBlocked.fetch_add(1, std::memory_order_relaxed);
            return static_cast<ssize_t>(msg->msg_iov[0].iov_len);
        }
        const bool engaged = net_lag::chokeEngaged.load(std::memory_order_relaxed)
            || net_lag::delayEnabled.load(std::memory_order_relaxed);
        if (engaged) {
            if (net_lag::chokeEngaged.load(std::memory_order_relaxed)) {
                if (shouldDrop())
                    return static_cast<ssize_t>(msg->msg_iov[0].iov_len);
                sendBlips(fd, static_cast<const sockaddr*>(msg->msg_name), msg->msg_namelen, flags);
            }
            if (net_lag::delayEnabled.load(std::memory_order_relaxed)) {
                static_cast<void>(enqueueDatagram(fd, static_cast<const unsigned char*>(msg->msg_iov[0].iov_base),
                    msg->msg_iov[0].iov_len, flags, static_cast<const sockaddr*>(msg->msg_name), msg->msg_namelen));
                return static_cast<ssize_t>(msg->msg_iov[0].iov_len);
            }
            net_lag::statPassed.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (!originalSendMsg)
        return -1;
    // Dup/flood on the simple-UDP shape, mirroring hookSendTo.
    if (msg && msg->msg_name && msg->msg_iov && msg->msg_iovlen >= 1 && !msg->msg_control
        && isGameDatagram(msg->msg_name, msg->msg_iov[0].iov_len)
        && net_lag::masterEnabled.load(std::memory_order_relaxed)) {
        const ssize_t sent = originalSendMsg(fd, msg, flags);
        if (sent > 0) {
            sendDuplicates(fd, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len, flags,
                static_cast<const sockaddr*>(msg->msg_name), msg->msg_namelen);
            sendFloodBurst(fd, msg->msg_iov[0].iov_base, msg->msg_iov[0].iov_len, flags,
                static_cast<const sockaddr*>(msg->msg_name), msg->msg_namelen);
        }
        return sent;
    }
    return originalSendMsg(fd, msg, flags);
}

// Walks the module's in-memory dynamic section and resolves its sendto/sendmsg relocation slots.
// Works for both lazy PLT (R_X86_64_JUMP_SLOT in DT_JMPREL) and -fno-plt builds
// (R_X86_64_GLOB_DAT in DT_RELA). d_ptr tags are already biased to absolute addresses by
// glibc on x86-64, but the heuristic below also accepts relative values just in case.
[[nodiscard]] inline bool findGotSlots(std::uintptr_t base, void*** outSendTo, void*** outSendMsg) noexcept
{
    const auto* ehdr = reinterpret_cast<const ElfW(Ehdr)*>(base);
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0)
        return false;

    const ElfW(Dyn)* dynamic = nullptr;
    for (unsigned i = 0; i < ehdr->e_phnum; ++i) {
        const auto* phdr = reinterpret_cast<const ElfW(Phdr)*>(base + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type == PT_DYNAMIC) {
            dynamic = reinterpret_cast<const ElfW(Dyn)*>(base + phdr->p_vaddr);
            break;
        }
    }
    if (!dynamic)
        return false;

    std::uintptr_t jmpRel = 0, jmpRelSize = 0, rela = 0, relaSize = 0, symTab = 0, strTab = 0;
    for (const ElfW(Dyn)* dyn = dynamic; dyn->d_tag != DT_NULL; ++dyn) {
        const auto address = [&dyn, base]() noexcept {
            std::uintptr_t value = static_cast<std::uintptr_t>(dyn->d_un.d_ptr);
            if (value < base)
                value += base;
            return value;
        };
        switch (dyn->d_tag) {
        case DT_JMPREL: jmpRel = address(); break;
        case DT_PLTRELSZ: jmpRelSize = dyn->d_un.d_val; break;
        case DT_RELA: rela = address(); break;
        case DT_RELASZ: relaSize = dyn->d_un.d_val; break;
        case DT_SYMTAB: symTab = address(); break;
        case DT_STRTAB: strTab = address(); break;
        default: break;
        }
    }
    if (!symTab || !strTab)
        return false;

    const auto matchReloc = [&symTab, &strTab, base, outSendTo, outSendMsg](std::uintptr_t table, std::size_t size) noexcept {
        for (std::size_t offset = 0; offset + sizeof(ElfW(Rela)) <= size; offset += sizeof(ElfW(Rela))) {
            const auto* entry = reinterpret_cast<const ElfW(Rela)*>(table + offset);
            const auto type = ELF64_R_TYPE(entry->r_info);
            if (type != R_X86_64_JUMP_SLOT && type != R_X86_64_GLOB_DAT)
                continue;
            const auto* symbol = reinterpret_cast<const ElfW(Sym)*>(symTab + ELF64_R_SYM(entry->r_info) * sizeof(ElfW(Sym)));
            const char* name = reinterpret_cast<const char*>(strTab + symbol->st_name);
            if (std::strcmp(name, "sendto") == 0)
                *outSendTo = reinterpret_cast<void**>(base + entry->r_offset);
            else if (std::strcmp(name, "sendmsg") == 0)
                *outSendMsg = reinterpret_cast<void**>(base + entry->r_offset);
        }
    };

    if (jmpRel && jmpRelSize)
        matchReloc(jmpRel, jmpRelSize);
    if (rela && relaSize)
        matchReloc(rela, relaSize);
    return *outSendTo && *outSendMsg;
}

inline void writeSlot(void** slot, void* value) noexcept
{
    constexpr std::uintptr_t kPageMask = 0xFFF;
    const auto address = reinterpret_cast<std::uintptr_t>(slot);
    auto* pageStart = reinterpret_cast<void*>(address & ~kPageMask);
    static_cast<void>(LinuxPlatformApi::mprotect(pageStart, 0x1000, PROT_READ | PROT_WRITE));
    *slot = value;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    static_cast<void>(LinuxPlatformApi::mprotect(pageStart, 0x1000, PROT_READ));
}

// Idempotent. Resolves the originals through dlsym (never through the pre-patch GOT value,
// which can still be a lazy-binding stub), then redirects the module's slots to our handlers.
[[nodiscard]] inline bool install() noexcept
{
    if (installed)
        return true;

    const LinuxDynamicLibrary library{"libsteamnetworkingsockets.so"};
    if (!library)
        return false;
    const link_map* map = library.getLinkMap();
    if (!map || map->l_addr == 0)
        return false;

    if (!originalSendTo)
        originalSendTo = reinterpret_cast<SendToFn>(LinuxPlatformApi::dlsym(reinterpret_cast<void*>(RTLD_DEFAULT), "sendto"));
    if (!originalSendMsg)
        originalSendMsg = reinterpret_cast<SendMsgFn>(LinuxPlatformApi::dlsym(reinterpret_cast<void*>(RTLD_DEFAULT), "sendmsg"));
    if (!originalSendTo || !originalSendMsg)
        return false;

    if (!findGotSlots(map->l_addr, &gotSendTo, &gotSendMsg))
        return false;

    savedSendTo = *gotSendTo;
    savedSendMsg = *gotSendMsg;
    writeSlot(gotSendTo, reinterpret_cast<void*>(&hookSendTo));
    writeSlot(gotSendMsg, reinterpret_cast<void*>(&hookSendMsg));
    installed = true;
    return true;
}

// Restores the GOT, releases everything still buffered and writes a final stats snapshot.
// In-flight calls that already read the handler stay safe: they only touch the originals,
// which remain valid.
inline void unload() noexcept
{
    if (!installed)
        return;
    writeSlot(gotSendTo, savedSendTo);
    writeSlot(gotSendMsg, savedSendMsg);
    installed = false;
    net_lag::chokeEngaged.store(false, std::memory_order_relaxed);
    net_lag::delayEnabled.store(false, std::memory_order_relaxed);
    net_lag::floodEngaged.store(false, std::memory_order_relaxed);
    net_lag::connlessFloodEngaged.store(false, std::memory_order_relaxed);
    net_region::clearBlockedIps();   // a script-owned list must not survive the unload
    pthread_mutex_lock(&ringMutex);
    serverEndpoint.valid = false;
    pthread_mutex_unlock(&ringMutex);
    flushDue(true);
    writeStats();
}

} // namespace netlag_hook

// Game-thread side: publishes the config to the network thread and drains the delay queue.
// SDL key state is not thread-safe, so the hold-to-choke bind is polled HERE (CreateMove) and
// only its result crosses to the network thread.
template <typename HookContext>
class NetLag {
public:
    explicit NetLag(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        netlag_hook::flushDue(false);

        const bool enabled = GET_CONFIG_VAR(net_lag_vars::Enabled);
        const bool always = GET_CONFIG_VAR(net_lag_vars::FakelagAlways);
        const int bindValue = GET_CONFIG_VAR(net_lag_vars::ChokeKeyBind);
        const bool held = bindValue > Bind::kOff && bindValue <= Bind::kLast && Bind::isDown(bindValue);

        net_lag::chokeEngaged.store(enabled && (always || held), std::memory_order_relaxed);
        net_lag::masterEnabled.store(enabled, std::memory_order_relaxed);
        net_lag::chokeWindow.store(GET_CONFIG_VAR(net_lag_vars::ChokeTicks), std::memory_order_relaxed);
        net_lag::blipCount.store(GET_CONFIG_VAR(net_lag_vars::BlipCount), std::memory_order_relaxed);
        net_lag::dupCount.store(GET_CONFIG_VAR(net_lag_vars::DupCount), std::memory_order_relaxed);
        net_lag::delayEnabled.store(enabled && GET_CONFIG_VAR(net_lag_vars::DelayEnabled), std::memory_order_relaxed);
        net_lag::delayMs.store(GET_CONFIG_VAR(net_lag_vars::DelayMs), std::memory_order_relaxed);

        const int floodKey = GET_CONFIG_VAR(net_lag_vars::FloodKeyBind);
        const bool floodHeld = floodKey > Bind::kOff && floodKey <= Bind::kLast && Bind::isDown(floodKey);
        const auto floodBurst = GET_CONFIG_VAR(net_lag_vars::FloodBurstCount);
        net_lag::floodCount.store(floodBurst, std::memory_order_relaxed);
        net_lag::floodEngaged.store(enabled && floodBurst > 0 && (floodKey == Bind::kOff || floodHeld), std::memory_order_relaxed);

        // Connectionless flood: sent from THIS thread (the game's own) straight through the
        // original sendto - the server's per-IP limiter then counts the address the game itself
        // uses, exactly like a real query flood would.
        const int connlessKey = GET_CONFIG_VAR(net_lag_vars::ConnlessKeyBind);
        const bool connlessHeld = connlessKey > Bind::kOff && connlessKey <= Bind::kLast && Bind::isDown(connlessKey);
        const auto connlessBurst = GET_CONFIG_VAR(net_lag_vars::ConnlessFloodCount);
        const bool connlessFire = enabled && connlessBurst > 0 && (connlessKey == Bind::kOff || connlessHeld);
        net_lag::connlessFloodCount.store(connlessBurst, std::memory_order_relaxed);
        net_lag::connlessFloodEngaged.store(connlessFire, std::memory_order_relaxed);
        if (connlessFire)
            netlag_hook::sendConnlessBurst(connlessBurst);

        const bool stats = GET_CONFIG_VAR(net_lag_vars::StatsEnabled);
        const bool wasStats = lastStatsEnabled;
        lastStatsEnabled = stats;
        net_lag::statsEnabled.store(stats, std::memory_order_relaxed);
        if (stats && (!wasStats || (net_lag::statSends.load(std::memory_order_relaxed) & 0x3F) == 0))
            netlag_hook::writeStats();
    }

private:
    inline static bool lastStatsEnabled{false};

    HookContext& hookContext;
};
