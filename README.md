# Airlock

**Airlock** is a clean-room, Wine-style **Windows compatibility layer for Linux and Android**, written in **C11**.

Its goal is to let Windows programs run on Linux by providing the pieces Windows applications expect from the operating system:

- a **PE loader** that parses and maps Windows executables (`.exe`) and libraries (`.dll`),
- a **Win32 API layer** that implements the system DLLs Windows binaries import (`KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ntdll.dll`, …),
- and, down the road, a **CPU/ABI emulation layer** that lets PE code actually execute.

This is the same architecture Wine uses. Airlock is a from-scratch implementation that builds up that capability incrementally — current status is in [docs/ROADMAP.md](docs/ROADMAP.md).

> **Project scope.** Airlock is clean-room software: all format knowledge comes from publicly documented specifications (the Microsoft PE/COFF format) and from observing real binaries. No Wine or ReactOS source is copied.

---

## Why C?

A Windows compatibility layer is the rare project where **C is genuinely the right tool**:

- **Raw ABI, byte-for-byte.** The PE format, Win32 calling conventions (`__stdcall`/`__fastcall`), structure layouts, and the x86 exception model are defined as exact bytes and register/stack rules. C describes these directly; a type-safe language adds friction rather than value.
- **Near-zero footprint.** A compatibility layer must be loadable by anything with no meaningful runtime cost. C compiles to a small static library and a thin CLI.
- **Direct OS interposition.** Airlock needs to `dlopen`, trap syscalls, map memory, and handle signals — C has first-class access to all of it.
- **It's what Wine does.** Wine is C (plus a little assembly), and it's the most successful Windows-on-Unix project ever built.

By contrast, an interpreter or JIT language injects a runtime dependency, and Rust's memory-safety guarantees actively fight this domain, which is full of untyped `void*` boundary tables and dynamic, ABI-level dispatch.

---

## Building

Requires a C11 compiler (GCC or Clang), GNU Make (or CMake), and `ar`.

```sh
make            # build the `airlock` CLI + libairlock.a
make test       # build and run the unit tests (loader/audio/perf/compat/kit/ecosystem)
make sample     # generate a synthetic test PE -> samples/hello.exe
make fuzz       # run a deterministic loader fuzz campaign
make ALSA=1     # also build the real ALSA audio backend (needs libasound2-dev)
make clean      # remove build artifacts
```

Optional knobs: `CC`, `CFLAGS`, and `AIRLOCK_LOG_LEVEL` (0–4, default 2 = info).

No third-party libraries are required — everything is C11 + POSIX. The only
optional dependency is ALSA for real audio output on desktop Linux.

**Android (NDK):** Airlock's platform layer targets Linux and Android with one
POSIX implementation. Cross-compile with the NDK toolchain through CMake:

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -B build-android
cmake --build build-android      # produces libairlock.a (+ airlock-cli)
```

A packaged Android app would link `libairlock.a` and call `airlock_win32_init()`
itself. Audio on Android defaults to the WAV/null sink; an AAudio/Oboe backend
can be dropped in later.

## Quick start

```sh
$ make test
test_loader: all tests passed

$ make sample
wrote samples/hello.exe (4608 bytes)

$ ./build/airlock samples/hello.exe
== samples/hello.exe ==
  format         PE/32-bit  (x86)
  kind           executable
  subsystem      windows-console
  entry point    RVA 0x00001000
  image base     0x00400000
  ...
  imports:
    KERNEL32.dll     ExitProcess
    KERNEL32.dll     GetStdHandle
