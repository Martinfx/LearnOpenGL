"""The plan of Mulberry Street (docs/LEVEL_ART_GUIDE.md section 12): streets, block faces, the lots on them.

Coordinates in metres, x east, z south (north is -z), y up. A piece of the Lower Manhattan grid (martin 2026-11-03:
"bigger, and properly"): four streets north-south (Baxter, Mulberry, Mott, Elizabeth) and four across (Broome,
Grand, Hester, Canal), nine blocks. Every block face is a row of narrow houses, one per lot (zonegen/walkup.py);
the back yards meet inside the blocks. Round the grid stand closed rows of houses (the edge of the map); Mulberry
runs on north of Broome Street to the church that closes it. Used by make_overgrown_street.py and draw_plan().
"""
import math

DEEP, SHALLOW = 18.0, 14.0     # house depths: on the long block faces / on the short ones and the outer rows
# (name, centre, width between the house fronts), west to east and north to south
NS = [('Baxter Street', -74.0, 16.0), ('Mulberry Street', 0.0, 16.0), ('Mott Street', 74.0, 16.0), ('Elizabeth Street', 148.0, 16.0)]
EW = [('Broome Street', -189.0, 14.0), ('Grand Street', -103.0, 14.0), ('Hester Street', -17.0, 14.0), ('Canal Street', 75.0, 30.0)]
MUL = 8.0                                        # Mulberry Street: house fronts at x = +-8
CHURCH_Z = -238.0                                # the church front closes Mulberry here
WEST = NS[0][1] - NS[0][2] / 2                   # -82: the outermost house fronts of the grid
EAST = NS[-1][1] + NS[-1][2] / 2                 # 156
NORTH = EW[0][1] - EW[0][2] / 2                  # -196
SOUTH = EW[-1][1] + EW[-1][2] / 2                # 90
CANAL = (EW[-1][1] - EW[-1][2] / 2, SOUTH)       # (60, 90)
CANAL_X = (WEST - 2 * SHALLOW, EAST + 2 * SHALLOW)   # Canal Street ends (wrecks) at these x
STUB = SHALLOW                                   # the cross streets run on between the outer rows to a closing house


def _ew(name):
    return next(e for e in EW if e[0] == name)


HESTER = (_ew('Hester Street')[1] - 7.0, _ew('Hester Street')[1] + 7.0)
GRAND = (_ew('Grand Street')[1] - 7.0, _ew('Grand Street')[1] + 7.0)
BROOME = (_ew('Broome Street')[1] - 7.0, _ew('Broome Street')[1] + 7.0)

STREETS = {}                                     # name -> (x0, z0, x1, z1) between the house fronts
for _n, _c, _w in NS:
    STREETS[_n] = (_c - _w / 2, (CHURCH_Z if _n == 'Mulberry Street' else NORTH), _c + _w / 2, CANAL[0])
for _n, _c, _w in EW:
    if _n == 'Canal Street':
        STREETS[_n] = (CANAL_X[0], _c - _w / 2, CANAL_X[1], _c + _w / 2)
    else:
        STREETS[_n] = (WEST - STUB, _c - _w / 2, EAST + STUB, _c + _w / 2)


class Face:
    """A row of house fronts: starts at (x0, z0), the fronts face n (unit, axis-aligned), runs along u = (nz, -nx)
    (the house's local +x) for length L. corner_start / corner_end: that end is a street corner."""
    def __init__(self, name, x0, z0, n, L, depth, corner_start=False, corner_end=False, kind='inner'):
        self.name, self.x0, self.z0, self.n, self.L, self.depth = name, x0, z0, n, L, depth
        self.u = (n[1], -n[0])
        self.corner_start, self.corner_end, self.kind = corner_start, corner_end, kind
        self.yaw = math.degrees(math.atan2(n[0], n[1]))

    def point(self, s, back=0.0):
        """World (x, z) at distance s along the face, `back` metres behind the fronts."""
        return (self.x0 + self.u[0] * s - self.n[0] * back, self.z0 + self.u[1] * s - self.n[1] * back)

    def contains(self, x, z, tol=0.6):
        """The point lies on this row of fronts."""
        s = (x - self.x0) * self.u[0] + (z - self.z0) * self.u[1]
        d = (x - self.x0) * self.n[0] + (z - self.z0) * self.n[1]
        return -0.1 <= s <= self.L + 0.1 and abs(d) < tol


