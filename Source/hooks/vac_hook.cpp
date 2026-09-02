#include "vac_hook.h"
#include "integrity_audit.h"

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <elf.h>

namespace fva::hooks
{

namespace
{

using FopenFn = FILE*(*)(const char*, const char*);
using FgetsFn = char*(*)(char*, int, FILE*);
using OpenFn = int(*)(const char*, int, ...);
using ReadFn = ssize_t(*)(int, void*, size_t);
using CloseFn = int(*)(int);

FopenFn g_orig_fopen = nullptr;
FgetsFn g_orig_fgets = nullptr;
OpenFn g_orig_open = nullptr;
ReadFn g_orig_read = nullptr;
CloseFn g_orig_close = nullptr;

struct GotBackup {
    void** entry;
    void* original;
    bool valid;
};

GotBackup g_fopen_backup;
GotBackup g_fgets_backup;
GotBackup g_open_backup;
GotBackup g_read_backup;
GotBackup g_close_backup;

bool g_initialized = false;
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

constexpr const char* kHideModule = "libutil_helper.so";

struct MapsTracker {
    FILE* fp;
    int fd;
    bool active;
};

constexpr int kMaxMapsFiles = 8;
MapsTracker g_maps_files[kMaxMapsFiles];
int g_maps_count = 0;
pthread_mutex_t g_maps_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    Elf64_Word* hashtab = nullptr;
    Elf64_Rel* jmprel = nullptr;
    size_t pltrelsz = 0;
    
    for (Elf64_Dyn* d = dyn; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_SYMTAB: symtab = (Elf64_Sym*)((uintptr_t)base + d->d_un.d_ptr); break;
            case DT_STRTAB: strtab = (char*)((uintptr_t)base + d->d_un.d_ptr); break;
            case DT_HASH:   hashtab = (Elf64_Word*)((uintptr_t)base + d->d_un.d_ptr); break;
            case DT_JMPREL: jmprel = (Elf64_Rel*)((uintptr_t)base + d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
        }
    }
    
    if (!symtab || !strtab || !hashtab || !jmprel) return nullptr;
    
    Elf64_Word nchain = hashtab[1];
    
