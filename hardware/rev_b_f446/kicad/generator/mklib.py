"""Projektbibliothek radar_revb: Symbole (radar_revb.kicad_sym) und Footprints (radar_revb.pretty)

WeAct STM32F4 64-Pin Core Board V1.x: Masse und Pinfolge aus dem Board-Shape-PDF von WeAct
(github.com/WeActStudio/WeActStudio.STM32F4_64Pin_CoreBoard, Hardware/...Board Shape.pdf):
  2 Leisten je 2 x 15, Raster 2,54 mm, innere Reihen 25,40 mm auseinander, Board 33,22 x 41,60 mm,
  aeussere Reihen 1,37 mm vom Rand, oberste Reihe 1,37 mm von der Oberkante (USB-C unten).
  Pad-Nummern: links Reihe r: aussen 2r-1, innen 2r; rechts Reihe r: innen 30+2r-1, aussen 30+2r.
K-LC1a: Datenblatt RFbeam, Fig. 3: 25 x 25 mm, 5 Pins 2,54 mm am Rand, Pin 1 7,42 mm von der Kante,
  Pins zeigen nach hinten (Antenne vorn) -> Modul auf der Platinenunterseite, Antenne nach unten.
TS3A5017 TSSOP-16 (PW): TI SCDS188G, PESD3V3S2UT SOT-23: Nexperia (1, 2 = I/O, 3 = GND).
"""
import os
F = lambda v: ('%.3f' % v).rstrip('0').rstrip('.')
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)

# ----------------------------------------------------------------- WeAct-Pinfolge (oben gesehen)
LEFT = [('VBAT', 'GND'), ('PC14', 'PC13'), ('PC0', 'PC15'), ('PC2', 'PC1'), ('PA0', 'PC3'), ('PA2', 'PA1'),
        ('PA4', 'PA3'), ('PA6', 'PA5'), ('PC4', 'PA7'), ('PB0', 'PC5'), ('PB2', 'PB1'), ('L12a', 'L12i'),
        ('3V3', 'L13i'), ('3V3', 'L14i'), ('3V3', 'GND')]          # (aussen, innen); L12/L13i/L14i: nicht beschriftet
RIGHT = [('PB9', 'GND'), ('PB7', 'PB8'), ('PB5', 'PB6'), ('PB3', 'PB4'), ('PC12', 'PD2'), ('PC10', 'PC11'),
         ('PA12', 'PA15'), ('PA10', 'PA11'), ('PA8', 'PA9'), ('PC8', 'PC9'), ('PC6', 'PC7'), ('PB14', 'PB15'),
         ('PB12', 'PB13'), ('5V', '5V'), ('GND', 'GND')]            # (innen, aussen)
WEACT_PINS = {}
for r, (o, i) in enumerate(LEFT, 1):
    WEACT_PINS[2 * r - 1] = o; WEACT_PINS[2 * r] = i
for r, (i, o) in enumerate(RIGHT, 1):
    WEACT_PINS[30 + 2 * r - 1] = i; WEACT_PINS[30 + 2 * r] = o


def pin(num, name, typ, x, y, ang, length=2.54):
    return ('      (pin %s line (at %s %s %d) (length %s) (name "%s" (effects (font (size 1.016 1.016)))) '
            '(number "%s" (effects (font (size 1.016 1.016)))))' % (typ, F(x), F(y), ang, F(length), name, num))


def ic(name, fp, desc, left, right, w=7.62, ref='U'):
    n = max(len(left), len(right), 1)
    hw = w
    hh = round(((n + 1) * 2.54 / 2) / 2.54 + 0.49) * 2.54
    pins = []
    for i, e in enumerate(left):
        if e: pins.append(pin(e[0], e[1], e[2], -hw - 2.54, hh - 2.54 * (i + 1), 0))
    for i, e in enumerate(right):
        if e: pins.append(pin(e[0], e[1], e[2], hw + 2.54, hh - 2.54 * (i + 1), 180))
    return '''  (symbol "%s" (in_bom yes) (on_board yes)
    (property "Reference" "%s" (at %s %s 0) (effects (font (size 1.27 1.27)) (justify left)))
    (property "Value" "%s" (at %s %s 0) (effects (font (size 1.27 1.27)) (justify left)))
    (property "Footprint" "%s" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
    (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
    (property "Description" "%s" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
    (symbol "%s_0_1"
      (rectangle (start %s %s) (end %s %s) (stroke (width 0.254) (type default)) (fill (type background)))
    )
    (symbol "%s_1_1"
%s
    )
  )
''' % (name, ref, F(-hw), F(hh + 1.27), name, F(-hw), F(-hh - 1.27), fp, desc, name, F(-hw), F(hh), F(hw), F(-hh),
       name, '\n'.join(pins))


P = 'passive'
weact = ic('WeAct_STM32F4_64Pin', 'radar_revb:WeAct_STM32F4_64Pin_CoreBoard',
           'WeAct Studio STM32F4 64-Pin Core Board (STM32F446RET6) auf 2 x 2x15 Buchsenleisten',
           [(str(k), WEACT_PINS[k], P) for k in range(1, 31)], [(str(k), WEACT_PINS[k], P) for k in range(31, 61)],
           w=10.16, ref='J')
klc = ic('K-LC1a', 'radar_revb:RFbeam_K-LC1a', 'RFbeam K-LC1a 24-GHz-Transceiver mit VCO-Eingang, 5 Pins',
         [('2', 'VCC', P), ('4', 'GND', P), None, ('5', 'VCO', P)], [('3', 'IF', P), None, ('1', 'NC', 'no_connect')],
         ref='M')
