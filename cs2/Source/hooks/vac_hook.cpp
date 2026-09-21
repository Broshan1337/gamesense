#include "vac_hook.h"
#include "integrity_audit.h"
#include "vac_str.h"

#include <UI/ImGui/GuiLog.h>

#include <atomic>
#include <cstddef>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include <elf.h>

namespace fva::hooks
{

namespace
{

using FopenFn = FILE*(*)(const char*, const char*);
using FgetsFn = char*(*)(char*, int, FILE*);
using OpenFn = int(*)(const char*, int, ...);
using OpenatFn = int(*)(int, const char*, int, ...);
using ReadFn = ssize_t(*)(int, void*, size_t);
using CloseFn = int(*)(int);
using FcloseFn = int(*)(FILE*);
using DlIterateFn = int(*)(int (*)(struct dl_phdr_info*, size_t, void*), void*);

FopenFn g_orig_fopen = nullptr;
FgetsFn g_orig_fgets = nullptr;
OpenFn g_orig_open = nullptr;
OpenatFn g_orig_openat = nullptr;
ReadFn g_orig_read = nullptr;
CloseFn g_orig_close = nullptr;
FcloseFn g_orig_fclose = nullptr;
DlIterateFn g_orig_dl_iterate = nullptr;

struct GotBackup {
    void** entry;
    void* original;
    int prot;
    bool valid;
};

GotBackup g_fopen_backup;
GotBackup g_fgets_backup;
GotBackup g_open_backup;
GotBackup g_openat_backup;
GotBackup g_read_backup;
GotBackup g_close_backup;
GotBackup g_fclose_backup;
GotBackup g_dl_iterate_backup;

bool g_initialized = false;
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

// Every maps line that must never reach steamclient. The memfd alias and the
// GDB-fallback copy share no single substring with the on-disk name, hence a
// token list instead of one needle:
//   libMangoHud.so    - on-disk name AND the memfd name (inject_memfd creates
//                       the memfd as "libMangoHud.so", so its maps line reads
//                       "/memfd:libMangoHud.so (deleted)")
//   memfd:libMangoHud - belt and suspenders if the memfd name ever changes
//   .fc-cache-        - inject.sh GDB fallback copies the lib to
//                       /tmp/.font-unix/.fc-cache-<ts>, whose maps line
//                       contains neither of the above
//
// Tokens live XOR-encrypted (vac_str.h): the module's own name must not sit
// in .rodata as a scannable byte string.
namespace
{
VAC_XSTR(kTokModule, "libMangoHud.so");
VAC_XSTR(kTokMemfd, "memfd:libMangoHud");
VAC_XSTR(kTokFallback, ".fc-cache-");

VAC_XSTR(kLogSpoofed, "[vac] spoofed file open (%s)");
VAC_XSTR(kLogMapsOpen, "[vac] maps open intercepted (%s)");
VAC_XSTR(kLogMapsOpenFd, "[vac] maps open intercepted (fd)");
VAC_XSTR(kLogFiltered, "[vac] filtered our module from maps output");
VAC_XSTR(kLogInstalled, "[vac] hooks installed (steamclient GOT)");
} // namespace

// gui_log::write carries no __attribute__((format)), so a decrypted runtime
// format string keeps -Wformat-security quiet while staying out of .rodata.
template <std::size_t N>
static void vac_log_fmt(const vac_str::Encrypted<N>& fmt)
{
    char plain[128];
    static_assert(N <= sizeof(plain), "vac log line too long");
    fmt.decrypt(plain);
    gui_log::write(plain);
}

template <std::size_t N>
static void vac_log_fmt_arg(const vac_str::Encrypted<N>& fmt, const char* arg)
{
    char plain[128];
    static_assert(N <= sizeof(plain), "vac log line too long");
    fmt.decrypt(plain);
    gui_log::write(plain, arg);
}

struct MapsTracker {
    FILE* fp;
    int fd;
    bool active;
    // Read-hook state. `clean` holds judged-clean bytes awaiting delivery
    // (short returns are legal for read(); the caller just calls again).
    // `part` holds the current incomplete line fragment. Invariant: part
    // never contains '\n' - a line is judged exactly once, whole.
    char clean[4352];
    std::size_t clean_len;
    std::size_t clean_pos;
    char part[4352];
    std::size_t part_len;
    // Fgets-hook state. `line` accumulates fragments of the current line
    // across small-buffer fgets calls; `pend` holds judged-clean output
    // served back in size-1 slices.
    char line[4352];
    std::size_t line_len;
    char pend[4352];
    std::size_t pend_len;
    std::size_t pend_pos;
};

// One judged line never exceeds these; anything longer is not maps-shaped
// and falls back to atomic judging (documented at each site).
constexpr std::size_t kCleanCap = sizeof(MapsTracker::clean);
constexpr std::size_t kPartCap = sizeof(MapsTracker::part);
constexpr std::size_t kLineCap = sizeof(MapsTracker::line);
constexpr std::size_t kPendCap = sizeof(MapsTracker::pend);

constexpr int kMaxMapsFiles = 8;
MapsTracker g_maps_files[kMaxMapsFiles];
int g_maps_count = 0;
pthread_mutex_t g_maps_mutex = PTHREAD_MUTEX_INITIALIZER;

// FILE* wrappers around spoofed cheat-file fds (created via fdopen in the
// fopen hook). Tracked so the fclose hook can untrack the fd it implicitly
// closes. Sized to integrity's kMaxFakeFiles.
struct SpoofFp {
    FILE* fp;
    int fd;
};
SpoofFp g_spoof_fps[4];

// Observability: VAC snooping us is an anomaly worth exactly one log line per
// session per event (the log contract is anomaly-only). Counters stay
// queryable via vac_hook_stats() for the in-game status page.
unsigned long g_stat_maps_opens = 0;
unsigned long g_stat_lines_filtered = 0;
unsigned long g_stat_spoofs = 0;
bool g_logged_first_open = false;
bool g_logged_first_filter = false;
bool g_logged_first_spoof = false;

static bool token_in_line(const char* token, std::size_t tlen, const char* data, std::size_t len) noexcept
{
    if (tlen == 0 || tlen > len)
        return false;
    for (std::size_t i = 0; i + tlen <= len; ++i) {
        if (::memcmp(data + i, token, tlen) == 0)
            return true;
    }
    return false;
}

static bool line_is_hidden(const char* data, std::size_t len) noexcept
{
    // Tokens are decrypted into stack buffers: the plaintext module name
    // exists only transiently, never in .rodata.
    char module[32];
    char memfd[32];
    char fallback[32];
    static_assert(decltype(kTokModule)::decrypted_size() <= sizeof(module), "token buffer too small");
    static_assert(decltype(kTokMemfd)::decrypted_size() <= sizeof(memfd), "token buffer too small");
    static_assert(decltype(kTokFallback)::decrypted_size() <= sizeof(fallback), "token buffer too small");
    kTokModule.decrypt(module);
    kTokMemfd.decrypt(memfd);
    kTokFallback.decrypt(fallback);
    return token_in_line(module, ::strlen(module), data, len) ||
           token_in_line(memfd, ::strlen(memfd), data, len) ||
           token_in_line(fallback, ::strlen(fallback), data, len);
}

static bool is_maps_path(const char* path) noexcept
{
    return path && (::strstr(path, "/proc/self/maps") ||
                    ::strstr(path, "/proc/self/task/") ||
                    ::strstr(path, "/proc/self/smaps") ||
                    ::strstr(path, "/proc/self/mem"));
}

// All slot helpers require g_maps_mutex held.
static int alloc_maps_slot() noexcept
{
    if (g_maps_count >= kMaxMapsFiles)
        return -1;
    for (int i = 0; i < kMaxMapsFiles; ++i) {
        if (!g_maps_files[i].active) {
            g_maps_files[i].active = true;
            g_maps_files[i].fp = nullptr;
            g_maps_files[i].fd = -1;
            g_maps_files[i].clean_len = 0;
            g_maps_files[i].clean_pos = 0;
            g_maps_files[i].part_len = 0;
            g_maps_files[i].line_len = 0;
            g_maps_files[i].pend_len = 0;
            g_maps_files[i].pend_pos = 0;
            ++g_maps_count;
            return i;
        }
    }
    return -1;
}

static void free_maps_slot(int idx) noexcept
{
    if (idx < 0 || idx >= kMaxMapsFiles || !g_maps_files[idx].active)
        return;
    g_maps_files[idx].active = false;
    g_maps_files[idx].fp = nullptr;
    g_maps_files[idx].fd = -1;
    g_maps_files[idx].clean_len = 0;
    g_maps_files[idx].clean_pos = 0;
    g_maps_files[idx].part_len = 0;
    g_maps_files[idx].line_len = 0;
    g_maps_files[idx].pend_len = 0;
    g_maps_files[idx].pend_pos = 0;
    --g_maps_count;
}

static int find_maps_slot_by_fp(FILE* fp) noexcept
{
    for (int i = 0; i < kMaxMapsFiles; ++i) {
        if (g_maps_files[i].active && g_maps_files[i].fp == fp)
            return i;
    }
    return -1;
}

static int find_maps_slot_by_fd(int fd) noexcept
{
    for (int i = 0; i < kMaxMapsFiles; ++i) {
        if (g_maps_files[i].active && g_maps_files[i].fd == fd)
            return i;
    }
    return -1;
}

static void** find_got_entry(void* base, const char* sym_name);

static void** resolve_got_dynamic(const char* sym_name)
{
    struct link_map* lm = nullptr;
    void* handle = dlopen(NULL, RTLD_LAZY);
    if (!handle) return nullptr;

    dlinfo(handle, RTLD_DI_LINKMAP, &lm);
    dlclose(handle);

    for (; lm; lm = lm->l_next) {
        if (strstr(lm->l_name, "steamclient.so")) {
            return find_got_entry((void*)lm->l_addr, sym_name);
        }
    }
    return nullptr;
}

// ld.so may have already adjusted .dynamic d_ptr entries to absolute
// addresses (glibc inside pressure-vessel containers does); other contexts
// leave link-time vaddrs. If the value lands inside the module image, use
// it as-is -- otherwise add the load base.
static uintptr_t ns_adjust_ptr(void* base, uintptr_t v)
{
    const uintptr_t b = (uintptr_t)base;
    if (v >= b && v - b < 0x80000000ULL) return v;
    return b + v;
}

static void** find_got_entry(void* base, const char* sym_name)
{
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)base;
    if (ehdr->e_type != ET_DYN) return nullptr;

