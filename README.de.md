# BoringOS

[English](README.md)

BoringOS ist ein experimentelles, unabhängiges Desktop-Betriebssystemprojekt.

Es ist **keine Linux-Distribution**, **keine BSD-Distribution** und **basiert weder auf Redox noch auf dem Kernel eines anderen Betriebssystems**. BoringOS entwickelt seinen eigenen Kernel, **BoringKernel**, sowie später einen nativen BoringOS-Userspace und Desktop-Stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extrem früher Bootstrap-Kernel.**

BoringKernel bootet unter **QEMU x86_64**. Limine bleibt der externe Bootloader. Nach der Übergabe initialisiert BoringKernel die serielle COM1-Ausgabe, verarbeitet Limines Memory Map für seinen 4096-Byte-Physical-Page-Frame-Allocator, kontrolliert ausgewählte x86_64-4-KiB-Virtual-to-Physical-Mappings und besitzt jetzt zusätzlich einen kleinen begrenzten dynamischen Kernel-Heap auf Basis dieser PMM-/VMM-Primitiven.

Die aktuelle serielle Ausgabe beginnt mit:

```text
BoringOS booting...
BoringKernel 0.0.4-dev
Arch: x86_64
Hello from BoringKernel.
```

Der VMM übernimmt bewusst die aktuell aktive, von Limine erzeugte vierstufige Page-Table-Root-Struktur, statt sofort den gesamten Adressraum zu ersetzen. Fehlende Page-Table-Frames kommen aus dem PMM und physischer Page-Table-Speicher wird über Limines gemeldete HHDM-Abbildung erreicht.

Der Kernel-Heap reserviert einen endlichen 16-MiB-Virtual-Address-Bereich, mappt initial nur zwei 4-KiB-Seiten, wächst jeweils um eine Seite über PMM + VMM, verwendet deterministisches First-Fit mit 16-Byte-Ausrichtung und führt benachbarte freie Blöcke wieder zusammen. Bereits gemappte Heap-Seiten bleiben in diesem Bootstrap-Meilenstein nach `kfree` bewusst erhalten.

Der automatisierte QEMU-Acceptance-Test prüft weiterhin sämtliche PMM- und VMM-Invarianten und führt nun zusätzlich echte bytegroße Heap-Allokationen, echte Schreib-/Lesetests, erzwungenes PMM-/VMM-gestütztes Heap-Wachstum, Free/Reuse, Double-Free-Ablehnung, Invalid-Free-Ablehnung und abschließende Allocator-Buchhaltung durch.

BoringKernel besitzt damit **partielle, gezielte Kontrolle über virtuellen Speicher und einen funktionierenden begrenzten Bootstrap-Kernel-Heap**, aber noch weder vollständige Adressraumhoheit noch einen fertigen Produktions-Allocator. Kernel-Ausführung, aktueller Stack, HHDM und Boot-Strukturen hängen weiterhin von geerbten Limine-Mappings ab.

Das System bleibt absichtlich winzig. Es gibt **noch keinen Userspace, Scheduler, kein Dateisystem, Networking, keine grafische Umgebung, keinen Input-Stack, Exception-/Interrupt-Unterbau oder Prozessmodell**.

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

Der automatisierte Acceptance-Test baut BoringOS neu, startet QEMU headless, erfasst die serielle Konsole und prüft Boot-Identität, PMM, ausgewählte VMM-Mapping-Kontrolle und den begrenzten Kernel-Heap:

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
