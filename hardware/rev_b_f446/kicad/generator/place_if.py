"""Platzierung der ZF-Kette und der VCO-Ansteuerung.
Feste Anker von Hand (U1, C21), danach jedes Bauteil in Signalrichtung auf den freien Platz mit der
kleinsten gewichteten Netzausdehnung; Abblockkondensatoren an ihren Versorgungspin.
Koordinaten in mm ab Platinenecke oben links (oben gesehen).
"""
import math
import pcbnew as P

# Modul-Pins (M1, Unterseite): x 80, Pin 1..5 bei y 22.42 + 2.54*(n-1); Pin 3 = IF_RAW bei y 27.50
HAND = {
    'U1': (68.0, 28.0, 180),     # Pins 1-7 (Stufe 1 und 2) rechts zum Modul, 8-14 (Anti-Alias, VMID) links
    'C21': (75.4, 28.65, 180),   # Pad 1 (IF_RAW) zum Modulpin 3, Pad 2 (IF_AC) auf gleicher Hoehe wie U1 Pin 3
    'U7': (73.5, 21.6, 0),       # Pin 7 (1D = IF_FB2) direkt neben U1 Pin 6
}
ORDER = ['R20', 'C20', 'R21', 'R22', 'R23', 'C22', 'C36', 'C23', 'R24', 'R25', 'R29', 'C24', 'C38',
         'R26', 'R27', 'R28', 'R30', 'C25', 'R31', 'C26', 'R33', 'R34', 'C28', 'R35', 'C29', 'C30', 'R36', 'C31',
         'U2', 'C37', 'R37', 'C32', 'R38', 'C33', 'R32', 'C27',
         'R13', 'C12', 'R14', 'C13', 'R12', 'C11', 'C10', 'R10', 'R11',
         'C15', 'C14', 'FB2', 'U6', 'C8', 'C9']
ANCHOR = {'C36': ('U1', '4'), 'C37': ('U2', '4'), 'C38': ('U7', '16'), 'C15': ('M1', '2'), 'C14': ('M1', '2'),
          'C9': ('U6', '5'), 'C8': ('U6', '1'), 'R32': ('J2', '17'), 'C27': ('J2', '17')}
WEIGHT = {'GND': 0.0, '/A3V3': 0.2, '/VMID': 0.6, '/GAIN_A': 0.1, '/GAIN_B': 0.1, '/MON_VTUNE': 0.2,
          '/DAC_RAMP': 0.3, '/5V_D': 0.1, '/A3V3_IN': 0.5, '/AA2_OUT': 0.3, '/VCO_TUNE': 0.6}
REGION = (35.6, 5.6, 79.0, 50.6)
MARGIN = 0.35


