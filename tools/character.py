"""
PocketLife character kit - stylised "urban" Sims built from smooth-shaded
low-poly parts. Every part is a DS display list WITHOUT colour commands: the
game sets the material colour (skin / hair / clothes) before calling the list,
so one mesh serves every Sim.

Units: a Sim is 1.0 tall; each part is modelled around its joint pivot.
"""
import math

E = 1e-9


def spow(x, e):
    return math.copysign(abs(x) ** e, x)


def norm(v):
    l = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2) or 1.0
    return (v[0] / l, v[1] / l, v[2] / l)


def face_n(f):
    n = [0.0, 0.0, 0.0]
    for i in range(1, len(f) - 1):
        a, b, c = f[0], f[i], f[i + 1]
        u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        n[0] += u[1] * v[2] - u[2] * v[1]
        n[1] += u[2] * v[0] - u[0] * v[2]
        n[2] += u[0] * v[1] - u[1] * v[0]
    return n


class Part:
    """list of faces; each face = (verts, normals)"""

    def __init__(self):
        self.faces = []

    def add(self, verts, normals):
        # drop repeated corners (sphere poles)
        vv, nn = [], []
        for v, n in zip(verts, normals):
            if vv and max(abs(v[k] - vv[-1][k]) for k in range(3)) < E:
                continue
            vv.append(v); nn.append(n)
        if len(vv) > 2 and max(abs(vv[0][k] - vv[-1][k]) for k in range(3)) < E:
            vv.pop(); nn.pop()
        if len(vv) < 3:
            return
        fn = face_n(vv)
        if fn[0] ** 2 + fn[1] ** 2 + fn[2] ** 2 < 1e-14:
            return
        # make sure the winding agrees with the normals (outward, counter-clockwise)
        avg = [sum(n[k] for n in nn) for k in range(3)]
        if fn[0] * avg[0] + fn[1] * avg[1] + fn[2] * avg[2] < 0:
            vv.reverse(); nn.reverse()
        if len(vv) <= 4:
            self.faces.append((vv, nn))
        else:  # fan into quads (+ one tri if odd)
            i = 1
            while i + 2 < len(vv):
                self.faces.append(([vv[0], vv[i], vv[i + 1], vv[i + 2]], [nn[0], nn[i], nn[i + 1], nn[i + 2]]))
                i += 2
            if i + 1 < len(vv):
                self.faces.append(([vv[0], vv[i], vv[i + 1]], [nn[0], nn[i], nn[i + 1]]))

    def merge(self, other):
        self.faces += other.faces
        return self

    def xform(self, fn_pos, fn_nrm=None):
        out = Part()
        for vv, nn in self.faces:
            out.faces.append(([fn_pos(v) for v in vv], [norm(fn_nrm(n)) if fn_nrm else n for n in nn]))
        return out

    def move(self, dx, dy, dz):
        return self.xform(lambda v: (v[0] + dx, v[1] + dy, v[2] + dz))

    def mirror_x(self):
        p = Part()
        for vv, nn in self.faces:
            p.faces.append(([(-v[0], v[1], v[2]) for v in reversed(vv)], [(-n[0], n[1], n[2]) for n in reversed(nn)]))
        return p

    def rot_x(self, deg):
        a = math.radians(deg)
        c, s = math.cos(a), math.sin(a)
        f = lambda v: (v[0], v[1] * c - v[2] * s, v[1] * s + v[2] * c)
        return self.xform(f, f)


# ---------------------------------------------------------------- primitives
def blob(cx, cy, cz, rx, ry, rz, lon=8, lat=6, e=1.0, ymin=-1.0, ymax=1.0, cap=True):
    """super-ellipsoid (e<1 = boxier). ymin/ymax in -1..1 cut it (for hair shells)."""
    p = Part()
    t0, t1 = math.asin(max(-1, min(1, ymin))), math.asin(max(-1, min(1, ymax)))

    def pt(t, ph):
        ct, st, cp, sp = math.cos(t), math.sin(t), math.cos(ph), math.sin(ph)
        x = rx * spow(ct, e) * spow(sp, e)
        y = ry * spow(st, e)
        z = rz * spow(ct, e) * spow(cp, e)
        nx = spow(ct, 2 - e) * spow(sp, 2 - e) / rx
        ny = spow(st, 2 - e) / ry
        nz = spow(ct, 2 - e) * spow(cp, 2 - e) / rz
        return (cx + x, cy + y, cz + z), norm((nx, ny, nz))

    for i in range(lat):
        ta = t0 + (t1 - t0) * i / lat
        tb = t0 + (t1 - t0) * (i + 1) / lat
        for j in range(lon):
            pa = 2 * math.pi * j / lon
            pb = 2 * math.pi * (j + 1) / lon
            q = [pt(ta, pa), pt(ta, pb), pt(tb, pb), pt(tb, pa)]
            p.add([v for v, _ in q], [n for _, n in q])
    if cap and ymin > -1:
        ring = [pt(t0, 2 * math.pi * j / lon)[0] for j in range(lon)]
        p.add(ring, [(0, -1, 0)] * lon)
    return p


