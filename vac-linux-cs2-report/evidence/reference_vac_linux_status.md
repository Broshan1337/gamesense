---
name: reference-vac-linux-status
description: "Research on whether VAC runs on Linux CS2"
metadata:
  node_type: memory
  type: reference
  originSessionId: current
  modified: 2026-08-30
---

## VAC on Linux CS2 - Status Unknown

**TL;DR: No authoritative public documentation confirms whether VAC runs on Linux CS2.**

### What We Know

1. **VAC integrity**: Confirmed NO memory/hook scanning - no `/proc/self/maps`, no `.text` checksum, no `ptrace` in libclient/libengine2 (see [[reference_cs2_re_findings.md:720-721]]). The only integrity check is the `move_crc` self-consistency proof, entirely client-built.

2. **Official matchmaking**: Status unclear. The Valve Developer Wiki was inaccessible during research.

3. **areweanticheatyet.com**: Shows VAC listed for games like ARK and DayZ running on Linux, suggesting VAC CAN run on Linux in some form. However, CS2-specific behavior is not documented.

### What VAC Historically Does (Windows)

- Kernel-mode components (VAC3/VAC4)
- Module CRC32 hashing (`AnalizePeModule` from the Andromeda commit)
- Memory pattern scanning
- Return address verification
- Send results to Valve servers for ban decisions

### Key Differences for Linux

- No kernel-mode driver possible (userland only)
- `/proc/self/maps` would be the Linux equivalent for module enumeration
- LD_PRELOAD and ptrace are detection vectors

### Empirical Verification (if you want to be certain)

```bash
# Check for VAC modules when connected to a secure server
cat /proc/$(pidof cs2)/maps | grep -i vac
ls -la ~/.steam/steam/steamapps/common/Counter-Strike\ 2/game/bin/linuxsteamrt64/ | grep -i vac

# Monitor network traffic for VAC-related communication
strace -f -e trace=network -p $(pidof cs2) 2>&1 | grep -i vac

# Check loaded shared libraries
pmap $(pidof cs2) | grep -i vac
```

### The Andromeda CRC32 Spoofing Code

The linked commit (`006ff8bea0ada49face963b9e733b5ca1a97e03b`) is **Windows-specific**:

1. **PEB/LDR structures**: `__readgsqword(0x60)`, `PEB`, `LDR_DATA_TABLE_ENTRY` - Windows-only
2. **Module handle semantics**: Windows `HANDLE` = module base address; Linux uses `void*` directly
3. **`AnalizePeModule`**: PE-format specific; Linux uses ELF

**For Linux equivalent**, you'd need:
- `/proc/self/maps` parsing for module enumeration
- ELF header parsing instead of PE
- `/proc/self/exe` or `dl_iterate_phdr` for module info
- CRC32 of ELF `.text` section

### Bottom Line

For this project's scope, VAC is not active regardless of platform. The CRC32 spoofing is unnecessary. If you want to use this on official servers, you'd need to do your own research into VAC's Linux behavior.

---

## IMPLEMENTATION STATUS (2026-08-30)

**VAC hooks now implemented for Linux.** See `Source/hooks/vac_hook.cpp`.

### Implementation Details

| Component | File | Description |
|-----------|------|-------------|
| VAC Hook | `vac_hook.cpp` | Hooks `analyze_module` in steamservice.so |
| Integrity Cache | `integrity_audit.cpp` | Caches module CRC32s for spoofing |
| InlineHook | `vac_hook.h` | Trampoline-based inline hooking |

### How It Works

1. **Module Enumeration**: `/proc/self/maps` is read at init to find our base address
2. **Cache**: Stores base address with CRC=0 for libOsiris.so
3. **Hook**: Intercepts VAC's `analyze_module` function in steamservice.so
4. **Spoof**: When VAC tries to hash libOsiris.so, returns cached "clean" CRC instead

### Key Function Addresses (steamservice.so)

| Function | Offset | Description |
|----------|--------|-------------|
| `analyze_module` | ~0x88cd70 | Main VAC hashing function |
| `CRC32_Init` | 0x88cd40 | Initialize CRC |
| `CRC32_ProcessBuffer` | 0x88cd70 | Process buffer |
| `CRC32_Final` | 0x88cd50 | Finalize CRC |

### Usage

The VAC hook is installed automatically by `hook_manager.cpp` on injection. No user configuration needed.

### Limitations

- Pattern may break on steamservice.so updates (needs re-derivation)
- Hook itself modifies steamservice.so code (potentially detectable)
- Only handles CRC32 spoofing, not memory dumps or string reads

### Related

- Full RE: [[reference_vac_linux_internal]]
- Implementation: `Source/hooks/vac_hook.{h,cpp}`, `Source/hooks/integrity_audit.{h,cpp}`
