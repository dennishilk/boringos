# BoringOS

[English](README.md)

BoringOS ist ein experimentelles, unabhängiges Desktop-Betriebssystemprojekt.

Es ist **keine Linux-Distribution**, **keine BSD-Distribution** und **basiert weder auf Redox noch auf dem Kernel eines anderen Betriebssystems**. BoringOS entwickelt seinen eigenen Kernel, **BoringKernel**, sowie später einen nativen BoringOS-Userspace und Desktop-Stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extrem früher Bootstrap-Kernel.**

BoringKernel bootet unter **QEMU x86_64**. Limine bleibt der externe Bootloader. BoringKernel besitzt aktuell COM1-Seriellenausgabe, Speicher- und Adressraumverwaltung, Exceptions und PIT/PIC-Interrupts, Kernel-Scheduling, echten CPL3-ELF-Userspace, eine geprüfte native Syscall-Grenze, VFS/RAMFS, schreibbares VirtIO-gestütztes BoringFS, PID 1 `boring-init`, eine interaktive native `boring-shell`, VFS-gestützte statische ELF-Programme, eine prozesslokale native 16-Slot-Descriptor-/stdio-Schicht für die eigenständigen `/bin/boringfetch` und `/bin/cat` im persistenten BoringFS, eine optionale validierte native Framebuffer-Grafikgrundlage mit einem einmalig vom Kernel gerenderten grafischen Boot-Dashboard sowie eine begrenzte native i8042-/PS/2-Tastatur-und-Maus-Input-Grundlage mit exklusiven blockierenden Ring-3-Event-Syscalls und dem eigenständigen `/bin/input-test` sowie die dynamische anonyme Ring-3-Speicherverwaltung aus M32, einen minimalen Userspace-Heap und generische kernel-eigene Shared-Byte-Buffer mit prozesslokalen Capability-Handles und `/bin/memory-test`.

Die aktuelle serielle Ausgabe beginnt mit:

```text
BoringOS booting...
BoringKernel 0.0.33-dev
Arch: x86_64
Hello from BoringKernel.
```

Der ursprüngliche VMM übernimmt für PID 0 weiterhin die aktive, von Limine erzeugte vierstufige x86_64-Root. BoringKernel 0.0.9-dev führte PMM-gestützte Prozess-Roots mit leerer privater Lower Half und gemeinsam genutzten Higher-Half-Kernel-Mappings ein. BoringKernel 0.0.10-dev ergänzte den ersten echten Ring-3-Übergang, 0.0.11-dev die native x86_64-Syscall-Grenze. Spätere Milestones erweiterten genau dieses geprüfte Fundament um native ELF-Programme, Dateisysteme, Storage, Systemidentität, den M27-Shell-Lebenszyklus, M28-VFS-Programme und die native M29-Descriptor-/stdio-Schicht, die M30-Framebuffer-Grundlage, die native M31-PS/2-Input-Grundlage und die M32-Userspace-Memory-/Shared-Buffer-Grundlage; den exakten aktuellen Stand dokumentiert [`docs/roadmap.md`](docs/roadmap.md).

Die aktuelle Aufteilung ist bewusst einfach:

```text
PML4-Slots   0-255   prozessprivate Lower Half
PML4-Slots 256-511   gemeinsam genutzte Kernel-Higher-Half
```

Die gemeinsam genutzte Higher Half erhält die benötigten Mappings für Kernel-Image, HHDM, Heap, Task-Stacks, PMM/VMM-Metadaten, IDT, IRQ-/Exception-Code, Scheduler-State und weiterhin benötigte Bootstrap-Strukturen. Gemeinsam genutzte Page Tables gelten niemals als prozesseigene Frames und werden bei der Prozesszerstörung nicht freigegeben. Die Ring-3- und Syscall-Acceptance-Tests prüfen, dass die kopierten Shared-Root-Einträge weiterhin exakt der Bootstrap-Root entsprechen, und laufen jeden gemeinsamen Übersetzungspfad ab, dessen obere Ebenen user-enabled bleiben; kein vorhandenes Higher-Half-Leaf darf mit `U/S=1` auf jeder Paging-Ebene aus CPL3 erreichbar sein.

