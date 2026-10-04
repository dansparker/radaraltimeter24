# Review: Tests, gefundene Schwachstellen, offene Punkte

## Testumfang

`test/` enthält einen Radar-Simulator (Punktziele mit Doppler, Antennen-Leckage, umschaltbare
Verstärkung, Front-End- und ADC-Rauschen, 3 ZF-Hochpässe, 12-Bit-Quantisierung, Clipping) und
treibt damit die **unveränderte** Firmware-Pipeline (`altimeter.c` + `rdsp`). Die GitHub-Actions-CI
führt alle Tests normal und mit Address-/Undefined-Behaviour-Sanitizer aus und baut die Firmware.

| Test | Ergebnis (Simulator) |
|---|---|
| FFT gegen DFT, Fenster, PSD-Skalierung, Welch | rel. Fehler < 2·10⁻⁵, PSD ±5 % |
| OS-CFAR Falschalarmrate | 0,0096 gemessen (Soll 0,01) |
| Interpolation Hann | < 10⁻⁴ Bin |
| Statische Höhen 5 … 240 m | max. Fehler 0,04 m (5 m), sonst ≤ 0,002 m |
| Sink-/Steigflug −12 … +3 m/s | Höhe ≤ 0,001 m, Geschwindigkeit ≤ 0,005 m/s |
| Nur Rauschen (1500 Messungen) | 0 gültige Ausgaben |
| Mehrwegeechos 2R/3R | richtige Höhe |
| Ausreißer (starkes Scheinziel) | ignoriert |
| Zielverlust kurz/lang, Wiederaufnahme | Coast ≤ 0,25 s, dann ungültig; Wiederaufnahme nach 1 Paar |
| Sprung 60 → 45 m | nach 5 Paaren übernommen |
| Übersteuerung / schwaches Ziel | AGC auf Stufe 0 bzw. 3, keine Fehlmessung bei Clipping |
| Landung 30 → 4,5 m bei 3 m/s | gültig, max. 0,46 m Fehler im Einzelrampen-Bereich |
| ADC eingefroren | HW_FAULT, ungültig, Erholung |
| VCO-Richtung invertiert | korrekt mit `vsign -1` |
| Fahrwerksecho 4,5 m + Boden 35 m | ohne Hintergrundabzug 4,5 m (falsch), mit 35,00 m |
| 2-Punkt-Kalibrierung (Sollhub 110 MHz, Start 90 MHz) | 110,001 MHz |

Die Simulator-Ergebnisse sind ein **Bestfall**: VCO-Nichtlinearität, Temperaturdrift,
Bodenrauigkeit (ausgedehntes Ziel, Speckle) und Fluglage (Neigung/Rollwinkel) sind nicht
modelliert. Real sind Fehler im Bereich von einigen Zentimetern bis Dezimetern zu erwarten.

## Durch Tests gefundene und behobene Schwachstellen

1. **Geisterziele durch spektrale Mittelung**: Ein verschwundenes Ziel blieb ~13 Rampen im
   gemittelten Spektrum, bewegte Ziele hinkten um ~1 Messperiode nach (0,2 m bei 12 m/s).
   *Fix:* Detektion im Mittel, Bestätigung (≥ 9 dB) und Frequenzschätzung in der aktuellen Rampe.
2. **Falsche Up/Down-Paarung bei geringer Höhe** verfälschte die Doppler-Geschwindigkeit und
   brachte den Tracker zum Absturz. *Fix:* Doppler-Gate ±3 m/s, kein Doppler aus Beats < 4 Bins,
   Plausibilitätsprüfung vor der Fusion.
3. **Instabile Rückkopplung im Einzelrampen-Modus** (Höhe ↔ vorhergesagte Geschwindigkeit,
   2,17 m pro m/s): Die Höhe lief bei der Landung um 47 m weg. *Fix:* Einzelrampen-Messungen
   führen nur die Position nach.
4. **Tracker extrapolierte im Zustand LOST weiter.** *Fix:* Zustand eingefroren, v = 0.
5. **Fehlannahme Hochpass-Entzerrung**: Ein stationärer Ton wird von einem LTI-Filter nicht
   „gekippt“. Die eingebaute Entzerrung verschlechterte die Messung (10 m: 0,69 m Fehler) und
   wurde wieder entfernt.

## Unterschiede zur alten Firmware (Robustheit)

* DAC und ADC laufen mit **einem** Trigger und zirkulärem DMA, damit synchron. Vorher liefen
  getrennte Timer und es gab Software-Neustarts pro Rampe (Jitter, Versatz).
* Stetige Dreieckrampe statt Rücksprung: keine Anregung der ZF-Hochpässe.
* Doppler-Kompensation über Up/Down-Paare, Tracking mit Gating und definierte Gültigkeit.
* Watchdog, Taktüberwachung (CSS) mit Notbetrieb, CRC-geschützte Konfiguration,
  Fehlerzähler, Selbsterkennung eingefrorener ADC-Daten, automatischer Frontend-Neustart.
* Keine blockierenden Ausgaben im Messbetrieb (Ringpuffer, CAN nicht blockierend).

## Offene Punkte / Grenzen (bitte beachten)

1. **Frequenzhub unbekannt** → ohne Kalibrierung gilt `UNCAL`; die Höhe kann um den Faktor
   (wahrer Hub / 90 MHz) falsch sein.
2. **Minimale Höhe ≈ 2 Bins ≈ 4 m bei 90 MHz Hub.** Darunter ist das Ziel von DC/Leckage nicht
   trennbar. Abhilfe: größerer Hub (IVS-465 hat einen größeren Tuning-Bereich), Hintergrundabzug.
3. **Landung mit wechselnder Sinkrate:** Im Einzelrampen-Bereich (bei 3 m/s etwa 2,6 … 10,5 m)
   erzeugt jede m/s Fehler der vorhergesagten Sinkrate 2,17 m Höhenfehler (Status `DEGRADED`).
   **Empfohlene Erweiterung:** ein Kurzrampen-Modus für geringe Höhe (z. B. 2 ms statt 8 ms
   Rampendauer) verringert diese Kopplung auf ≈ 0,5 m pro m/s und hält die Beats von DC fern.
4. **CD4052-Verstärkungsstufen** sind nicht bekannt. Die AGC setzt ein Stufenverhältnis ≤ 4,25
   voraus (sonst `ALT_AGC_LO` verkleinern). Die Höhe hängt nicht von der Verstärkung ab.
5. **PA11/PA12**: Im Schaltplan als CAN, in der alten Firmware als TM1637-Anzeige genutzt. Die
   neue Firmware verwendet CAN; ein CAN-Transceiver ist im Schaltplan nicht sichtbar (Stecker P6).
6. **DAC-Ausgangspuffer** begrenzt die Spannung unter ~0,2 V (K-LC1a-Rampe startet bei 0 V).
7. **Hardware-Test steht aus**: Taktbaum, DMA/Trigger-Kopplung, CAN-Bit-Timing, Flash-Speicher
   und die tatsächlichen Signalpegel müssen am Gerät verifiziert werden (`dump`, `stat`, FCT2-Pin).
8. Keine Avionik-Zertifizierung (DO-178C/DO-254); nur für Experimentalzwecke.
