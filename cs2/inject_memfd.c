

#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#define NT_X86_XSTATE 0x202
#define XSTATE_BUF 8192

#define STACK_WINDOW 0x600
#define PAGE_NAME_OFF 0x100
#define PAGE_PATH_OFF 0x200

static pid_t g_pid;
static struct user_regs_struct g_saved;
static unsigned char g_xstate[XSTATE_BUF];
static int g_xstate_valid;

static uint64_t g_landing_addr;
static long g_landing_saved_word;
static int g_landing_patched;
static uint64_t g_stack_window_start;
static unsigned char g_stack_backup[STACK_WINDOW + 8];
static int g_stack_backup_valid;
static uint64_t g_page;


static int g_arch32; 


struct i386_user_regs {
    uint32_t ebx, ecx, edx, esi, edi, ebp, eax;
    uint32_t xds, xes, xfs, xgs;
    uint32_t orig_eax, eip, xcs, eflags, esp, xss;
};
static struct i386_user_regs g_saved32;


#define SYS_I386_mmap2 192
#define SYS_I386_munmap 91
#define SYS_I386_memfd_create 356


static int target_elf_class(void)
{
    char p[64];
    snprintf(p, sizeof(p), "/proc/%d/exe", g_pid);
    int fd = open(p, O_RDONLY);
    if (fd < 0)
        return -1;
    unsigned char id[5];
    ssize_t n = read(fd, id, sizeof(id));
    close(fd);
    if (n < 5)
        return -1;
    return id[4] == 1; 
}

static void die_restore(void) __attribute__((noreturn));




static int find_module(const char* substring, uint64_t* base, uint64_t* text_start, uint64_t* text_end, char* file_path, size_t path_sz)
{
    char p[128];
    snprintf(p, sizeof(p), "/proc/%d/maps", g_pid);
    FILE* fp = fopen(p, "r");
    if (!fp)
        return -1;

    char line[1024];
    *base = *text_start = *text_end = 0;
    file_path[0] = '\0';
    while (fgets(line, sizeof(line), fp)) {
        if (!strstr(line, substring))
            continue;
        uint64_t s, e, off;
        char perms[8], path[768];
        int n = sscanf(line, "%lx-%lx %7s %lx %*x:%*x %*u %767s", &s, &e, perms, &off, path);
        if (n < 4)
            continue;
        if (!*base) {
            *base = s;
            if (n == 5) {
                strncpy(file_path, path, path_sz - 1);
                file_path[path_sz - 1] = '\0';
            }
        }
        if (!strcmp(perms, "r-xp") && !*text_start) {
            *text_start = s;
            *text_end = e;
        }
        if (*base && *text_start)
            break;
    }
    fclose(fp);
    return (*base && *text_start && file_path[0]) ? 0 : -1;
}


static int open_module_file(const char* maps_path)
{
    int fd = open(maps_path, O_RDONLY);
    if (fd >= 0)
        return fd;
    char rooted[800];
    if ((size_t)snprintf(rooted, sizeof(rooted), "/proc/%d/root%s", g_pid, maps_path) < sizeof(rooted)) {
        fd = open(rooted, O_RDONLY);
        if (fd >= 0)
            return fd;
    }
    if (!strncmp(maps_path, "/run/host/", 10))
        return open(maps_path + 10, O_RDONLY);
    return -1;
}




static int vaddr_to_offset(const ElfW(Ehdr)* ehdr, uint64_t vaddr, uint64_t* off)
{
    const ElfW(Phdr)* phdr = (const ElfW(Phdr)*)((const char*)ehdr + ehdr->e_phoff);
    for (unsigned i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD || vaddr < phdr[i].p_vaddr || vaddr >= phdr[i].p_vaddr + phdr[i].p_filesz)
            continue;
        *off = vaddr - phdr[i].p_vaddr + phdr[i].p_offset;
        return 0;
    }
    return -1;
}


static int vaddr_to_offset32(const Elf32_Ehdr* ehdr, uint32_t vaddr, uint32_t* off)
{
    const Elf32_Phdr* phdr = (const Elf32_Phdr*)((const char*)ehdr + ehdr->e_phoff);
    for (unsigned i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD || vaddr < phdr[i].p_vaddr || vaddr >= phdr[i].p_vaddr + phdr[i].p_filesz)
            continue;
        *off = vaddr - phdr[i].p_vaddr + phdr[i].p_offset;
        return 0;
    }
    return -1;
}