## Tasks und Prozesse

Ein **Task** ist eine Ausführungs-/Scheduling-Einheit. Ein **Prozess** ist Identität plus Adressraumbesitz. Beide Konzepte bleiben bewusst getrennt.

Der Bootstrap-/Kernelprozess ist PID 0. Der normale Userspace-Boot erzeugt PID 1 `boring-init`, der synchron eine `boring-shell` als Kind mit eigener PMM-gestützter Root startet. M27 stellt echte RUNNING/WAITING/ZOMBIE-Snapshots und den engen Exit/Wait/Reap/Respawn-Lebenszyklus bereit; Prozess-Slots werden erst nach Reap wiederverwendet, PID-Werte bleiben monoton.

Jeder normale Kernel-Task erhält weiterhin einen unabhängigen **16-KiB-Stack aus dem Kernel-Heap**. Kooperatives Umschalten behält den kleinen SysV-AMD64-Call-Boundary-Kontext. Timerpräemption verwendet weiterhin den separaten vollständigen **192-Byte**-Interrupt-Frame zum Fortsetzen beliebiger Integer-Ausführungszustände.

Ein Task referenziert seinen besitzenden Prozess. Wählt der Scheduler einen Task eines anderen Prozesses, aktiviert er dessen Prozess-Root mit einem echten CR3-Load, bevor der ausgewählte Interrupt-Frame an Assembly zurückgegeben wird. Der PIC-EOI wird weiterhin gesendet, bevor Assembly den aktuellen IRQ-Stack verlässt.

## Nachweis unabhängiger Adressräume

Die zentrale Prozess-Isolationsadresse lautet:

```text
0x0000004000000000
```

Der QEMU-Acceptance-Test erzeugt zwei Prozessadressräume und mappt dieselbe VA auf unterschiedliche Physical Frames:

```text
PID 1: TEST_VA -> Frame A
PID 2: TEST_VA -> Frame B
Frame A != Frame B
```

Anschließend aktiviert er die echten Roots und dereferenziert `TEST_VA` durch die CPU:

```text
PID 1 schreibt 0xAAAAAAAAAAAAAAAA
PID 2 schreibt 0xBBBBBBBBBBBBBBBB
PID 1 liest weiterhin 0xAAAAAAAAAAAAAAAA
PID 2 liest weiterhin 0xBBBBBBBBBBBBBBBB
```

Die Isolation wird nicht über HHDM-Physical-Aliase vorgetäuscht.

Der stärkere Acceptance-Test bindet jeweils einen CPU-bound präemptiven Task an einen Prozess. Keiner der beiden Tasks ruft `task_yield()` auf. Echte PIT-IRQ0-Auslieferung wechselt sowohl Task als auch CR3, während jeder Task wiederholt dieselbe virtuelle Adresse liest und ausschließlich sein eigenes Muster sehen darf.

Ein verifizierter sauberer QEMU-Quelllauf meldete:

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

Nach dem Test stellt BoringKernel PID 0 samt Bootstrap-CR3 wieder her, gibt beide Task-Stacks frei, entfernt die privaten Test-Mappings, gibt ausschließlich prozesseigene Page-Table-Frames und beide Datenframes frei und prüft PMM-/Heap-/VMM-Bookkeeping.

Siehe [`docs/processes.md`](docs/processes.md) für das exakte Modell und die Besitzregeln.

## Aktuelle Grenze des Ausführungsmodells

BoringKernel besitzt einen bewusst eng begrenzten, aber echten nativen Ring-3-Userspace; er ist keine allgemeine POSIX-Umgebung.

Der Kernel lädt eine eigene GDT mit Kernel-Code/Data-Deskriptoren, DPL3-User-Data/Code-Deskriptoren und genau einem verfügbaren 64-Bit-TSS. Die aktuellen Selektoren sind:

```text
0x08  kernel code
0x10  kernel data
0x1B  user data, RPL3
0x23  user code, RPL3
0x28  TSS
```

