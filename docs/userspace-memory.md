# BoringOS userspace memory and shared buffers

Milestone 32 is complete. It adds a bounded dynamic data-memory foundation for real BoringOS Ring-3 processes without changing ELF code-page permissions, BoringFS semantics, or the existing syscall entry mechanism.

## Dynamic anonymous Ring-3 memory

`MEMORY_ALLOC` reserves page-rounded virtual space from the per-process user-memory arena, allocates PMM-owned frames, explicitly zero-fills every new 4-KiB frame, and maps the pages user-accessible, writable and non-executable through the existing Ring-3 mapper. `MEMORY_FREE` accepts only the exact base of a live anonymous allocation, removes the mappings and returns the owned frames.

The implementation is deliberately bounded: each process has 32 anonymous-allocation metadata slots and an individual allocation is limited to 16 MiB. Allocation failure rolls back partial mappings and frames rather than publishing partial state.

## Minimal native userspace heap

The freestanding BoringOS runtime now provides `boring_malloc`, `boring_calloc` and `boring_free` on top of `MEMORY_ALLOC`. The heap uses 16-byte payload alignment, first-fit reuse, block splitting and same-arena coalescing. It grows in page-rounded arenas with a 16-KiB minimum arena size. This is intentionally a small native runtime allocator, not a libc or POSIX allocator contract.

## Generic kernel-owned shared buffers

M32 also adds generic kernel-owned shared byte-buffer objects. Buffer backing frames are allocated by the kernel, zero-filled, and remain plain bytes: there is no width, height, stride, pixel format, framebuffer, surface, cursor, display-client, compositor, window or BoringWM meaning attached to them.

A creating process receives a process-local capability handle. Handles contain a bounded slot identity plus generation state and are decoded only against that process's handle table. Each process has 32 handle slots and 32 mapping slots; the kernel has a fixed 64-object buffer table. A single buffer is limited to 64 MiB.

`BUFFER_MAP` may be called multiple times for the same live handle. Each call maps the same backing frames at a distinct process-local virtual range, so the mappings are real aliases of the same bytes. The mappings are writable and non-executable.

Handle lifetime and mapping lifetime are intentionally separate. Closing a handle invalidates that capability and advances its generation, but existing mappings continue to keep the backing object alive. Unmapping drops a mapping reference. Backing storage is released only when no handle or mapping reference remains.

There is **no cross-process handle or buffer transfer in M32**. M32 provides the kernel-owned object and process-local capability/mapping foundation only; cross-process capability transfer belongs to Milestone 33.

## Process-exit reclamation

Process teardown releases all live anonymous allocations, then all live shared-buffer mappings, then all remaining buffer handles. The cleanup path verifies the process-local memory state is empty and allows unreferenced kernel buffer objects to release their backing frames. This makes forgotten M32 resources reclaimable at process exit.

## ABI

The provisional syscall ABI extends the frozen 0-24 surface with exactly:

```text
25 MEMORY_ALLOC
26 MEMORY_FREE
27 BUFFER_CREATE
28 BUFFER_MAP
29 BUFFER_UNMAP
30 BUFFER_CLOSE
```

The ABI remains provisional and BoringOS-specific.

## Acceptance and compatibility

The standalone static `/bin/memory-test` exercises the real Ring-3 wrappers and proves anonymous allocation/zero-fill, write/read behavior, buffer creation, multiple mappings with alias visibility, handle-vs-mapping lifetime, negative cases, explicit cleanup and process-exit reclamation. Host coverage also exercises the bounded kernel memory model and the native runtime heap.

Permanent CI includes the memory-test ELF audit, M32 host tests and the real userspace-memory/shared-buffer QEMU acceptance while retaining the complete historical suite. The historical 64-block BoringFS fixture geometry remains compatible; the larger fixture is selected only when `/bin/memory-test` must be seeded, so older fixture-dependent acceptance paths remain unchanged.

Semantic Freeze evidence:

```text
implementation SHA: a494a800818f14bb375bee5674dcd27d8bfb82ee
implementation tree: 4dd1b01c40d49a8447fd7fc52a047b7a40383d9e
exact-head Semantic Freeze CI: Run #338 / 32959154779 / SUCCESS
final version after closeout: BoringKernel 0.0.33-dev
```

Milestone 32 does not add IPC, cross-process capability transfer, a service registry, display/surface semantics, cursor work, `boring-display`, a compositor, BoringWM or terminal work. Milestone 33 is not part of M32.
