// On-demand equipment-icon rasterizer: reads the game's own VPK
// (panorama/images/icons/equipment/<name>.vsvg_c), pulls the embedded SVG XML out of
// the vsvg_c container, rasterizes with nanosvg and hands the RGBA buffer to a
// VulkanHook texture slot. Used by the inventory tab / player list (VpkIcons.h).
//
// All file IO happens once per icon on the menu thread - the dir VPK tree (~70MB of
// directory entries on 2026-10 builds) is parsed once lazily; individual vsvg_c blobs
// live in the pak01_NNN archives.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION
#include <ThirdParty/nanosvg/nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <ThirdParty/nanosvg/nanosvgrast.h>

#include <Hooks/Graphics/VulkanHook.h>
#include <Platform/Linux/LinuxPlatformApi.h>

namespace vpk_icons
{

inline constexpr int kIconSize = 40; // rasterize target height in px (width keeps aspect)

namespace detail
{

[[nodiscard]] inline int openVpkDir() noexcept
{
    constexpr const char* kPaths[]{
        "/mnt/HDD2/SteamLibrary/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk",
        "/mnt/HDD1/SteamLibrary/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk",
    };
    for (const auto* path : kPaths)
        if (const int fd = LinuxPlatformApi::open(path, O_RDONLY); fd >= 0)
            return fd;
    return -1;
}

[[nodiscard]] inline bool readExact(int fd, void* buffer, std::size_t size, off_t offset) noexcept
{
    auto* out = static_cast<std::uint8_t*>(buffer);
    std::size_t done = 0;
    while (done < size) {
        const auto n = LinuxPlatformApi::pread(fd, out + done, size - done, offset + static_cast<off_t>(done));
        if (n <= 0)
            return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}

[[nodiscard]] inline const char* readCStr(std::uint8_t* data, std::size_t size, std::size_t& cursor) noexcept
{
    if (cursor >= size)
        return nullptr;
    const auto start = cursor;
    while (cursor < size && data[cursor] != 0)
        ++cursor;
    if (cursor >= size)
        return nullptr;
    data[cursor] = 0;
    const char* result = reinterpret_cast<const char*>(data + start);
    ++cursor;
    return result;
}

// VPK v2 directory tree: <header 28> then extension\0 { path\0 { name\0 entry(18) } }.
struct DirEntry {
    std::uint16_t archiveIndex;
    std::uint32_t offset;
    std::uint32_t length;
};

inline DirEntry dirEntries[128];
inline char dirNames[128][40];
inline int dirCount = 0;
inline bool dirLoaded = false;
inline bool dirFailed = false;

inline void loadVpkDir() noexcept
{
    if (dirLoaded || dirFailed)
        return;
    const int fd = openVpkDir();
    if (fd < 0) {
        dirFailed = true;
        return;
    }
    std::uint8_t header[28];
    if (!readExact(fd, header, sizeof(header), 0)) {
        LinuxPlatformApi::close(fd);
        dirFailed = true;
        return;
    }
    std::uint32_t signature, version, treeSize;
    std::memcpy(&signature, header, 4);
    std::memcpy(&version, header + 4, 4);
    std::memcpy(&treeSize, header + 8, 4);
    if (signature != 0x55AA1234u || (version != 2 && version != 1)) {
        LinuxPlatformApi::close(fd);
        dirFailed = true;
        return;
    }
    auto* tree = static_cast<std::uint8_t*>(std::malloc(treeSize));
    if (!tree) {
        LinuxPlatformApi::close(fd);
        dirFailed = true;
        return;
    }
    if (!readExact(fd, tree, treeSize, 28)) {
        std::free(tree);
        LinuxPlatformApi::close(fd);
        dirFailed = true;
        return;
    }
    LinuxPlatformApi::close(fd);

    std::size_t cursor = 0;
    while (dirCount < 128) {
        const char* ext = readCStr(tree, treeSize, cursor);
        if (!ext || !ext[0])
            break;
        const bool isSvg = std::strcmp(ext, "vsvg_c") == 0;
        while (dirCount < 128) {
            const char* path = readCStr(tree, treeSize, cursor);
            if (!path || !path[0])
                break;
            const bool isEquipment = isSvg && std::strstr(path, "icons/equipment") != nullptr;
            while (dirCount < 128) {
                const char* name = readCStr(tree, treeSize, cursor);
                if (!name || !name[0])
                    break;
                std::uint32_t crc, entryOffset, entryLength;
                std::uint16_t preload, archiveIndex, terminator;
                std::memcpy(&crc, tree + cursor, 4);
                std::memcpy(&preload, tree + cursor + 4, 2);
                std::memcpy(&archiveIndex, tree + cursor + 6, 2);
                std::memcpy(&entryOffset, tree + cursor + 8, 4);
                std::memcpy(&entryLength, tree + cursor + 12, 4);
                std::memcpy(&terminator, tree + cursor + 16, 2);
                cursor += 18 + preload; // entry + inline preload payload
                if (!isEquipment)
                    continue;
                std::snprintf(dirNames[dirCount], sizeof(dirNames[dirCount]), "%s", name);
                dirEntries[dirCount] = DirEntry{archiveIndex, entryOffset, entryLength};
                ++dirCount;
            }
        }
    }
    std::free(tree);
    dirLoaded = true;
}

// Extract the SVG XML from a vsvg_c blob: the container header carries the raw
// '<svg ...>...</svg>' text verbatim (verified on 2026-10-10 build, ak47.vsvg_c).
[[nodiscard]] inline char* extractSvgXml(const std::uint8_t* blob, std::size_t size) noexcept
{
    const std::size_t limit = size < 4096 ? size : 4096;
    for (std::size_t i = 0; i + 5 < limit; ++i) {
        if (blob[i] == '<' && std::memcmp(blob + i, "<svg ", 5) == 0) {
            for (std::size_t j = i; j < size; ++j) {
                if (std::memcmp(blob + j, "</svg>", 6) == 0) {
                    const std::size_t length = j + 6 - i;
                    auto* xml = static_cast<char*>(std::malloc(length + 1));
                    if (xml) {
                        std::memcpy(xml, blob + i, length);
                        xml[length] = '\0';
                    }
                    return xml;
                }
            }
            return nullptr;
        }
    }
    return nullptr;
}

inline int iconSlotCursor = 0;

} // namespace detail

// Rasterize one equipment icon by name into a Vulkan texture slot; returns the descriptor
// once ready (nullptr while pending or on failure). Slot allocation is stable per name via
// a tiny linear cache so repeated calls (every menu frame) don't re-request.
struct Slot {
    char name[40];
    void* descriptor;
    float aspect;
    std::atomic<bool> requested;
    std::atomic<bool> failed;
};
inline static Slot slots[VulkanHook::lua_texture::kMaxIconSlots];

[[nodiscard]] inline void* icon(const char* name, float scale = 1.0f) noexcept
{

    if (!name || !name[0])
        return nullptr;
    Slot* slot = nullptr;
    for (auto& s : slots) {
        if (std::strcmp(s.name, name) == 0) {
            slot = &s;
            break;
        }
        if (s.name[0] == '\0' && !slot)
            slot = &s;
    }
    if (!slot)
        return nullptr;
    if (slot->failed.load(std::memory_order_acquire))
        return nullptr;

    if (!slot->requested.load(std::memory_order_acquire)) {
        // Claim the slot, load + rasterize once (async upload completes over the next
        // frames - the descriptor arrives via query() below).
        std::snprintf(slot->name, sizeof(slot->name), "%s", name);
        slot->requested.store(true, std::memory_order_release);

        detail::loadVpkDir();
        int found = -1;
        for (int i = 0; i < detail::dirCount; ++i) {
            if (std::strcmp(detail::dirNames[i], name) == 0) {
                found = i;
                break;
            }
        }
        bool failed = true;
        do {
            if (found < 0)
                break;
            const auto& entry = detail::dirEntries[found];
            if (entry.archiveIndex == 0x7FFF)
                break; // lives in the dir vpk data section - not the archive layout we read

            char archivePath[192];
            std::snprintf(archivePath, sizeof(archivePath),
                "/mnt/HDD2/SteamLibrary/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_%03d.vpk",
                static_cast<int>(entry.archiveIndex));
            int fd = LinuxPlatformApi::open(archivePath, O_RDONLY);
            if (fd < 0) {
                std::snprintf(archivePath, sizeof(archivePath),
                    "/mnt/HDD1/SteamLibrary/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_%03d.vpk",
                    static_cast<int>(entry.archiveIndex));
                fd = LinuxPlatformApi::open(archivePath, O_RDONLY);
                if (fd < 0)
                    break;
            }
            auto* blob = static_cast<std::uint8_t*>(std::malloc(entry.length));
            bool ok = blob && detail::readExact(fd, blob, entry.length, static_cast<off_t>(entry.offset));
            LinuxPlatformApi::close(fd);
            if (!ok) {
                std::free(blob);
                break;
            }
            char* xml = detail::extractSvgXml(blob, entry.length);
            std::free(blob);
            if (!xml)
                break;

            auto* image = nsvgParse(xml, "px", 96.0f);
            std::free(xml);
            if (!image)
                break;
            if (image->height > 0.0f)
                slot->aspect = image->width / image->height;
            const float targetHeight = static_cast<float>(kIconSize) * scale;
            const float rasterScale = targetHeight / image->height;
            const int width = static_cast<int>(image->width * rasterScale);
            const int height = static_cast<int>(image->height * rasterScale);
            if (width <= 0 || height <= 0 || width > 512 || height > 512) {
                nsvgDelete(image);
                break;
            }
            auto* rasterizer = nsvgCreateRasterizer();
            auto* pixels = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(width) * height * 4));
            if (!rasterizer || !pixels) {
                std::free(pixels);
                nsvgDeleteRasterizer(rasterizer);
                nsvgDelete(image);
                break;
            }
            nsvgRasterize(rasterizer, image, 0, 0, rasterScale, pixels, width, height, width * 4);
            nsvgDeleteRasterizer(rasterizer);
            nsvgDelete(image);

            failed = false;
            const int slotIndex = VulkanHook::lua_texture::kIconSlotBase + (slot - slots);
            VulkanHook::lua_texture::request(slotIndex, pixels, width, height);
        } while (false);
        if (failed)
            slot->failed.store(true, std::memory_order_release);
        return nullptr;
    }

    if (!slot->descriptor)
        slot->descriptor = VulkanHook::lua_texture::query(VulkanHook::lua_texture::kIconSlotBase + (slot - slots));
    return slot->descriptor;
}

// Aspect (width/height) of a loaded icon, 1.0 until the first load completes.
[[nodiscard]] inline float iconAspect(const char* name) noexcept
{
    for (auto& s : slots)
        if (std::strcmp(s.name, name) == 0)
            return s.aspect;
    return 1.0f;
}

}