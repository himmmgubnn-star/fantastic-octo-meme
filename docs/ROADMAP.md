# Airlock roadmap

Airlock orchestrates mature upstream runtimes. It does not reimplement Windows —
see [ADR-0002](adr/0002-orchestrate-dont-reimplement.md).

Legend: ✅ done · 🔶 in progress · ⬜ planned · ➖ withdrawn

Every phase below has an exit criterion that is a command you can run. A phase
is not done because code was written; it is done because the check passes.

---

## Withdrawn

These items came from the pre-rebrand plan to build a Wine competitor. They are
withdrawn, not deferred.

- ➖ **Milestone 1 — Execution & ABI emulation.** Invoking a PE entry point,
  marshalling `__stdcall`, threads, `GetLastError`. Superseded by running real
  Wine through `WineRuntimeAdapter` (Phase 2).
- ➖ **Milestone 3 — The GUI stack.** `USER32` → X11/Wayland, `GDI32` → 2D
  drawing, DirectX → Vulkan. Superseded by Wine's own windowing and by
  DXVK/VKD3D (Phase 3).
- ➖ **Milestone 2 — PE/PE+ architecture depth**, to the extent it existed to
  serve execution. The parts that serve *analysis* stay, and are listed under
  Airlock Core below.

The `src/win32/mod_*.c` modules remain in the tree. They are the API surface the
compatibility analyzer scores against — metadata, not a runtime. No user-facing
text may describe them as an implementation.

---

## Shipped: Airlock Core

The host-side toolkit. All of it is covered by `make test` (seven suites) and
runs clean under ASan+UBSan.

- ✅ **PE analysis** — DOS/COFF/optional headers (32- and 64-bit), section
  tables, data directories, section-aligned image mapping, bounds-checked RVA
  reads, import and export parsing, base-relocation validation.
- ✅ **Inspector and analyzer** — architecture, imported DLLs and APIs, TLS,
  resources, manifests, COM/CLR, delay-loads, .NET and VC-runtime requirements,
  detected graphics/audio/input/networking, per-subsystem scoring, missing-API
  diagnostics, configuration recommendations.
- ✅ **Compatibility database** — persistent per-application requirements and
  rating.
- ✅ **Container manager** — create, list, info, delete, clone, backup, restore,
  export, import; fixed-at-creation architecture; `ALK1` archives with
  backward-compatible reads and validated extraction paths.
- ✅ **Workspace layer** — app library, per-app launch settings, versioned
  exportable profiles, snapshots and rollback, config diff, repair, launch
  doctor, redacted support bundles, safety and permission reports, shader-cache
  management, device report.
- ✅ **Cross-cutting** — high-resolution timer and frame-time diagnostics,
  futex-based synchronization, lock-free shared-memory ring, persistent shader
  cache, dynamic tracing, crash capture, plugin and backend selection, audio
  backends (WAV sink / optional ALSA / null).
- ✅ **Project scaffolding** — MIT `LICENSE`, `THIRD_PARTY_NOTICES`,
  `SECURITY.md`, `CHANGELOG.md`, `.clang-format`, CI, architecture decision
  records.

**What Airlock Core cannot do:** run anything. No execution backend exists.

---

## Loader hardening (continuous)

Airlock parses executables it does not trust, so this is not a phase that
completes — it is a standing obligation.

- 🔶 **Bounds on every file-supplied offset.** RVA resolution, section mapping,
  thunk reads, and the `SizeOfImage` allocation are bounded. Four defects found
  by fuzzing are fixed and pinned by `test_malformed_headers()`.
- ✅ **Fuzzing in CI.** 40 seeds x 20,000 mutations under ASan+UBSan on every
  push.
- ⬜ A checked-in corpus of real-world malformed and hostile PEs, so coverage
  does not depend on one seed image.
- ⬜ Structural (grammar-aware) mutation rather than random byte flips.
- ⬜ Coverage-guided fuzzing (`libFuzzer`/AFL++) alongside the deterministic
  harness.
- ⬜ Fuzzing the `ALK1` archive reader and the profile parser, not just the PE
  loader.

## Phase 1 — Foundations 🔶

> **Goal:** the versioned data models and the safe primitives everything else
> is built on.

- ⬜ Versioned configuration schema for `Container`, `Launcher`,
  `VirtualMachine`, `GraphicsProfile`, `TranslationProfile` — JSON, with an
  explicit `schemaVersion` on every document.
- ⬜ Schema migration from older versions, with a test per migration step.
- ⬜ Validation rules: UUIDs, paths, architectures, enumerations, required
  fields, and rejection of anything not in the schema.
- ⬜ Storage abstraction (host filesystem, scoped storage later) behind an
  interface.
- ⬜ **Safe process-launch abstraction.** Argument vectors only, never a command
  string. Captured stdout/stderr, exit code, timeout, and cancellation.
- ⬜ Structured logging with levels, categories, and per-operation records.
- ⬜ CI runs the new unit tests on every push.

**Exit criterion:** unit tests prove that (a) every model round-trips through
serialize → deserialize → validate, (b) a downgraded `schemaVersion` document
migrates forward to the current one, (c) the process launcher runs
`/bin/echo` with an argument containing spaces, quotes, and `;` and the argument
arrives intact as a single argv entry, and (d) the same launcher never invokes a
shell.

## Phase 2 — Container MVP

> **Goal:** the first honest launch. A real Wine process, started by Airlock,
> with its output captured.

- ⬜ `WineRuntimeAdapter`: detect an installed Wine, read its version and
  architecture support, initialize a prefix, run a program, return its exit code.
- ⬜ Container create / list / rename / delete on top of the Phase 1 models.
- ⬜ Launcher records: executable path, working directory, arguments, icon,
  per-launch overrides.