def tube(y0, y1, r0, r1, sides=6, cap0=True, cap1=True, z0=None, z1=None, offz=0.0):
    """tapered elliptical prism along y. r = x radius, z = z radius"""
    z0 = r0 if z0 is None else z0
    z1 = r1 if z1 is None else z1
    p = Part()
    slope = (r0 - r1) / (y1 - y0) if y1 != y0 else 0
    ring = lambda y, rx, rz: [(rx * math.sin(2 * math.pi * k / sides + math.pi / sides),
                              y, offz + rz * math.cos(2 * math.pi * k / sides + math.pi / sides)) for k in range(sides)]
    a, b = ring(y0, r0, z0), ring(y1, r1, z1)
    for k in range(sides):
        k2 = (k + 1) % sides
        ang = [2 * math.pi * k / sides + math.pi / sides, 2 * math.pi * k2 / sides + math.pi / sides]
        n = [norm((math.sin(t), slope * (1 if y1 > y0 else -1), math.cos(t))) for t in ang]
        p.add([a[k], a[k2], b[k2], b[k]], [n[0], n[1], n[1], n[0]])
    up = (0, 1, 0) if y1 > y0 else (0, -1, 0)
    dn = (-up[0], -up[1], -up[2])
    if cap0:
        p.add(a, [dn] * sides)
    if cap1:
        p.add(b, [up] * sides)
    return p


def rbox(x0, y0, z0, x1, y1, z1, bevel=0.012):
    """box with chamfered vertical edges (8 side faces) - chunky sneaker / pocket"""
    b = bevel
    if b <= 0:
        pts = [(x0, z1), (x1, z1), (x1, z0), (x0, z0)]
    else:
        pts = [(x0 + b, z1), (x1 - b, z1), (x1, z1 - b), (x1, z0 + b), (x1 - b, z0), (x0 + b, z0), (x0, z0 + b), (x0, z1 - b)]
    p = Part()
    n = len(pts)
    for k in range(n):
        a, c = pts[k], pts[(k + 1) % n]
        nx, nz = c[1] - a[1], -(c[0] - a[0])
        nn = norm((nx, 0, nz))
        p.add([(a[0], y0, a[1]), (c[0], y0, c[1]), (c[0], y1, c[1]), (a[0], y1, a[1])], [nn] * 4)
    p.add([(x, y1, z) for x, z in pts], [(0, 1, 0)] * n)
    p.add([(x, y0, z) for x, z in pts], [(0, -1, 0)] * n)
    return p


def box(x0, y0, z0, x1, y1, z1):
    return rbox(x0, y0, z0, x1, y1, z1, 0)


def spike(bx, by, bz, tx, ty, tz, r):
    """4-sided pyramid from base centre to tip"""
    p = Part()
    d = norm((tx - bx, ty - by, tz - bz))
    # pick two vectors perpendicular to d
    a = norm((d[1], -d[0], 0)) if abs(d[2]) < 0.9 else norm((0, d[2], -d[1]))
    b = norm((d[1] * a[2] - d[2] * a[1], d[2] * a[0] - d[0] * a[2], d[0] * a[1] - d[1] * a[0]))
    base = [(bx + r * (a[0] * c + b[0] * s), by + r * (a[1] * c + b[1] * s), bz + r * (a[2] * c + b[2] * s))
            for c, s in ((1, 0), (0, 1), (-1, 0), (0, -1))]
    tip = (tx, ty, tz)
    for k in range(4):
        v0, v1 = base[k], base[(k + 1) % 4]
        n = norm(face_n([v0, v1, tip]))
        p.add([v0, v1, tip], [n, n, n])
    return p


# ---------------------------------------------------------------- the kit
# joint heights (Sim = 1.0 tall): hip 0.40, knee 0.22, ankle 0.06, shoulder 0.645, neck 0.68
HEAD_CY, HEAD_RX, HEAD_RY, HEAD_RZ = 0.135, 0.150, 0.140, 0.135