Der TSS besitzt einen dedizierten 16-KiB-RSP0-Stack. Der Ring-3-Test mappt genau eine feste User-Code-Seite bei `0x0000000040000000` als present + user + read-only/executable und genau eine feste User-Stack-Seite bei `0x0000000040010000` als present + user + writable. User-Zugriff wird auf jeder nötigen Ebene PML4/PDPT/PD/PT propagiert, ohne irgendein effektives gemeinsam genutztes Higher-Half-Kernel-Mapping aus CPL3 erreichbar zu machen.

Die CPU wechselt über einen echten `iretq`-Frame nach CPL3 mit CS `0x23`, SS `0x1B` und User-RSP `0x0000000040011000`. Die ursprüngliche Ring-3-Acceptance führt das privilegierte `cli` aus und beweist, dass der resultierende echte **#GP / Vector 13** über den separaten TSS-RSP0-Exception-Pfad zurück nach CPL0 gelangt.

Die Syscall-Grenze führt echtes x86_64-`SYSCALL` aus. BoringKernel aktiviert `IA32_EFER.SCE`, programmiert und liest `IA32_STAR`, `IA32_LSTAR` und `IA32_FMASK` zurück, sichert den noch user-kontrollierten RSP vor jeder normalen Stacknutzung und wechselt sofort auf einen dedizierten supervisor-only **16-KiB-Syscall-Kernelstack**. Das provisorische ABI verwendet `RAX` für die Syscall-Nummer, `RDI/RSI/RDX/R10/R8/R9` für Argumente und `RAX` für das Ergebnis; `RCX/R11` sind architektonische Clobbers. Die aktuelle begrenzte Oberfläche reicht mit den Nummern 0–17 von `GETPID` bis `WAITPID` und umfasst Console, Launch, VFS, Systeminformationen, CWD und Prozess-Snapshots.

`DEBUG_WRITE` reicht niemals einen rohen Userspace-Pointer an die Serial-Schicht weiter. Der `copy_from_user`-Pfad validiert den vollständigen Lower-Half-Bereich, läuft die aktuellen Prozess-Page-Tables mit effektiven Present- und U/S-Prüfungen ab, löst Physical Memory über den vertrauenswürdigen HHDM-Alias auf und kopiert zuerst in einen kernel-eigenen Buffer. `SYSRETQ` wird nur ausgeführt, nachdem gespeicherter User-RIP/RSP, aktiver Prozess/Adressraum, erwartete Selektoren und sanitizte Return-RFLAGS geprüft wurden. Der Test führt sieben echte Syscall-Dispatches aus, beweist mehrere `SYSRETQ`-Rückkehrpfade nach CPL3 und führt danach `cli` aus; dieser letzte echte #GP verwendet weiterhin TSS.RSP0 und nicht den Syscall-Stack.

Das Syscall-ABI ist **provisorisch** und kein stabiler öffentlicher Userspace-Vertrag. Es gibt weiterhin **kein libc, keinen FD-/TTY-Layer, kein allgemeines `fork` oder VFS-gestütztes `exec`, keine Signale, kein Authentifizierungs- oder Berechtigungsmodell, kein nebenläufiges Child-Scheduling, kein Networking, keine grafische Umgebung, keinen nativen Input-Stack, kein SMP, kein PCID, kein Copy-on-Write, kein Demand Paging, keinen Swap und keinen FPU/SIMD-Kontextwechsel**.

Siehe [`docs/syscalls.md`](docs/syscalls.md) für die exakt implementierte Syscall-Grenze und ihre aktuellen Begrenzungen.

Für den aktuellen Bootstrap-Nachweis bleibt QEMU:

```text
-M q35 -cpu qemu64,apic=off -m 128M
```

Das ist temporäre Bootstrap-Infrastruktur und keine Behauptung über physische Hardware-Kompatibilität.

## Technische Richtung

Von BoringOS entwickelte Systemkomponenten werden hauptsächlich in **C** geschrieben. Minimale architekturspezifische Assembly-Anteile sind dort erlaubt, wo sie technisch unvermeidbar sind; sie müssen klein, isoliert und dokumentiert bleiben.

