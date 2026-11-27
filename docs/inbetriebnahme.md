# Checkliste Inbetriebnahme auf der Hardware

Reihenfolge einhalten: Jeder Schritt setzt die vorherigen voraus. Messwerte und Abweichungen
direkt in der Spalte „Ist“ bzw. im Protokoll am Ende festhalten.

**Werkzeug:** Labornetzteil mit Strombegrenzung, ST-Link (SWD), USB-UART-Adapter 3,3 V,
Oszilloskop (2 Kanäle), Multimeter, CAN-Adapter mit Transceiver und 120-Ω-Abschluss,
Winkelreflektor oder große Metallplatte, Maßband/Laser-Entfernungsmesser.

**Hintergrundabzug:** Die Firmware entfernt im Betrieb laufend alle Echos, die sich relativ zum
Radar nicht bewegen (`bg on`, Standard). Im Flug ist das gewollt (Leckage, Radom, Fahrwerk), bei
Tests mit **ruhendem** Ziel verschwindet das Ziel dadurch nach etwa 2 s. Deshalb ab Schritt 6
bis einschließlich Schritt 11 `bg off` und vor dem Flug wieder `bg on`, `save`.
Rev. B (STM32F446): die Pins im Text beziehen sich auf Rev. A, siehe `firmware/bsp/bsp.h`.

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
| ☐ | `module klc1a`. PA4 (DAC): stetiges Dreieck, ohne Sprünge | Periode 2,35 ms (Rampe 1,17 ms) | |
| ☐ | Unteres Ende mit DAC-Puffer abgeflacht? Falls deutlich: `dacbuf 0` testen und vergleichen | notieren | |
| ☐ | Ausgang Sallen-Key (U1B, Pin 7) = VCO-Eingang: glattes Dreieck, linear, keine Begrenzung | Spannungsbereich laut Moduldatenblatt | |
| ☐ | **LM358 an 5 V kann nur bis ~3,5 V ausgeben**: bei `module ivs465` prüfen, ob das obere Ende abgeschnitten wird | nicht begrenzt | |
| ☐ | Ecken nur innerhalb der ersten/letzten **160 µs** verrundet (verworfene Randsamples), Mitte linear | ok | |
| ☐ | Verzögerung DAC → VCO-Eingang (Kanal 1 PA4, Kanal 2 VCO-Eingang) | < 50 µs | |

## 4 ZF-Kette und ADC (mit Radarmodul, Antenne frei in den Raum)

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | Radarmodul anschließen, Stromaufnahme erneut | +Modulstrom laut Datenblatt | |
| ☐ | PC4 Gleichanteil (Multimeter) | 1,0 … 2,3 V (nicht am Rand) | |
| ☐ | `dump`: Rohdaten beider Rampen speichern, Mittelwert und Rauschen auswerten | Mittelwert 200 … 3895 LSB, Rauschen > 1 LSB rms | |
| ☐ | Kein `HW_FAULT` (Statusbit `0010`) | nicht gesetzt | |
| ☐ | Debug-Pin (Rev. A FCT2/PC13, Rev. B PB13) High-Zeit = Rechenzeit pro Rampe, auch das Maximum (jede 7. Rampe mit Kantensuche) | **< 1,1 ms** (erwartet 0,3–0,5 ms) | |
| ☐ | `stat` nach 5 min Betrieb: `overruns`, `frontend errors`, `uart dropped` | 0 | |

## 5 Verstärkungsumschaltung (Rev. A CD4052, Rev. B TS3A5017)

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | Reflektor in ~10 m. `gain 0` … `gain 3` nacheinander, je `dump`: Amplitude der Beat-Schwingung | steigend mit der Stufe | |
| ☐ | Pegel A/B (Rev. A PB0/PB1, Rev. B PC6/PC7) je Stufe: 0 → 1/1, 1 → 0/1, 2 → 1/0, 3 → 0/0 | wie angegeben | |
| ☐ | Verhältnis benachbarter Stufen ausrechnen | **≤ 4,25** (sonst `ALT_AGC_LO` in `radar_params.h` senken) | |
| ☐ | Höchste Stufe ohne Ziel: kein Dauer-Clipping (Statusbit `0008`) | nicht gesetzt | |
| ☐ | `gain auto`: Reflektor nah/fern bewegen → Gain-Wechsel im `$RALT`, kein Pendeln | stabil | |