ts = ic('TS3A5017PW', 'Package_SO:TSSOP-16_4.4x5mm_P0.65mm', 'TI TS3A5017 Dual-SP4T-Analogschalter, TSSOP-16 (PW)',
        [('7', '1D', P), ('6', '1S1', P), ('5', '1S2', P), ('4', '1S3', P), ('3', '1S4', P), None,
         ('14', 'IN1', 'input'), ('2', 'IN2', 'input'), ('1', '~{1EN}', 'input'), ('15', '~{2EN}', 'input')],
        [('16', 'V+', 'power_in'), ('8', 'GND', 'power_in'), None, ('9', '2D', P), ('10', '2S1', P), ('11', '2S2', P),
         ('12', '2S3', P), ('13', '2S4', P)])
esd = ic('PESD3V3S2UT', 'Package_TO_SOT_SMD:SOT-23', 'Nexperia PESD3V3S2UT ESD-Schutz 2 Leitungen, SOT-23',
         [('1', 'IO1', P), ('2', 'IO2', P)], [('3', 'GND', P)], w=5.08, ref='D')
open(os.path.join(OUT, 'radar_revb.kicad_sym'), 'w', encoding='utf-8', newline='\n').write(
    '(kicad_symbol_lib (version 20220914) (generator radar_gen)\n' + weact + klc + ts + esd + ')\n')

# ----------------------------------------------------------------- Footprints
PRETTY = os.path.join(OUT, 'radar_revb.pretty')
os.makedirs(PRETTY, exist_ok=True)


def rect(x0, y0, x1, y1, layer, w=0.1):
    return '  (fp_rect (start %s %s) (end %s %s) (stroke (width %s) (type solid)) (fill none) (layer "%s"))\n' % (
        F(x0), F(y0), F(x1), F(y1), F(w), layer)


def text(t, x, y, layer, size=1.0):
    return '  (fp_text user "%s" (at %s %s) (layer "%s") (effects (font (size %s %s) (thickness %s))))\n' % (
        t, F(x), F(y), layer, F(size), F(size), F(size * 0.15))


def tht(num, x, y, first=False, size=1.7, drill=1.0):
    return '  (pad "%s" thru_hole %s (at %s %s) (size %s %s) (drill %s) (layers "*.Cu" "*.Mask"))\n' % (
        num, 'rect' if first else 'circle', F(x), F(y), F(size), F(size), F(drill))


def footprint(name, descr, body, refpos, valpos):
    return ('(footprint "%s" (version 20240108) (generator "radar_gen") (layer "F.Cu")\n'
            '  (descr "%s")\n  (attr through_hole)\n'
            '  (property "Reference" "REF**" (at %s %s 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))\n'
            '  (property "Value" "%s" (at %s %s 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))\n'
            '%s)\n') % (name, descr, F(refpos[0]), F(refpos[1]), name, F(valpos[0]), F(valpos[1]), body)


W, H, M = 33.22, 41.60, 1.37
b = rect(0, 0, W, H, 'F.Fab') + rect(-0.25, -0.25, W + 0.25, H + 0.25, 'F.CrtYd', 0.05)
b += rect(-0.12, -0.12, W + 0.12, H + 0.12, 'F.SilkS', 0.12)
b += text('USB-C', W / 2, H - 2.5, 'F.SilkS') + text('WeAct STM32F446', W / 2, H / 2, 'F.Fab')
for r in range(1, 16):
    y = M + 2.54 * (r - 1)
    b += tht(2 * r - 1, M, y, r == 1) + tht(2 * r, M + 2.54, y)
    b += tht(30 + 2 * r - 1, W - M - 2.54, y) + tht(30 + 2 * r, W - M, y)
open(os.path.join(PRETTY, 'WeAct_STM32F4_64Pin_CoreBoard.kicad_mod'), 'w', newline='\n').write(footprint(
    'WeAct_STM32F4_64Pin_CoreBoard', 'WeAct STM32F4 64-Pin Core Board V1.x auf 2 x 2x15 Buchsenleisten 2,54 mm, oben gesehen, USB-C unten',
    b, (W / 2, -1.5), (W / 2, H / 2 + 2)))

X0, Y0 = -1.45, -7.42           # Modulkante relativ zu Pin 1
b = rect(X0, Y0, X0 + 25, Y0 + 25, 'F.Fab') + rect(X0 - 0.25, Y0 - 0.25, X0 + 25.25, Y0 + 25.25, 'F.CrtYd', 0.05)
b += rect(X0 - 0.12, Y0 - 0.12, X0 + 25.12, Y0 + 25.12, 'F.SilkS', 0.12)
b += text('K-LC1a Antenne vom Pin weg', X0 + 12.5, Y0 + 12.5, 'F.Fab', 0.8)
for i in range(5):
    b += tht(i + 1, 0, 2.54 * i, i == 0, 1.6, 1.0)
open(os.path.join(PRETTY, 'RFbeam_K-LC1a.kicad_mod'), 'w', newline='\n').write(footprint(
    'RFbeam_K-LC1a', 'RFbeam K-LC1a 25x25 mm, 5 Pins 2,54 mm; Pins nach hinten -> auf die Unterseite setzen (Antenne nach aussen)',
    b, (X0 + 12.5, Y0 - 1.5), (X0 + 12.5, Y0 + 14)))
print('Bibliothek ok')
