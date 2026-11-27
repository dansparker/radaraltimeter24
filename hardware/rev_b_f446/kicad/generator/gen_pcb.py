"""Platine Rev. B: Umriss, 4 Lagen, Footprints mit Netzen, Vorplatzierung nach Gruppen, GND-Flaechen.
Laeuft mit dem Python von KiCad 9:  "<KiCad>/bin/python.exe" generator/gen_pcb.py
Nicht geroutet (Ratsnest). Lage (oben gesehen, mm ab Ecke oben links):
  WeAct-Board links (USB-C an der Oberkante), Analog-Leiste zur Platinenmitte;
  K-LC1a auf der Unterseite rechts, Pins rechts aussen; ZF-Kette oben darueber;
  Versorgung und Stecker J1 unten, Schaltregler weit weg von der ZF.
"""
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pcbnew as P
from design import comps, POWER_NETS
from sexp import flat_sym, pins as sympins

KICAD = os.environ.get('KICAD_DIR', r'D:/Program Files/KiCad/9.0')
FPDIR = os.path.join(KICAD, 'share', 'kicad', 'footprints')
PRJ = os.path.dirname(HERE)
OX, OY = 100.0, 100.0
W, H = 86.0, 64.0
mm = P.FromMM


def V(x, y): return P.VECTOR2I(mm(OX + x), mm(OY + y))


board = P.NewBoard(os.path.join(PRJ, 'radaraltimeter_revb.kicad_pcb'))
board.SetCopperLayerCount(4)
ds = board.GetDesignSettings()
ds.SetBoardThickness(mm(1.6))


def seg(a, b, layer=P.Edge_Cuts, w=0.1):
    s = P.PCB_SHAPE(board); s.SetShape(P.SHAPE_T_SEGMENT)
    s.SetStart(V(*a)); s.SetEnd(V(*b)); s.SetLayer(layer); s.SetWidth(mm(w)); board.Add(s)


for a, b in [((0, 0), (W, 0)), ((W, 0), (W, H)), ((W, H), (0, H)), ((0, H), (0, 0))]:
    seg(a, b)

nets = {}


def net(name):
    if name not in nets:
        n = P.NETINFO_ITEM(board, name if name in POWER_NETS else '/' + name); board.Add(n); nets[name] = n
    return nets[name]


SYMU = json.load(open(os.path.join(HERE, 'symuuid.json')))
FPS = {}
for c in comps:
    if c.get('virtual') or not c['fp']:
        continue
    lib, name = c['fp'].split(':')
    path = os.path.join(PRJ, 'radar_revb.pretty') if lib == 'radar_revb' else os.path.join(FPDIR, lib + '.pretty')
    fp = P.FootprintLoad(path, name)
    assert fp is not None, c['fp']
    fp.SetReference(c['ref']); fp.SetValue(c['value']); fp.SetFPID(P.LIB_ID(lib, name))
    board.Add(fp)
    pname = {p['num']: p.get('name', '~') for p in sympins(flat_sym(c['lib'], c['sym']))}
    for pad in fp.Pads():
        num = pad.GetNumber()
        n = c['pins'].get(num)
        if n:
            pad.SetNet(net(n))
        elif num in c['pins']:    # offener Pin: Netzname wie in KiCad
            nm = pname.get(num, '~')
            nn = 'unconnected-(%s-%sPad%s)' % (c['ref'], '' if nm == '~' else nm + '-', num)
            ni = P.NETINFO_ITEM(board, nn); board.Add(ni); pad.SetNet(ni)
    if c.get('dnp'):
        fp.SetDNP(True)
    fp.Reference().SetTextSize(P.VECTOR2I(mm(0.8), mm(0.8))); fp.Reference().SetTextThickness(mm(0.12))
    fp.Value().SetVisible(False)
    if c['ref'][0] == 'H':
        fp.Reference().SetVisible(False)
    if c['ref'] in SYMU:
        fp.SetPath(P.KIID_PATH('/' + SYMU[c['ref']]))
    FPS[c['ref']] = (fp, c)