- ⬜ Launch records: timestamps, exit code, runner version, log reference.
- ⬜ Safe stop of a hung launch.
- ⬜ Pre-launch "Run checks": executable exists, container exists, runner
  exists, free storage above threshold, selected backend available.

**Exit criterion:** an integration test creates a container, initializes it with
a Wine runner if one is present (and skips with an explicit reason if not), and
verifies that a deliberately failing launch produces a launch record with a
non-zero exit code and a non-empty captured log. A second test asserts that no
shell is involved in any launch.

## Phase 3 — Usable Compatibility Mode

- ⬜ Runner inventory: detect installed runners with version, architecture
  support, source, size, and status; per-container selection; import from a
  user-chosen local archive; download only from a user-confirmed trusted URL
  showing version, checksum, source, license, and storage cost; never silently
  replace a runner in use; rollback to a previous runner.
- ⬜ Installer flow for user-provided `.exe` / `.msi` / `.bat`, followed by
  executable discovery across sensible prefix locations and a launcher picker.
- ⬜ Restore points before installer and dependency actions.
- ⬜ Declarative dependency recipes (source, version, hash, license,
  architecture, install action, rollback guidance) — not opaque shell scripts.
- ⬜ `BoxRuntimeAdapter` for ARM64 hosts, exposing only presets the detected
  Box86/Box64 version actually supports.
- ⬜ `GraphicsBackendAdapter`: detect host capabilities first, default to Auto,
  never offer an unavailable backend as usable.
- ⬜ Basic input profiles.

**Exit criterion:** recipe validation rejects a recipe with a missing hash or an
unknown architecture; runner detection reports the real `wine --version` output
on a host that has Wine; and the backend list contains only backends whose probe
succeeded.

## Phase 4 — Android usability

- ⬜ Kotlin + Jetpack Compose app, calling application services through a
  view-model layer. No filesystem or process calls from UI code.
- ⬜ Scoped storage and the Storage Access Framework; explicit display of which
  folders are shared with a container.
- ⬜ Document picking, foreground-service handling, notifications.
- ⬜ External keyboard, mouse, gamepad, and display detection.
- ⬜ Touch-control presets (mouse + keyboard, FPS, gamepad, strategy, desktop),
  saved per launcher. A visual mapping editor comes only after presets and input
  dispatch work.
- ⬜ Android-specific diagnostics; thermal and battery-aware profiles, claiming
  only what the OS actually permits.

**Exit criterion:** instrumented tests cover scoped-storage permission flows and
preset persistence; the JNI surface is enumerated and each entry has a test.

## Phase 5 — VM foundation (experimental)

> **Goal:** boot the user's own Windows installation. Marked experimental at
> every layer.

- ⬜ `VirtualMachine` metadata and a creation wizard whose first step states the
  legal requirement to supply your own media and license.
- ⬜ `QemuRuntimeAdapter`: detect the QEMU binary and its supported features,
  report unsupported architecture or device combinations *before* launch.
- ⬜ Command-line generation from validated structured configuration, as an
  argument vector. QEMU-specific settings kept separate from generic VM
  metadata.
- ⬜ Virtual disk creation in a format the chosen QEMU integration supports.
- ⬜ Start, graceful shutdown, force stop, remove — each destructive disk
  operation behind explicit confirmation.
- ⬜ Networking off by default for new VMs, with an explicit
  disabled / NAT / advanced choice.

**Exit criterion:** a test generates the full QEMU argument vector from a fixture
configuration without launching anything, and asserts that a value containing
`"; rm -rf /"` appears as a single inert argv entry. A second test asserts that
an unsupported architecture/device pair is refused before any process starts.

## Phase 6 — VM improvements

- ⬜ Snapshots, only where the selected disk format and backend support them.
- ⬜ Display surface, input capture and release, keyboard shortcuts, mouse and
  touch-to-mouse modes.
- ⬜ Shared folders, opt-in and read-only by default.
- ⬜ Clipboard sharing only if it can be implemented safely.
- ⬜ VM logs and support bundles kept distinct from Compatibility Mode.
- ⬜ Honest, in-UI statements about expected performance.

## Phase 7 — Polish

- ⬜ Accessibility and localization-ready strings.
- ⬜ Onboarding and first-run guidance.
- ⬜ Upgrade and configuration migration, with recovery from interrupted
  operations.
- ⬜ Release packaging for Linux distributions and an Android APK.
- ⬜ In-app licenses screen backed by `THIRD_PARTY_NOTICES`.
- ⬜ Documentation for users and for contributors adding a runtime adapter.

---

## Manual test matrix

To be run before any release, and recorded in the release notes:

| Axis | Cases |
|---|---|
| Host | Linux x86_64 · Linux ARM64 · Android ARM64 (at least two capability tiers) |
| Graphics | No Vulkan · Vulkan available |
| Storage | Low storage · adequate storage |
| Input | External keyboard · mouse · gamepad · external display |
| Network | Offline · interrupted download · interrupted import |
| Data | Corrupt archive · missing runtime component · upgrade from an older configuration version |

---

## Guiding principles

1. **Integrate, do not reimplement.** Wine, Box86/Box64, and QEMU are the
   runtimes. Airlock is the layer that makes them usable.
2. **Never claim capability that is not tested.** If a backend is not detected,
   it is not offered. If a feature is not implemented, the UI says so.
3. **Consent before network.** Nothing is downloaded without an explicit,
   informed confirmation.
4. **Isolation is described honestly.** A container is file-layout and
   configuration isolation, not a security boundary.
5. **Keep the modes separate.** Compatibility Mode and VM Mode never share UI,
   logs, support bundles, or wording.
