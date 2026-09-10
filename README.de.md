# BoringOS

[English](README.md)

BoringOS ist ein experimentelles **eigenständiges Desktop-Betriebssystem** für x86_64, das von Grund auf um den eigenen Kernel **BoringKernel** gebaut wird.

Es ist **kein Linux**, **kein BSD** und verwendet keinen Kernel eines anderen Betriebssystems. Die von BoringOS entwickelten Systemkomponenten sind hauptsächlich in **C** geschrieben; kleine isolierte x86_64-Assembly-Anteile gibt es dort, wo die Architektur sie verlangt.

> boring is not a bug.  
> it's the entire operating system now.

## Aktueller Stand

```text
BoringKernel 0.0.62-dev
M66 physische USB/HID-Basis + physisch bewiesener Low-Latency-Cursor-/Fokuspfad
```

Die aktuelle physische Grundlage ist auf der echten Workstation **Cthulhu** bewiesen. BoringOS bootet sein natives schreibbares USB-System, verwaltet die relevanten xHCI-Controller unabhängig, enumeriert den echten Genesys-Logic-USB-Hub, nimmt die daran angeschlossene ROCCAT-Maus über den nativen xHCI-/HID-Pfad an, hält die direkte Holtek-USB-Tastatur funktionsfähig und betreibt den nativen BoringWM-Desktop mit BoringTerminal, BoringEdit und BoringFiles.

Die autoritative M66-USB/HID-Recovery-Basis bleibt eingefroren unter:

```text
freeze/m66-physical-hid-recovery-2026-09-08
bd3f181d24547189b004328aea54c54fd194fc96
TREE: 96f5b177a5afa8ea24c9f94168117ce09c1c8507
Artifact: 10055706794
RAW: 100663296 Bytes
SHA256: 83e22073a408fa45474b409804c4b29932b4451ce68ec87c8d21681d948858f8
```

### Physische Cursor-/Fokus-Reaktionszeit — abgenommen am 10.09.2026

Nachdem die M66-Basis stabil war, ließ sich das verbleibende physische Mausproblem auf den Software-Present-Pfad statt auf USB/HID eingrenzen. Eine normale Pointer-Bewegung löste zuvor einen vollständigen 800x600-Software-Compose samt vollständiger Framebuffer-Kopie aus. Bei Pointer-Fokuswechseln wurden außerdem synchron unveränderte `CONFIGURE`-Nachrichten an Clients geschickt; die nativen Clients antworteten mit `COMMIT`, was indirekt zusätzliche Full-Frame-Presents auslöste und weitere Mausbewegungen kurz blockierte.

Der abgenommene Fix lässt USB/HID, Pointer-Semantik, GOP-Modus und Auflösung unverändert. Normale Cursorbewegungen restaurieren und präsentieren nur noch die begrenzten alten/neuen 6x12-Cursorregionen. Pointer-erzeugte Fokuswechsel bestätigen Input zuerst, aktualisieren Fokus-Metadaten atomar, vermeiden unveränderte Client-`CONFIGURE`-Roundtrips, fassen ausstehende Fokus-Paints auf den neuesten Zustand zusammen und präsentieren die begrenzten Fokusrahmen-Regionen erst dann, wenn Input-/IPC-Arbeit frei ist.

Auf dem physischen Cthulhu sind damit beide beobachteten Latenzformen verschwunden: Normale Cursorbewegung reagiert jetzt so direkt wie auf dem Sway-Desktop des Nutzers, und beim kontinuierlichen Kreuzen zwischen Fenstern bleibt der Pointer nicht mehr an der Fenstergrenze stehen, während der Fokusrahmen gezeichnet wird.

Der exakte physisch akzeptierte Runtime-Stand ist eingefroren unter:

```text
freeze/mouse-present-latency-physical-2026-09-10
c5ded88e3d473162b94f963d9509394246ef782c
TREE: 8a8c01fa61670f8085d8259c7635072d7317f36e
Artifact: 10154738768
RAW: 100663296 Bytes
SHA256: 6ac9e6096849ea4e906f0c70a322a8599de163bec34499bdd8794f6b202ce26f
```

QEMU bleibt die automatisierte Regression-Plattform; USB-Topologie, Hub-verbundene Maus, Pointer-Fokus sowie der Low-Latency-Cursor-/Fokus-Present-Pfad sind zusätzlich physisch auf Cthulhu bewiesen.

## Was heute wirklich läuft

