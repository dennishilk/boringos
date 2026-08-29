# BoringOS

[English](README.md)

BoringOS ist ein experimentelles **unabhängiges Desktop-Betriebssystem** für x86_64, das von Grund auf um den eigenen Kernel **BoringKernel** gebaut wird.

Es ist **keine Linux-Distribution**, **kein BSD** und **basiert nicht auf dem Kernel eines anderen Betriebssystems**. Von BoringOS entwickelte Systemkomponenten werden hauptsächlich in **C** geschrieben; kleine isolierte x86_64-Assembly-Anteile gibt es nur dort, wo die Architektur sie wirklich verlangt.

> boring is not a bug.  
> it's the entire operating system now.

## Aktueller Stand

Der aktuelle Main-Stand ist:

```text
BoringKernel 0.0.56-dev
```

Die Entwicklung ist bis einschließlich **Milestone 55** abgeschlossen.

BoringOS ist längst nicht mehr nur ein früher Boot-Kernel: Unter QEMU bootet inzwischen eine echte native Ring-3-Desktop-Session mit eigenem Display-Service, Tiling-Window-Manager, grafischem Terminal, Shell, Editor, Dateimanager, persistentem BoringFS, Hardware-Inventar und einem wachsenden BoringOS-eigenen xHCI-/USB-Stack.

Es bleibt ein experimentelles Forschungs-/Lernbetriebssystem. QEMU ist weiterhin die primär verifizierte Plattform. Die Unterstützung echter PCs wird bewusst schrittweise aufgebaut und ist **noch keine Behauptung, dass BoringOS bereits auf physischer Hardware erfolgreich läuft**.

Die detaillierte Quelle der Wahrheit ist [`docs/roadmap.md`](docs/roadmap.md).

## Was heute wirklich läuft

```text
QEMU x86_64 / UEFI- oder BIOS-Pfad
        ↓
      Limine
        ↓
   BoringKernel
        ↓
PMM / VMM / Heap / IDT / Scheduler
        ↓
Prozesse + unabhängige Adressräume
        ↓
Ring 3 + natives SYSCALL/SYSRETQ-ABI
        ↓
VFS + RAMFS + VirtIO Block + BoringFS
        ↓
     boring-init
        ↓
  boring-display
        ↓
     BoringWM
        ↓
┌──────────────┬──────────────┬──────────────┐
│BoringTerminal│ BoringEdit   │ BoringFiles  │
│ boring-shell │              │              │
│ boringfetch  │              │              │
└──────────────┴──────────────┴──────────────┘
```

Zu den implementierten und verifizierten Grundlagen gehören:

- begrenzte physische und virtuelle Speicherverwaltung sowie ein Kernel-Heap
- x86_64-Exceptions, Legacy-PIC/PIT-Interrupts, kooperatives und timergetriebenes präemptives Scheduling
- Prozessidentität, unabhängige Page-Table-Roots, CR3-Wechsel, CPL3 und TSS/RSP0-Übergänge
- natives x86_64-`SYSCALL` / `SYSRETQ`
- validiertes statisches ELF64-Userspace-Loading und eine BoringOS-eigene freestanding C-Runtime
- VFS, veränderbares RAMFS, schreibbares persistentes BoringFS, generisches Block-I/O und moderner VirtIO-Block-Storage
- PID 1 `boring-init`, native `boring-shell`, Prozess-Lifecycle, PTYs, File Descriptors/stdio und Scheduler-eigenes `SPAWN`
- anonymer Ring-3-Speicher, kleiner Userspace-Heap, Shared Buffer, native Service Registry und IPC
- Framebuffer-Ownership/Presentation, natives `boring-display`, Software-Komposition und Cursor
- nativer C-**BoringWM** mit begrenztem Tiling, Fokus, Reorder und Lifecycle-Behandlung
- natives grafisches **BoringTerminal**, **BoringEdit**, **BoringFiles**, `/bin/boringfetch` und `/bin/cat`
- echtes CPUID-, PCI- und SMBIOS-Plattforminventar, das `boringfetch` ohne erfundene Hardwarewerte anzeigt
- begrenztes xHCI-Controller-Ownership, USB-Geräteadressierung, Descriptor Discovery, `SET_CONFIGURATION`, HID-Interrupt-IN-Endpoint-Setup, echte Interrupt-IN-Transfer-Events und begrenzte HID-Report-Decodierung bis Milestone 52

## Nativer Desktop

Der grafische Desktop gehört vollständig BoringOS. Er verwendet weder X11 noch Wayland.

Aktuelle Tastenkürzel sind unter anderem:

```text
Super+Return   BoringTerminal öffnen
Super+E        BoringEdit öffnen
Super+F        BoringFiles öffnen
Super+J/L      Fokus wechseln
Super+Q        fokussierten verwalteten Client schließen
```

