# Hardware und Firmware-Ressourcen

Diese Seite beschreibt **Rev. A** (STM32F405, Schaltplan `HF_Radar_uC_24GHz_v5`). Rev. B
(STM32F446 auf dem WeAct-Board, TLV9064) steht in
[hardware/rev_b_f446/README.md](../hardware/rev_b_f446/README.md). Die Firmware ist für beide
gleich bis auf `firmware/bsp` (`make -C firmware BOARD=reva|revb`).

## Signalpfad

```
STM32 PA4 (DAC) ─ R3/C5 ─ LM358 (U1A, Verstärker) ─ Sallen-Key-TP (U1B, ~5 kHz) ─ VCO-Eingang K-LC1a
K-LC1a IF ─ TL074 (Vorverstärker, CD4052-Gainumschaltung über PB0/PB1)
          ─ 3× OP27 (je HP 300 Ω/470 nF ≈ 1,1 kHz, TP 10 kΩ/200 pF ≈ 80 kHz) ─ PC4 (ADC1_IN14)
```

**Achtung Rev. A mit der kurzen Rampe (1,17 ms):** Die Schwebung liegt bei 513 Hz/m (200 m ≈
103 kHz). Die drei Tiefpässe bei 80 kHz dämpfen große Höhen (bei 200 m etwa −8 dB), und das
VCO-Filter (~5 kHz) rundet die Rampenecken über ~0,1 ms. Das liegt noch in den verworfenen
160 µs am Rampenanfang, sollte aber mit dem Oszilloskop geprüft werden (Checkliste, Schritt 3).

## Pinbelegung

| Pin | Funktion |
|---|---|
| PA4 | DAC1_OUT1 → VCO-Rampe |
| PC4 | ADC1_IN14 ← ZF-Signal |
| PB0 / PB1 | Analog-Switch A / B (über BC847 invertiert) → CD4052 Verstärkung |
| PA9 / PA10 | USART1 TX / RX, 115200 8N1 |
| PA11 / PA12 | CAN1 RX / TX (externer Transceiver nötig) |
| PC15 / PC14 | LED1 Herzschlag / LED2 „Höhe gültig“ |
| PC13 (FCT2) | Debug-Ausgang: high während der Verarbeitung einer Rampe (Laufzeitmessung) |
| PB12 (FCT1) | Eingang mit Pull-up (reserviert) |
| PA13/PA14 | SWD |

Verstärkungsstufen (wie alte Firmware): 0: A=1,B=1 · 1: A=0,B=1 · 2: A=1,B=0 · 3: A=0,B=0;
Stufe 0 = kleinste Verstärkung.

## Peripherie und Timing

| Ressource | Verwendung |
|---|---|
| HSE 8 MHz → PLL 168 MHz | APB1 42 MHz (Timer 84 MHz), APB2 84 MHz; CSS aktiv, bei Quarzausfall automatisch HSI-PLL |
| TIM2 | 300 kHz TRGO → triggert DAC **und** ADC |
| DMA1 Stream5 Ch7 | Rampentabelle → DAC (zirkulär, 2×352) |
| DMA2 Stream0 Ch0 | ADC → RAM (zirkulär, gleiche Länge, HT/TC-Interrupt = Rampenende) |
| USART1 + IRQ | Ringpuffer TX 2 kB / RX 256 B |
| CAN1 | 125/250/500/1000 kbit/s, nur Senden, automatische Bus-Off-Erholung |
| IWDG | 250 ms; wird nur bedient, solange Rampen ankommen (beim Flash-Löschen 32 s) |
| Flash Sektor 11 (Rev. B: Sektor 7) | Konfiguration (CRC32) |
| CCM-RAM (nur Rev. A) | DSP-Arbeitspuffer; DMA-Puffer liegen im SRAM |

Rampe: 352 Samples = 1,173 ms (300 kHz), davon 256 ausgewertet. Die Verarbeitung einer Rampe
(FFT 512, Mittelung, alle 7 Paare die Kantensuche) muss unter 1,17 ms bleiben; geschätzt
0,3–0,5 ms, mit FCT2 (Rev. A) bzw. PB13 (Rev. B) am Oszilloskop messen. Überläufe zeigt `stat`
bzw. Statusbit `0x0040`.

| Modul | DAC-Bereich | Hub (Startwert, **muss kalibriert werden**) |
|---|---|---|
| K-LC1a | 0 … 2480 | 90 MHz (abgeleitet aus den Konstanten der alten Firmware) |
| IVS-465 | 75 … 4091 | 150 MHz (Schätzung) |

## Kommandozeile (UART)

`help`, `info`, `stat`, `out 0|1|2`, `module klc1a|ivs465`, `gain auto|0..3`,
`cal1 <m>`, `cal2 <m>`, `zero [m]`, `sweep <MHz>`, `rampq <q>`, `bg on|off`,
`vsign 1|-1`, `range <m>`, `dacbuf 0|1`, `can on|off|<kbps>|id <hex>`, `dump`, `save`,
`defaults`, `reset`, `abort`.

Änderungen gelten sofort und werden erst mit `save` dauerhaft gespeichert.

### Kalibrierung

`cal1 <m>` und `cal2 <m>` mitteln jeweils 256 Messzyklen (≈ 4 s) der dopplerfreien
Beat-Frequenz `f_R` des **stärksten** Echos (Punktziel). Daraus ergeben sich der wirksame Hub
`B` und der Offset `R0` (Laufzeiten, Einbauort). Empfehlung: Winkelreflektor oder
Metallplatte in zwei deutlich verschiedenen Abständen (z. B. 10 m und 40 m), freies Umfeld,
`bg off` (der Reflektor ruht). `zero` setzt danach nur den Offset (Rad-Boden-Abstand); dabei
misst die Firmware die Vorderkante des Bodens, also auch hier `bg off`.

### Hintergrund (Leckage, Fahrwerk)

Mit `bg on` (Standard) entfernt die Firmware laufend alle Echos im Nahbereich bis 25 m, deren
Phase sich von Rampe zu Rampe nicht ändert: Leckage, Radom, Fahrwerk. Das bewegte Bodenecho
bleibt erhalten. Verfahren: [signal_chain.md, Schritt 3a](signal_chain.md). Keine Aufnahme und
kein Flash-Speicher nötig; Einlaufzeit 8 Rampen, Zeitkonstante 2 s. Während `cal1`/`cal2` ruht
der Abzug.

**Einschränkung:** Ein Ziel im Nahbereich, das relativ zum Radar ruht, wird ebenfalls entfernt
(am Boden stehend, Labortest mit festem Reflektor in < 25 m). Für solche Tests `bg off`.

### Rampe / VCO

* `vsign -1`, falls die VCO-Frequenz mit steigender Spannung *fällt* (vertauscht nur die
  Rampenrollen; erkennbar am falschen Vorzeichen der Geschwindigkeit in `$RALT`).
* `rampq` = quadratische Vorverzerrung der Rampe gegen eine gekrümmte VCO-Kennlinie.
  Kriterium: möglichst schmale Spitze eines Reflektors in großer Entfernung (`dump` auswerten).
* `dacbuf 0` schaltet den DAC-Ausgangspuffer ab. Mit Puffer kann der DAC nur etwa 0,2 V …
  VDDA−0,2 V ausgeben, die Rampe des K-LC1a (Start bei Code 0) wird also unten abgeflacht.
  Dieser Bereich liegt größtenteils in den verworfenen 48 Randsamples.
