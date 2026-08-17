# BoringFS v0 design

BoringFS is the planned native filesystem for BoringOS. This document defines the first deliberately small on-disk format and the architectural boundaries around it. It is a **design specification**, not a claim that BoringFS is implemented today.

> boring is not a bug.

The v0 goal is an understandable, auditable filesystem that can support ordinary files and directories without importing another operating system's filesystem implementation. BoringFS is part of BoringOS and is intended to remain MIT-licensed.

## Status

Current BoringKernel is still a boot-only kernel. It has no allocator, VFS, block-device layer, storage driver, userspace, shell, or filesystem. Persistent BoringFS therefore comes later; the immediate implementation work remains kernel foundations.

BoringFS v0 intentionally excludes journaling, copy-on-write, snapshots, compression, encryption, ACLs, extended attributes, deduplication, quotas, sophisticated sparse files, online resizing, and automatic repair.

## Design summary

BoringFS v0 uses:

- little-endian on-disk integers;
- a fixed **4096-byte filesystem block**;
- one superblock;
- one block-allocation bitmap;
- a fixed-size object table chosen at format time;
- fixed-size object records;
- up to eight extents per object;
- directories stored as arrays of fixed-size directory records;
- UTF-8, case-sensitive filenames;
- no hard links and no symbolic links in v0;
- no `.` or `..` records on disk;
- one parent object for every allocated object except that the root points to itself;
- strict validation before a volume may be mounted writable.

The simplicity is intentional. Some choices are conservative and may change in a future incompatible BoringFS format version after real usage teaches us more.

## On-disk conventions

All offsets in this document are **byte offsets within their containing record** unless explicitly described as filesystem block numbers.

All multi-byte integers are unsigned little-endian values. Code must encode and decode fields explicitly. Kernel and host tools must not write compiler-native C structs directly to disk.

Reserved bytes are written as zero. A v0 reader should reject metadata with reserved fields that are required to be zero; this makes accidental format drift visible.

A BoringFS volume begins at block 0 of the block-device slice supplied to the filesystem. Partition-table handling is outside BoringFS.

## Filesystem block size

The BoringFS v0 filesystem block size is exactly **4096 bytes**.

The superblock stores `block_shift = 12` as a consistency check. BoringFS v0 rejects any other value.

The generic block-device layer may expose a different device logical block size. BoringFS may use a device only when:

- the device logical block size is non-zero;
- the BoringFS volume start is correctly aligned; and
- `4096 % device_logical_block_size == 0`.

This keeps the filesystem format independent of a particular QEMU, VirtIO, NVMe, or AHCI implementation.

## Volume layout

The v0 layout is contiguous and deterministic:

```text
filesystem block 0
    superblock

filesystem blocks 1 .. bitmap end
    allocation bitmap

next blocks
    object table

remaining blocks
    data region
```

The formulas are:

```text
bitmap_start       = 1
bitmap_blocks      = ceil(total_blocks / 32768)
object_table_start = bitmap_start + bitmap_blocks
object_table_blocks = ceil(object_count * 128 / 4096)
data_start         = object_table_start + object_table_blocks
```

One 4096-byte bitmap block represents 32768 filesystem blocks.

All blocks before `data_start` are metadata blocks and must be marked allocated in the allocation bitmap.

BoringFS v0 has no backup superblock. Losing block 0 is therefore a fatal v0 corruption. Redundant superblocks can be considered only in a later format revision if experience justifies the complexity.

## Superblock

The superblock occupies filesystem block 0. Only the first 128 bytes are defined by v0; bytes 128 through 4095 must be zero when formatted.

| Offset | Size | Field | v0 value / meaning |
|---:|---:|---|---|
| 0 | 8 | `magic` | ASCII `BORINGFS` |
| 8 | 2 | `format_major` | `0` |
| 10 | 2 | `format_minor` | `1` |
| 12 | 2 | `header_size` | `128` |
| 14 | 1 | `block_shift` | `12` |
| 15 | 1 | `flags` | `0` |
| 16 | 4 | `total_blocks` | total filesystem blocks |
| 20 | 4 | `bitmap_start` | `1` |
| 24 | 4 | `bitmap_blocks` | allocation-bitmap length |
| 28 | 4 | `object_table_start` | first object-table block |
| 32 | 4 | `object_table_blocks` | object-table length |
| 36 | 4 | `object_count` | number of object slots |
| 40 | 4 | `root_object_id` | `1` |
| 44 | 4 | `data_start` | first data block |
| 48 | 2 | `object_record_size` | `128` |
| 50 | 2 | `directory_record_size` | `256` |
| 52 | 4 | `feature_compat` | `0` in v0 |
| 56 | 4 | `feature_ro_compat` | `0` in v0 |
| 60 | 4 | `feature_incompat` | `0` in v0 |
| 64 | 64 | reserved | all zero |

