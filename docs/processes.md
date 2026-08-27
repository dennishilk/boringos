# BoringKernel processes and independent address spaces

This document began with the **BoringKernel 0.0.9-dev** process/address-space
milestone. The original CPL0-only proof is retained below as historical design
context; later milestones added real userspace processes and M27 adds the
bounded shell-child lifecycle described here.

## M27 userspace process and shell-session lifecycle

The bounded process table now records a monotonic PID, parent PID, address
space, retained VFS CWD, canonical CWD text, process name, single-user name,
state and slot ownership. PID 0 is the bootstrap kernel, PID 1 is
`boring-init`, and every launched `boring-shell` is a real child of PID 1 in
its own address space.

The public snapshot state is derived from real table state:

```text
current alive process     -> RUNNING
other alive process       -> WAITING
finished unreaped process -> ZOMBIE
```

`SYS_EXIT` is limited to the currently suspended launch child. It activates
the saved parent, unloads the child's ELF mappings, marks the child finished
and non-runnable, preserves the signed exit status, and restores PID 1's
saved syscall return frame. The process object, CWD and address space remain
owned by the zombie until the parent reaps it.

`WAITPID` is likewise deliberately narrow: the caller must be the exact
saved parent and the requested PID must be that parent's exact exited child.
It copies the preserved status to checked userspace memory, destroys the
inactive child address space, releases the retained CWD, clears the process
slot and closes the suspended-launch record. PID 1 then launches the next
shell. Repeated acceptance proves that an old zombie disappears from `ps`
before the replacement shell is used and that the historical four-slot table
does not exhaust. M35 raises ordinary process capacity to eight, separate from
PID 0, for the display/WM/client session.

M36 keeps that eight-process/eight-task bound. Its minimum proven dual-terminal
session uses display, WM, two terminals and two shells (six ordinary processes);
one foreground command temporarily raises the live count to seven. `SPAWN`
creates independently rooted scheduler-owned children with explicit stdio and
the generic guarded two-page startup stack. Terminal and shell exit paths close
PTY/FD/IPC/input/framebuffer/M32 ownership before slots are reused; M36 does not
add PID 1 desktop supervision, process groups, sessions or signals.

Slots are reusable only after successful reap. PID numbers remain
monotonically allocated and are not reused in M27; there are no PID
namespaces, process groups, signals, `fork`, general `exec`, asynchronous
waiting or multiple concurrent launch children.

The username `boring` is real kernel-owned process identity metadata used by
`whoami`, the prompt and system information. It does not create credentials,
authentication, authorization or a Unix permission model. Accordingly,
`logout` means terminating the shell session through the same real lifecycle
as `exit`, not logging out of an authenticated session.

---

## Original 0.0.9 task and process proof

### Task and process are different objects

The current model keeps two concepts separate:

```text
task
→ execution context / scheduling entity

process
→ identity + address-space ownership
```

A task now holds a pointer to its owning `struct process`. The bootstrap and existing cooperative/preemptive regression tasks belong to PID 0. The process-isolation acceptance creates one preemptive task for PID 1 and one for PID 2.

At that original milestone there was no thread abstraction, parent/child
hierarchy, signal model, file-descriptor table, credential model, session,
process group, zombie state, or `waitpid`. The M27 extension above supersedes
the parent/child, zombie and `waitpid` parts of that historical boundary.

## Minimal process object

```c
struct process {
    uint64_t pid;
    struct address_space address_space;
    enum process_state state;
    bool slot_used;
};
```

States are intentionally limited to:

```text
PROCESS_ALIVE
PROCESS_FINISHED
```

Process metadata is currently stored in a bounded static table. Process address-space page tables come from PMM.

## PID policy

PID allocation is deterministic and monotonic for this bootstrap stage:

```text
PID 0 → bootstrap/kernel process
PID 1 → first test process
PID 2 → second test process
```

PIDs are not reused during the acceptance test. There are no PID namespaces or complex allocation structures.

## Bootstrap process

