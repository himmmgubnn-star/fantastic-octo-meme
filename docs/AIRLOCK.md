# Airlock product layer

**Airlock Core** is the C engine (PE analysis, prefixes, profiles, tracing);
the **Airlock product layer** sits on top of it: one isolated environment per app, guided setup, an app library,
versioned profiles, rollback, diagnostics, permissions, controls, performance
modes, and shader-cache management.

The whole layer is available from the CLI:

```sh
airlock app add "My Game" setup.exe --kind exe --launch drive_c/Games/My/game.exe
airlock app list
airlock app show "My Game"
airlock app set "My Game" gfx_backend Vulkan
airlock app doctor "My Game"
airlock app snapshot "My Game" before-update
airlock app rollback "My Game" before-update
airlock app support "My Game" support.zip
airlock app diagnose wine.log
airlock app safety "My Game"
airlock profile list|show|apply|export|import "My Game" [label|path]
airlock device report
airlock container create "My Game" setup.exe --launch game.exe
```

A workspace is a directory under `$AIRLOCK_ROOT` (defaults to the Airlock
prefix directory):

```
<root>/<name>/
  app.conf                  workspace metadata / app-library record
  prefix.conf               runner, windows version, gfx/audio
  drive_c/                  isolated Windows filesystem
  profiles/<label>.profile  versioned, exportable compatibility profiles
  profiles/current.profile   active profile
  snapshots/<label>/        one-click rollback points
  shortcuts/                .desktop launcher
  shadercache/              persistent shader cache
```

## Compatibility profile

Profiles are stored as editable YAML and are tied to an executable hash:

```yaml
name: Example Game
label: v1
runner: airlock-wine-10.x
architecture: x86
windows_version: win10
graphics.backend: vulkan
audio.backend: alsa
runtime: box64_preset=performance; esync=1
dependencies:
  - vcruntime
  - d3dcompiler_47
dll_overrides: d3d11=native,builtin
windows.resolution: 1280x720
windows.virtual_desktop: 1
launch.executable: drive_c/Games/ExampleGame/game.exe
launch.arguments: -fullscreen
trust: community
source: curated-pack
version: 1
```

## Feature matrix

| Area | Feature | Status |
|---|---|---|
| Core | One environment per app (workspace / prefix) | **Done** |
| Core | Guided setup (EXE / MSI / import / portable) | **Done** (MSI is setup metadata + journal; MSI database parsing remains planned) |
| Core | Smart executable detection (arch, gfx/audio, VC/CLR, hash, icon, shortcut) | **Done** |
| Core | App library (list/search/tags/favorites/size/runner/rating) | **Done** |
| Profiles | Versioned, exportable YAML profiles tied to executable hash | **Done** |
| Profiles | One-click snapshot / rollback before changes | **Done** (config snapshot) |
| Profiles | Config diff view | **Done** |
| Profiles | Repair mode (dirs, config, shortcut) | **Done** |
| Graphics | Renderer selector (Auto/Vulkan/OpenGL/Software) | **Done** in profile/workspace; runtime hot-selection already exists |
| Graphics | Per-app performance modes (balanced/battery/performance/custom) | **Done** (mode stored; no hardware governor yet) |
| Graphics | Shader-cache manager (size/clear) | **Done** |
| Graphics | First-run graphics wizard | **Partial** (device report + doctor diagnostics) |
| Graphics | Frame tools, resolution manager, virtual desktop | **Partial** (resolution/DPI/virtual desktop stored; overlay is planned) |
| Controls | Gamepad/touch profile per game | **Done** (metadata; editor itself planned) |
| Android | Device capability report (ABI/RAM/renderer/storage/controllers) | **Done** (report; storage/thermal real probes planned) |
| Android | Container manager | **Done** (alias for workspace create) |
| Android | Touch-control editor, external-display presets, thermal protection | **Planned** |
| Diagnostics | Launch doctor | **Done** |
| Diagnostics | Readable logs (classify Wine/Box64 messages) | **Done** |
| Diagnostics | Support bundle (sanitized) | **Done** |
| Diagnostics | Compatibility test button (`airlock test` / workspace doctor) | **Done** |
| Diagnostics | Privacy controls (redact home/user paths) | **Done** |
| Community | Compatibility cards / verified profiles | **Partial** (trust field + import/export) |
| Community | Reproducible reports, AppDB links, offline packs | **Partial** (export/import profiles; AppDB cards planned) |
| Safety | App isolation + permission dashboard | **Done** (permissions stored/reported; real sandboxing planned) |
| Safety | No hidden downloads (dependency metadata) | **Done** (runtime manager + deps field) |
| Safety | Malware warning | **Done** in safety report |
| Developer | CLI + library API | **Done** |
| Developer | Runner manager, plugin architecture, dev console | **Partial** (runtime manager + plugin layer exist; runner pinning planned) |
| Developer | Automated compatibility tests | **Done** (`airlock test`, `make test`) |

## Design rules

1. **Safe by default.** Permissions are opt-in; sandbox is on by default for
   new workspaces; support bundles redact `$HOME`/usernames; imported profiles
   are marked `community` and never silently overwrite local profiles.
2. **Reliability first.** The workspace is a thin record; the heavy isolation
   comes from the existing prefix/shell/runtime layers.
3. **Resource-bound.** The workspace manager only reads small metadata files;
   large caches are deleted/moved with the existing `ALK1`-style code paths.
4. **Everything is testable.** `tests/test_workspace.c` exercises install,
   library, profiles, diffs, snapshots, rollback, doctor, support, diagnose,
   safety, shader cache, and device report in a temporary directory.