The magic is exactly eight bytes and is not NUL-terminated.

A v0 implementation accepts only `format_major = 0`, `format_minor = 1`, all feature masks zero, and the exact record sizes above. Unknown versions or features are rejected rather than guessed.

## Allocation bitmap

The allocation bitmap contains one bit for every filesystem block.

For filesystem block `n`:

```text
byte_index = n / 8
bit_index  = n % 8
```

Bit value:

- `0` = free;
- `1` = allocated or reserved.

Bit 0 is the least-significant bit of its containing byte.

All bits representing blocks outside `total_blocks` in the final bitmap byte/block must be set to 1 by the formatter so an allocator can never treat them as real free storage.

Metadata blocks are permanently allocated while the filesystem is mounted. Every data block referenced by an object extent must have its bitmap bit set. A data-region allocation bit with no metadata owner is an allocation leak and is treated as structural corruption by v0 validation rather than silently reused.

The initial allocator should use predictable first-fit allocation. When possible it should allocate contiguous runs and merge them into an extent. Newly allocated blocks must be zeroed before they become visible through a file or directory.

## Object table

The object table contains fixed 128-byte records. Object IDs are one-based:

```text
object ID 1 -> object-table slot 0
object ID 2 -> object-table slot 1
...
```

Object ID 0 is always invalid and is used as an empty/null value.

The root directory is always object ID 1.

### Object record

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 1 | `state` | `0` free, `1` allocated |
| 1 | 1 | `type` | `0` none, `1` regular file, `2` directory |
| 2 | 2 | `flags` | `0` in v0 |
| 4 | 4 | `object_id` | must match this table slot's one-based ID |
| 8 | 4 | `parent_object_id` | containing directory; root uses `1` |
| 12 | 2 | `extent_count` | `0..8` |
| 14 | 2 | reserved | zero |
| 16 | 8 | `size_bytes` | logical byte length |
| 24 | 64 | `extents[8]` | eight 8-byte extent records |
| 88 | 40 | reserved | all zero |

A free object record must be entirely zero.

For an allocated object:

- `state` is 1;
- `type` is regular file or directory;
- `object_id` matches the slot;
- `parent_object_id` names an allocated directory object;
- the root object is a directory and has `parent_object_id = 1`;
- `flags` is zero;
- unused extent records are zero;
- `size_bytes` does not exceed allocated extent capacity.

BoringFS v0 has no hard links. Every allocated non-root object must be referenced by exactly one live directory entry, and the containing directory must match `parent_object_id`.

Timestamps are deliberately omitted from v0. A future format may add them through a compatible extension or a new format revision. Until then, `touch` can truthfully create an empty file but cannot promise POSIX timestamp semantics.

## Extent record

Each extent is exactly 8 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `start_block` | first physical filesystem block |
| 4 | 4 | `block_count` | number of contiguous blocks |

Extent-array order is logical file order. Physical extents do not need to be adjacent.

Valid extents must:

- have non-zero `block_count`;
- begin at or after `data_start`;
- remain strictly below `total_blocks` after length is applied;
- not overlap any other live extent or metadata block;
- correspond only to allocated bitmap bits.

Each object has at most eight extents in v0. If a file cannot grow without requiring a ninth extent, the operation fails cleanly even if fragmented free space still exists. This limitation is deliberate and should be visible in documentation and tests.

Truncation may release complete trailing blocks and extents. Bytes beyond the logical file end in the last allocated block should be zeroed when practical so deterministic images and deleted-data behavior are easier to reason about.

## Directories

A directory's contents live in ordinary data extents owned by a directory object. The logical directory byte stream is an array of fixed 256-byte records.

