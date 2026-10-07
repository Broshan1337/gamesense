#pragma once

















#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace expose
{

struct MapEntry {
    uintptr_t start = 0;
    uintptr_t end = 0;
    std::string perms; 
    unsigned long long offset = 0;
    std::string path; 
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
    std::string target_module; 
    int tier = 0; 
};

struct TextFinding {
    uintptr_t rva = 0; 
    unsigned char mem_byte = 0;
    unsigned char file_byte = 0;
};

bool is_self(pid_t pid) noexcept;
std::vector<MapEntry> parse_maps(pid_t pid);
std::vector<ExecRegion> scan_exec_regions(pid_t pid);
std::vector<ThreadInfo> scan_threads(pid_t pid);




bool module_bias(pid_t pid, const char* substr, uintptr_t& bias, std::string& path);



struct GotReport {
    bool mem_ok = true;
    std::string module;
    size_t slots_checked = 0;
    std::vector<GotFinding> flagged; 
};
GotReport audit_got(pid_t pid, const char* modsubstr);


struct TextReport {
    bool mem_ok = true;
    bool relocs_loaded = false;
    std::string module;
    size_t bytes_compared = 0;
    size_t diff_total = 0;
    std::vector<TextFinding> first; 
};
TextReport audit_text(pid_t pid, const char* modsubstr, size_t maxfindings = 32);

} 
