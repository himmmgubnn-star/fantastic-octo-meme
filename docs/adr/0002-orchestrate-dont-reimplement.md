# 0002. Orchestrate mature upstream runtimes instead of reimplementing Windows

- Status: accepted
- Date: 2026-08-30

## Context

Airlock Core began as a clean-room reimplementation of the Win32 API: a PE
loader plus eleven hand-written system DLL modules (`mod_kernel32.c`,
`mod_user32.c`, `mod_gdi32.c`, `mod_ntdll.c`, …). The roadmap treated
"execution and ABI emulation" and "the GUI stack" as the next milestones — in
other words, writing a Wine competitor.

That direction cannot succeed on the terms this project needs:

- Wine represents decades of work by hundreds of contributors, plus a long tail
  of application-specific fixes. Reimplementing it is not a plausible path to a
  product that runs real Windows software.
- The product brief explicitly forbids it: prefer integrating mature upstream
  components, do not build a Windows kernel clone, do not write a CPU emulator
  or a Windows clone from scratch.
- A reimplementation cannot honestly claim compatibility. Every "works" claim
  would have to be re-earned application by application.
- The existing code cannot execute anything. The PE loader maps an image and
  resolves imports against a registry of stubs; `prefix launch` prints a report
  and exits. There is no entry-point invocation, no thread model, no
  `GetLastError`, no windowing.

What the existing code *is* good at is understanding a Windows binary without
running it, and managing isolated per-application environments on disk. Both are
genuinely useful to an orchestrator and both are already tested.

## Decision

1. **Airlock orchestrates; it does not reimplement.** Compatibility Mode drives
   a real Wine runner (with Box86/Box64 for x86/x64 translation on ARM64
   hosts). VM Mode drives a real full-system virtualizer (QEMU). Airlock
   contributes configuration, isolation, lifecycle, diagnostics, and UI — never
   a Windows API implementation.
2. **Retain Airlock Core as an offline analysis toolkit.** The PE
   parser/inspector, the compatibility analyzer, the compatibility database, the
   prefix/container manager, the shader cache, tracing, and crash capture stay.
   They run host-side, they are covered by six test suites, and they answer
   "what will this executable need?" before a single byte is executed.
3. **Stop describing the stubs as a compatibility layer.** The `mod_*.c` modules
   are documentation of the API surface the analyzer scores against. They are
   not a runtime, and no user-facing text may imply that they are.
4. **Every runtime goes behind an adapter interface.** `WineRuntimeAdapter`,
   `BoxRuntimeAdapter`, `QemuRuntimeAdapter`, `GraphicsBackendAdapter`,
   `HostStorageAdapter`, `HostInputAdapter`, `NetworkAdapter`. Runtimes are
   detected at runtime and reported as present or absent; a backend that is not
   installed is never offered as usable.
5. **No process is ever started through a shell.** Adapters build an argument
   vector and call an `execve`-family function. Configuration data is never
   concatenated into a command string. See `SECURITY.md`.
6. **Nothing is downloaded silently.** A runner or dependency download requires
   explicit consent and must first display version, source, checksum, license,
   and storage cost.

## Consequences

- The old "Milestone 1 — Execution & ABI emulation" and "Milestone 3 — The GUI
  stack" items are withdrawn from the roadmap and replaced by the phase plan in
  `docs/ROADMAP.md`.
- Roughly 3,000 lines of `src/win32/` become analysis metadata rather than a
  runtime. They stay because the analyzer, the test lab, and the compatibility
  database are built on them and because deleting tested code buys nothing.
- Airlock's value proposition becomes integration quality — detection,
  configuration, isolation, diagnostics — which is testable without shipping a
  Windows implementation.
- Airlock inherits its compatibility ceiling from Wine and QEMU. That must be
  stated plainly to users instead of promising that every application, driver,
  anti-cheat system, or DRM scheme will work.

## Alternatives considered

- **Continue the reimplementation.** Rejected for the reasons above.
- **Delete Airlock Core entirely.** Rejected: the PE inspector, analyzer, and
  container manager are working, tested, and directly useful, and a rewrite
  would cost months to reach the same place.
- **Keep both paths and let users choose.** Rejected as a *product* position:
  shipping a non-functional "no-Wine mode" next to a real one invites users to
  pick the one that cannot work. The analysis toolkit remains available as a
  library and CLI, clearly labelled as analysis rather than execution.
