#!/usr/bin/env python3
"""Renders Tube Flux's title screen and writes it as an RGB565 header.

    python3 tools/tube_title_screen.py            # write the header
    python3 tools/tube_title_screen.py --preview  # also tube_title_preview.png (4x)

Needs numpy and Pillow (pip install numpy pillow). Takes a few seconds.

The scene is real 3D geometry rendered here, offline: an octagonal tunnel
with neon seams, gunmetal blocks, the ship (in the in-game sprite's
colours) banking and firing twin lasers at a crystal target exploding on
the right wall. It's drawn at 8x resolution with a glow layer that gets
bloomed, then downsampled to 160x128 and Floyd-Steinberg dithered to
RGB565, so gradients survive the display's 16-bit colour.

The title uses DejaVu Sans Bold, which most Linux systems have; point FONT
at another bold TTF if yours doesn't. The bottom strip (from STRIP_Y) is
left black for the text the game draws at runtime.

Output: src/games/TubeFlux/assets/TubeTitleScreen.h, 160x128, 40KB in flash
(the same size and format as Tank Flux's title screen).
"""
import math
import os
import random
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

random.seed(7)
W, H = 160, 128
SS = 8
SW, SH = W * SS, H * SS
F = (W / 2) / math.tan(math.radians(40)) * SS      # same FOV as the game
CX, CY = SW / 2, 57 * SS                            # vanishing point just under the title

# ---------------------------------------------------------------- camera
CAM = np.array([-20.0, 40.0, 0.0])                  # a little left of and above the axis
ROLL = math.radians(-7)                             # a touch of bank for drama
def to_cam(p):
    v = np.array(p, dtype=float) - CAM
    c, s = math.cos(ROLL), math.sin(ROLL)
    return np.array([v[0]*c - v[1]*s, v[0]*s + v[1]*c, v[2]])
def proj(p):
    v = to_cam(p)
    z = max(v[2], 1.0)
    return (CX + v[0]*F/z, CY - v[1]*F/z), z

# ---------------------------------------------------------------- layers
base = Image.new('RGB', (SW, SH), (0, 0, 0))
glow = Image.new('RGB', (SW, SH), (0, 0, 0))       # additive, bloomed
bd = ImageDraw.Draw(base)
gd = ImageDraw.Draw(glow)

def clamp(c): return tuple(int(max(0, min(255, v))) for v in c)
def mix(a, b, t): return tuple(a[i] + (b[i]-a[i])*t for i in range(3))
def scale(c, k): return tuple(v*k for v in c)

# ---------------------------------------------------------------- lights
END_GLOW = (120, 220, 255)                          # light at the end of the tunnel
EXPLO = np.array([270.0, 60.0, 1150.0])             # crystal being destroyed, right wall
EXPLO_COL = (255, 150, 50)

R = 400.0
SIDES = 8
SPACING = 420.0
def corner(j, z, r=R):
    a = math.radians((j - 0.5) * 45)
    return (r*math.sin(a), -r*math.cos(a), z)

def panel_light(centre, normal, base_col):
    n = np.array(normal); n /= np.linalg.norm(n)
    c = np.array(centre)
    amb = 0.28
    # far-end light: comes from +z
    L = np.array([0, 0, 1.0]); far = max(0, -np.dot(n, -L)) * 0.0
    # explosion point light
    d = EXPLO - c; dist = np.linalg.norm(d); d /= dist
    ex = max(0, np.dot(n, d)) * 1.6 / (1 + (dist/420)**2)
    # headlight from the ship/camera
    h = CAM - c; hd = np.linalg.norm(h); h /= hd
    hl = max(0, np.dot(n, h)) * 0.9 / (1 + (hd/900)**2)
    col = scale(base_col, amb + hl)
    col = tuple(col[i] + EXPLO_COL[i]*ex*0.30 for i in range(3))
    return col

def fog(col, z):
    # far walls fade into the glow at the end, not into black
    t = min(1, max(0, (z - 1600) / 4400))
    return mix(col, (20, 30, 70), t**1.2)

