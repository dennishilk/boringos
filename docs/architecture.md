# BoringOS architecture — Phase 0.1

These decisions are intentionally narrow and **provisional**. They exist only to make the first BoringKernel boot measurable.

- **Initial architecture:** x86_64.
- **Reference machine:** QEMU `q35`.
- **Bootloader:** Limine 12.5.2, used only to load and transfer control to BoringKernel.
- **Kernel format:** freestanding, statically linked ELF64 x86_64 (`kernel.elf`).
- **Kernel entry path:** firmware → Limine → ELF entry symbol `boring_kernel_entry` → BoringKernel C code.
- **C/assembly boundary:** kernel logic is C. No assembly source file exists in this milestone. Tiny x86_64 inline assembly is isolated to I/O-port access (`inb`/`outb`) and the controlled `cli`/`hlt` stop loop because those instructions cannot be expressed in ISO C.
- **Serial console:** legacy COM1 at I/O base `0x3f8`, configured for 115200 baud, 8 data bits, no parity, one stop bit; transmit is polled.
- **Privilege level:** the kernel currently executes at x86_64 CPL 0 (ring 0), as handed off by Limine. There is no ring-3/userspace execution yet.

Not implemented: allocator, virtual-memory management owned by BoringKernel, GDT redesign, IDT, interrupts, timer, scheduler, threads, processes, syscalls, userspace, filesystem, input, graphics, BoringWM, networking, USB, audio or GPU acceleration.

This phase does not decide the future syscall ABI, process model, VFS, networking model or desktop architecture.
