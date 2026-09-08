# BoringOS

[English](README.md)

BoringOS ist ein experimentelles **eigenständiges Desktop-Betriebssystem** für x86_64, das von Grund auf um den eigenen Kernel **BoringKernel** gebaut wird.

Es ist **kein Linux**, **kein BSD** und verwendet keinen Kernel eines anderen Betriebssystems. Die von BoringOS entwickelten Systemkomponenten sind hauptsächlich in **C** geschrieben; kleine isolierte x86_64-Assembly-Anteile gibt es dort, wo die Architektur sie verlangt.

> boring is not a bug.  
> it's the entire operating system now.

## Aktueller Stand

~~~text
BoringKernel 0.0.62-dev
Milestone 66 · physische Multi-xHCI-/USB-Hub-/HID-Recovery-Basis
~~~

Am **08.09.2026** hat BoringOS nach erneuter physischer Revalidierung der M66-Artefakte auf der echten Maschine **Cthulhu** einen korrigierten physischen USB-Basisstand festgelegt:

- der native BoringWM-Desktop bootet weiterhin vom schreibbaren USB-Image;
- BoringOS besitzt die relevanten xHCI-Controller unabhängig voneinander statt nur eine globale Controller-Instanz anzunehmen;
- der echte Genesys-Logic-USB-Hub wird über begrenzte Hub-Class-Power-/Status-/Reset-Pfade enumeriert;
- die echte ROCCAT-Maus hinter diesem Hub erreicht BoringOS über den nativen xHCI-/HID-Pfad;
- gültige Composite-/Generic-HID-Konfigurationen werden nicht mehr allein wegen legitimer Schwester-Endpunkte fälschlich als fatal ungültig klassifiziert; BoringOS behält den nutzbaren Interrupt-IN-Kandidaten, wertet den begrenzten Maus-Report-Descriptor aus und aktiviert danach den nativen Input-Pfad;
- physische Mausbewegung bewegt den bestehenden Software-Cursor und wechselt den BoringWM-Fokus zwischen zwei laufenden Fenstern, sobald der Zeiger darüber fährt;
- die echte USB-Tastatur funktioniert im selben Boot weiterhin;
- die bereits eingefrorene M63-Basis bleibt der physische Nachweis für persistentes BoringFS, Reboot und ACPI S5.

Der physisch akzeptierte Runtime-Stand ist eingefroren unter:

~~~text
freeze/m66-physical-hid-recovery-2026-09-08
bd3f181d24547189b004328aea54c54fd194fc96
TREE: 96f5b177a5afa8ea24c9f94168117ce09c1c8507
~~~

Autoritatives physisches Image:

~~~text
100663296 Bytes
SHA256: 83e22073a408fa45474b409804c4b29932b4451ce68ec87c8d21681d948858f8
Artifact: 10055706794
~~~

QEMU bleibt die automatisierte Regression-Plattform; Multi-Controller-xHCI, echte Hub-Enumeration und der Hub-verbundene physische Mauspfad sind jetzt zusätzlich auf Cthulhu bewiesen. Die frühere Zuordnung `freeze/m66-usb-hub-mouse-physical-2026-09-06` / `8ccd618d...` bleibt nur als historische Evidenz erhalten: Beim erneuten physischen Test trat ein schwarzer Bildschirm mit finalem POST `D8` auf, daher ist dieser Stand keine autoritative physisch gute Basis. Der Recovery-Freeze vom 08.09.2026 oben ist der maßgebliche physische M66-Anker. M63 bleibt die akzeptierte physische Durability-/Reboot-/Shutdown-Basis.

## Was heute wirklich läuft

~~~text
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
~~~

Zu den implementierten Grundlagen gehören physische und virtuelle Speicherverwaltung, Kernel-Heap, Exceptions, PIC/PIT, kooperatives und präemptives Scheduling, unabhängige Prozessadressräume, Ring 3, native Syscalls, ELF64-Userspace, PTYs, File Descriptors, VFS, BoringFS, natives IPC, Shared Buffer, Display-Service, Software-Komposition, BoringWM, native Anwendungen, CPUID/PCI/SMBIOS-Inventar, xHCI-HID, USB Mass Storage, AHCI/SATA-Storage und ACPI-System-Power-Control.

## Nativer Desktop

Der grafische Desktop gehört vollständig BoringOS. Darunter gibt es weder X11 noch Wayland.

