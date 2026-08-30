# 0001. Rebrand to Airlock and keep the two runtime modes separate

- Status: accepted
- Date: 2026-08-30

## Context

The repository contained two product identities. The engine was called
**Cellar**; a user-facing layer added later was called **Winaltor**. Neither
name is usable:

- **Winaltor** differs by one letter from **Winlator**, an established
  LGPL-2.1 Android application that does the same job (Wine plus Box86/Box64
  containers) and publishes at `winlator.org`. Shipping a product with a
  near-identical name in the identical category is an avoidable trademark and
  unfair-competition exposure. The project brief itself forbids copying
  Winlator branding.
- **Cellar** collides with the "bottle/cellar" metaphor used across the Wine
  ecosystem, including an existing GPL front-end named Bottles, so it does not
  distinguish the product either.

The repository also had no `LICENSE` file despite claiming MIT, no third-party
notices, and no CI.

## Decision

1. Rename the product to **Airlock**. The name evokes a sealed passage between
   two environments, which matches the product: a Compatibility Mode
   environment and a Virtual Machine environment, each sealed from the host and
   from each other.
2. Apply the rename **fully**, not just in documentation: `include/airlock/`,
   the `airlock_*` symbol prefix, `AIRLOCK_*` macros and environment variables,
   build artefacts, the CLI, the docs, and the sample profile. No occurrence of
   a retired name may remain; CI greps for them.
3. Where documentation needs to distinguish the engine from the product, the
   engine is **Airlock Core** and the user-facing surface is the **Airlock
   product layer**.
4. Keep **Compatibility Mode** and **Virtual Machine Mode** separate in code,
   UI, settings, logs, documentation, support bundles, and user messaging.
   Neither may be described as the other. Wine is a compatibility layer that
   implements Windows-facing APIs on a Unix-like host; it is not a full Windows
   emulator, and Airlock must never call it one.
5. All new code is written from scratch. No Wine, ReactOS, or Winlator source,
   branding, or assets are copied.

## Consequences

- Every tracked file changed in one mechanical commit. Reviewing it means
  reviewing the rename rule, not each line; the rule is a fixed set of
  case-sensitive substitutions plus five path moves.
- Users of pre-rebrand builds have one compatibility obligation: container
  archives. That is handled by ADR-0003.
- Environment variables change name (`AIRLOCK_LOG_LEVEL`, `AIRLOCK_TRACE`,
  `AIRLOCK_WAV_OUT`, `AIRLOCK_ROOT`, `AIRLOCK_PREFIX`). No shim is provided
  because no release ever shipped the old names.
- Choosing a name is not clearing it. A trademark search in the target
  jurisdictions is still required before any public release; that is a
  maintainer task, not an engineering one.

## Alternatives considered

- **Keep Winaltor.** Rejected: the confusion risk is the reason for the
  rebrand.
- **Rename documentation only.** Rejected: leaving `cellar_*` symbols and
  `CELLAR_*` environment variables in the source keeps the retired name in the
  shipped artefacts and in every stack trace.
- **A Windows-themed name.** Rejected: names built on "Win" sit closest to
  Microsoft's marks and to the existing Winlator-family of projects.
