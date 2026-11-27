# Review: Tests, gefundene Schwachstellen, offene Punkte

## Testumfang

`test/` enthält einen Radar-Simulator und treibt damit die **unveränderte** Firmware-Pipeline
(`altimeter.c` + `rdsp`). Die GitHub-Actions-CI führt alle Tests normal und mit
Address-/Undefined-Behaviour-Sanitizer aus und baut die Firmware für beide Boards.

Der Simulator bildet ab:
- **Boden als Fläche** (`sim_ground`): 512 Streupunkte in der Keule 34° × 80°, je eigene
  Entfernung, eigener Doppler aus Fluggeschwindigkeit und Steigrate, eigene Phase (Speckle);
  Empfindlichkeit nach dem K-LC1a-Datenblatt inklusive 1/f-Rauschen und TLV9064; Rückstreuung
  σ⁰ wählbar (−10 dB Schnee/Gestrüpp, 0 dB Wiese, +10 dB Asphalt/Wasser).
- Punktziele (Reflektor, Fahrwerk, Mehrwegeecho, Störer), feste Echos mit konstanter Phase.
- Antennen-Leckage, umschaltbare Verstärkung, Front-End- und ADC-Rauschen, 3 ZF-Hochpässe mit
  Gedächtnis über die Rampen, 12-Bit-Quantisierung, Clipping, eingefrorener ADC.

| Test | Ergebnis (Simulator) |
|---|---|
| FFT gegen DFT, Fenster, PSD-Skalierung, Welch | rel. Fehler < 2·10⁻⁵, PSD ±5 % |
| Vorderkante (synthetisch, auch mit zu DC abfallendem Rauschen) | 40,37 statt 40,3 Bin |
| Kohärenter Hintergrundabzug | festes Echo vollständig entfernt, bewegtes Echo unverändert |
| Wiese, 30 m/s, 5 … 100 m | 100 % gültig, mittlerer Fehler −0,2 … −0,6 m, max. 1,0 m |
| 100 m Wiese / 200 m und 250 m Asphalt | 100 % gültig, max. 0,9 m / 1,0 m / 0,7 m |
| Sink-/Steigflug −12 … +3 m/s in 100 m | Höhe max. 1,6 m, Geschwindigkeit unverzerrt (±0,1 m/s), Streuung ≈ 0,5 m/s |
| Nur Rauschen, 20 s | 0 gültige Ausgaben |
| Boden 40 m + Mehrwegeecho bei 80 m | richtige Höhe (−0,5 m) |
| Starker Störer bei 20 m in jeder 40. Rampe | ignoriert, max. 0,9 m |
| Echo weg / wieder da | nach 0,7 s ungültig, nach 0,1 s wieder gültig |
| Gebäudekante 60 → 45 m | nach 0,12 s übernommen |
| Übersteuerung / schwacher Boden in 150 m | AGC auf Stufe 0 bzw. 3, keine Fehlmessung bei Clipping |
| Landung 30 → 4,5 m bei 3 m/s, 30 m/s Fahrt | gültig, max. 0,9 m |
| Flare 3 → 0,8 m/s zwischen 12 und 5 m | gültig, max. 1,0 m |
| ADC eingefroren | HW_FAULT, ungültig, Erholung |
| VCO-Richtung invertiert | korrekt mit `vsign -1` |
| Fahrwerksecho 4,5 m (stark) + Boden 35 m | ohne Abzug falsch, mit laufendem Abzug nach 0,1 s 35 m; Sinkflug bis 7 m max. 1,4 m |
| Festes Ziel 6 m mit `bg on` | wird entfernt (erwartet, am Boden `bg off`) |
| 2-Punkt-Kalibrierung mit Reflektor (Sollhub 110 MHz, Start 90 MHz) | 110,004 MHz |

