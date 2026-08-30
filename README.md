# Cellar

**Cellar** is a clean-room, Wine-style **Windows compatibility layer for Linux and Android**, written in **C11**.

Its goal is to let Windows programs run on Linux by providing the pieces Windows applications expect from the operating system:

- a **PE loader** that parses and maps Windows executables (`.exe`) and libraries (`.dll`),
- a **Win32 API layer** that implements the system DLLs Windows binaries import (`KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ntdll.dll`, …),
- and, down the road, a **CPU/ABI emulation layer** that lets PE code actually execute.

This is the same architecture Wine uses. Cellar is a from-scratch implementation that builds up that capability incrementally — current status is in [docs/ROADMAP.md](docs/ROADMAP.md).

> **Project scope.** Cellar is clean-room software: all format knowledge comes from publicly documented specifications (the Microsoft PE/COFF format) and from observing real binaries. No Wine or ReactOS source is copied.

---

## Why C?

A Windows compatibility layer is the rare project where **C is genuinely the right tool**:

- **Raw ABI, byte-for-byte.** The PE format, Win32 calling conventions (`__stdcall`/`__fastcall`), structure layouts, and the x86 exception model are defined as exact bytes and register/stack rules. C describes these directly; a type-safe language adds friction rather than value.
- **Near-zero footprint.** A compatibility layer must be loadable by anything with no meaningful runtime cost. C compiles to a small static library and a thin CLI.
- **Direct OS interposition.** Cellar needs to `dlopen`, trap syscalls, map memory, and handle signals — C has first-class access to all of it.
- **It's what Wine does.** Wine is C (plus a little assembly), and it's the most successful Windows-on-Unix project ever built.

By contrast, an interpreter or JIT language injects a runtime dependency, and Rust's memory-safety guarantees actively fight this domain, which is full of untyped `void*` boundary tables and dynamic, ABI-level dispatch.

---

## Building

Requires a C11 compiler (GCC or Clang), GNU Make (or CMake), and `ar`.

```sh
make            # build the `cellar` CLI + libcellar.a
make test       # build and run the unit tests (loader + audio + perf)
make sample     # generate a synthetic test PE -> samples/hello.exe
make ALSA=1     # also build the real ALSA audio backend (needs libasound2-dev)
make clean      # remove build artifacts
```

Optional knobs: `CC`, `CFLAGS`, and `CELLAR_LOG_LEVEL` (0–4, default 2 = info).

No third-party libraries are required — everything is C11 + POSIX. The only
optional dependency is ALSA for real audio output on desktop Linux.

**Android (NDK):** Cellar's platform layer targets Linux and Android with one
POSIX implementation. Cross-compile with the NDK toolchain through CMake:

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -B build-android
cmake --build build-android      # produces libcellar.a (+ cellar-cli)
```

A packaged Android app would link `libcellar.a` and call `cellar_win32_init()`
itself. Audio on Android defaults to the WAV/null sink; an AAudio/Oboe backend
can be dropped in later.

## Quick start

```sh
$ make test
test_loader: all tests passed

$ make sample
wrote samples/hello.exe (1792 bytes)

$ ./build/cellar samples/hello.exe
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

`make sample` synthesizes a real, loadable PE without MinGW — handy for exercising the loader on a machine with no Windows toolchain. You can also point `cellar` at any genuine `.exe`.

```sh
cellar program.exe               # load + report a Windows executable
cellar --list-modules            # show registered Win32 modules & exports
cellar --platform                # OS / perf-hint info
cellar --perf program.exe        # load, then dump perf counters + tracing
cellar --papi=1 program.exe      # pre-fault pages (faster steady-state)
cellar --audio out.wav           # render a tone through the audio backend
cellar --help
```

## Audio

Cellar plays Windows audio through a pluggable backend:

- **WAV file sink** (default, dependency-free) — every `waveOutWrite` a game
  makes is captured to a real RIFF/WAVE file (`CELLAR_WAV_OUT` overrides the
  default `cellar-out.wav`).
- **ALSA** (`make ALSA=1`) — real, low-latency output on desktop Linux.
- **Null sink** — for platforms without an audio device (e.g. early Android).

The `WINMM.dll` module (`waveOut*`, `PlaySoundA`, `timeGetTime`) routes through
this backend, so games use familiar Win32 multimedia APIs regardless of the
underlying OS audio system.

## Performance & gaming optimization

Cellar ships a performance kit aimed at game-like Windows workloads:

- **Zero-copy loading** — large executables are memory-mapped rather than
  fully copied.
- **Page pre-faulting** (`--papi=1`) — touches every mapped page at load time
  so execution doesn't stall on page-fault latency.
- **Counters & tracing** (`--perf`) — images loaded, imports resolved, bytes
  mapped, audio bytes, plus a timestamped trace ring for profiling.
- **Scheduling hint** (`--platform`) — asks the OS for high-performance
  scheduling where permitted.
- **Tunables** — mmap threshold, large pages, papi via `cellar_perf_options_t`.

## What works today

- **PE parsing** — DOS/COFF/optional headers (32- and 64-bit), section tables, and the data directories (imports, exports, base relocations).
- **Section mapping** — assembles a section-aligned virtual image so RVA lookups and the entry point behave as on Windows.
- **Import resolution** — walks each DLL's import table and binds thunks to Cellar's native Win32 functions via an O(1) export registry.
- **Export indexing** — parses a module's export directory into a name index.
- **Win32 registry** — `KERNEL32.dll` (`ExitProcess`, `GetStdHandle`, `WriteFile`, `LoadLibraryA`, `GetProcAddress`, …) and `WINMM.dll` (multimedia/audio) modules.
- **Portability layer** — one POSIX implementation serves Linux and Android (clocks, sleep, mmap reads, pid/tid).
- **Audio subsystem** — backend dispatch with a testable WAV sink and optional ALSA.
- **Performance kit** — counters, tunables, tracing, pre-faulting, zero-copy mmap loads.
- **Unit tests** — loader + audio + perf suites driven by a synthetic PE and a real WAV output; all run clean under AddressSanitizer/UBsan.

## Layout

```
include/cellar/          public headers
  cellar.h               status codes, logging, common utilities
  pe.h                   clean-room PE/COFF format definitions
  loader.h               loader API + in-memory image model
  win32.h                Win32 export registry API
  platform.h             OS portability seam (Linux + Android)
  audio.h                audio backend API
  perf.h                 performance counters/tunables/tracing
src/
  loader/
    cellar_util.c        status strings, logging, endian reads, hashing
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
  cli.c                  command-line driver
tests/
  test_loader.c          loader unit tests (synthetic-PE driven)
  test_audio.c           audio backend / WAV sink tests
  test_perf.c            perf counters / ring / tunables tests
tools/
  gen_sample_pe.c        writes a minimal valid PE for testing
docs/
  ARCHITECTURE.md        how the pieces fit together
  ROADMAP.md             the path to actually running Windows programs
```

## Design notes

- **Format knowledge is centralized** in `include/cellar/pe.h` as *packed* structs that mirror the on-disk PE layout, so parsers map a buffer directly onto them.
- **Everything is host-endian-safe.** On-disk scalars are read little-endian; the loader is correct on big-endian hosts too.
- **The registry is O(1).** Modules are indexed by a stable hash of the DLL name, exports by a hash of the function name — import binding is fast.
- **Safety first.** All RVA reads are bounds-checked; malformed images yield a typed `cellar_status_t` error rather than a crash. Tests include fuzz-style negative cases.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full design, and [docs/ROADMAP.md](docs/ROADMAP.md) for where this is going (execution, relocation, threads, the GUI stack, …).

## License

MIT
