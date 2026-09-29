"""Star Flux's ship as a small 3D model, plus a tiny software rasteriser.

Shared by tools/star_ship_sprite.py (the in-game sprite frames) and
tools/star_title_screen.py (the title screen), so the ship on the title is
the ship you fly. Needs numpy.

Model space: x right, y up, z forward (the nose points +z). One unit is
roughly a metre; the ship is ~5 long and ~5.6 across its fins.

The design, seen from behind (which is how you mostly see it): a white
fuselage with a gold canopy, wings sloping down to two engine pods, and a
red fin rising outward from each pod - an "M" silhouette, where Tube
Flux's ship is a flat delta.
"""
import math

import numpy as np

# ---------------------------------------------------------------- colours
IVORY   = (236, 232, 220)
SILVER  = (170, 176, 192)
STEEL   = (92, 98, 116)
DARK    = (46, 50, 64)
CRIMSON = (214, 36, 48)
EMBER   = (150, 20, 34)
GOLD    = (255, 196, 64)
AMBER   = (190, 120, 30)
GLOW    = (140, 220, 255)   # engines: blue-white (Tube's are orange)
CORE    = (240, 252, 255)


def _mirror(p):
    return (-p[0], p[1], p[2])


def ship_triangles():
    """[(p0, p1, p2, colour, emissive)], in model space."""
    t = []

    def tri(a, b, c, col, em=False):
        t.append((a, b, c, col, em))

    def quad(a, b, c, d, col, em=False):
        tri(a, b, c, col, em)
        tri(a, c, d, col, em)

    def both(fn, *pts, col, em=False):
        """Adds a part and its mirror image."""
        fn(*pts, col, em)
        fn(*[_mirror(p) for p in pts], col, em)

    # --- fuselage: a hexagonal section, tapering to the nose ---------------
    nose = (0.0, 0.02, 3.3)
    s1 = [(0.0, 0.42, 1.0), (0.44, 0.20, 1.0), (0.36, -0.20, 1.0),
          (0.0, -0.32, 1.0), (-0.36, -0.20, 1.0), (-0.44, 0.20, 1.0)]
    s2 = [(0.0, 0.40, -1.3), (0.50, 0.18, -1.3), (0.42, -0.20, -1.3),
          (0.0, -0.30, -1.3), (-0.42, -0.20, -1.3), (-0.50, 0.18, -1.3)]
    for i in range(6):
        j = (i + 1) % 6
        col = IVORY if (i in (0, 5)) else SILVER if i == 1 or i == 4 else STEEL
        tri(nose, s1[j], s1[i], col)
        quad(s1[i], s1[j], s2[j], s2[i], col)
    # A red stripe down each flank.
    both(quad, (0.45, 0.10, 0.6), (0.49, 0.10, -1.2), (0.47, 0.0, -1.2), (0.42, 0.0, 0.6), col=CRIMSON)
    # Rear: dark plate, glowing engine.
    for i in range(6):
        j = (i + 1) % 6
        tri((0.0, 0.05, -1.3), s2[i], s2[j], DARK)
    eng = [(0.32 * math.cos(a), 0.05 + 0.26 * math.sin(a), -1.33) for a in
           [k * math.pi / 4 for k in range(8)]]
    for i in range(8):
        tri((0.0, 0.05, -1.34), eng[i], eng[(i + 1) % 8], GLOW, True)
    core = [(0.1 * math.cos(a), 0.05 + 0.09 * math.sin(a), -1.35) for a in
            [k * math.pi / 3 for k in range(6)]]
    for i in range(6):
        tri((0.0, 0.05, -1.36), core[i], core[(i + 1) % 6], CORE, True)

    # --- canopy -----------------------------------------------------------
    apex = (0.0, 0.66, 0.55)
    cf, cb = (0.0, 0.34, 1.95), (0.0, 0.44, -0.45)
    cl, cr = (-0.26, 0.36, 0.7), (0.26, 0.36, 0.7)
    tri(cf, cr, apex, GOLD)
    tri(cf, apex, cl, GOLD)
    tri(apex, cr, cb, AMBER)
    tri(apex, cb, cl, AMBER)

    # --- wings: root on the fuselage, sloping down to the pods -------------
    r0, r1 = (0.42, 0.02, 1.5), (0.46, 0.02, -1.2)
    k0, k1 = (2.25, -0.38, 0.35), (2.25, -0.38, -1.15)
    both(quad, r0, k0, k1, r1, col=IVORY)                 # top
    both(quad, (0.40, -0.12, 1.5), (0.40, -0.12, -1.2),
         (2.25, -0.50, -1.15), (2.25, -0.50, 0.35), col=DARK)   # underside
    # Red wing band near the tip.
    both(quad, (1.45, -0.20, 0.87), (1.85, -0.28, 0.6), (1.85, -0.28, -1.14),
         (1.45, -0.20, -1.14), col=CRIMSON)
    # Trailing edge, the bit you see from behind.
    both(quad, r1, k1, (2.25, -0.50, -1.15), (0.40, -0.12, -1.2), col=STEEL)

    # --- engine pods ------------------------------------------------------
    def pod(cx, cy):
        rs = [(cx + 0.24 * math.cos(a), cy + 0.22 * math.sin(a)) for a in
              [k * math.pi / 3 + math.pi / 6 for k in range(6)]]
        front, back = 0.55, -1.35
        tip = (cx, cy, front + 0.55)
        for i in range(6):
            j = (i + 1) % 6
            a, b = rs[i], rs[j]
            col = SILVER if (a[1] + b[1]) / 2 > cy else STEEL
            quad((a[0], a[1], front), (b[0], b[1], front), (b[0], b[1], back), (a[0], a[1], back), col)
            tri(tip, (b[0], b[1], front), (a[0], a[1], front), col)
            tri((cx, cy, back), (a[0], a[1], back), (b[0], b[1], back), DARK)
        for i in range(6):
            a, b = rs[i], rs[(i + 1) % 6]
            tri((cx, cy, back - 0.03), (cx + (a[0] - cx) * 0.9, cy + (a[1] - cy) * 0.9, back - 0.02),
                (cx + (b[0] - cx) * 0.9, cy + (b[1] - cy) * 0.9, back - 0.02), GLOW, True)
    pod(2.3, -0.42)
    pod(-2.3, -0.42)

    # --- fins: rising outward from the pods --------------------------------
    f0, f1 = (2.32, -0.22, 0.45), (2.32, -0.22, -1.25)
    f2, f3 = (2.85, 1.15, -1.3), (2.80, 1.15, -0.35)
    both(quad, f0, f1, f2, f3, col=CRIMSON)
    # A white tip on each fin.
    both(quad, (2.74, 0.85, -0.45), (2.76, 0.85, -1.29), f2, f3, col=IVORY)
    return t


