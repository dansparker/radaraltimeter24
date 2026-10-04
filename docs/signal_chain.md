# Signalverarbeitungskette

Die Kette ist in unabhängige Module der Bibliothek `lib/rdsp` zerlegt. Jedes Modul arbeitet
auf vom Aufrufer bereitgestellten Puffern (kein `malloc`), nutzt nur `float` und ist auf dem
PC wie auf dem Cortex-M4F identisch lauffähig. Die anwendungsspezifische Verknüpfung steht in
`firmware/app/altimeter.c`. Alle Parameter liegen in `firmware/app/radar_params.h`.

## Grundlagen FMCW

Der VCO wird linear über den Hub `B` in der Zeit `T` gewobbelt (Steigung `S = B/T`).
Ein Ziel in der Entfernung `R` erzeugt im ZF-Signal einen Ton

```
f_R = 2·S·R / c0                 (Entfernungs-Beat)
f_D = 2·v_c / λ                  (Doppler, v_c = Annäherungsgeschwindigkeit, λ = 12,4 mm)
steigende Rampe:  f_up = |f_R − f_D|
fallende Rampe:   f_dn = |f_R + f_D|
```

Daraus `f_R = (f_up + f_dn)/2` (dopplerfrei) und `f_D = (f_dn − f_up)/2`. Deshalb wird eine
**Dreieckmodulation** verwendet: Bei 24 GHz erzeugen schon 1 m/s Sinkrate 161 Hz Doppler. Mit
einer reinen Sägezahnrampe ergäbe das 2,2 m Höhenfehler pro m/s.

**Taktunabhängigkeit:** `f_R` wird in FFT-Bins gemessen (`bin·fs/N`), die Steigung ist
`S = B·fs/(L−1)`. Damit kürzt sich `fs` heraus: `R = c0·bin·(L−1)/(2·B·N)`. Die Höhe hängt nicht
von der Quarzgenauigkeit ab (wichtig für den HSI-Notbetrieb), nur die Geschwindigkeit.

**Auflösung:** `ΔR = c0 / (2·B_ausgewertet)`. Mit 90 MHz Hub sind das etwa 2 m pro Bin. Die
Messgenauigkeit ist durch die Interpolation (Schritt 6) viel besser als die Auflösung. Die
**kleinste messbare Höhe** liegt bei etwa 2 Bins (≈ 4 m bei 90 MHz): Darunter verschmilzt das
Ziel mit DC und Antennen-Leckage. Mehr Hub verringert diese Grenze proportional.

## Rampenmodi (lang / kurz)

| | lang | kurz |
|---|---|---|
| Samples (Rand + Auswertung + Rand) | 192 + 2048 + 192 = 2432 | 96 + 512 + 96 = 704 |
| Rampendauer | 8,107 ms | 2,347 ms |
| Messpaare/s | 61,7 | 213 (Ausgabe auf ~53 Hz dezimiert) |
| Entfernung pro Bin (90 MHz) | 1,98 m | 2,29 m |
| Höhenfehler pro m/s Doppler-Fehler | 2,17 m | 0,63 m |
| Beat bei 5 m (90 MHz) | 370 Hz | 1280 Hz |

Die Kopplung zwischen Doppler und Entfernung ist proportional zur Rampendauer
(`ΔR = v·T·f0/B`). Bei geringer Höhe und Sinkflug ist sie entscheidend, weil dort eine Rampe
oft nur mit vorhergesagtem Doppler ausgewertet werden kann. Kurze Rampen schieben außerdem
die Beat-Frequenzen nach oben, weg von DC, von der Leckage und von den ZF-Hochpässen (~1,1 kHz,
3. Ordnung: bei 370 Hz −29 dB, bei 1280 Hz nur −4 dB). Die lange Rampe hat 6 dB mehr
Integrationsgewinn und bleibt deshalb für die Zielerfassung und große Höhen zuständig.

**Automatik (`rmode auto`):** gültige Höhe < 25 m → kurz; gültige Höhe > 35 m oder Track
verloren → lang (Hysterese 10 m). Beim Wechsel wird das Frontend neu gestartet; der Tracker
läuft weiter, nur die Spektrenmittelung wird zurückgesetzt. Coasting-Zeiten sind in Sekunden
definiert und gelten für beide Modi gleich. Während Kalibrierung und Hintergrundaufnahme ist
der Modus fest.

## Erfassung (Firmware, `bsp/frontend.c`)

Ein einziger Timer (TIM2, 300 kHz) triggert **DAC und ADC gleichzeitig**. Beide laufen
per DMA in Endlosschleifen gleicher Länge (2 × Rampenlänge), damit ist Sample *i* fest an
Rampenwert *i* gekoppelt, ohne Software-Timing und ohne Neustart zwischen den Rampen.
Halb- und Voll-Interrupt markieren das Ende der steigenden bzw. fallenden Rampe.

