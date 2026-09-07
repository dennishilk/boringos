# BoringKernel VFS core

Milestone 14 introduces a small BoringOS-native virtual filesystem core. The VFS gives the kernel filesystem-independent names, paths, mounts, working directories, open handles and backend dispatch without implementing a storage filesystem.

The implementation is freestanding C and architecture-neutral. The VFS core does not perform COM1 I/O, page-table manipulation or x86_64-specific work.

## Object model

The core separates five concepts:

```text
vfs_filesystem
    ↓ owns a backend operation table and root node
vfs_mount
    ↓ places one filesystem into the global VFS tree
vfs_path
    ↓ resolved identity = mount + node
vfs_node
    ↓ filesystem-independent object identity/type
vfs_handle
    ↓ retained resolved object + byte offset + access intent
```

A `vfs_filesystem` is one filesystem instance. It owns a backend operation table and one directory root. The VFS does not assume whether that instance is memory-backed, disk-backed, BoringFS, or any other future implementation.

A `vfs_node` carries a stable backend-provided identity, a directory-or-regular-file type, its owning filesystem, a parent relation for the current no-hard-link model, opaque backend context, and VFS reference bookkeeping. Backend-returned nodes are validated before the VFS adopts them.

A `vfs_path` is not a string. It is a resolved `(mount, node)` pair. This lets the kernel distinguish identical backend node identities reached through different mount relationships and gives process working directories a concrete filesystem-independent origin.

A `vfs_handle` is a kernel-internal caller-owned object. It retains a resolved path, a current byte offset and minimal read/write access intent. It is **not** a numeric file descriptor and there is no per-process descriptor table in Milestone 14.

## Compile-time bounds

Milestone 14 deliberately uses finite bootstrap limits:

```text
VFS_PATH_MAX   1024 bytes
VFS_NAME_MAX    255 bytes
VFS_IO_MAX     4096 bytes per handle read/write request
VFS_MOUNT_MAX     8 mount slots including the global root
```

Inputs exceeding these bounds are rejected; they are never silently truncated.

Path parsing is iterative. No recursive path walker is used, so hostile component depth cannot cause unbounded kernel recursion.

## Path resolution

`vfs_resolve()` supports:

- global `/`;
- absolute paths;
- relative paths from an already resolved VFS path;
- repeated `/` separators;
- `.`;
- `..`;
- multiple ordinary path components;
- traversal into child mounts.

An empty path is explicitly rejected.

For a named component the current object must be a directory. The VFS invokes the owning filesystem's `lookup` callback and then validates the returned node before retaining it. A backend cannot make the VFS accept a foreign-filesystem node, invalid type, invalid identity, self-reference, or child with the wrong parent relationship.

There are no symbolic links in Milestone 14, so there is no symlink expansion or symlink-loop policy. Hard-link semantics are also not implemented.

## Root and `..`

The global VFS root is explicit.

At global `/`, resolving `..` remains at `/`; path walking cannot escape above the global root.

The root node of each filesystem has itself as its filesystem-local parent. Mount-boundary traversal is handled by the VFS rather than delegated to backend directory contents.

When walking downward onto a node covered by a child mount, the resolved path changes from the parent `(mount, mountpoint)` to `(child_mount, child_filesystem_root)`.

When resolving `..` from a child filesystem root, the VFS crosses back through the mount relationship to the parent directory of the mountpoint. This prevents a walker from becoming trapped inside the child filesystem root.

Mount namespaces are not implemented.

## Mount model

The mount table is small and bounded. Slot zero is the one global root mount; the remaining slots can hold child mounts.

`vfs_mount_filesystem()` requires:

- an initialized VFS;
- a valid directory target;
- a valid filesystem with a directory root;
- a target that is not a filesystem root under the current bootstrap policy;
- a filesystem that is not already mounted anywhere;
- a target that is not already covered by another child mount;
- a free bounded mount slot.

A filesystem instance may be mounted at most once. Combined with rejection of self/same-filesystem mounts and root targets, this prevents self-referential and cyclic mount relationships in the Milestone 14 model.

There are no mount syscalls, mount commands, block devices, filesystem discovery, partition parsing or persistent mount configuration.

## Process working directories

The existing process object now has:

```c
struct vfs_path cwd;
bool cwd_valid;
```

The cwd is therefore a retained resolved VFS reference, not unchecked path text.

