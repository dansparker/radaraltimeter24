# Checkliste Inbetriebnahme auf der Hardware

Reihenfolge einhalten: Jeder Schritt setzt die vorherigen voraus. Messwerte und Abweichungen
direkt in der Spalte „Ist“ bzw. im Protokoll am Ende festhalten.

**Werkzeug:** Labornetzteil mit Strombegrenzung, ST-Link (SWD), USB-UART-Adapter 3,3 V,
Oszilloskop (2 Kanäle), Multimeter, CAN-Adapter mit Transceiver und 120-Ω-Abschluss,
Winkelreflektor oder große Metallplatte, Maßband/Laser-Entfernungsmesser.

---

## 1 Versorgung (ohne Radarmodul, ohne Firmware)

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | 12 V mit Strombegrenzung ~150 mA anlegen, Verpolschutz (Q1) prüfen: kurz verpolt → kein Strom | 0 mA | |
| ☐ | 10 V (78L10) | 9,6 … 10,4 V | |
| ☐ | 8 V (78L08) | 7,7 … 8,3 V | |
| ☐ | 5 V (78L05) | 4,8 … 5,2 V | |
| ☐ | 3,3 V (AMS1117) | 3,2 … 3,4 V | |
| ☐ | Ruhestrom ohne Firmware | notieren | |
| ☐ | Brummen/Rauschen auf 3,3 V und 5 V (Oszi, AC) | < 20 mVpp | |

## 2 Firmware flashen und Grundfunktion

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | `make -C firmware` bzw. CI-Artefakt `radar_altimeter.bin` an 0x08000000 flashen (ST-Link) | ok | |
| ☐ | UART 115200 8N1: Startmeldung | `# config: defaults …`, `# RADAR ALTIMETER 24GHz FMCW …` | |
| ☐ | Meldung `# CAN init failed`, wenn noch kein Transceiver/Bus angeschlossen ist (erwartet) | – | |
| ☐ | `$RALT`-Zeilen laufen (ohne Radar: Höhe leer, Status mit `0004` kein Ziel, `0020` unkalibriert) | ~60 Zeilen/s | |
| ☐ | LED1 (PC15) blinkt (Herzschlag) | ~2 Hz | |
| ☐ | `info` und `stat` antworten; `stat`: `frontend errors 0`, `overruns 0` | 0 | |
| ☐ | Statusbit `0400` (Taktausfall) **nicht** gesetzt → 8-MHz-Quarz läuft | nicht gesetzt | |
| ☐ | Stromaufnahme mit laufender Firmware | notieren | |

## 3 Rampe / VCO-Ansteuerung (Oszi)

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | `module klc1a`. PA4 (DAC): stetiges Dreieck, ohne Sprünge | ~0,2 … 2,0 V, Periode 16,2 ms | |
| ☐ | Unteres Ende mit DAC-Puffer abgeflacht? Falls deutlich: `dacbuf 0` testen und vergleichen | notieren | |
| ☐ | Ausgang Sallen-Key (U1B, Pin 7) = VCO-Eingang: glattes Dreieck, linear, keine Begrenzung | Spannungsbereich laut Moduldatenblatt | |
| ☐ | **LM358 an 5 V kann nur bis ~3,5 V ausgeben**: bei `module ivs465` prüfen, ob das obere Ende abgeschnitten wird | nicht begrenzt | |
| ☐ | `rmode short`: Periode 4,7 ms, Ecken nur innerhalb der ersten/letzten ~0,3 ms verrundet, Mitte linear | ok | |
| ☐ | Verzögerung DAC → VCO-Eingang (Kanal 1 PA4, Kanal 2 U1B) | < 0,1 ms | |
| ☐ | Zurück: `rmode auto` | – | |

