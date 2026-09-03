# BoringOS

[English](README.md)

BoringOS ist ein experimentelles **unabhängiges Desktop-Betriebssystem** für x86_64, das von Grund auf um den eigenen Kernel **BoringKernel** gebaut wird.

Es ist **keine Linux-Distribution**, **kein BSD** und **basiert nicht auf dem Kernel eines anderen Betriebssystems**. Von BoringOS entwickelte Systemkomponenten werden hauptsächlich in **C** geschrieben; kleine isolierte x86_64-Assembly-Anteile gibt es nur dort, wo die Architektur sie wirklich verlangt.

> boring is not a bug.  
> it's the entire operating system now.

## Aktueller Stand

Der aktuelle Main-Stand ist:

```text
BoringKernel 0.0.62-dev
```

Die Entwicklung ist bis einschließlich **Milestone 61** abgeschlossen.

BoringOS ist längst nicht mehr nur ein früher Boot-Kernel: Unter QEMU bootet inzwischen eine echte native Ring-3-Desktop-Session mit eigenem Display-Service, Tiling-Window-Manager, grafischem Terminal, Shell, Editor, Dateimanager, persistentem BoringFS, Hardware-Inventar und einem wachsenden BoringOS-eigenen xHCI-/USB-Stack.

Es bleibt ein experimentelles Forschungs-/Lernbetriebssystem. QEMU ist weiterhin die primäre automatisierte Regression-Plattform, aber die physische Validierung ist nicht länger hypothetisch: Am **03.09.2026** bootete der exakte M61-Physical-Kandidat `ded342e76a35d9c4558d17dda919575f5fe329ee` auf der echten Maschine **Cthulhu** vom M61-USB-Image und zeichnete die erste absichtliche BoringOS-Framebuffer-Ausgabe auf echter Hardware. Das Board erreichte **POST 91** und bewies damit, dass der erste normale Framebuffer-Store erfolgreich zurückkehrte.

Das physische Boot-Dashboard zeigte **CPU-Inventar, PCI-Inventar, SMBIOS, PMM, VMM, Kernel-Heap, Exceptions, Input, IRQ, PIT, xHCI-Controller, USB-Adressierung, USB-Deskriptoren, USB HID, USB Mass Storage, persistente BoringFS-Root, `boring-init`, `boring-display` und BoringWM** als `[ OK ]`. Das automatische Terminal und die finale Desktop-Present-Stufe sind physisch noch nicht abgeschlossen; deshalb wird bewusst noch kein vollständiger physischer Desktop-Erfolg behauptet.

Die detaillierte Quelle der Wahrheit ist [`docs/roadmap.md`](docs/roadmap.md).

## Erste physische Framebuffer-Ausgabe

Der Cthulhu-Lauf im September 2026 ist die erste bestätigte absichtliche BoringOS-Grafikausgabe auf echter Hardware seit Beginn des Projekts.

Der physische Kandidat verlässt sich für normale Scanout-Writes nicht mehr auf den geerbten Limine-Framebuffer-Alias. Er löst die Framebuffer-Apertur über den Limine-HHDM-/Memory-Map-Vertrag auf, validiert den physischen Framebuffer-Bereich, mappt einen begrenzten schreibbaren cache-deaktivierten Kernel-Alias und lässt den bestehenden Software-Rendering-Pfad unverändert. Die eingefrorenen M61-Breadcrumbs geben `90` unmittelbar vor dem ersten normalen Framebuffer-Store und `91` erst nach dessen erfolgreicher Rückkehr aus; Cthulhu erreichte physisch `91`, während das Boot-Dashboard sichtbar auf dem Monitor stand.

