# Airlock Architecture

Airlock brings Windows programs to Linux by supplying the pieces a Windows
binary expects from the OS. This document describes how those pieces are
structured and how they fit together.

## Overview

```
   Windows binary (.exe / .dll)
        │  raw bytes
        ▼
 ┌───────────────┐    ┌─────────────────┐    ┌──────────────────────────┐
 │  PE loader     │──▶│  Image model     │──▶│  CPU / ABI emulation     │
 │  (src/loader)  │    │  airlock_image_t  │    │  (planned — see roadmap) │
 └───────────────┘    └────────┬─────────┘    └──────────────────────────┘
                               │ imports
                               ▼
                  ┌──────────────────────────┐
                  │  Win32 API layer          │
                  │  (src/win32)              │
                  │  KERNEL32.dll, USER32.dll │
                  │  ... → Linux syscalls     │
                  └──────────────────────────┘
```

There are three responsibilities, each intentionally decoupled:

1. **Parsing** — turn raw bytes into a validated, structured description of
   the PE (headers, sections, imports, exports, relocations).
2. **Mapping** — assemble a section-aligned *virtual image* so that relative
   virtual addresses (RVAs) and the entry point behave like they do on
   Windows.
3. **Execution** — run the entry point, resolving every call into a Win32 API
   Airlock implements. *(Not yet implemented — see ROADMAP.md.)*

## The PE loader (`src/loader`)

### Format definitions — `include/airlock/pe.h`

The PE/COFF layout is captured as **packed** structs that mirror the on-disk
format exactly (e.g. `IMAGE_DOS_HEADER`, `IMAGE_FILE_HEADER`,
`IMAGE_OPTIONAL_HEADER`, `IMAGE_SECTION_HEADER`, the import/export
directories). Because they are packed, the parser can map a file buffer onto
them directly. All on-disk integers are little-endian and are read through
`airlock_le16/32/64` helpers, so the loader is correct regardless of host
byte order.

### Parsing — `src/loader/pe.c`

`airlock_parse_headers()` validates signatures (`MZ`, `PE\0\0`, optional-header
magic `PE32`/`PE32+`), checks that every claimed structure fits inside the
buffer, and populates the header fields plus the section table. It returns a
typed `airlock_status_t` on any problem instead of dereferencing garbage.

RVA translation lives here too:

- `airlock_image_rva()` maps an RVA into the assembled virtual image.
- `airlock_image_rva_raw()` maps an RVA back to the original file bytes.
- `airlock_image_read()` is a bounds-checked memcpy against the image.

### Loading — `src/loader/loader.c`

`airlock_image_load_file()` / `airlock_image_load_buffer()` run the pipeline:

1. **Parse headers** into a `airlock_image_t`.
2. **Copy** the raw buffer so the image owns its bytes.
3. **Map sections** — `calloc` a section-aligned image sized by
   `size_of_image`, copy the headers region, then copy each section's raw data
   to its virtual address. Section boundaries are clamped so malformed images
   can't overflow.
4. **Parse imports** — walk the import descriptor array; for each thunk, read
   the hint/name (or ordinal) and resolve it against the Win32 registry,
   recording `module`, `name`, `ordinal`, and the resolved `fn`.
5. **Parse exports** — read the export directory and copy each named export
   into the image's name index.
6. **Parse relocations** — validate the base-relocation blocks (application
   of relocations will come with the execution layer).

## The Win32 API layer (`src/win32`)

Windows executables call functions in system DLLs. Airlock models each system
DLL as a *module* holding an array of `{name, fn}` export entries.

### Registry — `src/win32/api.c`

- `airlock_win32_register_module()` adds a module descriptor (referenced, not
  copied — descriptors must outlive the process).
- Module names are compared case- and extension-insensitively, so
  `kernel32` ≡ `KERNEL32.dll`.
- `airlock_win32_resolve(module, function, ordinal)` returns the Airlock
  function pointer for an import, or `NULL` (logged) when unknown.

The loader calls `airlock_win32_resolve()` for every import thunk, so a Windows
binary binds to Airlock's native implementations exactly as it would bind to
real DLLs.

### Modules — `src/win32/mod_*.c`

Each `mod_*.c` implements one Win32 DLL. Functions are written in C and
exported through the module table. Today they are honest **functional stubs**:
they log the call and return a harmless value. Real implementations map to
Linux syscalls and libc (file I/O, the console, timers, the heap, …).

`src/win32/init.c` registers every module via `airlock_win32_init()`.

## The portability layer — `src/port/posix.c`

