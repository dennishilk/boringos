# BoringOS RAMFS

## Purpose

Milestone 15 adds the first **real mutable filesystem backend** underneath the filesystem-independent VFS delivered in Milestone 14.

RAMFS exists to prove that the existing VFS boundary can drive real namespace and file-data state before BoringOS adds block I/O or an on-disk filesystem. It is intentionally small, volatile bootstrap infrastructure.

```text
kernel callers / acceptance
        |
        v
       VFS
        |
        v
filesystem-independent vfs_operations
        |
        v
      RAMFS
        |
        v
real mutable heap-backed RAM state
```

The VFS does not know RAMFS internals. RAMFS uses `vfs_filesystem.backend_context` and `vfs_node.backend_context` to retain backend ownership while exposing ordinary `vfs_filesystem` and `vfs_node` objects.

RAMFS is **not BoringFS** and is not persistent. A reboot destroys all RAMFS contents.

## Filesystem-instance model

`ramfs_create_filesystem()` creates one heap-backed RAMFS instance containing:

- one embedded `vfs_filesystem`;
- one bounded embedded RAMFS node pool;
- one real VFS root directory;
- a monotonically increasing node-ID source;
- backend ownership state.

A bounded internal registry validates filesystem and backend ownership before callbacks dereference RAMFS state. Up to `RAMFS_MAX_FILESYSTEMS` live instances may exist during this bootstrap stage.

The current compile-time bounds are:

```text
RAMFS_MAX_FILESYSTEMS = 8
RAMFS_MAX_NODES       = 32 per filesystem, including root
RAMFS_FILE_MAX        = 8192 bytes per regular file
RAMFS_TOTAL_DATA_MAX  = 32768 bytes of allocated file capacity per filesystem
```

These are explicit bootstrap contracts, not promises of future filesystem limits. Exceeding a configured storage bound returns `VFS_RESULT_NO_SPACE`; data and names are never silently truncated.

## Node and directory model

Each occupied RAMFS slot embeds one stable `vfs_node` plus backend-owned state:

- owner RAMFS instance;
- exact retained name;
- parent relationship;
- node type;
- regular-file size/capacity/data where applicable;
- occupied/live state.

The root is a directory and follows the VFS root contract (`root->parent == root`). Non-root membership is represented by the same parent relationship exposed through `vfs_node.parent`. Directory lookup and enumeration scan the bounded live node pool rather than using recursive traversal.

RAMFS stores neither `.` nor `..` entries. Those remain generic VFS path-walking semantics.

Names are case-sensitive and retained exactly. RAMFS independently rejects empty names, names over `VFS_NAME_MAX`, `.`, `..`, embedded slash and embedded NUL inside the supplied component. Duplicate names in one directory are rejected with `VFS_RESULT_ALREADY_EXISTS` regardless of file/directory type.

Every live node has a non-zero monotonically increasing logical ID. Freed pool slots may be reused only with a new node ID and only after VFS reference bookkeeping shows the old node is no longer retained. Pointer values are not the logical node-ID contract.

## Real file-data storage

Regular-file contents are actual bytes allocated from the existing BoringKernel heap. RAMFS does not add another general-purpose allocator.

When an operation needs more capacity and the existing allocation is too small, RAMFS:

1. validates offset/size arithmetic and configured bounds;
2. allocates replacement storage first;
3. zeroes replacement storage;
4. copies retained old bytes;
5. applies the requested mutation;
6. swaps the live allocation only after preparation succeeds;
7. frees the old allocation.

A failed growth attempt leaves the old logical file contents and size intact. Reads never expose uninitialized heap bytes.

## Write semantics

Writes are real mutations of RAMFS data.

- writes at the current handle offset store the supplied bytes;
- ordinary VFS handle writes advance the existing VFS handle offset;
- overwrites replace bytes inside the current file;
- extension grows logical EOF;
- a write beginning beyond EOF creates a deterministic zero-filled gap;
- a zero-length backend write succeeds without changing state;
- offset/length overflow is rejected;
- writes beyond `RAMFS_FILE_MAX` or the total instance capacity fail with `VFS_RESULT_NO_SPACE`.

The main acceptance round trip writes a nontrivial binary pattern through `vfs_handle_write()`, closes/reopens through the normal VFS handle API, reads through `vfs_handle_read()`, and compares every byte.

## Read semantics

Reads return only stored live file bytes.

- reads may be partial;
- a non-zero offset is honored by the backend;
- a read at or beyond EOF succeeds with zero bytes transferred;
- no read transfers more than requested;
- the generic VFS handle path retains its `VFS_IO_MAX` bound.

## Truncate semantics

`truncate` operates only on regular files.

- shrinking lowers logical EOF;
- bytes beyond the new EOF stop being readable;
- growing exposes zero-filled bytes;
- truncating to zero makes the file empty and frees its backing allocation;
- requests over `RAMFS_FILE_MAX` fail with `VFS_RESULT_NO_SPACE`;
- failed growth preserves the previous contents and logical size.

## Directory enumeration

