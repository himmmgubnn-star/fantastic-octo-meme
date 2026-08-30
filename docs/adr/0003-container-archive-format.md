# 0003. Container archive format ALK1, with backward-compatible reads

- Status: accepted
- Date: 2026-08-30

## Context

Container backup, export, clone, and import all go through one hand-rolled
archive format in `src/prefix/prefix.c`. Two problems surfaced during the
rebrand:

1. The magic bytes said `CBK1`, a name from a retired product identity.
2. Archive member paths were validated with `strstr(rel, "..")`. That rejects a
   legitimate filename such as `save..bak`, and — more importantly — it is the
   wrong shape of check. Substring matching is not path validation, and the
   format has no absolute-path rule at all, so a `C:\...` or `/etc/...` member
   was only stopped by accident of how the path was later joined.

Archives are user data. Some may already exist on disk.

## Decision

### Format

```
magic   4 bytes, "ALK1"
record  u32 path_len | path_len bytes relative path | u32 data_len | data
end     u32 path_len == 0
```

All integers are little-endian. Paths are relative to the container root and
use `/` as the separator. Only regular files are stored; directories are
recreated on demand from their members' paths.

### Versioning and migration

- New archives are always written with the magic `ALK1`.
- Restore accepts `ALK1` **and** the pre-rebrand `CBK1`, whose record layout is
  identical. No user action and no migration step is required.
- Any other magic is rejected with `AIRLOCK_ERR_INVALID_ARGUMENT` before
  anything is created on disk.
- If the layout ever changes incompatibly, bump the magic (`ALK2`) rather than
  overloading `ALK1`, and keep the older reader.

### Extraction safety

`archive_member_is_safe()` must pass before any member is written. It rejects:

- an empty path;
- a POSIX-absolute path (`/…`);
- a UNC path (`\\host\share`);
- a drive-absolute Windows path (`C:\…`);
- any path containing a `..` **component** (so `../x`, `a/../b`, and a bare `..`
  are all rejected, while `save..bak` is accepted).

Members are also length-bounded (`path_len < 512`) and a truncated record fails
the whole restore rather than producing a partial container.

## Consequences

- Archives are self-describing by magic, so a future format change is a reader
  addition rather than a data migration.
- The format carries no checksum and no compression. A truncated or corrupt
  archive is detected only structurally. Adding a trailing checksum record is
  the obvious next step and would require `ALK2`.
- Symlinks are not archived. That is deliberate: archiving a link that points
  outside the container would be a way to write outside it on restore.
- The archive is not a security boundary on its own. It is one of the untrusted
  inputs listed in `SECURITY.md`, and it is validated as such.

## Test coverage

`test_prefix_archive()` in `tests/test_ecosystem.c` asserts: new backups use
`ALK1`; a round trip preserves file content; a rewritten `CBK1` archive still
restores; an unknown magic is refused and creates no container; a `../escape.txt`
member is refused and no file appears outside the container root; and
`save..bak` extracts successfully.