`include/airlock/platform.h` is the seam for everything OS-specific: clocks and
sleep, pid/tid, `FILETIME` system time, and zero-copy file mapping. A single
POSIX implementation serves both **Linux and Android** (Android's Bionic libc
exposes the same syscalls). Porting to a new OS means adding one more
implementation file — nothing else changes. This is what makes "Windows EXEs on
all Linux environments, including Android" concrete.

## The audio subsystem — `src/audio/`

Games output audio through `WINMM`/`DirectSound`/`XAudio2`. Airlock funnels
those into a small set of **backends** so the Win32 layer never touches
hardware:

```
winmm.dll (waveOutWrite)  ->  airlock_audio_device_t  ->  backend
                                                      |-> WAV file sink (default)
                                                      |-> ALSA (make ALSA=1)
                                                      `-> null
```

- `src/audio/audio.c` — backend dispatch + the dependency-free **WAV sink**
  (writes real RIFF/WAVE files, used by tests and `airlock --audio`).
- `src/audio/alsa.c` — optional **ALSA** backend for real low-latency output
  on desktop Linux (`make ALSA=1`, `-lasound`).
- `src/win32/mod_winmm.c` — the `WINMM.dll` module games import: `waveOutOpen`,
  `waveOutWrite`, `waveOutGetPosition`, `PlaySoundA`, and `timeGetTime`
  (timing), all routed through the backend.

## The performance kit — `src/perf/perf.c`

Optimization for game-like Windows workloads:

- **Counters** (`images_loaded`, `imports_resolved`, `map_bytes`,
  `mmap_reads`, `audio_bytes`, …) incremented by the loader and Win32 layer.
- **Tunables** (`airlock_perf_options_t`): page pre-faulting (`--papi=1`),
  the mmap threshold for zero-copy file loads, and large pages.
- **Tracing** — a bounded ring buffer (`airlock_perf_trace`) for timeline dumps.
- **Helpers** — a high-performance scheduling hint and page pre-faulting.

Loading already benefits: large files are memory-mapped (zero-copy) via
`airlock_map_file`, and `--papi=1` pre-faults every page of the mapped image so
later execution doesn't stall on page-fault latency.

## The Application Compatibility Analyzer — `src/compat/`

The flagship. `airlock_compat_analyze()` takes a loaded image and:

1. **Classifies each import** into a subsystem (graphics, audio, input,
   networking, filesystem, threading, system) via DLL-prefix and
   function-name heuristics.
2. **Scores each subsystem** against Airlock's API database
   (`airlock_win32_export_exists`), producing a percentage.
3. **Records unresolvable imports** as structured missing-API diagnostics
   (`module`, `function`, `called by`, recommendation).
4. **Detects technologies** (Direct3D 11/12, OpenGL, Vulkan, XInput,
   Winsock, WINMM, WASAPI, …) and **issues** (D3D feature levels, high-res
   timer usage, advanced sync usage).
5. **Recommends a configuration** and saves a per-application profile
   (`src/compat/profile.c`), which also implements Windows-version behavior
   modes (7/8.1/10/11) as behavioral flag sets, and the prefix/profile store.

## Timing, sync, shared memory — `src/timer`, `src/sync`, `src/shmem`

- `timer.c` — calibrated monotonic ns clock (`clock_gettime`), deadline
  `nanosleep` and busy-spin, and a frame-time tracker (FPS, 1% low, waits).
- `sync.c` — futex-based mutex/event/semaphore/spinlock. The uncontended
  fast path is a single atomic; `futex_wait` is only entered on contention.
- `shmem.c` — a lock-free single-producer/single-consumer ring for zero-copy
  transfer between components.

## Tracing, shader cache, plugins, crash — cross-cutting

- `trace.c` — dynamic category tracing; `AIRLOCK_TRACE(cat, ...)` is a guarded
  branch that costs nothing when disabled.
- `gfx/shadercache.c` — persistent shader blob cache keyed by shader hash +
  GPU + driver + API version, with invalidation on identity change.
- `plugin.c` — backend registry with preference-ordered hot-selection
  (Vulkan → OpenGL → Software) and `.so` plugin loading via dlopen.
- `crash.c` — a signal handler that captures module/thread/API-call/DLLs
  into a structured EXCEPTION report.

## The CLI — `src/cli.c`

A thin driver: `airlock_win32_init()`, then either dump the module registry or
load and report one or more executables. It is intentionally small — it is a
demo and debugging surface, not the product. Subcommands: `inspect`, `analyze`,
`db`, `prefix`, `runtime`, `test`, `debug`, `--perf`, `--platform`, `--audio`,
`--papi=1`, and `--trace=...`.

## Compatibility ecosystem (Milestone 4.8)

Five systems sit *around* the loader so Airlock can understand, remember, and
isolate Windows applications before it can execute them:

1. **Inspector** (`src/inspect/inspect.c`) — walks PE data directories (TLS,
   resources / RT_MANIFEST, COM/CLR, delay-load) and classifies imported DLLs
   into graphics/audio/input/net plus .NET / VC-runtime requirements.
2. **Compatibility database** (`src/db/db.c`) — a text catalog (`compat.db`)
   of per-application requirements, a HIGH/MEDIUM/LOW rating derived from the
   analyzer score, and known issues. This is designed to become one of the
   project's biggest assets as real apps are inspected.
3. **Prefix manager** (`src/prefix/prefix.c` + `src/shell/shell.c`) — named
   bottles with a `drive_c` tree. Shell variables (`%APPDATA%`, `%WINDIR%`, …)
   expand into that tree. Backup/restore uses a tiny `ALK1` archive (archives
   written before the rebrand used the magic `CBK1`; restore still accepts
   them, and every new archive is written as `ALK1`).
4. **Test lab** (`src/testlab/testlab.c`) — behavioral tests plus one
   auto-generated export-presence test per registered Win32 function.
5. **COM + shell** (`src/com/com.c`, `src/win32/mod_ole32.c`,
   `src/win32/mod_shell32.c`) — IUnknown/GUIDs/apartments/class factories, and
   `SHGetFolderPathA` / `ShellExecuteA` on top of the shell map.

Supporting layers: runtime manager, desktop integration, virtual display,
security (SID/ACL → uid), installer journal, user-space services,
notifications, accessibility tree, locale/code pages, printing, devices.

### Win32 modules

Each system DLL is an independent `src/win32/mod_*.c` file registered from
`init.c`. Adding a new API is "implement the C function, add a row to that
module's export table." Modules today: `KERNEL32`, `ntdll`, `USER32`,
`ADVAPI32` (in-memory registry), `SHELL32`, `ole32`, `comdlg32`, `gdi32`,
`ws2_32`, `version`, `WINMM`.

## The Airlock workspace layer — `src/workspace/workspace.c`

This is the product surface on top of the compatibility ecosystem. Each app is
one isolated *workspace* (a Airlock prefix) plus a small `app.conf` record and a
`profiles/` directory. It owns:

- **Setup / library** — `airlock_workspace_install()` creates the prefix,
  inspects a source executable/installer, records architecture/graphics/audio/
  runtime requirements, and writes a shortcut.
- **Profiles** — `airlock_profile_point_t` is the YAML DTO. Profiles are stored
  by label and always keep the executable hash; `profile_apply()` pushes a
  profile into the workspace record and `profile_diff()` renders what changed.
- **Snapshots** — a config snapshot copies `app.conf`, `prefix.conf`, and the
  active profile into `snapshots/<label>/`; rollback restores those three.
- **Doctor / support / diagnose** — `airlock_workspace_doctor()` produces a
  structured pre-flight report; `support()` writes a sanitized bundle;
  `diagnose()` classifies raw Wine/Box64 log lines into readable symptoms.
- **Safety** — per-workspace permission bits and a plain-language dashboard
  (`airlock_workspace_permissions_text()`), plus a malware-warning section in
  the safety report.

## Conventions

- **Public API lives in `include/airlock/`**; internal helpers are `static`.
- **All failure paths return `airlock_status_t`** — never abort, never leak.
- **All RVA access is bounds-checked.** Malformed input must produce a typed
  error, not a crash.
- **Logging is compile-time configurable** via `AIRLOCK_LOG_LEVEL` (0–4).

## Testing strategy

`tests/test_loader.c` builds a complete, valid PE32 *in memory*, then verifies
header parsing, section mapping, RVA translation, import binding, and export
indexing — plus negative cases (not a PE, truncated buffer, NULL args). This
gives deterministic, fixture-free coverage.

`test_malformed_headers()` pins the bounds on every file-supplied offset:
a section whose raw range runs past the end of the file, a `SizeOfHeaders`
larger than the mapped image, an absurd `SizeOfImage`, and a `SizeOfHeaders`
larger than the file on the raw-RVA path. Each case was confirmed to crash or
fail against the pre-fix loader.

Both the suites and `tools/fuzz_loader.c` run clean under AddressSanitizer +
UndefinedBehaviorSanitizer. CI runs 40 fuzz seeds of 20,000 mutations each —
800,000 malformed inputs — on every push.

`tools/gen_sample_pe.c` writes the same synthetic PE to disk so the CLI can be
demonstrated and inspected without MinGW.

`tests/test_workspace.c` exercises the product layer in an isolated temp
directory: guided setup, app-library records, profile save/load/diff,
snapshot/rollback, launch doctor, support bundle, log diagnosis, permissions,
perf/controls/resolution settings, shader-cache management, and the device
capability report.
