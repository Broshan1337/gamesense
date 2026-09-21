#pragma once

// Linux-native exposure audit: what a mem-reading service module would see.
//
// This is our answer to the Windows "VAC emulation" helper's checks,
// rebuilt for how Linux actually works (ELF, /proc, loader structures):
//   - executable anonymous/memfd mappings (manual-map / JIT heuristics)
//   - thread list (start-address-outside-module has no Linux equivalent;
//     we report tids+comm for analyst review)
//   - GOT audit: every JMP_SLOT target must live in a plausible host
//     module (our own GOT hooks point at the cheat mapping -> flagged)
//   - .text audit: file-vs-memory compare of RX segments with a RELA
//     allowlist (our detours/patches -> flagged)
//
// pid==self (or getpid()) reads memory directly; external pids go through
// /proc/<pid>/mem, which the kernel may deny (yama) - callers get partial
// results with mem_ok=false rather than an error. Heuristic tiers mirror
// the friend tool's trusted-list approach; see audit_got.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace expose
{

struct MapEntry {
    uintptr_t start = 0;
    uintptr_t end = 0;
    std::string perms; // e.g. "r-xp"
    unsigned long long offset = 0;
    std::string path; // may be "", "[heap]", "/memfd:..", ...
};

struct ExecRegion {
    uintptr_t start = 0;
    uintptr_t end = 0;
    std::string perms;
    std::string path;
    bool rwx = false;
};

struct ThreadInfo {
    int tid = 0;
    std::string comm;
};

struct GotFinding {
    std::string symbol;
    uintptr_t slot = 0;
    uintptr_t target = 0;
    std::string target_module; // basename or "" / "[anon]"
    int tier = 0; // 0 clean, 1 suspicious (unusual host), 2 critical (anon/unmapped)
};

struct TextFinding {
    uintptr_t rva = 0; // module-relative virtual address
    unsigned char mem_byte = 0;
    unsigned char file_byte = 0;
};

bool is_self(pid_t pid) noexcept;
std::vector<MapEntry> parse_maps(pid_t pid);
std::vector<ExecRegion> scan_exec_regions(pid_t pid);
std::vector<ThreadInfo> scan_threads(pid_t pid);

// Load bias + canonical path for the first module whose maps path contains
// substr (skips the main executable when called on self? no - caller picks
// substr; the harness uses distinctive names).
bool module_bias(pid_t pid, const char* substr, uintptr_t& bias, std::string& path);

// GOT audit for modules matching substr. mem_ok=false when target memory
// could not be read (external pid + yama) - findings then only cover parse.
struct GotReport {
    bool mem_ok = true;
    std::string module;
    size_t slots_checked = 0;
    std::vector<GotFinding> flagged; // tier > 0 only
};
GotReport audit_got(pid_t pid, const char* modsubstr);

// .text audit (file vs memory, RELA allowlist). Same mem_ok contract.
struct TextReport {
    bool mem_ok = true;
    bool relocs_loaded = false;
    std::string module;
    size_t bytes_compared = 0;
    size_t diff_total = 0;
    std::vector<TextFinding> first; // capped
};
TextReport audit_text(pid_t pid, const char* modsubstr, size_t maxfindings = 32);

} // namespace expose