`size_bytes` for a directory must be a multiple of 256 and may not exceed its extent capacity.

### Directory record

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `object_id` | target object; 0 means unused slot |
| 4 | 2 | `name_length` | UTF-8 byte length, `1..240` for a live entry |
| 6 | 1 | `type_hint` | `1` file, `2` directory |
| 7 | 1 | `flags` | `0` in v0 |
| 8 | 240 | `name` | filename bytes, not NUL-terminated |
| 248 | 8 | reserved | all zero |

An unused directory record must be entirely zero.

For a live record:

- `object_id` is in range and refers to an allocated object;
- `type_hint` matches the target object's type, but object metadata remains authoritative;
- `name_length` is between 1 and 240;
- bytes after `name_length` in the 240-byte name field are zero;
- the name is valid UTF-8;
- the name contains no NUL byte and no `/` byte;
- the name is neither `.` nor `..`;
- `flags` and reserved bytes are zero;
- no other live entry in the same directory has the same filename bytes.

Directory deletion creates an all-zero reusable slot. Implementations may trim all-zero trailing records and release trailing directory blocks, but compaction of interior holes is not required for v0.

BoringFS does not store `.` or `..`. VFS resolves `.` itself and resolves `..` using the current directory object's `parent_object_id`. Root's parent is root.

## Filename and path semantics

BoringFS v0 filenames are valid UTF-8 byte strings with a maximum encoded length of **240 bytes**.

Comparison is:

- case-sensitive;
- byte-for-byte;
- without Unicode normalization.

This means visually equivalent Unicode spellings can be distinct names. That is acceptable for v0 because implicit normalization would add hidden policy and complexity. A future user-facing normalization policy can be considered separately from the on-disk identity rule.

`/` is the path separator at the VFS layer and cannot occur inside a BoringFS filename.

Paths themselves are a VFS concept rather than an on-disk BoringFS record type.

## BoringFS v0 limits

These limits are intentionally conservative:

| Limit | v0 value |
|---|---:|
| Filesystem block size | 4096 bytes |
| Maximum volume size | 1,048,576 blocks = 4 GiB |
| Maximum object slots | 16,384 |
| Reserved object ID | 0 |
| Root object ID | 1 |
| Maximum filename | 240 UTF-8 bytes |
| Maximum extents per object | 8 |
| Directory record size | 256 bytes |
| Object record size | 128 bytes |
| Hard links | not supported |
| Symbolic links | not supported |

`object_count` is chosen at format time and must be between 64 and 16,384 inclusive. A host formatter may later choose 4096 as a practical default, but the value is stored explicitly and is not hard-coded into readers.

The absolute regular-file logical-size limit is:

```text
min((total_blocks - data_start) * 4096, 4,294,967,295 bytes)
```

The eight-extent limit can make the practical maximum smaller on a fragmented volume. A directory can never contain more live children than the filesystem has non-root object slots, so the absolute per-directory live-entry limit is at most 16,383.

No v0 limit should be silently exceeded. Creation or growth must fail with an explicit error.

## Formatting behavior

A future `mkboringfs` should create the same bytes for the same explicit inputs.

At minimum it will:

1. validate requested image size and object count;
2. zero the complete image or target region;
3. write the superblock;
4. initialize the allocation bitmap;
5. mark metadata blocks allocated;
6. create object ID 1 as the empty root directory;
7. leave all other object records free and zero;
8. validate the completed image using the same structural validator used by tests before reporting success.

No random volume UUID is part of v0, which helps deterministic-format tests. A later version can add one if there is a demonstrated need.

## Corruption and mount policy

Filesystem code must treat every disk byte as untrusted.

Before a BoringFS v0 volume becomes writable, validation must establish at least:

### Superblock

- exact magic;
- supported exact version;
- supported block and record sizes;
- zero unsupported flags/features;
- sane `total_blocks` and `object_count`;
- layout arithmetic without integer overflow;
- bitmap, object table, and data region ordered and contained within the volume;
- root object ID exactly 1.

### Allocation metadata

- bitmap itself lies entirely inside the metadata area;
- all metadata blocks are marked allocated;
- no out-of-range block is considered free;
- every live extent maps only to allocated data blocks;
- no two live extents overlap;
- no unexplained allocated data block remains after a complete metadata scan.

