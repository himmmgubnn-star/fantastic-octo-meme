# Cellar Roadmap

Cellar is built bottom-up: first read Windows executables, then give them an
OS to run on, then let them execute. Each milestone is independently useful.

Legend: ✅ done · 🔶 in progress · ⬜ planned

## Milestone 0 — PE loader (current)

> **Goal:** understand Windows executables. Read them, validate them, map
> them, and say what they import and export.

- ✅ PE/COFF header parsing (32- and 64-bit optional headers)
- ✅ Section table parsing
- ✅ Section-aligned virtual-image mapping
- ✅ RVA / bounds-checked image reads
- ✅ Import directory parsing + resolution against the Win32 registry
- ✅ Export directory parsing + name index
- ✅ Base-relocation validation (parse only)
- ✅ CLI report + synthetic-PE generator + unit tests

**Definition of done:** the loader can parse real `.exe` files from MinGW and
the Microsoft toolchain and report them correctly.

## Milestone 1 — Execution & ABI emulation

> **Goal:** run code. Invoke the entry point, translate Win32 calls into C
> function calls, and give programs a working process model.

- ⬜ Base-relocation **application** (needed when preferred image base is busy)
- ⬜ Call the entry point and marshal the Win32 calling convention
- ⬜ Threads and synchronization (`CreateThread`, critical sections, events)
- ⬜ Full **kernel32** implementation mapped onto POSIX:
  - file I/O → Linux file descriptors
  - the console → stdio
  - process/env/args → `exec`-style setup
  - the heap → `mmap` / `brk`
  - timers → `nanosleep` / `timerfd`
- ⬜ Error model (`GetLastError`/`SetLastError` per thread)

**Definition of done:** a trivial console executable (e.g. one that calls
`ExitProcess` and `WriteFile`) runs and returns the correct exit code.

## Milestone 2 — PE/PE+ architecture depth

> **Goal:** cover the real-world variety of Windows binaries.

- ⬜ PE32+ (64-bit) import/export/reloc handling end-to-end
- ⬜ Delay-loaded imports
- ⬜ TLS (thread-local storage) directory
- ⬜ Forwarder exports (`KERNEL32` → `ntdll` chains)
- ⬜ Ordinal imports
- ⬜ Exceptions / SEH (Windows structured exception handling)

**Definition of done:** a representative set of console programs compiled by
several Windows toolchains run unmodified.

## Milestone 3 — The GUI stack

> **Goal:** run graphical Windows programs on the Linux desktop.

- ⬜ `USER32` → X11 / Wayland (window management, message loop)
- ⬜ `GDI32` → 2D drawing
- ⬜ `COMCTL32`, `COMDLG32` (common controls & dialogs)
- ⬜ Fonts, cursor, clipboard
- ⬜ DirectX → Vulkan/OpenGL (a very long tail)

**Definition of done:** a simple Win32 GUI app opens a window and renders.

## Milestone 4.5 — Portability, performance & audio (current)

> **Goal:** run anywhere, run fast, and make sound. Two cross-cutting subsystems
> that every Windows program needs.

- ✅ **Portability layer** (`include/cellar/platform.h`, `src/port/posix.c`)
  - Works on **Linux and Android** (both ship the POSIX surface)
  - Monotonic clock, `nanosleep`, perf counter, `FILETIME` system time
  - pid/tid, zero-copy `mmap` file reads
- ✅ **Audio subsystem** (`include/cellar/audio.h`, `src/audio/`)
  - Backend abstraction → WAV file sink (dependency-free, default) | ALSA | null
  - `WINMM.dll` (winmm) module: `waveOut*`, `PlaySoundA`, `timeGetTime`
  - Games' audio routes through the backend, never touching hardware directly
- ✅ **Performance kit** (`include/cellar/perf.h`, `src/perf/`)
  - Counters, tunables (`--papi`, mmap threshold, large pages), tracing ring
  - mmap-backed zero-copy file loading; optional page pre-faulting (`--papi=1`)
  - high-performance scheduling hint
- ✅ **CMake build** with an Android NDK toolchain recipe + CLI subcommands
  (`--perf`, `--platform`, `--audio`, `--papi=1`) + unit tests for all three

## Milestone 4.7 — Advanced compatibility (current)

> **Goal:** diagnose, remember, and optimize real Windows software.

- ✅ **Application Compatibility Analyzer** — `cellar analyze game.exe`:
  per-subsystem scoring vs the API database, missing-API diagnostics, issue
  detection, config recommendations
- ✅ **Per-application compatibility profiles** — persisted per-app config
  (version mode, backends, DLL overrides, sync, shader cache) under prefixes/
- ✅ **Windows-version behavior modes** — Win 7 / 8.1 / 10 / 11 behavioral
  flag sets (DPI, UTF-8, touch, threadpool, ARM translation)
- ✅ **High-resolution timer** — calibrated ns clock, deadline sleep/spin,
  frame-time diagnostics (FPS, 1% low, CPU/GPU/translation waits)
- ✅ **Lightweight synchronization** — futex mutex/event/semaphore/spinlock
- ✅ **Shared-memory ring** — lock-free zero-copy SPSC buffer
- ✅ **Shader pipeline cache** — persistent, identity-keyed, invalidating
- ✅ **Dynamic runtime tracing** — `--trace=...`, zero overhead when off
- ✅ **Crash-recovery diagnostics** — structured EXCEPTION capture
- ✅ **Plugin architecture + backend hot-selection** — Vulkan→OpenGL→Software
- ✅ **Regression + fuzz testing** — 5 unit suites + deterministic fuzzer
- 🔶 **Dependency management** (installing legitimate runtime components)

## Milestone 5 — Robustness & ecosystem

- ⬜ Loader hardening (more fuzzing, corpus testing)
- ⬜ A `winecfg`-style configuration surface
- ⬜ Install/uninstall, app registry (`AppCompat`)
- ⬜ Packaging for major distros

---

## Guiding principles

1. **Clean-room.** All behavior derives from public specs and observation —
   no copying of Wine or ReactOS.
2. **Incremental value.** Every milestone ships something runnable; you never
   wait for "the whole thing."
3. **Correctness over breadth.** Nail a small set of APIs to a high standard
   before adding more.
4. **Safety.** Malformed binaries must produce errors, never crashes — tested
   continuously under ASan/UBSan.