`BoringTerminal` startet eine separat geplante `boring-shell` über ein echtes PTY. Mehrere grafische Terminals besitzen eigene Prozesse, Adressräume und getrennten Input-Fokus. Im Terminal laufen echte eigenständige BoringFS-Programme wie `boringfetch` und `cat`.

`BoringEdit` ist ein begrenzter nativer Texteditor mit echtem Laden/Speichern über BoringFS. `BoringFiles` ist ein nativer Dateimanager auf Basis echter VFS-Verzeichniseinträge und kann Dateien in BoringEdit öffnen.

Auch der Desktop-Startpfad ist echt: PID 1 startet Display-Server und BoringWM aus BoringFS und überwacht die begrenzte Desktop-Session, statt für jede Komponente zusätzliche Boot-Module zu benötigen.

## Hardware-Inventar und Richtung echter PC

BoringOS liest reale Gast-Hardwaredaten, anstatt erfundene Rechnernamen in die Oberfläche einzubauen. Aktuelle Collector liefern:

```text
CPUID   → Vendor, Brand, Family/Model/Stepping und begrenzte Feature-Daten
PCI     → echtes BDF-/Vendor-/Device-/Class-Inventar
SMBIOS  → Firmware-, System-, Board- und Speicheridentität
INFO    → begrenzter versionierter Userspace-Snapshot
```

`boringfetch` verwendet diese Kernel-eigenen Werte. Unter QEMU zeigt es deshalb die emulierte QEMU-Hardware; auf echter Hardware soll es später die tatsächliche Maschine beschreiben.

Milestone 47 hat eine ehrliche Real-Hardware-Readiness-Grenze geschaffen. Gültige Memory Maps, die größer als die feste PMM-Kapazität sind, brechen den Boot nicht mehr ab. Der aktuelle PMM verwaltet aber weiterhin höchstens **1.048.576 4-KiB-Frames = 4 GiB**. Darüber hinaus vorhandener nutzbarer RAM wird als gecappt gemeldet und vorerst nicht verwaltet.

Das erste physische PC-Ziel bleibt ein begrenzter UEFI-Bring-up: Limine → BoringKernel → Firmware-Framebuffer → Hardware-Inventar → nativer Desktop. Interne Datenträger sollen nicht als beschreibbar behandelt werden, solange dafür kein ausdrücklich unterstützter Storage-Pfad existiert.

## xHCI-/USB-Stand

Der moderne USB-Pfad ist inzwischen deutlich weiter als reine PCI-Erkennung.

Milestones 48–52 liefern aktuell:

```text
xHCI PCI Discovery / BAR- + MMIO-Validierung
        ↓
Legacy-Ownership-Handoff / Halt / Reset / Start
        ↓
DCBAA + Command Ring + Event Ring + ERST
        ↓
echter Root-Port-Connect-State
        ↓
Enable Slot + Address Device
        ↓
EP0-Transfer-Ring pro Gerät
        ↓
echtes GET_DESCRIPTOR Control-IN
        ↓
Device- + Configuration-Descriptor-Validierung
        ↓
SET_CONFIGURATION
        ↓
aus Deskriptoren abgeleitete HID-Interrupt-IN-Endpoint-Contexts
        ↓
Configure Endpoint
        ↓
PMM-eigene Report-DMA-Puffer + Normal TRBs
        ↓
echte Transfer Events + begrenzte HID-Report-Decodierung
```

Die aktuelle Grenze ist bewusst eng: **M52 empfängt und decodiert echte USB-Tastatur- und QEMU-Absolute-Tablet-Reports erst nach echten xHCI Transfer Events, speist diese decodierten Reports aber noch nicht in die normale BoringOS-Inputqueue ein.** Der normale grafische Desktop verwendet deshalb weiterhin den bereits bewiesenen Legacy-i8042-/PS/2-Inputpfad.

USB-Hubs und USB-Massenspeicher werden noch nicht unterstützt.

## Storage und Dateisysteme

BoringOS besitzt mit **BoringFS** ein eigenes kleines Dateisystemformat. Das Repository enthält Codec/Validator, deterministischen Formatter, `boringfsck`, Kernel-Mount-Support und synchrones schreibbares Verhalten.

Die verifizierte persistente QEMU-Root verwendet derzeit modernen VirtIO-PCI-Block-Storage. AHCI und NVMe sind noch nicht implementiert; ein Controller im PCI-Inventar zu sehen bedeutet ausdrücklich nicht, dass bereits ein Storage-Treiber dafür vorhanden ist.

## Aktuelle Grenzen

