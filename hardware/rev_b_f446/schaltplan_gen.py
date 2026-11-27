"""Generates schaltplan.svg (hardware rev B). Run: python schaltplan_gen.py"""
import os
import sys

W, H = 1910, 1500
o = []


def a(s): o.append(s)
def esc(t): return t.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def txt(x, y, t, cls='t', anc='start'):
    a(f'<text class="{cls}" x="{x}" y="{y}" text-anchor="{anc}">{esc(t)}</text>')


def wire(*p):
    a('<polyline class="w" points="' + ' '.join(f'{x},{y}' for x, y in p) + '"/>')


def dot(x, y): a(f'<circle cx="{x}" cy="{y}" r="2.8" fill="#1a1a1a"/>')


def R(cx, cy, vert, ref, val, fuse=False):
    if vert:
        wire((cx, cy - 25), (cx, cy - 15)); wire((cx, cy + 15), (cx, cy + 25))
        a(f'<rect class="b" x="{cx-5}" y="{cy-15}" width="10" height="30"/>')
        if fuse: wire((cx, cy - 15), (cx, cy + 15))
        txt(cx + 9, cy - 2, ref, 'r'); txt(cx + 9, cy + 10, val, 'v')
    else:
        wire((cx - 25, cy), (cx - 15, cy)); wire((cx + 15, cy), (cx + 25, cy))
        a(f'<rect class="b" x="{cx-15}" y="{cy-5}" width="30" height="10"/>')
        if fuse: wire((cx - 15, cy), (cx + 15, cy))
        txt(cx, cy - 9, ref + ' ' + val, 'r', 'middle')


def C(cx, cy, vert, ref, val, pol=False, side=1):
    if vert:
        wire((cx, cy - 20), (cx, cy - 3)); wire((cx, cy + 3), (cx, cy + 20))
        a(f'<line class="p" x1="{cx-10}" y1="{cy-3}" x2="{cx+10}" y2="{cy-3}"/>')
        if pol:
            a(f'<rect x="{cx-10}" y="{cy+2}" width="20" height="3" fill="#1a1a1a"/>')
            txt(cx - 14 if side > 0 else cx + 14, cy - 6, '+', 'v', 'middle')
        else:
            a(f'<line class="p" x1="{cx-10}" y1="{cy+3}" x2="{cx+10}" y2="{cy+3}"/>')
        if side > 0: txt(cx + 13, cy - 2, ref, 'r'); txt(cx + 13, cy + 10, val, 'v')
        else: txt(cx - 13, cy - 2, ref, 'r', 'end'); txt(cx - 13, cy + 10, val, 'v', 'end')
    else:
        wire((cx - 20, cy), (cx - 3, cy)); wire((cx + 3, cy), (cx + 20, cy))
        a(f'<line class="p" x1="{cx-3}" y1="{cy-10}" x2="{cx-3}" y2="{cy+10}"/>')
        a(f'<line class="p" x1="{cx+3}" y1="{cy-10}" x2="{cx+3}" y2="{cy+10}"/>')
        txt(cx, cy - 14, ref + ' ' + val, 'r', 'middle')


def L(cx, cy, ref, val):
    wire((cx - 25, cy), (cx - 20, cy)); wire((cx + 20, cy), (cx + 25, cy))
    d = f'M{cx-20},{cy}' + ''.join(' a5,5 0 0 1 10,0' for _ in range(4))
    a(f'<path class="w" d="{d}"/>'); txt(cx, cy - 10, ref + ' ' + val, 'r', 'middle')


def D(cx, cy, vert, ref, val, side=1):
    if vert:  # cathode up
        wire((cx, cy + 25), (cx, cy + 8)); wire((cx, cy - 8), (cx, cy - 25))
        a(f'<polygon class="b" points="{cx-8},{cy+8} {cx+8},{cy+8} {cx},{cy-8}"/>')
        a(f'<line class="p" x1="{cx-8}" y1="{cy-8}" x2="{cx+8}" y2="{cy-8}"/>')
        if side > 0: txt(cx + 12, cy - 2, ref, 'r'); txt(cx + 12, cy + 10, val, 'v')
        else: txt(cx - 12, cy - 2, ref, 'r', 'end'); txt(cx - 12, cy + 10, val, 'v', 'end')
    else:     # anode left
        wire((cx - 25, cy), (cx - 8, cy)); wire((cx + 8, cy), (cx + 25, cy))
        a(f'<polygon class="b" points="{cx-8},{cy-8} {cx-8},{cy+8} {cx+8},{cy}"/>')
        a(f'<line class="p" x1="{cx+8}" y1="{cy-8}" x2="{cx+8}" y2="{cy+8}"/>')
        txt(cx, cy - 12, ref + ' ' + val, 'r', 'middle')


