# M69 networking foundation

M69 adds the first native BoringOS Ethernet and Internet-facing networking path.
It deliberately starts narrow: one bounded NIC driver and the protocols required
for an interactive `ping`, without introducing TCP, sockets, a background
network daemon, or a general-purpose dynamic packet allocator.

## Security contract

Every received frame is untrusted input.

The network path therefore:

- validates Ethernet, ARP, IPv4, UDP, ICMP, DHCP and DNS lengths before field access;
- validates IPv4, ICMP and non-zero UDP checksums;
- rejects IPv4 fragments instead of attempting reassembly;
- bounds the ARP cache, DHCP option walk, DNS record count and DNS compression depth;
- rejects malformed or looping DNS compression pointers;
- never allocates memory according to an untrusted packet length;
- drops unknown or malformed input without changing protocol state;
- accepts ICMP replies only for the expected peer, identifier and sequence;
- validates DHCP transaction identity and client MAC before accepting an offer/ACK;
- validates DNS transaction ID, response/opcode/rcode state and the expected DNS
  server path before accepting an A record.

This is a correctness boundary, not a claim that ARP, DHCP or DNS are
cryptographically authenticated. Those protocols are inherently spoofable on an
untrusted local network.

## Driver boundary

The first hardware driver is the classic Intel E1000 family used by the BoringOS
QEMU reference path. The driver uses bounded legacy RX/TX descriptor rings,
PMM-owned DMA memory below 4 GiB and polling rather than interrupts.

The physical Cthulhu Ethernet PCI identity was not recorded before M69. M69
therefore does not claim physical NIC support yet. If a physical `ping` sees an
Ethernet controller outside the supported set, the shell reports its PCI
vendor/device ID and BDF so the next driver can be added without reopening the
protocol stack.

## Protocol path

The initial command path is:

```text
boring-shell
  -> NET_PING syscall
  -> E1000
  -> DHCP
  -> ARP
  -> IPv4
  -> ICMP
```

For hostnames it extends to:

```text
DHCP-provided DNS server
  -> UDP
  -> bounded DNS A lookup
  -> ICMP
```

Network initialization is lazy: ordinary boot, USB, storage, display and input
do not touch the NIC. The first `ping` initializes the driver and obtains a DHCP
lease.

## Timeout clock

The x86-64 syscall entry masks IF through IA32_FMASK. A network syscall therefore
must not depend on interrupt-driven `timer_ticks()` advancing while the syscall
is active. M69 latches and reads PIT channel 0 directly for bounded polling
timeouts and millisecond round-trip reporting while leaving the established PIT
configuration intact.

## Acceptance

Host tests exercise IPv4 literal bounds, checksum validation and hostile DNS
name/compression cases.

The focused QEMU acceptance uses `-nic user,model=e1000`, obtains the normal
QEMU user-network DHCP address `10.0.2.15`, and requires four ICMP replies from
the QEMU gateway `10.0.2.2`.

The intended physical interactive tests are:

```text
ping 1.1.1.1
ping google.de
```

A successful QEMU test is not a substitute for physical Cthulhu evidence.
