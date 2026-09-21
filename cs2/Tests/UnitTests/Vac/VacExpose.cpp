// See VacExpose.h. Pure analysis tool: only reads /proc, ELF files and
// target memory. No hooks, no patching, no writes except the test-only
// synthetic patch the harness applies to itself.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "VacExpose.h"

#include <elf.h>
#include <dirent.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace expose
{
namespace
{

bool read_mem(pid_t pid, uintptr_t addr, void* out, size_t n, bool& ok)
{
    if (is_self(pid)) {
        // Fault-tolerant: a scanner routinely probes addresses that turn
        // out unmapped (stale bias, vsyscall-style gaps). process_vm_readv
        // against our own pid returns short instead of SIGSEGV.
        struct iovec local{out, n};
        struct iovec remote{reinterpret_cast<void*>(addr), n};
        const ssize_t r = ::process_vm_readv(::getpid(), &local, 1, &remote, 1, 0);
        ok = (r == static_cast<ssize_t>(n));
        return ok;
    }
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", (int)pid);
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        ok = false;
        return false;
    }
    size_t got = 0;
    while (got < n) {
        const ssize_t r = ::pread(fd, static_cast<char*>(out) + got, n - got,
                                  static_cast<off_t>(addr + got));
        if (r <= 0) {
            ::close(fd);
            ok = false;
            return false;
        }
        got += static_cast<size_t>(r);
    }
    ::close(fd);
    ok = true;
    return true;
}

std::string basename_of(const std::string& p)
{
    const size_t i = p.find_last_of('/');
    return (i == std::string::npos) ? p : p.substr(i + 1);
}

// Modules JMP_SLOTs may legitimately point into without suspicion: the
// platform loader set, plus the module itself (handled by the caller via
// exact path compare). Version suffixes vary by distro (libgcc_s.so.1 vs
// libgcc_s-16-....so.1), so match on the soname stem, not equality.
// Anything else hosting executable imports is worth a look;
// anonymous/deleted/unmapped targets are critical. Heuristic, by design -
// same caveat as the Windows helper's trusted list.
bool host_is_usual(const std::string& base)
{
    if (base == "[vdso]" || base == "[vvar]")
        return true;
    static const char* const stems[] = {"libc.so",   "libm.so",    "libpthread.so",
                                        "libdl.so",  "libgcc_s",   "libstdc++",
                                        "ld-linux",  "linux-vdso", "librt.so",
                                        "libresolv"};
    for (const char* s : stems) {
        if (base.compare(0, strlen(s), s) == 0)
            return true;
    }
    return false;
}

const MapEntry* containing(const std::vector<MapEntry>& maps, uintptr_t addr)
{
    for (const auto& m : maps) {
        if (addr >= m.start && addr < m.end)
            return &m;
    }
    return nullptr;
}

struct FileImage {
    std::vector<unsigned char> bytes;
    bool ok = false;
};

FileImage read_file(const std::string& path)
{
    FileImage img;
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp)
        return img;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        img.bytes.insert(img.bytes.end(), buf, buf + n);
    fclose(fp);
    img.ok = true;
    return img;
}

} // namespace

bool is_self(pid_t pid) noexcept
{
    return pid <= 0 || pid == ::getpid();
}

std::vector<MapEntry> parse_maps(pid_t pid)
{
    std::vector<MapEntry> out;
    char path[64];
    if (is_self(pid))
        snprintf(path, sizeof(path), "/proc/self/maps");
    else
        snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid);
    FILE* fp = fopen(path, "r");
    if (!fp)
        return out;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        MapEntry e;
        char perms[8]{};
        char pbuf[768]{};
        unsigned long long off = 0;
        const int n = sscanf(line, "%lx-%lx %7s %llx %*x:%*x %*u %767[^\n]", &e.start,
                             &e.end, perms, &off, pbuf);
        if (n < 3)
            continue;
        e.perms = perms;
        e.offset = off;
        if (n >= 4) {
            // Strip leading spaces from the path field.
            const char* p = pbuf;
            while (*p == ' ' || *p == '\t')
                ++p;
            e.path = p;
        }
        out.push_back(e);
    }
    fclose(fp);
    return out;
}