static int elf_dynsym_lookup32(const void* map, size_t size, const char* name, uint64_t* value)
{
    const Elf32_Ehdr* ehdr = map;
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0)
        return -1;

    const Elf32_Phdr* phdr = (const Elf32_Phdr*)((const char*)ehdr + ehdr->e_phoff);
    uint32_t dyn_off = 0, dyn_sz = 0;
    for (unsigned i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dyn_off = phdr[i].p_offset;
            dyn_sz = phdr[i].p_filesz;
            break;
        }
    }
    if (!dyn_sz)
        return -1;

    uint32_t symtab_v = 0, strtab_v = 0, gnuhash_v = 0;
    const Elf32_Dyn* dyn = (const Elf32_Dyn*)((const char*)ehdr + dyn_off);
    for (size_t i = 0; i < dyn_sz / sizeof(Elf32_Dyn); ++i) {
        if (dyn[i].d_tag == DT_NULL)
            break;
        if (dyn[i].d_tag == DT_SYMTAB)
            symtab_v = dyn[i].d_un.d_ptr;
        else if (dyn[i].d_tag == DT_STRTAB)
            strtab_v = dyn[i].d_un.d_ptr;
        else if (dyn[i].d_tag == DT_GNU_HASH)
            gnuhash_v = dyn[i].d_un.d_ptr;
    }
    if (!symtab_v || !strtab_v || !gnuhash_v)
        return -1;

    uint32_t symtab_off, strtab_off, gnuhash_off;
    if (vaddr_to_offset32(ehdr, symtab_v, &symtab_off) || vaddr_to_offset32(ehdr, strtab_v, &strtab_off)
        || vaddr_to_offset32(ehdr, gnuhash_v, &gnuhash_off))
        return -1;

    
    const uint32_t* gh = (const uint32_t*)((const char*)ehdr + gnuhash_off);
    uint32_t nbuckets = gh[0], symoffset = gh[1], bloom_size = gh[2];
    const uint32_t* bloom = gh + 4;
    const uint32_t* buckets = bloom + bloom_size;
    const uint32_t* chains = buckets + nbuckets;
    (void)bloom;

    uint32_t max_index = 0;
    int found = 0;
    for (uint32_t b = 0; b < nbuckets; ++b) {
        if (buckets[b] > max_index) {
            max_index = buckets[b];
            found = 1;
        }
    }
    if (!found)
        return -1;
    while (!(chains[max_index - symoffset] & 1))
        ++max_index;

    const char* strtab = (const char*)ehdr + strtab_off;
    for (uint32_t i = 0; i <= max_index; ++i) {
        const Elf32_Sym* sym = (const Elf32_Sym*)((const char*)ehdr + symtab_off + (size_t)i * sizeof(Elf32_Sym));
        if (!sym->st_value || sym->st_name >= (Elf32_Word)(size - strtab_off))
            continue;
        if (!strcmp(strtab + sym->st_name, name)) {
            *value = sym->st_value;
            return 0;
        }
    }
    return -1;
}