Aktuelle Tastenkürzel:

~~~text
Super+Return   BoringTerminal öffnen
Super+E        BoringEdit öffnen
Super+F        BoringFiles öffnen
Super+J/L      Fokus wechseln
Super+Q        fokussierten Client schließen
~~~

Der Desktop bleibt absichtlich auch mit **null offenen Fenstern** aktiv. Wird die letzte Anwendung geschlossen, bleibt der leere BoringWM-Desktop bestehen und Anwendungen können anschließend erneut gestartet werden.

BoringTerminal startet eine separat geplante `boring-shell` über ein echtes PTY. BoringEdit lädt und speichert über BoringFS. BoringFiles liest echte VFS-/BoringFS-Verzeichnisse.

## Shell und System-Lifecycle

Die native Shell besitzt inzwischen Dateisystem-, Identitäts-/Prozess- und Power-Kommandos, darunter:

~~~text
ls cd pwd mkdir rmdir touch write rm
clear echo history help
uname hostname whoami ps
reboot shutdown
~~~

Die physische M63-Abnahme hat auf Cthulhu diese komplette Sequenz bewiesen:

~~~text
mkdir
touch
write
reboot
erneut booten
persistierte Daten lesen
shutdown
~~~

`reboot` synchronisiert zuerst registrierte schreibbare Blockgeräte und wechselt danach in einen echten Plattform-Reset. `shutdown` synchronisiert Storage und wechselt in ACPI S5. Keines der beiden Kommandos ist als QEMU-only Magic-Port implementiert.

## Prozess- und Desktop-Kapazität

M62 hat die alten statischen Prozess-/Task-Arrays aus der Runtime-Architektur entfernt.

Aktuelle Policy-Grenzen:

~~~text
Prozess-Policy-Limit: 64
Task-Policy-Limit:    64
BoringWM-Clients:     16
WM-IPC-Peers:         16
Display-IPC-Peers:    16
~~~

Auf Cthulhu wurden physisch **sieben BoringTerminal-Fenster plus BoringEdit und BoringFiles** geöffnet, alles geschlossen, zum leeren Desktop zurückgekehrt und Anwendungen erneut gestartet — ohne Capacity- oder Lifecycle-Fehler.

Das sind Policy-Grenzen und keine fest verdrahteten Architektur-Slots.

## Physische Cthulhu-Hardware

~~~text
CPU:       AMD Ryzen 7 5800X3D
Board:     Gigabyte B550 VISION D
Memory:    32 GiB installiert, über SMBIOS erkannt
Firmware:  AMI / Gigabyte F18d
Display:   aktueller Firmware-Framebuffer 800x600x32, Pitch 3328
~~~

Cthulhu besitzt mehrere xHCI-Controller; die M66-Basis besitzt die relevanten Controller-Instanzen jetzt unabhängig voneinander.

Die direkt angeschlossene Holtek-USB-Tastatur und die ROCCAT-Maus hinter dem echten Genesys-Logic-Hub sind physisch durch den nativen Pfad bewiesen:

~~~text
xHCI-Controller
→ USB-Root / Hub-Topologie
→ HID Interrupt-IN
→ BoringOS-Inputqueue
→ boring-display
→ BoringWM
→ native Shortcuts / Cursor / Pointer-Fokus
~~~

## USB und Storage

BoringOS unterstützt begrenzte Multi-xHCI-Controller-Ownership, Root-Port- und Downstream-Hub-Topologie, Descriptor Discovery, HID Interrupt-IN und USB Mass Storage über Bulk/BOT/SCSI. Der physische M66-Boot beweist den echten Genesys-Logic-Hub und die darunter angeschlossene ROCCAT-Maus auf Cthulhu.

Der physische USB-Root besitzt jetzt einen strengen Durability-Pfad. Normale Geräte verwenden SCSI `SYNCHRONIZE CACHE(10)`. Meldet ein Gerät exakt den erwarteten SCSI-Sense-Nachweis dafür, dass dieses Kommando nicht unterstützt wird, schaltet BoringOS auf `WRITE(10)` mit FUA um, statt Flushfehler pauschal zu ignorieren. Andere Transport-, CSW-, Sense- oder FUA-Fehler bleiben harte I/O-Fehler.

