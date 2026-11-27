# Hardware Rev. B – STM32F446 (WeAct) + K-LC1a + TLV9064

![Schaltplan](schaltplan.svg)

> **Status: Entwurf, weder aufgebaut noch gemessen.** Die Pinbelegung des WeAct-Boards
> (HSE 8 MHz, LED PB2, µSD an PC8–PC12/PD2, USB an PA11/PA12) stammt aus der
> Zephyr-Boardbeschreibung und muss vor dem Layout am Board-Schaltplan geprüft werden.

## Auslegung

### VCO-Ansteuerung (Block 3)

| Punkt | Wahl | Begründung |
|---|---|---|
| Teiler R10/R11 | U_tune = 0,6 · U_DAC | Der DAC-Puffer ist aktiv (0,2…3,1 V), damit ergibt sich U_tune = 0,12…1,86 V. Damit bleibt man innerhalb der Datenblattgrenze von 2,0 V, und 12 bit lösen ≈ 0,4 mV ≈ 32 kHz auf. |
| Sallen-Key (U2A) | fc ≈ 18 kHz, Q ≈ 0,74 | Glättet die Stufen des DAC-Takts (300 kHz: −49 dB). Die Gruppenlaufzeit (≈ 13 µs) und das Einschwingen an den Rampenecken (≈ 30 µs) liegen unter dem verworfenen Rampenkopf (160 µs). |
| R13 = 47 Ω | Treiber | Der VCO-Eingang (570 Ω gegen ≈ 0,9 V intern) braucht Quelle und Senke bis 1,7 mA; das kann der TLV9064. |
| MON_VTUNE | ADC PC5 | Die Firmware kann prüfen, ob die Rampe am Modul wirklich ankommt (Unterbrechung, OPV-Defekt). |
| RF_EN | LDO-EN, Pull-down | Bei Reset ist das Modul aus und sendet nicht unkontrolliert. Außerdem lässt sich der Rauschboden bei abgeschaltetem Sender messen (Selbsttest). |

**ISM-Band:** Die Mittenfrequenz hat eine Toleranz von ±75 MHz (24,05…24,25 GHz). Die Rampe muss deshalb
per Kalibrierung so liegen, dass sie das Band nicht verlässt. Die Firmware-Vorgabe von 90 MHz Hub
entspricht etwa 1,1 V an U_tune.

### ZF-Kette (Block 6)

Die Stufen im Einzelnen:

1. **Last und Kopplung:**
   - R20 = 1 kΩ ist die Last laut Datenblatt.
   - C20 = 100 pF unterdrückt HF-Einstreuung.
   - C21 = 1 µF Folie mit R21 bildet einen Hochpass bei 34 Hz und trennt den Modul-Offset ab (10…200 mV).
2. **U1A, Vorverstärker V = 11:**
   - Der Gegenkopplungszweig ist niederohmig (470 Ω / 4,7 kΩ) und rauscht nur ≈ 2,8 nV/√Hz.
   - DC-Verstärkung 1 über C22, damit kein Offset verstärkt wird.
3. **C23/R24, Hochpass 106 kHz (Entfernungskompensation):**
   - Das Bodenecho fällt etwa mit 1/R, die Schwebung steigt mit R (513 Hz/m, 200 m ≈ 103 kHz). Ein Hochpass 1. Ordnung bei der Schwebung der größten Höhe macht den Pegel über die Höhe etwa flach und spart Dynamik.
   - Zusätzlich dämpft er die Eigenmischung (Gleichanteil, der mit der VCO-Spannung wandert, also im 426-Hz-Takt der Rampe) um ≈ 48 dB.
4. **U1B + U7 (TS3A5017), Verstärkung umschaltbar 1 / 4,04 / 14,6 / 55,5:**
   - Die Stufen sind ≤ 4,25 und passen damit zur AGC der Firmware.
   - Der Schalter liegt im hochohmigen Rückkopplungszweig, sein R_on (≈ 12 Ω) geht deshalb kaum ein.
   - R25 = 100 kΩ hält die Schleife geschlossen, während umgeschaltet wird.