def G(x, y):
    wire((x, y), (x, y + 6))
    for i, w in enumerate((9, 6, 3)):
        a(f'<line class="p" x1="{x-w}" y1="{y+6+i*3}" x2="{x+w}" y2="{y+6+i*3}"/>')


def S(x, y, name):
    wire((x, y), (x, y - 10)); a(f'<line class="p" x1="{x-10}" y1="{y-10}" x2="{x+10}" y2="{y-10}"/>')
    txt(x, y - 14, name, 'n', 'middle')


def N(x, y, name, d='r'):
    w = 7 * len(name) + 14
    if d == 'r': pts = f'{x},{y} {x+6},{y-8} {x+w},{y-8} {x+w},{y+8} {x+6},{y+8}'; tx = x + 9; anc = 'start'
    else: pts = f'{x},{y} {x-6},{y-8} {x-w},{y-8} {x-w},{y+8} {x-6},{y+8}'; tx = x - 9; anc = 'end'
    a(f'<polygon class="nl" points="{pts}"/>'); txt(tx, y + 4, name, 'n', anc)


def OP(cx, cy, ref, part='TLV9064'):
    a(f'<polygon class="b" points="{cx-30},{cy-32} {cx-30},{cy+32} {cx+35},{cy}"/>')
    wire((cx - 45, cy - 15), (cx - 30, cy - 15)); wire((cx - 45, cy + 15), (cx - 30, cy + 15)); wire((cx + 35, cy), (cx + 50, cy))
    txt(cx - 25, cy - 11, '+', 't'); txt(cx - 25, cy + 19, '−', 't')
    txt(cx - 4, cy + 4, ref, 'r', 'middle')
    if part: txt(cx, cy + 46, part, 'v', 'middle')


def BOX(x, y, w, h, title, sub=''):
    a(f'<rect class="ic" x="{x}" y="{y}" width="{w}" height="{h}" rx="3"/>')
    if title: txt(x + w / 2, y + h / 2 - 2, title, 'r', 'middle')
    if sub: txt(x + w / 2, y + h / 2 + 11, sub, 'v', 'middle')


def PIN(x, y, name, side):
    if side == 'l': wire((x, y), (x + 15, y)); txt(x + 18, y + 4, name, 'pn')
    if side == 'r': wire((x, y), (x - 15, y)); txt(x - 18, y + 4, name, 'pn', 'end')
    if side == 't': wire((x, y), (x, y + 15)); txt(x, y + 27, name, 'pn', 'middle')
    if side == 'b': wire((x, y), (x, y - 15)); txt(x, y - 19, name, 'pn', 'middle')


def FRAME(x, y, w, h, title):
    a(f'<rect class="fr" x="{x}" y="{y}" width="{w}" height="{h}" rx="6"/>')
    txt(x + 10, y + 20, title, 'h')


def SK(x0, y, r1, r2, cf, cg, ref, part='TLV9064'):
    """unity gain Sallen-Key low-pass, input pin at (x0, y), returns the output node x"""
    R(x0 + 25, y, False, r1[0], r1[1]); wire((x0 + 50, y), (x0 + 80, y)); dot(x0 + 65, y)
    wire((x0 + 65, y), (x0 + 65, y - 60), (x0 + 140, y - 60)); C(x0 + 160, y - 60, False, cf[0], cf[1])
    wire((x0 + 180, y - 60), (x0 + 270, y - 60), (x0 + 270, y + 65))
    R(x0 + 105, y, False, r2[0], r2[1]); wire((x0 + 130, y), (x0 + 175, y)); dot(x0 + 150, y)
    wire((x0 + 150, y), (x0 + 150, y + 20)); C(x0 + 150, y + 40, True, cg[0], cg[1], side=-1); G(x0 + 150, y + 60)
    OP(x0 + 220, y + 15, ref, part)
    wire((x0 + 175, y + 30), (x0 + 170, y + 30), (x0 + 170, y + 65), (x0 + 270, y + 65)); dot(x0 + 270, y + 15)
    return x0 + 270