## 4 ZF-Kette und ADC (mit Radarmodul, Antenne frei in den Raum)

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | Radarmodul anschließen, Stromaufnahme erneut | +Modulstrom laut Datenblatt | |
| ☐ | PC4 Gleichanteil (Multimeter) | 1,0 … 2,3 V (nicht am Rand) | |
| ☐ | `dump`: Rohdaten beider Rampen speichern, Mittelwert und Rauschen auswerten | Mittelwert 200 … 3895 LSB, Rauschen > 1 LSB rms | |
| ☐ | Kein `HW_FAULT` (Statusbit `0010`) | nicht gesetzt | |
| ☐ | FCT2 (PC13) High-Zeit = Rechenzeit pro Rampe, lange Rampe | < 7 ms (erwartet ~1,5 ms) | |
| ☐ | FCT2 High-Zeit, `rmode short` | **< 2,2 ms** (erwartet ~0,5 ms) | |
| ☐ | `stat` nach 5 min Betrieb in beiden Modi: `overruns`, `frontend errors`, `uart dropped` | 0 | |

## 5 Verstärkungsumschaltung (CD4052)

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | Reflektor in ~10 m. `gain 0` … `gain 3` nacheinander, je `dump`: Amplitude der Beat-Schwingung | steigend mit der Stufe | |
| ☐ | Pegel PB0/PB1 je Stufe: 0 → 1/1, 1 → 0/1, 2 → 1/0, 3 → 0/0 | wie angegeben | |
| ☐ | Verhältnis benachbarter Stufen ausrechnen | **≤ 4,25** (sonst `ALT_AGC_LO` in `radar_params.h` senken) | |
| ☐ | Höchste Stufe ohne Ziel: kein Dauer-Clipping (Statusbit `0008`) | nicht gesetzt | |
| ☐ | `gain auto`: Reflektor nah/fern bewegen → Gain-Wechsel im `$RALT`, kein Pendeln | stabil | |

## 6 Erste Radarmessung

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | `out 2`, Reflektor in ~10 m: `$RDBG` zeigt `f_rise` ≈ `f_fall` (ruhend) | Differenz < 30 Hz | |
| ☐ | Höhe im `$RALT` vorhanden und stabil (noch unkalibriert) | Streuung < 0,1 m | |
| ☐ | Reflektor nähert sich langsam → `vs` **negativ**; entfernt sich → positiv. Wenn umgekehrt: `vsign -1` | Vorzeichen korrekt | |
| ☐ | Reflektor abdecken → nach ≤ 0,25 s ungültig (Statusbit `0001` weg) | ok | |
| ☐ | Raum ohne Ziel 10 min laufen lassen: keine gültige Fehlmessung | 0 | |

## 7 Kalibrierung

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | `rmode long`. Ziel exakt in Abstand 1 (z. B. 10,00 m, Laser messen), `cal1 10` | Standardabweichung der Beat-Frequenz klein (< 1 % des Mittelwerts) | |
| ☐ | Ziel in Abstand 2 (z. B. 40,00 m), `cal2 40` → Hub und Offset werden angezeigt | Hub plausibel (K-LC1a: einige 10 … 100 MHz), Offset < 1 m | |
| ☐ | Optional `rmode short`, `cal1`/`cal2` wiederholen → `sweep short` | Abweichung zum langen Hub notieren | |
| ☐ | `rmode auto`, `save`, Versorgung aus/ein, `info`: Werte erhalten | erhalten | |
| ☐ | Kontrollmessung bei 3 weiteren Abständen (z. B. 6, 20, 80 m) | Fehler < max(0,3 m, 1 %) | |
| ☐ | Bei 6 m (Kurzrampe aktiv, `$RDBG` endet auf `S`) und bei 80 m (`L`) | beide innerhalb Toleranz | |

## 8 Rampenlinearität und Hintergrund

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | Großes Ziel in > 100 m (Gebäude): `dump`, Spektrum am PC anschauen | schmale Spitze (Hauptkeule ~4 Bins breit) | |
| ☐ | Bei breiter/verschmierter Spitze: `rampq` schrittweise (±0,05) variieren, schmalste Spitze wählen, `save` | notieren | |
| ☐ | Antenne zum freien Himmel, `bg capture` → Maske | ideal `255` (alle Stufen beider Modi) | |
| ☐ | `bg on`, `save`; Kontrollmessungen aus Schritt 7 wiederholen | unverändert oder besser | |

