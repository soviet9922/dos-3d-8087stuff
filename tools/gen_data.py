#!/usr/bin/env python3
"""Generate data.inc for the 8088/8087 flight demo.

Builds, at assembly time:
  * a 320-entry float sine table (sin + quarter-wave overlap for cos)
  * CGA 2bpp ordered-dither patterns for the shading ramps
  * the 3D models: integer vertices, convex faces, precomputed float
    plane normals (nx, ny, nz, d) for back-face culling and lighting
"""
import itertools
import math
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "src", "data.inc")

# ---------------------------------------------------------------- dithering
# CGA mode 4: 4 pixels per byte, pixel 0 in bits 7-6.
# 2x2 Bayer thresholds, indexed [y&1][x&1].
BAYER = [[0, 2], [3, 1]]


def dither(a, b, k):
    """Blend colour a -> b, k/4 of the pixels become b.
    Returns a word: low byte = even-row byte, high byte = odd-row byte."""
    rows = []
    for y in range(2):
        byte = 0
        for x in range(4):
            c = b if BAYER[y][x & 1] < k else a
            byte |= c << (6 - 2 * x)
        rows.append(byte)
    return rows[0] | (rows[1] << 8)


RAMP_LEVELS = 7
# colours: 0 = background (sky blue), 1 = cyan, 2 = magenta, 3 = white
RAMPS = {
    "CYAN":    [(0, 1, 2), (0, 1, 3), (1, 1, 0), (1, 3, 1), (1, 3, 2), (1, 3, 3), (3, 3, 0)],
    "MAGENTA": [(0, 2, 2), (0, 2, 3), (2, 2, 0), (2, 3, 1), (2, 3, 2), (2, 3, 3), (3, 3, 0)],
    "MIXED":   [(0, 2, 3), (2, 1, 1), (2, 1, 2), (2, 1, 3), (1, 1, 0), (1, 3, 2), (3, 3, 0)],
    "DARK":    [(0, 1, 1), (0, 1, 2), (0, 2, 2), (0, 1, 3), (1, 2, 2), (1, 1, 0), (1, 3, 1)],
    "FLASH":   [(1, 3, 2), (1, 3, 3), (3, 3, 0), (3, 3, 0), (3, 3, 0), (3, 3, 0), (3, 3, 0)],
}
RAMP_ORDER = ["CYAN", "MAGENTA", "MIXED", "DARK", "FLASH"]
RAMP_ID = {n: i for i, n in enumerate(RAMP_ORDER)}

# background patterns
BG = {
    "SKY":     (0, 0, 0),
    "HAZE1":   (0, 3, 1),
    "HAZE2":   (0, 1, 1),
    "GROUND_A": (0, 1, 1),
    "GROUND_B": (0, 1, 3),
    "FOG":     (0, 1, 2),
}

# ---------------------------------------------------------------- geometry


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    l = math.sqrt(dot(a, a))
    return (a[0] / l, a[1] / l, a[2] / l)


def order_ccw(pts_idx, pts, n):
    """Order coplanar points around their centroid (counter-clockwise seen
    from the side the normal points to)."""
    c = [sum(pts[i][k] for i in pts_idx) / len(pts_idx) for k in range(3)]
    # basis in the plane
    ref = sub(pts[pts_idx[0]], c)
    u = norm(ref)
    v = cross(n, u)
    return sorted(pts_idx, key=lambda i: math.atan2(dot(sub(pts[i], c), v),
                                                    dot(sub(pts[i], c), u)))


def convex_hull_faces(idx, pts, eps=1e-6):
    """Brute-force hull for tiny point sets: returns list of (indices, normal)."""
    planes = []
    for i, j, k in itertools.combinations(idx, 3):
        n = cross(sub(pts[j], pts[i]), sub(pts[k], pts[i]))
        if dot(n, n) < eps:
            continue
        n = norm(n)
        side = [dot(n, sub(pts[m], pts[i])) for m in idx]
        if all(s <= 1e-4 for s in side):
            pass
        elif all(s >= -1e-4 for s in side):
            n = (-n[0], -n[1], -n[2])
        else:
            continue
        d = dot(n, pts[i])
        if any(abs(dot(n, pn) - 1) < 1e-5 and abs(d - pd) < 1e-3 for pn, pd in planes):
            continue
        planes.append((n, d))
    faces = []
    for n, d in planes:
        on = [m for m in idx if abs(dot(n, pts[m]) - d) < 1e-3]
        faces.append((order_ccw(on, pts, n), n))
    return faces