Die physische M63-Abnahme hat schreibbares BoringFS, Persistenz über einen Reboot und anschließenden sauberen Poweroff auf dem echten SanDisk-USB-Gerät bewiesen.

Weitere verifizierte Storage-Pfade sind VirtIO Block und begrenztes synchrones AHCI/SATA.

## Physischer USB-Stand / nächstes Input-Polish

Der frühere USB-Topologie-Blocker ist physisch geschlossen:

1. **mehrere xHCI-Controller** werden unabhängig verwaltet;
2. der echte **Genesys-Logic-Hub** wird auf Hardware enumeriert;
3. die Downstream-**ROCCAT-Maus** bewegt den BoringOS-Cursor;
4. BoringWM-Hit-Testing wechselt den Fokus physisch zwischen zwei Fenstern.

Die nächste beobachtete Input-Ecke ist Tastatur-Typematic: Eine gehaltene Taste wie Backspace erzeugt auf dem USB-Boot-Keyboard-Pfad aktuell nur die erste Aktion; für wiederholtes Löschen muss die Taste erneut gedrückt werden. Das ist ein kleines Input-Polish und keine USB-Topologie-Regression.

## Grafik heute

BoringOS verwendet aktuell einen **softwaregerenderten Framebuffer-Desktop**.

~~~text
BoringWM / boring-display Komposition im RAM
        ↓
CPU-Software-Present / Kopie
        ↓
Firmware-/Limine-Framebuffer
        ↓
GPU-Scanout zum Monitor
~~~

Es gibt noch keinen nativen AMD-/NVIDIA-/Intel-Modesetting- oder Beschleunigungstreiber. Das unmittelbare Grafikziel ist ein besserer bzw. nativer GOP-Framebuffer-Modus, danach ein schnellerer Software-Present mit Damage-/Dirty-Regionen. Ein eigener AMD-Treiber ist deutlich spätere Arbeit.

## Aktuelle Grenzen

- gehaltenes Tastatur-Typematic/Key-Repeat ist auf dem USB-Tastaturpfad noch nicht implementiert;
- das physische Display nutzt aktuell den Firmware-Framebuffer mit 800x600;
- die vollständige praktische Nutzung der installierten 32 GiB auf Cthulhu ist noch zukünftige Arbeit;
- noch kein Netzwerk, Audio, NVMe, SMP-Runtime oder nativer GPU-Treiber.

Das sind Implementierungsgrenzen und keine als Support verkleideten Versprechen.

## Roadmap ab dem M66-Freeze

~~~text
M66 PHYSICAL FREEZE
    ↓
1. Tastatur-Typematic / gehaltenes Key-Repeat
    ↓
2. bessere / native GOP-Auflösung
    ↓
3. schnellere Software-Grafik / Present
    ↓
4. die vollen 32 GiB RAM physisch sinnvoll nutzen
    ↓
...
    ↓
irgendwann: eigener AMD-Grafiktreiber
~~~

Die detaillierte historische Aufzeichnung liegt in [docs/roadmap.md](docs/roadmap.md).

## Bauen und testen

Der Build verwendet GCC/binutils als freestanding x86_64-Toolchain und eine fest gepinnte Limine-Version.

~~~sh
make
make run
make test
~~~

Die GitHub-Actions-Workflows halten bewusst frühere Milestone-Regressionen am Leben. Sie sind Testabdeckung und keine aktiven Entwicklungsbranches.

## Eingefrorene physische Baselines

Das Repository behält nur wenige immutable-by-policy physische Freeze-Branches:

- `freeze/m61-physical-desktop-2026-09-04`
- `freeze/m62-dynamic-capacity-physical-2026-09-05`
- `freeze/m63-system-power-lifecycle-physical-2026-09-05`
- `freeze/m66-physical-hid-recovery-2026-09-08`

Normale Entwicklung läuft von `main` weiter; Freeze-Branches sind Referenzpunkte und dürfen nicht bewegt werden.

## BoringWM

BoringOS enthält eine eigene native C-Implementierung von BoringWM für den BoringOS-Display-/IPC-Stack.

Das separate Repository [dennishilk/boringwm](https://github.com/dennishilk/boringwm) ist das ursprüngliche Rust-/X11-Projekt und eine Verhaltensreferenz. Es ist keine BoringOS-Abhängigkeit.

## Lizenz

BoringOS steht unter der [MIT License](LICENSE).
