# BoringOS

[English](README.md)

BoringOS ist ein experimentelles, unabhängiges Desktop-Betriebssystemprojekt.

Es ist **keine Linux-Distribution**, **keine BSD-Distribution** und **basiert weder auf Redox noch auf dem Kernel eines anderen Betriebssystems**. BoringOS entwickelt seinen eigenen Kernel, **BoringKernel**, sowie später einen nativen BoringOS-Userspace und Desktop-Stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extrem früher Bootstrap-Kernel.**

BoringKernel bootet unter **QEMU x86_64**. Limine bleibt der externe Bootloader. Nach der Übergabe initialisiert BoringKernel die serielle COM1-Ausgabe, verarbeitet Limines Memory Map für seinen 4096-Byte-Physical-Page-Frame-Allocator, kontrolliert ausgewählte x86_64-4-KiB-Virtual-to-Physical-Mappings, besitzt einen begrenzten dynamischen Kernel-Heap, eine eigene x86_64-IDT für CPU-Exception-Vektoren 0–31 und kann jetzt zusätzlich einen echten periodischen Legacy-Hardware-Timer-Interrupt empfangen und aus ihm zurückkehren.

Die aktuelle serielle Ausgabe beginnt mit:

```text
BoringOS booting...
BoringKernel 0.0.6-dev
Arch: x86_64
Hello from BoringKernel.
```

Der VMM übernimmt bewusst die aktuell aktive, von Limine erzeugte vierstufige Page-Table-Root-Struktur, statt sofort den gesamten Adressraum zu ersetzen. Fehlende Page-Table-Frames kommen aus dem PMM und physischer Page-Table-Speicher wird über Limines gemeldete HHDM-Abbildung erreicht.

Der Kernel-Heap reserviert einen endlichen 16-MiB-Virtual-Address-Bereich, mappt initial nur zwei 4-KiB-Seiten, wächst jeweils um eine Seite über PMM + VMM, verwendet deterministisches First-Fit mit 16-Byte-Ausrichtung und führt benachbarte freie Blöcke wieder zusammen. Bereits gemappte Heap-Seiten bleiben in dieser Bootstrap-Phase nach `kfree` bewusst erhalten.

BoringKernel besitzt eine 256 Einträge große x86_64-IDT. Die CPU-Exception-Vektoren 0–31 bleiben DPL0-Interrupt-Gates, und die separaten QEMU-Acceptance-Modi beweisen weiterhin einen **echten CPU-Divide-Error** und einen **echten MMU-Page-Fault** über BoringKernels eigene Entry-Stubs und C-Exception-Diagnostik.

Der neue Bootstrap-Hardware-Interrupt-Pfad installiert Gates für die PIC-Vektoren 32–47, remappt den Legacy-8259-PIC auf Vektoren 32–39 / 40–47, maskiert zunächst sämtliche PIC-IRQs, programmiert PIT Channel 0 auf angeforderte 100 Hz und gibt anschließend ausschließlich IRQ0 frei. Der normale QEMU-Test aktiviert Interrupts erst nach der Validierung dieses Zustands und verlangt mindestens zehn echte IRQ0-Auslieferungen. Jeder Timer-IRQ erhöht einen Tick-Zähler, sendet das erforderliche PIC-End-Of-Interrupt und kehrt mit `iretq` zurück; Scheduler oder Task-Switching existieren nicht.

Für diesen bewusst historischen PIC/PIT-Nachweis bleibt die QEMU-Referenz `q35`, verwendet aber einen einzelnen Bootstrap-CPU mit explizit deaktiviertem Local-APIC-Feature (`qemu64,apic=off`). Damit wird nicht stillschweigend LAPIC-Konfiguration in einen PIC-only-Meilenstein hineingezogen. PIC/PIT ist temporäre Bootstrap-Infrastruktur und soll später durch eine moderne APIC-basierte Interrupt-Architektur ersetzt werden.

BoringKernel besitzt damit **partielle gezielte Virtual-Memory-Kontrolle, einen funktionierenden begrenzten Bootstrap-Kernel-Heap, einen funktionierenden fatalen CPU-Exception-Pfad und einen verifizierten periodischen Hardware-IRQ-Pfad**. Der Kernel besitzt weiterhin nicht den vollständigen Adressraum und bleibt Single-Core-Bootstrap-Software.

