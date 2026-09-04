# BoringOS

[English](README.md)

BoringOS ist ein experimentelles **eigenständiges Desktop-Betriebssystem** für x86_64, das von Grund auf um den eigenen Kernel **BoringKernel** gebaut wird.

Es ist **kein Linux**, **kein BSD** und verwendet keinen Kernel eines anderen Betriebssystems. Die von BoringOS entwickelten Systemkomponenten sind hauptsächlich in **C** geschrieben; kleine isolierte x86_64-Assembly-Anteile gibt es dort, wo die Architektur sie verlangt.

> boring is not a bug.  
> it's the entire operating system now.

## Aktueller Stand

~~~text
BoringKernel 0.0.62-dev
Milestone 61 · physischer Desktop-Meilenstein wird eingefroren
~~~

Am **04.09.2026** hat BoringOS auf der echten Maschine **Cthulhu** eine wichtige Grenze überschritten: Das M61-USB-Image bootet in einen persistenten nativen BoringWM-Desktop und verarbeitet echte USB-Tastatureingaben.

Der letzte physisch getestete Runtime-Kandidat ist:

~~~text
1e3c0e83e8e9159480782a6be624975ccbe0da3a
CI: 26 SUCCESS / 0 FAILURE
M61 RAW-Image: 100663296 Bytes
SHA256: 4ba4218ecafc04937737691f2133c8d8ed8b1bff29434048e2cdfb6f2a024947
~~~

Der physische Cthulhu-Lauf beweist inzwischen gemeinsam:

- Boot vom M61-UEFI-USB-Image;
- echtes xHCI-USB-Mass-Storage und schreibbares BoringFS-Root;
- <code>boring-init</code>, <code>boring-display</code> und nativen C-**BoringWM**;
- einen persistenten leeren Desktop, der auch nach dem Schließen des letzten Fensters weiterlebt;
- echte USB-Tastatureingaben über den BoringOS-eigenen xHCI/HID-Pfad;
- physisches <code>Super+Return</code>, <code>Super+E</code>, <code>Super+F</code> und <code>Super+Q</code>;
- mehrere gekachelte native Anwendungen, korrektes Schließen, erneutes Öffnen und Fokusverhalten;
- **BoringTerminal**, **BoringEdit** und **BoringFiles** auf echter Hardware;
- <code>boring-shell</code> und <code>boringfetch</code> im echten grafischen Terminal;
- reale CPUID-, PCI- und SMBIOS-Daten des Ryzen-7-5800X3D-/B550-VISION-D-Systems;
- echten Firmware-Framebuffer-Scanout auf dem Monitor.

QEMU bleibt die vollständige automatisierte Regression-Plattform, aber der Desktop selbst ist inzwischen kein reiner QEMU-Claim mehr.

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
Prozesse + unabhängige Adressräume
        ↓
Ring 3 + natives SYSCALL/SYSRETQ-ABI
        ↓
VFS + BoringFS + Blockgeräte
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

Zu den implementierten Grundlagen gehören physische und virtuelle Speicherverwaltung, Kernel-Heap, Exceptions, PIC/PIT, kooperatives und präemptives Scheduling, Prozessadressräume, Ring 3, native Syscalls, ELF64-Userspace, PTYs, File Descriptors, VFS, BoringFS, natives IPC, Shared Buffer, Display-Service, Software-Komposition, BoringWM, native Anwendungen, CPUID/PCI/SMBIOS-Inventar, xHCI-HID, USB Mass Storage sowie AHCI/SATA.

## Nativer Desktop

Der grafische Desktop gehört vollständig BoringOS. Darunter gibt es weder X11 noch Wayland.

Aktuelle Tastenkürzel:

~~~text
Super+Return   BoringTerminal öffnen
Super+E        BoringEdit öffnen
Super+F        BoringFiles öffnen
Super+J/L      Fokus wechseln
Super+Q        fokussierten verwalteten Client schließen
~~~

Der normale M61-Desktop bleibt absichtlich auch mit **null offenen Fenstern** aktiv. Wird die letzte Anwendung geschlossen, bleibt der leere BoringWM-Desktop bestehen und Anwendungen können anschließend erneut gestartet werden.

BoringTerminal startet eine separat geplante <code>boring-shell</code> über ein echtes PTY. BoringEdit lädt und speichert über BoringFS. BoringFiles liest echte VFS-/BoringFS-Verzeichnisse.

## Physische Cthulhu-Hardware

Die aktuelle physische Referenzmaschine umfasst:

~~~text
CPU:       AMD Ryzen 7 5800X3D
Board:     Gigabyte B550 VISION D
Memory:    32 GiB installiert, über SMBIOS erkannt
Firmware:  AMI / Gigabyte F18d
Display:   aktueller Firmware-Framebuffer 800x600x32, Pitch 3328
~~~

Cthulhu besitzt mehrere xHCI-Controller. Linux zeigt USB-Buspaare unter mindestens <code>02:00.0</code>, <code>29:00.0</code> und <code>58:00.3</code>. Der aktuelle BoringOS-Pfad besitzt weiterhin nur eine Controller-Instanz.

Eine direkt angeschlossene Holtek-USB-Tastatur am aktiven Controller ist physisch durch den gesamten Pfad bewiesen:

~~~text
xHCI Interrupt-IN
→ HID-Decoding
→ BoringOS-Inputqueue
→ boring-display
→ BoringWM
→ native Shortcuts / Anwendungen
~~~

## USB-Stand und aktuelle Grenze

