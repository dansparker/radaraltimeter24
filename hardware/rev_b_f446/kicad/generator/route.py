"""Routen der vorplatzierten Platine mit Freerouting.
  1. In1 als Masselage (Plane), GND-Pads der SMD-Bauteile per Via auf In1
  2. Export Specctra DSN -> Freerouting (Autorouter) -> Import SES
  3. GND-Flaechen auf In1 und B.Cu fuellen, speichern
Laeuft mit dem Python von KiCad 9; Freerouting-JAR ueber FREEROUTING (Standard: lokale 1.9.0).
"""
import os, sys, subprocess
import pcbnew as P

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
PCB = os.path.join(PRJ, 'radaraltimeter_revb.kicad_pcb')
JAR = os.environ.get('FREEROUTING', os.path.expanduser('~/AppData/Local/freerouting/freerouting-1.9.0.jar'))
PASSES = int(os.environ.get('PASSES', '40'))
TMP = os.path.join(HERE, 'build'); os.makedirs(TMP, exist_ok=True)
mm, tomm = P.FromMM, P.ToMM

b = P.LoadBoard(PCB)
b.SetLayerType(P.In1_Cu, P.LT_POWER)
gnd = b.FindNet('GND')

# B.Cu-Flaeche fuer das Routen entfernen (Freerouting kennt sie nicht), danach neu anlegen
bzones = [z for z in b.Zones() if z.GetLayer() == P.B_Cu]
outline = [[(p.x, p.y) for p in [z.Outline().CVertex(i) for i in range(z.Outline().TotalVertices())]] for z in bzones]
for z in bzones:
    b.Remove(z)

# ---------------------------------------------------------------- GND-Vias
VIA_D, VIA_DR, CLR = 0.6, 0.3, 0.2
obst = []          # (x0, y0, x1, y1, net)
for pad in b.GetPads():
    bb = pad.GetBoundingBox()
    obst.append((tomm(bb.GetLeft()), tomm(bb.GetTop()), tomm(bb.GetRight()), tomm(bb.GetBottom()), pad.GetNetname()))
edge = b.GetBoardEdgesBoundingBox()
EX0, EY0, EX1, EY1 = tomm(edge.GetLeft()), tomm(edge.GetTop()), tomm(edge.GetRight()), tomm(edge.GetBottom())


def free(x, y, r):
    if not (EX0 + r + 0.5 < x < EX1 - r - 0.5 and EY0 + r + 0.5 < y < EY1 - r - 0.5):
        return False
    for (x0, y0, x1, y1, n) in obst:
        if n == 'GND':
            continue
        dx = max(x0 - x, 0, x - x1); dy = max(y0 - y, 0, y - y1)
        if dx * dx + dy * dy < (r + CLR) ** 2:
            return False
    return True


def add_via(x, y):
    v = P.PCB_VIA(b); v.SetPosition(P.VECTOR2I(mm(x), mm(y))); v.SetWidth(mm(VIA_D)); v.SetDrill(mm(VIA_DR))
    v.SetNet(gnd); b.Add(v)
    obst.append((x - VIA_D / 2, y - VIA_D / 2, x + VIA_D / 2, y + VIA_D / 2, 'GND'))


def add_track(a, c, w=0.4):
    t = P.PCB_TRACK(b); t.SetStart(P.VECTOR2I(mm(a[0]), mm(a[1]))); t.SetEnd(P.VECTOR2I(mm(c[0]), mm(c[1])))
    t.SetWidth(mm(w)); t.SetLayer(P.F_Cu); t.SetNet(gnd); b.Add(t)


nvia, miss = 0, []
for fp in b.GetFootprints():
    for pad in fp.Pads():
        if pad.GetNetname() != 'GND' or pad.GetAttribute() != P.PAD_ATTRIB_SMD:
            continue
        px, py = tomm(pad.GetPosition().x), tomm(pad.GetPosition().y)
        sx, sy = tomm(pad.GetBoundingBox().GetWidth()) / 2, tomm(pad.GetBoundingBox().GetHeight()) / 2
        if sx > 0.9 and sy > 0.9:                        # grosses Pad (EP): Via ins Pad
            add_via(px, py); nvia += 1; continue
        cx, cy = tomm(fp.GetPosition().x), tomm(fp.GetPosition().y)
        dirs = sorted([(1, 0), (-1, 0), (0, 1), (0, -1)], key=lambda d: -(d[0] * (px - cx) + d[1] * (py - cy)))
        done = False
        for k in (0.0, 0.3, 0.7):
            for d in dirs:
                ext = (sx if d[0] else sy) + VIA_D / 2 + 0.15 + k
                x, y = px + d[0] * ext, py + d[1] * ext
                if free(x, y, VIA_D / 2):
                    add_via(x, y); add_track((px, py), (x, y), 0.4 if (sx if d[1] else sy) >= 0.2 else 0.25)
                    nvia += 1; done = True; break
            if done:
                break
        if not done:
            miss.append('%s.%s' % (fp.GetReference(), pad.GetNumber()))
print('GND-Vias:', nvia, ' ohne Platz (Router verbindet):', miss or 'keine')

# 5V-Pins des WeAct-Boards (Reihe 14 innen/aussen) direkt verbinden
j2 = b.FindFootprintByReference('J2')
p57, p58 = [pd for pd in j2.Pads() if pd.GetNumber() in ('57', '58')]
t = P.PCB_TRACK(b); t.SetStart(p57.GetPosition()); t.SetEnd(p58.GetPosition()); t.SetWidth(mm(0.5))
t.SetLayer(P.F_Cu); t.SetNet(p57.GetNet()); b.Add(t)

# ---------------------------------------------------------------- Freerouting
dsn, ses = os.path.join(TMP, 'board.dsn'), os.path.join(TMP, 'board.ses')
assert P.ExportSpecctraDSN(b, dsn)
r = subprocess.run(['java', '-jar', JAR, '-de', dsn, '-do', ses, '-mp', str(PASSES), '-l', 'en'],
                   capture_output=True, text=True, timeout=3600)
print('\n'.join(l for l in r.stdout.splitlines()[-6:]))
assert os.path.exists(ses), r.stderr[-2000:]
assert P.ImportSpecctraSES(b, ses)

# ---------------------------------------------------------------- Flaechen, speichern
for pts in outline:
    z = P.ZONE(b); z.SetLayer(P.B_Cu); z.SetNet(gnd)
    o = z.Outline(); o.NewOutline()
    for (x, y) in pts:
        o.Append(x, y)
    o.Outline(0).SetClosed(True)
    z.SetMinThickness(mm(0.25)); z.SetLocalClearance(mm(0.3)); z.SetPadConnection(P.ZONE_CONNECTION_THERMAL)
    b.Add(z)
P.ZONE_FILLER(b).Fill(b.Zones())
P.SaveBoard(PCB, b)
print('gespeichert:', PCB)