Das beweist den echten Framebuffer-Pfad, nicht native GPU-Beschleunigung. BoringOS verwendet weiterhin vom Firmware-/Limine-Pfad bereitgestellten Scanout-Speicher und Software-Komposition.

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
VFS + RAMFS + VirtIO-/AHCI-Block + BoringFS
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
- VFS, veränderbares RAMFS, schreibbares persistentes BoringFS, generisches Block-I/O, VirtIO und begrenzter synchroner AHCI-/SATA-Storage
- PID 1 `boring-init`, native `boring-shell`, Prozess-Lifecycle, PTYs, File Descriptors/stdio und Scheduler-eigenes `SPAWN`
- anonymer Ring-3-Speicher, kleiner Userspace-Heap, Shared Buffer, native Service Registry und IPC
- Framebuffer-Ownership/Presentation, natives `boring-display`, Software-Komposition und Cursor
- nativer C-**BoringWM** mit begrenztem Tiling, Fokus, Reorder und Lifecycle-Behandlung
- natives grafisches **BoringTerminal**, **BoringEdit**, **BoringFiles**, `/bin/boringfetch` und `/bin/cat`
- echtes CPUID-, PCI- und SMBIOS-Plattforminventar, das `boringfetch` ohne erfundene Hardwarewerte anzeigt
- begrenztes xHCI-Controller-Ownership, USB-Geräteadressierung, Descriptor Discovery, `SET_CONFIGURATION`, HID-Interrupt-IN-Endpoint-Setup, echte Interrupt-IN-Transfer-Events, Integration in die kanonische Inputqueue und ein vollständiger i8042-freier grafischer Desktop-Beweis bis Milestone 54
- begrenzte 32-GiB-PMM-Kapazität mit realem QEMU-Speicher oberhalb 4 GiB
- ein schreibgeschützter M59-Physical-Smoke-Bootpfad samt exaktem UEFI-USB-Image-Kandidaten und deaktivierten internen Storage-Writes
- begrenzter xHCI-USB-Mass-Storage-Bulk/BOT/SCSI-Transport als `usb0` mit echtem Read/Write/Cache-Flush-Persistenzbeweis und gleichzeitiger USB-HID-Koexistenz bis M60
- ein begrenztes GPT-/UEFI-Image, das vom selben `usb0`-Gerät bootet und dessen feste BoringFS-Slice als Root mountet, mit Zwei-Boot-Persistenz und live HID-Koexistenz bis M61
- physischer Cthulhu-Boot über genau diesen M61-USB-Pfad durch echtes xHCI/USB Mass Storage, schreibbare BoringFS-Root, `boring-init`, `boring-display`, BoringWM und die erste bestätigte physische BoringOS-Framebuffer-Ausgabe

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

`boringfetch` verwendet diese Kernel-eigenen Werte. Unter QEMU zeigt es deshalb die emulierte QEMU-Hardware. Der physische Cthulhu-Pfad führt die echten CPU-/PCI-/SMBIOS-Collector inzwischen ebenfalls während des Boots aus; vollständiges physisches `boringfetch` hängt noch vom Abschluss der Automatic-Terminal-Stufe ab.

Milestone 58 entfernt die alte ~4-GiB-Entwicklungsgrenze des PMM für das begrenzte Referenzziel. Der bestehende PMM besitzt jetzt Kapazität für **8.388.608 4-KiB-Frames = 32 GiB**, und eine reale QEMU-`-m 32G`-Acceptance beweist nutzbaren Speicher oberhalb 4 GiB sowie einen verwalteten physischen Frame bei `0x0000000100000000`. Maps jenseits der konfigurierten 32-GiB-PMM-Kapazität bleiben bewusst begrenzt/gecappt; daraus wird kein beliebig skalierender Memory-Support abgeleitet.

Milestone 59 ergänzt den begrenzten Physical-Smoke-Kandidaten: Das exakte Image wurde unter OVMF als schreibgeschützter xHCI-USB-Massenspeicher gebootet, während der Gast PMM/VMM/Heap, Framebuffer, Hardware-Inventar und USB-HID-Input prüft, ohne den normalen Block-Root-Pfad zu betreten. Interne Storage-Writes sind ausdrücklich deaktiviert. Die spätere physische M61-Arbeit hat dieses Smoke-Ziel auf Cthulhu inzwischen deutlich überschritten: Die Maschine bootet über den echten USB-Root-Pfad, mountet schreibbares BoringFS, startet Display-Stack und BoringWM und rendert das native Boot-Dashboard über das normalisierte physische Framebuffer-Mapping.