PID 0/bootstrap remains valid before a VFS exists and may have no cwd. Once VFS state exists, a live process can receive a directory cwd through the kernel-internal process API. Relative process lookup uses that retained `(mount, node)` path as its origin.

Different processes can retain different cwd values. The VFS acceptance proves that resolving the same relative name from two different process cwd values reaches two different nodes.

Process destruction releases its cwd reference. There are no `chdir` or `getcwd` syscalls and no userspace cwd API in Milestone 14.

## Backend operation contract

The filesystem-independent operation table is deliberately close to the roadmap contract:

```text
lookup
create
mkdir
unlink
rmdir
rename
read
write
truncate
readdir
```

The VFS validates arguments and dispatches to the owning filesystem backend. A missing callback produces a BoringOS-native `VFS_RESULT_NOT_SUPPORTED` result rather than an invalid indirect call.

Create/mkdir results are validated before the VFS exposes them as paths. `readdir` results are checked for valid node identity/type, bounded non-empty names, termination, and invalid `/`, `.` or `..` entries.

Rename is limited to one filesystem/mount. Cross-filesystem rename returns `VFS_RESULT_CROSS_FILESYSTEM`; the VFS does not invent copy-and-delete semantics.

The VFS uses its own internal `enum vfs_result`. This is not a POSIX errno contract.

## Open handles

`vfs_handle_open()` accepts a resolved regular-file path and minimal read/write access intent. The handle retains the VFS path and starts at byte offset zero.

Read/write requests:

- require a valid open handle;
- enforce the requested access intent;
- reject requests above `VFS_IO_MAX`;
- reject offset + request-length integer overflow before backend dispatch;
- reject missing backend callbacks;
- reject a backend that reports more transferred bytes than requested;
- advance the handle offset only after a successful backend operation.

`vfs_handle_close()` releases the retained path and invalidates the handle.

These objects are kernel-internal VFS handles. They are not `open(2)` state, do not have integer descriptor numbers, and do not imply stdin/stdout/stderr, `dup`, pipes or sockets.

## Lifetime and cleanup

External resolved paths increment VFS path-reference bookkeeping and retain their node. Open handles retain a path. Process cwd values retain a path. Mounts retain their child filesystem root and their parent mountpoint.

VFS shutdown is rejected while external paths or handles remain. On clean shutdown mount-owned references are released in reverse mount-table order.

The Milestone 14 acceptance destroys its test processes, closes handles, releases every path, shuts down VFS state, checks every synthetic node reference count, and verifies PMM/heap bookkeeping is restored.

## Test-only backend

`kernel/core/vfs_test.c` is compiled only for `TEST_MODE=vfs`. It supplies a deliberately synthetic backend for acceptance.

Its directory tree and node identities are statically declared. Lookup returns only predefined nodes. Mutation callbacks (`create`, `mkdir`, `unlink`, `rmdir`, `rename`, `truncate`) increment counters and return deterministic sentinel results; they do not create a general mutable directory tree. Read returns fixed sentinel bytes. Write records call metadata and reports a deterministic transfer length; it does not retain written contents. `readdir` returns one fixed deterministic entry.

Therefore the harness is not a hidden RAM filesystem: there is no arbitrary file allocation, no general in-memory file-content store, no dynamically growing directory state, and no production filesystem backend.

The QEMU gate proves path semantics, independent process cwd values, mount entry/exit, mount validation, every operation-table dispatch, cross-filesystem rename rejection, handle offset behavior, invalid backend metadata/callback handling, reference cleanup and PMM/heap restoration.

## Explicit non-goals

Milestone 14 does **not** implement:

- RAMFS;
- BoringFS;
- block devices or block I/O;
- partitions or persistent storage;
- real VFS-backed file storage;
- numeric file descriptors or per-process fd tables;
- stdin, stdout or stderr;
- file-related userspace syscalls (`OPEN`, `CLOSE`, `READ`, `WRITE`, `CHDIR`, `GETCWD`, `STAT`, `READDIR`, `MOUNT`);
- POSIX compatibility;
- symbolic links;
- mount namespaces;
- shell or init;
- networking;
- framebuffer/display;
- keyboard/mouse input;
- APIC migration or SMP.

The existing early serial-console syscalls remain the userspace I/O path while the VFS is still kernel-internal infrastructure.
