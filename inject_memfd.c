/*
 * Stealthy memfd injector for CS2 (Linux).
 *
 * Creates the memfd INSIDE the target via a short remote-syscall round, writes
 * the library into it, then dlopen()s /proc/<pid>/fd/<n> on the target's main
 * thread. No file ever touches disk; the memfd stays alive in the target's fd
 * table after this injector exits.
 *
 * Remote-call mechanics:
 *  - short calls (remote mmap / memfd_create / dlerror / munmap) land on an
 *    int3 patched over the target's getpid() (microsecond window, then unpatch)
 *  - the long dlopen() call lands on an int3 in a remote mmap'd RWX page, so
 *    no breakpoint is exposed to the game's other threads while dlopen runs
 *  - the stack window we borrow below the interrupted rsp is saved before
 *    poking and written back before resuming (protects the interrupted
 *    function's red zone / locals)
 *  - GPRs and XSAVE state (NT_X86_XSTATE) are restored before detach
 *
 * dlopen/getpid/dlerror are resolved by parsing the .dynsym of the libc file
 * the TARGET actually has mapped (works for glibc >= 2.34 where libdl is
 * merged into libc, and for container-mapped libc like /run/host/...), and
 * falls back to libdl.so.2 for older glibc. A `syscall; ret` gadget is found
 * by scanning the target's libc text via /proc/pid/mem.
 *
 * Usage: inject_memfd <pid> <library.so>   (root)
 */

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
static int g_getpid_patched;
static uint64_t g_getpid_addr;
static long g_getpid_saved_word;
static uint64_t g_stack_window_start;
static unsigned char g_stack_backup[STACK_WINDOW + 8];
static int g_stack_backup_valid;
static uint64_t g_page;

static void die_restore(void) __attribute__((noreturn));

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

/* ---- maps parsing ------------------------------------------------------- */

/* Find first r-xp text range + load base of a module substring in the target. */
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

/* maps paths seen from inside a container (/run/host/...) may not exist for
 * us - fall back to the host path by stripping the prefix. */
static int open_module_file(const char* maps_path)
{
    int fd = open(maps_path, O_RDONLY);
    if (fd >= 0)
        return fd;
    if (!strncmp(maps_path, "/run/host/", 10))
        return open(maps_path + 10, O_RDONLY);
    return -1;
}

/* ---- ELF .dynsym lookup -------------------------------------------------- */

/* Translate an image vaddr to a file offset via PT_LOAD headers. */
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

/* st_value (image vaddr) of an exported symbol in an ELF file. */
static int elf_dynsym_lookup(int fd, const char* name, uint64_t* value)
{
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < (off_t)sizeof(ElfW(Ehdr)))
        return -1;

    void* map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED)
        return -1;
    int result = -1;
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

    /* symbol count from DT_GNU_HASH: highest bucket index, then walk its
     * chain until the terminator bit */
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

/* Resolve a symbol against the module the TARGET has mapped. */
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

/* ---- gadget scan --------------------------------------------------------- */

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
    static const unsigned char pattern[] = {0x0f, 0x05, 0xc3}; /* syscall; ret */
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

/* ---- ptrace state --------------------------------------------------------- */

static int patch_getpid(void)
{
    if (g_getpid_patched)
        return 0;
    long w = peek_word(g_getpid_addr);
    if (w == -1 && errno)
        return -1;
    g_getpid_saved_word = w;
    if (poke_word(g_getpid_addr, (unsigned long)((w & ~0xffUL) | 0xCC)))
        return -1;
    g_getpid_patched = 1;
    return 0;
}

static void unpatch_getpid(void)
{
    if (!g_getpid_patched)
        return;
    poke_word(g_getpid_addr, (unsigned long)g_getpid_saved_word);
    g_getpid_patched = 0;
}

/* Write 8 bytes to target memory via process_vm_writev (writable pages only). */
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

static uint64_t gadget_addr;

/*
 * One remote round: set registers from the saved snapshot + call frame,
 * continue, wait for the stop at the landing int3, return rax.
 * Returns 0 on success (either SIGTRAP or SIGSEGV exactly at landing+1).
 * `sig_out` receives the stop signal; other signals are re-injected and
 * the round keeps waiting (mirrors GDB signal passing).
 */
static int do_round(uint64_t rip, uint64_t landing, uint64_t rdi, uint64_t rsi,
    uint64_t rax, uint64_t rdx, uint64_t r10, uint64_t r8, uint64_t r9,
    uint64_t rax_out[1], const char* what)
{
    struct user_regs_struct regs = g_saved;
    regs.rip = rip;
    /* return address slot: entry rsp must be 8 mod 16 (as after a real call
     * push) or the callee's aligned SSE stores movaps-fault */
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
        if (ptrace(PTRACE_CONT, g_pid, NULL, NULL) < 0)
            return -1;
        int status;
        if (waitpid(g_pid, &status, 0) < 0)
            return -1;
        if (!WIFSTOPPED(status))
            return -1;
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
        /* unrelated signal delivered to this thread mid-call: pass it through */
        printf("[Injector] %s: passing signal %d through\n", what, sig);
        if (ptrace(PTRACE_CONT, g_pid, NULL, (void*)(long)sig) < 0)
            return -1;
    }
}