def place(ref, x, y, rot=0, bottom=False):
    fp = FPS[ref][0]
    fp.SetOrientationDegrees(rot); fp.SetPosition(V(x, y))
    if bottom:
        fp.Flip(fp.GetPosition(), P.FLIP_DIRECTION_LEFT_RIGHT)
        for pd in fp.Pads():                    # Flip laesst THT-Pads nur auf F/B.Cu
            if pd.GetAttribute() == P.PAD_ATTRIB_PTH:
                pd.SetLayerSet(P.PAD.PTHMask())


# ------------------------------------------------------------ feste Teile
place('J2', 34.6, 42.2, 180)          # Board-Ursprung = Ecke; 180 deg: USB-C oben, PA4/PC4... zur Mitte
place('M1', 80.0, 22.42, 0)   # Pin 1 oben, Pins rechts aussen, Koerper 56,5...81,5 x 15...40
place('J1', 14.0, 60.5, 0)
for ref, (x, y) in {'H1': (4.0, 60.0), 'H2': (82.0, 4.0), 'H3': (82.0, 60.0), 'H4': (39.0, 4.0)}.items():
    place(ref, x, y)

# ------------------------------------------------------------ Gruppen packen (Regalverfahren)
GROUPS = {  # Rechteck x0, y0, x1, y1, Richtung
    'IF': (51.0, 6.5, 78.5, 50.0, 'rtl'),
    'VCO': (36.0, 8.6, 50.5, 27.0, 'ltr'),
    'AUX': (36.0, 27.5, 50.5, 50.0, 'ltr'),
    'IO': (6.0, 43.0, 34.5, 50.8, 'ltr'),
    'PWR': (21.0, 51.0, 62.0, 63.5, 'ltr'),
    'RFPWR': (62.5, 51.0, 78.0, 63.5, 'ltr'),
}
SIZE = {}


def bbox(fp):
    b = fp.GetBoundingBox(False)
    return P.ToMM(b.GetWidth()), P.ToMM(b.GetHeight()), P.ToMM(b.GetX()) - P.ToMM(fp.GetPosition().x), \
        P.ToMM(b.GetY()) - P.ToMM(fp.GetPosition().y)


overflow = []
for g, (x0, y0, x1, y1, d) in GROUPS.items():
    refs = [c['ref'] for c in comps if c.get('grp') == g and c['ref'] in FPS]
    x, y, rowh = (x0 if d == 'ltr' else x1), y0, 0.0
    GAP = 1.0 if g in ('IF', 'VCO', 'AUX') else 0.6
    for ref in refs:
        fp = FPS[ref][0]
        fp.SetPosition(V(0, 0))
        w, h, ox, oy = bbox(fp)
        if (d == 'ltr' and x + w > x1) or (d == 'rtl' and x - w < x0):
            x = x0 if d == 'ltr' else x1; y += rowh + GAP; rowh = 0.0
        if y + h > y1:
            overflow.append(ref)
        px = x if d == 'ltr' else x - w
        fp.SetPosition(V(px - ox, y - oy))
        x = x + w + GAP if d == 'ltr' else x - w - GAP
        rowh = max(rowh, h)
        SIZE[ref] = (round(w, 1), round(h, 1))
print('ueberzaehlig:', overflow or 'keine')

# ------------------------------------------------------------ Platzierung verbessern
# Gleich grosse Zweipoler innerhalb einer Gruppe tauschen und um 180 deg drehen, solange die
# Summe der Netz-Ausdehnungen (HPWL, ohne GND) kleiner wird.
POS, ROT, PADS = {}, {}, {}
for ref, (fp, c) in FPS.items():
    p0 = fp.GetPosition(); POS[ref] = (P.ToMM(p0.x), P.ToMM(p0.y)); ROT[ref] = 0
    PADS[ref] = [(pd.GetNetname(), P.ToMM(pd.GetPosition().x - p0.x), P.ToMM(pd.GetPosition().y - p0.y))
                 for pd in fp.Pads() if pd.GetNetname() and pd.GetNetname() != 'GND'
                 and not pd.GetNetname().startswith('unconnected')]