def blocks():
    """Inner blocks between the streets: (name, x0, z0, x1, z1) of the house fronts round them."""
    out = []
    for i in range(len(NS) - 1):
        xa, xb = NS[i][1] + NS[i][2] / 2, NS[i + 1][1] - NS[i + 1][2] / 2
        for j in range(len(EW) - 1):
            za, zb = EW[j][1] + EW[j][2] / 2, EW[j + 1][1] - EW[j + 1][2] / 2
            out.append((f'{NS[i][0].split()[0]}-{NS[i + 1][0].split()[0]} / {EW[j][0].split()[0]}-{EW[j + 1][0].split()[0]}', xa, za, xb, zb))
    return out


def faces():
    out = []
    for name, x0, z0, x1, z1 in blocks():
        out.append(Face(f'{name} west', x0, z0, (-1.0, 0.0), z1 - z0, DEEP, True, True))         # runs south
        out.append(Face(f'{name} east', x1, z1, (1.0, 0.0), z1 - z0, DEEP, True, True))          # runs north
        out.append(Face(f'{name} north', x1 - DEEP, z0, (0.0, -1.0), (x1 - x0) - 2 * DEEP, SHALLOW))
        out.append(Face(f'{name} south', x0 + DEEP, z1, (0.0, 1.0), (x1 - x0) - 2 * DEEP, SHALLOW))
    # the outer rows: closed fronts round the grid, the natural edge of the map
    for j in range(len(EW) - 1):
        za, zb = EW[j][1] + EW[j][2] / 2, EW[j + 1][1] - EW[j + 1][2] / 2
        out.append(Face(f'outer west {j}', WEST, zb, (1.0, 0.0), zb - za, SHALLOW, True, True, 'outer'))
        out.append(Face(f'outer east {j}', EAST, za, (-1.0, 0.0), zb - za, SHALLOW, True, True, 'outer'))
    for nm, c, w in EW:
        if nm == 'Canal Street':
            continue
        for sg, xe in ((-1, WEST - STUB), (1, EAST + STUB)):        # the house across the end of the cross street
            z0, z1 = c - w / 2, c + w / 2
            out.append(Face(f'{nm} end {sg}', xe, z1 if sg < 0 else z0, (-sg, 0.0), w, SHALLOW, kind='outer'))
    # north of Broome Street: one closed row, broken by Mulberry, which runs on to the church
    out.append(Face('outer north west', WEST - SHALLOW, NORTH, (0.0, 1.0), -MUL - WEST + SHALLOW - DEEP, SHALLOW, True, False, 'outer'))
    out.append(Face('outer north east', MUL + DEEP, NORTH, (0.0, 1.0), EAST + SHALLOW - MUL - DEEP, SHALLOW, False, True, 'outer'))
    out.append(Face('Mulberry north west', -MUL, NORTH, (1.0, 0.0), NORTH - CHURCH_Z, DEEP, True, False, 'outer'))
    out.append(Face('Mulberry north east', MUL, CHURCH_Z, (-1.0, 0.0), NORTH - CHURCH_Z, DEEP, False, True, 'outer'))
    # Canal Street: its south side one long closed row; its north side out past the outer rows to the wrecks
    out.append(Face('Canal south', CANAL_X[1], SOUTH, (0.0, -1.0), CANAL_X[1] - CANAL_X[0], SHALLOW, kind='outer'))
    out.append(Face('Canal north west', CANAL_X[0], CANAL[0], (0.0, 1.0), WEST - SHALLOW - CANAL_X[0], SHALLOW, kind='outer'))
    out.append(Face('Canal north east', EAST + SHALLOW, CANAL[0], (0.0, 1.0), CANAL_X[1] - EAST - SHALLOW, SHALLOW, kind='outer'))
    return [f for f in out if f.L > 3.0]


def inside_map(x, z):
    """In a street or in a block's yards: where the player can be."""
    for (x0, z0, x1, z1) in STREETS.values():
        if x0 <= x <= x1 and z0 <= z <= z1:
            return True
    return any(x0 <= x <= x1 and z0 <= z <= z1 for _n, x0, z0, x1, z1 in blocks())


