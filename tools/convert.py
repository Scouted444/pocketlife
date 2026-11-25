#!/usr/bin/env python3
"""
PocketLife asset converter.

Takes Kenney's Furniture Kit (CC0, https://kenney.nl/assets/furniture-kit),
finds the OBJ models the game needs, and turns each one into:
  * a Nintendo DS geometry-engine display list (packed GX commands,
    flat normals + per-material colours, auto-decimated to a polygon cap)
  * a 32x32 RGB15 catalog icon (from the kit's own renders when present,
    otherwise software-rendered from the mesh)

Output: source/gen_assets.c and include/gen_assets.h

Anything the kit doesn't have (or if the download failed) falls back to a
simple coloured box so the ROM always builds.

usage: convert.py <kit_dir> <repo_root>
"""
import math
import os
import re
import sys

try:
    from PIL import Image
except ImportError:  # icons fall back to the software renderer
    Image = None

# --------------------------------------------------------------------------
# What the game needs. key, candidate OBJ names (first match wins),
# fallback box (w, d, h in tiles, rgb), polygon cap, stack parts.
# --------------------------------------------------------------------------
MODELS = [
    # walls first: they define the tile size
    ("WALL",        ["wall"],                               (1, .1, 1.3, (230, 225, 210)), 16, None),
    ("WALL_WINDOW", ["wallWindow", "wallWindowSlide"],      (1, .1, 1.3, (200, 220, 240)), 48, None),
    ("WALL_DOOR",   ["wallDoorway", "wallDoorwayWide"],     (1, .1, 1.3, (180, 140, 100)), 40, None),
    ("FRIDGE",      ["kitchenFridge", "kitchenFridgeLarge"], (.8, .7, 1.3, (235, 235, 240)), 90, None),
    ("STOVE",       ["kitchenStove", "kitchenStoveElectric"], (.8, .7, .7, (200, 200, 205)), 110, None),
    ("COUNTER",     ["kitchenCabinet", "kitchenCabinetDrawer"], (.8, .7, .7, (190, 150, 110)), 80, None),
    ("KSINK",       ["kitchenSink"],                        (.8, .7, .7, (180, 190, 200)), 110, None),
    ("BED_SINGLE",  ["bedSingle"],                          (1, 2, .5, (120, 160, 220)), 110, None),
    ("BED_DOUBLE",  ["bedDouble"],                          (2, 2, .5, (200, 110, 120)), 120, None),
    ("TOILET",      ["toilet", "toiletSquare"],             (.6, .8, .7, (245, 245, 245)), 110, None),
    ("SHOWER",      ["shower", "showerRound"],              (.9, .9, 1.4, (190, 220, 235)), 110, None),
    ("BATHTUB",     ["bathtub"],                            (1, 2, .5, (240, 240, 245)), 110, None),
    ("BSINK",       ["bathroomSink", "bathroomSinkSquare"], (.6, .5, .9, (240, 240, 240)), 100, None),
    ("TV",          ["televisionModern", "televisionVintage"], (1, .4, 1.0, (50, 50, 60)), 90, None),
    ("SOFA",        ["loungeSofa", "loungeDesignSofa"],     (2, .9, .7, (90, 140, 120)), 120, None),
    ("ARMCHAIR",    ["loungeChair", "loungeDesignChair"],   (.9, .9, .7, (160, 110, 90)), 110, None),
    ("COMPUTER",    None,                                   (1, .6, 1.0, (150, 120, 90)), 150,
                    [["desk"], ["computerScreen", "laptop"]]),
    ("BOOKCASE",    ["bookcaseOpen", "bookcaseClosedWide", "bookcaseClosed"], (1, .4, 1.4, (150, 105, 70)), 110, None),
    ("TABLE",       ["table", "tableCloth"],                (1, 1, .7, (170, 125, 85)), 70, None),
    ("CHAIR",       ["chair", "chairCushion"],              (.6, .6, .9, (160, 115, 75)), 70, None),
    ("PLANT",       ["pottedPlant", "plantSmall1"],         (.5, .5, .9, (80, 160, 70)), 90, None),
    ("LAMP",        ["lampRoundFloor", "lampSquareFloor"],  (.4, .4, 1.3, (250, 230, 160)), 60, None),
    ("RUG",         ["rugRectangle", "rugRounded", "rugSquare"], (2, 1, .02, (190, 80, 80)), 24, None),
    ("RADIO",       ["radio", "speaker"],                   (.5, .3, .4, (60, 60, 70)), 70, None),
    ("TRASH",       ["trashcan"],                           (.4, .4, .5, (130, 140, 140)), 60, None),
    ("SIDETABLE",   ["sideTable", "sideTableDrawers"],      (.6, .6, .6, (170, 125, 85)), 60, None),
    ("COFFEETABLE", ["tableCoffee", "tableCoffeeGlass"],    (1.2, .7, .4, (150, 105, 70)), 70, None),
    ("DRESSER",     ["cabinetBedDrawer", "cabinetBedDrawerTable", "cabinetBed"], (1, .5, .8, (190, 150, 110)), 80, None),
    ("WASHER",      ["washer", "washerDryerStacked"],       (.8, .8, .9, (235, 235, 240)), 90, None),
    ("COATRACK",    ["coatRackStanding", "coatRack"],       (.4, .4, 1.4, (140, 100, 70)), 70, None),
]

