# Branch archive before main-only cleanup (2026-09-28)

The following remote branch tips were inspected against `main` at
`9ae499fee915933ae7395257c4364654ce381e62`. A verified Git bundle named
`boringos-branches-2026-09-28.bundle` preserves these refs and their objects;
its SHA256 is
`a058f634f38ce39110f67233b980ec7741df6f834538478200fbbca633cc236f`.
The repository owner retains the bundle separately. The bundle was verified
with `git bundle verify`; it is not part of the source repository.

| Historical branch | Tip | Already in main? |
| --- | --- | --- |
| `archive/m66-original-physical-good-5f189097` | `5f189097d872fad9ed0da267021b6b238841ec98` | No |
| `archive/m66-physical-bisect-52fcffd5` | `4836db39916f7cbfe0bb200e7d16bfa9c31765ad` | No |
| `feature/m67-m69-input-display-polish` | `31e28e4916b6cbecc1f27f15ca2e86a4fbc66308` | No |
| `feature/m67-typematic` | `7f97d6f00d5378db171eb23b0e2f7e6fd91f5b4e` | No; draft PR #95 was not physically accepted |
| `freeze/m61-physical-desktop-2026-09-04` | `1e3c0e83e8e9159480782a6be624975ccbe0da3a` | Yes |
| `freeze/m62-dynamic-capacity-physical-2026-09-05` | `f8b23490cd2e8e9095f6623d9d8b6230d3111080` | Yes |
| `freeze/m63-system-power-lifecycle-physical-2026-09-05` | `799d1e6529b8eafead37acc340f3fd18dbb2d655` | Yes |
| `freeze/m66-physical-hid-recovery-2026-09-08` | `bd3f181d24547189b004328aea54c54fd194fc96` | Yes |
| `freeze/m66-usb-hub-mouse-physical-2026-09-06` | `8ccd618dfc4e8163821de552a3c912bab4e6f36a` | Yes; historical failure witness, not the accepted HID baseline |
| `freeze/m68-1080p-physical-2026-09-16` | `7a9594508bc02b6a5c8d5d13c2ed5c858c9cb54e` | Yes |
| `freeze/mouse-present-latency-physical-2026-09-10` | `c5ded88e3d473162b94f963d9509394246ef782c` | Yes |

The four "No" rows contain commits outside `main`. They were preserved in the
bundle for later inspection, not merged into the production runtime. To
inspect or recover a tip from a copy of the bundle, run
`git bundle list-heads boringos-branches-2026-09-28.bundle` and fetch its
`refs/remotes/origin/<historical-branch>` ref into a local recovery branch.
