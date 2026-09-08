# M66 physical provenance correction and bisect status

Status date: 2026-09-08

This note is the authoritative correction to the previously misattributed M66
physical freeze. It records image-level evidence, not merely source labels.
The existing `freeze/m66-usb-hub-mouse-physical-2026-09-06` ref is not moved;
its association with physical success is superseded by this record.

## Corrected physical anchors

### Physical last good

```text
commit=5f189097d872fad9ed0da267021b6b238841ec98
tree=5c36c28647ce06828e1a5a34803c591bf73d0dfb
run=34029745271
artifact=9988319244
raw_filename=boringos-m66-usb.img
raw_bytes=100663296
raw_sha256=b452309a7f33beead7547631501c888f4e864fdf2a0f895d3bc7d6f270238706
xz_sha256=362820779d5b828003622dac58f0bca98fa32a6c7bcb9c4ede0999f1542b5815
zip_sha256=0a448f7df50d029d2db3130e373a3cf5b3f28d236ca88c59a6ddf61cfa42fa8e
classification=ORIGINAL HISTORICAL ARTIFACT
physical_result=WORKING
screen=DESKTOP
keyboard=WORKING
mouse=WORKING
usb_hub_path=WORKING
```

Dennis physically retested this exact artifact on Cthulhu. This is the M66
physical last good.

### Physical bad

```text
commit=8ccd618dfc4e8163821de552a3c912bab4e6f36a
tree=5f64717f5e2a1ed588602e9abe76b08459b8c186
run=34026943707
artifact=9987432260
raw_filename=boringos-m66-usb.img
raw_bytes=100663296
raw_sha256=84dfb521c2359364ba2f3f78718b686638d81f3d07f35050f32e5a2f91bd0c61
classification=ORIGINAL HISTORICAL ARTIFACT
physical_result=BAD
screen=BLACK
keyboard_light=OFF
mouse_light=ON
input=NONE
final_post=D8
```

The same source head was also rebuilt by run `34032352737` as artifact
`9989055081`, raw SHA-256
`e5e0e33c4750268c92c4a500b81dae3fa08e311c311dab0ba2165f2b5a2da9a5`.
Dennis retested that image and obtained the same physical D8 failure. The raw
images differ only in FAT directory timestamp fields; their kernel, init,
BoringFS payload, GPT, build inputs, and runtime semantics are identical.

`D8` is `M66_POST_DOWNSTREAM_HID_UNSUPPORTED`.

## Git relationship

The bad commit is a direct ancestor of the good commit. The merge base is the
bad commit itself:

```text
8ccd618dfc4e8163821de552a3c912bab4e6f36a
  M66: keep reset witnesses warning-clean
    |
    v
52fcffd5ade0d09f23013ecbb86fbaaff389dae0
  M66: support downstream generic HID mice
    |
    v
5f189097d872fad9ed0da267021b6b238841ec98
  M66: preserve non-HID parser classification
```

Relevant refs at the time of this record:

- `freeze/m66-usb-hub-mouse-physical-2026-09-06` points to the physically bad
  `8ccd618d` image source and must not be cited as a successful physical freeze.
- `agent/m64-usb-topology-mouse` points to the physically good `5f189097`
  image source.
- `main` contains `8ccd618d` as its M66 runtime ancestry and does not contain
  either `52fcffd5` or `5f189097`; its later M66 prose inherited the incorrect
  physical attribution.

Thus this is not a descendant regression after `5f189097`. Bisecting from the
working image toward the known D8 image traverses the two parent commits above.

## Semantic boundary

Both intervening commits are runtime relevant:

| Transition | Classification | Runtime meaning |
| --- | --- | --- |
| `8ccd618d` -> `52fcffd5` | RUNTIME RELEVANT | Adds generic HID report-descriptor parsing, generic mouse decoding, downstream report-descriptor fetch, supported/valid-unsupported/invalid classification, mixed-controller selection, and interrupt-IN arming. |
| `52fcffd5` -> `5f189097` | RUNTIME RELEVANT | Restores the legacy `xhci_parse_hid_configuration()` contract by returning false when a structurally valid configuration contains zero HID interfaces. |

No Makefile, physical image-construction script, Limine configuration, or M61
physical compile define changes between `8ccd618d` and `5f189097`. All three
physical builds use `BORING_M61_PHYSICAL_BREADCRUMBS=1`, the same
`limine-m61-usb.conf` (`timeout: 5`, `mouse: no`), and the same runtime source
file set. The image-level FAT timestamp variation does not explain the physical
result.

## Adjacent untested candidate

The only remaining semantic midpoint is:

```text
commit=52fcffd5ade0d09f23013ecbb86fbaaff389dae0
tree=f3d971e8a479679401b3da4706d6c21709c64386
parent=8ccd618dfc4e8163821de552a3c912bab4e6f36a
message=M66: support downstream generic HID mice
physical_result=UNKNOWN
original_physical_artifact=NONE
```

Historical exact-head CI at `52fcffd5` had 27 of the 28 M66 prerequisite
workflows succeed. M59 run `34029563320` failed while OVMF-booting its USB
image because the expected `mixed non-HID devices present` witness was absent;
its artifact upload was skipped. M61 run `34029563434` succeeded but its
pull-request artifact upload was skipped. M63 run `34029560351` then failed its
aggregate gate because M59 was red. Consequently no original historical raw
image survives for this commit.

A physical test of one explicitly labeled rebuilt `52fcffd5`-equivalent image
separates the remaining boundary:

- GOOD: the physical D8 recovery is in `8ccd618d` -> `52fcffd5`; the later
  one-line parser-contract repair is independently required for non-HID device
  classification.
- BAD: the physical recovery requires the small inseparable pair
  `52fcffd5` + `5f189097`.

No runtime repair is authorized by this note. Stop after the physical result.
