# BoringOS

[English](README.md)

BoringOS ist ein experimentelles, unabhängiges Desktop-Betriebssystemprojekt.

Es ist **keine Linux-Distribution**, **keine BSD-Distribution** und **basiert weder auf Redox noch auf dem Kernel eines anderen Betriebssystems**. BoringOS entwickelt seinen eigenen Kernel, **BoringKernel**, sowie später einen nativen BoringOS-Userspace und Desktop-Stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extrem früher Bootstrap-Kernel.**

BoringKernel bootet unter **QEMU x86_64**. Limine dient ausschließlich als externer Bootloader; nach der Übergabe erreicht die Ausführung den freestanding BoringKernel-Einstiegspunkt. BoringKernel initialisiert die serielle COM1-Ausgabe und verarbeitet jetzt zusätzlich die von Limine gelieferte Memory Map für einen minimalen 4096-Byte-Physical-Page-Frame-Allocator mit Allocate/Free und einem In-Kernel-Selbsttest unter QEMU.

Die aktuelle serielle Ausgabe beginnt mit:

```text
BoringOS booting...
BoringKernel 0.0.2-dev
Arch: x86_64
Hello from BoringKernel.
```

Der Boot-Acceptance-Test prüft zusätzlich die PMM-Initialisierung, die zur Laufzeit ermittelten Werte für nutzbaren Speicher und Frames, eindeutige ausgerichtete Allocations innerhalb gültiger usable-RAM-Bereiche, Free/Reuse sowie konsistente Buchhaltung.

Das System bleibt absichtlich winzig. Es gibt **noch keinen allgemeinen Kernel-Heap, keinen BoringKernel-eigenen Virtual-Memory-Manager, Userspace, Scheduler, kein Dateisystem, Networking, keine grafische Umgebung, keinen Input-Stack, Interrupt-Unterbau oder Prozessmodell**.

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

Der automatisierte Acceptance-Test baut BoringOS neu, startet QEMU headless, erfasst die serielle Konsole und prüft sowohl die Boot-Identität als auch den Physical-Memory-Selbsttest:

```sh
make test
```

Siehe [`docs/architecture.md`](docs/architecture.md), [`docs/boot.md`](docs/boot.md), [`docs/roadmap.md`](docs/roadmap.md) und [`docs/boringfs.md`](docs/boringfs.md).

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