ICON = 32


# --------------------------------------------------------------------------
# OBJ / MTL loading
# --------------------------------------------------------------------------
class Mesh:
    def __init__(self):
        self.faces = []  # list of (verts[(x,y,z)], rgb)

    def bbox(self):
        xs = [v[0] for f, _ in self.faces for v in f]
        ys = [v[1] for f, _ in self.faces for v in f]
        zs = [v[2] for f, _ in self.faces for v in f]
        return min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)

    def transform(self, fn):
        self.faces = [([fn(v) for v in f], c) for f, c in self.faces]


def load_mtl(path):
    mats, cur = {}, None
    base = os.path.dirname(path)
    try:
        lines = open(path, encoding="utf-8", errors="ignore").read().splitlines()
    except OSError:
        return mats
    for ln in lines:
        p = ln.strip().split()
        if not p:
            continue
        if p[0] == "newmtl":
            cur = " ".join(p[1:])
            mats[cur] = {"kd": (0.8, 0.8, 0.8), "tex": None}
        elif cur and p[0] == "Kd" and len(p) >= 4:
            mats[cur]["kd"] = tuple(float(x) for x in p[1:4])
        elif cur and p[0] == "map_Kd":
            t = os.path.join(base, p[-1])
            if os.path.exists(t) and Image is not None:
                try:
                    mats[cur]["tex"] = Image.open(t).convert("RGB")
                except Exception:
                    pass
    return mats


def load_obj(path):
    vs, vts = [], []
    mats, cur = {}, None
    mesh = Mesh()
    for ln in open(path, encoding="utf-8", errors="ignore"):
        p = ln.strip().split()
        if not p:
            continue
        if p[0] == "v":
            vs.append(tuple(float(x) for x in p[1:4]))
        elif p[0] == "vt":
            vts.append(tuple(float(x) for x in p[1:3]))
        elif p[0] == "mtllib":
            mats.update(load_mtl(os.path.join(os.path.dirname(path), " ".join(p[1:]))))
        elif p[0] == "usemtl":
            cur = " ".join(p[1:])
        elif p[0] == "f":
            idx, uvs = [], []
            for t in p[1:]:
                a = t.split("/")
                i = int(a[0])
                idx.append(vs[i - 1] if i > 0 else vs[i])
                if len(a) > 1 and a[1]:
                    j = int(a[1])
                    uvs.append(vts[j - 1] if j > 0 else vts[j])
            m = mats.get(cur, {"kd": (0.8, 0.8, 0.8), "tex": None})
            kd = m["kd"]
            if m["tex"] is not None and uvs:
                u = sum(t[0] for t in uvs) / len(uvs)
                v = sum(t[1] for t in uvs) / len(uvs)
                img = m["tex"]
                px = img.getpixel((int((u % 1.0) * (img.width - 1)), int((1 - (v % 1.0)) * (img.height - 1))))
                rgb = tuple(c for c in px[:3])
                # Kd multiplies the texture
                rgb = tuple(int(rgb[k] * min(1.0, kd[k] if max(kd) > 0 else 1)) for k in range(3))
            else:
                rgb = tuple(int(max(0, min(1, c)) * 255) for c in kd)
            if len(idx) >= 3:
                mesh.faces.append((idx, rgb))
    return mesh


