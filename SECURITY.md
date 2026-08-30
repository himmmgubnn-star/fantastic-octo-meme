# Airlock security policy

## Reporting a vulnerability

Report security problems privately to the repository owner before publishing
them. Include a reproduction, the affected version or commit, and whether the
issue needs a coordinated disclosure window. We aim to acknowledge reports
within five business days.

## Threat model

Airlock runs software the user did not write, on hardware the user owns. The
threat model is therefore explicit about what is and is not a boundary.

### Untrusted input

Treated as hostile, always:

- **Windows executables, installers (`.exe`, `.msi`, `.bat`), and DLLs** chosen
  by the user. These are arbitrary programs. They are never parsed as
  configuration and never executed as shell code.
- **Imported container archives and profiles.** Every archive member path is
  validated by `archive_member_is_safe()` in `src/prefix/prefix.c` before
  extraction: POSIX-absolute paths, UNC paths, drive-absolute Windows paths, and
  any path containing a `..` component are rejected. Covered by
  `test_prefix_archive()` in `tests/test_ecosystem.c`.
- **Downloaded components.** Nothing is downloaded without explicit user
  consent, and a download must present a visible version, source URL, checksum,
  license, and storage cost before the user confirms.

### Command execution

- No part of the codebase calls `system()`, `popen()`, or a shell. The single
  dynamic-load call is `dlopen()` in `src/plugin/plugin.c`, restricted to plugin
  paths the user selected.
- Configuration data is never interpreted as shell code.
- When execution backends land, they will be launched with an **argument vector
  passed to `execve`-family calls**, never by building a command string. Paths
  are passed as separate argv entries so that spaces, quotes, and shell
  metacharacters in a user's filename cannot alter the command.

### What Airlock does *not* promise

- **A Wine prefix is not a security boundary.** Wine implements the Windows API
  surface on a Unix-like host with the invoking user's privileges. A malicious
  Windows program running under Wine can read and write files that user can,
  open network connections, and exhaust resources. Per-container isolation in
  Airlock is **file-layout isolation and configuration separation**, not a
  sandbox. We will say so in the UI rather than implying containment.
- **Best-effort hardening only.** Where the host offers real isolation
  (Linux namespaces, `bwrap`/`firejail`, Android's app sandbox), Airlock will
  use it and will report honestly what is and is not active on that device.
  Where it does not, Airlock will not claim otherwise.
- **VM Mode isolation depends on the host.** QEMU provides process-level
  isolation, not a guarantee against virtualization escapes. Networking is off
  by default for new VMs, and shared folders are opt-in and read-only by
  default.

### Parsing untrusted executables

Airlock parses PE files it does not trust, so every offset that comes out of a
file is treated as a lie until bounded:

- `airlock_image_rva()` and `airlock_image_rva_raw()` bound the RVA against the
  bytes actually held, not against `SizeOfHeaders` or the section table.
- Section mapping bounds both the source range in the file and the destination
  range in the mapping.
- `SizeOfImage` is capped at `AIRLOCK_MAX_IMAGE_SIZE` (2 GiB) before it is used
  as an allocation size.
- All table walks go through `airlock_image_read()`, which checks `rva + len`
  against the mapped size.

This is enforced by CI, not by review alone: the `loader-fuzz` job runs 800,000
mutated inputs through the loader under ASan+UBSan on every push. Four real
out-of-bounds and over-allocation defects were found this way and are recorded
in the changelog; `test_malformed_headers()` pins each one.

### Data handling

- **No telemetry.** Airlock collects nothing by default and makes no network
  connection unless the user initiates one. If telemetry is ever added it will
  be opt-in, documented here, and removable.
- **Support bundles are redacted.** Home directory paths, usernames, tokens,
  documents, and app-private data are redacted or removable before a bundle is
  written, and uploading a bundle always requires explicit consent.
- **Least privilege.** Android uses scoped storage and the Storage Access
  Framework; broad storage permissions are not requested unless a feature
  genuinely requires them, and the UI explains why.

## Credential hygiene

- No secret may be committed. `.env` is gitignored and untracked.
- A Discord bot token was committed in this repository's early history (commit
  `338c6b6` and its descendants) inside an unrelated `.env` file. It was
  untracked in commit `aae7d17`. **Removing a file from tracking does not remove
  it from history.** That token must be treated as compromised and rotated, and
  history rewriting is required if it must not remain readable.

## Supported versions

Airlock is pre-1.0. Only the tip of the default branch receives security fixes.