std::vector<ExecRegion> scan_exec_regions(pid_t pid)
{
    std::vector<ExecRegion> out;
    for (const auto& m : parse_maps(pid)) {
        if (m.perms.size() < 3 || m.perms[2] != 'x')
            continue;
        const bool anon = m.path.empty() || m.path[0] == '[' ||
                          m.path.compare(0, 7, "/memfd:") == 0 ||
                          m.path.find("(deleted)") != std::string::npos;
        if (!anon)
            continue;
        // Kernel/loader artifacts ([vdso]/[vsyscall]/[vvar]/[stack]/...) can
        // never be cheat mappings; the interesting anon set is empty-path
        // (JITs, manual maps) and memfd/deleted file mappings.
        if (!m.path.empty() && m.path[0] == '[')
            continue;
        ExecRegion r;
        r.start = m.start;
        r.end = m.end;
        r.perms = m.perms;
        r.path = m.path;
        r.rwx = m.perms.size() > 1 && m.perms[1] == 'w';
        out.push_back(r);
    }
    return out;
}

std::vector<ThreadInfo> scan_threads(pid_t pid)
{
    std::vector<ThreadInfo> out;
    char dir[64];
    if (is_self(pid))
        snprintf(dir, sizeof(dir), "/proc/self/task");
    else
        snprintf(dir, sizeof(dir), "/proc/%d/task", (int)pid);
    DIR* dh = opendir(dir);
    if (!dh)
        return out;
    while (const dirent* de = readdir(dh)) {
        if (de->d_name[0] == '.')
            continue;
        ThreadInfo t;
        t.tid = atoi(de->d_name);
        char comm[256];
        snprintf(comm, sizeof(comm), "%s/%s/comm", dir, de->d_name);
        FILE* fp = fopen(comm, "r");
        if (fp) {
            char buf[256]{};
            if (fgets(buf, sizeof(buf), fp)) {
                buf[strcspn(buf, "\n")] = '\0';
                t.comm = buf;
            }
            fclose(fp);
        }
        out.push_back(t);
    }
    closedir(dh);
    return out;
}

bool module_bias(pid_t pid, const char* substr, uintptr_t& bias, std::string& path)
{
    if (!substr || !*substr)
        return false;
    bias = 0;
    path.clear();
    for (const auto& m : parse_maps(pid)) {
        if (m.offset == 0 && m.path.find(substr) != std::string::npos) {
            if (bias == 0 || m.start < bias) {
                bias = m.start;
                path = m.path;
            }
        }
    }
    // Fallback: lowest mapping with that path when no offset-0 line exists.
    if (bias == 0) {
        for (const auto& m : parse_maps(pid)) {
            if (m.path.find(substr) != std::string::npos) {
                if (bias == 0 || m.start < bias) {
                    bias = m.start;
                    path = m.path;
                }
            }
        }
    }
    return bias != 0;
}