# ---------------------------------------------------------------- tunnel
VIOLET_A = (120, 40, 175)
VIOLET_B = (55, 20, 100)
rings = 16
z0 = 60.0
panels = []
for k in range(rings):
    za, zb = z0 + k*SPACING - (SPACING*0.35 if k else 0), z0 + (k+1)*SPACING - SPACING*0.35
    if k == 0: za = 40.0
    for j in range(SIDES):
        p = [corner(j, za), corner(j+1, za), corner(j+1, zb), corner(j, zb)]
        cen = np.mean(np.array(p), axis=0)
        normal = -np.array([cen[0], cen[1], 0])
        colb = VIOLET_A if (k + j) % 2 == 0 else VIOLET_B
        panels.append((cen[2], p, colb, normal))
# far cap: the glow
cap_z = z0 + rings*SPACING
cap = [proj(corner(j, cap_z))[0] for j in range(SIDES)]

for zc, p, colb, normal in sorted(panels, key=lambda t: -t[0]):
    col = fog(panel_light(np.mean(np.array(p), axis=0), normal, colb), zc)
    bd.polygon([proj(q)[0] for q in p], fill=clamp(col))
bd.polygon(cap, fill=clamp(scale(END_GLOW, 0.9)))
# end glow bloom source
ex, ey = np.mean(np.array(cap), axis=0)
for r, a in [(150, 0.18), (80, 0.45), (36, 0.9)]:
    gd.ellipse([ex-r, ey-r, ex+r, ey+r], fill=clamp(scale((170, 235, 255), a)))

# neon seams: along each corner, and every other ring
for j in range(SIDES):
    pts = [proj(corner(j, z0 + k*SPACING))[0] for k in range(0, rings)]
    for k in range(len(pts)-1):
        zc = z0 + k*SPACING
        t = max(0.15, 1 - zc/6500)
        c = clamp(scale((255, 60, 220), t))
        bd.line([pts[k], pts[k+1]], fill=c, width=max(2, int(10*t)))
        gd.line([pts[k], pts[k+1]], fill=clamp(scale((255, 60, 220), t*0.6)), width=max(4, int(18*t)))
for k in range(1, rings, 2):
    zc = z0 + k*SPACING
    t = max(0.1, 1 - zc/6500)
    ring = [proj(corner(j, zc))[0] for j in range(SIDES)]
    bd.line(ring + [ring[0]], fill=clamp(scale((80, 230, 255), t*0.9)), width=max(1, int(6*t)))
    gd.line(ring + [ring[0]], fill=clamp(scale((80, 230, 255), t*0.35)), width=max(3, int(12*t)))

# ---------------------------------------------------------------- blocks on the walls
GUN = (70, 82, 104)
def block(lane, lanes, z, depth=150, h=150):
    apothem = R*math.cos(math.radians(22.5))
    ri = R*(1 - h/apothem)
    zf, zb = z - depth/2, z + depth/2
    faces = []
    for p in range(lane, lane+lanes):
        of0, of1 = corner(p, zf, R*0.99), corner(p+1, zf, R*0.99)
        if0, if1 = corner(p, zf, ri), corner(p+1, zf, ri)
        ib0, ib1 = corner(p, zb, ri), corner(p+1, zb, ri)
        faces.append(([of0, of1, if1, if0], 'front'))
        faces.append(([if0, if1, ib1, ib0], 'top'))
    for c in (lane, lane+lanes):
        faces.append(([corner(c, zf, R*0.99), corner(c, zf, ri), corner(c, zb, ri), corner(c, zb, R*0.99)], 'side'))
    out = []
    for q, kind in faces:
        cen = np.mean(np.array(q), axis=0)
        n = np.cross(np.array(q[1])-np.array(q[0]), np.array(q[2])-np.array(q[0]))
        if kind == 'front': n = np.array([0, 0, -1.0])
        elif kind == 'top': n = -np.array([cen[0], cen[1], 0])
        k = {'front': 1.0, 'top': 1.5, 'side': 0.55}[kind]
        col = fog(panel_light(cen, n, scale(GUN, k*1.4)), cen[2])
        out.append((to_cam(cen)[2], [proj(v)[0] for v in q], col, kind))
    return out

