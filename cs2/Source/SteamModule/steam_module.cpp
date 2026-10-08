#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <elf.h>
#include <stdarg.h>
#include <stdlib.h>

#include "hooks/vac_str.h"
#include <Utils/SessionBindKey.h>

#include <dirent.h>

static int g_initialized = 0;
static pthread_mutex_t g_init_mutex = PTHREAD_MUTEX_INITIALIZER;






namespace {
VAC_XSTR(kTokModule, "libMangoHud.so");
VAC_XSTR(kTokMemfd, "memfd:libMangoHud");


VAC_XSTR(kTokFallback, ".fc-cache-");
VAC_XSTR(kTokSteamTmp, ".Xauthority-");
} 

typedef FILE* (*FopenFn)(const char*, const char*);
typedef char* (*FgetsFn)(char*, int, FILE*);
typedef int (*OpenFn)(const char*, int, ...);
typedef ssize_t (*ReadFn)(int, void*, size_t);
typedef int (*FcloseFn)(FILE*);

static FopenFn g_orig_fopen = nullptr;
static FgetsFn g_orig_fgets = nullptr;
static OpenFn g_orig_open = nullptr;
static ReadFn g_orig_read = nullptr;
static FcloseFn g_orig_fclose = nullptr;

struct MapsTracker {
    FILE* fp;
    int fd;
    int active;
};

#define MAX_MAPS_FILES 8
static struct MapsTracker g_maps[MAX_MAPS_FILES];
static int g_maps_count = 0;
static pthread_mutex_t g_maps_mutex = PTHREAD_MUTEX_INITIALIZER;

#if __SIZEOF_POINTER__ == 8
typedef Elf64_Ehdr NsEhdr;
typedef Elf64_Phdr NsPhdr;
typedef Elf64_Dyn NsDyn;
typedef Elf64_Sym NsSym;
typedef Elf64_Rela NsRela;
typedef Elf64_Rel NsRel;
#define NS_R_SYM(i) ELF64_R_SYM(i)
#else
typedef Elf32_Ehdr NsEhdr;
typedef Elf32_Phdr NsPhdr;
typedef Elf32_Dyn NsDyn;
typedef Elf32_Sym NsSym;
typedef Elf32_Rela NsRela;
typedef Elf32_Rel NsRel;
#define NS_R_SYM(i) ELF32_R_SYM(i)
#endif




static uintptr_t ns_adjust_ptr(void* base, uintptr_t v)
{
    const uintptr_t b = (uintptr_t)base;
    if (v >= b && v - b < 0x80000000ULL) return v;
    return b + v;
}