| Bereich | Aktueller Stand |
| --- | --- |
| Primär verifizierte Plattform | QEMU x86_64 |
| Physische Hardware | Readiness Candidate; noch nicht physisch verifiziert |
| Verwalteter RAM | aktuell auf 4 GiB begrenzt; größere gültige Maps werden akzeptiert |
| Grafik | Firmware-/Limine-Framebuffer + Software-Compositor; keine native AMD/NVIDIA/Intel-GPU-Beschleunigung |
| Desktop-Input | nativer i8042-/PS/2-Pfad |
| USB | xHCI-Geräte adressiert/konfiguriert; echter HID-Interrupt-IN-Report-Transport + begrenzte Decodierung bewiesen; Inputqueue-Integration noch offen |
| Persistente Root | VirtIO Block + BoringFS |
| AHCI / NVMe | nicht implementiert |
| Networking | nicht implementiert |
| Audio | nicht implementiert |
| SMP-Runtime | nicht implementiert; Runtime bleibt bewusst begrenzt |
| POSIX-Kompatibilität | weder Ziel noch aktueller Vertrag |
| Installer / polierter Live-USB | nicht implementiert |

Diese Grenzen sind Absicht. BoringOS versucht, nicht unterstützte Hardware und unvollständige Subsysteme ehrlich zu melden, statt aus bloßer Erkennung bereits Support abzuleiten.

## Syscall-ABI

BoringOS verwendet aktuell ein eigenes provisorisches x86_64-Syscall-ABI. Aus der ursprünglichen `GETPID`-/Debug-Grenze sind begrenzte Console-, Filesystem-, Prozess-, Descriptor-, Input-, Memory-, Shared-Buffer-, IPC-, Framebuffer-, Event-, PTY- und Process-Spawn-Operationen entstanden.

Das aktuelle ABI belegt die Syscall-Nummern **0–43**. Es ist bewusst **kein stabiles POSIX-ABI**. Den exakten Vertrag dokumentieren [`docs/syscalls.md`](docs/syscalls.md) und [`docs/roadmap.md`](docs/roadmap.md).

## Bauen und testen

Der Build verwendet GCC/binutils als freestanding x86_64-Toolchain und eine fest gepinnte Limine-Version. Generierte Dateien bleiben unter `build/`.

Typische Host-Abhängigkeiten sind GNU Make, GCC/binutils, `curl`, `xorriso` und QEMU.

```sh
make
make run
make test
```

`make run` ist der historische/headless Bootstrap-Pfad und nicht die beste Demonstration des vollständigen grafischen Desktops. Die grafischen Milestones verwenden eigene QEMU-Acceptance-/Bundle-Pfade; siehe [`docs/RUNNING-M36.md`](docs/RUNNING-M36.md) sowie die aktuellen Milestone-Einträge in [`docs/roadmap.md`](docs/roadmap.md).

Die permanente CI hält frühere Milestone-Beweise am Leben, während neue Fähigkeiten hinzukommen. Dazu gehören Host-/Modelltests, Sanitizer wo sinnvoll, echte QEMU-Hardwarepfade, Prozess-/Ressourcen-Cleanup sowie exakte Framebuffer-Evidence für grafische Milestones.

## Roadmap

Das Projekt entwickelt sich in kleinen semantischen Milestones. Ein Milestone gilt nicht allein deshalb als fertig, weil der Code baut: fokussierte Acceptance, geerbte Regressionen, Semantic Freeze, runtime-neutraler Versions-Closeout, Exact-Head-CI, guarded Squash Merge und die Verifikation des neuen `main` gehören zur Entwicklungsdisziplin.

Zum Zeitpunkt dieser README-Aktualisierung ist **M52 abgeschlossen**. Der nächste moderne Input-Schritt ist **M53: USB-HID-Integration in die bestehende BoringOS-Inputqueue**. Ein späterer Milestone kann anschließend den vollständigen grafischen Desktop ohne i8042/PS/2 beweisen, bevor der bewusst sichere physische PC-/Live-Boot-Pfad weitergeht.

Für den exakten aktuellen Milestone-Stand gilt [`docs/roadmap.md`](docs/roadmap.md), nicht eine geplante Funktion aus dieser README.

## BoringWM

BoringOS enthält eine eigene native C-Implementierung von BoringWM für den BoringOS-Display-/IPC-Stack.

Das separate Repository [`dennishilk/boringwm`](https://github.com/dennishilk/boringwm) bleibt das ursprüngliche Rust-/X11-Projekt und eine Verhaltensreferenz. Es ist **keine** BoringOS-Abhängigkeit und kein Submodule.

## Prinzipien

BoringOS bevorzugt kleine Module, explizite Schnittstellen, vorhersehbares Verhalten, lesbares C, begrenzte Datenstrukturen, testbare/auditierbare Komponenten, strikte Diagnostik, minimale Abhängigkeiten und präzise Aussagen darüber, was funktioniert und was nicht.

Das Projekt soll für einen entschlossenen einzelnen Entwickler verständlich bleiben.

## Lizenz

BoringOS steht unter der [MIT License](LICENSE).