static int elf_dynsym_lookup(int fd, const char* name, uint64_t* value)
{
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < (off_t)sizeof(ElfW(Ehdr)))
        return -1;

    void* map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED)
        return -1;
    int result = -1;
    const unsigned char* ident = map;
    if (ident[EI_CLASS] == ELFCLASS32) {
        result = elf_dynsym_lookup32(map, (size_t)st.st_size, name, value);
        munmap(map, st.st_size);
        return result;
    }
    const ElfW(Ehdr)* ehdr = map;

    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 || ehdr->e_ident[EI_MAG1] != ELFMAG1
        || ehdr->e_ident[EI_MAG2] != ELFMAG2 || ehdr->e_ident[EI_MAG3] != ELFMAG3
        || ehdr->e_phoff == 0 || ehdr->e_phnum == 0)
        goto out;

    const ElfW(Phdr)* phdr = (const ElfW(Phdr)*)((const char*)ehdr + ehdr->e_phoff);
    uint64_t dyn_off = 0, dyn_sz = 0;
    for (unsigned i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dyn_off = phdr[i].p_offset;
            dyn_sz = phdr[i].p_filesz;
            break;
        }
    }
    if (!dyn_sz)
        goto out;

    uint64_t symtab_v = 0, strtab_v = 0, gnuhash_v = 0;
    const ElfW(Dyn)* dyn = (const ElfW(Dyn)*)((const char*)ehdr + dyn_off);
    for (size_t i = 0; i < dyn_sz / sizeof(ElfW(Dyn)); ++i) {
        if (dyn[i].d_tag == DT_NULL)
            break;
        if (dyn[i].d_tag == DT_SYMTAB)
            symtab_v = dyn[i].d_un.d_ptr;
        else if (dyn[i].d_tag == DT_STRTAB)
            strtab_v = dyn[i].d_un.d_ptr;
        else if (dyn[i].d_tag == DT_GNU_HASH)
            gnuhash_v = dyn[i].d_un.d_ptr;
    }
    if (!symtab_v || !strtab_v || !gnuhash_v)
        goto out;

    uint64_t symtab_off, strtab_off, gnuhash_off;
    if (vaddr_to_offset(ehdr, symtab_v, &symtab_off) || vaddr_to_offset(ehdr, strtab_v, &strtab_off)
        || vaddr_to_offset(ehdr, gnuhash_v, &gnuhash_off))
        goto out;

    
    const uint32_t* gh = (const uint32_t*)((const char*)ehdr + gnuhash_off);
    uint32_t nbuckets = gh[0], symoffset = gh[1], bloom_size = gh[2];
    const uint64_t* bloom = (const uint64_t*)(gh + 4);
    const uint32_t* buckets = (const uint32_t*)(bloom + bloom_size);
    const uint32_t* chains = buckets + nbuckets;

    uint32_t max_index = 0;
    int found = 0;
    for (uint32_t b = 0; b < nbuckets; ++b) {
        if (buckets[b] > max_index) {
            max_index = buckets[b];
            found = 1;
        }
    }
    if (!found)
        goto out;
    while (!(chains[max_index - symoffset] & 1))
        ++max_index;

    const char* strtab = (const char*)ehdr + strtab_off;
    for (uint32_t i = 0; i <= max_index; ++i) {
        const ElfW(Sym)* sym = (const ElfW(Sym)*)((const char*)ehdr + symtab_off + (size_t)i * sizeof(ElfW(Sym)));
        if (!sym->st_value || sym->st_name >= (ElfW(Word))(st.st_size - strtab_off))
            continue;
        if (!strcmp(strtab + sym->st_name, name)) {
            *value = sym->st_value;
            result = 0;
            break;
        }
    }
out:
    munmap(map, st.st_size);
    return result;
}


static int resolve_sym(const char* module_substring, const char* name, uint64_t* addr)
{
    uint64_t base, ts, te;
    char path[768];
    if (find_module(module_substring, &base, &ts, &te, path, sizeof(path)))
        return -1;
    int fd = open_module_file(path);
    if (fd < 0) {
        fprintf(stderr, "[Injector] cannot open module file '%s'\n", path);
        return -1;
    }
    uint64_t value;
    int rc = elf_dynsym_lookup(fd, name, &value);
    close(fd);
    if (rc) {
        fprintf(stderr, "[Injector] symbol '%s' not found in '%s'\n", name, path);
        return -1;
    }
    *addr = base + value;
    return 0;
}



static int find_syscall_gadget(uint64_t text_start, uint64_t text_end, uint64_t* gadget)
{
    char mempath[64];
    snprintf(mempath, sizeof(mempath), "/proc/%d/mem", g_pid);
    int fd = open(mempath, O_RDONLY);
    if (fd < 0)
        return -1;
    size_t len = text_end - text_start;
    unsigned char* buf = malloc(len);
    if (!buf) {
        close(fd);
        return -1;
    }
    ssize_t got = pread(fd, buf, len, (off_t)text_start);
    close(fd);
    if (got <= 0) {
        free(buf);
        return -1;
    }
    len = (size_t)got;
    static const unsigned char pattern[] = {0x0f, 0x05, 0xc3}; 
    for (size_t i = 0; i + 2 < len; ++i) {
        if (buf[i] == pattern[0] && buf[i + 1] == pattern[1] && buf[i + 2] == pattern[2]) {
            *gadget = text_start + i;
            free(buf);
            return 0;
        }
    }
    free(buf);
    return -1;
}




static int write_remote(uint64_t addr, const void* data, size_t len)
{
    struct iovec local = { (void*)data, len };
    struct iovec remote = { (void*)(uintptr_t)addr, len };
    return process_vm_writev(g_pid, &local, 1, &remote, 1, 0) == (ssize_t)len ? 0 : -1;
}

static int read_remote(uint64_t addr, void* data, size_t len)
{
    struct iovec local = { data, len };
    struct iovec remote = { (void*)(uintptr_t)addr, len };
    return process_vm_readv(g_pid, &local, 1, &remote, 1, 0) == (ssize_t)len ? 0 : -1;
}

static long peek_word(uint64_t addr)
{
    errno = 0;
    return ptrace(PTRACE_PEEKDATA, g_pid, (void*)(uintptr_t)addr, NULL);
}