5. **U1C + U2B, Anti-Alias Butterworth 4. Ordnung, 120 kHz** (zwei Sallen-Key-Stufen, Q 0,55 und 1,29), dann R32/C27 als Ladungspuffer für den ADC (PC4). Bei 200 kHz −18 dB: Rauschen oberhalb der Nyquistfrequenz (150 kHz) faltet kaum in das Nutzband bis 103 kHz.
6. **U1D, VMID = A3V3/2 gepuffert:**
   - VMID1 ist für den Eingang zusätzlich gefiltert.
   - Der Ruhepegel liegt bei 2048 LSB und passt damit zur HW-Fehlererkennung (200…3895 LSB).

Gesamtverstärkung 11…610 (21…56 dB). Bei 610 liegt das Rauschen von Modul und erster Stufe (≈ 12 nV/√Hz
bei 50 kHz) am ADC bei ≈ 7 µV/√Hz und damit über dem ADC-Rauschen (≈ 2 µV/√Hz).
Stufe 2 hat bei Verstärkung 55,5 nur noch ≈ 180 kHz Bandbreite; bei 100 kHz ist das ≈ 1 dB Abfall, unkritisch.

**Rauschen TLV9064:**
- Der TLV9064 hat ≈ 10 nV/√Hz, das Modul bei 50 kHz ≈ 5 nV/√Hz (1/f). Bei großer Höhe kostet der OPV deshalb mehrere dB; im Simulator ist das berücksichtigt.
- Ein rauschärmerer erster Verstärker (≤ 3 nV/√Hz, z. B. ADA4897-1) brächte ≈ 15 % mehr Reichweite.
- Der Rest kann beim TLV9064 bleiben.

**Kondensatoren im Signalpfad** nur in C0G, Folie oder Tantal. X7R/X5R sind piezoelektrisch und machen
aus Vibrationen ein Störsignal im ZF-Band.

### Versorgung (Block 1)

```
VIN 9–32 V → F1 → D1 (Verpolung) → D2 TVS → L1/C2 → U3 Buck 6,0 V ─┬→ U4 LDO 5V_D → Core-Board (D3), CAN, U6 → A3V3
                                                                   └→ FB1 → U5 ADP7142 5V_RF → Modul (EN = RF_EN)
```

- Das Modul unterdrückt Störungen auf der Versorgung nur um **26 dB**, deshalb bekommt es einen eigenen
  rauscharmen Regler.
- Der Buck läuft mit 0,7 MHz, also nicht mit einem Vielfachen von 300 kHz, und liegt räumlich weit weg von der ZF.

### Pinbelegung STM32F446

| Funktion | Pin | Bemerkung |
|---|---|---|
| DAC Rampe | PA4 (DAC1) | DMA1 Stream5 Ch7, wie F405 |
| ZF-ADC | PC4 (ADC1_IN14) | DMA2 Stream0 Ch0, wie F405 |
| MON_VTUNE / MON_5VRF / MON_VIN | PC5 / PB0 / PB1 | Injected oder langsam per Polling |
| GAIN_A / GAIN_B | PC6 / PC7 | TS3A5017 IN1/IN2 |
| RF_EN | PB12 | 100 kΩ Pull-down |
| CAN1 TX / RX | PB9 / PB8 (AF9) | PA11/PA12 belegt der USB-Anschluss |
| USART2 TX / RX | PA2 / PA3 | |

### Firmware

Build mit `make -C firmware` (Standard `BOARD=revb`, Rev. A: `BOARD=reva`).

- Gleicher Takt wie Rev. A: 168 MHz aus HSE 8 MHz (keine Over-Drive-Umschaltung nötig), TIM2 300 kHz.
- Konfiguration in Flash-Sektor 7 (0x08060000).
- Kein CCM: Alle Puffer liegen im SRAM, belegt sind 27 KB von 128 KB (inkl. 8 KB Stack).
  Flash 36 KB.
- Pins wie in der Tabelle; RF_EN wird erst unmittelbar vor dem Start der Rampe gesetzt.
- DAC-Standardbereich K-LC1a: 724…3000 (U_tune 0,35…1,45 V um die Leerlauffrequenz).
- Wellenform: Dreieck 2 × 1,17 ms, Kantensuche auf dem gemittelten Spektrum (siehe
  [docs/signal_chain.md](../../docs/signal_chain.md)).
- Noch offen: Selbsttest über MON_VTUNE, MON_5VRF, MON_VIN; Polarität der Board-LED PB2 prüfen.

## Mögliche Reichweite

### Simulation der kompletten Firmware

`make -C test range` (Quelle [test/range_sim.c](../../test/range_sim.c)) lässt die unveränderte
Firmware-Pipeline gegen das Bodenmodell des Simulators laufen:

- Boden aus 512 Streupunkten in der Keule 34° (in Flugrichtung) × 80°, Speckle,
  Doppler aus der Fluggeschwindigkeit, ebenes Gelände, Antenne senkrecht nach unten;
- Empfindlichkeit nach Datenblatt (−114 dBc / 500 Hz bei S/N 6 dB), 1/f-Rauschen des Moduls
  (−134 dBm/Hz bei 500 Hz, −164 dBm/Hz bei 1 MHz) plus TLV9064, 3 dB Verluste;
- je Fall 1,5 s Einlaufen aus dem Kaltstart, dann 3 s Auswertung.

Höhe, bis zu der mindestens 95 % der Ausgaben gültig sind, und der größte Fehler darin
(mittlere Abweichung / 95-%-Quantil):

| Boden (σ⁰) | 10 m/s | 30 m/s (Anflug) | 60 m/s |
|---|---|---|---|
| −10 dB (Schnee, Gestrüpp) | 30 m, 0,35 / 0,86 m | 30 m, 0,45 / 0,69 m | 20 m, 1,7 / 2,1 m |
| 0 dB (Wiese, Acker) | 125 m, 0,39 / 0,68 m | 100 m, 0,56 / 0,99 m | 100 m, 1,9 / 2,5 m |
| +10 dB (Asphalt, Wasser senkrecht) | 250 m, 0,40 / 1,07 m | 250 m, 0,58 / 3,4 m | 250 m, 2,0 / 2,8 m |

Kleinste Höhe etwa 4 m (darunter liegt das Echo bei DC). Über 250 m wurde nicht simuliert.

### Folgerungen

- **Über Wiese reicht das K-LC1a für etwa 100 m, über Asphalt oder Wasser für 250 m.** Für
  200 m über jedem Gelände fehlen rund 10 dB: mehr Sendeleistung oder Antennengewinn (Modul mit
  größerer Antenne, Linse/Horn), ein rauschärmerer erster Verstärker bringt nur wenige dB.
- **Fluggeschwindigkeit:** Die Doppler-Spreizung zieht die Vorderkante nach vorn. Die Höhe ist
  bei 30 m/s um etwa 0,5 m, bei 60 m/s um 1–2 m zu klein. Mit der Fluggeschwindigkeit als
  Eingang (GPS über CAN) ließe sich das korrigieren.
- Die schmale 34°-Ebene muss **in Flugrichtung** liegen (Patch-Spalten längs).
- Die Werte gelten für das Modell. σ⁰ schwankt real um ±10 dB, Fluglage und Geländeneigung
  sind nicht enthalten. Messen statt glauben: Checkliste
  [`docs/inbetriebnahme.md`](../../docs/inbetriebnahme.md), Abschnitt Flugerprobung.

Die frühere Abschätzung nur aus der Radargleichung steht in
[range_estimate.py](range_estimate.py); sie kommt für die Detektion zu ähnlichen Größenordnungen.

## Stückliste (Hauptteile)

| Ref | Teil | Funktion |
|---|---|---|
| — | WeAct STM32F446 Core Board | MCU |
| M1 | RFbeam K-LC1a (5 V, mit FM-Eingang) | Radar |
| U1, U2 | TLV9064IPWR | ZF-Kette, VCO-Treiber, 2. Anti-Alias-Stufe |
| U3 | LMR16006XDDCR | Buck 6 V, 4…60 V Eingang |
| U4 | TLV76750DCYR | LDO 5 V digital |
| U5 | ADP7142ARDZ-5.0 | LDO 5 V Radar, rauscharm |
| U6 | LP5907MFX-3.3 | LDO 3,3 V analog |
| U7 | TS3A5017PWR | Verstärkungsumschaltung |
| U8 | TCAN1042VDRQ1 | CAN mit VIO 3,3 V |
| D1 / D2 / D4 | SS210 / SMBJ33A / B160 | Verpolung / TVS / Freilauf |
| D5 / D6 | NUP2105L / PESD3V3S2UT | ESD CAN / UART |
| C21 | 1 µF Folie (PPS/PET) | ZF-Kopplung |
| C23, C25, C26, C32, C33 | C0G 1,5n / 1n / 820p / 2,2n / 330p | Entfernungs-Hochpass, Anti-Alias |
| C22, C24, C14, C28, C29, C31 | Tantal / Polymer | nicht mikrofonisch |
