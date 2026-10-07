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
} 



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
    
    
    
    
    char clean[4352];
    std::size_t clean_len;
    std::size_t clean_pos;
    char part[4352];
    std::size_t part_len;
    
    
    
    char line[4352];
    std::size_t line_len;
    char pend[4352];
    std::size_t pend_len;
    std::size_t pend_pos;
};



constexpr std::size_t kCleanCap = sizeof(MapsTracker::clean);
constexpr std::size_t kPartCap = sizeof(MapsTracker::part);
constexpr std::size_t kLineCap = sizeof(MapsTracker::line);
constexpr std::size_t kPendCap = sizeof(MapsTracker::pend);

constexpr int kMaxMapsFiles = 8;
MapsTracker g_maps_files[kMaxMapsFiles];
int g_maps_count = 0;
pthread_mutex_t g_maps_mutex = PTHREAD_MUTEX_INITIALIZER;




struct SpoofFp {
    FILE* fp;
    int fd;
};
SpoofFp g_spoof_fps[4];




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
} 

extern "C" {

__attribute__((noinline))
FILE* vac_fopen_hook(const char* path, const char* mode)
{
    if (!g_orig_fopen) return nullptr;

    
    
    
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

    
    
    
    
    
    
    pthread_mutex_lock(&g_maps_mutex);
    int slot = find_maps_slot_by_fp(fp);
    if (slot < 0) {
        pthread_mutex_unlock(&g_maps_mutex);
        return g_orig_fgets(buf, size, fp);
    }

    for (;;) {
        MapsTracker& t = g_maps_files[slot];

        
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
                
                
                n = room;
                if (n == 0) {
                    t.line_len = kLineCap + 1; 
                    break;
                }
            }
            ::memcpy(t.line + t.line_len, frag, n);
            t.line_len += n;
            if (t.line_len > 0 && t.line[t.line_len - 1] == '\n')
                break; 
            if (n < sizeof(frag) - 1)
                break; 
        }

        if (t.line_len == 0 && eof) {
            
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
                return nullptr; 
            }
            if (overlong) {
                
                char drain[1024];
                while (g_orig_fgets(drain, sizeof(drain), fp) &&
                       ::strlen(drain) == sizeof(drain) - 1 &&
                       drain[sizeof(drain) - 2] != '\n') {
                }
            }
            continue; 
        }

        if (overlong) {
            
            
            
            ::memcpy(t.pend, t.line, kLineCap);
            t.pend_len = kLineCap;
            t.pend_pos = 0;
            continue; 
        }

        
        ::memcpy(t.pend, t.line, t.line_len);
        t.pend_len = t.line_len;
        t.pend_pos = 0;
        
    }
}

__attribute__((noinline))
ssize_t vac_read_hook(int fd, void* buf, size_t count)
{
    if (!g_orig_read) return -1;
    if (!buf || count == 0) return g_orig_read(fd, buf, count);

    
    
    
    if (::security::integrity::should_spoof_fd(fd))
        return g_orig_read(fd, buf, count);

    
    
    
    
    
    
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
            return 0; 
        }
        MapsTracker& t = g_maps_files[slot];

        
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

        
        bool eof = false;
        while (t.part_len < kPartCap) {
            
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
                return n; 
            }
            if (n == 0) {
                eof = true;
                break;
            }
            t.part_len += static_cast<std::size_t>(n);
        }

        
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
            
            const std::size_t stage = overlong ? kPartCap : (complete ? line_end : t.part_len);
            ::memcpy(t.clean, t.part, stage);
            t.clean_len = stage;
            t.clean_pos = 0;
        }
        if (complete || overlong || eof) {
            
            
            const std::size_t consumed = overlong ? kPartCap : (complete ? line_end : t.part_len);
            const std::size_t rest = t.part_len - consumed;
            if (rest)
                ::memmove(t.part, t.part + consumed, rest);
            t.part_len = rest;
        }
        pthread_mutex_unlock(&g_maps_mutex);
        if (first) vac_log_fmt(kLogFiltered);
        
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

    const int r = g_orig_fclose(fp); 
    if (spoof_fd >= 0)
        ::security::integrity::forget_spoofed_fd(spoof_fd);
    return r;
}








struct PhdrFilterCtx {
    int (*real_cb)(struct dl_phdr_info*, size_t, void*);
    void* real_data;
    std::uintptr_t self_base;
};





std::uintptr_t g_phdr_self_base = 0;

static int vac_phdr_filter_cb(struct dl_phdr_info* info, size_t size, void* data)
{
    const auto* ctx = static_cast<const PhdrFilterCtx*>(data);
    if (info && static_cast<std::uintptr_t>(info->dlpi_addr) == ctx->self_base)
        return 0; 
    return ctx->real_cb(info, size, ctx->real_data);
}

int vac_dl_iterate_hook(int (*callback)(struct dl_phdr_info*, size_t, void*), void* data)
{
    if (!g_orig_dl_iterate || g_phdr_self_base == 0)
        return g_orig_dl_iterate ? g_orig_dl_iterate(callback, data) : -1;

    PhdrFilterCtx ctx{callback, data, g_phdr_self_base};
    return g_orig_dl_iterate(&vac_phdr_filter_cb, &ctx);
}

} 

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
        return; 

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




















link_map* g_lm_self = nullptr;
link_map* g_lm_prev = nullptr;
link_map* g_lm_next = nullptr;
bool g_link_hidden = false;
bool g_deferred_done = false;
std::atomic<bool> g_deferred_attempted{false};
















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

} 

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
        
        
        
        
        
        Dl_info self{};
        if (::dladdr(reinterpret_cast<const void*>(&vac_dl_iterate_hook), &self) != 0 && self.dli_fbase)
            g_phdr_self_base = reinterpret_cast<std::uintptr_t>(self.dli_fbase);
        void** dl_iterate_got = resolve_got_dynamic("dl_iterate_phdr");
        patch_got(dl_iterate_got, (void*)vac_dl_iterate_hook, &g_dl_iterate_backup);
    }

    ::security::integrity::initialize();

    
    
    
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
    
    for (link_map* lm = head->l_next; lm; lm = lm->l_next) {
        if (lm->l_name && ::strstr(lm->l_name, substr))
            return splice_out(lm, head);
    }
    return false;
}

void link_restore() noexcept
{
    if (!g_link_hidden || !g_lm_self || !g_lm_prev) return;
    
    
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





void apply_deferred_link_hide() noexcept
{
    if (g_deferred_attempted.load(std::memory_order_relaxed))
        return;
    pthread_mutex_lock(&g_mutex);
    if (!g_initialized || g_deferred_done) {
        pthread_mutex_unlock(&g_mutex);
        return; 
    }
    g_deferred_done = true;
    g_deferred_attempted.store(true, std::memory_order_relaxed);
    const bool already = g_link_hidden;
    pthread_mutex_unlock(&g_mutex);

    if (already || !kEnableLinkHide)
        return;
    
    
    
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

} 