static int poke_word(uint64_t addr, unsigned long val)
{
    errno = 0;
    ptrace(PTRACE_POKEDATA, g_pid, (void*)(uintptr_t)addr, (void*)val);
    return errno == 0 ? 0 : -1;
}


static int find_landing_pad(uint64_t text_start, uint64_t text_end)
{
    size_t len = (size_t)(text_end - text_start);
    if (len < 4)
        return -1;
    unsigned char* buf = malloc(len);
    if (!buf)
        return -1;
    if (read_remote(text_start, buf, len)) {
        free(buf);
        return -1;
    }
    int found = 0;
    for (size_t i = 0; i + 2 < len; ++i) {
        if (buf[i] != 0xC3)
            continue;
        unsigned char b1 = buf[i + 1], b2 = buf[i + 2];
        int is_padding = (b1 == 0xCC && b2 == 0xCC)
            || (b1 == 0x0F && b2 == 0x1F)
            || (b1 == 0x66 && (b2 == 0x0F || b2 == 0x90))
            || (b1 == 0x90 && b2 == 0x90);
        if (is_padding) {
            g_landing_addr = text_start + i + 1;
            found = 1;
            break;
        }
    }
    free(buf);
    return found ? 0 : -1;
}

static int patch_landing(void)
{
    if (g_landing_patched)
        return 0;
    long w = peek_word(g_landing_addr);
    if (w == -1 && errno)
        return -1;
    g_landing_saved_word = w;
    if (poke_word(g_landing_addr, (unsigned long)((w & ~0xffUL) | 0xCC)))
        return -1;
    g_landing_patched = 1;
    return 0;
}

static void unpatch_landing(void)
{
    if (!g_landing_patched)
        return;
    poke_word(g_landing_addr, g_landing_saved_word);
    g_landing_patched = 0;
}

static uint64_t gadget_addr;
static uint64_t syscall_fn_addr; 


static int do_round(uint64_t rip, uint64_t landing, uint64_t rdi, uint64_t rsi,
    uint64_t rax, uint64_t rdx, uint64_t r10, uint64_t r8, uint64_t r9,
    uint64_t rax_out[1], const char* what)
{
    struct user_regs_struct regs = g_saved;
    regs.rip = rip;
    
    regs.orig_rax = (unsigned long long)-1;
    
    regs.rsp = (g_saved.rsp - STACK_WINDOW) & ~0xfULL;
    regs.rsp -= 8;
    regs.rdi = rdi;
    regs.rsi = rsi;
    regs.rax = rax;
    regs.rdx = rdx;
    regs.r10 = r10;
    regs.r8 = r8;
    regs.r9 = r9;
    if (write_remote(regs.rsp, &landing, sizeof(landing)))
        return -1;
    struct iovec iov = { &regs, sizeof(regs) };
    if (ptrace(PTRACE_SETREGSET, g_pid, NT_PRSTATUS, &iov) < 0)
        return -1;

    for (;;) {
        if (ptrace(PTRACE_CONT, g_pid, NULL, NULL) < 0) {
            fprintf(stderr, "[Injector] %s: PTRACE_CONT failed (errno %d) - target %s\n",
                what, errno, kill(g_pid, 0) == 0 ? "alive" : "DIED mid-round");
            return -1;
        }
        int status;
        if (waitpid(g_pid, &status, 0) < 0) {
            fprintf(stderr, "[Injector] %s: waitpid failed (errno %d) - target %s\n",
                what, errno, kill(g_pid, 0) == 0 ? "alive" : "DIED mid-round");
            return -1;
        }
        if (!WIFSTOPPED(status)) {
            fprintf(stderr, "[Injector] %s: target %s (wait status 0x%x)\n",
                what, kill(g_pid, 0) == 0 ? "not stopped" : "DIED mid-round", status);
            return -1;
        }
        int sig = WSTOPSIG(status);
        iov.iov_base = &regs;
        if (ptrace(PTRACE_GETREGSET, g_pid, NT_PRSTATUS, &iov) < 0)
            return -1;
        if ((sig == SIGTRAP || sig == SIGSEGV) && regs.rip == landing + 1) {
            *rax_out = regs.rax;
            printf("[Injector] %-12s done, sig=%d rax=0x%llx rip=0x%llx\n", what, sig,
                (unsigned long long)regs.rax, (unsigned long long)regs.rip);
            return 0;
        }
        if (sig == SIGTRAP || sig == SIGINT || sig == SIGTERM || sig == SIGSEGV) {
            fprintf(stderr, "[Injector] %s failed: sig=%d rip=0x%llx rax=0x%llx rbp=0x%llx\n",
                what, sig, (unsigned long long)regs.rip, (unsigned long long)regs.rax,
                (unsigned long long)regs.rbp);
            return -1;
        }
        
        printf("[Injector] %s: passing signal %d through\n", what, sig);
        if (ptrace(PTRACE_CONT, g_pid, NULL, (void*)(long)sig) < 0)
            return -1;
    }
}

