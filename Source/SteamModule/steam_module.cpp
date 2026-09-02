#define _GNU_SOURCE
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

static int g_initialized = 0;
static pthread_mutex_t g_init_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char* kHideModule = "libutil_helper.so";
static const char* kHideMemfd = "memfd:util_helper";

typedef FILE* (*FopenFn)(const char*, const char*);
typedef char* (*FgetsFn)(char*, int, FILE*);
typedef int (*OpenFn)(const char*, int, ...);
typedef ssize_t (*ReadFn)(int, void*, size_t);

static FopenFn g_orig_fopen = nullptr;
static FgetsFn g_orig_fgets = nullptr;
static OpenFn g_orig_open = nullptr;
static ReadFn g_orig_read = nullptr;

struct MapsTracker {
    FILE* fp;
    int fd;
    int active;
};

#define MAX_MAPS_FILES 8
static struct MapsTracker g_maps[MAX_MAPS_FILES];
static int g_maps_count = 0;
static pthread_mutex_t g_maps_mutex = PTHREAD_MUTEX_INITIALIZER;

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
            case DT_SYMTAB: symtab = (Elf64_Sym*)(uintptr_t)d->d_un.d_ptr; break;
            case DT_STRTAB: strtab = (char*)(uintptr_t)d->d_un.d_ptr; break;
            case DT_HASH:   hashtab = (Elf64_Word*)(uintptr_t)d->d_un.d_ptr; break;
            case DT_JMPREL: jmprel = (Elf64_Rel*)(uintptr_t)d->d_un.d_ptr; break;
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

static int patch_got(void** got, void* newval)
{
    if (!got) return -1;
    
    uintptr_t page = (uintptr_t)got & ~0xFFF;
    mprotect((void*)page, 0x2000, PROT_READ | PROT_WRITE);
    *got = newval;
    mprotect((void*)page, 0x2000, PROT_READ);
    
    return 0;
}

static int should_filter(const char* line)
{
    if (!line) return 0;
    if (strstr(line, kHideModule)) return 1;
    if (strstr(line, kHideMemfd)) return 1;
    return 0;
}

FILE* hook_fopen(const char* path, const char* mode)
{
    if (!g_orig_fopen) return nullptr;
    
    FILE* result = g_orig_fopen(path, mode);
    
    if (path && strstr(path, "/proc/") && strstr(path, "/maps")) {
        pthread_mutex_lock(&g_maps_mutex);
        if (g_maps_count < MAX_MAPS_FILES) {
            for (int i = 0; i < MAX_MAPS_FILES; ++i) {
                if (!g_maps[i].active) {
                    g_maps[i].fp = result;
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
    
    if (path && strstr(path, "/proc/") && strstr(path, "/maps")) {
        pthread_mutex_lock(&g_maps_mutex);
        if (g_maps_count < MAX_MAPS_FILES) {
            for (int i = 0; i < MAX_MAPS_FILES; ++i) {
                if (!g_maps[i].active) {
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
    
    g_initialized = 1;
    fprintf(stderr, "[SteamVAC] Hooks installed\n");
}

__attribute__((constructor))
static void library_init(void)
{
    pthread_mutex_lock(&g_init_mutex);
    
    if (!g_initialized) {
        init_hooks();
    }
    
    pthread_mutex_unlock(&g_init_mutex);
}
