# What VAC actually does on Linux CS2 (2026)

Some background so you know where this is coming from. I've spent the last
couple weeks reverse engineering the Steam client binaries on Linux as part of
a personal research project, and the question that kept coming up was simple:
does VAC even do anything on Linux, or is it the same "barely exists" story
everyone repeats from 2020 forum posts?

Turns out nobody had written this up properly for CS2 in 2026, so I did the
work myself. Everything below comes from two things: a full static audit of
the shipped binaries, and (limited) dynamic observation. I'll say upfront
where my knowledge ends, because it ends in a pretty important place.

The binaries I audited:

- `~/.local/share/Steam/linux64/steamclient.so` (~7.8MB of code)
- `~/.local/share/Steam/steamrt64/steamservice.so` (~3MB)

Both unstripped, both current as of August 2026.

## TL;DR

- VAC **is** active on Linux CS2. It's not the ghost it's made out to be.
- The architecture is completely different from Windows. There's no injection
  into the game process. It's split across two Steam binaries and talks over
  Steam's IPC.
- Nothing in the shipped binaries does memory integrity checking. No .text
  hashing, no ptrace, no /proc/pid/mem reads. The actual scan logic gets
  downloaded and loaded at runtime as service modules, so a static audit
  can never fully rule that part out. More on that later.
- There IS an anti-debug check baked into both binaries (TracerPid), and a
  process monitor that tracks running game PIDs.

## The architecture

This was the most interesting finding. On Windows, VAC injects a module into
the game process and scans from inside. On Linux, that doesn't happen. Instead
it's split:

| Component | Lives in | Job |
|---|---|---|
| `steamservice.so` | Steam's process | VAC core, result handling |
| `steamclient.so` | CS2's process | module enumeration, reporting |

The flow works like this:

1. VAC in the Steam process asks for module info over Steam IPC.
2. `steamclient.so` inside the CS2 process opens `/proc/self/maps` and parses
   it (through Valve's own `__wrap_fopen` layer, which was a pain to trace).
3. It filters down to the address range of each loaded module and sends
   **just the name and mapped range** back via a protobuf message
   (`CMsgClientServiceModule`).

That last point matters. The game-side code is a pure "what's loaded and
where" reporter. I traced its only caller, and it does no file reading, no
hashing, no memory reads. The enumeration side is boring on purpose.

## What the scan logic actually is

Here's the part I want to be careful about. VAC's detection logic is not in
the shipped binaries. `steamservice.so` imports `dlopen` and runs a
`DynamicModule` manager that gets fed by IPC messages. In plain terms: VAC
modules are downloaded at runtime and loaded as shared objects.

lwss came to the same conclusion back in 2020 (his "State of VAC on Linux"
writeup is still the best public source, and my 2026 findings line up with
his independently — same delivery mechanism, same module structure). His
catalog of captured modules included things like `snapprocess` (reads
/proc/pid/cmdline, status, TracerPid, maps, mem), `directoryscan` (walks
your working directory, presumably looking for obvious cheat files),
`verifyclient`, and a `hardwarescan` that fingerprints hardware from
/sys — so much for "anonymous".

I cross-validated a few of these against the shipped binaries:

- The **TracerPid check** is in BOTH `steamclient.so` and `steamservice.so`.
  Both contain a function that opens `/proc/<own pid>/status`, reads through
  it with fgets, and does a `strncmp("TracerPid:", ..., 10)` followed by a
  strtol. So "am I being traced?" is a first-class question, wrapped in some
  genuinely obfuscated code (rol/xchg chains and int3 padding — whoever wrote
  it did not want it found easily).
- A **`VacProcessMonitor`** exists in steamservice.so — there are typeinfo
  strings for `CClientProcessMonitor` and `CUtlMap<pid, ActiveProcessData_t>`,
  so Steam keeps a map of running game processes. Didn't dig deeper, wasn't
  needed for what I was doing.

## What I could NOT find (and why that's a "so far")

I did an exhaustive audit of everything that ships:

- **No cross-process read primitives anywhere.** No `process_vm_readv`, no
  `ptrace` imports, no format strings for other processes' /proc entries.
- `pread64` is imported but has literally zero call sites (vestigial — only
  its own PLT stub points at it).
- **Every hash implementation is accounted for.** This was the tedious part.
  I enumerated every CRC32 implementation (it's all SSE4.2 `crc32`
  instructions plus one PCLMUL kernel — no classic table-driven CRC anywhere)
  and then walked every single caller. 20 callers in steamclient, 34 in
  steamservice, all of them mundane: zip/VPK handling, netfilter crypto,
  KeyValues, license caches, that kind of thing. Nobody hashes a module's
  memory. No FNV, xxHash, SHA, or MD5 constants outside OpenSSL either.

So the honest summary is: **the shipped binaries contain no memory integrity
checking.** If a scan hashes your modules or reads your memory, it arrives
later, as a dynamically delivered service module.

That's the hard limit of this research and I want to be upfront about it:
static analysis of what ships can never tell you what gets downloaded on
Tuesday. Anyone claiming "VAC does X on Linux" based on static analysis alone
(especially mine, if you're reading this from the repo) should carry that
asterisk.

One practical note if you want to look at these modules yourself: after a
session on a VAC-secured server, the downloaded modules get dropped to /tmp
as files named `<checksum>-<size>.so`. They're small (roughly 15-20KB, a few
dozen functions each, and amusingly the module names are embedded in .rodata
in plain text). There's also the vaclog kernel module approach from Heep042
if you'd rather watch them over dmesg. My own sessions happen on a private
server, so VAC never bothered loading anything on this box — the /tmp grab
remains something I've seen confirmed by others, not firsthand.

## Empirical side

Static findings matched behavior: across long sessions with an injected
module present, trust factor stayed green, no kicks, no weirdness. That's
consistent with "nothing shipped hashes module memory" but obviously proves
nothing about delayed ban waves or what a future downloaded module might do.
Delayed bans are VAC's whole thing. Nobody should read a green trust factor
as an all-clear, on any platform.

## Weird trivia

- The VAC thread on Linux is named `ClientModuleMan`. It also shows up as the
  crash reporter subject, because Valve's own modules segfault sometimes. If
  you ever see "Bad RIP value" in dmesg with that thread name, that's their
  bug, not yours.
- `/proc/self/maps` reading goes through Valve's `--wrap` fopen layer, which
  is a neat trick if you've never seen ld's symbol wrapping used in anger.
- The 2020 `hardwarescan` module collected HWIDs from /sys/devices, despite
  Valve's claims about VAC being anonymous. Decide for yourself how much that
  changed.

## Method, for anyone who wants to repeat this

Short version of the recipe (the full one is in the evidence folder):

1. Copy the binaries somewhere writable first — Steam likes to update them
   out from under you.
2. PLT stubs on this platform are `mov $NR,%r11d; jmp *GOT` — the r11 value
   IS the glibc syscall number, which lets you identify syscall stubs
   without symbols.
3. Ignore objdump's nearest-symbol labels inside the big symbol-less gap —
   they're all `HUF_decompress*`/`OPENSSL_*`/`ZSTD_*`+offset noise.
4. To find function starts, scan backwards from candidate code to the
   nearest `ret` followed by `push`/`endbr64`.
5. For string xrefs, `strings -t x` offsets equal vaddrs in these files.
6. To prove "no hashing of modules", enumerate ALL hash implementations
   first, then classify every caller. If you can enumerate the full set,
   caller-classification becomes exhaustive instead of suggestive. The
   absence of a classic CRC-32 table in both binaries is what made this
   tractable — SSE4.2 `crc32` + one PCLMUL kernel was the complete
   inventory.

`scratchpad/vac_pattern_scan.py` in the evidence folder has a working
example of scanning steamservice.so for specific VAC-related functions.

## Sources

- My own static audit (Aug 2026), full notes in `evidence/reference_vac_linux_internal.md`
- lwss — "State of VAC on Linux" (2020), https://lwss.github.io/State-Of-Vac-linux-2020/
  — cross-validated against above, agreement on architecture and delivery
- areweanticheatyet.com — confirms VAC runs on Linux for other titles
  (ARK, DayZ), nothing CS2-specific there

Questions/corrections welcome. This is a moving target — Steam updates these
binaries constantly, so if you repeat this work on a newer build, don't be
shocked if addresses shift (though the architecture has been stable since at
least 2020).