class Model:
    def __init__(self, name, ground=False, sort=False):
        self.name = name
        self.pts = []
        self.faces = []  # (indices, normal, ramp)
        self.ground = ground
        self.sort = sort

    def add_pts(self, pts):
        base = len(self.pts)
        self.pts.extend(pts)
        return list(range(base, base + len(pts)))

    def hull(self, pts, ramp):
        idx = self.add_pts(pts)
        for f, n in convex_hull_faces(idx, self.pts):
            self.faces.append((f, n, ramp))

    def flat(self, pts, ramp, ramp_back=None):
        """Thin double-sided polygon (wings, fins)."""
        idx = self.add_pts(pts)
        n = norm(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])))
        self.faces.append((idx, n, ramp))
        self.faces.append((idx[::-1], (-n[0], -n[1], -n[2]), ramp_back or ramp))


def box(hx, hy, hz, y0=None):
    ys = (-hy, hy) if y0 is None else (y0, y0 + 2 * hy)
    return [(x, y, z) for x in (-hx, hx) for y in ys for z in (-hz, hz)]


def build_models():
    models = []

    # ---- player ship: an original delta-wing design, nose toward +z
    s = Model("SHIP", sort=True)
    s.hull([(0, 1, 26), (0, 6, 1), (-6, 0, -10), (6, 0, -10),
            (0, -4, -5), (0, 4, -11)], "CYAN")
    s.hull([(0, 6, 1), (-2, 4, 6), (2, 4, 6), (0, 2, 12), (0, 4, -4)], "MAGENTA")  # canopy
    s.hull([(-4, 1, 7), (-5, 1, -9), (-27, -5, -14), (-5, -2, -5)], "CYAN")    # wings
    s.hull([(4, 1, 7), (5, 1, -9), (27, -5, -14), (5, -2, -5)], "CYAN")
    s.flat([(-3, 3, -6), (-4, 3, -11), (-9, 11, -15)], "MAGENTA")   # twin fins
    s.flat([(3, 3, -6), (9, 11, -15), (4, 3, -11)], "MAGENTA")
    models.append(s)

    m = Model("CUBE"); m.hull(box(16, 16, 16), None); models.append(m)
    m = Model("PILLAR", ground=True); m.hull(box(13, 75, 13, y0=0), None); models.append(m)
    m = Model("PYRAMID", ground=True)
    m.hull([(-38, 0, -38), (38, 0, -38), (-38, 0, 38), (38, 0, 38), (0, 85, 0)], None)
    models.append(m)
    m = Model("OCTA")
    m.hull([(28, 0, 0), (-28, 0, 0), (0, 28, 0), (0, -28, 0), (0, 0, 28), (0, 0, -28)], None)
    models.append(m)
    m = Model("DIAMOND")
    m.hull([(16, 0, 0), (-16, 0, 0), (0, 0, 16), (0, 0, -16), (0, 40, 0), (0, -40, 0),
            (11, 8, 11), (-11, 8, 11), (11, 8, -11), (-11, 8, -11)], None)
    models.append(m)
    m = Model("HEXCOL", ground=True)
    m.hull([(22 * math.cos(a), y, 22 * math.sin(a))
            for a in [i * math.pi / 3 for i in range(6)] for y in (0, 100)], None)
    models.append(m)
    m = Model("TETRA")
    m.hull([(30, 30, 30), (-30, -30, 30), (-30, 30, -30), (30, -30, -30)], None)
    models.append(m)
    m = Model("ARCHTOP", ground=True)  # floating slab on two legs = 3 hulls -> sort
    m.sort = True
    m.hull([(x - 40, y, z) for x, y, z in box(9, 45, 9, y0=0)], None)
    m.hull([(x + 40, y, z) for x, y, z in box(9, 45, 9, y0=0)], None)
    m.hull(box(58, 9, 12, y0=90), None)
    models.append(m)

    b = Model("BOLT")
    b.hull([(3, 0, 0), (-3, 0, 0), (0, 3, 0), (0, -3, 0), (0, 0, 16), (0, 0, -16)], "FLASH")
    models.append(b)
    return models