def octahedron(c, r, col, lit=1.0):
    c = np.array(c)
    v = [c+[0, r*1.4, 0], c+[0, -r*1.4, 0], c+[r, 0, 0], c+[-r, 0, 0], c+[0, 0, r], c+[0, 0, -r]]
    tris = [(0,2,5),(0,5,3),(0,3,4),(0,4,2),(1,5,2),(1,3,5),(1,4,3),(1,2,4)]
    out = []
    for a, b, d in tris:
        q = [v[a], v[b], v[d]]
        cen = np.mean(np.array(q), axis=0)
        n = cen - c; n /= np.linalg.norm(n)
        lightdir = np.array([0.3, 0.6, -0.7]); lightdir /= np.linalg.norm(lightdir)
        k = 0.45 + 0.9*max(0, np.dot(n, lightdir))
        out.append((to_cam(cen)[2], [proj(p)[0] for p in q], clamp(scale(col, k*lit)), 'crystal'))
    return out

objs = []
objs += block(5, 2, 1500)       # left wall
objs += block(3, 1, 2600)       # upper right, further on
objs += block(7, 2, 3400)       # lower left, far
objs += block(1, 1, 2000)       # lower right
# a second crystal further on, intact: shows what the targets look like
crys_pos = np.array(corner(6, 2400, R*0.62)[:2] + (2400,))
objs += octahedron(crys_pos, 70, (255, 120, 40))
for zc, pts, col, kind in sorted(objs, key=lambda t: -t[0]):
    bd.polygon(pts, fill=clamp(col))
    if kind == 'top':
        bd.line(pts[:2], fill=clamp(scale((230, 240, 255), 0.9)), width=6)
cp, _ = proj(crys_pos)
gd.ellipse([cp[0]-90, cp[1]-90, cp[0]+90, cp[1]+90], fill=(120, 50, 10))

# ---------------------------------------------------------------- the explosion
ep, ez = proj(EXPLO)
er = 80 * F / ez * 1.0
for r, col in [(3.2, (120, 30, 0)), (2.2, (200, 70, 10)), (1.5, (255, 140, 30)),
               (1.0, (255, 210, 90)), (0.55, (255, 250, 220))]:
    rr = er * r
    gd.ellipse([ep[0]-rr, ep[1]-rr, ep[0]+rr, ep[1]+rr], fill=col)
for r, col in [(1.2, (255, 120, 20)), (0.8, (255, 200, 80)), (0.45, (255, 255, 235))]:
    rr = er * r
    bd.ellipse([ep[0]-rr, ep[1]-rr, ep[0]+rr, ep[1]+rr], fill=col)
# sparks and crystal shards flying out
for i in range(34):
    a = random.uniform(0, 2*math.pi); L = er * random.uniform(1.3, 3.6)
    x0, y0 = ep[0] + math.cos(a)*er*0.6, ep[1] + math.sin(a)*er*0.6
    x1, y1 = ep[0] + math.cos(a)*L, ep[1] + math.sin(a)*L
    c = random.choice([(255, 230, 120), (255, 170, 60), (255, 255, 255)])
    bd.line([(x0, y0), (x1, y1)], fill=c, width=5)
    gd.line([(x0, y0), (x1, y1)], fill=clamp(scale(c, 0.6)), width=14)
for i in range(9):
    a = random.uniform(0, 2*math.pi); L = er * random.uniform(1.4, 2.8); s = er*random.uniform(0.25, 0.45)
    x, y = ep[0] + math.cos(a)*L, ep[1] + math.sin(a)*L
    b = random.uniform(0, 6.28)
    tri = [(x + s*math.cos(b+t), y + s*math.sin(b+t)*1.3) for t in (0, 2.3, 4.1)]
    bd.polygon(tri, fill=random.choice([(255, 130, 40), (200, 80, 20), (255, 180, 90)]))

# ---------------------------------------------------------------- the ship
SHIP_POS = np.array([-15.0, -150.0, 640.0])
BANK, YAW, PITCH = math.radians(-14), math.radians(4), math.radians(-24)
def ship_xf(p):
    x, y, z = p
    # bank (roll about z), then yaw, then pitch
    c, s = math.cos(BANK), math.sin(BANK); x, y = x*c - y*s, x*s + y*c
    c, s = math.cos(YAW), math.sin(YAW); x, z = x*c + z*s, -x*s + z*c
    c, s = math.cos(PITCH), math.sin(PITCH); y, z = y*c - z*s, y*s + z*c
    return np.array([x, y, z]) * 1.35 + SHIP_POS