def draw_plan(path, lots_by_face=None, notes=()):
    """Top view of the plan: streets, houses, yards, the critical path and landmarks."""
    from PIL import Image, ImageDraw, ImageFont
    X0, X1, Z0, Z1, S, M = WEST - 50, EAST + 50, CHURCH_Z - 40, SOUTH + 30, 2.4, 30
    W, H = int((X1 - X0) * S) + 2 * M + 330, int((Z1 - Z0) * S) + 2 * M
    img = Image.new('RGB', (W, H), (236, 232, 222))
    d = ImageDraw.Draw(img)
    try:
        f = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 13)
        fb = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 15)
    except OSError:
        f = fb = ImageFont.load_default()

    def P(x, z):
        return (M + (x - X0) * S, M + (z - Z0) * S)

    def rect(x0, z0, x1, z1, **kw):
        a, b = P(min(x0, x1), min(z0, z1)), P(max(x0, x1), max(z0, z1))
        d.rectangle([a, b], **kw)
    for _n, x0, z0, x1, z1 in blocks():
        rect(x0, z0, x1, z1, fill=(196, 214, 170))
    for name, (x0, z0, x1, z1) in STREETS.items():
        rect(x0, z0, x1, z1, fill=(120, 120, 124))
    for fc in faces():
        lots = (lots_by_face or {}).get(fc.name) or [[0.0, fc.L, None]]
        for lot in lots:
            s, w = lot[0], lot[1]
            info = lot[2] if len(lot) > 2 else None
            a = fc.point(s)
            b = fc.point(s + w, fc.depth)
            col = (150, 92, 72) if fc.kind == 'inner' else (118, 96, 88)
            if info:
                col = {'enter': (214, 120, 60), 'burnt': (60, 50, 46), 'vacant': (170, 190, 140), 'skeleton': (150, 150, 160),
                       'church': (90, 60, 110)}.get(info, col)
            rect(a[0], a[1], b[0], b[1], fill=col, outline=(70, 50, 44))
    rect(-16, CHURCH_Z - 35, 16, CHURCH_Z, fill=(90, 60, 110), outline=(40, 30, 50))
    for name, (x0, z0, x1, z1) in STREETS.items():
        cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
        txt = name.upper()
        if abs(z1 - z0) > abs(x1 - x0):
            t = Image.new('RGBA', (int(d.textlength(txt, font=fb)) + 4, 20), (0, 0, 0, 0))
            ImageDraw.Draw(t).text((2, 1), txt, font=fb, fill=(255, 255, 255))
            t = t.rotate(90, expand=True)
            p = P(cx, cz + 25)
            img.paste(t, (int(p[0] - t.width / 2), int(p[1] - t.height / 2)), t)
        else:
            p = P(cx - 35, cz)
            d.text((p[0] - d.textlength(txt, font=fb) / 2, p[1] - 9), txt, font=fb, fill=(255, 255, 255))
    for x, z, label, col in notes:
        p = P(x, z)
        d.ellipse([p[0] - 7, p[1] - 7, p[0] + 7, p[1] + 7], fill=col, outline=(0, 0, 0))
        d.text((p[0] + 10, p[1] - 8), label, font=f, fill=(20, 20, 20))
    lx = W - 320
    d.text((lx, M), 'MULBERRY STREET - plan', font=fb, fill=(0, 0, 0))
    items = [((150, 92, 72), 'house on its lot (7.6 m, 25 ft)'), ((214, 120, 60), 'enterable: hall, stair, roof, yard door'),
             ((60, 50, 46), 'burnt-out shell'), ((150, 150, 160), 'concrete skeleton (gutted loft)'),
             ((170, 190, 140), 'vacant lot, rubble / collapse'), ((118, 96, 88), 'closed row: edge of the map'),
             ((90, 60, 110), 'the church (goal, landmark)'), ((196, 214, 170), 'back yards inside the blocks'),
             ((120, 120, 124), 'street')]
    for k, (c, t) in enumerate(items):
        y = M + 30 + k * 22
        d.rectangle([lx, y, lx + 16, y + 14], fill=c, outline=(0, 0, 0))
        d.text((lx + 24, y), t, font=f, fill=(0, 0, 0))
    y = M + 30 + len(items) * 22 + 14
    for t in ('1 m = 2.4 px, north is up', f'{int(EAST - WEST)} x {int(SOUTH - NORTH)} m of streets, 9 blocks',
              'critical path: start on Canal Street', '-> Hester (camp, Eddie) -> a collapse',
              'closes Mulberry -> through a house and', 'the yards, or round by Mott Street',
              '-> Grand -> Broome -> the church (weenie,', 'in sight from the start)'):
        d.text((lx, y), t, font=f, fill=(0, 0, 0))
        y += 18
    img.save(path)