Reichweitentabelle über Boden und Fluggeschwindigkeit: `make -C test range`, Ergebnis in
[hardware/rev_b_f446/README.md](../hardware/rev_b_f446/README.md#mögliche-reichweite).

**Nicht modelliert:** VCO-Nichtlinearität, Temperaturdrift, Fluglage (Neigung, Rollwinkel),
winkelabhängige Rückstreuung σ⁰(θ), Geländeneigung, reale Rauschform der Hardware. Die
Kalibriertabelle der Kantenlage stammt aus dem Modell und muss mit Flugdaten bestätigt werden.

## Durch Tests gefundene und behobene Schwachstellen

1. **Punktziel-Auswertung im Vorwärtsflug unbrauchbar** (Bodenmodell): Die alte Kette (lange
   Rampe, OS-CFAR, nächstes Maximum) meldete über Wiese bei 30 m/s gültige Werte mit bis zu
   9 m Fehler, bei 60 m/s über 20 m. *Fix:* kurze Rampen, robuste Mittelung über viele Rampen,
   Vorderkante statt Maximum (siehe [signal_chain.md](signal_chain.md)).
2. **Ansteigendes Rauschen sieht aus wie eine Kante** (Hochpässe): Fehlalarme. *Fix:* Rauschen
   vor der Kante als Maximum aus Median, extrapoliertem Trend und Untergrenze.
3. **Leckage und Hochpass-Nachschwingen bei DC** ergaben Scheinkanten bei ~4 m. *Fix:* Ein
   Hügel, der schon im untersten Bin beginnt, wird übersprungen; Rampen aus der Einlaufzeit
   des Hintergrundabzugs gehen nicht in die Mittelung ein.
4. **Rückkopplung Verzögerungsausgleich ↔ Tracker**: Bei schwachem Echo schaukelte sich die
   Geschwindigkeit bis ±25 m/s auf. *Fix:* Ausgleich mit geglätteter Doppler-Geschwindigkeit,
   kleines β, Doppler bei schwachem Echo geglättet in den Tracker.
5. **Einzelner Störpuls setzte die Mittelung zurück** (AGC schaltete sofort). *Fix:* AGC
   schaltet erst nach 3 übersteuerten Rampen herunter.
6. **Künstliche Kante an der Bandgrenze des Hintergrundabzugs** durch Nebenzipfel eines starken
   Fahrwerksechos. *Fix:* Band 25 m.
7. **Schwache Einzelkanten versetzten den Track** (Sprung auf eine Rauschkante). *Fix:* Messung
   aus nur einer Rampenrichtung zählt nur innerhalb des Gates.
8. Frühere Funde (gelten weiter): instabile Rückkopplung bei Einzelrampen (nur Position
   nachführen), Tracker extrapolierte im Zustand LOST, falsche Up/Down-Paarung bei geringer
   Höhe.

## Unterschiede zur alten Firmware (Robustheit)

* DAC und ADC laufen mit **einem** Trigger und zirkulärem DMA, damit synchron.
* Stetige Dreieckrampe statt Rücksprung: keine Anregung der ZF-Hochpässe.
* Auswertung für ein ausgedehntes Ziel (Vorderkante), Doppler-Kompensation über Auf/Ab-Rampen,
  Tracking mit Gating und definierte Gültigkeit.
* Laufender Abzug fester Echos (Leckage, Fahrwerk) ohne Abgleich.
* Watchdog, Taktüberwachung (CSS) mit Notbetrieb, CRC-geschützte Konfiguration,
  Selbsterkennung eingefrorener ADC-Daten, automatischer Frontend-Neustart.
* Keine blockierenden Ausgaben im Messbetrieb (Ringpuffer, CAN nicht blockierend).

## Offene Punkte / Grenzen (bitte beachten)

1. **Systematischer Fehler durch die Fluggeschwindigkeit:** Die Doppler-Spreizung zieht die
   Vorderkante nach vorn, die Höhe ist zu klein: bei 30 m/s um etwa 0,5 m, bei 60 m/s in
   geringer Höhe um 1–2 m. Abhilfe wäre die Fluggeschwindigkeit als Eingang (GPS über CAN) und
   eine Korrekturtabelle.
2. **Kalibrierung der Kantenlage** (`ALT_EDGE_CORR_*`, bis 1,7 m) ist aus dem Modell abgeleitet.
   Sie hängt von Keule und Bodenart ab und muss mit Flugdaten (GPS-Höhe über Grund, Überflüge
   in bekannter Höhe) geprüft werden.
3. **Rauschform der echten Hardware:** Die Kantensuche ist gegen ansteigendes und fallendes
   Rauschen abgesichert, getestet ist das aber nur mit der Rauschform des Simulators. Auf der
   Hardware: 10 min ohne Ziel (Antenne zum Himmel) dürfen keine gültigen Werte entstehen.
4. **Frequenzhub unbekannt** → ohne Kalibrierung gilt `UNCAL`.
5. **Minimale Höhe ≈ 4 m** bei 90 MHz Hub (Kante unter 4 Bins: `DEGRADED`, darunter keine
   Messung).
6. **Weicher Boden, Schnee (σ⁰ ≈ −10 dB):** nur bis etwa 30 m.
7. **Verstärkungsstufen** (Rev. A: CD4052) sind nicht bekannt. Die AGC setzt ein
   Stufenverhältnis ≤ 4,25 voraus. Die Höhe hängt nicht von der Verstärkung ab.
8. **Rev. A mit der kurzen Rampe:** Die ZF-Tiefpässe (80 kHz) dämpfen die Schwebung großer
   Höhen; VCO-Filter (~5 kHz) an den Rampenecken prüfen. Rev. B ist dafür ausgelegt.
9. **Rechenzeit** pro Rampe (Budget 1,17 ms) am Gerät messen (FCT2 bzw. PB13).
10. **Hardware-Test steht aus**: Taktbaum, DMA/Trigger-Kopplung, CAN-Bit-Timing,
    Flash-Speicher und Signalpegel am Gerät verifizieren (`dump`, `stat`).
11. Keine Avionik-Zertifizierung (DO-178C/DO-254); nur für Experimentalzwecke.