def round_pts(m):
    m.pts = [tuple(int(round(c)) for c in p) for p in m.pts]


# ---------------------------------------------------------------- emit


def f(x):
    """Float literal for NASM.  Must always contain a '.' or exponent:
    'dd 1' would assemble the *integer* 1, not 1.0."""
    if abs(x) < 1e-9:
        return "0.0"
    s = "%.7g" % x
    if "." not in s and "e" not in s:
        s += ".0"
    return s


def main():
    L = []
    L.append("; generated by tools/gen_data.py -- do not edit")
    L.append("")
    L.append("sintab:  ; 320 floats, cos(a) = sintab[a+64]")
    for i in range(0, 320, 8):
        L.append("  dd " + ", ".join(f(math.sin(2 * math.pi * (i + k) / 256)) for k in range(8)))
    L.append("")
    L.append("RAMP_LEVELS equ %d" % RAMP_LEVELS)
    for n in RAMP_ORDER:
        L.append("RAMP_%s equ %d" % (n, RAMP_ID[n]))
    L.append("ramp_tab:")
    for n in RAMP_ORDER:
        L.append("  dw " + ", ".join("%05Xh" % dither(*t) for t in RAMPS[n]) + "  ; " + n)
    L.append("")
    for n, t in BG.items():
        L.append("PAT_%s equ %05Xh" % (n, dither(*t)))
    L.append("")

    models = build_models()
    maxv = 0
    for m in models:
        round_pts(m)
        maxv = max(maxv, len(m.pts))
        xs = [p[0] for p in m.pts]; ys = [p[1] for p in m.pts]; zs = [p[2] for p in m.pts]
        cy = (min(ys) + max(ys)) / 2
        hx = max(abs(x) for x in xs); hz = max(abs(z) for z in zs); hy = (max(ys) - min(ys)) / 2
        rad = max(math.sqrt(p[0] ** 2 + (p[1] - cy) ** 2 + p[2] ** 2) for p in m.pts)
        flags = (1 if m.ground else 0) | (2 if m.sort else 0)
        L.append("model_%s:" % m.name.lower())
        L.append("  dw %d, %d, %d  ; nverts nfaces flags" % (len(m.pts), len(m.faces), flags))
        # radius is around the centre (y offset cy)
        L.append("  dw %d, %d, %d, %d, %d  ; radius hx hy hz cyoff" %
                 (math.ceil(rad) + 1, hx, math.ceil(hy), hz, round(cy)))
        L.append("  dw .faces")
        for p in m.pts:
            L.append("  dw %d, %d, %d" % p)
        L.append(".faces:")
        for idx, n, ramp in m.faces:
            assert 3 <= len(idx) <= 6, (m.name, idx)
            # recompute the normal from the rounded points for exactness
            p0, p1, p2 = (m.pts[i] for i in idx[:3])
            nn = cross(sub(p1, p0), sub(p2, p0))
            if dot(nn, nn) > 0 and dot(nn, n) > 0:
                n = norm(nn)
            d = dot(n, m.pts[idx[0]])
            pad = list(idx) + [0] * (6 - len(idx))
            r = 255 if ramp is None else RAMP_ID[ramp]
            L.append("  db %d, %d, %s" % (len(idx), r, ", ".join(str(i) for i in pad)))
            L.append("  dd %s, %s, %s, %s" % (f(n[0]), f(n[1]), f(n[2]), f(d)))
        L.append("")
    L.append("MAXV equ %d" % maxv)
    L.append("FACE_SIZE equ 24")
    with open(OUT, "w") as fh:
        fh.write("\n".join(L) + "\n")
    print("wrote", OUT, "maxv", maxv, {m.name: (len(m.pts), len(m.faces)) for m in models})


if __name__ == "__main__":
    main()
