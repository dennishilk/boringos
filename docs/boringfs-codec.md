# BoringFS v0 codec and structural validator

Milestone 18 implements only the BoringFS v0 **format layer** described by `docs/boringfs.md`. The design specification remains the source of truth for magic bytes, offsets, record sizes, limits, allocation rules, directory semantics, ownership and corruption policy.

M18 does **not** make BoringFS mountable. It adds no formatter CLI, checker CLI, block device, kernel filesystem backend, persistent storage, file-content syscall or executable loading from BoringFS.

## Production layout

```text
libs/boringfs/
    include/boring/boringfs.h
    codec.c
    validate.c
```

The production core consumes caller-provided memory only. It contains no file I/O, POSIX calls, `FILE *`, `malloc`/`free`, block-device API, BoringKernel dependency, VFS dependency or RAMFS dependency.

## Codec API

The public header exposes decoded in-memory value types for:

- the v0 superblock;
- 8-byte extents;
- 128-byte object records;
- 256-byte directory records.

Each record has an explicit encode/decode function operating on `uint8_t *` / `const uint8_t *` buffers with caller-supplied lengths.

All multi-byte fields are loaded and stored explicitly as little-endian bytes. The codec never casts disk bytes to native structs, never uses packed structs, never performs an unaligned integer dereference and never depends on host alignment or endianness.

Superblock encoding deterministically zeros bytes 64..4095. Object encoding zeros reserved fields and unused extents. Directory-record encoding zeros reserved bytes and the unused tail of the 240-byte name field.

## Validator API

```c
enum boringfs_validation_result boringfs_validate_volume(
    const uint8_t *volume,
    size_t volume_size,
    const struct boringfs_validation_workspace *workspace,
    struct boringfs_validation_error *error_out);
```

The input volume is read-only and is never repaired or normalized.

Validation uses caller-owned scratch:

```text
block_owner_count >= superblock.total_blocks
    uint32_t entries

object_reference_count_count >= superblock.object_count
    uint8_t entries
```

At the v0 maxima this is 4,194,304 bytes for block ownership plus 16,384 bytes for object reference counts. The validator allocates no hidden memory and places only small decoded records / 256-byte directory-record buffers on its stack.

## Validation result model

The result enum distinguishes API errors, truncation, bad magic, unsupported version/features, bad superblock/layout/bitmap/object/extent/directory metadata, extent overlap, invalid UTF-8, duplicate names, orphan/multiple references, parent mismatch, directory cycles, allocation leaks and insufficient workspace.

`struct boringfs_validation_error` additionally carries deterministic numeric context where available:

- `object_id`;
- filesystem `block`;
- logical `directory_record_index`.

No raw pointer is exposed.

## Validation phases

The validator performs bounded, overflow-safe phases:

1. decode and validate the exact v0 superblock and zero-reserved bytes;
2. recompute the deterministic bitmap/object-table/data layout;
3. verify declared volume size and workspace bounds;
4. validate metadata and out-of-range bitmap bits;
5. validate every object record and every live extent;
6. prove extent allocation and exclusive block ownership;
7. validate parent targets and prove parent chains reach root without cycles;
8. read directory records through each directory's **logical extent stream**, including non-contiguous physical extents;
9. validate names, UTF-8, type hints, duplicate names and directory target ownership;
10. require exactly one directory reference for every allocated non-root object;
11. reject allocated-but-unowned data blocks as allocation leaks.

Arithmetic that combines block numbers, counts, byte sizes and offsets is checked before use. No validator rule relies on unsigned wraparound.

## UTF-8

The BoringOS-owned UTF-8 checker accepts valid 1/2/3/4-byte sequences and rejects bad continuations, truncation, overlong encodings, UTF-16 surrogate code points and code points above U+10FFFF.

It performs no Unicode normalization and no case folding. Filename identity remains valid UTF-8 bytes compared case-sensitively and byte-for-byte, exactly as specified by BoringFS v0.

## Host acceptance

`tests/boringfs-host-test.sh` builds the reusable core with strong warnings, audits its undefined symbols and source dependencies, runs normal host tests, then rebuilds/runs under AddressSanitizer and UndefinedBehaviorSanitizer.

The C test contains independent golden byte vectors rather than relying only on encoder/decoder round trips. It also contains:

- a minimal valid empty in-memory volume;
- a valid non-trivial volume with root directory, child directory, regular file, real extents and a root logical directory stream spanning two non-adjacent physical extents;
- a systematic corruption matrix for the documented v0 structural rules;
- deterministic prefix truncation testing for every shorter prefix of the minimal valid volume;
- proof that validation leaves input bytes unchanged.

The fixture builder is test-only. It is not `mkboringfs` and exposes no formatting CLI.

## Explicit non-goals

Milestone 18 does not implement:

- `mkboringfs`;
- a `boringfsck` executable or repair;
- kernel BoringFS mount/read/write support;
- a BoringFS VFS backend;
- block devices, VirtIO block, AHCI, NVMe or partitions;
- persistent root storage;
- loading executables from BoringFS;
- file descriptors or new syscalls;
- shell expansion;
- Milestone 19 or later work.