* Rampe: 2432 Samples = 8,107 ms; ausgewertet werden die mittleren 2048. Die ersten und letzten
  192 Samples (0,64 ms) enthalten den Umkehrpunkt: Einschwingen von Sallen-Key-Filter, VCO und
  ZF-Hochpässen, außerdem die DAC-Begrenzung nahe 0 V.
* Das Dreieck ist stetig, es gibt also keinen Rücksprung, der die ZF-Hochpässe anstößt.
* Verstärkungswechsel werden exakt an Rampengrenzen geschaltet. Die folgende Rampe wird als
  „settling“ markiert und verworfen.

## Schritt 0 – AGC (`rdsp_agc`)

Eingang: Spitzenauslenkung relativ zum verfügbaren Aussteuerbereich (`peak_frac`) und Clipping.
Bei Clipping oder `peak_frac > 0,85` wird sofort eine Stufe zurückgeschaltet. Erst nach 20
Rampen mit `peak_frac < 0,2` wird eine Stufe hochgeschaltet. Bedingung gegen Pendeln:
`lo·Stufenverhältnis < hi`. Nur Rampen, die schon mit der aktuell angeforderten Verstärkung
aufgenommen wurden, gehen in die Regelung ein; sonst würde wegen der Totzeit doppelt geschaltet.

## Schritt 1 – Frame-Aufbereitung (`rdsp_frame`)

* `uint16` → `float`, Mittelwert abziehen; Statistik: Mittelwert, RMS, Min/Max, Clipping-Zähler.
* **Gesundheitsprüfung:** RMS < 0,05 LSB (eingefrorene Daten) oder Mittelwert außerhalb
  200…3895 LSB (Arbeitspunkt weggelaufen) über 20 Rampen ergibt `HW_FAULT`.
* Rampen mit mehr als 3 geclippten Samples werden nicht ausgewertet: Die Oberwellen würden
  Scheinziele erzeugen.
* Optional linearer Trend-Abzug (Rest der Leckage, die mit der Rampe mitläuft).
* Optional **Hintergrundabzug**: Für jede Verstärkung und Rampenrichtung wird ein gemittelter
  Frame ohne Ziel gespeichert (Antenne zum Himmel) und im Zeitbereich abgezogen. Das entfernt
  feste Echos (Fahrwerk, Radom, Antennenübersprechen) phasenrichtig.

## Schritt 2 – Fensterung (`rdsp_window`)

Periodisches **Hann-Fenster**: −31 dB Nebenkeulen, die mit 18 dB/Oktave abfallen, 1,5 Bin
Rauschbandbreite. Das ist ein guter Kompromiss zwischen Trennschärfe gegenüber der starken
Leckage bei DC und der Auflösung. Für extrem starke Störer gibt es Blackman-Harris (−92 dB).
Die Summen `s1 = Σw`, `s2 = Σw²` werden für die korrekte Skalierung mitgeliefert.

## Schritt 3 – FFT (`rdsp_fft`)

Reelle FFT der Länge N über eine komplexe FFT der Länge N/2 plus Split-Schritt (Radix-2,
in place). Das Ausgabeformat ist identisch zu CMSIS `arm_rfft_fast_f32`, sodass bei Bedarf
getauscht werden kann. Laufzeit auf dem STM32F405 etwa 1 ms. Getestet gegen eine direkte DFT
(relativer Fehler < 2·10⁻⁵).

## Schritt 4 – Leistungsdichte und Welch-Mittelung (`rdsp_spectrum`)

`PSD[k] = c_k·|X[k]|² / (fs·s2)` (einseitig, `c_0 = 1`, sonst 2), Einheit LSB²/Hz.

**Welch:** Die klassische Methode (Segmente mit Überlappung innerhalb eines Datensatzes,
`rdsp_welch()`) ist enthalten. Für FMCW wäre sie innerhalb einer Rampe aber schädlich:
Jedes Segment überstreicht nur einen Teil des Hubs, die Entfernungsauflösung sinkt. Für den
Höhenmesser wird deshalb **über aufeinanderfolgende Rampen gleicher Richtung** gemittelt
(`rdsp_psd_avg_*`). Anfangs ist das ein laufender Mittelwert, danach exponentiell mit α = 0,5.
Das senkt die Varianz des Rauschbodens und stabilisiert CFAR, ohne Auflösung zu kosten.

Bei Verstärkungswechsel wird die Mittelung zurückgesetzt, Spektren unterschiedlicher
Verstärkung werden nie gemischt.

## Schritt 5 – CFAR-Detektion (`rdsp_cfar`)

Für jede Zelle wird der Rauschpegel aus 12 Trainingszellen je Seite geschätzt, mit 3
Schutzzellen neben dem Kandidaten (Hann-Hauptkeule ±2 Bins). Verwendet wird **OS-CFAR**
(Rang 18 von 24): robust bei mehreren Zielen (Mehrwegeecho) und an Störkanten (Leckage nahe DC).
Der Schwellfaktor α folgt aus der Falschalarmrate (Pfa = 10⁻⁴) und wird beim Start numerisch
berechnet; im Test wurde Pfa = 0,0096 gemessen (Soll 0,01). Gemeldet werden nur lokale Maxima,
also genau eine Detektion pro Spektrallinie, zusätzlich muss SNR ≥ 12 dB gelten.