GotReport audit_got(pid_t pid, const char* modsubstr)
{
    GotReport rep;
    uintptr_t map_base = 0;
    std::string path;
    if (!module_bias(pid, modsubstr, map_base, rep.module))
        return rep;
    path = rep.module;

    const FileImage img = read_file(path);
    if (!img.ok || img.bytes.size() < sizeof(Elf64_Ehdr))
        return rep;
    const Elf64_Ehdr* eh = reinterpret_cast<const Elf64_Ehdr*>(img.bytes.data());
    if (memcmp(eh->e_ident, "\x7F" "ELF", 4) != 0)
        return rep;
    // Non-PIE executables link at absolute addresses: adding the maps base
    // would double-count (observed: every slot unreadable). DSOs/PIE need it.
    const uintptr_t bias = (eh->e_type == ET_DYN) ? map_base : 0;
    const Elf64_Phdr* ph =
        reinterpret_cast<const Elf64_Phdr*>(img.bytes.data() + eh->e_phoff);

    // Locate PT_DYNAMIC in FILE offsets, then table pointers.
    const Elf64_Dyn* dyn = nullptr;
    size_t dyn_count = 0;
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_DYNAMIC) {
            dyn = reinterpret_cast<const Elf64_Dyn*>(img.bytes.data() + ph[i].p_offset);
            dyn_count = ph[i].p_filesz / sizeof(Elf64_Dyn);
            break;
        }
    }
    if (!dyn)
        return rep;

    // Translate image vaddrs to file offsets via PT_LOAD.
    auto to_off = [&](uint64_t v, uint64_t& off) {
        for (int i = 0; i < eh->e_phnum; ++i) {
            if (ph[i].p_type != PT_LOAD)
                continue;
            if (v >= ph[i].p_vaddr && v < ph[i].p_vaddr + ph[i].p_filesz) {
                off = v - ph[i].p_vaddr + ph[i].p_offset;
                return true;
            }
        }
        return false;
    };

    uint64_t sym_v = 0, str_v = 0, jmp_v = 0, pltsz = 0;
    bool rela = true;
    for (size_t i = 0; i < dyn_count; ++i) {
        if (dyn[i].d_tag == DT_NULL)
            break;
        switch (dyn[i].d_tag) {
        case DT_SYMTAB:
            sym_v = dyn[i].d_un.d_ptr;
            break;
        case DT_STRTAB:
            str_v = dyn[i].d_un.d_ptr;
            break;
        case DT_JMPREL:
            jmp_v = dyn[i].d_un.d_ptr;
            break;
        case DT_PLTRELSZ:
            pltsz = dyn[i].d_un.d_val;
            break;
        case DT_PLTREL:
            rela = (dyn[i].d_un.d_val == DT_RELA);
            break;
        default:
            break;
        }
    }
    if (!sym_v || !str_v || !jmp_v || !pltsz)
        return rep;

    // String table size: cap the read (names are short; 1MB is plenty).
    uint64_t sym_off = 0, str_off = 0, jmp_off = 0;
    if (!to_off(sym_v, sym_off) || !to_off(str_v, str_off) || !to_off(jmp_v, jmp_off))
        return rep;

    const auto maps = parse_maps(pid);
    const size_t step = rela ? sizeof(Elf64_Rela) : sizeof(Elf64_Rel);
    for (size_t j = 0; j + step <= pltsz; j += step) {
        const unsigned char* r = img.bytes.data() + jmp_off + j;
        uint64_t r_offset, r_info;
        if (rela) {
            const Elf64_Rela* ra = reinterpret_cast<const Elf64_Rela*>(r);
            r_offset = ra->r_offset;
            r_info = ra->r_info;
        } else {
            const Elf64_Rel* rl = reinterpret_cast<const Elf64_Rel*>(r);
            r_offset = rl->r_offset;
            r_info = rl->r_info;
        }
        const size_t sym_idx = (size_t)ELF64_R_SYM(r_info);
        uint64_t sym_fo = sym_off + sym_idx * sizeof(Elf64_Sym);
        if (sym_fo + sizeof(Elf64_Sym) > img.bytes.size())
            continue;
        const Elf64_Sym* s = reinterpret_cast<const Elf64_Sym*>(img.bytes.data() + sym_fo);
        // Bounded name read: never run past the image on a truncated file.
        char namebuf[128];
        const uint64_t name_at = str_off + s->st_name;
        if (name_at >= img.bytes.size()) {
            snprintf(namebuf, sizeof(namebuf), "#%zu", sym_idx);
        } else {
            const size_t maxlen = img.bytes.size() - (size_t)name_at;
            const size_t show = maxlen < sizeof(namebuf) - 1 ? maxlen : sizeof(namebuf) - 1;
            size_t len = 0;
            while (len < show && img.bytes[(size_t)name_at + len] != 0)
                ++len;
            memcpy(namebuf, img.bytes.data() + name_at, len);
            namebuf[len] = '\0';
        }

        const uintptr_t slot = bias + (uintptr_t)r_offset;
        uintptr_t target = 0;
        bool ok = true;
        read_mem(pid, slot, &target, sizeof(target), ok);
        if (!ok) {
            // Unreadable slot (unmapped bias edge, vsyscall-style gap):
            // note it and continue with the rest, don't abort the module.
            rep.mem_ok = false;
            continue;
        }
        ++rep.slots_checked;

        // Unresolved lazy/weak slots read as 0: nothing is hosted anywhere
        // (common for transactional-memory/libstdc++ extras). Not a finding.
        if (target == 0)
            continue;

        const MapEntry* host = containing(maps, target);
        std::string host_base = host ? basename_of(host->path) : "";
        if (!host)
            host_base = "[unmapped]";
        else if (host->path.empty())
            host_base = "[anon]";

        int tier = 0;
        if (!host || host->path.empty())
            tier = 2;
        else if (host->path != path && !host_is_usual(host_base))
            tier = 1;
        if (tier > 0) {
            GotFinding f;
            f.symbol = namebuf;
            f.slot = slot;
            f.target = target;
            f.target_module = host_base;
            f.tier = tier;
            rep.flagged.push_back(f);
        }
    }
    return rep;
}