`process_init()` wraps the already active inherited Limine/BoringKernel page-table root as the address space of PID 0.

PID 0 therefore owns the **identity** of the bootstrap address space but does not own or free the inherited root page tables. The bootstrap address space is marked specially and cannot be destroyed by the ordinary address-space destruction path.

The current active root is validated against the root already adopted by the existing VMM before the bootstrap process becomes online.

## Address-space object

The current explicit structure is:

```c
struct address_space {
    uint64_t root_physical;
    uint64_t owned_table_frames[16];
    uint64_t owned_table_count;
    bool bootstrap;
    bool initialized;
};
```

The architecture layer provides explicit operations to:

```text
create
activate
map page
translate page
audit shared kernel mappings
unmap page
destroy
```

These operations take an explicit address-space object. Lower-half mappings are therefore not implicitly applied to whichever CR3 happens to be active.

## Root ownership

A non-bootstrap process receives a new zeroed PML4 frame from the existing PMM. Any new lower-level PDPT/PD/PT frames created for that process also come from PMM and are recorded in `owned_table_frames`.

Only frames recorded in that list may be reclaimed by the process address-space layer.

The following are **not** added to a process-owned list and are therefore never freed as process page-table ownership:

- the bootstrap/Limine root;
- inherited/shared higher-half page-table structures;
- VMM-owned shared kernel page tables;
- page tables belonging to another process.

The process address space must be inactive before destruction. Its process-owned lower-half mappings must first be removed. Destruction then requires the only remaining owned page-table frame to be that process's own root PML4.

## Shared kernel mappings

The current verified x86_64 layout makes the process split simple and explicit:

```text
PML4 slots   0–255   process-private lower half
PML4 slots 256–511   shared kernel higher half
```

A newly allocated process PML4 starts zeroed. Slots 256–511 are then copied from the bootstrap PML4. These entries continue to point at the existing shared higher-half page-table structures; the mapped kernel physical memory is **not duplicated**.

The implementation also explicitly verifies the important currently used entries:

```text
slot 256 → HHDM
slot 510 → VMM test / kernel heap region
slot 511 → linked higher-half kernel image
```

Sharing the whole currently used higher half preserves the mappings needed by the executing kernel, including kernel code/data, HHDM-based physical access, heap mappings, task stacks, PMM/VMM metadata, IDT/IRQ/exception/scheduler state, and still-referenced Limine data reached through those mappings.

The acceptance test proves this at runtime by switching into each process root, calling shared kernel code, reading heap metadata, and emitting serial output before touching the process-private test mapping.

This is a bootstrap sharing policy, not a finalized userspace/kernel virtual-address ABI.

## Process-private test range

The centrally defined isolation address is:

```text
0x0000004000000000
```

It is canonical, 4096-byte aligned, and lies in lower-half PML4 slot 0. Newly created process roots start with that lower half empty.

The test allocates two distinct PMM data frames and maps:

```text
PID 1:
0x0000004000000000 → frame A

PID 2:
0x0000004000000000 → frame B
```

with `frame A != frame B`.

The mappings are present/writable supervisor mappings. This milestone does not create a full future user-page permission policy.

## CR3 activation

`address_space_activate()` validates the target address-space object, root ownership where applicable, and shared kernel mappings. It reads current CR3 and does nothing if the requested root is already active.

When the root differs, the x86_64 architecture layer executes a real CR3 load:

```text
mov <new root>, %cr3
```

and verifies the active root afterward. Loading CR3 provides the broad TLB invalidation required by this bootstrap design. There is no PCID support.

The address-space layer tracks actual root changes separately from task scheduler counters.

## Scheduler integration

Every kernel task now carries an owning process pointer. When the scheduler selects a task, it activates that task's process address space before returning the selected interrupt frame to IRQ assembly.

For timer preemption the ordering is:

```text
real PIT IRQ0 on current task stack
    ↓
complete interrupt frame saved
    ↓
task_scheduler_tick()
    ↓
select next task
    ↓
process_activate(next->process)
    ↓
real CR3 switch if roots differ
    ↓
return selected restore-frame pointer to irq.c
    ↓
PIC EOI while still on the current shared kernel IRQ stack
    ↓
assembly loads selected frame into RSP
    ↓
restore GPRs + iretq
```

