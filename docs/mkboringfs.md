# mkboringfs

`mkboringfs` is the Milestone 19 host-side formatter for BoringFS v0. It creates a new deterministic filesystem image containing the v0 superblock, allocation bitmap, fixed object table, an allocated empty root directory (object ID 1), and a zeroed data region.

It is intentionally a host tool. It does not mount BoringFS and adds no kernel filesystem backend, block-device support, persistence, filesystem repair, file descriptor layer, or syscall.

## Build

```sh
make mkboringfs
```

The executable is written to:

```text
build/mkboringfs
```

## Usage

```sh
build/mkboringfs --blocks <count> --objects <count> <image>
```

Both geometry inputs are explicit decimal integers.

- `--blocks` must be `1..1,048,576`, and the requested geometry must leave enough blocks for the v0 metadata layout.
- `--objects` must be `64..16,384`.
- filesystem blocks are always 4096 bytes.

Example:

```sh
build/mkboringfs --blocks 256 --objects 4096 build/boringfs-empty.img
```

For that geometry the v0 layout is:

```text
superblock            block 0
bitmap_start          1
bitmap_blocks         1
object_table_start    2
object_table_blocks   128
data_start            130
root object ID        1
```

## Format path

The formatter does not define a second disk format. It populates decoded BoringFS values and uses the shared Milestone 18 codec to encode the superblock and root object. Bitmap bits are initialized directly according to the documented v0 allocation rules. Every other object-table byte and every data byte begins as zero.

Before publishing the output path, `mkboringfs` runs the shared Milestone 18 `boringfs_validate_volume()` structural validator over the completed byte image with caller-owned workspace. A rejected image is not reported or renamed into place as a successful format.

The output is first built in a temporary file beside the requested path and is atomically renamed only after encoding, validation and flush succeed. Failed formatting removes the temporary output.

## Determinism

BoringFS v0 has no random UUID or on-disk timestamp. Identical explicit `--blocks` and `--objects` inputs therefore produce identical filesystem bytes. The formatter writes no process ID, host pathname, random value, environment value, or uninitialized padding into the filesystem image.

`make mkboringfs-test` proves this property with two independently formatted images, byte comparison and SHA-256 equality. The same acceptance also performs independent raw-byte checks of the superblock, bitmap, root object, unused object table and data region, then validates the real file through the shared M18 validator.

## Non-goals

Milestone 19 does not provide `boringfsck`, repair, a kernel BoringFS backend or mount, block devices, VirtIO/AHCI/NVMe, persistent root storage, executable loading from BoringFS, file-content syscalls, or writable kernel BoringFS support.