Der aktuelle USB-Stack unterstützt xHCI-Controller-Bring-up, direkte Root-Port-Geräteadressierung, Descriptor Discovery, Konfiguration, HID-Interrupt-IN-Endpunkte, die im begrenzten Pfad verwendeten HID-Tastatur-/Mausformate sowie USB Mass Storage über Bulk/BOT/SCSI.

Die nächste physische USB-Arbeit ist bewusst klar:

- **mehrere xHCI-Controller** statt nur des ersten kontrollierten Controllers unterstützen;
- **USB-Hub-Enumeration** ergänzen;
- danach die physische Maus in ihrer echten Topologie validieren;
- bei Bedarf HID-Report-Support für Geräte außerhalb des Boot-HID-Falls erweitern.

Die aktuelle ROCCAT-Maus ist noch kein BoringOS-Erfolgsclaim. In der getesteten Verkabelung hing sie hinter einem Genesys-Logic-Hub, den BoringOS bisher nicht enumeriert.

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

Die AMD-GPU ist also am Scanout beteiligt, aber BoringOS programmiert die AMD-Display-Engine noch nicht selbst und sendet auch keine eigenen GPU-Rendering-Kommandos.

Es gibt derzeit keinen nativen AMD-/NVIDIA-/Intel-Modesetting- oder Beschleunigungstreiber, keinen eigenen VRAM-/GTT-Manager und keine 2D-/3D-Command-Submission. Das unmittelbare Grafikziel ist zunächst ein besserer bzw. nativer GOP-Framebuffer-Modus, danach ein schnellerer Software-Present mit Damage-/Dirty-Regionen. Ein eigener AMD-Treiber ist ein deutlich späteres Projekt.

## Aktuelle Bootstrap-Grenzen

Einige Grenzen sind bewusst klein und auf echter Hardware inzwischen sichtbar:

- <code>KERNEL_PROCESS_MAX = 8</code>;
- <code>KERNEL_TASK_MAX = 8</code>;
- BoringWM verwaltet aktuell höchstens 6 Clients;
- ungefähr zwei vollständige Terminals passen gleichzeitig, weil jedes Terminal zusätzlich einen eigenen Shell-Prozess besitzt;
- das physische Display nutzt aktuell den Firmware-Framebuffer mit 800x600;
- der PMM besitzt eine größere begrenzte Designkapazität, die vollständige sinnvolle Nutzung der installierten 32 GiB auf Cthulhu ist aber noch zukünftige Arbeit;
- noch keine USB-Hubs und keine Multi-xHCI-Ownership;
- noch kein Netzwerk, Audio, NVMe, SMP-Runtime oder nativer GPU-Treiber.

Das sind Implementierungsgrenzen und keine als Support verkleideten Versprechen.

## Storage und BoringFS

BoringOS besitzt mit **BoringFS** ein eigenes Dateisystemformat. Das Repository enthält Codec und Validator, deterministischen Formatter, <code>boringfsck</code>, Kernel-Mount-Support und schreibbaren Betrieb.

Verifizierte Storage-Pfade umfassen VirtIO Block, begrenztes synchrones AHCI/SATA sowie M61s xHCI-USB-Mass-Storage-Pfad. Das physische M61-Image bootet und mountet sein schreibbares BoringFS-Root vom USB-Gerät.

## Hardware-Inventar

<code>boringfetch</code> zeigt Kernel-eigene Hardwarefakten statt erfundener Plattformstrings:

~~~text
CPUID   → CPU-Vendor, Brand, Family/Model/Stepping
PCI     → BDF-/Vendor-/Device-/Class-Inventar
SMBIOS  → Firmware, System, Board und installierter Speicher
INFO    → begrenzter versionierter Userspace-Snapshot
~~~

Auf Cthulhu läuft das inzwischen im echten grafischen BoringTerminal.

## Nach M61

M61 wird als physische Desktop-Basis eingefroren. Die nächste Reihenfolge ist bewusst:

~~~text
M61 EINFRIEREN
    ↓
1. Prozess + Task + Desktop-Kapazitäten
    ↓
2. USB: Multi-xHCI + Hubs + physische Maus
    ↓
3. native / bessere GOP-Auflösung
    ↓
4. Software-Grafik / Present beschleunigen
    ↓
5. die vollen 32 GiB RAM physisch sinnvoll nutzen
    ↓
...
    ↓
irgendwann: eigener AMD-Grafiktreiber
~~~

Das ist eine Richtungsplanung und kein Claim, dass die späteren Punkte bereits implementiert sind.

## Bauen und testen

Der Build verwendet GCC/binutils als freestanding x86_64-Toolchain und eine fest gepinnte Limine-Version.

~~~sh
make
make run
make test
~~~

Die permanente CI hält frühere Milestone-Beweise am Leben. M61 besitzt zusätzlich Exact-Head-Acceptance für USB-Image, persistentes Root, Desktop, Framebuffer, xHCI/HID und physische Observability.

Die detaillierte historische Quelle der Wahrheit bleibt [docs/roadmap.md](docs/roadmap.md).

## BoringWM

BoringOS enthält eine eigene native C-Implementierung von BoringWM für den BoringOS-Display-/IPC-Stack.

Das separate Repository [dennishilk/boringwm](https://github.com/dennishilk/boringwm) ist das ursprüngliche Rust-/X11-Projekt und eine Verhaltensreferenz. Es ist keine BoringOS-Abhängigkeit.

## Lizenz

BoringOS steht unter der [MIT License](LICENSE).