`readdir` enumerates the current **live** child membership of a directory. It does not emit fixed sentinel entries, tombstoned nodes, `.` or `..`.

Ordering is deterministic bootstrap **node-slot order**. The supplied index selects the Nth live child in that order. An index after the final live child returns `VFS_RESULT_NOT_FOUND`.

Because enumeration reads live membership, create/rename/unlink changes are immediately visible to later `vfs_readdir_path()` calls.

## Rename and move policy

M15 implements rename/move only within one RAMFS filesystem. Generic VFS continues to reject cross-filesystem rename with `VFS_RESULT_CROSS_FILESYSTEM`; RAMFS does not implement copy-and-delete.

Destination replacement is deliberately not implemented. If a distinct destination object already exists, rename returns `VFS_RESULT_ALREADY_EXISTS` and leaves both objects unchanged.

Successful rename/move changes the live node name and/or parent while preserving the same VFS node identity and regular-file contents. Directory descendants therefore survive a directory rename or move.

A directory may not be moved into itself or any descendant. RAMFS checks the destination-parent chain iteratively with a bound of `RAMFS_MAX_NODES`; encountering the source rejects the operation, while an impossible/cyclic parent chain is treated as corruption.

## Unlink lifetime policy

M15 deliberately does **not** implement Unix-style unlinked-but-open orphan files.

A regular file may be unlinked only when its `vfs_node.reference_count` is zero. A retained `vfs_path`, open `vfs_handle`, process cwd or mount relationship therefore keeps the object alive. Removal of a retained node returns `VFS_RESULT_BUSY`.

Once the final external VFS reference is released, unlink removes the object from namespace state, releases file backing data and invalidates the slot. A later lookup returns `VFS_RESULT_NOT_FOUND`, and the old name may be reused.

## rmdir policy

`rmdir` accepts only directories. A non-empty directory returns `VFS_RESULT_NOT_EMPTY`; RAMFS never recursively deletes it. An empty directory that is still externally retained returns `VFS_RESULT_BUSY`. The root cannot be removed through ordinary directory membership because it is not a child entry.

## Mount and cwd integration

RAMFS contains no mount-traversal special cases. The Milestone 14 VFS owns all mount relationships.

Acceptance creates two production RAMFS instances, mounts the second at `/mnt`, resolves a real child file through `/mnt/child-file`, verifies the established `/mnt/..` behavior, and confirms cross-filesystem rename is still rejected by generic VFS.

Process-relative acceptance uses the existing retained kernel-internal cwd support. Two processes receive different real RAMFS cwd directories containing same-named files with different bytes; `vfs_resolve_process()` plus ordinary VFS reads must return the distinct content.

No `chdir` or `getcwd` syscall is added.

## Cleanup and destruction

`ramfs_destroy_filesystem()` refuses destruction while any live RAMFS node has retained VFS references. Mounted filesystems therefore remain protected by the root/mountpoint references held by VFS.

After all handles, paths and process cwd references are released and `vfs_shutdown()` releases mount relationships, RAMFS destruction:

- frees all remaining regular-file backing allocations;
- invalidates every embedded node;
- invalidates the embedded filesystem;
- removes the instance from the bounded registry;
- frees the RAMFS instance allocation.

Acceptance compares `heap_get_stats()` before RAMFS creation and after final destruction. `used_bytes` and `allocation_count` must return to their prior values. The test deliberately does not require `mapped_pages` to shrink because the existing kernel heap keeps grown pages mapped after `kfree`.

## VFS result additions

M15 appends two BoringOS-native result values needed by real mutable directory semantics:

- `VFS_RESULT_ALREADY_EXISTS`;
- `VFS_RESULT_NOT_EMPTY`.

Existing result values are not renumbered. This is not a POSIX `errno` ABI.

## Acceptance mode

M15 uses independent:

```text
TEST_MODE=ramfs
BORING_TEST_MODE_RAMFS=8
```

Mode 8 instantiates the real production RAMFS backend. It does not reuse the synthetic Milestone 14 VFS backend and does not depend on the syscall-test exception hooks used only by modes 4 through 6.

The host QEMU gate requires focused markers for real create/mkdir/lookup, binary write/read round trips, overwrite, zero-gap extension, truncate, live readdir state, rename/move, cycle rejection, BUSY lifetime behavior, process cwd isolation, two-filesystem mount traversal, bounded failure and complete cleanup.

## Explicit non-goals

Milestone 15 does not add:

- BoringFS or any on-disk filesystem format;
- block devices, VirtIO block, AHCI, ATA, partitions or persistent storage;
- file descriptors or stdin/stdout/stderr;
- file-related userspace syscalls;
- POSIX compatibility or `errno`;
- dynamic linking or file-backed ELF loading;
- boring-init, boring-shell or shell commands;
- pipes, sockets, TTY/line discipline or networking;
- framebuffer/input/display/BoringWM work;
- SMP or APIC migration.

RAMFS is deliberately the smallest real storage backend needed to prove that the already-landed VFS operates on genuine mutable filesystem state.
