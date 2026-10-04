# radaraltimeter24

FMCW-Radar-Höhenmesser (Radar Altimeter) für Flugzeuge mit 24-GHz-Radarmodul
(**RFbeam K-LC1a** oder **InnoSenT IVS-465**) und **STM32F405RGT6**.
Messbereich ca. 4 m … 250 m (abhängig vom Frequenzhub des Moduls), Ausgabe ~60 Hz über
**UART** und **CAN**. Zwei Rampenmodi: lange Rampe (8,1 ms) für Erfassung und große Höhe,
**Kurzrampe (2,35 ms) automatisch unter 25 m** für Landung und Abfangbogen.

[![CI](https://github.com/dansparker/radaraltimeter24/actions/workflows/ci.yml/badge.svg)](https://github.com/dansparker/radaraltimeter24/actions/workflows/ci.yml)

## Aufbau

```
lib/rdsp/            portable Radar-DSP-Bibliothek (C99, ohne malloc, wiederverwendbar)
firmware/app/        Höhenmesser-Pipeline, Konfiguration, Protokolle (hardwareunabhängig)
firmware/bsp/        STM32F405: Startup, Takt, Frontend (DAC/ADC/DMA), UART, CAN, Flash, IWDG
firmware/src/        main.c, Kommandozeile
test/                Radar-Simulator + Unit- und End-to-End-Tests (laufen in GitHub Actions)
docs/                Signalverarbeitung, Hardware, Review/Schwachstellen
```

## Signalverarbeitung (Kurzfassung)

Dreieck-FMCW (steigende und fallende Rampe abwechselnd, je 8,1 ms bzw. 2,35 ms im Kurzrampen-Modus):

| Schritt | Modul | Zweck |
|---|---|---|
| 0 | `rdsp_agc` | Verstärkungsregelung (CD4052, 4 Stufen), schnell ab / langsam auf |
| 1 | `rdsp_frame` | ADC → float, Statistik, Clipping, DC/Trend-/Hintergrundabzug |
| 2 | `rdsp_window` | Hann-Fenster (+ Skalierungssummen) |
| 3 | `rdsp_fft` | reelle FFT 2048 Punkte |
| 4 | `rdsp_spectrum` | Leistungsdichte (PSD), Welch-Mittelung über Rampen |
| 5 | `rdsp_cfar` | OS-CFAR-Detektion (auch CA/GO/SO) |
| 6 | `rdsp_peak` | Sub-Bin-Interpolation (exakt für Hann) |
| 7 | `rdsp_fmcw` | Up/Down-Kombination → dopplerfreie Entfernung + Geschwindigkeit |
| 8 | `rdsp_track` | α-β-Tracker mit Gating, Coasting, Wiederaufnahme |

Details und Begründungen: [docs/signal_chain.md](docs/signal_chain.md).

## Bauen

Firmware (arm-none-eabi-gcc, z. B. aus STM32CubeIDE):

```bash
sh tools/fetch_cmsis.sh
```

```bash
make -C firmware
```

Ergebnis: `firmware/build/radar_altimeter.{elf,bin,hex}`. Alternativ kann `CMSIS=<pfad>` auf
einen vorhandenen CubeIDE-`Drivers/CMSIS`-Ordner zeigen. Der Code nutzt keine HAL, nur
CMSIS-Registerdefinitionen.

Host-Tests (gcc/clang):

```bash
make -C test
```

## Inbetriebnahme

1. Firmware flashen, UART 115200 8N1 öffnen → `$RALT`-Zeilen.
2. `module klc1a` bzw. `module ivs465`, dann `info`.
3. **Kalibrierung** (Frequenzhub der Module unbekannt!): `rmode long`, Radar auf ein großes,
   flaches Ziel in bekanntem Abstand richten, `cal1 <m>`, Abstand ändern (≥ 2 m, besser ≥ 10 m),
   `cal2 <m>`. Optional dasselbe mit `rmode short` (eigener Hub der Kurzrampe, sonst wird der
   Wert der langen Rampe verwendet). Dann `rmode auto`, `save`.
4. Optional, eingebaut am Flugzeug: `zero 0` (Anzeige = 0 auf dem Boden), `save`.
5. Optional: `bg capture` mit Antenne zum freien Himmel (Fahrwerks-/Leckage-Echos), `bg on`, `save`.

Alle Kommandos: `help`. Siehe [docs/hardware.md](docs/hardware.md).

## Ausgabe

UART (NMEA-ähnlich, XOR-Prüfsumme):

```
$RALT,<seq>,<höhe_m>,<vs_m/s>,<snr_dB>,<gain>,<status_hex>*CS
$RDBG,<seq>,<roh_m>,<f_steigend_Hz>,<f_fallend_Hz>,<f_R_Hz>,<track>*CS   (out 2)
```

Höhe und Geschwindigkeit sind leer, wenn ungültig. CAN (Standard-ID `0x3A0`, 500 kbit/s):

| ID | Bytes |
|---|---|
| 0x3A0 | int32 Höhe [mm], int16 Steigrate [cm/s], uint8 Status (Low-Byte), uint8 Zähler(0-3) \| Gain(4-5) \| degraded(6) \| busy(7) |
| 0x3A1 | uint16 Status, uint8 SNR [dB], uint8 Track-Zustand, uint32 seq (jede 8. Messung) |

Statusbits: `0x0001` gültig, `0x0002` Coasting, `0x0004` kein Ziel, `0x0008` Clipping,
`0x0010` Hardwarefehler, `0x0020` nicht kalibriert, `0x0040` Frame-Überlauf,
`0x0080` degradiert (Einzelrampe/gespiegelt), `0x0100` Kalibrierung/Hintergrund läuft,
`0x0200` Gain-Wechsel, `0x0400` Taktausfall (läuft auf HSI).
Der aktuelle Rampenmodus steht in `$RDBG` (Track-Feld) bzw. `info`.

## Status / Einschränkungen

Die Firmware ist im Simulator getestet und kompiliert, **aber noch nicht auf der Hardware
erprobt**. Offene Punkte und gefundene Schwachstellen: [docs/review.md](docs/review.md).
Nicht als zertifiziertes Avionik-Gerät gedacht.

## Lizenz

MIT, siehe [LICENSE](LICENSE).
