# radaraltimeter24

FMCW-Radar-Höhenmesser (Radar Altimeter) für Flugzeuge mit 24-GHz-Radarmodul
(**RFbeam K-LC1a** oder **InnoSenT IVS-465**) und **STM32F405RGT6** (Rev. A) bzw.
**STM32F446** auf dem WeAct-Core-Board (Rev. B, [Schaltplan](hardware/rev_b_f446/README.md)).
Ausgabe ~61 Hz über **UART** und **CAN**. Kurze Dreieckrampen (1,17 ms), viele Rampen
gemittelt und die **Vorderkante** des Bodenechos ausgewertet: Der Boden ist aus dem Flugzeug
kein Punktziel, sondern wegen Keule und Fluggeschwindigkeit ein breiter Hügel im Spektrum.

Reichweite mit dem K-LC1a im Simulator (Empfindlichkeit nach Datenblatt, 30 m/s): über Wiese
bis etwa 100 m, über Asphalt/Wasser bis 250 m, kleinste Höhe etwa 4 m. Einzelheiten:
[hardware/rev_b_f446/README.md](hardware/rev_b_f446/README.md#mögliche-reichweite).

[![CI](https://github.com/dansparker/radaraltimeter24/actions/workflows/ci.yml/badge.svg)](https://github.com/dansparker/radaraltimeter24/actions/workflows/ci.yml)

## Aufbau

```
lib/rdsp/            portable Radar-DSP-Bibliothek (C99, ohne malloc, wiederverwendbar)
firmware/app/        Höhenmesser-Pipeline, Konfiguration, Protokolle (hardwareunabhängig)
firmware/bsp/        STM32F405 (Rev. A) / F446 (Rev. B): Startup, Takt, Frontend (DAC/ADC/DMA), UART, CAN, Flash, IWDG
firmware/src/        main.c, Kommandozeile
test/                Radar-Simulator mit Bodenmodell + Unit-/End-to-End-Tests, Reichweitenauswertung
docs/                Signalverarbeitung, Hardware, Review/Schwachstellen
hardware/rev_b_f446/ Schaltplan Rev. B (STM32F446, TLV9064), Reichweitenabschätzung
```

## Signalverarbeitung (Kurzfassung)

Dreieck-FMCW (steigende und fallende Rampe abwechselnd, je 1,17 ms; ein Messzyklus = 7 Paare):

| Schritt | Modul | Zweck |
|---|---|---|
| 0 | `rdsp_agc` | Verstärkungsregelung (4 Stufen), ignoriert einzelne Störpulse |
| 1 | `rdsp_frame` | ADC → float, Statistik, Clipping, DC-/Trendabzug |
| 2 | `rdsp_window` | Hann-Fenster über 256 Samples, Nullauffüllung |
| 3 | `rdsp_fft` | reelle FFT 512 Punkte |
| 3a | `rdsp_clutter_update` | laufender Abzug fester Echos (Leckage, Fahrwerk), kohärenzgewichtet |
| 4 | `rdsp_spectrum` | Leistungsdichte, robuste Mittelung über Rampen (τ 40–250 ms) |
| 5 | `rdsp_edge` | Vorderkante des Bodenechos (nur rückwärts schauendes Rauschfenster) |
| 6 | `rdsp_fmcw` | Auf/Ab-Kombination → dopplerfreie Entfernung + Geschwindigkeit |
| 7 | `rdsp_track` | α-β-Tracker mit Gating, Coasting, Wiederaufnahme |

![Verarbeitung pro Rampe](docs/img/verarbeitung_rampe.svg)

Details, Begründungen und weitere Diagramme: [docs/signal_chain.md](docs/signal_chain.md).

## Bauen

Firmware (arm-none-eabi-gcc, z. B. aus STM32CubeIDE):

```bash
sh tools/fetch_cmsis.sh
```

```bash
make -C firmware              # Rev. B (STM32F446), Standard
make -C firmware BOARD=reva   # Rev. A (STM32F405)
```

Ergebnis: `firmware/build/<board>/radar_altimeter.{elf,bin,hex}`. Alternativ kann `CMSIS=<pfad>` auf
einen vorhandenen CubeIDE-`Drivers/CMSIS`-Ordner zeigen. Der Code nutzt keine HAL, nur
CMSIS-Registerdefinitionen.

Host-Tests (gcc/clang):

```bash
make -C test
```

Reichweite/Genauigkeit gegen das Bodenmodell (einige Minuten):

```bash
make -C test range
```

## Inbetriebnahme

1. Firmware flashen, UART 115200 8N1 öffnen → `$RALT`-Zeilen.
2. `module klc1a` bzw. `module ivs465`, dann `info`.
3. **Kalibrierung** (Frequenzhub der Module unbekannt!): `bg off`, Radar auf einen Reflektor
   (Winkelreflektor, Metallplatte) in bekanntem Abstand richten, `cal1 <m>`, Abstand ändern
   (≥ 2 m, besser ≥ 10 m), `cal2 <m>`, dann `bg on`, `save`.
4. Optional, eingebaut am Flugzeug: `zero 0` (Anzeige = 0 auf dem Boden), `save`.
5. Laufender Hintergrundabzug (`bg on`, Standard) entfernt im Flug feste Echos (Leckage, Radom,
   Fahrwerk). Bei Bodentests mit ruhendem Ziel `bg off`, vor dem Flug wieder `bg on`, `save`.

Alle Kommandos: `help`. Siehe [docs/hardware.md](docs/hardware.md) und die vollständige
**[Checkliste zur Inbetriebnahme](docs/inbetriebnahme.md)**.

## Ausgabe

UART (NMEA-ähnlich, XOR-Prüfsumme):

```
$RALT,<seq>,<höhe_m>,<vs_m/s>,<snr_dB>,<gain>,<status_hex>*CS
$RDBG,<seq>,<roh_m>,<kante_steigend_Hz>,<kante_fallend_Hz>,<f_R_Hz>,<track>,<tau_ms>*CS   (out 2)
```

Höhe und Geschwindigkeit sind leer, wenn ungültig. CAN (Standard-ID `0x3A0`, 500 kbit/s):

| ID | Bytes |
|---|---|
| 0x3A0 | int32 Höhe [mm], int16 Steigrate [cm/s], uint8 Status (Low-Byte), uint8 Zähler(0-3) \| Gain(4-5) \| degraded(6) \| busy(7) |
| 0x3A1 | uint16 Status, uint8 SNR [dB], uint8 Track-Zustand, uint32 seq (jede 8. Messung) |

Statusbits: `0x0001` gültig, `0x0002` Coasting, `0x0004` kein Ziel, `0x0008` Clipping,
`0x0010` Hardwarefehler, `0x0020` nicht kalibriert, `0x0040` Frame-Überlauf,
`0x0080` degradiert (nur eine Rampenrichtung oder Kante nahe DC), `0x0100` Kalibrierung läuft,
`0x0200` Gain-Wechsel, `0x0400` Taktausfall (läuft auf HSI).
Das letzte Feld von `$RDBG` ist die aktuelle Mittelungszeit in ms.

## Status / Einschränkungen

Die Firmware ist im Simulator getestet und kompiliert, **aber noch nicht auf der Hardware
erprobt**. Bekannte Grenze: Bei hoher Fluggeschwindigkeit misst sie systematisch zu niedrig
(30 m/s: ≈ 0,5 m, 60 m/s: 1–2 m). Offene Punkte und gefundene Schwachstellen: [docs/review.md](docs/review.md).
Nicht als zertifiziertes Avionik-Gerät gedacht.

## Lizenz

MIT, siehe [LICENSE](LICENSE).