def head():
    p = blob(0, HEAD_CY, 0, HEAD_RX, HEAD_RY, HEAD_RZ, lon=8, lat=5, e=0.62)
    # ears
    p.merge(blob(0.150, 0.125, -0.005, 0.022, 0.034, 0.020, lon=4, lat=2))
    p.merge(blob(-0.150, 0.125, -0.005, 0.022, 0.034, 0.020, lon=4, lat=2))
    # nose
    p.merge(blob(0, 0.105, HEAD_RZ - 0.004, 0.020, 0.024, 0.020, lon=4, lat=2))
    # neck
    p.merge(tube(-0.05, 0.03, 0.045, 0.042, sides=5, cap0=False, cap1=False))
    return p


def hair(style):
    cy, rx, ry, rz = HEAD_CY, HEAD_RX + 0.014, HEAD_RY + 0.016, HEAD_RZ + 0.014
    if style == 0:   # spiky
        p = blob(0, cy + 0.004, -0.006, rx, ry, rz, lon=8, lat=2, e=0.62, ymin=0.22)
        for (x, z, tx, ty, tz) in [(0, 0.06, 0.0, 0.33, 0.16), (0.07, 0.03, 0.12, 0.31, 0.10), (-0.07, 0.03, -0.12, 0.31, 0.10),
                                   (0.05, -0.06, 0.10, 0.32, -0.12), (-0.05, -0.06, -0.10, 0.32, -0.12), (0, -0.02, 0.0, 0.35, -0.03)]:
            p.merge(spike(x, cy + 0.11, z, tx, ty, tz, 0.045))
        return p
    if style == 1:   # long / bob with bangs
        p = blob(0, cy + 0.004, -0.004, rx, ry, rz, lon=8, lat=2, e=0.62, ymin=0.15)
        p.merge(box(-rx, -0.03, -rz - 0.01, rx, cy + 0.06, -0.03))            # back
        p.merge(box(-rx - 0.005, -0.01, -0.06, -rx + 0.03, cy + 0.08, 0.07))  # sides
        p.merge(box(rx - 0.03, -0.01, -0.06, rx + 0.005, cy + 0.08, 0.07))
        p.merge(box(-0.10, cy + 0.07, rz - 0.04, 0.06, cy + 0.12, rz + 0.012))  # bangs
        return p
    if style == 2:   # afro
        return blob(0, cy + 0.06, -0.015, rx + 0.05, ry + 0.03, rz + 0.04, lon=8, lat=3, e=0.9, ymin=-0.05)
    if style == 3:   # ponytail
        p = blob(0, cy + 0.004, -0.006, rx, ry, rz, lon=8, lat=2, e=0.62, ymin=0.12)
        p.merge(blob(0, cy + 0.08, -rz - 0.01, 0.04, 0.04, 0.035, lon=4, lat=2))
        p.merge(tube(cy + 0.07, -0.02, 0.035, 0.022, sides=5, z0=0.03, z1=0.02, offz=-rz - 0.035))
        return p
    if style == 4:   # backwards cap
        p = blob(0, cy + 0.02, -0.004, rx + 0.004, ry, rz + 0.004, lon=8, lat=2, e=0.7, ymin=0.25)
        p.merge(box(-0.08, cy + 0.07, -rz - 0.09, 0.08, cy + 0.085, -rz + 0.02))   # brim at the back
        return p
    # 5: mohawk
    p = blob(0, cy + 0.002, -0.004, rx - 0.004, ry - 0.004, rz - 0.004, lon=8, lat=2, e=0.62, ymin=0.45)
    for k in range(5):
        z = 0.09 - k * 0.05
        p.merge(spike(0, cy + 0.12 - abs(k - 1.5) * 0.008, z, 0, cy + 0.25 - abs(k - 2) * 0.02, z - 0.03, 0.03))
    return p


HAIR_STYLES = 6


def torso(top):
    """hips are separate (pants colour). top 0 = hoodie, 1 = tee"""
    if top == 0:
        p = tube(0.34, 0.52, 0.18, 0.195, sides=8, cap0=True, cap1=False, z0=0.11, z1=0.118)
        p.merge(tube(0.52, 0.655, 0.195, 0.17, sides=8, cap0=False, cap1=True, z0=0.118, z1=0.11))
        p.merge(blob(0, 0.655, -0.07, 0.13, 0.05, 0.07, lon=6, lat=2, e=0.8))                      # hood
        p.merge(box(-0.09, 0.40, 0.10, 0.09, 0.47, 0.13))                                          # pocket
        return p
    p = tube(0.37, 0.52, 0.16, 0.18, sides=8, cap0=True, cap1=False, z0=0.10, z1=0.108)
    p.merge(tube(0.52, 0.65, 0.18, 0.155, sides=8, cap0=False, cap1=True, z0=0.108, z1=0.10))
    return p