# ---------------------------------------------------------------- transforms
def rot_z(deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def rot_y(deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_x(deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def transform(tris, m, offset=(0, 0, 0), scale=1.0):
    off = np.array(offset, float)
    out = []
    for a, b, c, col, em in tris:
        out.append(tuple(m @ (np.array(p, float) * scale) + off for p in (a, b, c)) + (col, em))
    return out


# ---------------------------------------------------------------- rasteriser
class Camera:
    def __init__(self, pos, target, width, height, fov_deg, up=(0, 1, 0), roll_deg=0.0):
        self.pos = np.array(pos, float)
        f = np.array(target, float) - self.pos
        f /= np.linalg.norm(f)
        r = np.cross(np.array(up, float), f)
        r /= np.linalg.norm(r)
        u = np.cross(f, r)
        if roll_deg:
            a = math.radians(roll_deg)
            r, u = r * math.cos(a) + u * math.sin(a), -r * math.sin(a) + u * math.cos(a)
        self.r, self.u, self.f = r, u, f
        self.w, self.h = width, height
        self.focal = (width / 2) / math.tan(math.radians(fov_deg) / 2)

    def view(self, p):
        v = np.array(p, float) - self.pos
        return np.array([v @ self.r, v @ self.u, v @ self.f])

    def project(self, p):
        v = self.view(p)
        z = max(v[2], 1e-3)
        return self.w / 2 + v[0] * self.focal / z, self.h / 2 - v[1] * self.focal / z, z


def shade(col, normal, light, ambient=0.38, emissive=False):
    if emissive:
        return np.array(col, float)
    k = ambient + (1 - ambient) * max(0.0, float(normal @ light))
    return np.array(col, float) * k


def rasterise(tris, cam, rgb, depth, light=(-0.4, 0.8, -0.45), ambient=0.38, cover=None, ident=None, tag=1):
    """Draws `tris` into rgb (H x W x 3 float) with a z-buffer. Faces are
    two-sided: lit from whichever side faces the camera. `cover`, if given,
    gets `tag` wherever these triangles win the depth test."""
    L = np.array(light, float)
    L /= np.linalg.norm(L)
    H, W = depth.shape
    for a, b, c, col, em in tris:
        pa, pb, pc = cam.project(a), cam.project(b), cam.project(c)
        if min(pa[2], pb[2], pc[2]) < 0.05:
            continue
        n = np.cross(np.array(b, float) - np.array(a, float), np.array(c, float) - np.array(a, float))
        nl = np.linalg.norm(n)
        if nl == 0:
            continue
        n /= nl
        if n @ (cam.pos - np.array(a, float)) < 0:
            n = -n
        colour = shade(col, n, L, ambient, em)
        xs = np.array([pa[0], pb[0], pc[0]])
        ys = np.array([pa[1], pb[1], pc[1]])
        x0, x1 = max(int(math.floor(xs.min())), 0), min(int(math.ceil(xs.max())), W - 1)
        y0, y1 = max(int(math.floor(ys.min())), 0), min(int(math.ceil(ys.max())), H - 1)
        if x0 > x1 or y0 > y1:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (pb[1] - pc[1]) * (pa[0] - pc[0]) + (pc[0] - pb[0]) * (pa[1] - pc[1])
        if abs(d) < 1e-9:
            continue
        w0 = ((pb[1] - pc[1]) * (gx - pc[0]) + (pc[0] - pb[0]) * (gy - pc[1])) / d
        w1 = ((pc[1] - pa[1]) * (gx - pc[0]) + (pa[0] - pc[0]) * (gy - pc[1])) / d
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        invz = w0 / pa[2] + w1 / pb[2] + w2 / pc[2]
        z = 1.0 / np.maximum(invz, 1e-9)
        sub = depth[y0:y1 + 1, x0:x1 + 1]
        win = inside & (z < sub)
        sub[win] = z[win]
        rgb[y0:y1 + 1, x0:x1 + 1][win] = colour
        if cover is not None:
            cover[y0:y1 + 1, x0:x1 + 1][win] = tag


# ---------------------------------------------------------------- enemies
# The in-game meshes (src/games/StarFlux/StarFluxScene.cpp) follow these.
VENOM   = (178, 26, 58)
MAROON  = (96, 14, 40)
PLUM    = (80, 60, 110)
MAGENTA = (255, 60, 190)
LIME    = (190, 255, 80)


def fighter_triangles():
    """The enemy fighter: a red dart with four forward-swept blades in an
    X, so it has a shape from behind as well as from the side. Nose +z."""
    t = []

    def tri(a, b, c, col, em=False):
        t.append((a, b, c, col, em))

    nose = (0, 0, 1.7)
    top, rgt, bot, lft = (0, 0.45, -0.9), (0.45, 0, -0.9), (0, -0.45, -0.9), (-0.45, 0, -0.9)
    tri(nose, rgt, top, VENOM)
    tri(nose, top, lft, VENOM)
    tri(nose, bot, rgt, MAROON)
    tri(nose, lft, bot, MAROON)
    tri(top, rgt, bot, LIME, True)        # the engine: the whole tail
    tri(top, bot, lft, LIME, True)
    for sx in (-1, 1):
        for sy in (-1, 1):
            tip = (sx * 1.6, sy * 1.1, 0.4)
            tri((sx * 0.25, sy * 0.25, -0.2), tip, (sx * 0.25, sy * 0.25, -0.85), PLUM)
            tri((sx * 1.25, sy * 0.86, 0.3), tip, (sx * 1.3, sy * 0.9, 0.05), MAGENTA, True)
    return t


def rock_triangles(seed, radius=1.0, jitter=0.28):
    """An icosahedron with its corners pushed in and out: an asteroid."""
    import random as _r
    rnd = _r.Random(seed)
    p = (1 + 5 ** 0.5) / 2
    v = [(-1, p, 0), (1, p, 0), (-1, -p, 0), (1, -p, 0), (0, -1, p), (0, 1, p),
         (0, -1, -p), (0, 1, -p), (p, 0, -1), (p, 0, 1), (-p, 0, -1), (-p, 0, 1)]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4),
         (11, 10, 2), (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8),
         (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    n = (1 + p * p) ** 0.5
    pts = []
    for x, y, z in v:
        k = radius * (1 + rnd.uniform(-jitter, jitter)) / n
        pts.append((x * k, y * k * 0.85, z * k))
    rock = (150, 128, 110)
    return [(pts[a], pts[b], pts[c], rock, False) for a, b, c in f]
