# BoringOS

[English](README.md)

BoringOS ist ein experimentelles, unabhängiges Desktop-Betriebssystemprojekt.

Es ist **keine Linux-Distribution**, **keine BSD-Distribution** und **basiert weder auf Redox noch auf dem Kernel eines anderen Betriebssystems**. BoringOS entwickelt seinen eigenen Kernel, **BoringKernel**, sowie später einen nativen BoringOS-Userspace und Desktop-Stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extrem früher Bootstrap-Kernel.**

BoringKernel bootet unter **QEMU x86_64**. Limine bleibt der externe Bootloader. Nach der Übergabe initialisiert BoringKernel die serielle COM1-Ausgabe, verarbeitet Limines Memory Map für seinen 4096-Byte-Physical-Page-Frame-Allocator und kontrolliert jetzt zusätzlich ausgewählte x86_64-4-KiB-Virtual-to-Physical-Mappings.

Die aktuelle serielle Ausgabe beginnt mit:

```text
BoringOS booting...
BoringKernel 0.0.3-dev
Arch: x86_64
Hello from BoringKernel.
```

Der VMM übernimmt bewusst die aktuell aktive, von Limine erzeugte vierstufige Page-Table-Root-Struktur, statt sofort den gesamten Adressraum zu ersetzen. Fehlende Page-Table-Frames kommen aus dem PMM, physischer Page-Table-Speicher wird über Limines gemeldete HHDM-Abbildung erreicht, normale 4-KiB-Kernel-Seiten können gemappt, zurückübersetzt und wieder entfernt werden, und geänderte Übersetzungen werden mit `invlpg` invalidiert.

Der automatisierte QEMU-Acceptance-Test prüft weiterhin alle PMM-Invarianten und zusätzlich einen echten VMM-Mapping-Test, der Daten durch ein neu erzeugtes virtuelles Mapping schreibt und wieder liest, bevor das Mapping entfernt und alle test-eigenen Frames zurückgegeben werden.

BoringKernel besitzt damit **partielle, gezielte Kontrolle über virtuellen Speicher**, aber noch nicht den vollständigen Adressraum. Kernel-Ausführung, aktueller Stack, HHDM und Boot-Strukturen hängen weiterhin von geerbten Limine-Mappings ab.

Das System bleibt absichtlich winzig. Es gibt **noch keinen allgemeinen Kernel-Heap, Userspace, Scheduler, kein Dateisystem, Networking, keine grafische Umgebung, keinen Input-Stack, Exception-/Interrupt-Unterbau oder Prozessmodell**.

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

Der automatisierte Acceptance-Test baut BoringOS neu, startet QEMU headless, erfasst die serielle Konsole und prüft Boot-Identität, PMM sowie den ausgewählten VMM-Mapping-Selbsttest:

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