static void** find_got_entry(void* base, const char* sym_name)
{
    NsEhdr* ehdr = (NsEhdr*)base;
    if (ehdr->e_type != ET_DYN) return nullptr;

    NsPhdr* phdr = (NsPhdr*)((uintptr_t)base + ehdr->e_phoff);
    NsDyn* dyn = nullptr;

    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dyn = (NsDyn*)((uintptr_t)base + phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return nullptr;

    NsSym* symtab = nullptr;
    char* strtab = nullptr;
    const unsigned char* jmprel = nullptr;
    bool use_rela = true;
    size_t pltrelsz = 0;

    for (NsDyn* d = dyn; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_SYMTAB: symtab = (NsSym*)ns_adjust_ptr(base, d->d_un.d_ptr); break;
            case DT_STRTAB: strtab = (char*)ns_adjust_ptr(base, d->d_un.d_ptr); break;
            case DT_JMPREL: jmprel = (const unsigned char*)ns_adjust_ptr(base, d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
            case DT_PLTREL: use_rela = (d->d_un.d_val == DT_RELA); break;
        }
    }

    
    
    
    
    
    if (!symtab || !strtab || !jmprel || !pltrelsz) return nullptr;

    const size_t entry_size = use_rela ? sizeof(NsRela) : sizeof(NsRel);

    for (size_t j = 0; j < pltrelsz; j += entry_size) {
        const void* rel = jmprel + j;
        size_t sym_idx = use_rela
            ? NS_R_SYM(((const NsRela*)rel)->r_info)
            : NS_R_SYM(((const NsRel*)rel)->r_info);
        NsSym* s = &symtab[sym_idx];
        if (s->st_shndx == SHN_UNDEF && strcmp(strtab + s->st_name, sym_name) == 0) {
            uintptr_t got_offset = use_rela
                ? (uintptr_t)((const NsRela*)rel)->r_offset
                : (uintptr_t)((const NsRel*)rel)->r_offset;
            return (void**)((uintptr_t)base + got_offset);
        }
    }

    return nullptr;
}

static int patch_got(void** got, void* newval)
{
    if (!got) return -1;

    
    
    
    
    const uintptr_t page = (uintptr_t)got & ~(uintptr_t)0xFFFu;
    int saved = PROT_READ;
    FILE* maps = fopen("/proc/self/maps", "r");
    if (maps) {
        char line[512];
        while (fgets(line, sizeof(line), maps)) {
            unsigned long start = 0, end = 0;
            char perms[8] = {0};
            if (sscanf(line, "%lx-%lx %7s", &start, &end, perms) < 3)
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
        fclose(maps);
    }

    if (mprotect((void*)page, 0x1000, PROT_READ | PROT_WRITE) != 0)
        return -1;
    *got = newval;
    mprotect((void*)page, 0x1000, saved);

    return 0;
}

static int token_in_line(const char* token, const char* line)
{
    size_t tlen = strlen(token);
    size_t llen = strlen(line);
    if (tlen == 0 || tlen > llen) return 0;
    for (size_t i = 0; i + tlen <= llen; ++i) {
        if (memcmp(line + i, token, tlen) == 0) return 1;
    }
    return 0;
}

static int should_filter(const char* line)
{
    if (!line) return 0;
    
    
    char module[32];
    char memfd[32];
    char fallback[32];
    char steamtmo[32];
    kTokModule.decrypt(module);
    kTokMemfd.decrypt(memfd);
    kTokFallback.decrypt(fallback);
    kTokSteamTmp.decrypt(steamtmo);
    if (token_in_line(module, line)) return 1;
    if (token_in_line(memfd, line)) return 1;
    if (token_in_line(fallback, line)) return 1;
    if (token_in_line(steamtmo, line)) return 1;
    return 0;
}

FILE* hook_fopen(const char* path, const char* mode)
{
    if (!g_orig_fopen) return nullptr;

    FILE* result = g_orig_fopen(path, mode);

    if (result && path && strstr(path, "/proc/") && strstr(path, "/maps")) {
        pthread_mutex_lock(&g_maps_mutex);
        if (g_maps_count < MAX_MAPS_FILES) {
            for (int i = 0; i < MAX_MAPS_FILES; ++i) {
                if (!g_maps[i].active) {
                    g_maps[i].fp = result;
                    g_maps[i].fd = -1;
                    g_maps[i].active = 1;
                    g_maps_count++;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&g_maps_mutex);
    }

    return result;
}

int hook_open(const char* path, int flags, ...)
{
    if (!g_orig_open) return -1;

    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    int result = g_orig_open(path, flags, mode);

    if (result >= 0 && path && strstr(path, "/proc/") && strstr(path, "/maps")) {
        pthread_mutex_lock(&g_maps_mutex);
        if (g_maps_count < MAX_MAPS_FILES) {
            for (int i = 0; i < MAX_MAPS_FILES; ++i) {
                if (!g_maps[i].active) {
                    g_maps[i].fp = NULL;
                    g_maps[i].fd = result;
                    g_maps[i].active = 1;
                    g_maps_count++;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&g_maps_mutex);
    }

    return result;
}

static int is_maps_fp(FILE* fp)
{
    pthread_mutex_lock(&g_maps_mutex);
    for (int i = 0; i < MAX_MAPS_FILES; ++i) {
        if (g_maps[i].active && g_maps[i].fp == fp) {
            pthread_mutex_unlock(&g_maps_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&g_maps_mutex);
    return 0;
}

static int is_maps_fd(int fd)
{
    pthread_mutex_lock(&g_maps_mutex);
    for (int i = 0; i < MAX_MAPS_FILES; ++i) {
        if (g_maps[i].active && g_maps[i].fd == fd) {
            pthread_mutex_unlock(&g_maps_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&g_maps_mutex);
    return 0;
}

char* hook_fgets(char* buf, int size, FILE* fp)
{
    if (!g_orig_fgets) return nullptr;

    char* result = g_orig_fgets(buf, size, fp);

    if (result && is_maps_fp(fp)) {
        while (result && should_filter(buf)) {
            result = g_orig_fgets(buf, size, fp);
        }
    }

    return result;
}

ssize_t hook_read(int fd, void* buf, size_t count)
{
    if (!g_orig_read) return -1;

    ssize_t result = g_orig_read(fd, buf, count);

    if (result > 0 && is_maps_fd(fd)) {
        char* data = (char*)buf;
        char* src = data;
        char* dst = data;
        char* end = data + result;

        while (src < end) {
            char* line_start = src;
            while (src < end && *src != '\n') src++;
            if (src < end) src++;

            size_t line_len = src - line_start;
            if (line_len > 0 && !should_filter(line_start)) {
                if (dst != line_start) {
                    memmove(dst, line_start, line_len);
                }
                dst += line_len;
            }
        }

        result = dst - data;
    }

    return result;
}

static void free_maps_slot_fp(FILE* fp)
{
    pthread_mutex_lock(&g_maps_mutex);
    for (int i = 0; i < MAX_MAPS_FILES; ++i) {
        if (g_maps[i].active && g_maps[i].fp == fp) {
            g_maps[i].active = 0;
            g_maps[i].fp = NULL;
            g_maps[i].fd = -1;
            g_maps_count--;
            break;
        }
    }
    pthread_mutex_unlock(&g_maps_mutex);
}

int hook_fclose(FILE* fp)
{
    if (!g_orig_fclose) return -1;
    
    
    
    if (fp) free_maps_slot_fp(fp);
    return g_orig_fclose(fp);
}

static int find_module_base(const char* name, void** base)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/maps");
    FILE* fp = fopen(path, "r");
    if (!fp) return -1;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, name)) {
            unsigned long addr;
            sscanf(line, "%lx", &addr);
            *base = (void*)addr;
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return -1;
}

static void init_hooks(void)
{
    g_orig_fopen = (FopenFn)dlsym(RTLD_NEXT, "fopen");
    g_orig_fgets = (FgetsFn)dlsym(RTLD_NEXT, "fgets");
    g_orig_open = (OpenFn)dlsym(RTLD_NEXT, "open");
    g_orig_read = (ReadFn)dlsym(RTLD_NEXT, "read");
    g_orig_fclose = (FcloseFn)dlsym(RTLD_NEXT, "fclose");

    if (!g_orig_fopen || !g_orig_fgets) {
        fprintf(stderr, "[SteamVAC] Failed to resolve libc functions\n");
        return;
    }

    void* steamservice_base = nullptr;
    if (find_module_base("steamservice.so", &steamservice_base) != 0) {
        fprintf(stderr, "[SteamVAC] steamservice.so not found\n");
        return;
    }

    fprintf(stderr, "[SteamVAC] steamservice.so at %p\n", steamservice_base);

    void** fopen_got = find_got_entry(steamservice_base, "fopen");
    void** fgets_got = find_got_entry(steamservice_base, "fgets");
    void** open_got = g_orig_open ? find_got_entry(steamservice_base, "open") : nullptr;
    void** read_got = g_orig_read ? find_got_entry(steamservice_base, "read") : nullptr;
    void** fclose_got = g_orig_fclose ? find_got_entry(steamservice_base, "fclose") : nullptr;

    if (fopen_got) {
        patch_got(fopen_got, (void*)hook_fopen);
        fprintf(stderr, "[SteamVAC] Hooked fopen at %p\n", fopen_got);
    }
    if (fgets_got) {
        patch_got(fgets_got, (void*)hook_fgets);
        fprintf(stderr, "[SteamVAC] Hooked fgets at %p\n", fgets_got);
    }
    if (open_got) {
        patch_got(open_got, (void*)hook_open);
        fprintf(stderr, "[SteamVAC] Hooked open at %p\n", open_got);
    }
    if (read_got) {
        patch_got(read_got, (void*)hook_read);
        fprintf(stderr, "[SteamVAC] Hooked read at %p\n", read_got);
    }
    if (fclose_got) {
        patch_got(fclose_got, (void*)hook_fclose);
        fprintf(stderr, "[SteamVAC] Hooked fclose at %p\n", fclose_got);
    }

    
    
    
    static const int kEnableLinkHide = 0;
    if (kEnableLinkHide)
    {
    
    
    
    
    
    {
        Dl_info info;
        if (dladdr((const void*)hook_fopen, &info) != 0 && info.dli_fbase) {
            void* mh = dlopen(NULL, RTLD_LAZY);
            struct link_map* head = NULL;
            if (mh) {
                dlinfo(mh, RTLD_DI_LINKMAP, &head);
                dlclose(mh);
            }
            struct link_map* self = NULL;
            for (struct link_map* lm = head; lm; lm = lm->l_next) {
                if ((uintptr_t)lm->l_addr == (uintptr_t)info.dli_fbase) {
                    self = lm;
                    break;
                }
            }
            if (self && self != head && self->l_prev && self->l_prev->l_next == self &&
                (!self->l_next || self->l_next->l_prev == self)) {
                self->l_prev->l_next = self->l_next;
                if (self->l_next) self->l_next->l_prev = self->l_prev;
                fprintf(stderr, "[SteamVAC] link_map node unlinked\n");
            }
        }
    }
    }

    g_initialized = 1;
    fprintf(stderr, "[SteamVAC] Hooks installed\n");
}









static int verify_session_trailer(void)
{
    unsigned char key[32];
    for (int i = 0; i < 32; ++i)
        key[i] = (unsigned char)(session_bind_key::kKeyMasked[i] ^ session_bind_key::kKeyMask[i]);

    DIR* dir = opendir("/proc/self/fd");
    if (!dir)
        return 0;
    int fd = -1;
    char fdPath[64];
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        int numeric = entry->d_name[0] != '\0';
        for (const char* p = entry->d_name; *p; ++p) {
            if (*p < '0' || *p > '9') { numeric = 0; break; }
        }
        if (!numeric)
            continue;
        snprintf(fdPath, sizeof(fdPath), "/proc/self/fd/%s", entry->d_name);
        char target[128];
        ssize_t len = readlink(fdPath, target, sizeof(target) - 1);
        if (len <= 0)
            continue;
        target[len] = '\0';
        if (strncmp(target, "/memfd:libMangoHud.so", 21) != 0)
            continue;
        fd = open(fdPath, O_RDONLY);
        if (fd >= 0)
            break;
    }
    closedir(dir);
    if (fd < 0)
        return 0;

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 64) {
        close(fd);
        return 0;
    }
    unsigned char trailer[64];
    if (pread(fd, trailer, 64, size - 64) != 64) {
        close(fd);
        return 0;
    }
    close(fd);
    
    static const unsigned char kMagicObf[6] = {0x14, 0x09, 0x12, 0x18, 0x6A, 0x68};
    unsigned char magic[6];
    for (int i = 0; i < 6; ++i)
        magic[i] = (unsigned char)(kMagicObf[i] ^ 0x5A);
    if (memcmp(trailer, magic, 6) != 0)
        return 0;
    
    const unsigned long long ownPid = (unsigned long long)getpid();
    for (int i = 0; i < 32; ++i) {
        const unsigned char targetByte = (unsigned char)((ownPid >> ((i % 8) * 8)) & 0xff);
        if (trailer[32 + i] != (unsigned char)(trailer[16 + (i % 16)] ^ key[i] ^ targetByte))
            return 0;
    }
    return 1;
}

__attribute__((constructor))
static void library_init(void)
{
    pthread_mutex_lock(&g_init_mutex);

    if (!g_initialized) {
        if (verify_session_trailer()) {
            init_hooks();
        } else {
            fprintf(stderr, "[SteamVAC] no valid session trailer - staying inert\n");
        }
    }

    pthread_mutex_unlock(&g_init_mutex);
}

