// Fake-VAC harness: automated adversary against the production VAC hooks.
//
// Architecture mirrors the real one: VacFixture (a stand-in "steamclient.so",
// recognised by the production resolver via the "steamclient.so" filename
// substring) plays the VAC side - every libc call it makes resolves through
// ITS OWN GOT, which the harness patches with the real hook addresses from
// Source/hooks/vac_hook.cpp. The test binary itself only observes and
// asserts, exactly like an external analyst would.
//
// What runs here, in order (gtest runs TEST_F in declaration order):
//   1. BaselineSeesEverything - pre-hook scan: hidden entries MUST be visible
//      (proves the scanner + fixture visibility actually work).
//   2. InstallAndHide - post-hook scans with hostile sizes (fgets buf 53,
//      read chunks 37/500/4096/65536): hidden entries gone, everything else
//      byte-identical to baseline minus hidden lines.
//   3. SlotReuse - 25 maps open/close cycles, then still hidden (fclose fix).
//   4. FileSpoof - the cheat-named file served fake-clean through fopen,
//      open, openat, pread, fstat, mmap and lseek; control file untouched.
//   5. StatsAdvance - hook counters moved.
//   6. UninstallRestores - entries visible again (restore path works).
//   7. ServiceModuleHunter - synthetic <checksum>-<size>.so decoys in a temp
//      dir are found/flagged correctly (validates the offline capture tool).
//
// Verbose transcript: run the binary directly (or ctest -V) - every scan
// prints method, sizes, counts and hook stats. NOTE: installing the hooks
// writes a few first-hit lines to /tmp/gamesense_gui.log (and rotates it),
// same as in-game.
//
// Manual service-module capture after a secured-server session:
//   ./VacHarnessBin --scan /tmp
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <Platform/Linux/LinuxPlatformApiImpl.h>

#include <hooks/vac_hook.h>

#include "UnitTests/Vac/VacExpose.h"

#ifndef VAC_FIXTURE_PATH
#error "VAC_FIXTURE_PATH must be defined by CMake (TARGET_FILE:vacfixture)"
#endif
#ifndef VAC_DECOY_RUNFUNC_PATH
#error "VAC_DECOY_RUNFUNC_PATH must be defined by CMake"
#endif
#ifndef VAC_DECOY_PLAIN_PATH
#error "VAC_DECOY_PLAIN_PATH must be defined by CMake"
#endif

extern "C" {
typedef char* (*VacfFgetsAll)(const char*, int, size_t*);
typedef char* (*VacfReadAll)(const char*, size_t, size_t*);
typedef int (*VacfFopenFirst)(const char*, unsigned char*, size_t, size_t*);
typedef ssize_t (*VacfOpenRead)(const char*, void*, size_t);
typedef ssize_t (*VacfOpenatRead)(const char*, void*, size_t);
typedef ssize_t (*VacfPreadFirst)(const char*, void*, size_t, off_t);
typedef int (*VacfFstatProbe)(const char*, long*, int*);
typedef int (*VacfMmapFirst)(const char*, unsigned char[4]);
typedef int (*VacfLseekSize)(const char*, long*);
}