NETS = {}
for ref, pl in PADS.items():
    for (n, dx, dy) in pl:
        NETS.setdefault(n, []).append((ref, dx, dy))


def hpwl(n):
    xs, ys = [], []
    for ref, dx, dy in NETS[n]:
        sg = -1 if ROT[ref] else 1
        xs.append(POS[ref][0] + sg * dx); ys.append(POS[ref][1] + sg * dy)
    return max(xs) - min(xs) + max(ys) - min(ys)


def cost(refs):
    ns = {n for r in refs for (n, _, _) in PADS[r]}
    return sum(hpwl(n) for n in ns), ns


total0 = sum(hpwl(n) for n in NETS)
for rnd in range(30):
    better = 0
    for g in GROUPS:
        mv = [c['ref'] for c in comps if c.get('grp') == g and c['ref'] in SIZE and len(FPS[c['ref']][0].Pads()) == 2]
        for i, a in enumerate(mv):
            for b in mv[i:]:
                if SIZE[a] != SIZE[b]:
                    continue
                c0, _ = cost({a, b})
                best = (c0 - 1e-6, None)
                pa, pb, ra, rb = POS[a], POS[b], ROT[a], ROT[b]
                for sw in ((False, True) if a != b else (False,)):
                    for na in (0, 1):
                        for nb in ((0, 1) if a != b else (na,)):
                            if sw:
                                POS[a], POS[b] = pb, pa
                            ROT[a], ROT[b] = na, nb
                            c1, _ = cost({a, b})
                            if c1 < best[0]:
                                best = (c1, (sw, na, nb))
                            POS[a], POS[b], ROT[a], ROT[b] = pa, pb, ra, rb
                if best[1]:
                    sw, na, nb = best[1]
                    if sw:
                        POS[a], POS[b] = pb, pa
                    ROT[a], ROT[b] = na, nb
                    better += 1
    if not better:
        break
print('HPWL %.0f -> %.0f mm (%d Runden)' % (total0, sum(hpwl(n) for n in NETS), rnd + 1))
for ref, (fp, c) in FPS.items():
    if ref in SIZE:
        fp.SetPosition(P.VECTOR2I(mm(POS[ref][0]), mm(POS[ref][1]))); fp.SetOrientationDegrees(180 if ROT[ref] else 0)

# ------------------------------------------------------------ GND-Flaechen In1 (durchgehend) und B.Cu
for layer in (P.In1_Cu, P.B_Cu):
    z = P.ZONE(board); z.SetLayer(layer); z.SetNet(net('GND'))
    o = z.Outline(); o.NewOutline()
    for (x, y) in [(0.3, 0.3), (W - 0.3, 0.3), (W - 0.3, H - 0.3), (0.3, H - 0.3)]:
        o.Append(mm(OX + x), mm(OY + y))
    o.Outline(0).SetClosed(True)
    z.SetMinThickness(mm(0.25)); z.SetLocalClearance(mm(0.3))
    z.SetPadConnection(P.ZONE_CONNECTION_THERMAL)
    board.Add(z)
P.ZONE_FILLER(board).Fill(board.Zones())


def text(t, x, y, size=1.2, layer=P.F_SilkS):
    s = P.PCB_TEXT(board); s.SetText(t); s.SetPosition(V(x, y)); s.SetLayer(layer)
    s.SetTextSize(P.VECTOR2I(mm(size), mm(size))); s.SetTextThickness(mm(size * 0.15)); s.SetMirrored(layer == P.B_SilkS); board.Add(s)


text('radaraltimeter24 Rev. B', 58, 3.0)
text('K-LC1a unten, Antenne nach unten', 68, 44.0, 0.9, P.B_SilkS)
P.SaveBoard(os.path.join(PRJ, 'radaraltimeter_revb.kicad_pcb'), board)
print('Footprints:', len(FPS), 'Netze:', len(nets))
