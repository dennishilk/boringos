# BoringOS

[English](README.md)

BoringOS ist ein experimentelles, unabhängiges Desktop-Betriebssystemprojekt.

Es ist **keine Linux-Distribution**, **keine BSD-Distribution** und **basiert weder auf Redox noch auf dem Kernel eines anderen Betriebssystems**. BoringOS entwickelt seinen eigenen Kernel, **BoringKernel**, sowie später einen nativen BoringOS-Userspace und Desktop-Stack.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extrem früher Bootstrap-Kernel.**

BoringKernel bootet unter **QEMU x86_64**. Limine bleibt der externe Bootloader. BoringKernel besitzt aktuell COM1-Seriellenausgabe, einen Physical-Page-Frame-Allocator, ausgewählte 4-KiB-Virtual-Mappings, einen begrenzten dynamischen Kernel-Heap, eine eigene x86_64-IDT, echte CPU-Exception-Diagnostik, wiederholte echte PIT/PIC-IRQ0-Auslieferung, kooperative Kernel-Kontexte und jetzt zusätzlich **echtes hardware-timergetriebenes präemptives Umschalten zwischen unabhängigen Kernel-Tasks**.

Die aktuelle serielle Ausgabe beginnt mit:

```text
BoringOS booting...
BoringKernel 0.0.8-dev
Arch: x86_64
Hello from BoringKernel.
```

Der VMM übernimmt bewusst die aktive, von Limine erzeugte vierstufige x86_64-Page-Table-Root-Struktur, statt bereits den gesamten Adressraum zu ersetzen. BoringKernel besitzt damit gezielte Virtual-Memory-Kontrolle, aber noch keinen vollständig eigenen unabhängigen Adressraum.

Der begrenzte Kernel-Heap reserviert einen endlichen 16-MiB-Virtual-Address-Bereich, wächst über PMM + VMM, verwendet deterministisches First-Fit mit 16-Byte-Ausrichtung und führt benachbarte freie Blöcke zusammen. Bereits gemappte Heap-Seiten bleiben nach `kfree` in dieser Bootstrap-Phase erhalten.

BoringKernel besitzt eine 256 Einträge große x86_64-IDT. Separate QEMU-Modi beweisen weiterhin einen **echten CPU-Divide-Error** und einen **echten MMU-Page-Fault** über BoringKernels eigenen Exception-Entry-Pfad und kontrollierte fatale Diagnostik.

Der Bootstrap-Hardware-Pfad remappt den Legacy-8259-PIC auf Vektoren 32–47, programmiert PIT Channel 0 auf angeforderte 100 Hz und gibt ausschließlich IRQ0 frei. Der tatsächliche Divisor ist 11932, entsprechend ungefähr 99,998491 Hz; seriell werden `99998 mHz` ausgegeben, statt exaktes Timing vorzutäuschen.

## Kernel-Tasks

Jeder normale Kernel-Task erhält einen unabhängigen **16-KiB-Stack aus dem Kernel-Heap**. Die Task-Zustände bleiben bewusst klein:

```text
READY
RUNNING
FINISHED
```

BoringKernel besitzt zwei bewusst getrennte Umschaltgrenzen.

### Kooperatives Umschalten

Ein explizites `task_yield()` verwendet den kleinen SysV-AMD64-Call-Boundary-Kontext:

```text
RSP RBX RBP R12 R13 R14 R15
```

Der bestehende QEMU-Acceptance-Test beweist weiterhin alternierende kooperative Ausführung, task-lokalen Stackzustand, Erhaltung der Callee-Saved-Register, saubere Task-Rückkehr, Stack-Cleanup, Bootstrap-Rückkehr und weiterlaufende PIT-Ticks.

### Timergetriebene Präemption

Präemption ist ein separater Interrupt-Time-Mechanismus. Ein PIT-IRQ kann an einer beliebigen Instruktion eintreffen; deshalb bewahrt der IRQ-Pfad den vollständigen Integer-Registerzustand und den Interrupt-Return-State auf dem eigenen Stack des unterbrochenen Tasks.

Der aktuelle normalisierte x86_64-Preemption-Frame ist **192 Byte** groß und enthält:

```text
RAX RBX RCX RDX RSI RDI RBP
R8 R9 R10 R11 R12 R13 R14 R15
vector error-code
RIP CS RFLAGS RSP SS
```

Die Scheduling-Policy ist absichtlich minimal:

```text
deterministisches Round-Robin
1 PIT-Tick = 1 Quantum
```

Wenn Präemption aktiv ist, erhöht IRQ0 den Timer, tritt in den Scheduler ein, belässt den unterbrochenen Frame auf dem Stack des aktuellen Tasks, wählt den nächsten READY-Task, sendet den PIC-EOI **bevor der aktuelle IRQ-Stack verlassen wird**, und gibt den ausgewählten Frame-Zeiger an Assembly zurück. Der Restore-Pfad wechselt anschließend `RSP`, stellt den vollständigen Integer-State wieder her und setzt den ausgewählten Task mit `iretq` fort.