### Object metadata

- IDs are in range and self-consistent;
- state/type combinations are legal;
- parent IDs refer to directories;
- extent counts are in range;
- extent arithmetic does not overflow;
- file and directory sizes fit allocated storage;
- root is a directory whose parent is itself;
- every non-root allocated object has exactly one directory reference;
- directory parent relationships are consistent and do not create cycles.

### Directory metadata

- record sizes and directory lengths are valid;
- entry object IDs are in range;
- names are valid according to v0 rules;
- no duplicate live name exists in one directory;
- type hints agree with target objects;
- empty slots are fully zero.

On structural corruption, the initial BoringFS driver should **refuse the mount** and must not write to the volume. The first `boringfsck` should be an inspector/validator, not an aggressive repair engine. Reporting precise corruption is preferred to guessing how to repair it.

Integer operations that combine block numbers, counts, offsets, and byte lengths must be overflow-checked before use.

## Compatibility and versioning

BoringFS owns its on-disk format. Compatibility therefore needs to be explicit from the beginning.

For v0:

- only major 0 / minor 1 is accepted;
- all feature masks must be zero;
- unknown values are rejected.

Future policy:

- a changed `format_major` means incompatible layout or semantics;
- a newer minor version may be accepted only if the older implementation can prove that the changes are backward-compatible;
- `feature_incompat` bits require explicit implementation support or the mount is rejected;
- `feature_ro_compat` can eventually allow read-only mounting when understood policy permits it;
- `feature_compat` may describe optional features that old implementations can safely ignore.

No implementation should silently reinterpret fields based on guesses.

## VFS boundary

BoringFS should be one backend behind a BoringKernel VFS. The VFS, not the BoringFS driver, owns generic path walking, process working directories, mount points, open-file handles, and syscall-facing semantics.

A first internal VFS operations set should conceptually cover:

```text
lookup(parent, name)
create(parent, name)
mkdir(parent, name)
unlink(parent, name)
rmdir(parent, name)
rename(old_parent, old_name, new_parent, new_name)
read(node, offset, buffer, length)
write(node, offset, buffer, length)
truncate(node, length)
readdir(node, cursor, entry)
```

Exact C signatures are deliberately not frozen here because the process model, locking model, and syscall ABI do not exist yet.

The VFS should expose generic node types such as regular file and directory. Filesystem-specific object IDs remain private to a backend where possible.

A process's current working directory should be held as a VFS object/reference, not merely an unchecked string. `pwd` can reconstruct a path from VFS parent/name relationships when needed.

## RAMFS should come first

**Recommendation: yes. RAMFS should precede persistent BoringFS in the kernel.**

This separates two difficult problem sets:

```text
path semantics + VFS + userspace syscalls
```

from:

```text
block I/O + storage driver + persistent format + corruption handling
```

A tiny RAMFS can prove real behavior for:

- root directory lookup;
- nested directories;
- file creation;
- read/write;
- enumeration;
- unlink/rmdir;
- rename/move;
- process working directories;
- shell commands such as `pwd`, `ls`, `cd`, `mkdir`, `touch`, `cat`, `echo`, `rm`, `rmdir`, `mv`, and `cp`.

RAMFS is not a fake filesystem: it is a real volatile VFS backend whose state changes through the same VFS operations the persistent filesystem will later implement. Its limitation is persistence, not semantics.

The real `boring-shell` must still be a userspace program. Temporary kernel-console commands may exist only as debugging aids and must not be presented as the shell architecture.

## Generic block-device boundary

Persistent BoringFS must not know about VirtIO, NVMe, AHCI, or a specific QEMU controller.

A future generic block device should expose information equivalent to:

```text
logical_block_size
logical_block_count
writable
read(device_lba, count, destination)
write(device_lba, count, source)
flush()
```

The exact C type and asynchronous model are deferred until interrupts and scheduler semantics exist.

A partition/slice wrapper can expose a bounded subsection of a physical or virtual disk as another block device. BoringFS then receives a volume whose LBA 0 corresponds to BoringFS filesystem block 0.

The initial QEMU persistent-storage path should prefer **VirtIO block** unless implementation work uncovers a concrete blocker. Later NVMe or AHCI support should plug into the same generic block interface rather than changing BoringFS.