def hips():
    return tube(0.33, 0.40, 0.16, 0.165, sides=8, z0=0.100, z1=0.104)


def upper_arm(top):
    r = 0.056 if top == 0 else 0.05
    return tube(0.02, -0.15, r, r - 0.006, sides=6)


def lower_arm(top):
    if top == 0:
        return tube(0.0, -0.14, 0.046, 0.054, sides=6, cap0=False)        # baggy sleeve
    return tube(0.01, -0.14, 0.038, 0.033, sides=6, cap0=False)           # bare forearm


def hand():
    return blob(0, -0.04, 0.005, 0.036, 0.046, 0.028, lon=5, lat=3, e=0.85)


def upper_leg():
    return tube(0.0, -0.18, 0.072, 0.064, sides=6, cap0=False, z0=0.075, z1=0.066)


def lower_leg():
    return tube(0.0, -0.165, 0.064, 0.074, sides=6, cap0=False, z0=0.066, z1=0.076)  # baggy, flares out


def shoe():
    return rbox(-0.054, -0.045, -0.065, 0.054, 0.01, 0.115, 0.03)


def sole():
    return box(-0.058, -0.064, -0.07, 0.058, -0.044, 0.125)


def parts():
    """(name, Part) in the order the game indexes them"""
    out = [("HEAD", head()), ("HIPS", hips()), ("UPPER_LEG", upper_leg()), ("LOWER_LEG", lower_leg()),
           ("SHOE", shoe()), ("SOLE", sole()), ("HAND", hand())]
    for t, nm in ((0, "HOODIE"), (1, "TEE")):
        out.append((f"TORSO_{nm}", torso(t)))
        out.append((f"UARM_{nm}", upper_arm(t)))
        out.append((f"LARM_{nm}", lower_arm(t)))
    for s in range(HAIR_STYLES):
        out.append((f"HAIR_{s}", hair(s)))
    return out


# ---------------------------------------------------------------- encoding
def _f412(x):
    v = int(round(x * 4096))
    return max(-32768, min(32767, v)) & 0xFFFF


def _n10(x):
    v = int(round(x * 511))
    return max(-512, min(511, v)) & 0x3FF


def encode_part(part):
    """packed GX list: BEGIN, then NORMAL+VTX16 per vertex; no colour commands"""
    cmds = []
    for kind, n in ((0, 3), (1, 4)):
        sel = [f for f in part.faces if len(f[0]) == n]
        if not sel:
            continue
        cmds.append((0x40, [kind]))
        for vv, nn in sel:
            for v, nr in zip(vv, nn):
                cmds.append((0x21, [_n10(nr[0]) | (_n10(nr[1]) << 10) | (_n10(nr[2]) << 20)]))
                cmds.append((0x23, [_f412(v[0]) | (_f412(v[1]) << 16), _f412(v[2])]))
    words = []
    for i in range(0, len(cmds), 4):
        grp = cmds[i:i + 4]
        hdr = 0
        for k, (cid, _) in enumerate(grp):
            hdr |= cid << (8 * k)
        words.append(hdr)
        for _, ps in grp:
            words.extend(ps)
    return [len(words)] + words


def write_c(fh_h, fh_c):
    ps = parts()
    fh_h.write("\n// character kit (tools/character.py): colourless parts, set the material first\nenum {\n")
    for nm, _ in ps:
        fh_h.write(f"    CP_{nm},\n")
    fh_h.write("    CP_COUNT\n};\n#define CP_HAIR_STYLES %d\n" % HAIR_STYLES)
    fh_h.write("extern const u32* const g_charParts[CP_COUNT];\nextern const u16 g_charPolys[CP_COUNT];\n")
    total = 0
    for nm, p in ps:
        dl = encode_part(p)
        fh_c.write(f"static const u32 cp_{nm.lower()}[{len(dl)}] __attribute__((aligned(4))) = {{\n")
        for j in range(0, len(dl), 8):
            fh_c.write("    " + ", ".join(f"0x{x & 0xFFFFFFFF:08X}" for x in dl[j:j + 8]) + ",\n")
        fh_c.write("};\n")
    fh_c.write("const u32* const g_charParts[CP_COUNT] = {\n")
    for nm, _ in ps:
        fh_c.write(f"    cp_{nm.lower()},\n")
    fh_c.write("};\nconst u16 g_charPolys[CP_COUNT] = { " + ", ".join(str(len(p.faces)) for _, p in ps) + " };\n")
    return [(nm, len(p.faces)) for nm, p in ps]