```

`make sample` synthesizes a real, loadable PE without MinGW — handy for exercising the loader on a machine with no Windows toolchain. You can also point `airlock` at any genuine `.exe`.

```sh
airlock inspect program.exe       # Application Inspector (arch, DLLs, TLS, runtimes)
airlock analyze program.exe       # Application Compatibility Analysis
airlock db list | db show APP     # compatibility database
airlock prefix create Game        # isolated Windows environment (bottle)
airlock prefix create Game --arch win32   # force a 32-bit container
airlock prefix clone|import|export Game   # copy / archive a container
airlock prefix set Game box64 performance # container-level settings
airlock prefix settings Game
airlock prefix launch Game game.exe
airlock prefix backup|restore|delete Game
airlock runtime list|install vcruntime|dotnet|directx|fonts|systemlibs
airlock app add Game setup.exe --kind exe --launch game.exe
airlock app list|show|remove|run|doctor|repair Game
airlock app snapshot|rollback|diff|support|safety|shader Game ...
airlock profile list|show|apply|export|import Game [label|path]
airlock container create Game setup.exe --launch game.exe
airlock device report             # device / capability report
airlock test                      # compatibility test lab
airlock debug program.exe         # debugger snapshot
airlock program.exe               # load + report a Windows executable
airlock --list-modules            # show registered Win32 modules & exports
airlock --platform                # OS / perf-hint info
airlock --perf program.exe        # load, then dump perf counters + tracing
airlock --papi=1 program.exe      # pre-fault pages (faster steady-state)
airlock --trace=dll,api program   # dynamic tracing (graphics,filesystem,...)
airlock --audio out.wav           # render a tone through the audio backend
airlock --help
```

## Application Compatibility Analyzer (flagship)

`airlock analyze game.exe` inspects an executable before launch and produces a
boxed report: architecture, detected graphics/audio/input/network tech,
per-category compatibility scores against Airlock's API database, concrete
missing-API diagnostics, potential issues, and a recommended configuration.
It also remembers a per-application profile so working configs are reused.

```sh
$ airlock analyze samples/hello.exe
============================================
   COMPATIBILITY ANALYSIS — hello.exe
============================================
Architecture:        x86
PE format:           Valid
Graphics:            Direct3D 11
Audio:               WINMM
Input:               XInput
Networking:          Winsock

