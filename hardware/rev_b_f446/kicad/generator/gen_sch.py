import uuid, math, sys, collections
sys.path.insert(0, '.')
from sexp import *
from design import comps, POWER_NETS
POWER_LIB = {}
import os
HERE = os.path.dirname(os.path.abspath(__file__))

def U(): return str(uuid.uuid4())
ROOT = U()
PROJECT = 'radaraltimeter_revb'
TWO_PIN = {'R', 'C', 'C_Polarized', 'L', 'FerriteBead_Small', 'Fuse', 'D_TVS', 'SS210', 'SS14', 'B160-E3'}

def snap(v, g=2.54): return round(v / g) * g

lib_cache = {}
def get_sym(lib, sym):
    k = lib + ':' + sym
    if k not in lib_cache:
        s = flat_sym(lib, sym)
        lib_cache[k] = s
    return lib_cache[k]

def rotvec(x, y, a):
    # x,y in display coords (y down); a = CCW visual degrees
    r = math.radians(a)
    return (x * math.cos(r) + y * math.sin(r), -x * math.sin(r) + y * math.cos(r))

def sym_bbox(s, unit):
    xs, ys = [], []
    for sub in s:
        if isinstance(sub, list) and sub[0] == 'symbol':
            nm = val(sub[1]); u = int(nm.split('_')[-2])
            if u not in (0, unit): continue
            for e in sub:
                if isinstance(e, list) and e[0] == 'rectangle':
                    for f in e:
                        if isinstance(f, list) and f[0] in ('start', 'end'):
                            xs.append(float(f[1])); ys.append(-float(f[2]))
                if isinstance(e, list) and e[0] == 'pin':
                    for f in e:
                        if isinstance(f, list) and f[0] == 'at':
                            xs.append(float(f[1])); ys.append(-float(f[2]))
    if not xs: xs, ys = [-2.54, 2.54], [-2.54, 2.54]
    return min(xs), min(ys), max(xs), max(ys)

def label_len(name): return 1.0 * len(name) + 4

# --------------------------------------------------------------- Instanzen bilden (je Unit)
items = []  # dict(comp, unit, rot, pins:[(pin, net, dx, dy, dir)], bbox)
for c in comps:
    s = get_sym(c['lib'], c['sym'])
    ps = pins(s)
    allnums = {p['num'] for p in ps}
    missing = allnums - set(c['pins'])
    extra = set(c['pins']) - allnums
    assert not missing and not extra, (c['ref'], missing, extra)
    units = sorted({p['unit'] for p in ps if p['unit'] > 0}) or [1]
    rot = 90 if c['sym'] in TWO_PIN else 0
    for u in units:
        plist = []
        for p in ps:
            if p['unit'] not in (0, u): continue
            if p['unit'] == 0 and u != units[0]: continue
            dx, dy = rotvec(p['x'], -p['y'], rot)
            # Pin zeigt (lib-Winkel) vom Anschlusspunkt in Richtung Koerper; Label nach aussen
            d = (p['rot'] + 180 + rot) % 360
            plist.append((p['num'], c['pins'][p['num']], dx, dy, d))
        x0, y0, x1, y1 = sym_bbox(s, u)
        cs = [rotvec(x, y, rot) for x, y in [(x0, y0), (x1, y1), (x0, y1), (x1, y0)]]
        bx0 = min(p[0] for p in cs); bx1 = max(p[0] for p in cs)
        by0 = min(p[1] for p in cs); by1 = max(p[1] for p in cs)
        lw = max([label_len(n or '') for (_, n, dx, dy, d) in plist if d == 180] + [3])
        rw = max([label_len(n or '') for (_, n, dx, dy, d) in plist if d == 0] + [3])
        tw = max([label_len(n or '') for (_, n, dx, dy, d) in plist if d == 90] + [5])
        bw = max([label_len(n or '') for (_, n, dx, dy, d) in plist if d == 270] + [5])
        items.append(dict(c=c, unit=u, rot=rot, pins=plist, bbox=(bx0, by0, bx1, by1),
                          W=(bx1 - bx0) + lw + rw + 6, H=(by1 - by0) + tw + bw + 8, lw=lw, tw=tw))

# --------------------------------------------------------------- Blockweise anordnen
BLOCKS = [('PWR', '1 Versorgung 9-32 V: Schutz, 6-V-Buck, LDOs (5 V digital, 5 V Radar rauscharm, 3,3 V analog)'),
          ('MCU', '2 WeAct STM32F446 Core Board (Buchsenleisten), Ueberwachung'),
          ('VCO', '3 VCO-Ansteuerung: Teiler + Sallen-Key 18 kHz, U2A'),
          ('RF', '4 Radarmodul K-LC1a (Unterseite, Antenne nach unten)'),
          ('IO', '5 CAN (TCAN1042V), UART, Spannungsmessung'),
          ('IF', '6 ZF-Kette: V 11 x 1...55,5, Hochpass 106 kHz, Anti-Alias 120 kHz 4. Ordnung, VMID'),
          ('MECH', 'Montage, PWR_FLAG')]