N  = (0, 2, 150)      # nose
CK = (0, 14, 40)      # canopy front
C  = (0, 22, -10)     # spine top
T  = (0, 16, -80)     # tail top
B  = (0, -10, -40)    # belly
LW, RW = (-135, -4, -85), (135, -4, -85)     # wing tips
LT, RT = (-40, 4, -100), (40, 4, -100)       # trailing edge inner
LE, RE = (-38, 6, -20), (38, 6, -20)         # wing root
FIN = (0, 55, -105)
FB  = (0, 18, -45)
HULL_L, HULL_M, HULL_D = (140, 172, 238), (70, 100, 192), (40, 55, 122)   # the sprite's palette, a touch bluer
parts = [
    # upper surfaces
    ((N, LE, CK), HULL_L), ((N, CK, RE), HULL_M),
    ((CK, LE, C), HULL_L), ((CK, C, RE), HULL_M),
    ((LE, LW, LT), HULL_M), ((RE, RT, RW), HULL_M),
    ((LE, LT, C), HULL_L), ((RE, C, RT), HULL_M),
    ((C, LT, T), HULL_M), ((C, T, RT), HULL_M),
    ((N, LW, LE), HULL_L), ((N, RE, RW), HULL_M),
    # fin
    ((T, FIN, FB), (140, 160, 210)),
    # canopy
    ((CK, (-12, 14, 5), C), (60, 210, 250)), ((CK, C, (12, 14, 5)), (30, 140, 190)),
    # the rear: where the engines sit
    ((LT, T, RT), (40, 48, 90)), ((LT, RT, (0, -6, -95)), (30, 36, 70)),
    # underside glimpses
    ((LW, B, LT), (35, 42, 80)), ((RW, RT, B), (30, 36, 70)),
]
LIGHT = np.array([-0.35, 0.8, 0.45]); LIGHT /= np.linalg.norm(LIGHT)
tris = []
for (a, b, c), col in parts:
    pa, pb, pc = ship_xf(a), ship_xf(b), ship_xf(c)
    n = np.cross(pb - pa, pc - pa); nn = np.linalg.norm(n)
    if nn == 0: continue
    n /= nn
    cen = (pa + pb + pc) / 3
    view = CAM - cen; view /= np.linalg.norm(view)
    if np.dot(n, view) < 0: n = -n
    diff = max(0, np.dot(n, LIGHT))
    h = LIGHT + view; h /= np.linalg.norm(h)
    spec = max(0, np.dot(n, h)) ** 24
    d = EXPLO - cen; d /= np.linalg.norm(d)
    rim = max(0, np.dot(n, d)) * 0.6
    k = 0.5 + 0.8*diff
    shaded = tuple(col[i]*k + 255*spec*0.55 + EXPLO_COL[i]*rim for i in range(3))
    tris.append((to_cam(cen)[2], [proj(pa)[0], proj(pb)[0], proj(pc)[0]], clamp(shaded)))
for zc, pts, col in sorted(tris, key=lambda t: -t[0]):
    bd.polygon(pts, fill=col, outline=(12, 14, 34), width=9)
# wingtip lights and red stripes
for tip in (LW, RW):
    p, _ = proj(ship_xf(tip))
    bd.ellipse([p[0]-7, p[1]-7, p[0]+7, p[1]+7], fill=(255, 60, 70))
    gd.ellipse([p[0]-26, p[1]-26, p[0]+26, p[1]+26], fill=(150, 20, 30))
# engines
for ex_ in (-15, 15):
    p, _ = proj(ship_xf((ex_, 6, -96)))
    for r, c in [(60, (140, 60, 10)), (34, (255, 140, 30)), (18, (255, 240, 170))]:
        gd.ellipse([p[0]-r, p[1]-r*0.8, p[0]+r, p[1]+r*0.8], fill=c)
    bd.ellipse([p[0]-15, p[1]-11, p[0]+15, p[1]+11], fill=(255, 220, 140))