Windows APIs
Graphics         [                    ]   0%  (0/1)
Audio            [####################] 100%  (1/1)
...
Recommended configuration
-------------------------
Graphics: Vulkan
Shader cache: ENABLED
```

The analyzer drives several advanced features described below.

## Five major systems

These are the pieces that push Airlock past "parse a PE" toward a project that
can actually host Windows applications:

1. **Application Inspector** (`airlock inspect game.exe`) — PE architecture,
   imported DLLs and APIs, TLS, resources, manifests, COM/CLR, delay-loads,
   .NET and VC-runtime requirements, detected graphics/audio/input/net.
2. **Compatibility database** (`airlock db`) — remembers what each application
   needs and how well Airlock covers it (HIGH / MEDIUM / LOW + known issues).
   Inspector and analyzer results are stored automatically.
3. **Prefix / environment manager** (`airlock prefix`) — isolated bottles with
   their own `drive_c`, registry journal, Windows-version behavior, and
   graphics/audio settings. `%APPDATA%` / `%WINDIR%` / `%TEMP%` / … map into
   the bottle.
4. **Compatibility test lab** (`airlock test`) — Windows-behavior tests
   (kernel32, user32, ntdll, advapi32, COM, locale, security, …) plus one
   auto-generated test per registered Win32 export. Output looks like:

   ```
   Tests:       76
   Passed:      76
   Failed:         0
   Skipped:        0
   Compatibility: 100.0%
   ```
5. **COM/OLE + Windows shell** — IUnknown, GUIDs, refcounting, class
   registration, STA/MTA apartments, same-process marshaling; `SHGetFolderPathA`
   / `ShellExecuteA`; desktop shortcuts, MIME associations, clipboard, and
   notifications.

Around those: a **runtime manager** (VC++ / .NET / DirectX / fonts / system
libs), a **virtual display** (multi-monitor, DPI, HDR, orientation), a
**security model** (SIDs, tokens, ACLs → Linux uids), an **installer journal**,
**user-space services**, **locale/code pages**, **printing**, **devices**, and
an **accessibility tree**.

## Audio

Airlock plays Windows audio through a pluggable backend:

- **WAV file sink** (default, dependency-free) — every `waveOutWrite` a game
  makes is captured to a real RIFF/WAVE file (`AIRLOCK_WAV_OUT` overrides the
  default `airlock-out.wav`).
- **ALSA** (`make ALSA=1`) — real, low-latency output on desktop Linux.
- **Null sink** — for platforms without an audio device (e.g. early Android).

The `WINMM.dll` module (`waveOut*`, `PlaySoundA`, `timeGetTime`) routes through
this backend, so games use familiar Win32 multimedia APIs regardless of the
underlying OS audio system.

## Advanced compatibility features

Airlock bundles a set of advanced, realistic features for running real Windows
software — including games:

- **Per-application compatibility profiles** — remember a working configuration
  (Windows-version mode, graphics/audio backend, DLL overrides, sync mode,
  shader cache) per app under a prefix directory (`~/.airlock/prefixes/`).
- **Windows-version behavior profiles** — Win 7 / 8.1 / 10 / 11 modes that
  reproduce behavioral differences (DPI awareness, UTF-8 code page, touch
  input, threadpool model, ARM translation), not just the reported version.
- **Missing-API diagnostics** — the analyzer reports each unresolvable import
  as `module`, `missing API`, `called by`, and a recommendation.
- **High-resolution timer subsystem** — calibrated monotonic ns clock,
  deadline sleep and spin primitives, plus a **frame-time tracker** (FPS,
  1% low, average, max, and CPU/GPU/translation waits).
- **Lightweight synchronization** — futex-based mutex/event/semaphore/spinlock;
  the uncontended path is a single atomic operation with no syscall.
- **Shared-memory ring buffer** — lock-free single-producer/single-consumer
  zero-copy transfer between compatibility components.
- **Shader pipeline cache** — persistent, keyed by shader hash + GPU + driver +
  API version, with invalidation rules on identity change.
- **Dynamic runtime tracing** — `--trace=graphics,api,dll,...` (also
  `$AIRLOCK_TRACE`); zero overhead when disabled.
- **Crash-recovery diagnostics** — a signal handler that captures module,
  thread, the Windows API call in flight, syscall context, and loaded DLLs
  into a structured EXCEPTION report.
- **Plugin architecture & backend hot-selection** — backends registered by
  preference (Vulkan → OpenGL → Software; ALSA → PipeWire → WAV); plugins
  loadable as `.so` files.
- **Regression + fuzz testing** — `make test` verifies API behavior against
  expected results; `make fuzz` runs a deterministic PE-loader fuzz campaign.

## Performance & gaming optimization

Airlock ships a performance kit aimed at game-like Windows workloads:

- **Zero-copy loading** — large executables are memory-mapped rather than
  fully copied.
- **Page pre-faulting** (`--papi=1`) — touches every mapped page at load time
  so execution doesn't stall on page-fault latency.
- **Counters & tracing** (`--perf`) — images loaded, imports resolved, bytes
  mapped, audio bytes, plus a timestamped trace ring for profiling.
- **Scheduling hint** (`--platform`) — asks the OS for high-performance
  scheduling where permitted.
- **Tunables** — mmap threshold, large pages, papi via `airlock_perf_options_t`.

## What works today

- **PE parsing** — DOS/COFF/optional headers (32- and 64-bit), section tables, and the data directories (imports, exports, base relocations).
- **Section mapping** — assembles a section-aligned virtual image so RVA lookups and the entry point behave as on Windows.
- **Import resolution** — walks each DLL's import table and binds thunks to Airlock's native Win32 functions via an O(1) export registry.
- **Export indexing** — parses a module's export directory into a name index.
- **Win32 registry** — independent modules for `KERNEL32.dll`, `ntdll.dll`,
  `USER32.dll`, `ADVAPI32.dll`, `SHELL32.dll`, `ole32.dll`, `comdlg32.dll`,
  `gdi32.dll`, `ws2_32.dll`, `version.dll`, and `WINMM.dll`.
- **Portability layer** — one POSIX implementation serves Linux and Android (clocks, sleep, mmap reads, pid/tid).
- **Audio subsystem** — backend dispatch with a testable WAV sink and optional ALSA.
- **Performance kit** — counters, tunables, tracing, pre-faulting, zero-copy mmap loads.
- **Application Compatibility Analyzer** — per-subsystem scoring, missing-API diagnostics, issue detection, and config recommendations.
- **Compatibility profiles** — per-app remembered configs and Windows-version behavior modes (7/8.1/10/11).
- **High-res timing** — calibrated ns clock, deadline sleep/spin, and frame-time diagnostics (FPS, 1% low, waits).
- **Lightweight sync** — futex-based mutex/event/semaphore/spinlock (no syscall when uncontended).
- **Shared-memory ring** — lock-free zero-copy SPSC buffer.
- **Shader cache** — persistent, identity-keyed with invalidation.
- **Dynamic tracing** — `--trace=...` categories, zero overhead when off.
- **Crash diagnostics** — structured EXCEPTION capture on fatal signals.
- **Plugins & backend selection** — Vulkan→OpenGL→Software / ALSA→PipeWire→WAV hot-selection, `.so` plugin loading.
- **Testing** — 7 unit suites (loader, audio, perf, compat, kit, ecosystem, workspace) plus a deterministic loader fuzz harness (`make fuzz`) and `airlock test` (compatibility lab), all clean under ASan/UBSan.
- **Inspector + compatibility DB** — `airlock inspect` / `airlock db`.
- **Prefix manager** — create (win32/win64) / clone / export / import /
  backup / restore / delete isolated bottles, plus generic container-level
  settings (runner, resolution, Box64 preset, CPU/frame limits, ESync/FSync).
- **COM / OLE** — IUnknown, GUIDs, apartments, class registration.
- **Shell / desktop / display / security / locale / print / devices / a11y.**
- **Airlock workspace layer** — one isolated environment per app, guided
  setup (`exe`/`msi`/`import`/`portable`), an app library, versioned YAML
  profiles tied to executable hashes, one-click snapshot/rollback, config
  diff, repair mode, launch doctor, readable log diagnostics, sanitized
  support bundles, permission dashboards, performance modes, controls,
  shader-cache management, device capability report, and community profile
  import/export. See [docs/AIRLOCK.md](docs/AIRLOCK.md).

## Layout

```
include/airlock/          public headers
  airlock.h               status codes, logging, common utilities
  pe.h                   clean-room PE/COFF format definitions
  loader.h               loader API + in-memory image model
  win32.h                Win32 export registry API
  platform.h             OS portability seam (Linux + Android)
  audio.h                audio backend API
  perf.h                 performance counters/tunables/tracing
  compat.h               compatibility analysis + profiles + version modes
  timer.h                high-res timer + frame diagnostics
  sync.h                 lightweight futex synchronization primitives
  shmem.h                zero-copy shared-memory ring
  trace.h                dynamic category-based tracing
  shadercache.h          persistent shader pipeline cache
  plugin.h               plugin architecture + backend hot-selection
  crash.h                crash-recovery diagnostics
  inspect.h              application inspector
  db.h                   compatibility database
  runtime.h              runtime (VC++/.NET/DirectX/fonts) manager
  shell.h                %APPDATA% / %WINDIR% / … mapping
  prefix.h               bottle create/backup/restore/delete
  display.h              virtual multi-monitor display
  security.h             SIDs, tokens, ACLs
  desktop.h              .desktop, MIME, clipboard, notifications
  installer.h            installer side-effect journal
  com.h                  COM/OLE (IUnknown, GUIDs, apartments)
  service.h              user-space Windows service manager
  notify.h               notification history
  a11y.h                 accessibility tree
  locale.h               code pages + date/number format
  print.h                virtual printers
  device.h               device registry
  debug.h                compatibility debugger snapshot
  testlab.h              compatibility test lab
  workspace.h            Airlock app workspaces, profiles, doctor, support
src/
  loader/
    airlock_util.c        status strings, logging, endian reads, hashing
    pe.c                 header + section parsing, RVA translation
    loader.c             full image-load pipeline, imports, exports
  win32/
    api.c                export registry (module + function lookup)
    mod_kernel32.c       KERNEL32.dll implementation
    mod_winmm.c          WINMM.dll (multimedia/audio) implementation
    init.c               module registration bootstrap
  port/
    posix.c              POSIX platform layer (Linux + Android/Bionic)
  audio/
    audio.c              backend dispatch + WAV file sink
    alsa.c               optional ALSA backend (make ALSA=1)
  perf/
    perf.c               performance kit
  compat/
    compat.c             the Application Compatibility Analyzer
    profile.c            per-app profiles + Windows-version behavior modes
  timer/
    timer.c              high-res clock, sleep/spin, frame-time stats
  sync/
    sync.c               futex mutex/event/semaphore/spinlock
  shmem/
    shmem.c              zero-copy SPSC ring buffer
  trace/
    trace.c              dynamic tracing
  gfx/
    shadercache.c        persistent shader cache
  plugin/
    plugin.c             backend registry + plugin loading
  crash/
    crash.c              crash diagnostics + signal handler
  inspect/inspect.c      application inspector
  db/db.c                compatibility database
  runtime/runtime.c      runtime manager
  shell/shell.c          Windows env-var mapping
  prefix/prefix.c        bottle manager
  display/display.c      virtual display
  security/security.c    security model
  desktop/desktop.c      Linux desktop integration
  installer/installer.c  installer journal
  com/com.c              COM/OLE
  service/service.c      user-space services
  notify/notify.c        notifications
  a11y/a11y.c            accessibility tree
  locale/locale.c        locale + code pages
  print/print.c          printing
  device/device.c        devices
  debug/debug.c          debugger snapshot
  testlab/testlab.c      compatibility test lab
  workspace/workspace.c  Airlock workspace/app/manager
  cli.c                  command-line driver
tests/
  test_loader.c          loader unit tests (synthetic-PE driven)
  test_audio.c           audio backend / WAV sink tests
  test_perf.c            perf counters / ring / tunables tests
  test_compat.c          analyzer + version profiles + app profiles + crash
  test_kit.c             timer, sync, shmem, trace, shadercache tests
  test_ecosystem.c       inspector, db, prefix, COM, test lab
  test_workspace.c       Airlock workspaces/profiles/doctor/support
tools/
  gen_sample_pe.c        writes a minimal valid PE for testing
  fuzz_loader.c          deterministic PE-loader fuzz harness
docs/
  ARCHITECTURE.md        how the pieces fit together
  ROADMAP.md             the path to actually running Windows programs
```

## Design notes

- **Format knowledge is centralized** in `include/airlock/pe.h` as *packed* structs that mirror the on-disk PE layout, so parsers map a buffer directly onto them.
- **Everything is host-endian-safe.** On-disk scalars are read little-endian; the loader is correct on big-endian hosts too.
- **The registry is O(1).** Modules are indexed by a stable hash of the DLL name, exports by a hash of the function name — import binding is fast.
- **Safety first.** All RVA reads are bounds-checked; malformed images yield a typed `airlock_status_t` error rather than a crash. Tests include fuzz-style negative cases.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full design, and [docs/ROADMAP.md](docs/ROADMAP.md) for where this is going (execution, relocation, threads, the GUI stack, …).

## License

MIT