    for (Elf64_Word i = 0; i < nchain; ++i) {
        Elf64_Sym* s = &symtab[i];
        char* name = strtab + s->st_name;
        if (strcmp(name, sym_name) == 0 && s->st_shndx == SHN_UNDEF) {
            for (size_t j = 0; j < pltrelsz; j += sizeof(Elf64_Rel)) {
                Elf64_Rel* r = (Elf64_Rel*)((uintptr_t)jmprel + j);
                if (ELF64_R_SYM(r->r_info) == i) {
                    return (void**)((uintptr_t)base + r->r_offset);
                }
            }
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
    
    if (path && (strstr(path, "/proc/self/maps") ||
                 strstr(path, "/proc/self/task/") ||
                 strstr(path, "/proc/self/smaps") ||
                 strstr(path, "/proc/self/mem"))) {
        pthread_mutex_lock(&g_maps_mutex);
        if (g_maps_count < kMaxMapsFiles) {
            for (int i = 0; i < kMaxMapsFiles; ++i) {
                if (!g_maps_files[i].active) {
                    g_maps_files[i].fp = g_orig_fopen(path, mode);
                    g_maps_files[i].active = true;
                    g_maps_count++;
                    pthread_mutex_unlock(&g_maps_mutex);
                    return g_maps_files[i].fp;
                }
            }
        }
        pthread_mutex_unlock(&g_maps_mutex);
    }
    
    return g_orig_fopen(path, mode);
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
        if (spoofed_fd >= 0)
            return spoofed_fd;
    }
    
    if (path && (strstr(path, "/proc/self/maps") ||
                 strstr(path, "/proc/self/task/") ||
                 strstr(path, "/proc/self/smaps") ||
                 strstr(path, "/proc/self/mem"))) {
        pthread_mutex_lock(&g_maps_mutex);
        if (g_maps_count < kMaxMapsFiles) {
            for (int i = 0; i < kMaxMapsFiles; ++i) {
                if (!g_maps_files[i].active) {
                    g_maps_files[i].fd = g_orig_open(path, flags, mode);
                    g_maps_files[i].active = true;
                    g_maps_count++;
                    pthread_mutex_unlock(&g_maps_mutex);
                    return g_maps_files[i].fd;
                }
            }
        }
        pthread_mutex_unlock(&g_maps_mutex);
    }
    
    return g_orig_open(path, flags, mode);
}

static bool is_maps_file(FILE* fp)
{
    pthread_mutex_lock(&g_maps_mutex);
    for (int i = 0; i < kMaxMapsFiles; ++i) {
        if (g_maps_files[i].active && g_maps_files[i].fp == fp) {
            pthread_mutex_unlock(&g_maps_mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&g_maps_mutex);
    return false;
}

static bool is_maps_fd(int fd)
{
    pthread_mutex_lock(&g_maps_mutex);
    for (int i = 0; i < kMaxMapsFiles; ++i) {
        if (g_maps_files[i].active && g_maps_files[i].fd == fd) {
            pthread_mutex_unlock(&g_maps_mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&g_maps_mutex);
    return false;
}

static void close_maps_tracker(FILE* fp, int fd)
{
    pthread_mutex_lock(&g_maps_mutex);
    for (int i = 0; i < kMaxMapsFiles; ++i) {
        if (g_maps_files[i].active) {
            if ((fp && g_maps_files[i].fp == fp) || (fd >= 0 && g_maps_files[i].fd == fd)) {
                g_maps_files[i].active = false;
                g_maps_count--;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_maps_mutex);
}

__attribute__((noinline))
char* vac_fgets_hook(char* buf, int size, FILE* fp)
{
    if (!g_orig_fgets) return nullptr;
    
    char* result = g_orig_fgets(buf, size, fp);
    
    if (result && is_maps_file(fp)) {
        while (result && strstr(buf, kHideModule)) {
            result = g_orig_fgets(buf, size, fp);
        }
        if (!result) {
            close_maps_tracker(fp, -1);
        }
    }
    
    return result;
}

__attribute__((noinline))
ssize_t vac_read_hook(int fd, void* buf, size_t count)
{
    if (!g_orig_read) return -1;
    
    if (::security::integrity::should_spoof_fd(fd))
    {
        return ::security::integrity::read_spoofed_file(fd, buf, count);
    }
    
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
            if (line_len > 0) {
                bool contains_cheat = false;
                for (size_t i = 0; i + strlen(kHideModule) <= line_len; ++i) {
                    if (memcmp(line_start + i, kHideModule, strlen(kHideModule)) == 0) {
                        contains_cheat = true;
                        break;
                    }
                }
                if (!contains_cheat) {
                    if (dst != line_start) {
                        memmove(dst, line_start, line_len);
                    }
                    dst += line_len;
                }
            }
        }
        
        result = dst - data;
        if (result == 0) {
            close_maps_tracker(nullptr, fd);
        }
    }
    
    return result;
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
    
    close_maps_tracker(nullptr, fd);
    
    return g_orig_close(fd);
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
    
    uintptr_t page = (uintptr_t)got & ~0xFFF;
    mprotect((void*)page, 0x2000, PROT_READ | PROT_WRITE);
    
    backup->entry = got;
    backup->original = *got;
    backup->valid = true;
    *got = newval;
    
    mprotect((void*)page, 0x2000, PROT_READ);
}

static void restore_got(GotBackup* backup)
{
    if (!backup->valid || !backup->entry) return;
    
    uintptr_t page = (uintptr_t)backup->entry & ~0xFFF;
    mprotect((void*)page, 0x2000, PROT_READ | PROT_WRITE);
    *backup->entry = backup->original;
    mprotect((void*)page, 0x2000, PROT_READ);
    backup->valid = false;
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
    g_orig_read = (ReadFn)dlsym(RTLD_NEXT, "read");
    g_orig_close = (CloseFn)dlsym(RTLD_NEXT, "close");
    
    if (!g_orig_fopen || !g_orig_fgets) {
        pthread_mutex_unlock(&g_mutex);
        return false;
    }
    
    void** fopen_got = resolve_got_dynamic("fopen");
    void** fgets_got = resolve_got_dynamic("fgets");
    void** open_got = g_orig_open ? resolve_got_dynamic("open") : nullptr;
    void** read_got = g_orig_read ? resolve_got_dynamic("read") : nullptr;
    void** close_got = g_orig_close ? resolve_got_dynamic("close") : nullptr;
    
    if (!fopen_got || !fgets_got) {
        constexpr uintptr_t kFopenGotOffset = 0x2add9b0;
        constexpr uintptr_t kFgetsGotOffset = 0x2addcd8;
        
        fopen_got = (void**)((uintptr_t)steamclient_base + kFopenGotOffset);
        fgets_got = (void**)((uintptr_t)steamclient_base + kFgetsGotOffset);
    }
    
    patch_got(fopen_got, (void*)vac_fopen_hook, &g_fopen_backup);
    patch_got(fgets_got, (void*)vac_fgets_hook, &g_fgets_backup);
    
    if (open_got) {
        patch_got(open_got, (void*)vac_open_hook, &g_open_backup);
    }
    if (read_got) {
        patch_got(read_got, (void*)vac_read_hook, &g_read_backup);
    }
    if (close_got) {
        patch_got(close_got, (void*)vac_close_hook, &g_close_backup);
    }
    
    ::security::integrity::initialize();
    
    g_initialized = true;
    pthread_mutex_unlock(&g_mutex);
    
    return true;
}

void uninstall_vac_hook()
{
    pthread_mutex_lock(&g_mutex);
    
    restore_got(&g_fopen_backup);
    restore_got(&g_fgets_backup);
    restore_got(&g_open_backup);
    restore_got(&g_read_backup);
    restore_got(&g_close_backup);
    
    ::security::integrity::shutdown();
    
    g_initialized = false;
    pthread_mutex_unlock(&g_mutex);
}

} // namespace fva::hooks