```text
UEFI / QEMU oder physischer Cthulhu
        ↓
      Limine
        ↓
   BoringKernel
        ↓
PMM / VMM / Heap / IDT / Scheduler
        ↓
dynamische Prozess- + Task-Objekte
        ↓
Ring 3 + natives SYSCALL/SYSRETQ-ABI
        ↓
VFS + schreibbares BoringFS
        ↓
Multi-xHCI / USB-Hubs / HID / USB Mass Storage
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

Zu den implementierten Grundlagen gehören physische und virtuelle Speicherverwaltung, Kernel-Heap, Exceptions, PIC/PIT, kooperatives und präemptives Scheduling, unabhängige Prozessadressräume, Ring 3, native Syscalls, ELF64-Userspace, PTYs, File Descriptors, VFS, BoringFS, natives IPC, Shared Buffer, Display-Service, Software-Komposition, BoringWM, native Anwendungen, CPUID/PCI/SMBIOS-Inventar, xHCI-HID, USB Mass Storage, AHCI/SATA-Storage und ACPI-System-Power-Control.

## Nativer Desktop

Der grafische Desktop gehört vollständig BoringOS. Darunter gibt es weder X11 noch Wayland.

Aktuelle Tastenkürzel:

```text
Super+Return   BoringTerminal öffnen
Super+E        BoringEdit öffnen
Super+F        BoringFiles öffnen
Super+J/L      Fokus wechseln
Super+Q        fokussierten Client schließen
```

Der Desktop bleibt absichtlich auch mit **null offenen Fenstern** aktiv. Wird die letzte Anwendung geschlossen, bleibt der leere BoringWM-Desktop bestehen und Anwendungen können anschließend erneut gestartet werden.

BoringTerminal startet eine separat geplante `boring-shell` über ein echtes PTY. BoringEdit lädt und speichert über BoringFS. BoringFiles liest echte VFS-/BoringFS-Verzeichnisse.

## Shell und System-Lifecycle

Die native Shell besitzt Dateisystem-, Identitäts-/Prozess- und Power-Kommandos, darunter:

```text
ls cd pwd mkdir rmdir touch write rm
clear echo history help
uname hostname whoami ps
reboot shutdown
```

Die physische M63-Abnahme bewies einen echten schreibbaren USB-Root-Lifecycle auf Cthulhu: Dateien anlegen und schreiben, rebooten, erneut booten, persistierte Daten lesen und anschließend sauber über ACPI S5 ausschalten. `reboot` und `shutdown` synchronisieren zuerst registrierte schreibbare Blockgeräte; keines der beiden Kommandos ist als QEMU-only Magic-Port implementiert.

## Prozess- und Desktop-Kapazität

M62 hat die alten statischen Prozess-/Task-Arrays aus der Runtime-Architektur entfernt.

```text
Prozess-Policy-Limit: 64
Task-Policy-Limit:    64
BoringWM-Clients:     16
WM-IPC-Peers:         16
Display-IPC-Peers:    16
```

Auf Cthulhu wurden physisch **sieben BoringTerminal-Fenster plus BoringEdit und BoringFiles** geöffnet, alles geschlossen, zum leeren Desktop zurückgekehrt und Anwendungen erneut gestartet — ohne Capacity- oder Lifecycle-Fehler. Das sind Policy-Grenzen und keine fest verdrahteten Architektur-Slots.

## Physische Cthulhu-Hardware

```text
CPU:       AMD Ryzen 7 5800X3D
Board:     Gigabyte B550 VISION D
Memory:    32 GiB installiert, über SMBIOS erkannt
Firmware:  AMI / Gigabyte F18d
Display:   aktueller Firmware-Framebuffer 800x600x32, Pitch 3328
```

Der bewiesene native Input-Pfad ist:

```text
xHCI-Controller
→ USB-Root / Genesys-Logic-Hub
→ HID Interrupt-IN
→ BoringOS-Inputqueue
→ boring-display
→ BoringWM
→ native Shortcuts / Software-Cursor / Pointer-Fokus
```

## USB und Storage

BoringOS unterstützt begrenzte Multi-xHCI-Controller-Ownership, Root-Port- und Downstream-Hub-Topologie, Descriptor Discovery, HID Interrupt-IN und USB Mass Storage über Bulk/BOT/SCSI.

Der physische USB-Root besitzt einen strengen Durability-Pfad. Normale Geräte verwenden SCSI `SYNCHRONIZE CACHE(10)`. Meldet ein Gerät exakt den erwarteten SCSI-Sense-Nachweis dafür, dass dieses Kommando nicht unterstützt wird, schaltet BoringOS auf `WRITE(10)` mit FUA um, statt Flushfehler pauschal zu ignorieren. Andere Transport-, CSW-, Sense- oder FUA-Fehler bleiben harte I/O-Fehler.

Weitere verifizierte Storage-Pfade sind VirtIO Block und begrenztes synchrones AHCI/SATA.

## Grafik heute

BoringOS verwendet aktuell einen **softwaregerenderten Framebuffer-Desktop**.

```text
BoringWM / boring-display Komposition im RAM
        ↓