a(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" role="img">')
a('<title>Radar-Höhenmesser Rev. B – Schaltplan</title>')
a('<desc>STM32F446 WeAct Core Board, K-LC1a, TLV9064: Versorgung, VCO-Ansteuerung, ZF-Kette, CAN/UART, Überwachung.</desc>')
a('<style>.t{font:12px sans-serif;fill:#1a1a1a}.r{font:11px sans-serif;fill:#1a1a1a}.v{font:10px sans-serif;fill:#555}'
  '.n{font:600 10px sans-serif;fill:#0b4f9c}.pn{font:10px sans-serif;fill:#333}.h{font:600 14px sans-serif;fill:#3C3489}'
  '.note{font:11px sans-serif;fill:#333}.big{font:600 16px sans-serif;fill:#1a1a1a}'
  '.w{stroke:#1a1a1a;stroke-width:1.3;fill:none}.b{stroke:#1a1a1a;stroke-width:1.3;fill:#fff}.p{stroke:#1a1a1a;stroke-width:2}'
  '.nl{fill:#E6F0FB;stroke:#0b4f9c;stroke-width:0.8}.ic{fill:#FAF9F5;stroke:#1a1a1a;stroke-width:1.3}'
  '.fr{fill:none;stroke:#8a86c9;stroke-width:1;stroke-dasharray:6 4}</style>')
a(f'<rect width="{W}" height="{H}" fill="#fff"/>')

# ---------- 1 Versorgung
FRAME(20, 40, 1090, 385, '1  Versorgung (9–32 V Bordnetz)')
a('<rect class="ic" x="40" y="110" width="45" height="160" rx="3"/>'); txt(62, 104, 'J1', 'r', 'middle')
for i, y in enumerate([130, 155, 180, 205, 230, 255]):
    wire((85, y), (100, y)); txt(80, y + 4, str(i + 1), 'pn', 'end')
txt(62, 285, '6-pol.', 'v', 'middle')
wire((100, 155), (110, 155)); G(110, 155)
for nm, y in [('CANH', 180), ('CANL', 205), ('TX_EXT', 230), ('RX_EXT', 255)]:
    wire((100, y), (112, y)); N(112, y, nm)
wire((100, 130), (120, 130)); R(145, 130, False, 'F1', '0,5 A T', fuse=True)
wire((170, 130), (190, 130)); D(215, 130, False, 'D1', 'SS210')
wire((240, 130), (320, 130)); dot(260, 130); dot(300, 130)
wire((260, 130), (260, 145)); D(260, 170, True, 'D2', 'SMBJ33CA', side=-1); G(260, 195)
wire((300, 130), (300, 150)); C(300, 170, True, 'C1', '4,7µ/100V'); G(300, 190)
L(345, 130, 'L1', '10µH')
wire((370, 130), (460, 130)); dot(390, 130); dot(440, 130); txt(415, 122, 'VIN_F', 'n', 'middle')
wire((390, 130), (390, 150)); C(390, 170, True, 'C2', '4,7µ/100V'); G(390, 190)
BOX(460, 105, 120, 110, ''); txt(462, 99, 'U3 LMR16006X', 'r')
PIN(460, 130, 'VIN', 'l'); PIN(460, 160, 'EN', 'l'); PIN(520, 215, 'GND', 'b'); PIN(580, 150, 'SW', 'r'); PIN(580, 195, 'FB', 'r'); PIN(560, 105, 'CB', 't')
wire((460, 160), (440, 160), (440, 130)); G(520, 215)
wire((560, 105), (560, 88), (610, 88), (610, 98)); C(610, 118, True, 'C3', '100n'); wire((610, 138), (610, 150))
wire((580, 150), (675, 150)); dot(610, 150); dot(650, 150)
wire((650, 150), (650, 165)); D(650, 190, True, 'D4', 'B160'); G(650, 215)
wire((580, 195), (595, 195)); N(595, 195, 'FB')
L(700, 150, 'L2', '22µH')
wire((725, 150), (860, 150)); dot(760, 150); dot(790, 150); dot(835, 150); txt(760, 120, 'V_PRE 6,0 V', 'n', 'middle')
wire((760, 150), (760, 170)); C(760, 190, True, 'C4', '22µ', side=-1); G(760, 210)
wire((790, 150), (790, 160)); R(790, 185, True, 'R1', '68k'); wire((790, 210), (790, 235)); R(790, 260, True, 'R2', '10k'); G(790, 285)
dot(790, 222); wire((790, 222), (812, 222)); N(812, 222, 'FB')
txt(560, 262, 'Buck 0,7 MHz; U_out = 0,765 V·(1+R1/R2) ≈ 6,0 V', 'v')
BOX(870, 135, 90, 60, ''); txt(872, 129, 'U4 TLV76750 (5 V)', 'r')
PIN(870, 150, 'IN', 'l'); PIN(870, 175, 'EN', 'l'); PIN(915, 195, 'GND', 'b'); PIN(960, 150, 'OUT', 'r')
wire((860, 150), (870, 150)); wire((870, 175), (855, 175), (855, 150)); dot(855, 150); G(915, 195)
wire((960, 150), (1045, 150)); dot(1000, 150); wire((1000, 150), (1000, 165)); C(1000, 185, True, 'C5', '10µ'); G(1000, 205); N(1045, 150, '5V_D')
wire((835, 150), (835, 300), (845, 300)); L(870, 300, 'FB1', '600Ω')
wire((895, 300), (935, 300)); dot(905, 300); wire((905, 300), (905, 315)); C(905, 335, True, 'C6', '10µ', side=-1); G(905, 355)
BOX(935, 285, 90, 90, '', 'rauscharm'); txt(937, 279, 'U5 ADP7142-5.0', 'r')
PIN(935, 300, 'VIN', 'l'); PIN(960, 375, 'EN', 'b'); PIN(1005, 375, 'GND', 'b'); PIN(1025, 300, 'OUT', 'r')
G(1005, 375); wire((960, 375), (960, 392)); N(960, 392, 'RF_EN', 'l')
wire((1025, 300), (1050, 300)); dot(1040, 300); wire((1040, 300), (1040, 315)); C(1040, 335, True, 'C7', '10µ'); G(1040, 355); N(1050, 300, '5V_RF')
txt(930, 250, 'Modul-PSRR nur 26 dB →', 'v'); txt(930, 262, 'eigener rauscharmer Regler', 'v')
N(140, 330, '5V_D', 'l'); wire((140, 330), (150, 330)); L(175, 330, 'FB2', '600Ω')
wire((200, 330), (250, 330)); dot(220, 330); dot(240, 330); wire((220, 330), (220, 345)); C(220, 365, True, 'C8', '1µ', side=-1); G(220, 385)
BOX(250, 315, 80, 60, ''); txt(252, 309, 'U6 LP5907-3.3', 'r')
PIN(250, 330, 'IN', 'l'); PIN(250, 355, 'EN', 'l'); PIN(290, 375, 'GND', 'b'); PIN(330, 330, 'OUT', 'r')
wire((250, 355), (240, 355), (240, 330)); G(290, 375)
wire((330, 330), (420, 330)); dot(380, 330); wire((380, 330), (380, 345)); C(380, 365, True, 'C9', '1µ'); G(380, 385); N(420, 330, 'A3V3')
txt(440, 365, 'A3V3: Analogteil (U1, U2, U7, VMID)', 'v')

# ---------- 2 MCU
FRAME(1130, 40, 760, 385, '2  Mikrocontroller')
a('<rect class="ic" x="1380" y="70" width="220" height="340" rx="4"/>')
txt(1490, 88, 'WeAct STM32F446 Core Board', 'r', 'middle'); txt(1490, 101, 'STM32F446RET6, 168 MHz', 'v', 'middle')
left = [('5V', 120), ('GND', 150), ('3V3', 180), ('PA4  DAC1', 210), ('PC4  ADC1_IN14', 240), ('PC5  ADC1_IN15', 270), ('PB0  ADC1_IN8', 300), ('PB1  ADC1_IN9', 330)]
for nm, y in left: PIN(1365, y, nm, 'l')
right = [('PC6', 120), ('PC7', 150), ('PB9  CAN1_TX', 180), ('PB8  CAN1_RX', 210), ('PA2  USART2_TX', 240), ('PA3  USART2_RX', 270), ('PB12', 320)]
for nm, y in right: PIN(1615, y, nm, 'r')
for i, s in enumerate(['auf dem Board: USB-C (PA11/12), SWD,', 'HSE 8 MHz, LED PB2, Taster PC13,', 'µSD (PC8–12, PD2) → nicht belegen']):
    txt(1490, 358 + i * 13, s, 'v', 'middle')
N(1200, 120, '5V_D', 'l'); wire((1200, 120), (1215, 120)); D(1240, 120, False, 'D3', 'SS14'); wire((1265, 120), (1365, 120))
wire((1365, 150), (1340, 150)); G(1340, 150)
wire((1365, 180), (1340, 180)); N(1340, 180, '3V3', 'l'); txt(1240, 200, '(Ausgang Board-LDO)', 'v', 'middle')
for nm, y in [('DAC_RAMP', 210), ('ADC_IF', 240), ('MON_VTUNE', 270), ('MON_5VRF', 300), ('MON_VIN', 330)]:
    wire((1365, y), (1345, y)); N(1345, y, nm, 'l')
for nm, y in [('GAIN_A', 120), ('GAIN_B', 150), ('CAN_TX', 180), ('CAN_RX', 210), ('UART_TX', 240), ('UART_RX', 270)]:
    wire((1615, y), (1635, y)); N(1635, y, nm)
wire((1615, 320), (1700, 320)); dot(1660, 320); R(1660, 345, True, 'R3', '100k'); G(1660, 370); N(1700, 320, 'RF_EN')
txt(1700, 350, 'Reset → Modul aus', 'v')
txt(1150, 400, 'TIM2 300 kHz → DAC + ADC per DMA, Rampe 1,17 ms (352 Samples)', 'v')

# ---------- 3 VCO
FRAME(20, 440, 690, 355, '3  VCO-Ansteuerung (Rampe)')
N(70, 560, 'DAC_RAMP', 'l'); wire((70, 560), (80, 560)); R(105, 560, False, 'R10', '10k')
wire((130, 560), (170, 560)); dot(150, 560); wire((150, 560), (150, 575)); R(150, 600, True, 'R11', '15k'); G(150, 625)
R(195, 560, False, 'R12', '6,2k'); wire((220, 560), (275, 560)); dot(255, 560); dot(160, 560)
wire((160, 560), (160, 500), (310, 500)); C(330, 500, False, 'C10', '2,2n C0G'); wire((350, 500), (410, 500), (410, 575))
wire((255, 560), (255, 580)); C(255, 600, True, 'C11', '1n C0G', side=-1); G(255, 620)
OP(320, 575, 'U2A')
wire((275, 590), (270, 590), (270, 645), (395, 645), (395, 575)); dot(395, 575); dot(410, 575)
wire((370, 575), (430, 575)); R(455, 575, False, 'R13', '47')
wire((480, 575), (560, 575)); dot(500, 575); dot(530, 575); N(560, 575, 'VCO_TUNE')
wire((500, 575), (500, 595)); C(500, 615, True, 'C12', 'DNP', side=-1); G(500, 635)
wire((530, 575), (530, 590)); R(530, 615, True, 'R14', '10k'); wire((530, 640), (530, 665), (610, 665)); dot(570, 665)
wire((570, 665), (570, 680)); C(570, 700, True, 'C13', '1n'); G(570, 720); N(610, 665, 'MON_VTUNE')
txt(40, 690, 'U_tune = 0,6·U_DAC → 0,12…1,86 V', 'note'); txt(40, 705, '(DAC-Puffer an: 0,2…3,1 V)', 'note')
txt(40, 725, 'Sallen-Key fc ≈ 18 kHz, Q ≈ 0,74', 'note'); txt(40, 740, 'Rampe 1,17 ms: Ecken nach ~30 µs eingeschwungen', 'note')
txt(40, 760, 'K-LC1a: −80 MHz/V, 570 Ω gg. ≈0,9 V', 'note'); txt(40, 775, 'U2C/D unbenutzt: + an VMID, − an Ausg.', 'note')
txt(560, 597, '→ Modul Pin 5', 'v')

# ---------- 4 Modul
FRAME(720, 440, 400, 355, '4  Radarmodul')
a('<rect class="ic" x="880" y="520" width="140" height="170" rx="4"/>')
txt(950, 575, 'K-LC1a', 'big', 'middle'); txt(950, 592, 'RFbeam, 24 GHz', 'v', 'middle'); txt(950, 606, '15 dBm EIRP', 'v', 'middle')
txt(950, 620, '80° × 34°', 'v', 'middle'); txt(950, 650, 'Pin 1 n.c.', 'v', 'middle')
PIN(950, 500, 'VCC 2', 't'); PIN(865, 580, 'IF 3', 'l'); PIN(865, 640, 'VCO 5', 'l'); PIN(950, 705, 'GND 4', 'b')
wire((860, 480), (1085, 480)); wire((950, 480), (950, 500)); dot(950, 480); dot(1050, 480); N(860, 480, '5V_RF', 'l')
wire((1050, 480), (1050, 500)); C(1050, 520, True, 'C14', '10µ Ta', pol=True, side=-1); G(1050, 540)
wire((1085, 480), (1085, 500)); C(1085, 520, True, 'C15', '100n'); G(1085, 540)
G(950, 705)
wire((865, 580), (850, 580)); N(850, 580, 'IF_RAW', 'l')
wire((865, 640), (850, 640)); N(850, 640, 'VCO_TUNE', 'l')
txt(740, 745, 'Modul direkt auf die Platine (kurze ZF-Leitung).', 'note')
txt(740, 760, 'Patch-Spalten (34°) in Flugrichtung, 80° quer.', 'note')
txt(740, 775, 'ESD-Klasse 0 (< 250 V) – nur geerdet handhaben!', 'note')

# ---------- 5 Schnittstellen
FRAME(1130, 440, 760, 355, '5  Schnittstellen und Überwachung')
BOX(1290, 505, 160, 120, 'U8 TCAN1042V', '')
PIN(1275, 525, 'TXD', 'l'); PIN(1275, 555, 'RXD', 'l'); PIN(1275, 585, 'STB', 'l')
PIN(1465, 525, 'CANH', 'r'); PIN(1465, 585, 'CANL', 'r')
PIN(1335, 490, 'VCC', 't'); PIN(1385, 490, 'VIO', 't'); PIN(1360, 640, 'GND', 'b')
S(1335, 490, '5V_D'); S(1385, 490, '3V3'); G(1360, 640)
N(1260, 525, 'CAN_TX', 'l'); wire((1260, 525), (1275, 525)); N(1260, 555, 'CAN_RX', 'l'); wire((1260, 555), (1275, 555))
wire((1275, 585), (1268, 585), (1268, 600)); G(1268, 600)
wire((1465, 525), (1720, 525)); wire((1465, 585), (1470, 585), (1470, 665), (1720, 665))
dot(1495, 525); dot(1525, 665); wire((1495, 525), (1495, 585)); wire((1525, 665), (1525, 625))
a('<rect class="ic" x="1480" y="585" width="60" height="40" rx="3"/>'); txt(1510, 602, 'D5', 'r', 'middle'); txt(1510, 615, 'NUP2105L', 'v', 'middle')
wire((1540, 605), (1560, 605)); G(1560, 605)
dot(1610, 525); wire((1610, 525), (1610, 530))
a('<rect class="b" x="1604" y="530" width="12" height="8"/><rect class="b" x="1604" y="540" width="12" height="8"/>'); txt(1622, 543, 'JP1', 'r')
wire((1610, 548), (1610, 550)); R(1610, 575, True, 'R40', '60,4'); dot(1610, 600)
R(1610, 625, True, 'R41', '60,4'); wire((1610, 650), (1610, 665)); dot(1610, 665)
wire((1610, 600), (1655, 600)); C(1675, 600, False, 'C40', '4,7n'); wire((1695, 600), (1705, 600)); G(1705, 600)
N(1720, 525, 'CANH'); N(1720, 665, 'CANL')
txt(1640, 470, 'Split-Abschluss 120 Ω (nur am Busende)', 'v', 'middle')
N(1235, 680, 'UART_TX', 'l'); wire((1235, 680), (1250, 680)); R(1275, 680, False, 'R42', '100'); wire((1300, 680), (1380, 680)); N(1380, 680, 'TX_EXT')
N(1235, 720, 'UART_RX', 'l'); wire((1235, 720), (1250, 720)); R(1275, 720, False, 'R43', '100'); wire((1300, 720), (1380, 720)); N(1380, 720, 'RX_EXT')
dot(1330, 680); dot(1350, 720); wire((1330, 680), (1330, 745)); wire((1350, 720), (1350, 745))
a('<rect class="ic" x="1320" y="745" width="40" height="30" rx="3"/>'); txt(1340, 764, 'D6', 'r', 'middle'); wire((1360, 760), (1372, 760)); G(1372, 760)
txt(1150, 790, 'UART 3,3 V TTL, 115200 Bd ($RALT/$RDBG)', 'v')
N(1500, 690, 'VIN_F', 'l'); wire((1500, 690), (1515, 690)); R(1540, 690, False, 'R44', '150k'); wire((1565, 690), (1615, 690)); dot(1590, 690)
wire((1590, 690), (1590, 700)); R(1590, 725, True, 'R45', '10k'); G(1590, 750); N(1615, 690, 'MON_VIN')
N(1720, 725, '5V_RF', 'l'); wire((1720, 725), (1730, 725)); R(1755, 725, False, 'R46', '10k'); wire((1780, 725), (1810, 725)); dot(1795, 725)
wire((1795, 725), (1795, 735)); R(1795, 760, True, 'R47', '10k'); G(1795, 785); N(1810, 725, 'MON_5VRF')
txt(1440, 780, 'MON_*: je 100 nF an GND', 'v')

# ---------- 6 ZF-Kette
FRAME(20, 810, 1870, 545, '6  ZF-Kette')
N(95, 920, 'IF_RAW', 'l'); wire((95, 920), (170, 920)); dot(110, 920); dot(140, 920)
wire((110, 920), (110, 930)); R(110, 955, True, 'R20', '1k'); G(110, 980)
wire((140, 920), (140, 935)); C(140, 955, True, 'C20', '100p'); G(140, 975)
C(190, 920, False, 'C21', '1µ Folie'); wire((210, 920), (255, 920)); dot(230, 920)
wire((230, 920), (230, 900)); R(230, 875, True, 'R21', '4,7k'); S(230, 850, 'VMID1')
OP(300, 935, 'U1A', '')
wire((255, 950), (250, 950), (250, 1000)); dot(250, 1000); wire((250, 1000), (280, 1000)); R(305, 1000, False, 'R22', '4,7k'); wire((330, 1000), (360, 1000), (360, 935)); dot(360, 935)
R(250, 1025, True, 'R23', '470'); wire((250, 1050), (250, 1055)); C(250, 1075, True, 'C22', '22µ Ta', pol=True, side=-1); G(250, 1095)
wire((350, 935), (400, 935)); C(420, 935, False, 'C23', '1,5n C0G'); wire((440, 935), (495, 935)); dot(460, 935)
wire((460, 935), (460, 920)); R(460, 895, True, 'R24', '1k'); S(460, 870, 'VMID')
OP(540, 950, 'U1B', '')
wire((495, 965), (490, 965), (490, 1010)); dot(490, 1010); wire((440, 1010), (530, 1010)); dot(510, 1010)
wire((440, 1010), (440, 1020)); R(440, 1045, True, 'R29', '390'); wire((440, 1070), (440, 1080)); C(440, 1100, True, 'C24', '10µ Ta', pol=True, side=-1); G(440, 1120)
R(555, 1010, False, 'R25', '100k'); wire((580, 1010), (620, 1010), (620, 950))
wire((590, 950), (760, 950)); dot(620, 950); dot(730, 950)
a('<rect class="ic" x="560" y="1040" width="80" height="140" rx="3"/>'); txt(600, 1196, 'U7 TS3A5017 (½)', 'r', 'middle')
wire((510, 1010), (510, 1060), (560, 1060)); txt(565, 1064, 'COM', 'pn')
for i, (ref, val, g) in enumerate([('R26', '27k', '55,5'), ('R27', '5,6k', '14,6'), ('R28', '1,2k', '4,04'), (None, '0 Ω', '1')]):
    y = 1060 + i * 30; txt(635, y + 4, f'S{i+1}', 'pn', 'end')
    if ref: wire((640, y), (655, y)); R(680, y, False, ref, val); wire((705, y), (730, y))
    else: wire((640, y), (730, y)); txt(685, y - 5, '0 Ω', 'v', 'middle')
    txt(737, y + 4, f'V2 = {g}', 'v')
wire((730, 950), (730, 1150))
for y in (1060, 1090, 1120): dot(730, y)
N(548, 1112, 'GAIN_A', 'l'); wire((548, 1112), (560, 1112)); txt(565, 1116, 'IN1', 'pn')
N(548, 1140, 'GAIN_B', 'l'); wire((548, 1140), (560, 1140)); txt(565, 1144, 'IN2', 'pn')
wire((560, 1166), (530, 1166), (530, 1172)); G(530, 1172); txt(565, 1170, 'EN', 'pn')
# anti-alias: 4th order Butterworth 120 kHz = two Sallen-Key stages
xo1 = SK(760, 950, ('R30', '1,5k'), ('R31', '1,5k'), ('C25', '1n C0G'), ('C26', '820p C0G'), 'U1C', '')
xo2 = SK(xo1, 950, ('R37', '1,5k'), ('R38', '1,5k'), ('C32', '2,2n C0G'), ('C33', '330p C0G'), 'U2B', '')
wire((xo2, 965), (xo2 + 30, 965)); R(xo2 + 55, 965, False, 'R32', '470'); wire((xo2 + 80, 965), (xo2 + 140, 965)); dot(xo2 + 105, 965)
wire((xo2 + 105, 965), (xo2 + 105, 980)); C(xo2 + 105, 1000, True, 'C27', '2,2n C0G'); G(xo2 + 105, 1020); N(xo2 + 140, 965, 'ADC_IF')
# VMID (moved down)
dy = 255
S(1270, 880 + dy, 'A3V3'); R(1270, 905 + dy, True, 'R33', '10k'); wire((1270, 930 + dy), (1270, 950 + dy)); dot(1270, 940 + dy); R(1270, 975 + dy, True, 'R34', '10k'); G(1270, 1000 + dy)
wire((1270, 940 + dy), (1335, 940 + dy)); dot(1312, 940 + dy); wire((1312, 940 + dy), (1312, 955 + dy)); C(1312, 975 + dy, True, 'C28', '10µ Ta', pol=True); G(1312, 995 + dy)
OP(1380, 955 + dy, 'U1D', '')
wire((1335, 970 + dy), (1330, 970 + dy), (1330, 1030 + dy), (1430, 1030 + dy), (1430, 955 + dy)); dot(1430, 955 + dy)
R(1465, 955 + dy, False, 'R35', '47'); wire((1490, 955 + dy), (1600, 955 + dy)); dot(1510, 955 + dy); dot(1545, 955 + dy); dot(1570, 955 + dy)
wire((1510, 955 + dy), (1510, 970 + dy)); C(1510, 990 + dy, True, 'C29', '10µ Ta', pol=True, side=-1); G(1510, 1010 + dy)
wire((1545, 955 + dy), (1545, 970 + dy)); C(1545, 990 + dy, True, 'C30', '100n'); G(1545, 1010 + dy)
S(1570, 955 + dy, 'VMID')
R(1625, 955 + dy, False, 'R36', '1k'); wire((1650, 955 + dy), (1720, 955 + dy)); dot(1680, 955 + dy)
wire((1680, 955 + dy), (1680, 970 + dy)); C(1680, 990 + dy, True, 'C31', '10µ Ta', pol=True); G(1680, 1010 + dy); N(1720, 955 + dy, 'VMID1')
notes = [(40, 1135, 'Stufe 1: V1 = 1 + R22/R23 = 11 (21 dB)'), (40, 1150, 'Eingangs-HP 34 Hz; R20 = Modul-Last 1 kΩ'),
         (40, 1165, 'C21 Folie, C22/C24 Tantal (nicht mikrofonisch)'),
         (800, 1060, 'V2 = 1 + (R25 ∥ R2x)/R29, Stufen ≤ 4,04'), (800, 1075, 'Gesamt 11 … 610 (21 … 56 dB)'),
         (800, 1100, 'C23/R24: Hochpass 106 kHz = R-Kompensation'), (800, 1115, 'Bodenecho ~1/R, Schwebung ~R → flacher Pegel;'),
         (800, 1130, 'Eigenmischung (426-Hz-Rampe) −48 dB'),
         (1060, 1060, 'Anti-Alias: Butterworth 4. Ordnung, 120 kHz'), (1060, 1075, 'U1C: Q 0,55 / U2B: Q 1,29 (Sallen-Key)'),
         (1060, 1090, 'R32/C27: Ladungspuffer ADC'), (1060, 1105, 'Schwebung 513 Hz/m: 200 m ≈ 103 kHz, fs = 300 kHz'),
         (40, 1250, 'VMID = A3V3/2 gepuffert, VMID1 nachgefiltert (nur Stufe 1)'),
         (40, 1265, 'U1, U2 = TLV9064; U7: V+ = A3V3, je 100 nF'), (40, 1280, 'Ruhepegel ADC_IF 1,65 V ≙ 2048 LSB')]
for x, y, s in notes: txt(x, y, s, 'note')

# ---------- Hinweise + Schriftfeld
a('<rect class="fr" x="20" y="1370" width="1290" height="115" rx="6"/>')
txt(30, 1390, 'Hinweise', 'h')
hs = ['1  Layout: Massefläche durchgehend; ZF-Teil (U1, U2, U7) weit weg von U3/L2; Schaltfrequenz U3 kein Vielfaches von 300 kHz.',
      '2  Kondensatoren im Signalpfad nur C0G, Folie oder Tantal (X7R/X5R sind piezoelektrisch → Vibration wird zu Störsignal).',
      '3  U_tune hart auf 0,12–1,86 V begrenzt; Rampe per Kalibrierung im ISM-Band 24,05–24,25 GHz halten (Mittenfrequenz ±75 MHz).',
      '4  Core-Board über D3 versorgt; vor gleichzeitigem USB-Betrieb prüfen, ob das Board eine VBUS-Diode hat (sonst Rückspeisung in den PC).',
      '5  Firmware: make -C firmware (BOARD=revb): CAN1 PB8/PB9, USART2 PA2/PA3, Gain PC6/PC7, RF_EN PB12, Config Sektor 7.']
for i, s in enumerate(hs): txt(30, 1410 + i * 16, s, 'note')
a('<rect class="ic" x="1330" y="1370" width="560" height="115" rx="6"/>')
txt(1345, 1395, 'radaraltimeter24 – Hardware Rev. B', 'big')
txt(1345, 1416, 'FMCW-Radarhöhenmesser 24 GHz · STM32F446 (WeAct) · K-LC1a · TLV9064', 'note')
txt(1345, 1436, 'Blatt 1/1 · Stand 2026-11-27 · Entwurf, nicht aufgebaut/geprüft', 'note')
txt(1345, 1456, 'Lizenz MIT · github.com/dansparker/radaraltimeter24', 'note')
a('</svg>')
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'schaltplan.svg')
open(sys.argv[1] if len(sys.argv) > 1 else out, 'w', encoding='utf-8', newline='\n').write('\n'.join(o))
