# BoringOS

[English](README.md)

BoringOS ist ein experimentelles, unabhängiges Desktop-Betriebssystemprojekt.

Es ist **keine Linux-Distribution**, **keine BSD-Distribution** und **basiert weder auf Redox noch auf dem Kernel eines anderen Betriebssystems**. Das Projekt soll einen eigenen Kernel, **BoringKernel**, einen nativen BoringOS-Userspace und später einen eigenen Desktop-Stack entwickeln.

> boring is not a bug.  
> it's the entire operating system now.

## Status

**Extrem frühe Bootstrap-Phase.**

Dieses Repository enthält derzeit nur die Projektgrundlage und Dokumentation. Es enthält **noch keinen** funktionsfähigen BoringKernel, kein bootfähiges BoringOS-Abbild, keinen nativen Userspace, keinen Display-Server, keinen Paketmanager und keine native BoringWM-Implementierung. Geplante Funktionen dürfen nicht als bereits implementiert dargestellt werden.

## Technische Richtung

Von BoringOS entwickelte Systemkomponenten sollen hauptsächlich in **C** geschrieben werden. Minimale architekturspezifische Assembly-Anteile sind dort erlaubt, wo sie technisch unvermeidbar sind, zum Beispiel beim frühen CPU-Start, beim Eintritt und Verlassen von Interrupts, beim Context Switch oder beim Zugriff auf spezielle CPU-Instruktionen. Solcher Assembly-Code soll klein, isoliert und dokumentiert bleiben.

Eine präzise langfristige Beschreibung lautet:

> BoringOS ist ein unabhängiges Betriebssystem, dessen eigene Systemkomponenten hauptsächlich in C geschrieben werden.

Die erste Referenzplattform ist **x86_64 unter QEMU**. Breite Unterstützung physischer PC-Hardware ist bewusst kein frühes Ziel.

## Erstes Ziel

Der erste Entwicklungsmeilenstein ist ein kleines, tatsächlich bootfähiges und unabhängiges System:

```text
boot
  ↓
BoringKernel
  ↓
Speicherverwaltung
  ↓
Scheduler / Ausführungsumgebung
  ↓
nativer BoringOS-Userspace
  ↓
boring-init
  ↓
boring-shell
```

Die erste Shell soll später die Identität des Systems wahrheitsgemäß anzeigen können, zum Beispiel:

```text
BoringOS 0.0.1-dev

Kernel:    BoringKernel
Arch:      x86_64
Userspace: BoringOS
Shell:     boring-shell
```

Das ist ein Ziel und keine Aussage über den aktuellen Funktionsumfang des Repositorys.

## Bewusst keine frühen Ziele

BoringOS soll schrittweise wachsen. Die frühen Meilensteine zielen ausdrücklich **nicht** auf:

- Networking
- komplexe USB-Unterstützung
- Audio
- fortgeschrittene GPU-Beschleunigung
- Wi-Fi oder Bluetooth
- Suspend/Resume
- Browser
- große Kompatibilitätsschichten
- breite Unterstützung physischer PC-Hardware

Insbesondere Networking wird bewusst aufgeschoben, bis Kernel/User-Trennung, Prozessisolation, getrennte Adressräume, kontrollierte System-Call-Schnittstellen, validierte Kernel-Grenzen und grundlegende defensive Testverfahren vorhanden sind.

## Richtung des nativen Desktops

Der native BoringOS-Desktop soll nicht von X11 oder Wayland abhängen. Ein späterer Meilenstein soll ein bewusst kleines, natives BoringOS-Display-/Window-Protokoll und einen Display-Dienst definieren, die echte grafische Clients ermöglichen, ohne ein komplettes fremdes Display-Ökosystem nachzubauen.

Eine mögliche langfristige Architektur ist:

```text
Anwendungen
    ↓
boring-window-Protokoll
    ↓
boring-display
    ↓
BoringWM
    ↓
Framebuffer / Grafik-Backend
```

Die native BoringOS-Version von **BoringWM wird in C geschrieben**.

## Bestehendes BoringWM

Das bestehende Repository [dennishilk/boringwm](https://github.com/dennishilk/boringwm) ist ein separates Rust/X11-Projekt. Es bleibt unverändert bestehen und ist **keine Code-Abhängigkeit** von BoringOS.

BoringOS verwendet es als Verhaltensreferenz für Konzepte wie deterministisches Master/Stack-Layout, Workspaces, Fokusverhalten, tastaturorientierte Bedienung, Client-Reihenfolge, Promotion zum Master und bewusst kleine Konfiguration. X11-/EWMH-spezifische Mechanismen gehören nicht zum nativen BoringOS-Vertrag.

Siehe [`docs/boringwm-reference.md`](docs/boringwm-reference.md) für die Referenzanalyse und Integrationsempfehlung der Bootstrap-Phase.

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

Diese Verzeichnisse sind in der Bootstrap-Phase bewusst weitgehend leer. Ihre Existenz definiert Projektgrenzen; sie behauptet keine implementierten Subsysteme.

## Prinzipien

BoringOS soll bevorzugen:

- kleine Module
- explizite Schnittstellen
- vorhersehbares Verhalten
- lesbaren C-Code
- testbare Komponenten
- auditierbare Komponenten
- strikte Compiler-Diagnostik
- minimale Abhängigkeiten
- dokumentierte Architekturentscheidungen
- ehrliche Berichte darüber, was funktioniert und was nicht

Das Projekt soll für einen einzelnen entschlossenen Entwickler verständlich bleiben.

## Lizenz

BoringOS steht unter der [MIT-Lizenz](LICENSE).