static int round_syscall(uint64_t nr, uint64_t rdi, uint64_t rsi, uint64_t rdx, uint64_t r10,
    uint64_t r8, uint64_t r9, uint64_t rax_out[1], const char* what)
{
    return do_round(gadget_addr, g_landing_addr, rdi, rsi, nr, rdx, r10, r8, r9, rax_out, what);
}


static int do_round32(uint32_t fn, uint32_t landing, const uint32_t* args, int nargs,
    uint32_t* eax_out, const char* what)
{
    struct i386_user_regs regs = g_saved32;
    regs.eip = fn;
    
        regs.esp = ((g_saved32.esp - STACK_WINDOW) & ~0xfu) - 4;
    
    regs.orig_eax = 0xffffffffu;
    
    uint32_t frame[1 + 7];
    int n = 1 + (nargs > 7 ? 7 : nargs);
    frame[0] = landing;
    for (int i = 1; i < n; ++i)
        frame[i] = args[i - 1];
    if (write_remote(regs.esp, frame, (size_t)n * sizeof(uint32_t)))
        return -1;
    struct iovec iov = { &regs, sizeof(regs) };
    if (ptrace(PTRACE_SETREGSET, g_pid, NT_PRSTATUS, &iov) < 0)
        return -1;

    for (;;) {
        if (ptrace(PTRACE_CONT, g_pid, NULL, NULL) < 0) {
            fprintf(stderr, "[Injector] %s: PTRACE_CONT failed (errno %d) - target %s\n",
                what, errno, kill(g_pid, 0) == 0 ? "alive" : "DIED mid-round");
            return -1;
        }
        int status;
        if (waitpid(g_pid, &status, 0) < 0) {
            fprintf(stderr, "[Injector] %s: waitpid failed (errno %d) - target %s\n",
                what, errno, kill(g_pid, 0) == 0 ? "alive" : "DIED mid-round");
            return -1;
        }
        if (!WIFSTOPPED(status)) {
            fprintf(stderr, "[Injector] %s: target %s (wait status 0x%x)\n",
                what, kill(g_pid, 0) == 0 ? "not stopped" : "DIED mid-round", status);
            return -1;
        }
        int sig = WSTOPSIG(status);
        iov.iov_base = &regs;
        if (ptrace(PTRACE_GETREGSET, g_pid, NT_PRSTATUS, &iov) < 0)
            return -1;
        if ((sig == SIGTRAP || sig == SIGSEGV) && regs.eip == landing + 1) {
            *eax_out = regs.eax;
            printf("[Injector] %-12s done, sig=%d eax=0x%x eip=0x%x\n", what, sig, regs.eax, regs.eip);
            return 0;
        }
        if (sig == SIGTRAP || sig == SIGINT || sig == SIGTERM || sig == SIGSEGV) {
            unsigned char fault_bytes[8] = {0};
            read_remote(regs.eip, fault_bytes, sizeof(fault_bytes));
            fprintf(stderr, "[Injector] %s failed: sig=%d eip=0x%x eax=0x%x"
                " ebx=0x%x esi=0x%x edi=0x%x esp=0x%x bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n",
                what, sig, regs.eip, regs.eax, regs.ebx, regs.esi, regs.edi, regs.esp,
                fault_bytes[0], fault_bytes[1], fault_bytes[2], fault_bytes[3],
                fault_bytes[4], fault_bytes[5], fault_bytes[6], fault_bytes[7]);
            return -1;
        }
        printf("[Injector] %s: passing signal %d through\n", what, sig);
        if (ptrace(PTRACE_CONT, g_pid, NULL, (void*)(long)sig) < 0)
            return -1;
    }
}


static int round_syscall32(uint32_t nr, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
    uint32_t a5, uint32_t a6, uint32_t* eax_out, const char* what)
{
    uint32_t args[7] = { nr, a1, a2, a3, a4, a5, a6 };
    return do_round32((uint32_t)syscall_fn_addr, (uint32_t)g_landing_addr, args, 7, eax_out, what);
}

