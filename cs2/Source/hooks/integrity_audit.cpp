#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "integrity_audit.h"
#include "vac_str.h"

#include <UI/ImGui/GuiLog.h>
#include "vac_hook.h"

#include <Platform/Linux/LinuxDynamicLibrary.h>
#include <Platform/Linux/LinuxPlatformApi.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <linux/limits.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>
#include <link.h>

namespace security::integrity
{

namespace
{

constexpr std::size_t kMaxModules = 32;

struct ModuleCache
{
    ModuleInfo modules[kMaxModules];
    std::size_t count{0};
};

ModuleCache g_module_cache;

std::uint32_t calculate_crc32(const std::uint8_t* data, std::size_t size) noexcept
{
    static constexpr std::uint32_t crc32_table[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
        0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
        0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
        0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
        0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
        0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
        0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
        0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
        0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
        0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
        0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
        0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
        0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
        0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
        0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
        0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
        0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
        0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
        0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
        0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
        0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
        0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    };
    
    std::uint32_t crc = 0xFFFFFFFF;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// Own module name + diagnostic formats live XOR-encrypted (vac_str.h): the
// module name must not sit in .rodata as a scannable byte string, and the
// diagnostics describe evasion outright. Plaintext exists only in transient
// stack buffers (and our own log file, which is the point of logging).
namespace
{
VAC_XSTR(kModName, "libMangoHud.so");

VAC_XSTR(kIntInit, "[Integrity] Initializing module cache...");
VAC_XSTR(kIntCachedClient, "[Integrity] Cached libclient.so: base=0x%llx crc=0x%08X size=0x%zx");
VAC_XSTR(kIntNoClient, "[Integrity] Warning: libclient.so not loaded");
VAC_XSTR(kIntCachedSelf, "[Integrity] Cached libMangoHud.so: base=0x%llx (CRC=0 for spoofing)");
VAC_XSTR(kIntReady, "[Integrity] Spoofing initialized - VAC will see fake clean ELF");
VAC_XSTR(kIntCleared, "[Integrity] Module cache cleared");

template <std::size_t N>
void int_log(const vac_str::Encrypted<N>& msg)
{
    char plain[192];
    static_assert(N <= sizeof(plain), "integrity log line too long");
    msg.decrypt(plain);
    gui_log::write(plain);
}

template <std::size_t N, typename... Args>
void int_log_fmt(const vac_str::Encrypted<N>& fmt, Args... args)
{
    // gui_log::write carries no __attribute__((format)): a decrypted runtime
    // format keeps -Wformat-security quiet while staying out of .rodata.
    char plain[192];
    static_assert(N <= sizeof(plain), "integrity log line too long");
    fmt.decrypt(plain);
    gui_log::write(plain, args...);
}

void mod_name(char* out, std::size_t n) noexcept
{
    if (n >= decltype(kModName)::decrypted_size())
        kModName.decrypt(out);
    else if (n)
        out[0] = '\0';
}
} // namespace

struct FakeFile {
    int fd{-1};
    std::size_t offset{0};
    std::size_t size{0};
    std::uint8_t* data{nullptr};
    bool active{false};
};

constexpr int kMaxFakeFiles = 4;
FakeFile g_fake_files[kMaxFakeFiles];

std::uint8_t g_fake_cheat_content[256];
std::size_t g_fake_cheat_size = 0;
bool g_spoofing_ready = false;

void init_fake_content()
{
    constexpr std::uint8_t fake_elf_header[] = {
        0x7F, 'E', 'L', 'F', 2, 1, 1, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        3, 0, 0x3E, 0, 1, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0x40, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0x40, 0, 0x38, 0,
        1, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0x40, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0x40, 0, 0, 0, 0, 0, 0, 0,
        0x38, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        1, 0, 0, 0, 1, 0, 0, 0,
        5, 0, 0, 0, 0x40, 0, 0, 0
    };
    
    g_fake_cheat_size = sizeof(fake_elf_header);
    std::memcpy(g_fake_cheat_content, fake_elf_header, g_fake_cheat_size);
    
    std::uint32_t clean_crc = calculate_crc32(g_fake_cheat_content, g_fake_cheat_size);
    
    ModuleInfo* existing = get(0);
    if (existing && !existing->valid)
    {
        char mod[32];
        mod_name(mod, sizeof(mod));
        for (std::size_t i = 0; i < g_module_cache.count; ++i)
        {
            if (std::strstr(reinterpret_cast<char*>(g_module_cache.modules[i].module_base), mod))
            {
                g_module_cache.modules[i].crc32 = clean_crc;
                g_module_cache.modules[i].valid = true;
                break;
            }
        }
    }
    
    g_spoofing_ready = true;
}

bool is_cheat_path_raw(const char* path)
{
    if (!path) return false;

    // Note: the old "/tmp/libMangoHud.so" special case is subsumed by the
    // module-name check (it contains the same substring), so one check does it.
    char mod[32];
    mod_name(mod, sizeof(mod));
    if (std::strstr(path, mod)) return true;

    char buf[PATH_MAX];
    if (::realpath(path, buf))
    {
        if (std::strstr(buf, mod)) return true;
    }

    return false;
}

}

ModuleInfo* get(std::uintptr_t module_base)
{
    for (std::size_t i = 0; i < g_module_cache.count; ++i)
    {
        if (g_module_cache.modules[i].module_base == module_base)
            return &g_module_cache.modules[i];
    }
    return nullptr;
}

void add(std::uintptr_t module_base, std::uint32_t crc32)
{
    for (std::size_t i = 0; i < g_module_cache.count; ++i)
    {
        if (g_module_cache.modules[i].module_base == module_base)
        {
            g_module_cache.modules[i].crc32 = crc32;
            g_module_cache.modules[i].valid = true;
            return;
        }
    }
    
    if (g_module_cache.count < kMaxModules)
    {
        g_module_cache.modules[g_module_cache.count] = {module_base, crc32, true};
        ++g_module_cache.count;
    }
}

int open_spoofed_file(const char* path, int flags, mode_t mode)
{
    (void)flags;
    (void)mode;
    if (!g_spoofing_ready || !is_cheat_path_raw(path))
        return -1;

    // memfd-backed, not pipe: the fd is seekable, pread-able and mmap-able, so
    // fread/lseek/mmap/fstat on it behave like a real (tiny) ELF file instead
    // of failing with ESPIPE or reporting S_FIFO. Plain reads work through the
    // kernel with no hook involvement, so consumer-side lseek stays coherent
    // (no userspace offset shadow to drift).
    char memfd_name[32];
    mod_name(memfd_name, sizeof(memfd_name));
    int memfd = ::memfd_create(memfd_name, MFD_CLOEXEC);
    if (memfd < 0)
        return -1;

    if (write(memfd, g_fake_cheat_content, g_fake_cheat_size) != static_cast<ssize_t>(g_fake_cheat_size))
    {
        close(memfd);
        return -1;
    }

    if (::lseek(memfd, 0, SEEK_SET) < 0)
    {
        close(memfd);
        return -1;
    }

    const int read_fd = memfd;

    for (int i = 0; i < kMaxFakeFiles; ++i)
    {
        if (!g_fake_files[i].active)
        {
            g_fake_files[i].fd = read_fd;
            g_fake_files[i].size = g_fake_cheat_size;
            g_fake_files[i].offset = 0;
            g_fake_files[i].active = true;
            return read_fd;
        }
    }

    close(read_fd);
    return -1;
}

ssize_t read_spoofed_file(int fd, void* buf, std::size_t count)
{
    for (int i = 0; i < kMaxFakeFiles; ++i)
    {
        if (g_fake_files[i].active && g_fake_files[i].fd == fd)
        {
            const std::size_t remaining = g_fake_files[i].size - g_fake_files[i].offset;
            const std::size_t to_read = (count < remaining) ? count : remaining;
            
            if (to_read > 0)
            {
                std::memcpy(buf, g_fake_cheat_content + g_fake_files[i].offset, to_read);
                g_fake_files[i].offset += to_read;
            }
            
            return static_cast<ssize_t>(to_read);
        }
    }
    return -1;
}

bool should_spoof_fd(int fd)
{
    for (int i = 0; i < kMaxFakeFiles; ++i)
    {
        if (g_fake_files[i].active && g_fake_files[i].fd == fd)
            return true;
    }
    return false;
}

void close_spoofed_file(int fd)
{
    for (int i = 0; i < kMaxFakeFiles; ++i)
    {
        if (g_fake_files[i].active && g_fake_files[i].fd == fd)
        {
            close(fd);
            g_fake_files[i].active = false;
            g_fake_files[i].fd = -1;
            break;
        }
    }
}

bool initialize()
{
    int_log(kIntInit);

    init_fake_content();
    
    LinuxDynamicLibrary client("libclient.so");
    if (client)
    {
        const auto codeSection = client.getCodeSection();
        const auto rawSection = codeSection.raw();
        if (rawSection.data())
        {
            const auto linkMap = client.getLinkMap();
            if (linkMap)
            {
                const auto base = reinterpret_cast<std::uintptr_t>(linkMap->l_addr);
                const auto crc = calculate_crc32(reinterpret_cast<const std::uint8_t*>(rawSection.data()), rawSection.size());
                
                add(base, crc);
                int_log_fmt(kIntCachedClient,
                            static_cast<unsigned long long>(base), crc, rawSection.size());
            }
        }
    }
    else
    {
        int_log(kIntNoClient);
    }
    
    const auto mapsFd = LinuxPlatformApi::open("/proc/self/maps", O_RDONLY);
    if (mapsFd >= 0)
    {
        char line[512];
        std::size_t lineLength = 0;
        std::int64_t fileOffset = 0;
        
        while (true)
        {
            char chunk[4096];
            const auto n = LinuxPlatformApi::pread(mapsFd, chunk, sizeof(chunk), fileOffset);
            if (n <= 0)
                break;
            fileOffset += n;
            
            for (std::int64_t i = 0; i < n; ++i)
            {
                const char c = chunk[i];
                if (c == '\n')
                {
                    line[lineLength] = '\0';
                    lineLength = 0;
                    
                    char mod[32];
                    mod_name(mod, sizeof(mod));
                    if (std::strstr(line, mod) != nullptr)
                    {
                        std::uintptr_t base = 0;
                        std::sscanf(line, "%lx", &base);

                        if (base)
                        {
                            add(base, 0);
                            int_log_fmt(kIntCachedSelf,
                                        static_cast<unsigned long long>(base));
                            fva::hooks::security::regions::add(reinterpret_cast<void*>(base), 0x100000);
                        }
                    }
                }
                else if (lineLength + 1 < sizeof(line))
                {
                    line[lineLength++] = c;
                }
                else
                {
                    lineLength = 0;
                }
            }
        }
        
        LinuxPlatformApi::close(mapsFd);
    }
    
    int_log(kIntReady);
    return true;
}

void shutdown()
{
    g_module_cache.count = 0;
    g_spoofing_ready = false;
    
    for (int i = 0; i < kMaxFakeFiles; ++i)
    {
        if (g_fake_files[i].active)
        {
            close(g_fake_files[i].fd);
            g_fake_files[i].active = false;
        }
    }
    
    int_log(kIntCleared);
}

void forget_spoofed_fd(int fd) noexcept
{
    for (int i = 0; i < kMaxFakeFiles; ++i)
    {
        if (g_fake_files[i].active && g_fake_files[i].fd == fd)
        {
            g_fake_files[i].active = false;
            g_fake_files[i].fd = -1;
            break;
        }
    }
}

bool is_cheat_path(const char* path) noexcept
{
    if (!path) return false;
    return is_cheat_path_raw(path);
}

bool is_ready() noexcept
{
    return g_spoofing_ready;
}

} // namespace security::integrity
