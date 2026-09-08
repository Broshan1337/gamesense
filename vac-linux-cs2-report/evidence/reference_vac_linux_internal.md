---
name: reference-vac-linux-internal
description: "VAC internal reverse engineering on Linux - module hashing and detection mechanisms"
metadata:
  node_type: memory
  type: reference
  originSessionId: current
  modified: 2026-08-30
---

## OFFLINE STATIC AUDIT: NO MEMORY-INTEGRITY CHECKS IN SHIPPED BINARIES (2026-08-30)

Question: does VAC verify game .text integrity in memory (which our patches/hooks would trip)?
Method: full offline audit of BOTH shipped 64-bit binaries (`~/.local/share/Steam/linux64/steamclient.so`,
`steamrt64/steamservice.so`, both NOT stripped, copies in /tmp/opencode/vac/). Both are ~7.8MB/3MB
of code living in a symbol-less gap after the last export — nm annotations inside are garbage;
classify by disassembly + rip-relative string resolution instead.

**Result: NO memory-integrity checking exists in the shipped binaries. Evidence chain:**

1. **No cross-process read primitives in either binary**: no `process_vm_readv`/`ptrace` imports;
   no `/proc/<pid>/mem` or other-pid `/proc/<pid>/maps` format strings; `pread64` imported but
   ZERO call sites (vestigial — only its own PLT GOT jmp references it).
2. **Every hash implementation enumerated, all consumers mundane.** Only 5 `crc32`-instruction
   sites per binary = 1 CRC32C function each: Valve's `CRC32_ProcessBuffer` (steamclient
   0x220ea30 wrapper / 0x220eab0 core; the string "CRC32_ProcessBuffer" is its stats name,
   referenced at 0x2d1e828). Table-driven CRC32 does NOT exist (no 0x77073096 table start, no
   poly constants outside one PCLMUL SIMD zlib kernel at 0x1f8xxxx). No FNV/xxHash/SHA/MD5
   constants outside OpenSSL. ALL 20 CRC32_ProcessBuffer callers in steamclient + all 34 in
   steamservice are mundane: zip/VPK helpers, file transfer, netfilter crypto, KeyValues,
   user/license cache, depot logging, rbtree key hashing. None touches a module range.