COLS = [['PWR', 'VCO', 'RF'], ['MCU', 'IO', 'MECH'], ['IF']]
COLW = 260
placed = []
blockrects = []
for ci, col in enumerate(COLS):
    y = 25
    for b in col:
        bx = 15 + ci * (COLW + 10)
        x = bx + 3; rowh = 0; ystart = y
        y += 10
        for it in [i for i in items if i['c']['block'] == b]:
            if x + it['W'] > bx + COLW:
                x = bx + 3; y += rowh; rowh = 0
            it['X'] = snap(x + it['lw'] + 3 - it['bbox'][0])
            it['Y'] = snap(y + it['tw'] + 4 - it['bbox'][1])
            x += it['W']; rowh = max(rowh, it['H'])
            placed.append(it)
        y += rowh + 5
        blockrects.append((b, bx, ystart, bx + COLW, y))
        y += 8
sheet_h = max(r[4] for r in blockrects) + 20
paper = 'A1' if sheet_h < 580 else 'A0'
print('Blatthoehe', sheet_h, '->', paper)

# --------------------------------------------------------------- S-Expr schreiben
out = []
def F(x): return ('%.3f' % x).rstrip('0').rstrip('.')
pwr_n = 0
SYMUUID = {}
used_libs = {}
for it in placed:
    c = it['c']; used_libs[c['lib'] + ':' + c['sym']] = (c['lib'], c['sym'])
for n in POWER_NETS: used_libs[POWER_LIB.get(n, 'power') + ':' + n] = (POWER_LIB.get(n, 'power'), n)

def power_symbol(net, x, y, d):
    global pwr_n
    pwr_n += 1
    natural = 270 if net.startswith('GND') else 90
    r = (d - natural) % 360
    ref = '#PWR%03d' % pwr_n
    plib = POWER_LIB.get(net, 'power')
    s = get_sym(plib, net)
    pnums = [p['num'] for p in pins(s)]
    vx, vy = rotvec(0, -3.2 if natural == 90 else 3.2, r)
    o = ['(symbol (lib_id "%s:%s") (at %s %s %d) (unit 1) (in_bom yes) (on_board yes) (dnp no) (uuid %s)' % (plib, net, F(x), F(y), r, U()),
         '  (property "Reference" "%s" (at %s %s 0) (effects (font (size 1.27 1.27)) hide))' % (ref, F(x), F(y)),
         '  (property "Value" "%s" (at %s %s 0) (effects (font (size 1.27 1.27))))' % (net, F(x + vx * 1.6), F(y + vy * 1.6)),
         '  (property "Footprint" "" (at %s %s 0) (effects (font (size 1.27 1.27)) hide))' % (F(x), F(y))]
    for pn in pnums: o.append('  (pin "%s" (uuid %s))' % (pn, U()))
    o.append('  (instances (project "%s" (path "/%s" (reference "%s") (unit 1)))))' % (PROJECT, ROOT, ref))
    return '\n'.join(o)