namespace
{

const char* kTokModule = "libMangoHud.so"; // these live in the TEST binary
const char* kTokFallback = ".fc-cache-424242"; // (never injected) - fine plain
const char* kTokMemfd = "memfd:libMangoHud";
const char* kControlNeedle = "libc";

struct FixtureApi {
    VacfFgetsAll fgets_all = nullptr;
    VacfReadAll read_all = nullptr;
    VacfFopenFirst fopen_first = nullptr;
    VacfOpenRead open_read = nullptr;
    VacfOpenatRead openat_read = nullptr;
    VacfPreadFirst pread_first = nullptr;
    VacfFstatProbe fstat_probe = nullptr;
    VacfMmapFirst mmap_first = nullptr;
    VacfLseekSize lseek_size = nullptr;
};

std::vector<std::string> split_lines(const char* data, size_t len)
{
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(data[i]);
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

bool contains_token(const std::vector<std::string>& lines, const char* needle)
{
    for (const auto& l : lines) {
        if (l.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

size_t count_token(const std::vector<std::string>& lines, const char* needle)
{
    size_t n = 0;
    for (const auto& l : lines) {
        if (l.find(needle) != std::string::npos)
            ++n;
    }
    return n;
}

// Baseline lines minus hidden lines must equal hooked lines EXACTLY: no
// truncation (fake EOF would drop trailing libc lines), no extras, no leaks.
// On mismatch, dump the difference (capped) - that diff IS the diagnosis.
void expect_exact_filter(const std::vector<std::string>& baseline,
                         const std::vector<std::string>& hooked)
{
    std::vector<std::string> expected;
    for (const auto& l : baseline) {
        if (l.find(kTokModule) == std::string::npos &&
            l.find(kTokFallback) == std::string::npos &&
            l.find(kTokMemfd) == std::string::npos)
            expected.push_back(l);
    }
    if (hooked.size() != expected.size()) {
        printf("[harness] MISMATCH hooked=%zu expected=%zu\n", hooked.size(), expected.size());
        size_t shown = 0;
        for (size_t i = 0; i < hooked.size() && shown < 10; ++i) {
            bool found = false;
            for (const auto& e : expected) {
                if (e == hooked[i]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                printf("[harness]   hooked-extra[%zu]: %s\n", i, hooked[i].c_str());
                ++shown;
            }
        }
        for (size_t i = 0; i < expected.size() && shown < 20; ++i) {
            bool found = false;
            for (const auto& h : hooked) {
                if (h == expected[i]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                printf("[harness]   expected-missing[%zu]: %s\n", i, expected[i].c_str());
                ++shown;
            }
        }
    }
    ASSERT_EQ(hooked.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        EXPECT_EQ(hooked[i], expected[i]);
}

bool copy_file(const char* src, const char* dst)
{
    FILE* in = fopen(src, "rb");
    if (!in)
        return false;
    FILE* out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    if (ferror(in))
        ok = false;
    fclose(in);
    fclose(out);
    return ok;
}

// ---- service-module hunter (offline capture tool) ----

struct SvcFinding {
    std::string path;
    unsigned long long size = 0;
    bool elf = false;
    bool has_runfunc = false;
};

bool file_has_bytes(const char* path, const char* needle)
{
    FILE* fp = fopen(path, "rb");
    if (!fp)
        return false;
    std::string data;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        data.append(buf, n);
    fclose(fp);
    return data.find(needle) != std::string::npos;
}

std::vector<SvcFinding> scan_service_modules(const std::string& dir)
{
    std::vector<SvcFinding> out;
    void* dh = LinuxPlatformApi::openDir(dir.c_str());
    if (!dh)
        return out;
    for (;;) {
        const char* name = LinuxPlatformApi::readDir(dh);
        if (!name)
            break;
        std::string base(name);
        // VAC drops runtime modules as <checksum>-<size>.so (see the VAC
        // report: small ~15-20KB objects with a `runfunc` export).
        if (base.size() < 5 || base.compare(base.size() - 3, 3, ".so") != 0)
            continue;
        if (base.find('-') == std::string::npos)
            continue;
        const std::string full = dir + "/" + base;
        struct stat st{};
        if (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        SvcFinding f;
        f.path = full;
        f.size = static_cast<unsigned long long>(st.st_size);
        unsigned char magic[4]{};
        FILE* fp = fopen(full.c_str(), "rb");
        if (fp) {
            f.elf = fread(magic, 1, 4, fp) == 4 && magic[0] == 0x7F &&
                    magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
            fclose(fp);
        }
        f.has_runfunc = file_has_bytes(full.c_str(), "runfunc");
        out.push_back(f);
    }
    LinuxPlatformApi::closeDir(dh);
    return out;
}

// ---- shared environment ----

class VacEnv : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        char tmpl[] = "/tmp/vacharness-XXXXXX";
        ASSERT_NE(::mkdtemp(tmpl), nullptr);
        tmpdir_ = tmpl;

        // Hidden entry A: a mapping whose path names the module.
        path_module_ = tmpdir_ + "/libMangoHud.so";
        ASSERT_TRUE(copy_file(VAC_FIXTURE_PATH, path_module_.c_str()));

        // Hidden entry B: a mapping under the GDB-fallback tmp name.
        path_fallback_ = tmpdir_ + "/.fc-cache-424242";
        ASSERT_TRUE(copy_file(VAC_FIXTURE_PATH, path_fallback_.c_str()));

        // Hidden entry C: an anonymous memfd mapping with the module name.
        memfd_ = ::memfd_create("libMangoHud.so", MFD_CLOEXEC);
        ASSERT_GE(memfd_, 0);
        ASSERT_EQ(::ftruncate(memfd_, 4096), 0);
        memfd_map_ = ::mmap(nullptr, 4096, PROT_READ, MAP_SHARED, memfd_, 0);
        ASSERT_NE(memfd_map_, MAP_FAILED);

        // Plain control file: must pass through byte-identical.
        path_control_ = tmpdir_ + "/control.txt";
        {
            FILE* fp = fopen(path_control_.c_str(), "w");
            ASSERT_NE(fp, nullptr);
            fputs("harmless control content 12345\n", fp);
            fclose(fp);
        }

        // The "steamclient": filename carries the substring the production
        // resolver looks for, so install_vac_hook() patches THIS module.
        h_fixture_ = ::dlopen(VAC_FIXTURE_PATH, RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(h_fixture_, nullptr);
        api_.fgets_all = (VacfFgetsAll)::dlsym(h_fixture_, "vacf_fgets_all");
        api_.read_all = (VacfReadAll)::dlsym(h_fixture_, "vacf_read_all");
        api_.fopen_first = (VacfFopenFirst)::dlsym(h_fixture_, "vacf_fopen_first");
        api_.open_read = (VacfOpenRead)::dlsym(h_fixture_, "vacf_open_read");
        api_.openat_read = (VacfOpenatRead)::dlsym(h_fixture_, "vacf_openat_read");
        api_.pread_first = (VacfPreadFirst)::dlsym(h_fixture_, "vacf_pread_first");
        api_.fstat_probe = (VacfFstatProbe)::dlsym(h_fixture_, "vacf_fstat_probe");
        api_.mmap_first = (VacfMmapFirst)::dlsym(h_fixture_, "vacf_mmap_first");
        api_.lseek_size = (VacfLseekSize)::dlsym(h_fixture_, "vacf_lseek_size");
        ASSERT_NE(api_.fgets_all, nullptr);
        ASSERT_NE(api_.read_all, nullptr);
        ASSERT_NE(api_.fopen_first, nullptr);
        ASSERT_NE(api_.open_read, nullptr);
        ASSERT_NE(api_.openat_read, nullptr);
        ASSERT_NE(api_.pread_first, nullptr);
        ASSERT_NE(api_.fstat_probe, nullptr);
        ASSERT_NE(api_.mmap_first, nullptr);
        ASSERT_NE(api_.lseek_size, nullptr);

        h_a_ = ::dlopen(path_module_.c_str(), RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(h_a_, nullptr);
        h_b_ = ::dlopen(path_fallback_.c_str(), RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(h_b_, nullptr);

        // Baseline scan BEFORE install (validates the scanner itself), then
        // install once for the whole process. ctest runs every TEST_F below
        // as its own process (--gtest_filter), so setup must leave each test
        // hermetic: installed hooks + populated baseline, no cross-test order.
        {
            size_t len = 0;
            char* text = api_.fgets_all("/proc/self/maps", 4096, &len);
            ASSERT_NE(text, nullptr);
            baseline_ = split_lines(text, len);
            free(text);
        }

        // Pre-install control: the fixture GOT must be fully clean before
        // our hooks land (proves the auditor isn't flagging everything).
        {
            const auto pre = expose::audit_got(::getpid(), "vacfixture_steamclient");
            ASSERT_TRUE(pre.mem_ok);
            EXPECT_EQ(pre.flagged.size(), 0u);
            printf("[harness] pre-install GOT slots checked: %zu\n", pre.slots_checked);
        }

        ASSERT_TRUE(fva::hooks::install_vac_hook());
    }

    static void TearDownTestSuite()
    {
        fva::hooks::uninstall_vac_hook(); // idempotent: safe after the uninstall test
        if (memfd_map_ != MAP_FAILED)
            ::munmap(memfd_map_, 4096);
        if (memfd_ >= 0)
            ::close(memfd_);
        if (h_a_)
            ::dlclose(h_a_);
        if (h_b_)
            ::dlclose(h_b_);
        if (h_fixture_)
            ::dlclose(h_fixture_);
        ::unlink(path_module_.c_str());
        ::unlink(path_fallback_.c_str());
        ::unlink(path_control_.c_str());
        ::rmdir(tmpdir_.c_str());
    }

    static void print_stats(const char* tag)
    {
        const auto s = fva::hooks::vac_hook_stats();
        printf("[harness] stats %s: maps_opens=%lu lines_filtered=%lu spoofs=%lu\n",
               tag, s.maps_opens, s.lines_filtered, s.spoofs);
    }

    static inline std::string tmpdir_;
    static inline std::string path_module_;
    static inline std::string path_fallback_;
    static inline std::string path_control_;
    static inline void* h_fixture_ = nullptr;
    static inline void* h_a_ = nullptr;
    static inline void* h_b_ = nullptr;
    static inline int memfd_ = -1;
    static inline void* memfd_map_ = MAP_FAILED;
    static inline FixtureApi api_;
    static inline std::vector<std::string> baseline_;
};

TEST_F(VacEnv, BaselineScanValid)
{
    // The pre-hook scan (taken in setup) must show every hidden entry, or
    // the scanner is blind and every later "hidden" assertion is vacuous.
    printf("[harness] baseline: %zu lines\n", baseline_.size());
    EXPECT_GT(baseline_.size(), 10u);
    EXPECT_GT(count_token(baseline_, kTokModule), 0u) << "module copy not mapped?";
    EXPECT_GT(count_token(baseline_, kTokFallback), 0u) << "fallback copy not mapped?";
    EXPECT_GT(count_token(baseline_, kTokMemfd), 0u) << "memfd mapping not visible?";
    EXPECT_GT(count_token(baseline_, kControlNeedle), 0u) << "scanner broken (no libc)?";
    print_stats("baseline");
}

TEST_F(VacEnv, InstallAndHide)
{
    // Post-hook scans with hostile sizes (fgets buf 53, read chunks
    // 37/500/4096/65536): hidden entries gone, everything else
    // byte-identical to baseline minus hidden lines.
    {
        size_t len = 0;
        char* text = api_.fgets_all("/proc/self/maps", 53, &len);
        ASSERT_NE(text, nullptr);
        auto lines = split_lines(text, len);
        printf("[harness] fgets/53: %zu bytes %zu lines (baseline %zu)\n",
               len, lines.size(), baseline_.size());
        EXPECT_FALSE(contains_token(lines, kTokModule));
        EXPECT_FALSE(contains_token(lines, kTokFallback));
        EXPECT_FALSE(contains_token(lines, kTokMemfd));
        expect_exact_filter(baseline_, lines);
        free(text);
    }
    for (size_t chunk : {37u, 500u, 4096u, 65536u}) {
        size_t len = 0;
        char* text = api_.read_all("/proc/self/maps", chunk, &len);
        ASSERT_NE(text, nullptr) << "chunk " << chunk;
        auto lines = split_lines(text, len);
        printf("[harness] read/%zu: %zu bytes %zu lines\n", chunk, len, lines.size());
        EXPECT_FALSE(contains_token(lines, kTokModule)) << "chunk " << chunk;
        EXPECT_FALSE(contains_token(lines, kTokFallback)) << "chunk " << chunk;
        EXPECT_FALSE(contains_token(lines, kTokMemfd)) << "chunk " << chunk;
        EXPECT_TRUE(contains_token(lines, kControlNeedle)) << "chunk " << chunk;
        expect_exact_filter(baseline_, lines);
        free(text);
    }

    // Transcript head: what VAC would actually receive.
    {
        size_t len = 0;
        char* text = api_.fgets_all("/proc/self/maps", 4096, &len);
        ASSERT_NE(text, nullptr);
        auto lines = split_lines(text, len);
        printf("[harness] hooked maps head (%zu lines total):\n", lines.size());
        for (size_t i = 0; i < lines.size() && i < 12; ++i)
            printf("[harness]   %s\n", lines[i].c_str());
        free(text);
    }
    print_stats("hooked");
}

TEST_F(VacEnv, SlotReuseAfterManyOpens)
{
    // 25 tracked open/close cycles against an 8-slot table: with the fclose
    // fix every slot is recycled; without it the 9th open goes untracked and
    // the module leaks back into view.
    for (int i = 0; i < 25; ++i) {
        size_t len = 0;
        char* text = api_.fgets_all("/proc/self/maps", 4096, &len);
        EXPECT_NE(text, nullptr) << "cycle " << i;
        free(text);
    }
    size_t len = 0;
    char* text = api_.fgets_all("/proc/self/maps", 4096, &len);
    ASSERT_NE(text, nullptr);
    auto lines = split_lines(text, len);
    EXPECT_FALSE(contains_token(lines, kTokModule));
    EXPECT_FALSE(contains_token(lines, kTokFallback));
    EXPECT_FALSE(contains_token(lines, kTokMemfd));
    expect_exact_filter(baseline_, lines);
    printf("[harness] 25 open/close cycles: still hidden, %zu lines\n", lines.size());
    free(text);
}

TEST_F(VacEnv, FileSpoofSurvivesAllAccessPatterns)
{
    // The cheat-named file must serve the fake clean ELF through every access
    // pattern a hashing module might use. Expected fake: 128 bytes (sizeof
    // fake_elf_header in integrity_audit.cpp) starting 7F 45 4C 46, regular
    // file, seekable, mappable.
    static constexpr long kFakeSize = 128;
    unsigned char buf[128]{};
    size_t got = 0;
    ASSERT_EQ(api_.fopen_first(path_module_.c_str(), buf, sizeof(buf), &got), 0);
    printf("[harness] fopen/fread: got=%zu first4=%02X%02X%02X%02X\n",
           got, buf[0], buf[1], buf[2], buf[3]);
    EXPECT_EQ(got, sizeof(buf));
    EXPECT_EQ(buf[0], 0x7F);
    EXPECT_EQ(buf[1], 'E');
    EXPECT_EQ(buf[2], 'L');
    EXPECT_EQ(buf[3], 'F');

    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(api_.open_read(path_module_.c_str(), buf, 4), 4);
    EXPECT_EQ(memcmp(buf, "\x7F" "ELF", 4), 0);
    EXPECT_EQ(api_.openat_read(path_module_.c_str(), buf, 4), 4);
    EXPECT_EQ(memcmp(buf, "\x7F" "ELF", 4), 0);
    printf("[harness] open/read + openat/read: ELF magic ok\n");

    memset(buf, 0, 16);
    EXPECT_EQ(api_.pread_first(path_module_.c_str(), buf, 16, 100), 16);
    printf("[harness] pread@100: ok\n");

    long size = 0;
    int isreg = 0;
    ASSERT_EQ(api_.fstat_probe(path_module_.c_str(), &size, &isreg), 0);
    printf("[harness] fstat: size=%ld isreg=%d\n", size, isreg);
    EXPECT_EQ(size, kFakeSize);
    EXPECT_EQ(isreg, 1);

    unsigned char first4[4]{};
    ASSERT_EQ(api_.mmap_first(path_module_.c_str(), first4), 0);
    EXPECT_EQ(memcmp(first4, "\x7F" "ELF", 4), 0);
    printf("[harness] mmap: ok\n");

    long end = 0;
    ASSERT_EQ(api_.lseek_size(path_module_.c_str(), &end), 0);
    EXPECT_EQ(end, kFakeSize);
    printf("[harness] lseek END: %ld\n", end);

    // Negative control: unrelated files pass through byte-identical.
    memset(buf, 0, sizeof(buf));
    ssize_t n = api_.open_read(path_control_.c_str(), buf, 32);
    ASSERT_EQ(n, 31);
    EXPECT_EQ(strcmp((const char*)buf, "harmless control content 12345\n"), 0);
    printf("[harness] control file passthrough: ok\n");
}

TEST_F(VacEnv, StatsAdvance)
{
    // Self-sufficient: exercise one maps scan and one spoof probe, then
    // assert the counters moved (delta-based, so parallel/full runs agree).
    const auto before = fva::hooks::vac_hook_stats();
    {
        size_t len = 0;
        char* text = api_.fgets_all("/proc/self/maps", 4096, &len);
        EXPECT_NE(text, nullptr);
        free(text);
    }
    {
        unsigned char buf[16]{};
        size_t got = 0;
        EXPECT_EQ(api_.fopen_first(path_module_.c_str(), buf, sizeof(buf), &got), 0);
    }
    const auto s = fva::hooks::vac_hook_stats();
    print_stats("final");
    EXPECT_GT(s.maps_opens, before.maps_opens);
    EXPECT_GT(s.lines_filtered, before.lines_filtered);
    EXPECT_GT(s.spoofs, before.spoofs);
}

TEST_F(VacEnv, LinkMapHideRestore)
{
    // In-process enumeration (what a dlopen'd service module would use)
    // must lose exactly the hidden node and nothing else, while hooked
    // filtering keeps working throughout.
    auto collect = []() {
        std::vector<std::string> names;
        ::dl_iterate_phdr(
            [](dl_phdr_info* info, size_t, void* data) noexcept -> int {
                auto* out = static_cast<std::vector<std::string>*>(data);
                if (info->dlpi_name && *info->dlpi_name)
                    out->push_back(info->dlpi_name);
                return 0;
            },
            &names);
        return names;
    };
    auto has = [](const std::vector<std::string>& v, const char* s) {
        for (const auto& n : v) {
            if (n.find(s) != std::string::npos)
                return true;
        }
        return false;
    };

    // Setup's install must NOT have unlinked the test executable itself.
    EXPECT_FALSE(fva::hooks::link_is_hidden());

    const auto pre = collect();
    ASSERT_TRUE(has(pre, "libMangoHud.so")) << "copy A not enumerable?";
    ASSERT_TRUE(has(pre, ".fc-cache-424242")) << "copy B not enumerable?";
    ASSERT_TRUE(has(pre, "vacfixture_steamclient")) << "fixture not enumerable?";
    ASSERT_TRUE(has(pre, "libc")) << "iteration broken?";
    printf("[harness] link_map entries pre-hide: %zu\n", pre.size());

    ASSERT_TRUE(fva::hooks::link_hide_by_substr("libMangoHud.so"));
    EXPECT_TRUE(fva::hooks::link_is_hidden());

    const auto hid = collect();
    printf("[harness] link_map entries hidden: %zu\n", hid.size());
    EXPECT_FALSE(has(hid, "libMangoHud.so")) << "node still listed while hidden";
    EXPECT_TRUE(has(hid, ".fc-cache-424242")) << "surgical miss: copy B dropped too";
    EXPECT_TRUE(has(hid, "vacfixture_steamclient")) << "surgical miss: fixture dropped";
    EXPECT_TRUE(has(hid, "libc")) << "chain corrupted?";

    // Functionality intact while hidden: maps filtering still applies.
    {
        size_t len = 0;
        char* text = api_.fgets_all("/proc/self/maps", 4096, &len);
        ASSERT_NE(text, nullptr);
        auto lines = split_lines(text, len);
        EXPECT_FALSE(contains_token(lines, kTokModule));
        EXPECT_TRUE(contains_token(lines, kControlNeedle));
        free(text);
    }

    fva::hooks::link_restore();
    EXPECT_FALSE(fva::hooks::link_is_hidden());
    const auto post = collect();
    EXPECT_TRUE(has(post, "libMangoHud.so")) << "node not restored";
    printf("[harness] link_map entries restored: %zu\n", post.size());
}

TEST_F(VacEnv, DeferredHideSkipsExe)
{
    // Production path (not the seam): must refuse the main executable and
    // stay idempotent across presents. Would abort the process if the guard
    // ever failed to stop an exe unlink.
    EXPECT_FALSE(fva::hooks::link_is_hidden());
    fva::hooks::apply_deferred_link_hide();
    EXPECT_FALSE(fva::hooks::link_is_hidden());
    fva::hooks::apply_deferred_link_hide();
    EXPECT_FALSE(fva::hooks::link_is_hidden());
    printf("[harness] deferred hide: exe guard held, idempotent\n");
}

TEST_F(VacEnv, ExposeFlagsOwnHooks)
{
    // Red-team proof that a GOT-integrity check sees exactly our hooks
    // (accepted exposure): the flagged set must equal the 7 patched imports.
    const auto rep = expose::audit_got(::getpid(), "vacfixture_steamclient");
    ASSERT_TRUE(rep.mem_ok);
    EXPECT_GT(rep.slots_checked, 0u);
    std::vector<std::string> names;
    for (const auto& f : rep.flagged) {
        EXPECT_GT(f.tier, 0);
        names.push_back(f.symbol);
        printf("[harness] got-flagged: %-8s slot=0x%lx target=0x%lx host=%s tier=%d\n",
               f.symbol.c_str(), (unsigned long)f.slot, (unsigned long)f.target,
               f.target_module.c_str(), f.tier);
    }
    std::sort(names.begin(), names.end());
    const std::vector<std::string> expected = {"close", "fclose", "fgets", "fopen",
                                               "open", "openat", "read"};
    EXPECT_EQ(names, expected);
}

TEST_F(VacEnv, ExposeTextSynthetic)
{
    // Flip one .text byte in the fixture: the file-vs-mem auditor must flag
    // exactly that rva; after restore it must be clean again.
    uintptr_t bias = 0;
    std::string mpath;
    ASSERT_TRUE(expose::module_bias(::getpid(), "vacfixture_steamclient", bias, mpath));
    uintptr_t text = 0;
    for (const auto& m : expose::parse_maps(::getpid())) {
        if (m.path == mpath && m.perms.size() >= 3 && m.perms[0] == 'r' &&
            m.perms[2] == 'x') {
            text = m.start;
            break;
        }
    }
    ASSERT_NE(text, 0u);
    ASSERT_EQ(::mprotect(reinterpret_cast<void*>(text), 4096, PROT_READ | PROT_WRITE), 0);
    volatile unsigned char* byte = reinterpret_cast<volatile unsigned char*>(text);
    const unsigned char saved = *byte;
    *byte = static_cast<unsigned char>(saved ^ 0xFF);

    auto rep = expose::audit_text(::getpid(), "vacfixture_steamclient");
    ASSERT_TRUE(rep.mem_ok);
    printf("[harness] text audit with 1 flipped byte: diffs=%zu relocs=%d\n",
           rep.diff_total, (int)rep.relocs_loaded);
    EXPECT_GE(rep.diff_total, 1u);
    bool found = false;
    for (const auto& f : rep.first) {
        if (f.rva == text - bias)
            found = true;
    }
    EXPECT_TRUE(found) << "flipped rva not reported (reloc-covered?)";

    *byte = saved;
    ASSERT_EQ(::mprotect(reinterpret_cast<void*>(text), 4096, PROT_READ | PROT_EXEC), 0);
    rep = expose::audit_text(::getpid(), "vacfixture_steamclient");
    ASSERT_TRUE(rep.mem_ok);
    EXPECT_EQ(rep.diff_total, 0u);
}

TEST_F(VacEnv, ExposeListsSelf)
{
    // An RWX probe mapping must show up flagged rwx; our own tid listed.
    void* probe = ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(probe, MAP_FAILED);
    auto execs = expose::scan_exec_regions(::getpid());
    bool found = false;
    for (const auto& r : execs) {
        if (reinterpret_cast<uintptr_t>(probe) >= r.start &&
            reinterpret_cast<uintptr_t>(probe) < r.end) {
            found = true;
            EXPECT_TRUE(r.rwx);
        }
    }
    EXPECT_TRUE(found) << "RWX probe mapping not reported";
    ::munmap(probe, 4096);

    const int me = static_cast<int>(::syscall(SYS_gettid));
    auto threads = expose::scan_threads(::getpid());
    bool met = false;
    for (const auto& t : threads) {
        if (t.tid == me && !t.comm.empty())
            met = true;
    }
    EXPECT_TRUE(met) << "own tid not listed";
    printf("[harness] self: %zu anon-exec regions, %zu threads\n", execs.size(),
           threads.size());
}

TEST_F(VacEnv, UninstallRestores)
{
    fva::hooks::uninstall_vac_hook();
    size_t len = 0;
    char* text = api_.fgets_all("/proc/self/maps", 4096, &len);
    ASSERT_NE(text, nullptr);
    auto lines = split_lines(text, len);
    EXPECT_GT(count_token(lines, kTokModule), 0u) << "GOT not restored?";
    printf("[harness] after uninstall: module visible again (%zu lines)\n", lines.size());
    free(text);
}

TEST(ServiceModuleHunter, FlagsDecoys)
{
    char tmpl[] = "/tmp/vachunter-XXXXXX";
    ASSERT_NE(::mkdtemp(tmpl), nullptr);
    const std::string dir(tmpl);

    auto stage = [&](const char* src, const char* name) -> std::string {
        const std::string dst = dir + "/" + name;
        FILE* in = fopen(src, "rb");
        EXPECT_NE(in, nullptr) << src;
        if (!in)
            return {};
        FILE* out = fopen(dst.c_str(), "wb");
        EXPECT_NE(out, nullptr) << dst;
        if (!out) {
            fclose(in);
            return {};
        }
        char buf[65536];
        size_t n;
        bool ok = true;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            if (fwrite(buf, 1, n, out) != n) {
                ok = false;
                break;
            }
        }
        EXPECT_TRUE(ok);
        fclose(in);
        fclose(out);
        return dst;
    };
    stage(VAC_DECOY_RUNFUNC_PATH, "deadbeef-4096.so");
    stage(VAC_DECOY_PLAIN_PATH, "cafef00d-1234.so");
    {
        FILE* fp = fopen((dir + "/notes.txt").c_str(), "w");
        ASSERT_NE(fp, nullptr);
        fputs("runfunc mentioned here but not an .so\n", fp);
        fclose(fp);
    }

    const auto found = scan_service_modules(dir);
    printf("[harness] hunter found %zu candidates in %s\n", found.size(), dir.c_str());
    for (const auto& f : found) {
        printf("[harness]   %s size=%llu elf=%d runfunc=%d\n",
               f.path.c_str(), f.size, (int)f.elf, (int)f.has_runfunc);
    }
    ASSERT_EQ(found.size(), 2u);
    int flagged = 0;
    for (const auto& f : found) {
        EXPECT_TRUE(f.elf);
        EXPECT_GT(f.size, 0u);
        if (f.has_runfunc)
            ++flagged;
    }
    EXPECT_EQ(flagged, 1);

    ::unlink((dir + "/deadbeef-4096.so").c_str());
    ::unlink((dir + "/cafef00d-1234.so").c_str());
    ::unlink((dir + "/notes.txt").c_str());
    ::rmdir(dir.c_str());
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--scan" && i + 1 < argc) {
            const auto found = scan_service_modules(argv[i + 1]);
            printf("[harness] %zu service-module candidates in %s\n",
                   found.size(), argv[i + 1]);
            for (const auto& f : found) {
                printf("[harness] %s size=%llu elf=%d runfunc=%d\n",
                       f.path.c_str(), f.size, (int)f.elf, (int)f.has_runfunc);
            }
            return 0;
        }
        if (std::string(argv[i]) == "--expose") {
            // Exposure audit: what a mem-reading module would see. Optional
            // pid argument (default: self). External pids need mem access
            // (same user usually denied by yama - reported, not fatal).
            pid_t pid = 0;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                pid = static_cast<pid_t>(atoi(argv[++i]));
            printf("[expose] pid=%d\n", (int)(pid ? pid : ::getpid()));

            const auto execs = expose::scan_exec_regions(pid);
            printf("[expose] anon-exec regions: %zu\n", execs.size());
            for (const auto& r : execs) {
                printf("[expose]   %lx-%lx %s %s%s\n", (unsigned long)r.start,
                       (unsigned long)r.end, r.perms.c_str(),
                       r.rwx ? "[RWX] " : "", r.path.c_str());
            }

            const auto threads = expose::scan_threads(pid);
            printf("[expose] threads: %zu\n", threads.size());
            for (const auto& t : threads)
                printf("[expose]   tid=%d comm=%s\n", t.tid, t.comm.c_str());

            // GOT + text audit per file-backed module.
            std::vector<std::string> mods;
            for (const auto& m : expose::parse_maps(pid)) {
                if (m.path.empty() || m.path[0] == '[')
                    continue;
                if (std::find(mods.begin(), mods.end(), m.path) == mods.end())
                    mods.push_back(m.path);
            }
            printf("[expose] modules: %zu\n", mods.size());
            for (const auto& mod : mods) {
                // Basename substring is enough for module_bias to find it.
                const size_t slash = mod.find_last_of('/');
                const std::string sub =
                    (slash == std::string::npos) ? mod : mod.substr(slash + 1);
                const auto got = expose::audit_got(pid, sub.c_str());
                if (!got.mem_ok) {
                    printf("[expose]   %s: GOT mem-unreadable\n", sub.c_str());
                    continue;
                }
                if (got.flagged.empty())
                    printf("[expose]   %s: GOT clean (%zu slots)\n", sub.c_str(),
                           got.slots_checked);
                for (const auto& f : got.flagged) {
                    printf("[expose]   %s: GOT %s -> 0x%lx (%s) tier=%d\n",
                           sub.c_str(), f.symbol.c_str(), (unsigned long)f.target,
                           f.target_module.c_str(), f.tier);
                }
                const auto text = expose::audit_text(pid, sub.c_str(), 5);
                if (!text.mem_ok) {
                    printf("[expose]   %s: text mem-unreadable\n", sub.c_str());
                } else if (text.diff_total == 0) {
                    printf("[expose]   %s: text clean (%zu bytes%s)\n", sub.c_str(),
                           text.bytes_compared,
                           text.relocs_loaded ? "" : ", NO reloc allowlist");
                } else {
                    printf("[expose]   %s: text DIFFS=%zu shown=%zu%s\n", sub.c_str(),
                           text.diff_total, text.first.size(),
                           text.relocs_loaded ? "" : " (NO reloc allowlist)");
                    for (const auto& f : text.first) {
                        printf("[expose]     rva=0x%lx mem=%02X file=%02X\n",
                               (unsigned long)f.rva, f.mem_byte, f.file_byte);
                    }
                }
            }
            return 0;
        }
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
