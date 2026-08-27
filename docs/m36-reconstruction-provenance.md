# M36 reconstruction provenance

The M36 foundation was recovered after loss of the original local reconstruction workspace using two explicit provenance classes.

- **Exact recovered:** `kernel/core/fd.c` is the previously verified Git blob `2beb870ddcbbd6e09bc4c04da4cab550ba4e243d`. Other already-published M36 source whose complete Git object was retained remains exact recovered.
- **Semantically reconstructed after workspace loss:** `Makefile`, `kernel/core/task.c`, `kernel/core/m36_syscall.c`, `kernel/core/syscall.c`, and `user/boring-shell/main.c` were reconstructed from the documented M36 contract because the original complete bytes were no longer provable.

The semantically reconstructed blobs at this checkpoint are:

- `Makefile`: `300617b187da7d1bd97fbcf7140a245f9d9fdf06`
- `kernel/core/task.c`: `36f04d5c04b478916ce36bd4a57e002500a12c48`
- `kernel/core/m36_syscall.c`: `412bc0615108255d77b23620ae3ef1ec3074b0eb`
- `kernel/core/syscall.c`: `5f86891013dea6caf61686e246de6395e529b0ac`
- `user/boring-shell/main.c`: `097ce8ece116f7bcffeccf8204f033f7a5737a99`

This checkpoint does **not** claim byte identity with lost commit `79e1fedcee2be57691d9b5576e633a54b2941f0c`. Its validity is established by the retained ABI contract, freestanding target builds, targeted host tests, real QEMU acceptance, historical regressions, and later permanent CI. The final M36 Semantic Freeze, not the lost intermediate commit, becomes the immutable source of truth.