def box_mesh(w, d, h, rgb):
    m = Mesh()
    x0, x1, z0, z1 = -w / 2, w / 2, -d / 2, d / 2
    v = lambda x, y, z: (x, y, z)
    q = [
        [v(x0, 0, z1), v(x1, 0, z1), v(x1, h, z1), v(x0, h, z1)],  # front +z
        [v(x1, 0, z0), v(x0, 0, z0), v(x0, h, z0), v(x1, h, z0)],  # back
        [v(x1, 0, z1), v(x1, 0, z0), v(x1, h, z0), v(x1, h, z1)],  # +x
        [v(x0, 0, z0), v(x0, 0, z1), v(x0, h, z1), v(x0, h, z0)],  # -x
        [v(x0, h, z1), v(x1, h, z1), v(x1, h, z0), v(x0, h, z0)],  # top
    ]
    for f in q:
        m.faces.append((f, rgb))
    # a darker front stripe so you can tell which way it faces
    dark = tuple(int(c * 0.55) for c in rgb)
    s = 0.01
    m.faces.append(([v(x0 * .6, h * .3, z1 + s), v(x1 * .6, h * .3, z1 + s),
                     v(x1 * .6, h * .7, z1 + s), v(x0 * .6, h * .7, z1 + s)], dark))
    return m


# --------------------------------------------------------------------------
# geometry helpers
# --------------------------------------------------------------------------
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def face_normal(f):
    n = (0.0, 0.0, 0.0)
    for i in range(1, len(f) - 1):
        c = cross(sub(f[i], f[0]), sub(f[i + 1], f[0]))
        n = (n[0] + c[0], n[1] + c[1], n[2] + c[2])
    l = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
    if l < 1e-12:
        return None
    return (n[0] / l, n[1] / l, n[2] / l)


def split_polys(faces):
    """turn arbitrary polygons into DS-friendly tris and quads"""
    out = []
    for f, c in faces:
        # drop repeated verts
        g = []
        for v in f:
            if not g or v != g[-1]:
                g.append(v)
        if len(g) > 1 and g[0] == g[-1]:
            g.pop()
        if len(g) < 3:
            continue
        if face_normal(g) is None:
            continue
        if len(g) == 3:
            out.append((g, c))
        elif len(g) == 4:
            out.append((g, c))
        else:
            # fan into quads + tris
            i = 1
            while i + 2 < len(g):
                out.append(([g[0], g[i], g[i + 1], g[i + 2]], c))
                i += 2
            if i + 1 < len(g):
                out.append(([g[0], g[i], g[i + 1]], c))
    # remove degenerate pieces produced by the fan
    return [(f, c) for f, c in out if face_normal(f) is not None]


def decimate(faces, cap):
    """vertex-clustering decimation until polygon count <= cap"""
    polys = split_polys(faces)
    if len(polys) <= cap:
        return polys
    cell = 0.004
    best = polys
    for _ in range(60):
        snap = lambda v: (round(v[0] / cell) * cell, round(v[1] / cell) * cell, round(v[2] / cell) * cell)
        d = split_polys([([snap(v) for v in f], c) for f, c in faces])
        # dedupe identical faces
        seen, dd = set(), []
        for f, c in d:
            k = tuple(sorted(f))
            if k in seen:
                continue
            seen.add(k)
            dd.append((f, c))
        best = dd
        if len(dd) <= cap:
            break
        cell *= 1.1
    if len(best) > cap:
        # last resort: keep the biggest faces
        def area(f):
            n = (0, 0, 0)
            for i in range(1, len(f) - 1):
                c = cross(sub(f[i], f[0]), sub(f[i + 1], f[0]))
                n = (n[0] + c[0], n[1] + c[1], n[2] + c[2])
            return math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
        best = sorted(best, key=lambda fc: -area(fc[0]))[:cap]
    return best