## 9 CAN

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | Transceiver an PA11/PA12 (Stecker P6), Bus mit 120 Ω an beiden Enden, `reset` | keine Meldung `CAN init failed` | |
| ☐ | Frame `0x3A0` mit 500 kbit/s, ~60 Hz; Byte 7 Bits 0–3 zählen hoch | ok | |
| ☐ | Höhe (int32 mm) stimmt mit `$RALT` überein; bei ungültiger Höhe 0 und Statusbit 0 | ok | |
| ☐ | Frame `0x3A1` (Diagnose) jede 8. Messung | ok | |
| ☐ | Bus kurz trennen/kurzschließen → danach Frames wieder vorhanden (Bus-Off-Erholung) | ok | |

## 10 Robustheit

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | ZF-Signal an PC4 abklemmen / auf festen Pegel → `HW_FAULT` (`0010`), ungültig; wieder anschließen → Erholung | < 0,5 s / < 0,5 s | |
| ☐ | Versorgung langsam von 12 V auf 6 V und zurück: kein Hänger (Watchdog setzt ggf. zurück, dann Neustart) | läuft wieder | |
| ☐ | 50 × Ein/Aus: Konfiguration bleibt erhalten | ok | |
| ☐ | Aufwärmdrift: festes Ziel 10 m, 30 min ab Kaltstart, Höhenverlauf loggen | Drift < 0,1 m | |
| ☐ | Temperaturbereich (falls möglich −20 … +60 °C): Drift von Hub und Offset | notieren | |
| ☐ | Vibration/Klopfen am Gehäuse: keine Fehlmessungen, keine Neustarts | ok | |

## 11 Fahrzeug-/Bodentest vor dem Flug

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | Einbau am Flugzeug, Fahrwerk belastet: `zero 0`, `save` | Anzeige 0,0 m | |
| ☐ | Fahrwerks-/Strukturechos: `bg capture` im eingebauten Zustand (Flugzeug angehoben oder später im Flug > 300 m AGL) | – | |
| ☐ | Gerät nach unten von Brücke/Dach/Kran über Gras, Asphalt, Wasser | Höhe korrekt, kein Dauer-Coasting | |
| ☐ | Gerät auf Fahrzeug (Antenne nach unten) während der Fahrt: Höhe stabil, keine Sprünge | Streuung < 0,1 m | |
| ☐ | Neigung ±15° (Längs-/Querlage simulieren): Höhe weiter gültig | gültig | |

## 12 Flugerprobung

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | UART-Log (`out 2`) und Referenz (GPS-Höhe über Boden, Barometer, Video) aufzeichnen | – | |
| ☐ | Tiefe Überflüge in 10, 30, 100, 200 m über flachem Gelände | Abweichung notieren | |
| ☐ | Umschaltung `L`/`S` im `$RDBG` bei ~25 m (Sinkflug) bzw. ~35 m (Steigflug), ohne Lücke in der Gültigkeit | ok | |
| ☐ | Landeanflüge mit unterschiedlicher Sinkrate: Verlauf bis zum Aufsetzen (unter ~4 m ungültig ist erwartet) | plausibel | |
| ☐ | Statusbit `0080` (degradiert) nur im Endanflug unter ~10 m | ok | |
| ☐ | Höhe > Messbereich: sauber ungültig, keine Scheinhöhen | ok | |

---

## Protokoll

| Datum | Modul / Seriennr. | Firmware (Git-Hash) | Hub lang / kurz [MHz] | Offset [m] | Gain-Verhältnisse | Bemerkungen |
|---|---|---|---|---|---|---|
| | | | | | | |
