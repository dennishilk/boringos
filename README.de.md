# BoringOS

[English](README.md)

BoringOS ist ein experimentelles **eigenständiges Desktop-Betriebssystem** für x86_64, das von Grund auf um den eigenen Kernel **BoringKernel** gebaut wird.

Es ist **kein Linux**, **kein BSD** und verwendet keinen Kernel eines anderen Betriebssystems. Die von BoringOS entwickelten Systemkomponenten sind hauptsächlich in **C** geschrieben; kleine isolierte x86_64-Assembly-Anteile gibt es dort, wo die Architektur sie verlangt.

> boring is not a bug.  
> it's the entire operating system now.

## Aktueller Stand

~~~text
BoringKernel 0.0.62-dev
Milestone 63 · physische System-Power-Basis
~~~

Am **05.09.2026** hat BoringOS auf der echten Maschine **Cthulhu** einen neuen physischen Basisstand erreicht:

- der native BoringWM-Desktop bootet vom schreibbaren USB-Image;
- echte USB-Tastatureingaben laufen über den BoringOS-eigenen xHCI/HID-Pfad;
- BoringTerminal, BoringEdit und BoringFiles laufen auf echter Hardware;
- die dynamische Prozess-/Task-Kapazität ist physisch deutlich über der alten Acht-Slot-Bootstrap-Grenze bewiesen;
- BoringFS erstellt und persistiert Dateien auf dem physischen USB-Root;
- `reboot` führt einen echten Maschinen-Reset aus und BoringOS bootet danach erneut;
- `shutdown` führt einen echten ACPI-S5-Poweroff aus;
- vor dem Reboot geschriebene Daten überleben den vollständigen Power-Lifecycle.

Der physisch akzeptierte Runtime-Stand ist eingefroren unter:

~~~text
freeze/m63-system-power-lifecycle-physical-2026-09-05
799d1e6529b8eafead37acc340f3fd18dbb2d655
~~~

Autoritatives physisches Image:

~~~text
100663296 Bytes
SHA256: 457535a2d27d98e489868a9d33cf8e8c2e2c83de13fc659bcea30e937c9018ab
~~~

QEMU bleibt die automatisierte Regression-Plattform, aber Desktop, persistenter Storage, Reboot und Shutdown sind jetzt zusätzlich auf echter Hardware bewiesen.

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
xHCI USB Mass Storage / AHCI / VirtIO
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

Cthulhu besitzt mehrere xHCI-Controller. Die aktuelle Runtime besitzt weiterhin nur eine Controller-Instanz.

Eine direkt angeschlossene Holtek-USB-Tastatur ist physisch durch den ganzen Pfad bewiesen:

~~~text
xHCI Interrupt-IN
→ HID-Decoding
→ BoringOS-Inputqueue
→ boring-display
→ BoringWM
→ native Shortcuts / Anwendungen
~~~

## USB und Storage

BoringOS unterstützt xHCI-Controller-Bring-up, direkte Root-Port-Adressierung, Descriptor Discovery, HID Interrupt-IN und USB Mass Storage über Bulk/BOT/SCSI.

Der physische USB-Root besitzt jetzt einen strengen Durability-Pfad. Normale Geräte verwenden SCSI `SYNCHRONIZE CACHE(10)`. Meldet ein Gerät exakt den erwarteten SCSI-Sense-Nachweis dafür, dass dieses Kommando nicht unterstützt wird, schaltet BoringOS auf `WRITE(10)` mit FUA um, statt Flushfehler pauschal zu ignorieren. Andere Transport-, CSW-, Sense- oder FUA-Fehler bleiben harte I/O-Fehler.

Die physische M63-Abnahme hat schreibbares BoringFS, Persistenz über einen Reboot und anschließenden sauberen Poweroff auf dem echten SanDisk-USB-Gerät bewiesen.

Weitere verifizierte Storage-Pfade sind VirtIO Block und begrenztes synchrones AHCI/SATA.

## USB-Grenze / nächste Hardware-Arbeit

Die nächste physische USB-Arbeit bleibt bewusst eng:

1. **mehrere xHCI-Controller** statt nur des ersten kontrollierten Controllers unterstützen;
2. **USB-Hub-Enumeration** ergänzen;
3. danach die physische Maus in ihrer echten Hub-Topologie validieren;
4. HID-Report-Support nur dann erweitern, wenn das reale Gerät ihn tatsächlich benötigt.

Die aktuelle ROCCAT-Maus ist noch kein BoringOS-Erfolgsclaim. In der getesteten Verkabelung hängt sie hinter einem Genesys-Logic-Hub, den BoringOS bisher nicht enumeriert.

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

- nur eine kontrollierte xHCI-Controller-Instanz;
- noch keine USB-Hub-Enumeration;
- die physische Maus hinter dem Hub ist noch nicht unterstützt;
- das physische Display nutzt aktuell den Firmware-Framebuffer mit 800x600;
- die vollständige praktische Nutzung der installierten 32 GiB auf Cthulhu ist noch zukünftige Arbeit;
- noch kein Netzwerk, Audio, NVMe, SMP-Runtime oder nativer GPU-Treiber.

Das sind Implementierungsgrenzen und keine als Support verkleideten Versprechen.

## Roadmap ab dem M63-Freeze

~~~text
M63 PHYSICAL FREEZE
    ↓
1. USB Multi-xHCI-Ownership
    ↓
2. USB-Hub-Enumeration + physische Maus
    ↓
3. bessere / native GOP-Auflösung
    ↓
4. schnellere Software-Grafik / Present
    ↓
5. die vollen 32 GiB RAM physisch sinnvoll nutzen
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

Normale Entwicklung läuft von `main` weiter; Freeze-Branches sind Referenzpunkte und dürfen nicht bewegt werden.

## BoringWM

BoringOS enthält eine eigene native C-Implementierung von BoringWM für den BoringOS-Display-/IPC-Stack.

Das separate Repository [dennishilk/boringwm](https://github.com/dennishilk/boringwm) ist das ursprüngliche Rust-/X11-Projekt und eine Verhaltensreferenz. Es ist keine BoringOS-Abhängigkeit.

## Lizenz

BoringOS steht unter der [MIT License](LICENSE).