# --------------------------------------------------------------------------
# DS display list encoding
# --------------------------------------------------------------------------
CMD_BEGIN, CMD_DIFAMB, CMD_NORMAL, CMD_VTX16 = 0x40, 0x30, 0x21, 0x23


def rgb15(c):
    return (c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10)


def f412(x):
    v = int(round(x * 4096))
    return max(-32768, min(32767, v)) & 0xFFFF


def n10(x):
    v = int(round(x * 511))
    return max(-512, min(511, v)) & 0x3FF


def encode(polys):
    cmds = []
    for kind, n in ((0, 3), (1, 4)):
        sel = [p for p in polys if len(p[0]) == n]
        if not sel:
            continue
        sel.sort(key=lambda p: p[1])
        cmds.append((CMD_BEGIN, [kind]))
        last = None
        for f, c in sel:
            if c != last:
                dif = rgb15(c)
                amb = rgb15(tuple(int(x * 0.5) for x in c))
                cmds.append((CMD_DIFAMB, [dif | (1 << 15) | (amb << 16)]))
                last = c
            nn = face_normal(f)
            cmds.append((CMD_NORMAL, [n10(nn[0]) | (n10(nn[1]) << 10) | (n10(nn[2]) << 20)]))
            for v in f:
                cmds.append((CMD_VTX16, [f412(v[0]) | (f412(v[1]) << 16), f412(v[2])]))
    # pack four command ids per header word
    words = []
    for i in range(0, len(cmds), 4):
        grp = cmds[i:i + 4]
        hdr = 0
        for k, (cid, _) in enumerate(grp):
            hdr |= cid << (8 * k)
        words.append(hdr)
        for _, params in grp:
            words.extend(params)
    return [len(words)] + words


# --------------------------------------------------------------------------
# icons
# --------------------------------------------------------------------------
def soft_render(polys, size=ICON):
    """tiny painter's-algorithm isometric renderer, returns RGB rows"""
    yaw, pitch = math.radians(35), math.radians(30)
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    L = (-0.4, 0.8, 0.45)
    ll = math.sqrt(sum(x * x for x in L))
    L = tuple(x / ll for x in L)

    def proj(v):
        x = v[0] * cy - v[2] * sy
        z = v[0] * sy + v[2] * cy
        y = v[1]
        return (x, y * cp - z * sp, y * sp + z * cp)  # screen x, screen y(up), depth(toward viewer)
    tris = []
    for f, c in polys:
        n = face_normal(f)
        if n is None:
            continue
        lit = 0.55 + 0.45 * max(0, n[0] * L[0] + n[1] * L[1] + n[2] * L[2])
        col = tuple(min(255, int(x * lit)) for x in c)
        pf = [proj(v) for v in f]
        for i in range(1, len(pf) - 1):
            tri = (pf[0], pf[i], pf[i + 1])
            tris.append((sum(p[2] for p in tri) / 3, tri, col))
    if not tris:
        return None
    xs = [p[0] for _, t, _ in tris for p in t]
    ys = [p[1] for _, t, _ in tris for p in t]
    span = max(max(xs) - min(xs), max(ys) - min(ys)) or 1
    sc = (size - 4) / span
    ox = (size - (max(xs) - min(xs)) * sc) / 2 - min(xs) * sc
    oy = (size - (max(ys) - min(ys)) * sc) / 2 + max(ys) * sc
    img = [[None] * size for _ in range(size)]
    tris.sort(key=lambda t: t[0])
    for _, tri, col in tris:
        pts = [(p[0] * sc + ox, -p[1] * sc + oy) for p in tri]
        minx, maxx = max(0, int(min(p[0] for p in pts))), min(size - 1, int(max(p[0] for p in pts)) + 1)
        miny, maxy = max(0, int(min(p[1] for p in pts))), min(size - 1, int(max(p[1] for p in pts)) + 1)
        (x0, y0), (x1, y1), (x2, y2) = pts
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9:
            continue
        for yy in range(miny, maxy + 1):
            for xx in range(minx, maxx + 1):
                px, py = xx + .5, yy + .5
                a = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / den
                b = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / den
                if a >= 0 and b >= 0 and a + b <= 1:
                    img[yy][xx] = col
    return img