**Bestätigung im aktuellen Frame:** Detektiert wird im gemittelten Spektrum (stabil). Die
Spitze muss aber auch in der aktuellen Rampe ≥ 9 dB über dem Rauschen liegen, und die
Frequenz wird aus der **aktuellen** Rampe geschätzt. Das hat der Test erzwungen. Ohne diese
Regel hinkt die Mittelung bewegten Zielen hinterher (0,2 m bei 12 m/s), und ein
verschwundenes Ziel bleibt noch ~13 Rampen als „Geist“ sichtbar.

## Schritt 6 – Interpolation (`rdsp_peak`)

Aus Spitzen-Bin und Nachbarn wird die Lage `k + δ` bestimmt. Für Hann gibt es eine exakte
Formel (Grandke): `a = |X[k±1]|/|X[k]|` (größerer Nachbar), `δ = ±(2a−1)/(a+1)`. Der Fehler
im Test liegt unter 10⁻⁴ Bin; die log-parabolische Variante kommt auf 0,016 Bin.

## Schritt 7 – Zielauswahl und Up/Down-Paarung (`rdsp_fmcw`, `altimeter.c`)

* **Ohne Track:** In jeder Rampe wird das *nächstgelegene* Ziel gewählt, dessen Leistung
  höchstens 15 dB unter dem stärksten liegt. Der Boden ist das erste starke Echo; Mehrwegeechos
  bei 2R, 3R sind schwächer und weiter entfernt. Gepaart wird nur, wenn |f_D| zur maximalen
  Vertikalgeschwindigkeit (25 m/s) passt.
* **Mit Track:** Alle Paare und drei Hypothesen (normal, Up-Rampe gespiegelt, Down-Rampe
  gespiegelt) werden gegen Vorhersage geprüft: Entfernungs-Gate und Doppler-Gate (±3 m/s).
  Gewählt wird die Kombination mit minimalen Kosten. Die gespiegelten Fälle treten bei
  geringer Höhe und großer Sinkrate auf (`f_D > f_R`, z. B. unter 6,5 m bei 3 m/s).
* **Einzelrampe:** Liegt eine Beat-Frequenz zu nahe an DC (Landung, `f_R ≈ f_D`), wird die
  andere Rampe mit dem vorhergesagten Doppler verwendet (Status `DEGRADED`).
* Doppler aus Beat-Frequenzen unter 4 Bins (Leckage-Rest, Spiegelanteil) gilt als unzuverlässig
  und geht nicht in die Geschwindigkeit ein.

## Schritt 8 – Tracking (`rdsp_track`)

α-β-Filter (α = 0,4, β = 0,05) auf der Höhe. Die Doppler-Geschwindigkeit fließt mit γ = 0,2
direkt ein, aber nur wenn sie plausibel ist (±3 m/s um den Track). Zustände: LOST → TENTATIVE
(3 Treffer) → CONFIRMED → COAST (Vorhersage, bis 15 Paare ≈ 0,25 s gültig) → LOST (nach ~1 s).
Einzelne Ausreißer außerhalb des Gates werden verworfen. 6 konsistente Messungen außerhalb des
Gates gelten als echter Sprung (z. B. Gebäudekante) und starten den Track neu.

**Wichtig (im Test gefunden):** Einzelrampen-Messungen hängen selbst von der vorhergesagten
Geschwindigkeit ab (2,17 m Höhe pro m/s). Würden sie die Geschwindigkeit nachführen,
entstünde eine instabile Rückkopplung. Sie aktualisieren deshalb nur die Position
(`rdsp_track_update_pos`).

## Wiederverwendung

`lib/rdsp` hat keine Abhängigkeit zur Hardware oder zum Höhenmesser. Typische Nutzung für
ein anderes FMCW-Projekt:

```c
rdsp_rfft_init(&fft, N, tw);                  rdsp_window_make(win, N, RDSP_WIN_HANN, &wi);
rdsp_cfar_init(&cfar, RDSP_CFAR_OS, 12, 3, 0, 1e-4f);
rdsp_frame_condition_u16(adc, N, x, 8, 4087, 4095, &st);
rdsp_window_apply(x, win, N); rdsp_rfft(&fft, x); rdsp_rfft_power(x, x, N);
rdsp_psd_onesided(x, N/2, fs, wi.s2);         rdsp_psd_avg_update(&avg, x);
n = rdsp_cfar_detect(&cfar, avg.acc, N/2, 2, N/2-2, det, 8);
f = (det[0].bin + rdsp_peak_interp(x, N/2, det[0].bin, RDSP_INTERP_HANN)) * fs / N;
```
