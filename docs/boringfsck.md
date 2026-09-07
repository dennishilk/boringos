# boringfsck

`boringfsck` is the Milestone 20 host-side, read-only structural checker for BoringFS v0 images.

## Purpose

The checker reads the bytes of the image supplied on the command line, decodes BoringFS metadata through the shared BoringFS codec, and delegates structural validity to the shared `boringfs_validate_volume()` implementation. It does not maintain a parallel filesystem model or duplicate the validator rules.

The data path is:

```text
image bytes
    ↓
shared BoringFS codec
    ↓
shared BoringFS structural validator
    ↓
boringfsck human-readable status + process exit status
```

## Syntax

```text
build/boringfsck <image>
build/boringfsck --help
```

There is no interactive mode and no repair option.

## Exit status

```text
0  structurally valid BoringFS v0 image
1  readable image rejected by the shared BoringFS validator
2  invocation, host-I/O or local resource failure
```

A corrupt filesystem is therefore distinguished from a path that cannot be opened or inspected.

## Output

A valid image reports decoded geometry only after the shared validator has accepted the image. Stable acceptance markers include:

```text
Status: VALID
```

A corrupt image reports the validator's own result name:

```text
Status: CORRUPT
Reason: <shared validator result name>
```

When the shared validator supplies a real location, `boringfsck` also emits one or more of:

```text
Object: <id>
Block: <block>
Directory record: <index>
```

Sentinel location values are never presented as real locations.

## Read-only guarantee

The target is opened with `O_RDONLY` and mapped with `PROT_READ | MAP_PRIVATE`. The checker never requests write access to the target and never truncates, renames, chmods, repairs or rewrites filesystem metadata. Automated acceptance compares SHA-256 before and after checks of both valid and deliberately corrupted images.

Validator workspace is caller-owned. Workspace sizes are derived only from decoded counts that have first been bounded by the public BoringFS v0 maxima; malformed out-of-range counts never drive unbounded allocation.

## Acceptance

The focused host acceptance uses the real Milestone 19 `mkboringfs` formatter to create at least two valid geometries, then checks the resulting files with `boringfsck`. Disposable copies are corrupted at documented byte offsets to exercise real shared-validator results including bad magic, unsupported version/features, bad layout, bad bitmap, root-object corruption and allocation leaks. Truncated images and host-I/O failures are covered separately.

## Non-goals

Milestone 20 does not implement repair, `--fix`, `--repair`, automatic metadata reconstruction, a kernel BoringFS backend, a BoringFS mount, block devices, VirtIO, persistent root filesystems, new syscalls, an FD layer, executable loading from BoringFS, networking, display/input or BoringWM integration.