def preview_sheet(items, path, cell=112, cols=6):
    """contact sheet of every converted model (what the DS will draw), red mark = +z 'front'"""
    if Image is None:
        return
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell, rows * (cell + 12)), (40, 48, 56))
    from PIL import ImageDraw
    dr = ImageDraw.Draw(sheet)
    for i, (name, polys) in enumerate(items):
        bb = None
        xs = [v[0] for f, _ in polys for v in f] or [0]
        zs = [v[2] for f, _ in polys for v in f] or [0]
        zmax, xr = max(zs), (max(xs) - min(xs)) / 2 or 0.3
        mark = [([(-xr * .3, 0.001, zmax + .02), (xr * .3, 0.001, zmax + .02), (0, 0.001, zmax + .35)], (255, 0, 0))]
        img = soft_render(polys + mark, cell - 8)
        x0, y0 = (i % cols) * cell, (i // cols) * (cell + 12)
        if img:
            for y, row in enumerate(img):
                for x, px in enumerate(row):
                    if px is not None:
                        sheet.putpixel((x0 + 4 + x, y0 + 4 + y), px)
        dr.text((x0 + 3, y0 + cell - 2), f"{name} {len(polys)}p", fill=(255, 255, 255))
    sheet.save(path)


def icon_from_png(path):
    im = Image.open(path).convert("RGBA")
    bb = im.getbbox()
    if bb:
        im = im.crop(bb)
    w, h = im.size
    s = (ICON - 2) / max(w, h)
    im = im.resize((max(1, int(w * s)), max(1, int(h * s))), Image.LANCZOS)
    canvas = Image.new("RGBA", (ICON, ICON), (0, 0, 0, 0))
    canvas.paste(im, ((ICON - im.width) // 2, (ICON - im.height) // 2), im)
    rows = []
    for y in range(ICON):
        row = []
        for x in range(ICON):
            r, g, b, a = canvas.getpixel((x, y))
            row.append((r, g, b) if a > 110 else None)
        rows.append(row)
    return rows


def icon_words(img):
    out = []
    for row in img:
        for px in row:
            out.append(0 if px is None else (rgb15(px) | 0x8000))
    return out


# --------------------------------------------------------------------------
def main():
    kit = sys.argv[1] if len(sys.argv) > 1 else "assets/kit"
    root = sys.argv[2] if len(sys.argv) > 2 else "."
    objs, pngs = {}, []
    for dp, _, fs in os.walk(kit):
        for f in fs:
            lf = f.lower()
            if lf.endswith(".obj"):
                objs.setdefault(os.path.splitext(lf)[0], os.path.join(dp, f))
            elif lf.endswith(".png"):
                pngs.append(os.path.join(dp, f))
    print(f"[convert] kit: {len(objs)} OBJ files, {len(pngs)} PNG files")

    def find(cands):
        for c in cands:
            if c.lower() in objs:
                return c, objs[c.lower()]
        return None, None

    # tile size = the length of the plain wall piece
    tile = 1.0
    name, p = find(["wall"])
    if p:
        bx = load_obj(p).bbox()
        wall_len = max(bx[1] - bx[0], bx[5] - bx[4]) or 1.0
        # Kenney's wall panel is ~2 m wide; the game uses Sims-style 1 m tiles,
        # so a tile is half a wall panel and wall pieces get squeezed to 1 tile.
        tile = wall_len / 2.0
    print(f"[convert] tile size = {tile:.3f} kit units")

    out_models, icons, report, previews = [], [], [], []
    wall_h, wall_rgb = 1.3, (230, 225, 210)
    for key, cands, fb, cap, parts in MODELS:
        src, mesh = "fallback", None
        try:
            if parts:
                mesh = Mesh()
                ytop, used = 0.0, []
                for plist in parts:
                    n, pth = find(plist)
                    if not pth:
                        continue
                    m = load_obj(pth)
                    bx = m.bbox()
                    cx, cz = (bx[0] + bx[1]) / 2, (bx[4] + bx[5]) / 2
                    oy = ytop - bx[2]
                    m.transform(lambda v, cx=cx, cz=cz, oy=oy: (v[0] - cx, v[1] + oy, v[2] - cz))
                    ytop = ytop + (bx[3] - bx[2]) if not used else ytop
                    mesh.faces += m.faces
                    used.append(n)
                if used:
                    src = "+".join(used)
                    cands = [used[-1]] + [u for u in used[:-1]]
                else:
                    mesh = None
            else:
                n, pth = find(cands)
                if pth:
                    mesh = load_obj(pth)
                    src = n
                    if not mesh.faces:
                        mesh = None
        except Exception as e:  # never let one model break the build
            print(f"[convert] {key}: failed to load ({e}); using fallback")
            mesh = None
        if mesh is None:
            mesh = box_mesh(fb[0], fb[1], fb[2], fb[3])
            scale = 1.0
            src = "fallback"
        else:
            scale = 1.0 / tile
        # normalise: scale to tiles, centre x/z, sit on y=0
        bx = mesh.bbox()
        cx, cz, y0 = (bx[0] + bx[1]) / 2, (bx[4] + bx[5]) / 2, bx[2]
        mesh.transform(lambda v: ((v[0] - cx) * scale, (v[1] - y0) * scale, (v[2] - cz) * scale))
        bx = mesh.bbox()
        w, d, h = bx[1] - bx[0], bx[5] - bx[4], bx[3] - bx[2]
        if key.startswith("WALL") and d > w:  # walls must run along x
            mesh.transform(lambda v: (v[2], v[1], -v[0]))
            w, d = d, w
        if key.startswith("WALL") and src != "fallback" and w > 1.01:  # one wall piece per tile edge
            kx = 1.0 / w
            mesh.transform(lambda v: (v[0] * kx, v[1], v[2]))
            w = 1.0
        # keep vertices inside the DS's 4.12 range (+-8 tiles), shrink if huge
        big = max(w, d, h)
        if big > 7.5:
            k = 7.5 / big
            mesh.transform(lambda v: (v[0] * k, v[1] * k, v[2] * k))
            w, d, h = w * k, d * k, h * k
        polys = decimate(mesh.faces, cap)
        dl = encode(polys)
        fw = max(1, min(3, math.ceil(w - 0.3)))
        fd = max(1, min(3, math.ceil(d - 0.3)))
        if key == "WALL":
            wall_h = h
            cols = {}
            for f, c in polys:
                cols[c] = cols.get(c, 0) + 1
            if cols:
                wall_rgb = max(cols, key=cols.get)
        # icon
        img = None
        if Image is not None and src != "fallback":
            stem = cands[0].lower()
            want = [p for p in pngs if os.path.splitext(os.path.basename(p))[0].lower().startswith(stem)]
            exact = [p for p in want if re.fullmatch(re.escape(stem) + r"(_?(se|sw|ne|nw|s|n|e|w))?",
                                                      os.path.splitext(os.path.basename(p))[0].lower())]
            pick = exact or want
            pref = sorted(pick, key=lambda p: (0 if "iso" in p.lower() else 1,
                                                0 if p.lower().endswith(("_se.png", "_sw.png")) else 1, len(p)))
            if pref:
                try:
                    img = icon_from_png(pref[0])
                    src += "  [icon " + os.path.basename(pref[0]) + "]"
                except Exception:
                    img = None
        if img is None:
            img = soft_render(polys) or [[None] * ICON for _ in range(ICON)]
        out_models.append((key, dl, fw, fd, w, d, h, len(polys)))
        previews.append((key, polys))
        icons.append(icon_words(img))
        report.append(f"  {key:12s} <- {src:28s} {len(polys):4d} polys  {fw}x{fd} tiles  h={h:.2f}")

    try:
        preview_sheet(previews, os.path.join(root, "model_preview.png"))
        print("[convert] wrote model_preview.png")
    except Exception as e:
        print(f"[convert] preview failed: {e}")
    print("[convert] models:")
    print("\n".join(report))

    # ---- write header
    os.makedirs(os.path.join(root, "include"), exist_ok=True)
    os.makedirs(os.path.join(root, "source"), exist_ok=True)
    with open(os.path.join(root, "include", "gen_assets.h"), "w") as fh:
        fh.write("// generated by tools/convert.py - do not edit\n#pragma once\n#include <nds.h>\n\n")
        fh.write("enum {\n")
        for m in out_models:
            fh.write(f"    MDL_{m[0]},\n")
        fh.write("    MDL_COUNT\n};\n\n")
        fh.write("typedef struct {\n    const u32* list;   // packed GX display list, [0] = word count\n"
                 "    u8 tilesW, tilesD;  // footprint\n    s32 w, d, h;        // exact size, f32 (20.12)\n"
                 "    u16 polys;\n    u8 fromKit;\n} ModelInfo;\n\n")
        fh.write("extern const ModelInfo g_models[MDL_COUNT];\n")
        fh.write("extern const u16 g_icons[MDL_COUNT][32 * 32];\n")
        fh.write("extern const s32 g_wallHeight;   // f32\nextern const u16 g_wallColor;\n")
        fh.write("extern const int g_assetsFromKit; // how many models came from the kit\n")
    with open(os.path.join(root, "source", "gen_assets.c"), "w") as fc:
        fc.write("// generated by tools/convert.py - do not edit\n#include \"gen_assets.h\"\n\n")
        kitcount = 0
        for i, (key, dl, *_rest) in enumerate(out_models):
            fc.write(f"static const u32 dl_{key.lower()}[{len(dl)}] __attribute__((aligned(4))) = {{\n")
            for j in range(0, len(dl), 8):
                fc.write("    " + ", ".join(f"0x{x & 0xFFFFFFFF:08X}" for x in dl[j:j + 8]) + ",\n")
            fc.write("};\n")
        fc.write("\nconst ModelInfo g_models[MDL_COUNT] = {\n")
        for (key, dl, fw, fd, w, d, h, np), r in zip(out_models, report):
            fromkit = 0 if "fallback" in r else 1
            kitcount += fromkit
            fc.write(f"    {{ dl_{key.lower()}, {fw}, {fd}, {int(w * 4096)}, {int(d * 4096)}, {int(h * 4096)}, {np}, {fromkit} }},\n")
        fc.write("};\n\nconst u16 g_icons[MDL_COUNT][32 * 32] = {\n")
        for ic in icons:
            fc.write("  {\n")
            for j in range(0, len(ic), 16):
                fc.write("    " + ",".join(f"0x{x:04X}" for x in ic[j:j + 16]) + ",\n")
            fc.write("  },\n")
        fc.write("};\n\n")
        fc.write(f"const s32 g_wallHeight = {int(wall_h * 4096)};\n")
        fc.write(f"const u16 g_wallColor = 0x{rgb15(wall_rgb) | 0x8000:04X};\n")
        fc.write(f"const int g_assetsFromKit = {kitcount};\n")
    print(f"[convert] wrote gen_assets.c/.h ({kitcount}/{len(out_models)} models from the kit)")


if __name__ == "__main__":
    main()
