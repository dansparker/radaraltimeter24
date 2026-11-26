# Review: Tests, gefundene Schwachstellen, offene Punkte

## Testumfang

`test/` enthält einen Radar-Simulator (Punktziele mit Doppler, Antennen-Leckage, umschaltbare
Verstärkung, Front-End- und ADC-Rauschen, 3 ZF-Hochpässe, 12-Bit-Quantisierung, Clipping,
zufällige Trägerphase des Bodenechos je Rampe wie im Flug, feste Echos mit konstanter Phase) und
treibt damit die **unveränderte** Firmware-Pipeline (`altimeter.c` + `rdsp`). Die GitHub-Actions-CI
führt alle Tests normal und mit Address-/Undefined-Behaviour-Sanitizer aus und baut die Firmware.

| Test | Ergebnis (Simulator) |
|---|---|
| FFT gegen DFT, Fenster, PSD-Skalierung, Welch | rel. Fehler < 2·10⁻⁵, PSD ±5 % |
| OS-CFAR Falschalarmrate | 0,0096 gemessen (Soll 0,01) |
| Interpolation Hann | < 10⁻⁴ Bin |
| Konstante Höhen 5 … 240 m | max. Fehler 0,06 m (5 m), sonst ≤ 0,02 m |
| Sink-/Steigflug −12 … +3 m/s | Höhe ≤ 0,001 m, Geschwindigkeit ≤ 0,005 m/s |
| Nur Rauschen (1500 Messungen) | 0 gültige Ausgaben |
| Mehrwegeechos 2R/3R | richtige Höhe |
| Ausreißer (starkes Scheinziel) | ignoriert |
| Zielverlust kurz/lang, Wiederaufnahme | Coast ≤ 0,25 s, dann ungültig; Wiederaufnahme nach 1 Paar |
| Sprung 60 → 45 m | nach 5 Paaren übernommen |
| Übersteuerung / schwaches Ziel | AGC auf Stufe 0 bzw. 3, keine Fehlmessung bei Clipping |
| Landung 30 → 4,5 m bei 3 m/s | gültig, max. 0,25 m |
| Flare 3 → 0,8 m/s zwischen 12 und 5 m | Kurzrampe (auto) 0,23 m; nur lange Rampe 1,2 m und 3 Lücken |
| Sinken 45 → 12 m, Abfangen, Steigen auf 50 m | Umschalten lang ↔ kurz ohne ungültige Ausgabe, max. 0,07 m |
| ADC eingefroren | HW_FAULT, ungültig, Erholung |
| VCO-Richtung invertiert | korrekt mit `vsign -1` |
| Fahrwerksecho 4,5 m + Boden 35 m | ohne Hintergrundabzug 4,5 m (falsch), mit laufendem Abzug nach 13 Paaren 35,00 m; Sinkflug bis 7 m max. 0,21 m |
| Festes Ziel 4,5 m mit `bg on` | wird entfernt (erwartet, am Boden `bg off`) |
| 2-Punkt-Kalibrierung (Sollhub 110 MHz, Start 90 MHz) | lang 110,001 MHz, kurz 110,004 MHz |

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

0. **KRITISCH – Vorwärtsflug (Bodenmodell, `make -C test range`):** Mit der ausgedehnten
   Bodenfläche (Keule 34° × 80°, Doppler durch die Fluggeschwindigkeit, Speckle, Empfindlichkeit
   nach Datenblatt) ist das Bodenecho kein Punktziel mehr, sondern ein breiter Hügel im Spektrum.
   - Bei der langen Rampe und 30 m/s ist er ±13 m breit, bei der Kurzrampe ±4 m.
   - Zusätzlich liegt er wegen der 80°-Keule hinter der Senkrechten.
   - Die heutige Detektion (OS-CFAR, nächstes starkes Maximum) liefert dann **gültige Werte mit
     Metern Fehler**:
     - Wiese, 30 m/s, Kurzrampe: bis 9 m Fehler (95-%-Quantil) schon zwischen 10 und 30 m;
     - 60 m/s: über 20 m Fehler;
     - die Automatik (Erfassung mit der langen Rampe) erfasst schlechter.
   - Bis zur Überarbeitung (kurze Rampen, viele Rampen mitteln, Vorderkanten-Schätzung auf dem
     Hügel) ist die Firmware **nicht flugtauglich**.

1. **Frequenzhub unbekannt** → ohne Kalibrierung gilt `UNCAL`; die Höhe kann um den Faktor
   (wahrer Hub / 90 MHz) falsch sein.
2. **Minimale Höhe ≈ 2 Bins ≈ 4 m bei 90 MHz Hub.** Darunter ist das Ziel von DC/Leckage nicht
   trennbar. Abhilfe: größerer Hub (IVS-465 hat einen größeren Tuning-Bereich), Hintergrundabzug.
3. **Landung mit wechselnder Sinkrate:** Im Einzelrampen-Bereich (Status `DEGRADED`) erzeugt
   jede m/s Fehler der vorhergesagten Sinkrate einen Höhenfehler: 0,63 m mit der Kurzrampe
   (unter 25 m automatisch aktiv), 2,17 m mit der langen Rampe. Ungetestet ist, ob der VCO des
   K-LC1a/IVS-465 und das Sallen-Key-Filter der 2,35-ms-Rampe linear folgen. Prüfen mit
   `rmode short` + `dump`; falls nötig, eigenen Hub mit `rmode short` + `cal1/cal2` kalibrieren.
4. **CD4052-Verstärkungsstufen** sind nicht bekannt. Die AGC setzt ein Stufenverhältnis ≤ 4,25
   voraus (sonst `ALT_AGC_LO` verkleinern). Die Höhe hängt nicht von der Verstärkung ab.
5. **PA11/PA12**: Im Schaltplan als CAN, in der alten Firmware als TM1637-Anzeige genutzt. Die
   neue Firmware verwendet CAN; ein CAN-Transceiver ist im Schaltplan nicht sichtbar (Stecker P6).
6. **DAC-Ausgangspuffer** begrenzt die Spannung unter ~0,2 V (K-LC1a-Rampe startet bei 0 V).
7. **Hardware-Test steht aus**: Taktbaum, DMA/Trigger-Kopplung, CAN-Bit-Timing, Flash-Speicher
   und die tatsächlichen Signalpegel müssen am Gerät verifiziert werden (`dump`, `stat`, FCT2-Pin).
8. Keine Avionik-Zertifizierung (DO-178C/DO-254); nur für Experimentalzwecke.
