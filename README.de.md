# BoringOS

[English](README.md)

BoringOS ist ein experimentelles, unabhängiges Desktop-Betriebssystemprojekt.

Es ist **keine Linux-Distribution**, **keine BSD-Distribution** und **basiert weder auf Redox noch auf dem Kernel eines anderen Betriebssystems**. BoringOS entwickelt seinen eigenen Kernel, **BoringKernel**, sowie später einen nativen BoringOS-Userspace und Desktop-Stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extrem früher Bootstrap-Kernel.**

BoringKernel bootet unter **QEMU x86_64**. Limine bleibt der externe Bootloader. BoringKernel besitzt aktuell COM1-Seriellenausgabe, einen Physical-Page-Frame-Allocator, ausgewählte 4-KiB-Virtual-Mappings, einen begrenzten dynamischen Kernel-Heap, eine eigene x86_64-IDT, echte CPU-Exception-Diagnostik, wiederholte PIT/PIC-IRQ0-Auslieferung, kooperative und echte hardware-timerpräemptive Kernel-Tasks und jetzt zusätzlich ein bewusst kleines **Prozessidentitäts- und unabhängiges Adressraummodell**.

Die aktuelle serielle Ausgabe beginnt mit:

```text
BoringOS booting...
BoringKernel 0.0.9-dev
Arch: x86_64
Hello from BoringKernel.
```

Der ursprüngliche VMM übernimmt für PID 0 weiterhin die aktive, von Limine erzeugte vierstufige x86_64-Root. BoringKernel 0.0.9-dev erzeugt zusätzlich PMM-gestützte Prozess-Roots mit leerer privater Lower Half und gemeinsam genutzten Higher-Half-Kernel-Mappings.

Die aktuelle Aufteilung ist bewusst einfach:

```text
PML4-Slots   0-255   prozessprivate Lower Half
PML4-Slots 256-511   gemeinsam genutzte Kernel-Higher-Half
```

Die gemeinsam genutzte Higher Half erhält die benötigten Mappings für Kernel-Image, HHDM, Heap, Task-Stacks, PMM/VMM-Metadaten, IDT, IRQ-/Exception-Code, Scheduler-State und weiterhin benötigte Bootstrap-Strukturen. Gemeinsam genutzte Page Tables gelten niemals als prozesseigene Frames und werden bei der Prozesszerstörung nicht freigegeben.

## Tasks und Prozesse

Ein **Task** ist eine Ausführungs-/Scheduling-Einheit. Ein **Prozess** ist Identität plus Adressraumbesitz. Beide Konzepte bleiben bewusst getrennt.

Der Bootstrap-/Kernelprozess ist PID 0. Der aktuelle Acceptance-Test erzeugt PID 1 und PID 2, jeweils mit einer eigenen PMM-gestützten Root-PML4. Die Prozesszustände bleiben bewusst klein: `ALIVE` und `FINISHED`.

Jeder normale Kernel-Task erhält weiterhin einen unabhängigen **16-KiB-Stack aus dem Kernel-Heap**. Kooperatives Umschalten behält den kleinen SysV-AMD64-Call-Boundary-Kontext. Timerpräemption verwendet weiterhin den separaten vollständigen **192-Byte**-Interrupt-Frame zum Fortsetzen beliebiger Integer-Ausführungszustände.

Ein Task referenziert jetzt seinen besitzenden Prozess. Wählt der Scheduler einen Task eines anderen Prozesses, aktiviert er dessen Prozess-Root mit einem echten CR3-Load, bevor der ausgewählte Interrupt-Frame an Assembly zurückgegeben wird. Der PIC-EOI wird weiterhin gesendet, bevor Assembly den aktuellen IRQ-Stack verlässt.

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

Das System ist **weiterhin ausschließlich CPL0**. Unabhängige Prozessadressräume bedeuten noch keinen Userspace.

Es gibt **kein Ring 3, keine User-CS/SS, keinen TSS-Privilege-Stack-Wechsel, keinen Syscall-Mechanismus, keine Userspace-Runtime, keinen ELF-Loader, kein fork/exec/wait/signals, keine User-Memory-Copy-API, kein VFS, keinen Storage-Stack, kein Networking, keine grafische Umgebung, keinen Input-Stack, kein SMP, kein PCID, kein Copy-on-Write, kein Demand Paging, keinen Swap und kein FPU/SIMD-Kontextwechsel**.

Die nächste Grenze des Ausführungsmodells ist ein separat zu definierender echter Ring-3-Übergang. Er wurde nicht begonnen.

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

Die vollständige Acceptance-Suite führt echte QEMU-Boots für den normalen Kernel sowie absichtliche echte Divide-Error- und Page-Fault-Modi aus. Sie erhält alle bisherigen PMM-, VMM-, Heap-, IRQ-, Cooperative-Task- und Timer-Preemption-Prüfungen und prüft zusätzlich Prozesserzeugung, unterschiedliche Roots, Same-VA/Different-PA-Isolation, echte CR3-Wechsel, Erhalt der Kernel-Mappings, PIT-präemptive Adressraumwechsel, Bootstrap-Rückkehr und Cleanup.

Ein erfolgreicher normaler Lauf endet mit:

```text
BoringKernel process/address-space test passed.
BoringKernel QEMU boot verification passed.
```

Siehe [`docs/architecture.md`](docs/architecture.md), [`docs/interrupts.md`](docs/interrupts.md), [`docs/tasks.md`](docs/tasks.md), [`docs/processes.md`](docs/processes.md), [`docs/boot.md`](docs/boot.md), [`docs/roadmap.md`](docs/roadmap.md) und [`docs/boringfs.md`](docs/boringfs.md).

## Richtung des nativen Desktops

Der native BoringOS-Desktop soll nicht von X11 oder Wayland abhängen. Ein späterer Meilenstein wird ein bewusst kleines natives BoringOS-Display-/Window-Protokoll und einen Display-Dienst definieren. Die native BoringOS-Version von **BoringWM wird in C geschrieben**.

Das bestehende Repository [dennishilk/boringwm](https://github.com/dennishilk/boringwm) bleibt ein separates Rust/X11-Projekt und eine externe Verhaltensreferenz. Es ist weder Code-Abhängigkeit noch Submodul von BoringOS.

## Prinzipien

BoringOS soll kleine Module, explizite Schnittstellen, vorhersehbares Verhalten, lesbaren C-Code, test- und auditierbare Komponenten, strikte Diagnostik, minimale Abhängigkeiten, dokumentierte Architekturentscheidungen und ehrliche Aussagen über den tatsächlichen Funktionsumfang bevorzugen.

Das Projekt soll für einen einzelnen entschlossenen Entwickler verständlich bleiben.

## Lizenz

BoringOS steht unter der [MIT-Lizenz](LICENSE).
