# Hardware und Firmware-Ressourcen

Grundlage: Schaltplan `HF_Radar_uC_24GHz_v5` (Radar-Analogteil + Mikrocontroller).

## Signalpfad

```
STM32 PA4 (DAC) ─ R3/C5 ─ LM358 (U1A, Verstärker) ─ Sallen-Key-TP (U1B, ~5 kHz) ─ VCO-Eingang K-LC1a
K-LC1a IF ─ TL074 (Vorverstärker, CD4052-Gainumschaltung über PB0/PB1)
          ─ 3× OP27 (je HP 300 Ω/470 nF ≈ 1,1 kHz, TP 10 kΩ/200 pF ≈ 80 kHz) ─ PC4 (ADC1_IN14)
```

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
| DMA1 Stream5 Ch7 | Rampentabelle → DAC (zirkulär, 2×2432 lang / 2×704 kurz) |
| DMA2 Stream0 Ch0 | ADC → RAM (zirkulär, gleiche Länge, HT/TC-Interrupt = Rampenende) |
| USART1 + IRQ | Ringpuffer TX 2 kB / RX 256 B |
| CAN1 | 125/250/500/1000 kbit/s, nur Senden, automatische Bus-Off-Erholung |
| IWDG | 250 ms; wird nur bedient, solange Rampen ankommen (beim Flash-Löschen 32 s) |
| Flash Sektor 11 | Konfiguration (CRC32) + Hintergrund-Frames |
| CCM-RAM | DSP-Arbeitspuffer (52 kB); DMA-Puffer liegen im SRAM |

Rampen: lang 2432 Samples = 8,107 ms, kurz 704 Samples = 2,347 ms (300 kHz). Im Kurzrampen-Modus
muss die Verarbeitung einer Rampe unter 2,3 ms bleiben (geschätzt ~0,5 ms, mit FCT2 messen;
Überläufe zeigt `stat` bzw. Statusbit `0x0040`).

| Modul | DAC-Bereich | Hub (Startwert, **muss kalibriert werden**) |
|---|---|---|
| K-LC1a | 0 … 2480 | 90 MHz (abgeleitet aus den Konstanten der alten Firmware) |
| IVS-465 | 75 … 4091 | 150 MHz (Schätzung) |

## Kommandozeile (UART)

`help`, `info`, `stat`, `out 0|1|2`, `module klc1a|ivs465`, `gain auto|0..3`,
`cal1 <m>`, `cal2 <m>`, `zero [m]`, `sweep <MHz>`, `rampq <q>`, `bg capture|on|off`,
`vsign 1|-1`, `rmode auto|long|short`, `range <m>`, `dacbuf 0|1`, `can on|off|<kbps>|id <hex>`, `dump`, `save`,
`defaults`, `reset`, `abort`.

Änderungen gelten sofort und werden erst mit `save` dauerhaft gespeichert.

### Kalibrierung

`cal1 <m>` und `cal2 <m>` mitteln jeweils 256 dopplerfreie Messungen der Beat-Frequenz `f_R`.
Daraus ergeben sich der wirksame Hub `B` und der Offset `R0` (Laufzeiten, Einbauort).
Empfehlung: zwei Abstände mit großem Unterschied (z. B. 10 m und 40 m) auf ein großes,
flaches Ziel (Wand, Boden). Kalibriert wird im festen Modus: `rmode long` → Hub + Offset;
optional `rmode short` → eigener Hub der Kurzrampe (falls das VCO-Ansteuerfilter bei der
schnelleren Rampe den Hub verändert). `zero` setzt danach nur den Offset (Rad-Boden-Abstand).

### Hintergrund (Leckage, Fahrwerk)

`bg capture` mittelt für beide Rampenmodi je Verstärkung und Rampenrichtung 128 Rampen
(Gültigkeitsmaske: Bits 0–3 lang, 4–7 kurz). Dabei darf **kein Ziel im
Messbereich** sein (Antenne zum freien Himmel, oder im Flug über 300 m AGL). Danach `bg on` und
`save`. Der Abzug verbessert die Messung bei geringer Höhe und unterdrückt feste Echos am Flugzeug.

### Rampe / VCO

* `vsign -1`, falls die VCO-Frequenz mit steigender Spannung *fällt* (vertauscht nur die
  Rampenrollen; erkennbar am falschen Vorzeichen der Geschwindigkeit in `$RALT`).
* `rampq` = quadratische Vorverzerrung der Rampe gegen eine gekrümmte VCO-Kennlinie.
  Kriterium: möglichst schmale Spitze bei großer Entfernung (`dump` auswerten).
* `dacbuf 0` schaltet den DAC-Ausgangspuffer ab. Mit Puffer kann der DAC nur etwa 0,2 V …
  VDDA−0,2 V ausgeben, die Rampe des K-LC1a (Start bei Code 0) wird also unten abgeflacht.
  Dieser Bereich liegt größtenteils in den verworfenen 192 Randsamples.