3. **Game-side VAC pipeline fully mapped** (steamclient.so): parser 0x2232dd0 = fopen
   `/proc/self/maps` (via `__wrap_fopen`, Valve's own --wrap layer) → fgets 0x40c8 buffer →
   sscanf → range-filter 0x22267c0 → collect (0x2251ee0). Its ONLY caller 0x2232f09 takes a
   module record, strcmps names, parses maps for that module's ADDRESS RANGE, then sends
   **name + mapped range only** via `CMsgClientServiceModule` protobuf. No file open, no hash,
   no memory read — game side is a pure "what's loaded + where" reporter.
4. **TracerPid anti-debug checker exists in BOTH binaries** (steamservice 0x86bae0,
   steamclient 0x26f5820): fopen `/proc/<getpid()>/status`, fgets loop, `strncmp("TracerPid:",…,10)`,
   strtol(line+11) → bool "am I traced". Wrapped in rol $3/$d/$3d/$33 chains + `xchg %rbx,%rbx`
   markers + int3 padding = obfuscated anti-tamper code. **This checks its own process —
   relevant to OUR ptrace-based memfd injector: steamclient.so inside cs2 can see a ptrace
   attach to the game.** Short rounds minimize exposure; dynamic test pending.
5. **NEW: `VacProcessMonitor`/`ClientProcessMonitor`** (typeinfo in steamservice.so):
   `CClientProcessMonitor`, `CMonitoredProcess`, `CUtlMap<pid, VacProcessMonitor::ActiveProcessData_t>`
   — Steam tracks active processes. No method symbols; deep RE not done (not needed for our threat model yet).

**THE HARD LIMIT: VAC's live scan logic may NOT be in these binaries.** steamservice.so imports
`dlopen`/`dlsym` (single wrapped call site `__wrap_dlopen` 0x8d10c0) and runs a `DynamicModule_s`
manager (`servicemodulemanagerbase.cpp`, `m_mapModules`) fed by `CMsgClientServiceModule`/
`k_EMsgAMServiceModulesCall` IPC — VAC modules are delivered/downloaded at runtime. Static
analysis of the shipped binaries can never rule out an in-memory integrity check that arrives
as a service module. (Also why the "CRC32 from disk" conclusion needs dynamic confirmation.)

**Verification recipe used (reusable):** (a) syscall-stub semantics: PLT stubs do
`mov $NR,%r11d; jmp *GOT` — r11 IS the glibc syscall number (0x97=getpid); objdump's
nearest-symbol labels inside the symbol-less gap are all `HUF_decompress*/OPENSSL_*/ZSTD_*+off`
noise, ignore them; (b) `strings -t x` offsets == vaddrs for .rodata here (vaddr==file offset in
these files); (c) find fn starts by scanning back to `ret` followed by push/endbr64; (d) no
classic CRC-32 table (`00000000 96307707…`) exists in either binary — SSE4.2 `crc32` instr +
one PCLMUL kernel are the complete CRC inventory, making caller-classification exhaustive.

**Verdict: shipped binaries = module-list reporting only; our .text patches are not hashed by
anything shipped. Residual risk = dynamically-delivered VAC service modules (unknowable
offline) + delayed-ban waves. The TracerPid checker is the one NEW exposure for our ptrace
injector.** Next step if this matters: dynamic strace/IPC-dump (methods 2/3 of the audit plan).

**EMPIRICAL CORROBORATION (2026-08-30, user-reported): green trust, no kick, no trust-lowering
across ALL sessions with .text patches active** (legs 0xC3 SingleBytePatch, prologue detour
hooks, vtable patches, fopen/fgets maps hook). Consistent with the static audit: nothing
shipped hashes game .text. Still not proof (delayed waves / runtime service modules remain
unknowable), but the two evidence types now AGREE: static (no integrity code shipped) +
empirical (sustained green trust while patched).

---

## VAC IS ACTIVE ON LINUX CS2

**Status**: CONFIRMED - VAC runs on Linux and uses steamclient.so for module enumeration.

---

## Linux VAC Architecture

### Key Discovery (2026-08-30)

Unlike Windows where VAC injects into the game process, **Linux VAC is split**:

| Component | Location | Purpose |
|-----------|----------|---------|
| `steamservice.so` | Steam's process | VAC core logic, hash comparison |
| `steamclient.so` | CS2's process | Module enumeration, `/proc/self/maps` reading |

### How It Works

1. **VAC in Steam** (`steamservice.so`) requests module info via Steam IPC
2. **steamclient.so** (in CS2) reads `/proc/self/maps` to enumerate modules
3. **steamclient.so** sends module list back to Steam via IPC
4. **VAC** computes CRC32 of module files (from disk) and compares to known-cheat database

### Evidence

```
$ strings ~/.local/share/Steam/linux64/steamclient.so | grep "/proc/self/maps"
/proc/self/maps

$ strings ~/.local/share/Steam/linux64/steamclient.so | grep "LoadedModules"
g_VecLoadedModules[ iLoadedModules ].m_cRef > 0
g_VecLoadedModules
```

---

## VAC Network Protocol

| Message | Description |
|---------|-------------|
| `k_EMsgVACResponse` | Server response |
| `k_EMsgClientVACResponse` | Client response |
| `k_EMsgClientVACBanStatus` | Ban status |
| `k_EMsgClientServiceModule` | Module information |
| `k_EMsgAMServiceModulesCache` | Module cache |

---

## Implementation

### Our VAC Bypass Approach

We hook `fopen`/`fgets` in CS2 to intercept `/proc/self/maps` reading:

1. **fopen hook**: Detect when steamclient reads `/proc/self/maps`
2. **fgets hook**: Filter out `libOsiris.so` from module list
3. **Result**: VAC never sees our module

See `Source/hooks/vac_hook.cpp` for implementation.

### Alternative Approaches

1. **Hook g_VecLoadedModules**: Patch steamclient's internal module vector
2. **Hook CRC32 functions**: Return clean hashes (requires injection into Steam)
3. **Fileless injection**: Use memfd to avoid disk presence (optional)

## 2026-08-30 — LWSS 2020 ARTICLE CROSS-VALIDATION + THE /tmp CAPTURE TECHNIQUE

Source: https://lwss.github.io/State-Of-Vac-linux-2020/ (lwss = known Linux Steam RE). Cross-checks our 2026 audit:
- CONFIRMED by both: VAC-on-Linux architecture = steamclient.so downloads modules → runs them (export `runfunc`) → ships results to Valve. Our "VAC scan logic arrives as dlopen'd service modules (DynamicModule manager)" is the same mechanism, independently derived.
- CONFIRMED by both: TracerPid anti-debug (their snapprocess module reads /proc/pid/status for it; ours: found in BOTH shipped binaries).
- Their 2020 module catalog: snapprocess (cmdline/status/TracerPid/maps/mem), directoryscan (/proc/pid/cwd fts walk — the imgui.ini joke), verifyclient (steam's maps), hardwarescan (/sys/devices HWID — "anonymous" claim false).
- BEYOND their article (ours, 2026): CRC32 module-hashing machinery (SSE4.2+PCLMUL) in the shipped binaries; name+address-range reporter via CMsgClientServiceModule; full shipped-binary audit showing no memory-integrity checks static.
- **NEW TOOL WE WERE MISSING — closes our audit's "hard limit" (can't rule out runtime-delivered checks offline): VAC modules DROP TO /tmp as `<checksum>-<size>.so` after playing on a VAC-secured server.** Watch with `ls /tmp/*-*-*.so` after a VAC round (none present on this box — user plays on own server only). Captured modules are small (~0x3b00-0x4c00, ~25 functions, ~50 strings, embedded module NAMES in .rodata as a goof) → static-audit them with the same offline recipe as the shipped binaries. Alternative: Heep042/vaclog kernel module (echo biggest/smallest steam pids into /proc/vaclog, follow dmesg).
- VAC thread name on Linux: **ClientModuleMan** (also the crash reporter subject — their modules segfault sometimes; "Bad RIP value" in dmesg = their bug, not yours).
- OPSEC deltas for our injector: their snapprocess reads /proc/pid/mem + maps (memfd injection keeps the .so off the FILESYSTEM but maps still show the /memfd:/ or /proc/pid/fd/N path) and directoryscan walks cwd (our config/lets keep cwd clean-ish anyway).
