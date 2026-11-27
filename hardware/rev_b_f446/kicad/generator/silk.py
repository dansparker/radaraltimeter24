"""Bestueckungsdruck aufraeumen (nach route.py):
Referenzen auf freie Plaetze um das Bauteil setzen (oben, unten, links, rechts, auch 90 Grad gedreht),
frei von Pads, Vias, anderem Druck und der Platinenkante; notfalls kleiner, sonst nur auf F.Fab.
Druckstriche der Footprints, die fremde Pads oder Vias schneiden, werden entfernt.
"""
import os
import pcbnew as P

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(os.path.dirname(HERE), 'radaraltimeter_revb.kicad_pcb')
mm, tomm = P.FromMM, P.ToMM
b = P.LoadBoard(PCB)
CLR = 0.15          # Abstand Druck zu Lötstopp-Oeffnung / anderem Druck


def box(bb, m=0.0):
    return (tomm(bb.GetLeft()) - m, tomm(bb.GetTop()) - m, tomm(bb.GetRight()) + m, tomm(bb.GetBottom()) + m)


def gi(fp):
    try:
        return [x for x in fp.GraphicalItems()]
    except TypeError:
        return []


def hit(a, c):
    return not (a[2] <= c[0] or c[2] <= a[0] or a[3] <= c[1] or c[3] <= a[1])


edge = box(b.GetBoardEdgesBoundingBox())
inside = (edge[0] + 0.3, edge[1] + 0.3, edge[2] - 0.3, edge[3] - 0.3)
openings = {P.F_SilkS: [], P.B_SilkS: []}     # (box, footprint-ref)
for pad in b.GetPads():
    for L, cu in ((P.F_SilkS, P.F_Cu), (P.B_SilkS, P.B_Cu)):
        if pad.IsOnLayer(cu):
            openings[L].append((box(pad.GetBoundingBox(), CLR), pad.GetParentFootprint().GetReference()))
for t in b.GetTracks():
    if t.Type() == P.PCB_VIA_T:
        for L in openings:
            openings[L].append((box(t.GetBoundingBox(), CLR), None))

# 1) Druckstriche, die fremde Pads/Vias schneiden, entfernen
REFS = [fp.GetReference() for fp in b.GetFootprints()]
FP = lambda r: b.FindFootprintByReference(r)
drop = []
for r in REFS:
    fp = FP(r)
    for it in gi(fp):
        L = it.GetLayer()
        if L not in openings or it.Type() == P.PCB_TEXT_T:
            continue
        bb = box(it.GetBoundingBox())
        if any(hit(bb, o) for (o, rr) in openings[L] if rr != r):
            drop.append((r, it))
removed = len(drop)        # nur gemeldet: Footprints bleiben wie in der Bibliothek (KiCad schneidet beim Plot)

# 2) Referenzen setzen
graphics = {P.F_SilkS: [], P.B_SilkS: []}
for r in REFS:
    for it in gi(FP(r)):
        if it.GetLayer() in graphics and it.Type() != P.PCB_TEXT_T:
            bx = box(it.GetBoundingBox(), CLR)
            if it.GetShape() == P.SHAPE_T_RECT:      # nur die vier Kanten, das Innere ist frei
                t = 2 * CLR + 0.15
                graphics[it.GetLayer()] += [(bx[0], bx[1], bx[2], bx[1] + t), (bx[0], bx[3] - t, bx[2], bx[3]),
                                            (bx[0], bx[1], bx[0] + t, bx[3]), (bx[2] - t, bx[1], bx[2], bx[3])]
            else:
                graphics[it.GetLayer()].append(bx)
for d in b.GetDrawings():
    if d.GetLayer() in graphics:
        graphics[d.GetLayer()].append(box(d.GetBoundingBox(), CLR))

placed = {P.F_SilkS: [], P.B_SilkS: []}
moved, small, fab = 0, 0, []
fps = sorted([FP(r) for r in REFS], key=lambda f: -tomm(f.GetBoundingBox(False).GetWidth()) * tomm(f.GetBoundingBox(False).GetHeight()))
for fp in fps:
    ref = fp.Reference()
    if not ref.IsVisible():
        continue
    L = ref.GetLayer()
    if L not in placed:
        continue
    cb = box(fp.GetBoundingBox(False))
    cx, cy = (cb[0] + cb[2]) / 2, (cb[1] + cb[3]) / 2
    ok = False
    for size in (0.8,):
        ref.SetTextSize(P.VECTOR2I(mm(size), mm(size))); ref.SetTextThickness(mm(size * 0.15))
        cands = []
        for ang in (0, 90):
            ref.SetTextAngleDegrees(ang)
            tb = box(ref.GetBoundingBox())
            w, h = tb[2] - tb[0], tb[3] - tb[1]
            big = (cb[2] - cb[0]) > 4 and (cb[3] - cb[1]) > 3
            if big:
                cands.append((ang, cx, cy))       # auf dem Gehaeuse
            for g in (0.2, 0.5, 0.9):
              cands += [(ang, cx, cb[1] - g - h / 2), (ang, cx, cb[3] + g + h / 2),
                      (ang, cb[0] - g - w / 2, cy), (ang, cb[2] + g + w / 2, cy)]
              for s in (-1, 1):
                cands += [(ang, cx + s * w / 2, cb[1] - g - h / 2), (ang, cx + s * w / 2, cb[3] + g + h / 2)]
        cands.sort(key=lambda c: (abs(c[1] - cx) + abs(c[2] - cy), c[0]))
        for ang, x, y in cands:
            ref.SetTextAngleDegrees(ang); ref.SetPosition(P.VECTOR2I(mm(x), mm(y)))
            tb = box(ref.GetBoundingBox())
            if not (inside[0] <= tb[0] and inside[1] <= tb[1] and tb[2] <= inside[2] and tb[3] <= inside[3]):
                continue
            if any(hit(tb, o) for (o, r) in openings[L]) or any(hit(tb, o) for o in graphics[L]) \
                    or any(hit(tb, o) for o in placed[L]):
                continue
            ok = True; break
        if ok:
            break
    if ok:
        placed[L].append(box(ref.GetBoundingBox(), CLR)); moved += 1; small += size < 0.8
    else:
        ref.SetLayer(P.F_Fab if L == P.F_SilkS else P.B_Fab)
        ref.SetTextSize(P.VECTOR2I(mm(0.6), mm(0.6))); ref.SetTextThickness(mm(0.09))
        ref.SetPosition(P.VECTOR2I(mm(cx), mm(cy))); ref.SetTextAngleDegrees(0)
        fab.append(fp.GetReference())
P.SaveBoard(PCB, b)
print('Druckstriche ueber fremden Pads (bleiben): %d, Referenzen gesetzt %d (davon %d klein), nur auf Fab: %s' % (removed, moved, small, fab or 'keine'))