# ---------------------------------------------------------------- lasers
for gun in ((-95, -2, -40), (95, -2, -40)):
    g = ship_xf(gun)
    tgt = EXPLO + np.array([random.uniform(-20, 20), random.uniform(-20, 20), 0])
    # a bolt in flight, and the tail of the one that hit
    for t0, t1 in ((0.18, 0.55), (0.72, 0.98)):
        a = g + (tgt - g)*t0; b = g + (tgt - g)*t1
        pa, _ = proj(a); pb, _ = proj(b)
        gd.line([pa, pb], fill=(30, 140, 255), width=46)
        gd.line([pa, pb], fill=(90, 220, 255), width=22)
        bd.line([pa, pb], fill=(120, 235, 255), width=14)
        bd.line([pa, pb], fill=(255, 255, 255), width=6)
    # muzzle flash
    p, _ = proj(ship_xf((gun[0], gun[1], 10)))
    gd.ellipse([p[0]-40, p[1]-40, p[0]+40, p[1]+40], fill=(80, 200, 255))

# speed streaks near the edges
for i in range(26):
    a = random.uniform(0, 2*math.pi)
    r0 = random.uniform(0.55, 0.8) * SW*0.6; r1 = r0 * random.uniform(1.25, 1.6)
    x0, y0 = CX + math.cos(a)*r0, CY + math.sin(a)*r0*0.8
    x1, y1 = CX + math.cos(a)*r1, CY + math.sin(a)*r1*0.8
    gd.line([(x0, y0), (x1, y1)], fill=(60, 70, 110), width=5)

# ---------------------------------------------------------------- composite + bloom
bloom = glow.filter(ImageFilter.GaussianBlur(SS*3))
bloom2 = glow.filter(ImageFilter.GaussianBlur(SS*9))
arr = np.asarray(base).astype(np.float32)
arr += np.asarray(bloom).astype(np.float32) * 0.55 + np.asarray(bloom2).astype(np.float32) * 0.12
# vignette
yy, xx = np.mgrid[0:SH, 0:SW]
d = np.sqrt(((xx - SW/2)/(SW*0.62))**2 + ((yy - SH*0.48)/(SH*0.62))**2)
arr *= np.clip(1.15 - 0.55*d**2, 0.35, 1)[..., None]
# soft tone curve so highlights roll off instead of clipping flat
arr = np.where(arr > 190, 190 + 65 * (1 - np.exp(-(arr - 190) / 65)), arr)   # compress highlights only
scene = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))

# ---------------------------------------------------------------- title
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
def title_word(text, size, top_col, bot_col, y, extrude_col, shear=-0.18):
    font = ImageFont.truetype(FONT, size)
    # mask of the word
    tmp = Image.new('L', (SW, int(size*1.5)), 0)
    td = ImageDraw.Draw(tmp)
    bb = td.textbbox((0, 0), text, font=font)
    tw = bb[2] - bb[0]
    x0 = (SW - tw)//2 - bb[0]
    td.text((x0, -bb[1] + int(size*0.15)), text, font=font, fill=255)
    # italic shear
    tmp = tmp.transform(tmp.size, Image.AFFINE, (1, shear, -shear*tmp.size[1]*0.5, 0, 1, 0), Image.BICUBIC)
    h = tmp.size[1]
    mask = tmp
    layer = Image.new('RGB', (SW, h), (0, 0, 0))
    alpha = Image.new('L', (SW, h), 0)
    # 3D extrusion: stacked copies down-right
    depth = int(size*0.12)
    for i in range(depth, 0, -1):
        k = 0.35 + 0.4*(1 - i/depth)
        m = mask.transform(mask.size, Image.AFFINE, (1, 0, -i*0.5, 0, 1, -i), Image.BICUBIC)
        layer.paste(clamp(scale(extrude_col, k)), (0, 0), m)
        alpha = Image.fromarray(np.maximum(np.asarray(alpha), np.asarray(m)))
    # dark outline
    outline = mask.filter(ImageFilter.MaxFilter(2*int(SS*0.9)+1))
    layer2 = Image.new('RGB', (SW, h), (10, 8, 20))
    layer2.paste(layer, (0, 0), alpha)
    alpha = Image.fromarray(np.maximum(np.asarray(alpha), np.asarray(outline)))
    # gradient face with a horizon highlight band (chrome / gold look)
    grad = np.zeros((h, SW, 3), np.float32)
    ys = np.nonzero(np.asarray(mask).max(axis=1) > 0)[0]
    ya, yb = ys.min(), ys.max()
    for yy_ in range(h):
        t = min(1, max(0, (yy_ - ya) / max(1, yb - ya)))
        c = mix(top_col, bot_col, t)
        if 0.45 < t < 0.55: c = mix(c, (255, 255, 255), 0.55)
        grad[yy_, :, :] = c
    face = Image.fromarray(grad.astype(np.uint8))
    layer2.paste(face, (0, 0), mask)
    return layer2, alpha, y