static void save_state(void)
{
    struct iovec iov = { g_xstate, sizeof(g_xstate) };
    if (ptrace(PTRACE_GETREGSET, g_pid, NT_X86_XSTATE, &iov) == 0)
        g_xstate_valid = 1;
    else
        printf("[Injector] note: XSAVE state capture failed (errno %d), FPU regs won't be restored\n", errno);

    if (g_arch32) {
        
        g_stack_window_start = ((g_saved32.esp - STACK_WINDOW) & ~0xfu) - 4;
    } else {
        g_stack_window_start = ((g_saved.rsp - STACK_WINDOW) & ~0xfULL) - 8;
    }
    if (read_remote(g_stack_window_start, g_stack_backup, STACK_WINDOW + 8) == 0)
        g_stack_backup_valid = 1;
    else
        printf("[Injector] note: could not back up the stack window\n");
}

static void die_restore(void)
{
    unpatch_landing();
    if (g_stack_backup_valid)
        write_remote(g_stack_window_start, g_stack_backup, STACK_WINDOW + 8);
    if (g_arch32) {
        struct iovec iov32 = { &g_saved32, sizeof(g_saved32) };
        if (g_xstate_valid)
            ptrace(PTRACE_SETREGSET, g_pid, NT_X86_XSTATE, &iov32);
        ptrace(PTRACE_SETREGSET, g_pid, NT_PRSTATUS, &iov32);
    } else {
        struct iovec iov = { &g_saved, sizeof(g_saved) };
        if (g_xstate_valid)
            ptrace(PTRACE_SETREGSET, g_pid, NT_X86_XSTATE, &iov);
        ptrace(PTRACE_SETREGSET, g_pid, NT_PRSTATUS, &iov);
    }
    ptrace(PTRACE_DETACH, g_pid, NULL, NULL);
    exit(1);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <pid> <library.so>\n", argv[0]);
        return 1;
    }
    if (geteuid() != 0) {
        fprintf(stderr, "[Injector] Need root\n");
        return 1;
    }

    g_pid = (pid_t)atoi(argv[1]);
    const char* lib_path = argv[2];
    int read_stdin = strcmp(lib_path, "-") == 0;
    if (g_pid <= 0 || kill(g_pid, 0) != 0) {
        fprintf(stderr, "[Injector] PID %d is not a live process\n", g_pid);
        return 1;
    }
    if (!read_stdin && access(lib_path, R_OK) != 0) {
        fprintf(stderr, "[Injector] library not readable: %s\n", lib_path);
        return 1;
    }

    printf("[Injector] Target: %d, Library: %s\n", g_pid, lib_path);

    
    int cls = target_elf_class();
    if (cls < 0) {
        fprintf(stderr, "[Injector] cannot read the target's ELF class (/proc/%d/exe)\n", g_pid);
        return 1;
    }
    g_arch32 = cls;
    printf("[Injector] target is %s\n", g_arch32 ? "i386 (32-bit)" : "x86-64");

    uint64_t libc_base, libc_text_start, libc_text_end;
    char libc_path[768];
    if (find_module("libc.so.6", &libc_base, &libc_text_start, &libc_text_end, libc_path, sizeof(libc_path))) {
        fprintf(stderr, "[Injector] libc not found in target maps\n");
        return 1;
    }
    printf("[Injector] libc base 0x%llx text 0x%llx-0x%llx file %s\n",
        (unsigned long long)libc_base, (unsigned long long)libc_text_start,
        (unsigned long long)libc_text_end, libc_path);

    uint64_t dlopen_addr = 0, dlerror_addr = 0;
    if (resolve_sym("libc.so.6", "dlopen", &dlopen_addr)
        || resolve_sym("libc.so.6", "dlerror", &dlerror_addr)) {
        
        if (resolve_sym("libdl.so.2", "dlopen", &dlopen_addr) || resolve_sym("libdl.so.2", "dlerror", &dlerror_addr)) {
            fprintf(stderr, "[Injector] could not resolve dlopen/dlerror in target\n");
            return 1;
        }
    }
    printf("[Injector] dlopen 0x%llx dlerror 0x%llx\n",
        (unsigned long long)dlopen_addr, (unsigned long long)dlerror_addr);

    if (g_arch32) {
        
        if (resolve_sym("libc.so.6", "syscall", &syscall_fn_addr)) {
            fprintf(stderr, "[Injector] cannot resolve syscall() in the i386 target libc\n");
            return 1;
        }
        printf("[Injector] syscall wrapper: 0x%llx\n", (unsigned long long)syscall_fn_addr);
    } else if (find_syscall_gadget(libc_text_start, libc_text_end, &gadget_addr)) {
        fprintf(stderr, "[Injector] no 'syscall; ret' gadget in target libc text\n");
        return 1;
    } else {
        printf("[Injector] syscall gadget: 0x%llx\n", (unsigned long long)gadget_addr);
    }

    
    if (find_landing_pad(libc_text_start, libc_text_end)) {
        fprintf(stderr, "[Injector] no ret+int3 padding landing pad found in target libc text\n");
        return 1;
    }
    printf("[Injector] landing pad: 0x%llx (alignment padding after a ret; other threads never execute it)\n",
        (unsigned long long)g_landing_addr);

    
    unsigned char probe[8];
    if (read_remote(dlopen_addr, probe, sizeof(probe))) {
        fprintf(stderr, "[Injector] cannot read target memory (pread /proc/%d/mem)\n", g_pid);
        return 1;
    }
    printf("[Injector] bytes at dlopen: %02x %02x %02x %02x %02x %02x %02x %02x\n",
        probe[0], probe[1], probe[2], probe[3], probe[4], probe[5], probe[6], probe[7]);

    
    printf("[Injector] Attaching...\n");
    if (ptrace(PTRACE_ATTACH, g_pid, NULL, NULL) < 0) {
        perror("PTRACE_ATTACH");
        return 1;
    }
    int status;
    if (waitpid(g_pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }
    if (g_arch32) {
        struct iovec iov32 = { &g_saved32, sizeof(g_saved32) };
        if (ptrace(PTRACE_GETREGSET, g_pid, NT_PRSTATUS, &iov32) < 0) {
            perror("GETREGSET");
            return 1;
        }
        printf("[Injector] attached at eip 0x%x esp 0x%x\n", g_saved32.eip, g_saved32.esp);
    } else {
        struct iovec iov = { &g_saved, sizeof(g_saved) };
        if (ptrace(PTRACE_GETREGSET, g_pid, NT_PRSTATUS, &iov) < 0) {
            perror("GETREGSET");
            return 1;
        }
        printf("[Injector] attached at rip 0x%llx rsp 0x%llx\n",
            (unsigned long long)g_saved.rip, (unsigned long long)g_saved.rsp);
    }
    save_state();
    if (!g_stack_backup_valid) {
        fprintf(stderr, "[Injector] cannot back up the stack window, aborting\n");
        ptrace(PTRACE_DETACH, g_pid, NULL, NULL);
        return 1;
    }

    
    if (patch_landing())
        die_restore();
    uint64_t rax = 0;
    if (g_arch32) {
        uint32_t eax = 0;
        
        if (round_syscall32(SYS_I386_mmap2, 0, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_PRIVATE | MAP_ANONYMOUS, 0xffffffffu, 0, &eax, "mmap")
            || eax < 0x1000 || eax >= 0xfffff000u) {
            fprintf(stderr, "[Injector] remote mmap failed (eax=0x%x)\n", eax);
            die_restore();
        }
        rax = eax;
    } else if (round_syscall(SYS_mmap, 0, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS, (uint64_t)-1, 0, &rax, "mmap")
        || (int64_t)rax < 0) {
        fprintf(stderr, "[Injector] remote mmap failed (rax=0x%llx)\n", (unsigned long long)rax);
        die_restore();
    }
    g_page = rax;

    
    static const char memfd_name[] = "libMangoHud.so";
    if (write_remote(g_page + PAGE_NAME_OFF, memfd_name, sizeof(memfd_name))) {
        fprintf(stderr, "[Injector] cannot write the memfd name into the helper page\n");
        die_restore();
    }
    if (g_arch32) {
        uint32_t eax = 0;
        if (round_syscall32(SYS_I386_memfd_create, (uint32_t)(g_page + PAGE_NAME_OFF), MFD_CLOEXEC,
                0, 0, 0, 0, &eax, "memfd_create")
            || eax > 0xffff) {  
            fprintf(stderr, "[Injector] remote memfd_create failed (eax=0x%x)\n", eax);
            die_restore();
        }
        rax = eax;
    } else if (round_syscall(SYS_memfd_create, g_page + PAGE_NAME_OFF, MFD_CLOEXEC, 0, 0, 0, 0, &rax, "memfd_create")
        || (int64_t)rax < 0 || rax > 0xffff) {
        fprintf(stderr, "[Injector] remote memfd_create failed (rax=0x%llx)\n", (unsigned long long)rax);
        die_restore();
    }
    long target_fd = (long)rax;

    
    char fdpath[64];
    snprintf(fdpath, sizeof(fdpath), "/proc/%d/fd/%ld", g_pid, target_fd);
    
    int libfd = read_stdin ? STDIN_FILENO : open(lib_path, O_RDONLY);
    int memfd = open(fdpath, O_WRONLY);
    if (libfd < 0 || memfd < 0) {
        fprintf(stderr, "[Injector] cannot open %s (%s)\n", fdpath, strerror(errno));
        die_restore();
    }
    char buf[0x8000];
    ssize_t n;
    off_t total = 0;
    while ((n = read(libfd, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(memfd, buf + written, n - written);
            if (w < 0) {
                fprintf(stderr, "[Injector] memfd write failed: %s\n", strerror(errno));
                die_restore();
            }
            written += w;
        }
        total += n;
    }
    close(libfd);
    close(memfd);
    printf("[Injector] wrote %ld bytes into target memfd (fd %ld: %s)\n", (long)total, target_fd, fdpath);


    
    char dlopen_path[64];
    snprintf(dlopen_path, sizeof(dlopen_path), "/proc/%d/fd/%ld", g_pid, target_fd);
    if (write_remote(g_page + PAGE_PATH_OFF, dlopen_path, strlen(dlopen_path) + 1)
        || write_remote(g_page, "\xCC", 1)) {
        fprintf(stderr, "[Injector] page write failed\n");
        die_restore();
    }
    printf("[Injector] Calling dlopen(\"%s\", RTLD_LAZY)...\n", dlopen_path);
    if (g_arch32) {
        uint32_t handle32 = 0;
        const uint32_t dlopen_args[2] = { (uint32_t)(g_page + PAGE_PATH_OFF), RTLD_LAZY };
        if (do_round32((uint32_t)dlopen_addr, (uint32_t)g_page, dlopen_args, 2, &handle32, "dlopen")) {
            fprintf(stderr, "[Injector] dlopen round failed\n");
            die_restore();
        }
        rax = handle32;
    } else if (do_round(dlopen_addr, g_page, g_page + PAGE_PATH_OFF, RTLD_LAZY, 0, 0, 0, 0, 0, &rax, "dlopen")) {
        fprintf(stderr, "[Injector] dlopen round failed\n");
        die_restore();
    }
    uint64_t handle = rax;

    if (!handle) {
        
        uint64_t errptr = 0;
        if (g_arch32) {
            uint32_t errptr32 = 0;
            if (do_round32((uint32_t)dlerror_addr, (uint32_t)g_landing_addr, NULL, 0, &errptr32, "dlerror") == 0
                && errptr32) {
                char err[256] = { 0 };
                if (read_remote(errptr32, err, sizeof(err) - 1) == 0)
                    fprintf(stderr, "[Injector] dlopen error: %s\n", err);
            }
        } else if (do_round(dlerror_addr, g_landing_addr, 0, 0, 0, 0, 0, 0, 0, &errptr, "dlerror") == 0 && errptr) {
            char err[256] = { 0 };
            if (read_remote(errptr, err, sizeof(err) - 1) == 0)
                fprintf(stderr, "[Injector] dlopen error: %s\n", err);
        }
        fprintf(stderr, "[Injector] dlopen returned NULL\n");
        die_restore();
    }

    printf("[Injector] dlopen returned handle: 0x%llx\n", (unsigned long long)handle);

    
    if (g_arch32) {
        uint32_t eax = 0;
        if (round_syscall32(SYS_I386_munmap, (uint32_t)g_page, 0x1000, 0, 0, 0, 0, &eax, "munmap"))
            printf("[Injector] note: munmap round failed, helper page stays\n");
    } else if (round_syscall(SYS_munmap, g_page, 0x1000, 0, 0, 0, 0, &rax, "munmap")) {
        printf("[Injector] note: munmap round failed, helper page stays\n");
    }

    
    unpatch_landing();
    write_remote(g_stack_window_start, g_stack_backup, STACK_WINDOW + 8);
    if (g_arch32) {
        struct iovec iov32 = { g_xstate, sizeof(g_xstate) };
        if (g_xstate_valid)
            ptrace(PTRACE_SETREGSET, g_pid, NT_X86_XSTATE, &iov32);
        iov32.iov_base = &g_saved32;
        iov32.iov_len = sizeof(g_saved32);
        ptrace(PTRACE_SETREGSET, g_pid, NT_PRSTATUS, &iov32);
    } else {
        struct iovec iov = { g_xstate, sizeof(g_xstate) };
        if (g_xstate_valid)
            ptrace(PTRACE_SETREGSET, g_pid, NT_X86_XSTATE, &iov);
        iov.iov_base = &g_saved;
        iov.iov_len = sizeof(g_saved);
        ptrace(PTRACE_SETREGSET, g_pid, NT_PRSTATUS, &iov);
    }
    ptrace(PTRACE_DETACH, g_pid, NULL, NULL);
    printf("[Injector] state restored, detached\n");
    return 0;
}
