# Scheduler-started process stack (M36)

SYS43 uses the same bounded startup contract for every executable; no program
name or ELF size selects a different stack. Historical boot-module launch
fixtures retain their one-page stack. The generic ELF loader accepts one or two
whole pages, maps **every** page user/writable/NX, zeroes them, and owns each frame
until unload. Partial allocation/mapping failure unloads all acquired pages.

## Layout and limits

- Mapped stack: `[0x40010000, 0x40012000)` (8192 bytes), growing downward.
- Lower guard: `[0x4000f000, 0x40010000)` is unmapped; ELF PT_LOAD overlap with
  the guard or stack is rejected. There is no demand growth or guard recovery.
- `top` is exclusive. `RSP = top - align_up((argc + 1) * 8 + string_bytes, 16)`.
- At `_start`, RSP is 16-byte aligned, RDI carries argc, and RSI points at argv.
  argc is not pushed on the stack. argv starts at initial RSP, ends in a NULL
  pointer, and is followed by copied NUL-terminated argument strings. Their
  storage belongs to the child and lasts until process teardown.
- `_start` calls `boring_main`: the return-address push means its entry RSP is
  8 modulo 16, as required by the x86-64 C ABI. All userspace uses `-mno-red-zone`.
- Arguments are bounded at 16 entries and 1024 aggregate bytes including NULs.
  The largest aligned startup block is 1168 bytes, leaving **7024 bytes** below
  initial RSP. The required runtime reserve is 4096 bytes for one maximum FD
  I/O buffer plus 2048 bytes for entry, calls, and other bounded locals. Two
  pages are the smallest page-rounded allocation satisfying these bounds.
- Invalid count is EINVAL, excessive string bytes are ENAMETOOLONG, invalid
  user pointers are EFAULT. A startup-layout/copy failure publishes no task and
  returns EIO after full child-image/FD/process rollback. No unbounded allocation
  is attempted. Exceeding the runtime stack bound faults; it is not auto-grown.

## Fault and regression

The original terminal `boring_main` inlined a 4096-byte PTY read buffer. Its
prologue pushed four registers, subtracted 0x1000, and probed the stack at
RIP `0x40002791`; with the original top `0x40011000`, the probe touched
`0x4000ffa8`, below the only mapped page. Another 0x1a8 bytes of locals followed.
This is stack consumption, not an ELF image/stack overlap: its PT_LOAD memory
ended below `0x40008000`. Smaller programs had sub-page frames.

`spawn-stack-host-test` exhausts all permitted count/byte combinations and
rejects the old single-page layout. The inherited ELF QEMU gate additionally
checks two zeroed RW/NX pages, the guard, size rejection and complete unmapping.
The real SPAWN Ring3 child has a volatile 4224-byte entry frame, checks its data
again after blocking/wakeup, and exercises the real loader and scheduler. The
unmodified graphical launch path remains a separate acceptance gate.