for it in placed:
    c = it['c']; X, Y = it['X'], it['Y']
    x0, y0, x1, y1 = it['bbox']
    vis_ref = not c.get('virtual')
    su = U()
    SYMUUID.setdefault(c['ref'], su)
    o = ['(symbol (lib_id "%s:%s") (at %s %s %d) (unit %d) (in_bom %s) (on_board %s) (dnp %s) (uuid %s)' % (
        c['lib'], c['sym'], F(X), F(Y), it['rot'], it['unit'], 'no' if c.get('virtual') or c['ref'][:2] in ('H1', 'H2', 'H3', 'H4', 'JP', 'TP') else 'yes',
        'no' if c.get('virtual') else 'yes', 'yes' if c.get('dnp') else 'no', su)]
    fa = it['rot']
    o.append('  (property "Reference" "%s" (at %s %s %d) (effects (font (size 1.27 1.27)) (justify left)%s))' % (
        c['ref'], F(X + x0), F(Y + y0 - 1.5 - (1.0 if fa else 0)), fa, '' if vis_ref else ' hide'))
    o.append('  (property "Value" "%s" (at %s %s %d) (effects (font (size 1.27 1.27)) (justify left)%s))' % (
        c['value'], F(X + x0), F(Y + y1 + 2.0 + (1.0 if fa else 0)), fa, '' if vis_ref else ' hide'))
    o.append('  (property "Footprint" "%s" (at %s %s 0) (effects (font (size 1.27 1.27)) hide))' % (c['fp'], F(X), F(Y)))
    o.append('  (property "Datasheet" "" (at %s %s 0) (effects (font (size 1.27 1.27)) hide))' % (F(X), F(Y)))
    for (pn, net, dx, dy, d) in it['pins']:
        o.append('  (pin "%s" (uuid %s))' % (pn, U()))
    o.append('  (instances (project "%s" (path "/%s" (reference "%s") (unit %d)))))' % (PROJECT, ROOT, c['ref'], it['unit']))
    out.append('\n'.join(o))
    # Netze anbinden: gleiche Netze auf gleicher Seite/Linie per Draht buendeln
    lines = collections.defaultdict(list)   # alle Pins pro Seite/Linie
    for (pn, net, dx, dy, d) in it['pins']:
        lk = (d, round(dx, 2) if d in (0, 180) else round(dy, 2))
        lines[lk].append((dy if d in (0, 180) else dx, net, dx, dy, pn))
    skip = set()
    if not c.get('virtual'):
        for lk, lst in lines.items():
            lst.sort(key=lambda t: t[0])
            i = 0
            while i < len(lst):
                j = i
                while j + 1 < len(lst) and lst[j + 1][1] == lst[i][1] and lst[i][1] is not None and abs(lst[j + 1][0] - lst[j][0]) < 2.6:
                    j += 1
                if j > i:
                    for k in range(i, j):
                        a_, b_ = lst[k], lst[k + 1]
                        if abs(a_[0] - b_[0]) < 0.01: continue
                        out.append('(wire (pts (xy %s %s) (xy %s %s)) (stroke (width 0) (type default)) (uuid %s))' % (F(X + a_[2]), F(Y + a_[3]), F(X + b_[2]), F(Y + b_[3]), U()))
                    for t in lst[i + 1:j + 1]: skip.add(t[4])
                i = j + 1
    for (pn, net, dx, dy, d) in it['pins']:
        if net is not None and not c.get('virtual'):
            if pn in skip: continue
        px, py = X + dx, Y + dy
        if c.get('virtual'):
            continue
        if net is None:
            out.append('(no_connect (at %s %s) (uuid %s))' % (F(px), F(py), U()))
        elif net in POWER_NETS:
            out.append(power_symbol(net, px, py, d))
        else:
            just = 'left' if d in (0, 90) else 'right'
            out.append('(label "%s" (at %s %s %d) (fields_autoplaced) (effects (font (size 1.27 1.27)) (justify %s bottom)) (uuid %s))' % (
                net, F(px), F(py), d, just, U()))
# PWR_FLAGs: an Power-Symbol haengen
for it in placed:
    c = it['c']
    if c.get('virtual'):
        (pn, net, dx, dy, d) = it['pins'][0]
        if net in POWER_NETS:
            out.append(power_symbol(net, it['X'] + dx, it['Y'] + dy, 270))
        else:
            out.append('(label "%s" (at %s %s 270) (fields_autoplaced) (effects (font (size 1.27 1.27)) (justify right bottom)) (uuid %s))' % (net, F(it['X'] + dx), F(it['Y'] + dy), U()))

for (b, bx0, by0, bx1, by1) in blockrects:
    title = dict(BLOCKS)[b]
    out.append('(rectangle (start %s %s) (end %s %s) (stroke (width 0.3) (type dash)) (fill (type none)) (uuid %s))' % (F(bx0), F(by0), F(bx1), F(by1), U()))
    out.append('(text "%s" (at %s %s 0) (effects (font (size 3 3) (thickness 0.6) bold) (justify left bottom)) (uuid %s))' % (title, F(bx0 + 3), F(by0 + 7), U()))

# lib_symbols
libtxt = []
for k, (l, s) in sorted(used_libs.items()):
    sy = get_sym(l, s)
    sy = copy.deepcopy(sy); sy[1] = ('S', k)
    libtxt.append(dump(sy, 1))

head = '''(kicad_sch (version 20230121) (generator eeschema)
  (uuid %s)
  (paper "%s")
  (title_block (title "radaraltimeter24 - Hardware Rev. B (STM32F446 WeAct, K-LC1a, TLV9064)") (date "2026-11-27") (rev "B")
    (comment 1 "Entwurf, nicht aufgebaut/geprueft. Lizenz MIT, github.com/dansparker/radaraltimeter24")
    (comment 2 "Generiert mit generator/gen_sch.py aus generator/design.py - Aenderungen dort machen"))
  (lib_symbols
%s
  )
''' % (ROOT, paper, '\n'.join(libtxt))
tail = '\n(sheet_instances (path "/" (page "1")))\n)\n'
open(os.path.join(HERE, '..', 'radaraltimeter_revb.kicad_sch'), 'w', encoding='utf-8', newline=chr(10)).write(head + '\n'.join(out) + '\n' + tail)
print('Symbole:', len(placed), ' Power-Symbole:', pwr_n)

import json
json.dump(SYMUUID, open(os.path.join(HERE, 'symuuid.json'), 'w'))