## Host-side tooling

BoringFS should have host-side C tooling independent of the running kernel:

### `mkboringfs`

Initial responsibility:

- format an image deterministically;
- create a valid empty root filesystem;
- optionally add files only after the base formatter is proven;
- run structural validation before success.

### `boringfsck`

Initial responsibility:

- open an image read-only;
- decode every field explicitly;
- validate superblock/layout/bitmap/object/directories;
- report exact failures;
- return non-zero on corruption.

The first version should not repair images automatically.

Where practical, encoding/decoding and validation logic should live in small pure-C format modules usable by host tests and the kernel without importing host libc assumptions into kernel builds. Host I/O adapters and kernel block-device adapters should remain separate.

## Testing strategy

Host-side tests should cover format logic before kernel persistence work depends on it.

Required cases include:

- deterministic empty filesystem image;
- valid root directory;
- nested directories;
- empty files;
- files spanning filesystem-block boundaries;
- multiple files;
- deletion and allocation reuse;
- directory deletion;
- rename and move;
- full-volume behavior;
- ninth-extent failure behavior;
- invalid magic;
- unsupported version or feature bits;
- out-of-range block pointers;
- overlapping extents;
- malformed allocation bitmap;
- malformed or duplicate directory entries;
- invalid UTF-8 filenames;
- truncated image;
- object-parent cycles;
- leaked allocated blocks;
- deterministic repeated formatter output.

QEMU integration tests should later verify the kernel path separately:

```text
storage driver
  -> generic block device
  -> BoringFS validation/mount
  -> VFS
  -> syscalls
  -> userspace command
```

A successful host-side parser test is not a substitute for a real QEMU mount test, and a successful QEMU boot is not a substitute for corruption tests.

## Expected persistent-storage path

The intended incremental path is:

```text
kernel foundations
    ↓
VFS
    ↓
RAMFS
    ↓
real userspace shell filesystem semantics
    ↓
host BoringFS formatter + validator
    ↓
generic block-device layer
    ↓
QEMU VirtIO-block driver
    ↓
read-only BoringFS kernel mount
    ↓
validated BoringFS writes
    ↓
persistent BoringFS root
```

Read-only mounting should precede kernel-side mutation. A host-created image gives the kernel driver known-good metadata to parse before allocator and write-order bugs can corrupt it.

## Shell relationship

BoringFS does not define shell commands. VFS and syscalls provide filesystem semantics; `boring-shell` uses them from userspace.

A sensible order for real shell behavior is:

1. `help`, `version` — userspace-only information;
2. `pwd`, `ls` — read-only VFS semantics;
3. `cd` — process working-directory semantics;
4. `mkdir`, `touch` — first mutations;
5. `cat` — file open/read;
6. `echo` with redirection — file create/open/write/truncate semantics;
7. `rm`, `rmdir` — deletion rules;
8. `mv` — VFS rename/move;
9. `cp` — userspace read/write loop, not a special filesystem primitive;
10. `pause` — terminal/input behavior, not a filesystem operation;
11. `clear` — terminal control behavior, not blank-line simulation;
12. `reboot`, `shutdown` — privileged system-control path, not filesystem operations.

The first interactive shell may use a serial TTY while the native graphical terminal does not yet exist. That is legitimate if it is explicitly described as an early console; it must still be a real userspace `boring-shell` running on BoringKernel rather than a host shell or kernel-mode imitation.

## Non-goals for BoringFS v0

Do not add in v0:

- journaling;
- copy-on-write;
- snapshots;
- compression;
- encryption;
- ACLs;
- extended attributes;
- deduplication;
- quotas;
- hard links;
- symbolic links;
- sparse-file optimization;
- online resizing;
- automatic repair;
- filesystem-level networking semantics.

If later requirements justify one of these, it should arrive as an explicit new milestone with format-compatibility analysis.

## Provenance rule

BoringFS is independently designed for BoringOS. Public filesystem concepts and specifications may be studied, but implementation code must not be copied from Linux, BSD, ext filesystems, ZFS, Btrfs, FAT implementations, or other operating-system filesystem code.

Any external algorithm or code introduced later must record its source, exact license, and reason for inclusion. BoringFS itself should remain MIT-compatible.