    Elf64_Phdr* phdr = (Elf64_Phdr*)((uintptr_t)base + ehdr->e_phoff);
    Elf64_Dyn* dyn = nullptr;

    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dyn = (Elf64_Dyn*)((uintptr_t)base + phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return nullptr;

    Elf64_Sym* symtab = nullptr;
    char* strtab = nullptr;
    const unsigned char* jmprel = nullptr;
    bool use_rela = true;
    size_t pltrelsz = 0;

    for (Elf64_Dyn* d = dyn; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_SYMTAB: symtab = (Elf64_Sym*)ns_adjust_ptr(base, d->d_un.d_ptr); break;
            case DT_STRTAB: strtab = (char*)ns_adjust_ptr(base, d->d_un.d_ptr); break;
            case DT_JMPREL: jmprel = (const unsigned char*)ns_adjust_ptr(base, d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
            case DT_PLTREL: use_rela = (d->d_un.d_val == DT_RELA); break;
        }
    }

    // Walk the PLT relocation table directly -- no DT_HASH/DT_GNU_HASH dependency.
    // steamclient.so dropped DT_HASH in the 2026-09-10 Steam client update, which
    // broke the old hash-chain lookup and sent us into the stale hardcoded-offset
    // fallback: two de-Xed text pages + clobbered code inside steamclient.
    if (!symtab || !strtab || !jmprel || !pltrelsz) return nullptr;

    const size_t entry_size = use_rela ? sizeof(Elf64_Rela) : sizeof(Elf64_Rel);

    for (size_t j = 0; j < pltrelsz; j += entry_size) {
        const void* rel = jmprel + j;
        size_t sym_idx = use_rela
            ? ELF64_R_SYM(((const Elf64_Rela*)rel)->r_info)
            : ELF64_R_SYM(((const Elf64_Rel*)rel)->r_info);
        Elf64_Sym* s = &symtab[sym_idx];
        if (s->st_shndx == SHN_UNDEF && strcmp(strtab + s->st_name, sym_name) == 0) {
            uintptr_t got_offset = use_rela
                ? ((const Elf64_Rela*)rel)->r_offset
                : ((const Elf64_Rel*)rel)->r_offset;
            return (void**)((uintptr_t)base + got_offset);
        }
    }

    return nullptr;
}
} // anonymous namespace

extern "C" {

__attribute__((noinline))
FILE* vac_fopen_hook(const char* path, const char* mode)
{
    if (!g_orig_fopen) return nullptr;

    // Cheat-file opens (VAC hashing our file from disk) get the fake clean
    // ELF on a memfd-backed fd, wrapped with fdopen so fread/fseek/ftell keep
    // working on it. Fail closed: never hand out the real cheat file.
    if (path && ::security::integrity::is_ready() &&
        ::security::integrity::is_cheat_path(path)) {
        const int sfd = ::security::integrity::open_spoofed_file(path, O_RDONLY, 0);
        if (sfd >= 0) {
            FILE* sfp = ::fdopen(sfd, mode ? mode : "r");
            if (sfp) {
                pthread_mutex_lock(&g_maps_mutex);
                for (int i = 0; i < 4; ++i) {
                    if (!g_spoof_fps[i].fp) {
                        g_spoof_fps[i].fp = sfp;
                        g_spoof_fps[i].fd = sfd;
                        break;
                    }
                }
                ++g_stat_spoofs;
                const bool first = !g_logged_first_spoof;
                if (first) g_logged_first_spoof = true;
                pthread_mutex_unlock(&g_maps_mutex);
                if (first) vac_log_fmt_arg(kLogSpoofed, path);
                return sfp;
            }
            ::security::integrity::close_spoofed_file(sfd);
            return nullptr;
        }
        // Cheat path confirmed but no spoof fd (pool exhausted / memfd
        // failed): fail closed rather than handing out the real file.
        return nullptr;
    }

    if (is_maps_path(path)) {
        FILE* fp = g_orig_fopen(path, mode);
        pthread_mutex_lock(&g_maps_mutex);
        if (fp) {
            const int slot = alloc_maps_slot();
            if (slot >= 0)
                g_maps_files[slot].fp = fp;
            ++g_stat_maps_opens;
            const bool first = !g_logged_first_open;
            if (first) g_logged_first_open = true;
            pthread_mutex_unlock(&g_maps_mutex);
            // Proves the GOT hook actually intercepts steamclient's maps
            // enumeration (which nominally goes through __wrap_fopen): if this
            // line never appears, the hook is bypassed and maps hiding is off.
            if (first) vac_log_fmt_arg(kLogMapsOpen, path);
        } else {
            pthread_mutex_unlock(&g_maps_mutex);
        }
        return fp;
    }

    return g_orig_fopen(path, mode);
}

static int track_maps_fd(int fd) noexcept
{
    pthread_mutex_lock(&g_maps_mutex);
    const int slot = alloc_maps_slot();
    if (slot >= 0)
        g_maps_files[slot].fd = fd;
    ++g_stat_maps_opens;
    const bool first = !g_logged_first_open;
    if (first) g_logged_first_open = true;
    pthread_mutex_unlock(&g_maps_mutex);
    if (first) vac_log_fmt(kLogMapsOpenFd);
    return fd;
}

__attribute__((noinline))
int vac_open_hook(const char* path, int flags, ...)
{
    if (!g_orig_open) return -1;

    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    if (::security::integrity::is_ready())
    {
        int spoofed_fd = ::security::integrity::open_spoofed_file(path, flags, mode);
        if (spoofed_fd >= 0) {
            pthread_mutex_lock(&g_maps_mutex);
            ++g_stat_spoofs;
            const bool first = !g_logged_first_spoof;
            if (first) g_logged_first_spoof = true;
            pthread_mutex_unlock(&g_maps_mutex);
            if (first) vac_log_fmt_arg(kLogSpoofed, path ? path : "?");
            return spoofed_fd;
        }
        // Cheat path confirmed but no spoof fd (pool exhausted / memfd
        // failed): fail closed with ENOENT rather than the real file.
        if (path && ::security::integrity::is_cheat_path(path)) {
            errno = ENOENT;
            return -1;
        }
    }

    if (is_maps_path(path)) {
        const int fd = g_orig_open(path, flags, mode);
        if (fd >= 0)
            track_maps_fd(fd);
        return fd;
    }

    return g_orig_open(path, flags, mode);
}

__attribute__((noinline))
int vac_openat_hook(int dirfd, const char* path, int flags, ...)
{
    if (!g_orig_openat) return -1;

    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    // Modern glibc fopen() opens via openat, not open: without this hook the
    // file-spoof above misses every fopen-issued open, and a maps enumeration
    // issued as openat(AT_FDCWD, "/proc/self/maps") would bypass tracking.
    if (::security::integrity::is_ready())
    {
        int spoofed_fd = ::security::integrity::open_spoofed_file(path, flags, mode);
        if (spoofed_fd >= 0) {
            pthread_mutex_lock(&g_maps_mutex);
            ++g_stat_spoofs;
            const bool first = !g_logged_first_spoof;
            if (first) g_logged_first_spoof = true;
            pthread_mutex_unlock(&g_maps_mutex);
            if (first) vac_log_fmt_arg(kLogSpoofed, path ? path : "?");
            return spoofed_fd;
        }
        // Cheat path confirmed but no spoof fd (pool exhausted / memfd
        // failed): fail closed with ENOENT rather than the real file.
        if (path && ::security::integrity::is_cheat_path(path)) {
            errno = ENOENT;
            return -1;
        }
    }

    if (is_maps_path(path)) {
        const int fd = g_orig_openat(dirfd, path, flags, mode);
        if (fd >= 0)
            track_maps_fd(fd);
        return fd;
    }

    return g_orig_openat(dirfd, path, flags, mode);
}

__attribute__((noinline))
char* vac_fgets_hook(char* buf, int size, FILE* fp)
{
    if (!g_orig_fgets) return nullptr;
    if (!buf || size < 2 || !fp) return g_orig_fgets(buf, size, fp);

    // Small-buffer callers split one maps line across many fgets calls; a
    // hide token straddling the split would never match if each fragment
    // were judged alone (observed live with 53-byte reads: leak). So
    // fragments are accumulated until the newline and the whole line is
    // judged exactly once. Clean output is served back in size-1 slices,
    // preserving fgets short-return semantics for small buffers.
    pthread_mutex_lock(&g_maps_mutex);
    int slot = find_maps_slot_by_fp(fp);
    if (slot < 0) {
        pthread_mutex_unlock(&g_maps_mutex);
        return g_orig_fgets(buf, size, fp);
    }

    for (;;) {
        MapsTracker& t = g_maps_files[slot];

        // 1. Serve pending clean output first.
        if (t.pend_pos < t.pend_len) {
            std::size_t n = t.pend_len - t.pend_pos;
            if (n > static_cast<std::size_t>(size - 1))
                n = static_cast<std::size_t>(size - 1);
            ::memcpy(buf, t.pend + t.pend_pos, n);
            buf[n] = '\0';
            t.pend_pos += n;
            if (t.pend_pos == t.pend_len) {
                t.pend_pos = 0;
                t.pend_len = 0;
            }
            pthread_mutex_unlock(&g_maps_mutex);
            return buf;
        }

        // 2. Accumulate one full line, skipping hidden ones entirely.
        t.line_len = 0;
        bool eof = false;
        for (;;) {
            char frag[1024];
            char* r = g_orig_fgets(frag, sizeof(frag), fp);
            if (!r) {
                eof = true;
                break;
            }
            std::size_t n = ::strlen(frag);
            std::size_t room = (t.line_len < kLineCap) ? kLineCap - t.line_len : 0;
            if (n > room) {
                // Pathological >4K line (never maps-shaped): judge the capped
                // head atomically below; the rest is drained as continuation.
                n = room;
                if (n == 0) {
                    t.line_len = kLineCap + 1; // overflow marker
                    break;
                }
            }
            ::memcpy(t.line + t.line_len, frag, n);
            t.line_len += n;
            if (t.line_len > 0 && t.line[t.line_len - 1] == '\n')
                break; // complete line (fgets keeps the newline)
            if (n < sizeof(frag) - 1)
                break; // short fragment without newline: EOF-adjacent tail
        }

        if (t.line_len == 0 && eof) {
            // True EOF, nothing pending: keep EOF semantics, free the slot.
            free_maps_slot(slot);
            pthread_mutex_unlock(&g_maps_mutex);
            return nullptr;
        }

        const bool overlong = t.line_len > kLineCap;
        const std::size_t judge_len = overlong ? kLineCap : t.line_len;
        const bool hidden = judge_len > 0 && line_is_hidden(t.line, judge_len);
        if (hidden) {
            ++g_stat_lines_filtered;
            const bool first = !g_logged_first_filter;
            if (first) g_logged_first_filter = true;
            pthread_mutex_unlock(&g_maps_mutex);
            if (first) vac_log_fmt(kLogFiltered);
            pthread_mutex_lock(&g_maps_mutex);
            if (!g_maps_files[slot].active || g_maps_files[slot].fp != fp) {
                pthread_mutex_unlock(&g_maps_mutex);
                return nullptr; // closed mid-read: fail closed
            }
            if (overlong) {
                // Drain the rest of the over-long hidden line, then continue.
                char drain[1024];
                while (g_orig_fgets(drain, sizeof(drain), fp) &&
                       ::strlen(drain) == sizeof(drain) - 1 &&
                       drain[sizeof(drain) - 2] != '\n') {
                }
            }
            continue; // next line
        }

        if (overlong) {
            // Over-long but clean: serve the capped head now; the remainder
            // stays in the stream and is judged as continuation (documented
            // fallback for non-maps-shaped input only).
            ::memcpy(t.pend, t.line, kLineCap);
            t.pend_len = kLineCap;
            t.pend_pos = 0;
            continue; // loop back to serve from pend
        }

        // Clean line (possibly an EOF tail without newline): stage + serve.
        ::memcpy(t.pend, t.line, t.line_len);
        t.pend_len = t.line_len;
        t.pend_pos = 0;
        // Loop back to the serve step (handles size < line uniformly).
    }
}

__attribute__((noinline))
ssize_t vac_read_hook(int fd, void* buf, size_t count)
{
    if (!g_orig_read) return -1;
    if (!buf || count == 0) return g_orig_read(fd, buf, count);

    // Spoofed cheat-file fds are memfd-backed: the kernel itself serves the
    // fake content, so pass through and keep pread/lseek/mmap coherent
    // (no userspace offset shadow to drift).
    if (::security::integrity::should_spoof_fd(fd))
        return g_orig_read(fd, buf, count);

    // Exact line filtering at ANY caller chunk size: judged-clean bytes wait
    // in `clean` (short returns are legal for read()), while the current
    // incomplete line accumulates in `part` and is judged exactly once,
    // whole. A token split across read() boundaries can therefore never be
    // judged half-seen (observed live with 37-byte reads: leak). 0 is
    // returned only at true EOF - never to paper over an all-hidden chunk.
    pthread_mutex_lock(&g_maps_mutex);
    const int slot0 = find_maps_slot_by_fd(fd);
    if (slot0 < 0) {
        pthread_mutex_unlock(&g_maps_mutex);
        return g_orig_read(fd, buf, count);
    }
    pthread_mutex_unlock(&g_maps_mutex);

    char* out = static_cast<char*>(buf);

    for (;;) {
        pthread_mutex_lock(&g_maps_mutex);
        int slot = find_maps_slot_by_fd(fd);
        if (slot < 0) {
            pthread_mutex_unlock(&g_maps_mutex);
            return 0; // closed mid-read: fail closed, emit nothing
        }
        MapsTracker& t = g_maps_files[slot];

        // 1. Deliver pending clean bytes first.
        if (t.clean_pos < t.clean_len) {
            std::size_t n = t.clean_len - t.clean_pos;
            if (n > count)
                n = count;
            ::memcpy(out, t.clean + t.clean_pos, n);
            t.clean_pos += n;
            if (t.clean_pos == t.clean_len) {
                t.clean_pos = 0;
                t.clean_len = 0;
            }
            pthread_mutex_unlock(&g_maps_mutex);
            return static_cast<ssize_t>(n);
        }

        // 2. Accumulate the current line until newline, EOF, or cap.
        bool eof = false;
        while (t.part_len < kPartCap) {
            // Newline check first: part may already hold one from a big read.
            bool has_nl = false;
            for (std::size_t i = 0; i < t.part_len; ++i) {
                if (t.part[i] == '\n') {
                    has_nl = true;
                    break;
                }
            }
            if (has_nl)
                break;
            ssize_t n;
            do {
                n = g_orig_read(fd, t.part + t.part_len, kPartCap - t.part_len);
            } while (n < 0 && errno == EINTR);
            if (n < 0) {
                pthread_mutex_unlock(&g_maps_mutex);
                return n; // nothing emitted yet: part preserved for retry
            }
            if (n == 0) {
                eof = true;
                break;
            }
            t.part_len += static_cast<std::size_t>(n);
        }

        // 3. Carve one line (through the first newline) or the EOF tail.
        std::size_t line_end = t.part_len;
        bool complete = false;
        for (std::size_t i = 0; i < t.part_len; ++i) {
            if (t.part[i] == '\n') {
                line_end = i + 1;
                complete = true;
                break;
            }
        }
        const bool overlong = !complete && !eof && t.part_len >= kPartCap;

        if (t.part_len == 0 && eof) {
            // True EOF, nothing pending: keep EOF semantics, free the slot.
            free_maps_slot(slot);
            pthread_mutex_unlock(&g_maps_mutex);
            return 0;
        }

        const std::size_t judge_len = overlong ? kPartCap : (complete ? line_end : t.part_len);
        const bool hidden = judge_len > 0 && line_is_hidden(t.part, judge_len);
        if (hidden)
            ++g_stat_lines_filtered;
        const bool first = hidden && !g_logged_first_filter;
        if (first) g_logged_first_filter = true;

        if (!hidden) {
            // Stage the clean line (or capped head) for delivery.
            const std::size_t stage = overlong ? kPartCap : (complete ? line_end : t.part_len);
            ::memcpy(t.clean, t.part, stage);
            t.clean_len = stage;
            t.clean_pos = 0;
        }
        if (complete || overlong || eof) {
            // Consume the judged head; the remainder (next line's head, or
            // an over-long line's continuation) stays for the next round.
            const std::size_t consumed = overlong ? kPartCap : (complete ? line_end : t.part_len);
            const std::size_t rest = t.part_len - consumed;
            if (rest)
                ::memmove(t.part, t.part + consumed, rest);
            t.part_len = rest;
        }
        pthread_mutex_unlock(&g_maps_mutex);
        if (first) vac_log_fmt(kLogFiltered);
        // Loop back: deliver staged clean bytes, or read past a hidden line.
    }
}

__attribute__((noinline))
int vac_close_hook(int fd)
{
    if (!g_orig_close) return -1;

    if (::security::integrity::should_spoof_fd(fd))
    {
        ::security::integrity::close_spoofed_file(fd);
        return 0;
    }

    pthread_mutex_lock(&g_maps_mutex);
    free_maps_slot(find_maps_slot_by_fd(fd));
    pthread_mutex_unlock(&g_maps_mutex);

    return g_orig_close(fd);
}

__attribute__((noinline))
int vac_fclose_hook(FILE* fp)
{
    if (!g_orig_fclose) return -1;
    if (!fp) return g_orig_fclose(fp);

    // Without this, every tracked maps FILE* leaks its slot (fclose never
    // reaches the close hook - it closes via libc internally), and the 9th
    // maps open in a session goes untracked and leaks our module name.
    pthread_mutex_lock(&g_maps_mutex);
    free_maps_slot(find_maps_slot_by_fp(fp));
    int spoof_fd = -1;
    for (int i = 0; i < 4; ++i) {
        if (g_spoof_fps[i].fp == fp) {
            spoof_fd = g_spoof_fps[i].fd;
            g_spoof_fps[i].fp = nullptr;
            g_spoof_fps[i].fd = -1;
            break;
        }
    }
    pthread_mutex_unlock(&g_maps_mutex);

    const int r = g_orig_fclose(fp); // also closes the underlying fd
    if (spoof_fd >= 0)
        ::security::integrity::forget_spoofed_fd(spoof_fd);
    return r;
}

// dl_iterate_phdr filter hook: steamclient's dl_iterate_phdr calls (VAC
// enumerating loaded objects) never see our module - the entry whose base is
// ours is dropped from the callback chain. This replaces the permanent
// link_map splice, which left the loader map/counters desynced and asserted
// the game dead on every audio capture device switch (_dl_close_worker
// 'idx == nloaded', 2026-09-13 x3). The REAL map stays consistent 100% of the
// time: no mutation, no dlcloses walking a broken list, evasion intact.
struct PhdrFilterCtx {
    int (*real_cb)(struct dl_phdr_info*, size_t, void*);
    void* real_data;
    std::uintptr_t self_base;
};

// Self base cached at install time: dladdr() takes the loader lock, and the
// hook runs INSIDE dl_iterate_phdr's own loader-lock window - a dladdr there
// self-deadlocks (observed 2026-09-13: every spawn through Steam's socket
// stalled with "no status report" while the hook was live).
std::uintptr_t g_phdr_self_base = 0;

static int vac_phdr_filter_cb(struct dl_phdr_info* info, size_t size, void* data)
{
    const auto* ctx = static_cast<const PhdrFilterCtx*>(data);
    if (info && static_cast<std::uintptr_t>(info->dlpi_addr) == ctx->self_base)
        return 0; // our entry: swallowed, the walk continues normally
    return ctx->real_cb(info, size, ctx->real_data);
}

int vac_dl_iterate_hook(int (*callback)(struct dl_phdr_info*, size_t, void*), void* data)
{
    if (!g_orig_dl_iterate || g_phdr_self_base == 0)
        return g_orig_dl_iterate ? g_orig_dl_iterate(callback, data) : -1;

    PhdrFilterCtx ctx{callback, data, g_phdr_self_base};
    return g_orig_dl_iterate(&vac_phdr_filter_cb, &ctx);
}

} // extern "C"

namespace
{

static int find_module_base(struct dl_phdr_info* info, size_t, void* data)
{
    if (!info->dlpi_name) return 0;
    if (strstr(info->dlpi_name, "steamclient.so")) {
        *(void**)data = (void*)info->dlpi_addr;
        return 1;
    }
    return 0;
}

static void patch_got(void** got, void* newval, GotBackup* backup)
{
    if (!got) return;

    // Save/restore the page's ACTUAL protection: the old code forced
    // PROT_READ on restore, which permanently de-wrote an rw-p GOT page
    // (later lazy PLT resolutions would SIGSEGV on the write) and merged
    // adjacent VMAs - i.e. visibly changed the very maps layout VAC's
    // range reporter sends home. Per-page restore keeps the layout
    // bit-identical (the harness diffs maps before/after for exactly this).
    //
    // Exactly ONE page is touched: a GOT entry is one 8-byte aligned pointer
    // and can never straddle a page boundary. The old 0x2000 span could run
    // past a mapping end (ENOMEM: mprotect fails wholesale, the write then
    // faults) - observed live when .got ended at a segment boundary.
    // A single page containing a readable address always re-protects, so
    // this cannot fail that way. Page size is 4096: x86_64-only project.
    // NOTE: the mask MUST stay uintptr_t-wide (~(uintptr_t)0xFFF): a 32-bit
    // ~0xFFFu truncates the address to 32 bits (observed: page 0x43034000
    // for a 0x7f.. got -> ENOMEM on every patch -> silent no-op hooks).
    const uintptr_t page = (uintptr_t)got & ~(uintptr_t)0xFFFu;
    int saved = PROT_READ;
    FILE* maps = ::fopen("/proc/self/maps", "r");
    if (maps) {
        char line[512];
        while (::fgets(line, sizeof(line), maps)) {
            unsigned long start = 0, end = 0;
            char perms[8] = {0};
            if (::sscanf(line, "%lx-%lx %7s", &start, &end, perms) < 3)
                continue;
            if (page >= start && page < end) {
                int p = 0;
                if (perms[0] == 'r') p |= PROT_READ;
                if (perms[1] == 'w') p |= PROT_WRITE;
                if (perms[2] == 'x') p |= PROT_EXEC;
                saved = p;
                break;
            }
        }
        ::fclose(maps);
    }

    if (mprotect((void*)page, 0x1000, PROT_READ | PROT_WRITE) != 0)
        return; // fail closed: never patch what we cannot write safely

    backup->entry = got;
    backup->original = *got;
    backup->prot = saved;
    backup->valid = true;
    *got = newval;

    mprotect((void*)page, 0x1000, saved);
}

static void restore_got(GotBackup* backup)
{
    if (!backup->valid || !backup->entry) return;

    const uintptr_t page = (uintptr_t)backup->entry & ~(uintptr_t)0xFFFu;
    if (mprotect((void*)page, 0x1000, PROT_READ | PROT_WRITE) != 0)
        return;
    *backup->entry = backup->original;
    mprotect((void*)page, 0x1000, backup->prot);
    backup->valid = false;
}

// ---- link_map hiding ----
//
// Removes our loader node so in-process enumeration (dl_iterate_phdr,
// dlopen-NOLOAD-by-name service modules) no longer lists the module. The
// mapping itself stays (that layer is filtered separately via maps hooks).
//
// Safety case (audited 2026-09-11):
// - CrashLogger enumerates via /proc/self/maps, not link_map: unaffected.
// - RTLD_DEFAULT lookups target libc symbols: unaffected by our absence.
// - LuaJIT unwinding uses the libgcc frame registry (__register_frame),
//   which is independent of link_map and persists until dlclose: unaffected.
// - l_moduleBase/l_patternScan by OUR name start returning nil: accepted
//   (no shipped script depends on it; keeps scripts from hard-depending).
// - Forward-chain consistency: splice and restore only ever rewrite l_next
//   forward links around an untouched self node, so a concurrent
//   dl_iterate_phdr (forward walk) always sees a valid chain. Same risk
//   posture as the GOT patching.
// - install runs under the loader lock (constructor time); uninstall runs
//   on the menu unload path before SelfUnload's dladdr/RTLD_NOLOAD/dlclose.
link_map* g_lm_self = nullptr;
link_map* g_lm_prev = nullptr;
link_map* g_lm_next = nullptr;
bool g_link_hidden = false;
bool g_deferred_done = false;
std::atomic<bool> g_deferred_attempted{false};

// REVERTED 2026-09-11: production self-hide disabled while a deterministic
// inject-time freeze (log ends at "tryInstall: attempt begin", no fault, no
// core, no crash log) is bisected - it correlated with this feature landing.
// RE-ENABLED deferred 2026-09-11: the splice itself is unchanged, but it now
// runs from apply_deferred_link_hide() on the present path (loader lock long
// released) instead of inside the constructor's dlopen. See header.
//
// DISABLED AGAIN 2026-09-13 (diagnostic): three crashes, all exactly at audio
// capture device switches, all `_dl_close_worker: Assertion 'idx == nloaded'`
// — a loader map/counter desync. Our spliced-out node is the only loader-level
// anomaly in the process, and glibc's _dl_nloaded still counts the node we
// removed from the list. The SteamModule reverted this same feature for the
// same class of bug on 09-11. Test session: if crashes stop with the hide off,
// the hide design must become hide-only-during-dl_iterate_phdr (hook the call,
// splice, delegate, restore) instead of a permanent splice.
constexpr bool kEnableLinkHide = false;

VAC_XSTR(kLogLinkHide, "[vac] link_map node unlinked");
VAC_XSTR(kLogLinkShow, "[vac] link_map node restored");
VAC_XSTR(kLogLinkSkip, "[vac] link_map hide skipped (main executable)");
VAC_XSTR(kLogLinkAttempt, "[vac] link_map hide attempt");
VAC_XSTR(kLogLinkDisabled, "[vac] link_map hide DISABLED (revert)");

static link_map* link_head() noexcept
{
    void* h = ::dlopen(nullptr, RTLD_LAZY);
    if (!h) return nullptr;
    link_map* lm = nullptr;
    dlinfo(h, RTLD_DI_LINKMAP, &lm);
    ::dlclose(h);
    return lm;
}

static bool splice_out(link_map* self, link_map* head) noexcept
{
    if (!self || self == head || !self->l_prev) return false;
    if (self->l_prev->l_next != self) return false;
    if (self->l_next && self->l_next->l_prev != self) return false;
    g_lm_self = self;
    g_lm_prev = self->l_prev;
    g_lm_next = self->l_next;
    self->l_prev->l_next = self->l_next;
    if (self->l_next) self->l_next->l_prev = self->l_prev;
    g_link_hidden = true;
    return true;
}

// Self-locate via our own function address: exact by construction (no other
// module can contain it). Refuses the main executable (head of the chain).
static bool hide_self() noexcept
{
    Dl_info info{};
    if (::dladdr((const void*)&install_vac_hook, &info) == 0 || !info.dli_fbase)
        return false;
    link_map* head = link_head();
    if (!head) return false;
    for (link_map* lm = head; lm; lm = lm->l_next) {
        if ((uintptr_t)lm->l_addr == (uintptr_t)info.dli_fbase)
            return splice_out(lm, head);
    }
    return false;
}

} // anonymous namespace

namespace security::regions
{
void add(void*, size_t) {}
void remove(void*) {}
bool is_protected(void*, size_t) { return false; }
}

namespace security::prologues
{
void save(uintptr_t) {}
}

bool install_vac_hook()
{
    pthread_mutex_lock(&g_mutex);

    if (g_initialized) {
        pthread_mutex_unlock(&g_mutex);
        return true;
    }

    void* steamclient_base = nullptr;
    dl_iterate_phdr(find_module_base, &steamclient_base);

    if (!steamclient_base) {
        pthread_mutex_unlock(&g_mutex);
        return true;
    }

    g_orig_fopen = (FopenFn)dlsym(RTLD_NEXT, "fopen");
    g_orig_fgets = (FgetsFn)dlsym(RTLD_NEXT, "fgets");
    g_orig_open = (OpenFn)dlsym(RTLD_NEXT, "open");
    g_orig_openat = (OpenatFn)dlsym(RTLD_NEXT, "openat");
    g_orig_read = (ReadFn)dlsym(RTLD_NEXT, "read");
    g_orig_close = (CloseFn)dlsym(RTLD_NEXT, "close");
    g_orig_fclose = (FcloseFn)dlsym(RTLD_NEXT, "fclose");
    g_orig_dl_iterate = (DlIterateFn)dlsym(RTLD_NEXT, "dl_iterate_phdr");

    if (!g_orig_fopen || !g_orig_fgets) {
        pthread_mutex_unlock(&g_mutex);
        return false;
    }

    void** fopen_got = resolve_got_dynamic("fopen");
    void** fgets_got = resolve_got_dynamic("fgets");
    void** open_got = g_orig_open ? resolve_got_dynamic("open") : nullptr;
    void** openat_got = g_orig_openat ? resolve_got_dynamic("openat") : nullptr;
    void** read_got = g_orig_read ? resolve_got_dynamic("read") : nullptr;
    void** close_got = g_orig_close ? resolve_got_dynamic("close") : nullptr;
    void** fclose_got = g_orig_fclose ? resolve_got_dynamic("fclose") : nullptr;

    // No hardcoded-offset fallback: after a steamclient.so update those stale
    // offsets land inside .text and patch_got de-Xes 2 text pages + clobbers
    // code (the 2026-09-10 SEGV inside steamclient). If GOT resolution fails,
    // skip the hook -- a failed hide is never a crash.

    patch_got(fopen_got, (void*)vac_fopen_hook, &g_fopen_backup);
    patch_got(fgets_got, (void*)vac_fgets_hook, &g_fgets_backup);

    if (open_got) {
        patch_got(open_got, (void*)vac_open_hook, &g_open_backup);
    }
    if (openat_got) {
        patch_got(openat_got, (void*)vac_openat_hook, &g_openat_backup);
    }
    if (read_got) {
        patch_got(read_got, (void*)vac_read_hook, &g_read_backup);
    }
    if (close_got) {
        patch_got(close_got, (void*)vac_close_hook, &g_close_backup);
    }
    if (fclose_got) {
        patch_got(fclose_got, (void*)vac_fclose_hook, &g_fclose_backup);
    }
    if (g_orig_dl_iterate) {
        // The dl_iterate_phdr filter is the SAFE link-hide: VAC's object
        // enumerations never see us, and the loader map itself is never
        // mutated - dlcloses (audio capture device switches included) walk a
        // fully consistent map. (The permanent splice asserted the game dead
        // three times on 2026-09-13.)
        Dl_info self{};
        if (::dladdr(reinterpret_cast<const void*>(&vac_dl_iterate_hook), &self) != 0 && self.dli_fbase)
            g_phdr_self_base = reinterpret_cast<std::uintptr_t>(self.dli_fbase);
        void** dl_iterate_got = resolve_got_dynamic("dl_iterate_phdr");
        patch_got(dl_iterate_got, (void*)vac_dl_iterate_hook, &g_dl_iterate_backup);
    }

    ::security::integrity::initialize();

    // NOTE: no link_map work here anymore - apply_deferred_link_hide() (Vulkan
    // present path) handles it once the loader lock is released. Splicing
    // inside this constructor froze the game (2026-09-11).
    g_deferred_done = false;
    g_deferred_attempted.store(false, std::memory_order_relaxed);

    g_initialized = true;
    pthread_mutex_unlock(&g_mutex);

    vac_log_fmt(kLogInstalled);
    return true;
}

void uninstall_vac_hook()
{
    pthread_mutex_lock(&g_mutex);

    // First: re-link our node, before anything else. The SelfUnload
    // dladdr/RTLD_NOLOAD/dlclose chain and any later dlopen expect a
    // consistent loader view again.
    const bool was_hidden = g_link_hidden;
    link_restore();

    restore_got(&g_fopen_backup);
    restore_got(&g_fgets_backup);
    restore_got(&g_open_backup);
    restore_got(&g_openat_backup);
    restore_got(&g_read_backup);
    restore_got(&g_close_backup);
    restore_got(&g_fclose_backup);
    restore_got(&g_dl_iterate_backup);

    ::security::integrity::shutdown();

    g_initialized = false;
    pthread_mutex_unlock(&g_mutex);

    if (was_hidden) vac_log_fmt(kLogLinkShow);
}

bool link_hide_by_substr(const char* substr) noexcept
{
    if (!substr || !*substr || g_link_hidden) return false;
    link_map* head = link_head();
    if (!head) return false;
    // Skip head (main executable) by construction: only named DSOs qualify.
    for (link_map* lm = head->l_next; lm; lm = lm->l_next) {
        if (lm->l_name && ::strstr(lm->l_name, substr))
            return splice_out(lm, head);
    }
    return false;
}

void link_restore() noexcept
{
    if (!g_link_hidden || !g_lm_self || !g_lm_prev) return;
    // Only re-link if nobody disturbed the splice point meanwhile; if it
    // moved, stay hidden (fail safe) - unload still works via fname lookup.
    if (g_lm_prev->l_next != g_lm_next) return;
    if (g_lm_next && g_lm_next->l_prev != g_lm_prev) return;
    g_lm_prev->l_next = g_lm_self;
    if (g_lm_next) g_lm_next->l_prev = g_lm_self;
    g_link_hidden = false;
}

bool link_is_hidden() noexcept
{
    return g_link_hidden;
}

// Deferred one-shot, called from hkQueuePresentKHR (see VulkanHook.cpp):
// first present happens long after our dlopen returned, so no loader lock
// is held here - unlike the constructor, where the splice froze the game.
// Cost after the first call: one relaxed atomic load.
void apply_deferred_link_hide() noexcept
{
    if (g_deferred_attempted.load(std::memory_order_relaxed))
        return;
    pthread_mutex_lock(&g_mutex);
    if (!g_initialized || g_deferred_done) {
        pthread_mutex_unlock(&g_mutex);
        return; // not installed (yet): retry on a later present
    }
    g_deferred_done = true;
    g_deferred_attempted.store(true, std::memory_order_relaxed);
    const bool already = g_link_hidden;
    pthread_mutex_unlock(&g_mutex);

    if (already || !kEnableLinkHide)
        return;
    // Attempt/result pair is deliberate debug scaffolding for the 2026-09-11
    // freeze: if the log ever ends at "attempt" again, the splice itself is
    // guilty; if "attempt" never appears, the present path never ran.
    vac_log_fmt(kLogLinkAttempt);
    if (hide_self())
        vac_log_fmt(kLogLinkHide);
    else
        vac_log_fmt(kLogLinkSkip);
}

VacHookStats vac_hook_stats() noexcept
{
    VacHookStats out;
    pthread_mutex_lock(&g_maps_mutex);
    out.maps_opens = g_stat_maps_opens;
    out.lines_filtered = g_stat_lines_filtered;
    out.spoofs = g_stat_spoofs;
    pthread_mutex_unlock(&g_maps_mutex);
    return out;
}

} // namespace fva::hooks