begrenzte Cursor-/Fokus-Region-Presents, wo möglich
oder Full-Software-Present bei Scene-/Layout-/Client-Änderungen
        ↓
Firmware-/Limine-Framebuffer
        ↓
GPU-Scanout zum Monitor
```

Der physisch bewiesene Cursor-Fast-Path präsentiert pro normaler Bewegung höchstens die geclippten alten/neuen 6x12-Cursorrechtecke statt jedes Mal alle 480.000 Pixel neu zu schreiben. Reine Pointer-Fokuswechsel verwenden begrenzte Border-Region-Presents und werden hinter bereits bereiter Input-/IPC-Arbeit zurückgestellt. Vollständige Scene-Änderungen, Fenstergeometrie, Wallpaper-Änderungen und Client-Commits behalten weiterhin den etablierten Full-Software-Compose-Pfad.

Es gibt noch keinen nativen AMD-/NVIDIA-/Intel-Modesetting- oder Beschleunigungstreiber. Das nächste sichtbare Grafikziel ist ein besserer bzw. nativer GOP-Framebuffer-Modus; breiteres allgemeines Damage-/Present-Polish folgt später, ein eigener AMD-Treiber ist deutlich spätere Arbeit.

## Aktuelle Grenzen

- gehaltenes Tastatur-Typematic/Key-Repeat auf der physischen USB-Tastatur ist weiterhin ungelöst und bewusst für späteres Input-Polish geparkt;
- das aktuelle Tastaturlayout ist praktisch noch ENG/US und noch kein fertiges DE-Layout;
- das physische Display nutzt aktuell den Firmware-Framebuffer mit 800x600;
- die vollständige praktische Nutzung der installierten 32 GiB auf Cthulhu ist noch zukünftige Arbeit;
- noch kein Netzwerk, Audio, NVMe, SMP-Runtime oder nativer GPU-Treiber.

Das sind Implementierungsgrenzen und keine als Support verkleideten Versprechen.

## Roadmap ab der physischen M66-Basis

```text
M66 PHYSICAL USB/HID FREEZE
    ↓
Low-Latency-Cursor + Pointer-Fokus  ✅ physisch auf Cthulhu bewiesen
    ↓
bessere / native GOP-Auflösung
    ↓
breitere Software-Grafik / Damage / Present
    ↓
die vollen 32 GiB RAM physisch sinnvoll nutzen
    ↓
Networking / NVMe / Audio / SMP
    ↓
irgendwann: eigener AMD-Grafiktreiber

geparktes Input-Polish:
- gehaltenes Typematic / Key-Repeat
- richtiges DE-Tastaturlayout
```

Die detaillierte historische Aufzeichnung liegt in [docs/roadmap.md](docs/roadmap.md).

## Bauen und testen

Der Build verwendet GCC/binutils als freestanding x86_64-Toolchain und eine fest gepinnte Limine-Version.

```sh
make
make run
make test
```

Die GitHub-Actions-Workflows halten bewusst frühere Milestone-Regressionen am Leben. Sie sind Testabdeckung und keine aktiven Entwicklungsbranches.

## Eingefrorene physische Baselines

Das Repository behält nur wenige immutable-by-policy physische Freeze-Branches:

- `freeze/m61-physical-desktop-2026-09-04`
- `freeze/m62-dynamic-capacity-physical-2026-09-05`
- `freeze/m63-system-power-lifecycle-physical-2026-09-05`
- `freeze/m66-physical-hid-recovery-2026-09-08`
- `freeze/mouse-present-latency-physical-2026-09-10`

Normale Entwicklung läuft von `main` weiter; Freeze-Branches sind Referenzpunkte und dürfen nicht bewegt werden.

## BoringWM

BoringOS enthält eine eigene native C-Implementierung von BoringWM für den BoringOS-Display-/IPC-Stack.

Das separate Repository [dennishilk/boringwm](https://github.com/dennishilk/boringwm) ist das ursprüngliche Rust-/X11-Projekt und eine Verhaltensreferenz. Es ist keine BoringOS-Abhängigkeit.

## Lizenz

BoringOS steht unter der [MIT License](LICENSE).