> BoringOS ist ein unabhängiges Betriebssystem, dessen eigene Systemkomponenten hauptsächlich in C geschrieben werden.

Die erste Referenzplattform ist **x86_64 unter QEMU**. Breite Unterstützung physischer PC-Hardware ist bewusst kein frühes Ziel.

## Bauen und testen

Der aktuelle Build verwendet GCC/binutils als freestanding x86_64-Toolchain und lädt eine fest gepinnte Limine-Version. Generierte Dateien bleiben unter `build/`.

Benötigte Host-Werkzeuge sind unter anderem GNU Make, GCC/binutils, `curl`, `xorriso` und QEMU.

```sh
make
make run
make test
```

Die vollständige Acceptance-Suite führt echte QEMU-Boots für den normalen Kernel sowie absichtliche echte Divide-Error-, Page-Fault-, den vorhandenen dedizierten Ring-3- und den dedizierten Syscall-Modus aus. Sie erhält alle bisherigen PMM-, VMM-, Heap-, IRQ-, Cooperative-Task-, Timer-Preemption-, Prozess-/Adressraum- und CR3-Wechsel-Prüfungen. Der Ring-3-Modus beweist separat GDT/TSS-Zustand, User-Page-Permissions, effektiv supervisor-only Shared Higher Half, echten `iretq`-Eintritt nach CPL3, echten durch `cli` erzeugten #GP, Erhalt der Hardware-User-RSP/SS-Werte und den TSS-RSP0-Kernelstack-Wechsel. Der Syscall-Modus beweist MSR-Konfiguration, Nutzung des vertrauenswürdigen Syscall-Stacks, GETPID, begrenztes sicheres User-Copy für DEBUG_WRITE, negative Pointer-/Range-/Fehlerfälle, mehrere echte `SYSCALL`/`SYSRETQ`-Roundtrips, Erhalt von User-RSP und Callee-Saved-Registern sowie abschließend CPL3-`cli` -> #GP über TSS.RSP0.

Ein erfolgreicher normaler Lauf endet mit:

```text
BoringKernel process/address-space test passed.
BoringKernel QEMU boot verification passed.
```

Ein erfolgreicher Ring-3-Lauf endet mit:

```text
BoringKernel Ring 3 test passed.
BoringKernel Ring 3 verification passed.
```

Ein erfolgreicher Syscall-Lauf endet mit:

```text
BoringKernel syscall boundary test passed.
BoringKernel syscall verification passed.
```

Siehe [`docs/architecture.md`](docs/architecture.md), [`docs/interrupts.md`](docs/interrupts.md), [`docs/tasks.md`](docs/tasks.md), [`docs/processes.md`](docs/processes.md), [`docs/syscalls.md`](docs/syscalls.md), [`docs/boot.md`](docs/boot.md), [`docs/roadmap.md`](docs/roadmap.md) und [`docs/boringfs.md`](docs/boringfs.md).

## Richtung des nativen Desktops

Der native BoringOS-Desktop soll nicht von X11 oder Wayland abhängen. Ein späterer Meilenstein wird ein bewusst kleines natives BoringOS-Display-/Window-Protokoll und einen Display-Dienst definieren. Die native BoringOS-Version von **BoringWM wird in C geschrieben**.

Das bestehende Repository [dennishilk/boringwm](https://github.com/dennishilk/boringwm) bleibt ein separates Rust/X11-Projekt und eine externe Verhaltensreferenz. Es ist weder Code-Abhängigkeit noch Submodul von BoringOS.

## Prinzipien

BoringOS soll kleine Module, explizite Schnittstellen, vorhersehbares Verhalten, lesbaren C-Code, test- und auditierbare Komponenten, strikte Diagnostik, minimale Abhängigkeiten, dokumentierte Architekturentscheidungen und ehrliche Aussagen über den tatsächlichen Funktionsumfang bevorzugen.

Das Projekt soll für einen einzelnen entschlossenen Entwickler verständlich bleiben.

## Lizenz

BoringOS steht unter der [MIT-Lizenz](LICENSE).
