# Run BoringOS in QEMU

This bundle boots BoringKernel, mounts the included writable BoringFS image as
the native root filesystem, starts `boring-init`, and hands control to the
native PID 2 `boring-shell`.

Requirements: `qemu-system-x86_64` on an x86_64 Linux host.

Run from the extracted bundle:

```sh
chmod +x run-boringos.sh
./run-boringos.sh
```

The root image is attached through modern VirtIO PCI. Writes are persistent;
do not add QEMU snapshot options. Try:

```text
help
ls
mkdir test
cd test
touch hello.txt
write hello.txt Hallo-von-BoringOS
cat hello.txt
boringfetch
```

Stop QEMU, start the same script again, and `cat /test/hello.txt` to verify
persistence. BoringFS v0 uses synchronous metadata updates but has no journal
and does not yet promise crash consistency.
