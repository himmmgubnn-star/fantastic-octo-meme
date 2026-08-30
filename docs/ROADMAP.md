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

## Milestone 4 — Robustness & ecosystem

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