TextReport audit_text(pid_t pid, const char* modsubstr, size_t maxfindings)
{
    TextReport rep;
    uintptr_t map_base = 0;
    std::string path;
    if (!module_bias(pid, modsubstr, map_base, rep.module))
        return rep;
    path = rep.module;

    const FileImage img = read_file(path);
    if (!img.ok || img.bytes.size() < sizeof(Elf64_Ehdr))
        return rep;
    const Elf64_Ehdr* eh = reinterpret_cast<const Elf64_Ehdr*>(img.bytes.data());
    if (memcmp(eh->e_ident, "\x7F" "ELF", 4) != 0)
        return rep;
    // Same ET_EXEC-vs-ET_DYN bias rule as audit_got.
    const uintptr_t bias = (eh->e_type == ET_DYN) ? map_base : 0;
    const Elf64_Phdr* ph =
        reinterpret_cast<const Elf64_Phdr*>(img.bytes.data() + eh->e_phoff);

    // RELA allowlist: bytes the loader legitimately rewrote (relocated
    // pointers differ file-vs-memory by design). Built from section headers
    // when present; otherwise we compare raw and say so.
    struct Span {
        uint64_t lo, hi;
    };
    std::vector<Span> relocs;
    if (eh->e_shoff && eh->e_shnum) {
        const Elf64_Shdr* sh =
            reinterpret_cast<const Elf64_Shdr*>(img.bytes.data() + eh->e_shoff);
        for (int i = 0; i < eh->e_shnum; ++i) {
            if (sh[i].sh_type != SHT_RELA && sh[i].sh_type != SHT_REL)
                continue;
            if (sh[i].sh_offset + sh[i].sh_size > img.bytes.size())
                continue; // corrupt/truncated headers: skip, don't read OOB
            const size_t n =
                sh[i].sh_size / (sh[i].sh_type == SHT_RELA ? sizeof(Elf64_Rela) : sizeof(Elf64_Rel));
            for (size_t k = 0; k < n; ++k) {
                uint64_t off = 0, info = 0;
                size_t sz = 0;
                if (sh[i].sh_type == SHT_RELA) {
                    const Elf64_Rela* r = reinterpret_cast<const Elf64_Rela*>(
                        img.bytes.data() + sh[i].sh_offset + k * sizeof(Elf64_Rela));
                    off = r->r_offset;
                    info = r->r_info;
                } else {
                    const Elf64_Rel* r = reinterpret_cast<const Elf64_Rel*>(
                        img.bytes.data() + sh[i].sh_offset + k * sizeof(Elf64_Rel));
                    off = r->r_offset;
                    info = r->r_info;
                }
                const uint64_t type = (uint64_t)ELF64_R_TYPE(info);
                switch (type) {
                case R_X86_64_64:
                case R_X86_64_GLOB_DAT:
                case R_X86_64_JUMP_SLOT:
                case R_X86_64_RELATIVE:
                case R_X86_64_IRELATIVE:
                    sz = 8;
                    break;
                case R_X86_64_32:
                case R_X86_64_32S:
                case R_X86_64_PC32:
                    sz = 4;
                    break;
                case R_X86_64_16:
                case R_X86_64_PC16:
                    sz = 2;
                    break;
                case R_X86_64_8:
                case R_X86_64_PC8:
                    sz = 1;
                    break;
                default:
                    break;
                }
                if (sz)
                    relocs.push_back({off, off + sz});
            }
        }
        rep.relocs_loaded = true;
    }
    std::sort(relocs.begin(), relocs.end(), [](const Span& a, const Span& b) {
        return a.lo < b.lo;
    });
    auto relocated = [&](uint64_t vaddr) {
        for (const auto& s : relocs) {
            if (vaddr >= s.hi)
                continue;
            if (vaddr < s.lo)
                break;
            return true;
        }
        return false;
    };

    // Compare every RX (no-W) LOAD segment, chunked (no giant buffers).
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (!(ph[i].p_flags & PF_R) || !(ph[i].p_flags & PF_X) || (ph[i].p_flags & PF_W))
            continue;
        uint64_t left = ph[i].p_filesz;
        uint64_t voff = 0;
        while (left) {
            unsigned char mbuf[4096], fbuf[4096];
            const size_t n = left > sizeof(mbuf) ? sizeof(mbuf) : (size_t)left;
            memcpy(fbuf, img.bytes.data() + ph[i].p_offset + voff, n);
            bool ok = true;
            read_mem(pid, bias + ph[i].p_vaddr + voff, mbuf, n, ok);
            if (!ok) {
                // Mapping changed mid-scan (or unreadable hole): flag and
                // continue with the next chunk instead of aborting.
                rep.mem_ok = false;
                voff += n;
                left -= n;
                continue;
            }
            rep.bytes_compared += n;
            for (size_t k = 0; k < n; ++k) {
                if (mbuf[k] == fbuf[k])
                    continue;
                const uint64_t va = ph[i].p_vaddr + voff + k;
                if (relocated(va))
                    continue;
                ++rep.diff_total;
                if (rep.first.size() < maxfindings) {
                    TextFinding f;
                    f.rva = va;
                    f.mem_byte = mbuf[k];
                    f.file_byte = fbuf[k];
                    rep.first.push_back(f);
                }
            }
            voff += n;
            left -= n;
        }
    }
    return rep;
}

} // namespace expose
