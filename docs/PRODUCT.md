# Winaltor → Cellar: product spec → implementation map

This document records how the "Winaltor" product spec maps onto **Cellar**.
Two efforts contributed to this surface:

- **Milestone 4.8** — the underlying compatibility ecosystem (prefix manager,
  compatibility analyzer, runtime manager, test lab, COM, …).
- **Milestone 4.9** — the user-facing layer: the **Winaltor application
  workspace** (`src/workspace/workspace.c`, `include/cellar/workspace.h`,
  `cellar app/profile/container/device`) plus the **container/prefix manager
  upgrades** (`src/prefix/prefix.c` — architecture, clone, export/import,
  container-level settings).

Legend: ✅ implemented · ⬜ deferred (next release) · ➖ intentionally postponed

---

## Must-have features

| Feature | Status | Implementation |
|---|---|---|
| App containers | ✅ | `cellar prefix create [--arch win32\|win64]` — one container = one prefix. Architecture is fixed at creation (converting is not supported), per the spec. |
| App library | ✅ | `cellar app list / search / add / remove / show / favorite` — one workspace per app with tags, favorites, size, runner, and compatibility rating. |
| EXE / MSI / BAT flow | ✅* | `cellar app add [--kind exe\|msi\|import\|portable] [--launch EXE]` records the launcher and setup. (`MSI` here is a metadata/journal flow; real MSI database parsing is deferred.) |
| Per-app launch settings | ✅ | `cellar app set` — working dir, launch args, environment variables, Windows version, graphics backend, resolution, virtual desktop, Box64 preset, CPU/frame limits, ESync/FSync, DLL overrides. |
| Multiple runners | ✅ | Per-container runner (`cellar prefix set NAME runner stable\|experimental\|<path>`). |
| Prefix backup | ✅ | `cellar prefix backup / restore / clone / export / import` (CBK1 archive). |
| Basic logs | ✅ | `cellar app diagnose`, `cellar app support` (support bundle), `cellar app doctor`. |
| Open in file manager | ⬜ | Workspace + prefix `drive_c` paths are exposed (`cellar app show`); a dedicated open-folder command is deferred. |

## Settings worth exposing (safe subset first, rest behind Advanced)

| Setting | Status | Notes |
|---|---|---|
| Architecture (Win32/Win64) | ✅ | Set only at container creation (`--arch`). |
| Windows version | ✅ | Win 7 / 8.1 / 10 / 11 presets. |
| Runner | ✅ | Stable / Experimental / manual path. |
| Graphics backend | ✅ | Auto / DXVK / VKD3D / wined3d. |
| Rendering resolution | ✅ | `cellar app resolution NAME W H [DPI [VIRTUAL]]`. |
| Virtual desktop | ✅ | On/off, width, height, DPI. |
| Box64 preset | ✅ | `cellar prefix set NAME box64 compat\|balanced\|perf\|custom`. |
| Environment variables | ✅ | Validated key/value editor (`cellar app set`). |
| DLL overrides | ✅ | Search-and-select list (`cellar app set NAME dll_overrides ...`). |
| CPU core limit + frame cap | ✅ | Per app / container. |
| ESync / FSync | ✅ | Advanced options, documented as varying by app. |

## Android features

| Feature | Status |
|---|---|
| Storage-location selector | ➖ (workspace + prefix paths already parameterized) |
| Touch control presets | ⬜ (deferred; `app controls` stores the selection) |
| External-input detection | ⬜ |
| Resolution presets | ✅ (shared with desktop resolution presets) |
| Device diagnostics | ✅ (`cellar device report`) |
| Thermal/battery mode | ⬜ |
| Import/export container ZIP | ✅ (`cellar prefix export/import`; ZIP wrapper is a front-end concern) |

## Installation helpers

| Feature | Status |
|---|---|
| Dependency installer | ✅* | Runtime manager exists (`cellar runtime install vcruntime\|dotnet\|directx\|fonts\|systemlibs`) but is marker-based, not a binary downloader. |
| Recipe system | ⬜ | Workspace profiles are a partial step (`cellar profile`). |
| Known-app templates | ⬜ |
| Executable finder | ⬜ | Workspace records the launcher at install. |
| Install rollback / snapshots | ✅ | `cellar app snapshot` / `cellar app rollback`. |

## Troubleshooting features

| Feature | Status |
|---|---|
| Launch checklist | ✅ (`cellar app doctor` launch doctor) |
| Clear error cards | ✅ (doctor report translates failures) |
| Last launch details | ✅ (workspace record + support bundle) |
| Export diagnostic bundle | ✅ (`cellar app support`) |
| Safe reset actions | ✅ (`cellar app shader clear`, `app repair`, `app rollback`) |
| Task viewer | ⬜ (deferred) |

## Features to postpone

All respected — no custom driver, no "best-settings" claims, no cloud sync,
no store integration, no registry optimizers, no plugin system, no
multiplayer/anti-cheat promises. A custom Win32 implementation is Cellar's
whole point, but it is built incrementally rather than promised upfront.

## Release plan status

- **v0.1** (containers + run exe/msi/bat + per-container runner/arch/args/env
  + gfx/resolution + logs/support bundle + input detection) — ✅ except process
  execution (Milestone 1) and touch/input detection (deferred).
- **v0.2** (dependency installer, recipes, snapshots, executable discovery,
  save-data mgmt, per-app gamepad/touch editor) — foundation done, pieces ⬜.
- **v0.3** (curated compatibility profiles, repair wizard, runner manager,
  import from other Wine managers) — partially ⬜ (profiles/repair landed).
