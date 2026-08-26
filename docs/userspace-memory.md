# BoringOS userspace memory and shared buffers

Milestone 32 is in progress on `agent/userspace-memory-shared-buffers` from the exact verified Milestone 31 base `39c016a20f8629bef7d43e58c0a35e26720b3368`.

This milestone is limited to bounded anonymous Ring-3 data memory, a minimal native userspace heap, and generic kernel-owned shared byte buffers. Existing syscall numbers 0 through 24 remain frozen; the exact M31 base was inspected and slots 25 through 30 are free for the planned M32 ABI.

The existing Ring-3 mapper already provides explicit user/writable/executable permissions and NX validation. M32 will reuse that mechanism for data mappings rather than weakening ELF code-page permissions.

Shared buffers are generic byte storage only. They carry no width, height, stride, pixel format, framebuffer, window, display-client, cursor, compositor, or BoringWM semantics.

Cross-process capability transfer, generic IPC, service registry, boring-display, cursor rendering, compositor work, BoringWM, terminal/PTY work, GUI applications, framebuffer mmap, demand paging, copy-on-write, swap, memory-mapped files, fork, ASLR, executable writable memory, PMM >4 GiB scalability work, and Milestone 33 are explicitly deferred.

The final virtual arena, metadata bounds, limits, lifetime rules, syscall contracts, runtime allocator details, zero-fill guarantees, process-exit cleanup, and acceptance evidence will be recorded here only after the corresponding implementation has been verified.
