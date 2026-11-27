# KiCad-Projekt Rev. B (KiCad 9)

Inhaltlich gleich wie [`../schaltplan.svg`](../schaltplan.svg). Der SVG-Plan ist die lesbare
Zeichnung. Der KiCad-Schaltplan ist netzlistenartig aufgebaut: Jeder Pin hat ein Netzlabel,
geordnet nach den sechs Blöcken des SVG-Plans.

| Datei | Inhalt |
|---|---|
| `radaraltimeter_revb.kicad_pro / .kicad_sch / .kicad_pcb` | Projekt, Schaltplan, Platine |
| `radar_revb.kicad_sym`, `radar_revb.pretty/` | Projektbibliothek: WeAct-Board, K-LC1a, TS3A5017 (TSSOP), PESD3V3S2UT |
| `radaraltimeter_revb_schaltplan.pdf`, `radaraltimeter_revb_platine.pdf` | Export zum Ansehen ohne KiCad |
| `generator/` | Skripte, aus denen alles erzeugt wurde (`design.py` = Netzliste) |

## Stand

- **Schaltplan:** ERC 0 Fehler, 0 Warnungen.
- **Platine:**
  - 4 Lagen, 86 × 64 mm, 4 × M3 an GND.
  - GND-Fläche auf In1 (durchgehend) und B.Cu; jedes SMD-GND-Pad hat ein eigenes Via auf In1.
  - Bauteile nach Gruppen platziert, Zweipoler per Tausch/Drehung auf kurze Netze optimiert.
  - **Mit Freerouting automatisch geroutet** (F.Cu, In2, B.Cu): DRC mit Schaltplan-Parität
    0 Fehler, 0 offene Verbindungen; übrig sind nur Überlappungen im Bestückungsdruck.
  - **Erster Stand, nicht bestellreif:** Der Autorouter kennt die Analog-Prioritäten nicht.
    Die empfindlichen ZF-Netze sind zu lang (IF_RAW 27 mm, IF_FB1 27 mm, IF_FB2 44 mm,
    ADC_IF 45 mm) und laufen teils über Vias und In2. Vor der Bestellung die ZF-Kette um U1/U7
    von Hand dichter platzieren und diese Netze kurz auf F.Cu über der GND-Lage führen.
- Netzklasse `Power` (0,5 mm) für VIN*, BUCK_SW, V_PRE*, 5V_* und GND.

Anordnung (oben gesehen):
- WeAct-Board links auf Buchsenleisten 2 × 2×15, um 180° gedreht. USB-C liegt an der Oberkante, die Analog-Leiste (PA4, PC4, PC5, PB0, PB1) zeigt zur Mitte.
- K-LC1a **auf der Unterseite**, Antenne nach unten. Die Pins stehen rechts, die ZF-Kette liegt darüber auf der Oberseite.
- Schaltregler und Stecker J1 liegen unten, möglichst weit weg von der ZF-Kette.

## Neu erzeugen

Änderungen an Bauteilen oder Netzen in `generator/design.py` machen, dann:

```bash
python generator/mklib.py && python generator/gen_sch.py
"D:/Program Files/KiCad/9.0/bin/python.exe" generator/gen_pcb.py
"D:/Program Files/KiCad/9.0/bin/python.exe" generator/route.py
```

`gen_pcb.py` und `route.py` brauchen das Python von KiCad (Modul `pcbnew`), `route.py` zusätzlich
Java und Freerouting (Pfad über `FREEROUTING`, Standard `~/AppData/Local/freerouting/freerouting-1.9.0.jar`).
Der K-LC1a hat einen eigenen Unterseiten-Footprint statt eines gespiegelten Teils, weil der
DSN-Export von KiCad 9 gespiegelten Durchsteck-Pads die Innenlagen nimmt. **Achtung:** Neu erzeugen
überschreibt die Platine samt Routing. Sobald in KiCad geroutet wird, Änderungen nur noch in
KiCad machen (Schaltplan → Platine mit F8).

## Vor dem Routing/Bestellen prüfen

1. **WeAct-Pinfolge:** Die Pinfolge stammt aus dem Board-Shape-PDF V1.0 von WeAct. Pro Leiste gilt: linke Beschriftung = äußere Reihe (links) bzw. innere Reihe (rechts).
   - Genutzt werden PA2, PA3, PA4, PC4, PC5, PB0, PB1, PB8, PB9, PB12, PB13, PC6, PC7, 5V, 3V3 und GND.
   - Mit Durchgangsprüfer am echten Board (V1.1) nachmessen, vor allem 5V/GND unten rechts und 3V3/GND unten links. Die Reihen L12 bis L14 innen sind unbeschriftet und bleiben offen.
2. **K-LC1a:**
   - Laut Datenblatt Fig. 3 sind die Pins 2,54 mm, Pin 1 sitzt 7,42 mm von der Kante.
   - Der Abstand der Pinreihe zum Modulrand (1,45 mm) ist aus der Zeichnung geschätzt.
   - Bohrung 1,0 mm für 0,64-mm-Pins. Abstand Platine zu Modul so wählen, dass nichts auf der Unterseite das Gehäuse berührt.
3. **TS3A5017 TSSOP-16:** Pinbelegung nach TI SCDS188G (1 = 1EN, 2 = IN2, 7 = 1D, 14 = IN1, 15 = 2EN, 16 = V+). Die Kanalzuordnung (S1 = 55,5-fach … S4 = 1-fach) muss zur Tabelle der Firmware passen.
4. **Footprints der Fremdteile:**
   - J1 ist JST GH, Kabelaustritt prüfen.
   - L1/L2 als Bourns SRN4018 oder Gleichwertiges (L2: Sättigungsstrom ≥ 1 A).
   - C21 ist Folie im Format 1812, eine passende Bauform suchen.
5. **Layout-Regeln** (siehe Hinweise im SVG-Plan):
   - Massefläche nicht auftrennen.
   - IF_RAW → C21 → U1A kurz halten.
   - BUCK_SW-Schleife (U3, D4, L2, C2) klein halten und weit weg von der ZF.
   - ADC_IF mit R32/C27 nahe an PC4 führen.