Neue präemptive Tasks erhalten bewusst einen synthetischen IRQ-/`iretq`-Frame auf ihrem eigenen Stack und starten über denselben C-Task-Trampoline. Der Bootstrap-Kernelstack wird nicht kopiert: Sein echter PIT-Interrupt-Frame wird behalten und später wiederhergestellt, sodass die normale Kernelinitialisierung exakt im unterbrochenen Bootstrap-Instruktionsstrom fortgesetzt wird.

Der verifizierte CPU-bound Preemption-Test enthält in **keinem der beiden Stress-Tasks einen Aufruf von `task_yield()`**. Ein akzeptierter QEMU-Branch-Run meldete:

```text
Timer ticks during test: 7
Scheduler ticks: 7
Preemptions: 7
Task A slices: 3
Task B slices: 3
Task A resumes: 2
Task B resumes: 2
Cooperative yields during test: 0
```

Der gleiche Test prüft task-lokale Counter/Checksummen, unabhängige Stack-Adressen, Stack-Sentinels, das Überspringen beendeter Tasks, Bootstrap-Rückkehr, Cleanup, Heap-Bookkeeping und eine Assembly-unterstützte **vollständige Integer-GPR-Erhaltungsprobe** über echte Hardware-Timer-Präemption hinweg.

Kernel-C wird weiterhin so gebaut, dass keine unbeabsichtigte x87/MMX/SSE/SSE2-Codeerzeugung stattfindet. FPU-/SIMD-Task-State-Switching existiert noch nicht.

## Aktuelle Grenze des Ausführungsmodells

Das ist **ausschließlich Kernel-Task-Präemption**. Alle aktuellen Tasks laufen in CPL0 im selben Kernel-Adressraum.

Es gibt weiterhin **kein User-Prozessmodell, Ring 3, keine Syscall-Schicht, getrennten Prozessadressräume, CR3-Task-Switches, keinen Userspace-Loader, kein Dateisystem, keinen Storage-Stack, kein Networking, keine grafische Umgebung und keinen Input-Stack**. Ebenso fehlen Sleeping/Blocking, Wait Queues, Prioritäten, Realtime-Policy, ein allgemeines Synchronisationsframework, SMP-Scheduling, LAPIC/IOAPIC-Timerarchitektur und FPU/SIMD-Kontextwechsel.

Für den aktuellen Legacy-Timer-Nachweis bleibt QEMU:

```text
-M q35 -cpu qemu64,apic=off -m 128M
```

Das ist temporäre Bootstrap-Infrastruktur und keine Behauptung über physische Hardware-Kompatibilität.

## Technische Richtung

Von BoringOS entwickelte Systemkomponenten werden hauptsächlich in **C** geschrieben. Minimale architekturspezifische Assembly-Anteile sind dort erlaubt, wo sie technisch unvermeidbar sind; sie müssen klein, isoliert und dokumentiert bleiben.

> BoringOS ist ein unabhängiges Betriebssystem, dessen eigene Systemkomponenten hauptsächlich in C geschrieben werden.

Die erste Referenzplattform ist **x86_64 unter QEMU**. Breite Unterstützung physischer PC-Hardware ist bewusst kein frühes Ziel.

## Bauen und testen

Der aktuelle Build verwendet GCC/binutils als freestanding x86_64-Toolchain und lädt eine fest gepinnte Limine-Version. Generierte Dateien bleiben unter `build/`.

Benötigte Host-Werkzeuge sind unter anderem GNU Make, GCC/binutils, `curl`, `xorriso` und QEMU.

```sh
make
make run
```

Die vollständige Acceptance-Suite baut BoringOS im normalen Modus und in den absichtlichen Fatal-Test-Modi neu. Sie prüft PMM, VMM, Heap, IDT, wiederholte echte PIT/PIC-IRQ0-Auslieferung, kooperatives Kernel-Switching, timergetriebenes präemptives Kernel-Scheduling, einen echten Divide Error und einen echten Page Fault:

```sh
make test
```

Siehe [`docs/architecture.md`](docs/architecture.md), [`docs/interrupts.md`](docs/interrupts.md), [`docs/tasks.md`](docs/tasks.md), [`docs/boot.md`](docs/boot.md), [`docs/roadmap.md`](docs/roadmap.md) und [`docs/boringfs.md`](docs/boringfs.md).

## Richtung des nativen Desktops

Der native BoringOS-Desktop soll nicht von X11 oder Wayland abhängen. Ein späterer Meilenstein wird ein bewusst kleines natives BoringOS-Display-/Window-Protokoll und einen Display-Dienst definieren. Die native BoringOS-Version von **BoringWM wird in C geschrieben**.

Das bestehende Repository [dennishilk/boringwm](https://github.com/dennishilk/boringwm) bleibt ein separates Rust/X11-Projekt und eine externe Verhaltensreferenz. Es ist weder Code-Abhängigkeit noch Submodul von BoringOS.

## Prinzipien

BoringOS soll kleine Module, explizite Schnittstellen, vorhersehbares Verhalten, lesbaren C-Code, test- und auditierbare Komponenten, strikte Diagnostik, minimale Abhängigkeiten, dokumentierte Architekturentscheidungen und ehrliche Aussagen über den tatsächlichen Funktionsumfang bevorzugen.

Das Projekt soll für einen einzelnen entschlossenen Entwickler verständlich bleiben.

## Lizenz

BoringOS steht unter der [MIT-Lizenz](LICENSE).
