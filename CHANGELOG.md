# Changelog

All notable changes to Airlock are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
semantic versioning once it reaches 1.0; until then, minor versions may break
configuration formats, and any such break is listed here with its migration.

## [Unreleased]

### Changed — breaking

- **Renamed the project to Airlock.** Every identifier, path, header, macro,
  environment variable, build artefact, CLI command, and document moved from
  the retired `Cellar`/`Winaltor` names. There is no compatibility shim,
  because no release ever shipped the old names.
  - `include/cellar/` → `include/airlock/`, `cellar.h` → `airlock.h`
  - symbol prefix `cellar_*` → `airlock_*`
  - `CELLAR_LOG_LEVEL`, `CELLAR_TRACE`, `CELLAR_WAV_OUT`, `CELLAR_PREFIX` →
    `AIRLOCK_LOG_LEVEL`, `AIRLOCK_TRACE`, `AIRLOCK_WAV_OUT`, `AIRLOCK_PREFIX`
  - `WINALTOR_ROOT` → `AIRLOCK_ROOT`
  - `build/cellar` → `build/airlock`, `libcellar.a` → `libairlock.a`,
    CMake target `cellar-cli` → `airlock-cli`
  - `docs/WINALTOR.md` → `docs/AIRLOCK.md`,
    `samples/winaltor.profile` → `samples/airlock.profile`
- **Container archive magic `CBK1` → `ALK1`.** Restore still reads `CBK1`, so
  existing backups and exports keep working with no migration. See
  [ADR-0003](docs/adr/0003-container-archive-format.md).

### Added

- `LICENSE` (MIT). The repository previously claimed MIT in the README while
  shipping no license file.
- `THIRD_PARTY_NOTICES` recording that nothing third-party is currently
  bundled, and the obligations for Wine, Box86/Box64, DXVK, VKD3D-Proton, QEMU,
  and Mesa when they are invoked or bundled.
- `SECURITY.md` with the threat model, the untrusted-input list, the command
  execution rules, and an explicit statement that a Wine prefix is not a
  security boundary.
- `CHANGELOG.md`.
- `.clang-format` codifying the style the sources already follow.
- `.github/workflows/ci.yml`: Make build and tests on gcc and clang, an
  ASan+UBSan run, a **sanitized PE-loader fuzz job** (40 seeds x 20,000
  mutations), a CMake+ctest run, an Android `arm64-v8a` cross-compile, a
  retired-brand-name grep over code and tracked paths, and an advisory
  clang-format check.
- `docs/adr/` with ADR-0001 (rebrand), ADR-0002 (orchestrate rather than
  reimplement), and ADR-0003 (archive format).
- `test_prefix_archive()` covering archive format, backward compatibility, and
  extraction safety.
- `test_malformed_headers()` pinning the four loader bounds above.

### Fixed — security

- **Four memory-safety defects in the PE loader**, all reachable from a
  malformed or hostile `.exe` and all found by running `tools/fuzz_loader.c`
  under AddressSanitizer:
  1. `map_sections()` checked a section's `pointer_to_raw_data` against the file
     length but never `pointer_to_raw_data + size_of_raw_data`, so a section
     claiming more raw data than the file holds caused a heap out-of-bounds
     read. A source bound was added.
  2. `airlock_image_rva()` returned `mapped + rva` for any RVA below
     `SizeOfHeaders`, or inside a section's claimed extent, without bounding
     `rva` against the size actually mapped. A mutated `SizeOfHeaders` therefore
     yielded pointers past the end of the allocation.
  3. `airlock_image_rva_raw()` had the same two holes against the file buffer,
     and additionally computed `raw + pointer_to_raw_data + delta` without
     checking the sum against the file length.
  4. `map_sections()` used the file-supplied `SizeOfImage` directly as a
     `calloc` size with no ceiling, so a few mutated bytes produced a
     multi-gigabyte allocation — a trivially reachable out-of-memory condition.
     `SizeOfImage` is now capped at `AIRLOCK_MAX_IMAGE_SIZE` (2 GiB).
  `read_thunk()` now reads through the bounds-checked `airlock_image_read()`
  instead of dereferencing a resolved pointer, so a thunk in the last bytes of
  an image cannot read past the mapping.

  Before the fix, 5 of 6 fuzz seeds aborted under ASan (heap-buffer-overflow,
  heap-use-after-free, SEGV, allocator out-of-memory). After it, 40 seeds x
  20,000 mutations — 800,000 malformed PEs — run clean under ASan+UBSan with
  leak detection. `test_malformed_headers()` pins all four cases, and each was
  confirmed to fail or crash against the pre-fix loader.

- **Archive extraction path validation.** `airlock_prefix_restore()` validated
  member paths with `strstr(rel, "..")`, which wrongly rejected legitimate names
  such as `save..bak` while relying on substring matching rather than path
  structure. It now uses `archive_member_is_safe()`, which rejects empty,
  POSIX-absolute, UNC, drive-absolute, and `..`-component paths.
- **Honest capability wording.** The CLI no longer calls itself "a Windows
  compatibility layer", and both `--help` and `prefix launch` now state that no
  execution backend is wired up and name the intended backends. Previously the
  wording implied a compatibility layer that could run programs.

### Security

- Untracked `.env`, which contained a Discord bot token unrelated to this
  project. **The token remains readable in git history and must be rotated.**
  See the credential hygiene section of `SECURITY.md`.

### Removed

- The roadmap items "Milestone 1 — Execution & ABI emulation" and
  "Milestone 3 — The GUI stack", which described reimplementing Wine. They are
  superseded by the phase plan in `docs/ROADMAP.md`. See
  [ADR-0002](docs/adr/0002-orchestrate-dont-reimplement.md).

## [0.2.0] — pre-rebrand, as `Cellar`

- PE loader, Win32 export registry, portability layer, audio backends,
  performance kit.
- Application Compatibility Analyzer, per-app profiles, Windows-version
  behavior modes.
- Inspector, compatibility database, prefix/container manager, runtime manager,
  compatibility test lab, COM/OLE, desktop integration.
- Application workspace layer: app library, versioned profiles, snapshots and
  rollback, launch doctor, support bundles, safety and permission reports,
  shader-cache management, device report.
- No execution capability in any of the above.