The CR3 load therefore happens while executing shared kernel code/stack mappings, and the previously proven EOI-before-stack-switch rule is preserved.

Existing PID-0 cooperative and preemptive regression tasks normally request the already active bootstrap root, so they do not cause unnecessary CR3 reloads.

## Real same-VA isolation proof

The acceptance test first verifies isolation manually with real CPU dereferences.

After activating PID 1 it performs:

```text
*(uint64_t *)0x0000004000000000 = 0xAAAAAAAAAAAAAAAA
```

After activating PID 2 it performs:

```text
*(uint64_t *)0x0000004000000000 = 0xBBBBBBBBBBBBBBBB
```

It then switches back to each root and directly rereads the same virtual address:

```text
PID 1 → 0xAAAAAAAAAAAAAAAA
PID 2 → 0xBBBBBBBBBBBBBBBB
```

The test does not emulate isolation by translating the VA and dereferencing the HHDM physical address. The process test VA itself is accessed after real CR3 activation.

## PIT-preemptive address-space proof

The stronger test binds:

```text
Task A → PID 1 → address space A
Task B → PID 2 → address space B
```

Both remain ordinary CPL0 kernel tasks. Neither calls `task_yield()`.

Each task repeatedly dereferences the **same** test VA. Task A accepts only pattern A and Task B accepts only pattern B. The ordinary PIT scheduler repeatedly switches the task and process address space together.

The verified clean-source QEMU run reported:

```text
Process A root:           0x0000000000078000
Process B root:           0x0000000000079000
Process A physical frame: 0x000000000007A000
Process B physical frame: 0x000000000007B000

Address-space switches:   18
Preemptive CR3 switches:   7
Process A slices:           3
Process B slices:           3
```

All required checks passed:

```text
process-create
unique-pid
address-space-create
distinct-root
same-va-different-pa
cr3-switch
kernel-mappings
process-a-isolation
process-b-isolation
preemptive-address-space-switch
bootstrap-return
address-space-cleanup
pmm-bookkeeping
```

The existing independent preemption regression remained green with seven PIT-driven preemptions and full integer GPR preservation.

## Return and cleanup

After both process-owned tasks finish, the scheduler restores the saved PID-0 bootstrap interrupt frame and bootstrap CR3.

Cleanup runs only after the CPU is back in the bootstrap address space:

1. validate and free the two finished task stacks;
2. mark PID 1 and PID 2 finished;
3. unmap the process-private test VA in each address space;
4. reclaim empty process-owned PT/PD/PDPT frames;
5. free the two test data frames back to PMM;
6. destroy each inactive process root PML4;
7. verify PID 0/root is still active;
8. verify PMM/heap/VMM bookkeeping, accounting only for legitimate retained kernel-heap growth.

Shared higher-half page tables are never reclaimed by process destruction.

## What 0.0.9-dev proves

BoringKernel now proves that it can maintain distinct process identities with independent x86_64 page-table roots, map the same lower-half VA to different physical frames, switch those roots with real CR3 loads, and couple CR3 switching to real PIT-preemptive kernel-task scheduling while preserving shared kernel execution.

## What it does not prove

All process test tasks still execute in **CPL0**.

This milestone does not implement:

- ring 3 / user mode;
- user CS/SS or TSS privilege-stack switching;
- syscall/sysret or an interrupt syscall ABI;
- userspace runtime or ELF loading;
- user-memory copy/validation policy;
- fork, exec, wait, signals, or copy-on-write;
- file descriptors, VFS, RAMFS, BoringFS, or storage;
- demand paging, swap, or PCID;
- FPU/SIMD process state;
- SMP or per-CPU process/scheduler state.

The next execution-boundary milestone is a separately scoped **ring-3 transition**. No ring-3 or syscall implementation is part of this process/address-space milestone.