## 6 Erste Radarmessung

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | `bg off` (ruhendes Ziel, siehe oben) | `info` zeigt `bg off` | |
| ☐ | `out 2`, Reflektor in ~10 m: `$RDBG` zeigt Kante steigend ≈ Kante fallend (ruhend) | Differenz < 300 Hz (≈ 0,5 Bin) | |
| ☐ | Höhe im `$RALT` vorhanden und stabil (noch unkalibriert) | Streuung < 0,3 m | |
| ☐ | Reflektor nähert sich langsam → `vs` **negativ**; entfernt sich → positiv. Wenn umgekehrt: `vsign -1` | Vorzeichen korrekt | |
| ☐ | Reflektor abdecken → nach ≤ 1 s ungültig (Statusbit `0001` weg; die Mittelung braucht etwas) | ok | |
| ☐ | **Antenne zum freien Himmel, 10 min:** keine einzige gültige Ausgabe (prüft, ob die Rauschform der Hardware Scheinkanten erzeugt) | 0 | |

## 7 Kalibrierung

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | `bg off`. Reflektor exakt in Abstand 1 (z. B. 10,00 m, Laser messen), `cal1 10` | Standardabweichung der Beat-Frequenz klein (< 1 % des Mittelwerts) | |
| ☐ | Ziel in Abstand 2 (z. B. 40,00 m), `cal2 40` → Hub und Offset werden angezeigt | Hub plausibel (K-LC1a: einige 10 … 100 MHz), Offset < 1 m | |
| ☐ | `save`, Versorgung aus/ein, `info`: Werte erhalten | erhalten | |
| ☐ | Kontrollmessung mit Reflektor bei 3 weiteren Abständen (z. B. 6, 20, 80 m). Hinweis: Die Höhe kommt aus der Vorderkante, die für den Boden kalibriert ist; ein Punktreflektor erscheint in großem Abstand bis ~1,5 m zu nah | Fehler < max(1 m, 2 %) | |

## 8 Rampenlinearität und Hintergrund

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | Großes Ziel in > 100 m (Gebäude): `dump`, Spektrum am PC anschauen | schmale Spitze (Hauptkeule ~4 Bins breit) | |
| ☐ | Bei breiter/verschmierter Spitze: `rampq` schrittweise (±0,05) variieren, schmalste Spitze wählen, `save` | notieren | |
| ☐ | Kontrolle des laufenden Hintergrunds: Reflektor in ~10 m langsam vor und zurück bewegen (±0,5 m), `bg on` | Höhe bleibt gültig, folgt der Bewegung | |
| ☐ | Reflektor ruhig halten, `bg on` | wird nach ~2 s ungültig (erwartet) | |
| ☐ | `bg off` für die weiteren Bodentests | – | |

## 9 CAN

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | Transceiver an CAN1 (Rev. A PA11/PA12 Stecker P6, Rev. B U8 an PB8/PB9), Bus mit 120 Ω an beiden Enden, `reset` | keine Meldung `CAN init failed` | |
| ☐ | Frame `0x3A0` mit 500 kbit/s, ~61 Hz; Byte 7 Bits 0–3 zählen hoch | ok | |
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
| ☐ | Fahrwerks-/Strukturechos: `bg off`, `out 2` im eingebauten Zustand ohne Bodenecho (z. B. angehoben) → Echos in `$RDBG` notieren | im Flug werden sie vom laufenden Hintergrund entfernt | |
| ☐ | Gerät nach unten von Brücke/Dach/Kran über Gras, Asphalt, Wasser | Höhe korrekt, kein Dauer-Coasting | |
| ☐ | Gerät auf Fahrzeug (Antenne nach unten) während der Fahrt: Höhe stabil, keine Sprünge | Streuung < 0,1 m | |
| ☐ | Neigung ±15° (Längs-/Querlage simulieren): Höhe weiter gültig | gültig | |

## 12 Flugerprobung

| ☐ | Prüfung | Soll | Ist |
|---|---|---|---|
| ☐ | Vor dem Flug `bg on`, `save`, `info` prüfen | `bg on` | |
| ☐ | UART-Log (`out 2`) und Referenz (GPS-Höhe über Boden, Barometer, Video) aufzeichnen | – | |
| ☐ | Tiefe Überflüge in 10, 30, 100, 200 m über flachem Gelände | Abweichung notieren | |
| ☐ | Höhe gegen GPS-Höhe über Grund bei verschiedenen Fluggeschwindigkeiten (erwartet: bei 30 m/s ≈ 0,5 m zu niedrig, bei 60 m/s 1–2 m) → Daten für die Kantenkorrektur `ALT_EDGE_CORR_*` | notieren | |
| ☐ | Landeanflüge mit unterschiedlicher Sinkrate: Verlauf bis zum Aufsetzen (unter ~4 m ungültig ist erwartet) | plausibel | |
| ☐ | Statusbit `0080` (degradiert) nur im Endanflug unter ~10 m | ok | |
| ☐ | Höhe > Messbereich: sauber ungültig, keine Scheinhöhen | ok | |

---

## Protokoll

| Datum | Modul / Seriennr. | Firmware (Git-Hash) | Hub lang / kurz [MHz] | Offset [m] | Gain-Verhältnisse | Bemerkungen |
|---|---|---|---|---|---|---|
| | | | | | | |