Es gibt **noch keinen Scheduler, keine Präemption, Threads, Prozesse, Ring 3, Syscall-Schicht, kein Dateisystem, keinen Storage-Stack, kein Networking, keine grafische Umgebung und keinen Input-Stack**. LAPIC, IOAPIC, HPET, ACPI/MADT und SMP sind ebenfalls nicht implementiert.

## Technische Richtung

Von BoringOS entwickelte Systemkomponenten werden hauptsächlich in **C** geschrieben. Minimale architekturspezifische Assembly-Anteile sind dort erlaubt, wo sie technisch unvermeidbar sind; sie müssen klein, isoliert und dokumentiert bleiben.

> BoringOS ist ein unabhängiges Betriebssystem, dessen eigene Systemkomponenten hauptsächlich in C geschrieben werden.

Die erste Referenzplattform ist **x86_64 unter QEMU**. Breite Unterstützung physischer PC-Hardware ist bewusst kein frühes Ziel.

## Bauen und booten

Der aktuelle Build verwendet GCC/binutils als freestanding x86_64-Toolchain und lädt eine fest gepinnte Limine-Version. Generierte Dateien bleiben unter `build/`.

Benötigte Host-Werkzeuge sind unter anderem GNU Make, GCC/binutils, `curl`, `xorriso` und QEMU zum Starten/Testen.

```sh
make
make run
```

Der aktuelle Bootstrap-QEMU-Befehl hinter `make run` verwendet weiterhin `q35`, deaktiviert für den derzeitigen PIC/PIT-only-Referenzpfad jedoch explizit das Local-APIC-CPU-Feature.

Die vollständige Acceptance-Suite baut BoringOS im normalen Modus und in den absichtlichen Fatal-Test-Modi neu. Sie prüft PMM, VMM, Heap, IDT-Initialisierung, wiederholte echte PIT/PIC-IRQ0-Auslieferung, einen echten Divide Error und einen echten Page Fault:

```sh
make test
```

Die Exception-Modi sind interne Entwicklungs-/Acceptance-Modi und kein benutzerseitiges Runtime-Testframework.

Siehe [`docs/architecture.md`](docs/architecture.md), [`docs/interrupts.md`](docs/interrupts.md), [`docs/boot.md`](docs/boot.md), [`docs/roadmap.md`](docs/roadmap.md) und [`docs/boringfs.md`](docs/boringfs.md).

## Bewusst keine frühen Ziele

BoringOS soll schrittweise wachsen. Frühe Meilensteine zielen ausdrücklich **nicht** auf Networking, komplexe USB-Unterstützung, Audio, fortgeschrittene GPU-Beschleunigung, Wi-Fi/Bluetooth, Suspend/Resume, Browser, große Kompatibilitätsschichten oder breite Unterstützung physischer PC-Hardware.

Insbesondere Networking wird bewusst aufgeschoben, bis Kernel/User-Trennung, Prozessisolation, getrennte Adressräume, kontrollierte System-Call-Schnittstellen, validierte Kernel-Grenzen und grundlegende defensive Testverfahren vorhanden sind.

## Richtung des nativen Desktops

Der native BoringOS-Desktop soll nicht von X11 oder Wayland abhängen. Ein späterer Meilenstein wird ein bewusst kleines natives BoringOS-Display-/Window-Protokoll und einen Display-Dienst definieren. Die native BoringOS-Version von **BoringWM wird in C geschrieben**.

Das bestehende Repository [dennishilk/boringwm](https://github.com/dennishilk/boringwm) bleibt ein separates Rust/X11-Projekt und eine externe Verhaltensreferenz. Es ist weder Code-Abhängigkeit noch Submodul von BoringOS. Siehe [`docs/boringwm-reference.md`](docs/boringwm-reference.md).

## Repository-Struktur

```text
boringos/
├── docs/
├── kernel/
├── user/
├── libs/
├── tests/
└── scripts/
```

## Prinzipien

BoringOS soll kleine Module, explizite Schnittstellen, vorhersehbares Verhalten, lesbaren C-Code, test- und auditierbare Komponenten, strikte Diagnostik, minimale Abhängigkeiten, dokumentierte Architekturentscheidungen und ehrliche Aussagen über den tatsächlichen Funktionsumfang bevorzugen.

Das Projekt soll für einen einzelnen entschlossenen Entwickler verständlich bleiben.

## Lizenz

BoringOS steht unter der [MIT-Lizenz](LICENSE).
