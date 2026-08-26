# BoringOS native boring-display foundation (M34)

M34 introduces the first BoringOS-native Ring-3 display service. During semantic implementation the kernel remains `BoringKernel 0.0.34-dev`.

## Architectural boundary

The physical Limine framebuffer remains kernel-owned. The kernel exposes only narrow presentation primitives; it does not own surfaces, windows, stacking policy, a cursor, or compositing policy.

`boring-display` is a real freestanding Ring-3 service installed as `/bin/boring-display` and registered through the M33 service registry as `boring.display`.

M34 reuses existing milestones rather than creating parallel mechanisms:

- M30: validated kernel-owned framebuffer and pixel packing primitives;
- M31: exclusive blocking native input ownership and PS/2 keyboard/mouse events;
- M32: kernel-owned shared byte buffers with process-local capability handles and alias mappings;
- M33: named services, full-duplex FIFO IPC, blocking accept/receive, process-local IPC handles, and transactional M32 capability grants.

Future BoringWM is explicitly outside M34. Window placement, focus policy, tiling, keybindings and workspaces remain M35-or-later policy.

## Kernel presentation primitive

M34 reserves the next free syscall slots without changing 0..36:

```text
37 BUFFER_INFO
38 FRAMEBUFFER_CLAIM
39 FRAMEBUFFER_PRESENT
40 FRAMEBUFFER_RELEASE
```

`BUFFER_INFO` is a generic read-only M32 extension that returns the authoritative byte size for one caller-owned shared-buffer capability. It exists so a receiver can validate metadata against the actual granted object rather than trusting a peer-declared size.

`FRAMEBUFFER_CLAIM` is exclusive and process-local. It returns bounded scanout information to the caller. `FRAMEBUFFER_PRESENT` accepts a canonical userspace XRGB8888 frame and copies/converts it into the already validated kernel-owned physical framebuffer. Userspace never receives the physical framebuffer address or an HHDM alias. `FRAMEBUFFER_RELEASE` relinquishes ownership. Process teardown releases a forgotten claim.

The exported source layout is deliberately simple and fixed for M34: one full-screen XRGB8888 image, 4 bytes per pixel, stride `width * 4`. All width/height/stride/byte calculations are overflow checked before copying.

These syscalls are primitive device facilities only; the compositor and cursor remain in Ring 3.

## Display protocol v1

Service name:

```text
boring.display
```

Protocol messages are fixed-size, versioned and bounded. M34 defines:

- `CREATE_SURFACE`: width, height, stride, declared byte size and format; exactly one M32 buffer capability must be attached;
- `COMMIT`: publish new bytes already written through the shared mapping;
- `DESTROY_SURFACE`: release one client-owned surface;
- bounded replies containing status and the service-issued surface token.

The only M34 pixel format is XRGB8888. Dimensions must be non-zero and fit inside the current scanout. Stride must equal `width * 4`; `stride * height` must not overflow. The declared byte count must equal that exact layout and the authoritative `BUFFER_INFO` size of the granted M32 object.

A surface token is service-issued and generation protected. Every operation is checked against the accepting IPC connection that owns that token; guessing another client's token is not authority.

## Bounded service state

M34 uses fixed tables:

- at most 8 connected display clients;
- at most 16 live surfaces;
- at most one attached M32 backing object per surface;
- no unbounded userspace-controlled allocation.

A surface owns the receiver-local granted M32 handle and one mapping for as long as the surface is live. Destroy, peer close or client death unmaps and closes that capability.

The permanent bundle gate uses the real built ELF sizes rather than assuming a milestone-sized expansion. The complete inherited bundle cannot fit in the historical 80-block M32 geometry because the M33 IPC program crosses that boundary, while all eight programs including the three M34 display ELFs fit and verify byte-for-byte in the existing 96-block M33 geometry. M34 therefore retains 96 blocks instead of forcing an unnecessary 112-block image.

## Composition

`boring-display` owns a userspace composition buffer sized from the validated scanout geometry. Every presentation is generated in CPU software:

1. clear to a deterministic dark desktop background;
2. composite live client surfaces in deterministic creation order, oldest at the bottom;
3. draw the software mouse cursor last;
4. call the kernel framebuffer-present primitive.

Creation order is only a deterministic presentation primitive for M34 acceptance; it is not BoringWM placement/focus/tiling policy.

A client surface is shared memory. After initial creation the client may modify pixels through its original M32 mapping and send only `COMMIT`; the service must observe those same backing bytes through its granted alias. No second copy/grant is required.

## Input and cursor

`boring-display` claims the existing M31 userspace input stream with the existing input syscalls. It does not read controller ports or bypass the M31 queue.

Mouse relative-motion events update a real software cursor in Ring 3. Cursor coordinates are clipped to scanout bounds. The cursor is drawn by the compositor after all surfaces and is therefore never a kernel cursor.

The QEMU acceptance uses real QMP mouse injection through the existing PS/2 path and proves that the visible cursor moves to the clipped expected location.

## Lifecycle

Peer close or client process exit removes all surfaces belonging to that IPC connection and releases their mappings/handles. The service itself may deliberately exit with live service/input/framebuffer claims in the teardown acceptance; established process-exit cleanup must unregister the M33 service, release M31 input ownership, release the M34 framebuffer claim, close IPC state and reclaim M32 resources.

## Acceptance

Permanent M34 acceptance must prove at minimum:

- three distinct real Ring-3 processes/address spaces: `/bin/boring-display`, display client A and display client B;
- registration of `boring.display` and two real M33 connections;
- two shared-buffer-backed client surfaces using M32 capability grants;
- same-backing alias visibility after a client pixel update followed by `COMMIT`;
- malformed/overflowing metadata rejection without leaking state;
- cross-client surface-token authority isolation;
- deterministic stacking and dark desktop background;
- software CPU compositing into a kernel-presented framebuffer;
- real M31 mouse input delivered to Ring 3;
- real QMP mouse injection, cursor motion and clipping;
- client-death surface cleanup;
- display-service teardown cleanup and resource recovery;
- host-side protocol/compositor tests;
- a real CPL3 QEMU acceptance and deterministic framebuffer/reference artifact;
- all permanent M0..M33 gates remain green;
- BoringFS/qemu-bundle contains `/bin/boring-display` and both dedicated client programs while preserving historical fixture geometries.

M34 does not implement BoringWM, tiling, focus policy, workspaces, Super keybindings, a terminal, X11, Wayland, GPU acceleration, USB input or a generic graphics-driver ABI.