words = [
    title_word("TUBE", 26*SS, (255, 255, 255), (90, 170, 230), 1*SS, (40, 70, 140)),
    title_word("FLUX", 26*SS, (255, 250, 170), (230, 120, 20), 26*SS, (130, 50, 10)),
]
# a dark halo behind the title so it separates from the busy tunnel
for layer, alpha, y in words:
    halo = alpha.filter(ImageFilter.GaussianBlur(SS*3))
    shadow = Image.new('RGB', layer.size, (0, 0, 0))
    scene.paste(shadow, (0, y), halo.point(lambda v: int(v*0.75)))
for layer, alpha, y in words:
    scene.paste(layer, (0, y), alpha)
# title glint
gl = Image.new('RGB', (SW, SH), (0, 0, 0)); gld = ImageDraw.Draw(gl)
gx, gy = SW*0.30, 9*SS
gld.line([(gx-60, gy), (gx+60, gy)], fill=(255, 255, 255), width=6)
gld.line([(gx, gy-45), (gx, gy+45)], fill=(255, 255, 255), width=6)
gl = gl.filter(ImageFilter.GaussianBlur(SS*0.8))
scene = Image.fromarray(np.clip(np.asarray(scene).astype(np.int32) + np.asarray(gl), 0, 255).astype(np.uint8))

# ---------------------------------------------------------------- downsample, strip, RGB565
# The strip below STRIP_Y is left black: the game prints "[BTN A] TO PLAY"
# and the high score there every frame (see TubeFluxHud.cpp).
small = scene.resize((W, H), Image.LANCZOS)
STRIP_Y = 106
sd = ImageDraw.Draw(small)
sd.rectangle([0, STRIP_Y, W-1, H-1], fill=(0, 0, 0))
sd.line([(0, STRIP_Y), (W-1, STRIP_Y)], fill=(160, 40, 150))

# Floyd-Steinberg to RGB565 levels
a = np.asarray(small).astype(np.float32)
levels = np.array([31, 63, 31], np.float32)
out565 = np.zeros((H, W), np.uint16)
for y in range(H):
    for x in range(W):
        old = a[y, x].copy()
        q = np.round(np.clip(old, 0, 255) / 255 * levels)
        new = q / levels * 255
        a[y, x] = new
        err = old - new
        if x+1 < W: a[y, x+1] += err*7/16
        if y+1 < H:
            if x > 0: a[y+1, x-1] += err*3/16
            a[y+1, x] += err*5/16
            if x+1 < W: a[y+1, x+1] += err*1/16
        out565[y, x] = (int(q[0]) << 11) | (int(q[1]) << 5) | int(q[2])

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.join(root, "src", "games", "TubeFlux", "assets", "TubeTitleScreen.h")
os.makedirs(os.path.dirname(out), exist_ok=True)
rows = []
for y in range(H):
    rows.append("    " + ", ".join(f"0x{int(v):04X}" for v in out565[y]) + ",")
with open(out, "w") as f:
    f.write(f"""#ifndef TUBE_TITLE_SCREEN_H
#define TUBE_TITLE_SCREEN_H

#include <Arduino.h>

// Tube Flux title screen: 160x128 RGB565, row-major, 40KB in flash.
// Rows from TITLE_STRIP_Y down are black: the game prints the start prompt
// and high score there each frame.
//
// GENERATED by tools/tube_title_screen.py - change the scene there and
// re-run it; don't edit these numbers.

namespace tubeflux {{

inline constexpr int TITLE_W = {W};
inline constexpr int TITLE_H = {H};
inline constexpr int TITLE_STRIP_Y = {STRIP_Y};

inline const uint16_t TITLE_SCREEN[TITLE_W * TITLE_H] PROGMEM = {{
""" + "\n".join(rows) + """
};

}  // namespace tubeflux

#endif  // TUBE_TITLE_SCREEN_H
""")
print("wrote", out)

if "--preview" in sys.argv:
    Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).resize((W * 4, H * 4), Image.NEAREST).save("tube_title_preview.png")
    print("wrote tube_title_preview.png")