def run(FPS, V, OX, OY):
    mm, tomm = P.FromMM, P.ToMM
    # Rotation -> Huellrechteck und Pad-Offsets je Bauteil (relativ zur Position)
    geo = {}

    def shape(ref, rot):
        k = (ref, rot)
        if k not in geo:
            fp = FPS[ref][0]
            fp.SetOrientationDegrees(rot); fp.SetPosition(V(0, 0))
            bb = fp.GetBoundingBox(False)
            box = (tomm(bb.GetLeft()) - OX, tomm(bb.GetTop()) - OY, tomm(bb.GetRight()) - OX, tomm(bb.GetBottom()) - OY)
            pads = [(pd.GetNumber(), pd.GetNetname(), tomm(pd.GetPosition().x) - OX, tomm(pd.GetPosition().y) - OY)
                    for pd in fp.Pads()]
            geo[k] = (box, pads)
        return geo[k]

    movable = set(HAND) | set(ORDER)
    # Hindernisse: alle anderen Teile (M1 nur mit Pads, Koerper liegt unten)
    obst = []
    pads_at = {}
    for ref, (fp, c) in FPS.items():
        if ref in movable:
            continue
        if ref == 'M1':
            for pd in fp.Pads():
                b = pd.GetBoundingBox()
                obst.append((tomm(b.GetLeft()) - OX, tomm(b.GetTop()) - OY, tomm(b.GetRight()) - OX, tomm(b.GetBottom()) - OY))
        else:
            b = fp.GetBoundingBox(False)
            obst.append((tomm(b.GetLeft()) - OX, tomm(b.GetTop()) - OY, tomm(b.GetRight()) - OX, tomm(b.GetBottom()) - OY))
        for pd in fp.Pads():
            pads_at[(ref, pd.GetNumber())] = (tomm(pd.GetPosition().x) - OX, tomm(pd.GetPosition().y) - OY, pd.GetNetname())
    nb = {}                                    # Netz -> [xmin, ymin, xmax, ymax] der platzierten Pads

    def addpads(ref, pads, x, y):
        for (num, n, dx, dy) in pads:
            pads_at[(ref, num)] = (x + dx, y + dy, n)
    for (x, y, n) in list(pads_at.values()):
        if n and not n.startswith('unconnected'):
            b = nb.setdefault(n, [x, y, x, y])
            b[0], b[1], b[2], b[3] = min(b[0], x), min(b[1], y), max(b[2], x), max(b[3], y)

    def commit(ref, x, y, rot):
        box, pads = shape(ref, rot)
        fp = FPS[ref][0]
        fp.SetOrientationDegrees(rot); fp.SetPosition(V(x, y))
        obst.append((box[0] + x, box[1] + y, box[2] + x, box[3] + y))
        addpads(ref, pads, x, y)
        for (num, n, dx, dy) in pads:
            if n and not n.startswith('unconnected'):
                px, py = x + dx, y + dy
                b = nb.setdefault(n, [px, py, px, py])
                b[0], b[1], b[2], b[3] = min(b[0], px), min(b[1], py), max(b[2], px), max(b[3], py)

    def free(bx):
        if bx[0] < REGION[0] or bx[1] < REGION[1] or bx[2] > REGION[2] or bx[3] > REGION[3]:
            return False
        for o in obst:
            if not (bx[2] + MARGIN <= o[0] or o[2] + MARGIN <= bx[0] or bx[3] + MARGIN <= o[1] or o[3] + MARGIN <= bx[1]):
                return False
        return True

    for ref, (x, y, r) in HAND.items():
        commit(ref, x, y, r)

    for ref in ORDER:
        # Ziel: Schwerpunkt der schon platzierten, verbundenen Pads
        _, pads0 = shape(ref, 0)
        pts = []
        for (num, n, dx, dy) in pads0:
            if n in nb and WEIGHT.get(n, 1.0) > 0:
                b = nb[n]; pts.append(((b[0] + b[2]) / 2, (b[1] + b[3]) / 2))
        anc = ANCHOR.get(ref)
        if anc:
            ax, ay, _ = pads_at[anc]; pts = [(ax, ay)]
        tx = sum(p[0] for p in pts) / len(pts) if pts else 60.0
        ty = sum(p[1] for p in pts) / len(pts) if pts else 28.0
        best = None
        for rot in (0, 90, 180, 270):
            box, pads = shape(ref, rot)
            for i in range(-36, 37):
                for j in range(-36, 37):
                    x, y = tx + i * 0.5, ty + j * 0.5
                    if math.hypot(i, j) > 36:
                        continue
                    bx = (box[0] + x, box[1] + y, box[2] + x, box[3] + y)
                    cost = 0.0
                    for (num, n, dx, dy) in pads:
                        if not n or n.startswith('unconnected'):
                            continue
                        w = WEIGHT.get(n, 1.0)
                        px, py = x + dx, y + dy
                        if anc and n != 'GND' and (anc[0], anc[1]) in pads_at and pads_at[anc][2] == n:
                            cost += 4.0 * math.hypot(px - pads_at[anc][0], py - pads_at[anc][1])
                        if w == 0 or n not in nb:
                            continue
                        b = nb[n]
                        cost += w * ((max(b[2], px) - min(b[0], px) + max(b[3], py) - min(b[1], py))
                                     - (b[2] - b[0] + b[3] - b[1]))
                    cost += 0.02 * math.hypot(x - tx, y - ty)
                    if best and cost >= best[0]:
                        continue
                    if free(bx):
                        best = (cost, x, y, rot)
        if not best:
            raise RuntimeError('kein Platz fuer ' + ref)
        commit(ref, round(best[1], 2), round(best[2], 2), best[3])
    print('ZF/VCO von Hand platziert:', len(HAND) + len(ORDER), 'Teile')
