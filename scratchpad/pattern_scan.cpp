// Offline pattern uniqueness scanner (no game needed).
//
// Recreated 2026-09-23 for the 1.41.8.2 update (the original cs2/build/pattern_scan.cpp
// did not survive the repo restructure). Same contract as the 2026-08-24 tool:
//   * extract each module's .text with objcopy (driver script does this)
//   * re-include the project's own MemoryPatterns.h pools (pattern vault included -
//     getView() decrypts in place, exactly like the live module does)
//   * count occurrences of EVERY pool pattern against its OWN module's .text
//     (module scoping per AllMemoryPatternSearchResults.h - the #1 porting trap)
//   * print pool + index + count + .text-relative match offsets for anything != 1
// Identify a failing pattern by grepping its hex in cs2/Source/MemoryPatterns/Linux/*.h.
//
// Build: g++ -std=c++20 -O2 -I cs2/Source -o pattern_scan scratchpad/pattern_scan.cpp \
//          -fconstexpr-depth=4096 -fbracket-depth=1024 -ftemplate-depth=2048
// Run:   ./pattern_scan <dir-with-<module>.text-files>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include <Platform/Macros/IsPlatform.h>
#include <MemoryPatterns/MemoryPatterns.h>
#include <MemorySearch/HybridPatternFinder.h>

namespace
{

struct ModuleText {
    std::vector<std::byte> bytes;
};

[[nodiscard]] bool loadText(const std::string& dir, const char* file, ModuleText& out)
{
    FILE* f = std::fopen((dir + "/" + file).c_str(), "rb");
    if (!f) {
        std::printf("!! cannot open %s/%s (run the objcopy driver first)\n", dir, file);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.bytes.resize(static_cast<std::size_t>(size));
    const std::size_t got = std::fread(out.bytes.data(), 1, out.bytes.size(), f);
    std::fclose(f);
    return size > 0 && got == out.bytes.size();
}

// Count + locate occurrences via the SAME finder the live module uses.
[[nodiscard]] std::vector<std::size_t> occurrences(const ModuleText& module, BytePattern pattern)
{
    std::vector<std::size_t> offsets;
    HybridPatternFinder finder{std::span<const std::byte>{module.bytes.data(), module.bytes.size()}, pattern};
    for (;;) {
        const auto* hit = finder.findNextOccurrence();
        if (!hit)
            break;
        offsets.push_back(static_cast<std::size_t>(hit - module.bytes.data()));
        if (offsets.size() >= 3)
            break; // report at most 3 matches; >= 2 already means broken
    }
    return offsets;
}


// Pool-order -> pattern-name mapping straight from the pool's TypeList
// (no source parsing - the earlier python mapper misordered multi-method headers).
template <typename T>
const char* typeName() noexcept { return __PRETTY_FUNCTION__; }

template <typename PoolType, std::size_t... I>
void printNamesAt(std::index_sequence<I...>)
{
    using Types = typename PoolType::PatternPool::PatternTypes;
    (std::printf("    %3zu %s\n", I,
                 typeName<std::tuple_element_t<I, typename Types::TypesTuple>>()), ...);
}

template <typename PoolType>
void printPoolNames(const char* poolName)
{
    using Types = typename PoolType::PatternPool::PatternTypes;
    std::printf("NAMES %s (%zu)\n", poolName, Types::size());
    printNamesAt<PoolType>(std::make_index_sequence<Types::size()>{});
}

template <typename Pool>
void scanPool(const char* poolName, Pool& pool, const ModuleText& module)
{
    if (module.bytes.empty()) {
        std::printf("## %s: SKIPPED (no .text loaded)\n", poolName);
        return;
    }
    int index = 0;
    int bad = 0;
    pool.getView().forEach([&](BytePattern pattern, std::uint8_t offset, CodePatternOperation operation) {
        (void)offset;
        (void)operation;
        const auto offsets = occurrences(module, pattern);
        if (offsets.size() != 1) {
            ++bad;
            std::printf("BAD %s[%d] count=%zu hex=", poolName, index, offsets.size());
            for (char c : pattern.raw())
                std::printf("%02x", static_cast<unsigned char>(c));
            std::printf(" matches:");
            for (const auto off : offsets)
                std::printf(" +%#zx", off);
            std::printf("\n");
        }
        ++index;
    });
    std::printf("## %s: %d pattern(s) scanned, %d broken\n", poolName, index, bad);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: %s <dir-with-<module>.text-files>\n", argv[0]);
        return 1;
    }
    const std::string dir = argv[1];

    ModuleText client, tier0, soundSystem, fileSystem, panorama, sceneSystem, schemaSystem;
    const bool ok = loadText(dir, "client", client)
        && loadText(dir, "tier0", tier0)
        && loadText(dir, "soundsystem", soundSystem)
        && loadText(dir, "filesystem", fileSystem)
        && loadText(dir, "panorama", panorama)
        && loadText(dir, "scenesystem", sceneSystem)
        && loadText(dir, "schemasystem", schemaSystem);
    if (!ok)
        return 1;

    printPoolNames<decltype(kClientPatterns)>("client");
    scanPool("client", kClientPatterns, client);
    printPoolNames<decltype(kTier0Patterns)>("tier0");
    scanPool("tier0", kTier0Patterns, tier0);
    printPoolNames<decltype(kSoundSystemPatterns)>("soundsystem");
    scanPool("soundsystem", kSoundSystemPatterns, soundSystem);
    printPoolNames<decltype(kFileSystemPatterns)>("filesystem");
    scanPool("filesystem", kFileSystemPatterns, fileSystem);
    printPoolNames<decltype(kPanoramaPatterns)>("panorama");
    scanPool("panorama", kPanoramaPatterns, panorama);
    printPoolNames<decltype(kSceneSystemPatterns)>("scenesystem");
    scanPool("scenesystem", kSceneSystemPatterns, sceneSystem);
    printPoolNames<decltype(kSchemaSystemPatterns)>("schemasystem");
    scanPool("schemasystem", kSchemaSystemPatterns, schemaSystem);
    std::printf("done\n");
    return 0;
}