## xHCI-/USB-Stand

Der moderne USB-Pfad ist inzwischen deutlich weiter als reine PCI-Erkennung.

Milestones 48–61 liefern aktuell:

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
        ↓
kanonische BoringOS-Inputqueue
        ↓
vollständige i8042-freie grafische Desktop-Acceptance
```

M59 verwendet den HID-Pfad für einen Storage-Write-deaktivierten Physical-Smoke-Kandidaten. M60 ergänzt auf demselben xHCI-Controller genau ein begrenztes direkt angeschlossenes USB-Mass-Storage-Gerät: aus Deskriptoren abgeleitete Klasse/Subklasse/Protokoll `08/06/50`, aus Deskriptoren abgeleitete Bulk-IN/OUT-Endpunkte, xHCI-Bulk-Transfers, BOT-CBW/CSW-Validierung und ein One-LUN-SCSI-Subset mit INQUIRY, TEST UNIT READY, REQUEST-SENSE-Fallback, READ CAPACITY(10), READ(10), WRITE(10) und SYNCHRONIZE CACHE(10). Das Gerät wird über die bestehende M21-Block-Device-API als `usb0` registriert. Reale q35-Acceptance beweist exakte LBA-8-Persistenz bei unveränderten Bytes außerhalb dieses Sektors, während USB-Tastatur-/Tablet-Input nach Storage-I/O weiter funktioniert. USB-Hubs, SuperSpeed-Companion-Semantik, BOT-Reset/Stall-Recovery, Dateisystem-Mounting und Root-from-USB bleiben außerhalb M60.

M61 ergänzt ein 96-MiB-GPT-/UEFI-Image mit festem Layout, dessen Limine-Bootdateien und beschreibbare BoringFS-Root auf demselben xHCI-USB-Gerät liegen. Zwei aufeinanderfolgende QEMU-Boots exakt dieses Images beweisen ein geflushtes `/persist/m61.txt`, exakte hostseitige Bytes, die Wiederanzeige im Terminal, live Tastatur-/Tablet-Input und unveränderte geschützte Bootbereiche. Der physische Cthulhu-Lauf beweist zusätzlich, dass die echte Maschine über xHCI-Controller-Initialisierung, USB-Adressierung, Descriptor Discovery, USB HID, USB Mass Storage und den persistenten BoringFS-Root-Mount dieses Pfads kommt. Das ist ein begrenztes USB-Root-Image, kein Installer und kein allgemeiner Partitions-Discovery-Pfad.

## Storage und Dateisysteme

BoringOS besitzt mit **BoringFS** ein eigenes kleines Dateisystemformat. Das Repository enthält Codec/Validator, deterministischen Formatter, `boringfsck`, Kernel-Mount-Support und synchrones schreibbares Verhalten.

Die verifizierte persistente QEMU-Root kann modernen VirtIO-PCI-Block-Storage, den in M57 abgeschlossenen begrenzten synchronen AHCI-/SATA-Pfad oder die feste M61-BoringFS-Slice auf `usb0` verwenden. AHCI- und USB-Pfad führen echte Reads, Writes und erforderliche Cache-Flushes über die generische Block-Device-API aus; NVMe ist nicht implementiert.

Auf Cthulhu bestätigt das physische M61-Boot-Dashboard inzwischen den echten USB-Mass-Storage-Pfad und den persistenten BoringFS-Root-Mount, bevor Display-Stack und BoringWM starten.

## Aktuelle Grenzen

| Bereich | Aktueller Stand |
| --- | --- |
| Primär verifizierte Plattform | QEMU x86_64 für vollständige automatisierte Acceptance; physisches Cthulhu ist inzwischen bis BoringWM und nativer Framebuffer-Bootausgabe verifiziert |
| Physische Hardware | M61-USB-Boot auf Cthulhu physisch durch PMM/VMM, xHCI/HID/Mass Storage, BoringFS-Root, `boring-init`, `boring-display`, BoringWM und erste echte Framebuffer-Ausgabe verifiziert; Automatic Terminal/finales Desktop Present noch ausstehend |
| Verwalteter RAM | begrenzte 32-GiB-PMM-Kapazität; reale 32-GiB-QEMU-Maps und Frames >= 4 GiB sind verifiziert |
| Grafik | Firmware-/Limine-Framebuffer + Software-Compositor; echte physische Cthulhu-Framebuffer-Ausgabe verifiziert; keine native AMD/NVIDIA/Intel-GPU-Beschleunigung |
| Desktop-Input | xHCI-USB-HID-Pfad und Legacy-i8042-/PS/2-Pfad sind jeweils in begrenzten QEMU-Acceptances bewiesen; xHCI-HID-Initialisierung wird auch physisch auf Cthulhu erreicht |
| USB | xHCI HID plus begrenzter direkt angeschlossener Bulk/BOT/SCSI-Mass-Storage und Boot-/Root-Nutzung mit festem Layout bis M61; entsprechender physischer Cthulhu-Pfad bis zum persistenten Root-Mount verifiziert; Hubs und SuperSpeed-Companion-Semantik nicht implementiert |
| Persistente Root | VirtIO, AHCI/SATA oder feste M61-`usb0`-Slice + BoringFS; die M61-`usb0`-BoringFS-Root wird jetzt physisch auf Cthulhu erreicht |
| AHCI / NVMe | begrenztes synchrones AHCI Read/Write/Flush; NVMe nicht implementiert |
| Networking | nicht implementiert |
| Audio | nicht implementiert |
| SMP-Runtime | nicht implementiert; Runtime bleibt bewusst begrenzt |
| POSIX-Kompatibilität | weder Ziel noch aktueller Vertrag |
| Installer / polierter Live-USB | nicht implementiert; M61 veröffentlicht ein begrenztes bootfähiges/persistentes USB-Image mit festem Layout |

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

Zum Zeitpunkt dieser README-Aktualisierung ist **M61 bootfähige persistente USB-Root abgeschlossen** und der aktive Entwicklungsbanner lautet **BoringKernel 0.0.62-dev**. Die physische M61-Validierung auf Cthulhu ist jetzt durch den echten USB-Root-Pfad, BoringWM-Start und die erste absichtliche native Framebuffer-Ausgabe bestätigt. Das automatische Terminal und die finale physische Desktop-Present-Stufe sind noch nicht abgeschlossen. Es ist keine M62-Implementierung enthalten.

Für den exakten aktuellen Milestone-Stand gilt [`docs/roadmap.md`](docs/roadmap.md), nicht eine geplante Funktion aus dieser README.

## BoringWM

BoringOS enthält eine eigene native C-Implementierung von BoringWM für den BoringOS-Display-/IPC-Stack.

Das separate Repository [`dennishilk/boringwm`](https://github.com/dennishilk/boringwm) bleibt das ursprüngliche Rust-/X11-Projekt und eine Verhaltensreferenz. Es ist **keine** BoringOS-Abhängigkeit und kein Submodule.

## Prinzipien

BoringOS bevorzugt kleine Module, explizite Schnittstellen, vorhersehbares Verhalten, lesbares C, begrenzte Datenstrukturen, testbare/auditierbare Komponenten, strikte Diagnostik, minimale Abhängigkeiten und präzise Aussagen darüber, was funktioniert und was nicht.

Das Projekt soll für einen entschlossenen einzelnen Entwickler verständlich bleiben.

## Lizenz

BoringOS steht unter der [MIT License](LICENSE).