static int round_syscall(uint64_t nr, uint64_t rdi, uint64_t rsi, uint64_t rdx, uint64_t r10,
    uint64_t r8, uint64_t r9, uint64_t rax_out[1], const char* what)
{
    return do_round(gadget_addr, g_getpid_addr, rdi, rsi, nr, rdx, r10, r8, r9, rax_out, what);
}

static void save_state(void)
{
    struct iovec iov = { g_xstate, sizeof(g_xstate) };
    if (ptrace(PTRACE_GETREGSET, g_pid, NT_X86_XSTATE, &iov) == 0)
        g_xstate_valid = 1;
    else
        printf("[Injector] note: XSAVE state capture failed (errno %d), FPU regs won't be restored\n", errno);

    g_stack_window_start = ((g_saved.rsp - STACK_WINDOW) & ~0xfULL) - 8;
    if (read_remote(g_stack_window_start, g_stack_backup, STACK_WINDOW + 8) == 0)
        g_stack_backup_valid = 1;
    else
        printf("[Injector] note: could not back up the stack window\n");
}

static void die_restore(void)
{
    unpatch_getpid();
    if (g_stack_backup_valid)
        write_remote(g_stack_window_start, g_stack_backup, STACK_WINDOW + 8);
    struct iovec iov = { &g_saved, sizeof(g_saved) };
    if (g_xstate_valid)
        ptrace(PTRACE_SETREGSET, g_pid, NT_X86_XSTATE, &iov);
    ptrace(PTRACE_SETREGSET, g_pid, NT_PRSTATUS, &iov);
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
    if (g_pid <= 0 || kill(g_pid, 0) != 0) {
        fprintf(stderr, "[Injector] PID %d is not a live process\n", g_pid);
        return 1;
    }
    if (access(lib_path, R_OK) != 0) {
        fprintf(stderr, "[Injector] library not readable: %s\n", lib_path);
        return 1;
    }

    printf("[Injector] Target: %d, Library: %s\n", g_pid, lib_path);

    uint64_t libc_base, libc_text_start, libc_text_end;
    char libc_path[768];
    if (find_module("libc.so.6", &libc_base, &libc_text_start, &libc_text_end, libc_path, sizeof(libc_path))) {
        fprintf(stderr, "[Injector] libc not found in target maps\n");
        return 1;
    }
    printf("[Injector] libc base 0x%llx text 0x%llx-0x%llx file %s\n",
        (unsigned long long)libc_base, (unsigned long long)libc_text_start,
        (unsigned long long)libc_text_end, libc_path);

    if (find_syscall_gadget(libc_text_start, libc_text_end, &gadget_addr)) {
        fprintf(stderr, "[Injector] no 'syscall; ret' gadget in target libc text\n");
        return 1;
    }
    printf("[Injector] syscall gadget: 0x%llx\n", (unsigned long long)gadget_addr);

    uint64_t dlopen_addr = 0, dlerror_addr = 0;
    if (resolve_sym("libc.so.6", "getpid", &g_getpid_addr)
        || resolve_sym("libc.so.6", "dlopen", &dlopen_addr)
        || resolve_sym("libc.so.6", "dlerror", &dlerror_addr)) {
        /* glibc < 2.34 keeps these in libdl */
        if (resolve_sym("libdl.so.2", "dlopen", &dlopen_addr) || resolve_sym("libdl.so.2", "dlerror", &dlerror_addr)) {
            fprintf(stderr, "[Injector] could not resolve dlopen/dlerror in target\n");
            return 1;
        }
    }
    printf("[Injector] dlopen 0x%llx dlerror 0x%llx getpid 0x%llx\n",
        (unsigned long long)dlopen_addr, (unsigned long long)dlerror_addr, (unsigned long long)g_getpid_addr);

    /* sanity-check the resolved dlopen entry looks like a function prologue */
    unsigned char probe[8];
    if (read_remote(dlopen_addr, probe, sizeof(probe))) {
        fprintf(stderr, "[Injector] cannot read target memory (pread /proc/%d/mem)\n", g_pid);
        return 1;
    }
    printf("[Injector] bytes at dlopen: %02x %02x %02x %02x %02x %02x %02x %02x\n",
        probe[0], probe[1], probe[2], probe[3], probe[4], probe[5], probe[6], probe[7]);

    /* ---- attach ---- */
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
    struct iovec iov = { &g_saved, sizeof(g_saved) };
    if (ptrace(PTRACE_GETREGSET, g_pid, NT_PRSTATUS, &iov) < 0) {
        perror("GETREGSET");
        return 1;
    }
    printf("[Injector] attached at rip 0x%llx rsp 0x%llx\n",
        (unsigned long long)g_saved.rip, (unsigned long long)g_saved.rsp);
    save_state();
    if (!g_stack_backup_valid) {
        fprintf(stderr, "[Injector] cannot back up the stack window, aborting\n");
        ptrace(PTRACE_DETACH, g_pid, NULL, NULL);
        return 1;
    }

    /* ---- remote mmap (RWX page: landing pad + strings) ---- */
    if (patch_getpid())
        die_restore();
    uint64_t rax = 0;
    if (round_syscall(SYS_mmap, 0, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS, (uint64_t)-1, 0, &rax, "mmap")
        || (int64_t)rax < 0) {
        fprintf(stderr, "[Injector] remote mmap failed (rax=0x%llx)\n", (unsigned long long)rax);
        die_restore();
    }
    g_page = rax;

    /* ---- remote memfd_create in the target ---- */
    static const char memfd_name[] = "libutil_helper.so";
    if (write_remote(g_page + PAGE_NAME_OFF, memfd_name, sizeof(memfd_name))
        || round_syscall(SYS_memfd_create, g_page + PAGE_NAME_OFF, MFD_CLOEXEC, 0, 0, 0, 0, &rax, "memfd_create")
        || (int64_t)rax < 0 || rax > 0xffff) {
        fprintf(stderr, "[Injector] remote memfd_create failed (rax=0x%llx)\n", (unsigned long long)rax);
        die_restore();
    }
    long target_fd = (long)rax;
    /* getpid int3 must not be exposed during the long dlopen round */
    unpatch_getpid();

    /* ---- write the library into the target's memfd ---- */
    char fdpath[64];
    snprintf(fdpath, sizeof(fdpath), "/proc/%d/fd/%ld", g_pid, target_fd);
    int libfd = open(lib_path, O_RDONLY);
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

    /* ---- dlopen on the target thread ---- */
    char dlopen_path[64];
    snprintf(dlopen_path, sizeof(dlopen_path), "/proc/%d/fd/%ld", g_pid, target_fd);
    if (write_remote(g_page + PAGE_PATH_OFF, dlopen_path, strlen(dlopen_path) + 1)
        || write_remote(g_page, "\xCC", 1)) {
        fprintf(stderr, "[Injector] page write failed\n");
        die_restore();
    }
    printf("[Injector] Calling dlopen(\"%s\", RTLD_LAZY)...\n", dlopen_path);
    if (do_round(dlopen_addr, g_page, g_page + PAGE_PATH_OFF, RTLD_LAZY, 0, 0, 0, 0, 0, &rax, "dlopen")) {
        fprintf(stderr, "[Injector] dlopen round failed\n");
        die_restore();
    }
    uint64_t handle = rax;

    if (!handle) {
        /* dlerror on the target for the actual reason */
        patch_getpid();
        uint64_t errptr = 0;
        if (do_round(dlerror_addr, g_getpid_addr, 0, 0, 0, 0, 0, 0, 0, &errptr, "dlerror") == 0 && errptr) {
            char err[256] = { 0 };
            if (read_remote(errptr, err, sizeof(err) - 1) == 0)
                fprintf(stderr, "[Injector] dlopen error: %s\n", err);
        }
        fprintf(stderr, "[Injector] dlopen returned NULL\n");
        die_restore();
    }

    printf("[Injector] dlopen returned handle: 0x%llx\n", (unsigned long long)handle);

    /* ---- cleanup: drop the helper page ---- */
    patch_getpid();
    if (round_syscall(SYS_munmap, g_page, 0x1000, 0, 0, 0, 0, &rax, "munmap"))
        printf("[Injector] note: munmap round failed, helper page stays\n");
    unpatch_getpid();

    /* ---- restore everything and detach ---- */
    write_remote(g_stack_window_start, g_stack_backup, STACK_WINDOW + 8);
    iov.iov_base = g_xstate;
    iov.iov_len = sizeof(g_xstate);
    if (g_xstate_valid)
        ptrace(PTRACE_SETREGSET, g_pid, NT_X86_XSTATE, &iov);
    iov.iov_base = &g_saved;
    iov.iov_len = sizeof(g_saved);
    ptrace(PTRACE_SETREGSET, g_pid, NT_PRSTATUS, &iov);
    ptrace(PTRACE_DETACH, g_pid, NULL, NULL);
    printf("[Injector] state restored, detached\n");
    return 0;
}
