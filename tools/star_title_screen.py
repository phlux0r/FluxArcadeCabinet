#!/usr/bin/env python3
"""Renders Star Flux's title screen and writes it as an RGB565 header.

    python3 tools/star_title_screen.py            # write the header
    python3 tools/star_title_screen.py --preview  # also star_title_preview.png (4x)

Needs numpy and Pillow. Takes a few seconds.

Deliberately unlike Tube Flux's and Tank Flux's titles (stacked, bevelled,
blocky capitals): one huge slanted "STAR" in an 80s chrome-sunset fill -
ice-white sky over a dark horizon over a magenta-to-gold sunset, with
slits cut through its lower half - and "F L U X" spaced out small beneath
it between two thin rules.

The scene is deep space: a nebula, a ringed gas giant, asteroids, and the
game's own ship (tools/star_ship_model.py) from behind, banking into a
squadron of enemy fighters with its twin lasers, one of them exploding.
Rendered at 4x with a bloomed glow layer, reduced to 160x128 and
Floyd-Steinberg dithered to RGB565. The bottom strip (from STRIP_Y) is
left black for the text the game draws there at runtime.

Fonts: FreeSans Bold Oblique and DejaVu Sans Bold (fonts-freefont-ttf,
fonts-dejavu-core); change FONT_* for others.

Output: src/games/StarFlux/assets/StarTitleScreen.h, 160x128, 40KB in flash.
"""
import math
import os
import random
import sys

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from star_ship_model import (Camera, fighter_triangles, rasterise, rock_triangles,  # noqa: E402
                             rot_x, rot_y, rot_z, ship_triangles, transform)

FONT_TITLE = "/usr/share/fonts/truetype/freefont/FreeSansBoldOblique.ttf"
FONT_SUB = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

random.seed(11)
rng = np.random.default_rng(11)
W, H = 160, 128
SS = 4
SW, SH = W * SS, H * SS
STRIP_Y = 106


def to_img(a):
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


# ---------------------------------------------------------------- backdrop
yy, xx = np.mgrid[0:SH, 0:SW].astype(np.float32)
sky = np.zeros((SH, SW, 3), np.float32)
# Deep blue-violet, lighter towards the right where the nebula glows.
sky[..., 0] = 6 + 10 * (xx / SW)
sky[..., 1] = 4 + 4 * (yy / SH)
sky[..., 2] = 22 + 18 * (xx / SW)


def noise(scale, octaves=4):
    out = np.zeros((SH, SW), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        n = max(2, int(scale * 2 ** o))
        small = rng.random((n, int(n * SW / SH) + 1)).astype(np.float32)
        big = np.asarray(Image.fromarray((small * 255).astype(np.uint8)).resize((SW, SH), Image.BICUBIC),
                         np.float32) / 255
        out += big * amp
        tot += amp
        amp *= 0.5
    return out / tot


# Nebula: magenta and teal clouds in a band across the upper right.
n1, n2 = noise(3), noise(4)
band = np.exp(-((yy / SH - 0.42 - 0.25 * (xx / SW - 0.6)) ** 2) / 0.05)
mag = np.clip((n1 - 0.45) * 3.2, 0, 1) * band
teal = np.clip((n2 - 0.5) * 3.0, 0, 1) * band * (xx / SW)
sky += mag[..., None] * np.array([150, 30, 120], np.float32)
sky += teal[..., None] * np.array([20, 110, 130], np.float32)

# Stars: many dim, a few bright with a small cross.
for _ in range(420):
    x, y = random.randrange(SW), random.randrange(SH)
    b = random.random() ** 3
    c = np.array(random.choice([(255, 255, 255), (200, 220, 255), (255, 230, 200)]), np.float32)
    sky[y:y + 2, x:x + 2] = np.maximum(sky[y:y + 2, x:x + 2], c * (0.25 + 0.75 * b))
    if b > 0.7:
        for d in range(1, 6):
            k = (1 - d / 6) * 0.6
            for dx, dy in ((d, 0), (-d, 0), (0, d), (0, -d)):
                if 0 <= x + dx < SW and 0 <= y + dy < SH:
                    sky[y + dy, x + dx] = np.maximum(sky[y + dy, x + dx], c * k)

# Gas giant, bottom left, lit from the upper right, with rings.
PX, PY, PR = SW * 0.12, SH * 0.80, SW * 0.33
dx, dy = (xx - PX) / PR, (yy - PY) / PR
r2 = dx * dx + dy * dy
disc = r2 < 1
dz = np.sqrt(np.clip(1 - r2, 0, 1))
light = np.array([0.55, -0.55, 0.63])
light /= np.linalg.norm(light)
lam = np.clip(dx * light[0] + dy * light[1] + dz * light[2], 0, 1)
lat = dy * 0.94 + dx * 0.34                           # tilted bands
bands = 0.5 + 0.5 * np.sin(lat * 19 + 1.7 * np.sin(lat * 7) + 3 * noise(6, 2))
pcol = (np.array([226, 150, 90], np.float32) * bands[..., None] +
        np.array([150, 70, 60], np.float32) * (1 - bands[..., None]))
shade = (0.08 + 0.92 * lam ** 0.8)[..., None]
sky = np.where(disc[..., None], pcol * shade, sky)
# Atmosphere rim on the lit side.
rim = np.exp(-((np.sqrt(r2) - 1) ** 2) / 0.0012) * np.clip(lam + 0.25, 0, 1)
sky += rim[..., None] * np.array([255, 170, 120], np.float32) * 0.55


def ring_mask(front):
    # A flat ring round the planet, seen nearly edge-on and tilted.
    a = math.radians(-22)
    rx = (xx - PX) * math.cos(a) + (yy - PY) * math.sin(a)
    ry = -(xx - PX) * math.sin(a) + (yy - PY) * math.cos(a)
    e = np.sqrt((rx / (PR * 1.38)) ** 2 + (ry / (PR * 0.26)) ** 2)
    m = np.clip(1 - np.abs(e - 0.86) / 0.14, 0, 1) ** 0.5
    m *= 0.7 + 0.3 * np.sin(e * 25)                    # grooves
    side = ry > 0 if front else ry <= 0
    return m * side


rf, rb = ring_mask(True), ring_mask(False)
ring_col = np.array([205, 215, 240], np.float32)
sky = np.where((rb > 0)[..., None] & ~disc[..., None], sky * (1 - rb[..., None] * 0.8) + ring_col * rb[..., None] * 0.8, sky)
sky = sky * (1 - rf[..., None] * 0.85) + ring_col * rf[..., None] * 0.85 * np.clip(0.4 + lam[..., None] * 0.2 + 0.4, 0, 1)

# ---------------------------------------------------------------- 3D scene
cam = Camera(pos=(0.0, 5.6, -12.5), target=(-0.6, 1.2, 20.0), width=SW, height=SH, fov_deg=62)
rgb = sky.copy()
depth = np.full((SH, SW), np.inf)
glow = np.zeros((SH, SW, 3), np.float32)
LIGHT = (0.6, 0.7, -0.4)

# Asteroids, far to near.
rocks = [((-9.5, 3.5, 34), 1.8, 1), ((8.5, 5.0, 40), 2.4, 2), ((-4.0, -3.4, 16), 1.0, 3),
         ((6.8, -2.0, 11), 0.9, 4), ((12.0, 1.0, 26), 1.4, 5), ((-13, 8.0, 48), 2.0, 6)]
for pos, r, seed in rocks:
    m = rot_x(seed * 37) @ rot_y(seed * 71)
    tr = transform(rock_triangles(seed, r), m, pos)
    rasterise(tr, cam, rgb, depth, light=LIGHT, ambient=0.22)

# The squadron: three fighters ahead, one exploding.
ft = fighter_triangles()
fighters = [((-3.6, 3.2, 22), -160, 30), ((-0.4, 4.6, 27), -175, -20), ((3.4, 3.6, 24), 168, -35)]
for pos, yaw, roll in fighters:
    rasterise(transform(ft, rot_y(yaw) @ rot_z(roll), pos, 2.6), cam, rgb, depth, light=LIGHT, ambient=0.3)

ship = transform(ship_triangles(), rot_y(6) @ rot_x(-6) @ rot_z(-20), (1.8, 1.3, 1.0), 0.95)
rasterise(ship, cam, rgb, depth, light=LIGHT, ambient=0.42)

gimg = Image.new("RGB", (SW, SH))
gd = ImageDraw.Draw(gimg)


def p2(p):
    x, y, _ = cam.project(p)
    return x, y


# Engine glow and short exhaust trails.
for ex, ey, ez in [(0.0, 0.05, -1.35), (2.3, -0.42, -1.35), (-2.3, -0.42, -1.35)]:
    wp = rot_y(6) @ rot_x(-6) @ rot_z(-20) @ np.array([ex, ey, ez]) * 0.95 + np.array([1.8, 1.3, 1.0])
    x, y = p2(wp)
    for k in range(10, 0, -1):
        rr = k * 2.2
        c = int(35 + 18 * (10 - k))
        gd.ellipse([x - rr, y - rr * 0.8, x + rr, y + rr * 0.8], fill=(c // 2, c, min(255, c + 60)))

# Twin lasers from the wing pods towards the lead fighter, which explodes.
target = np.array(fighters[0][0], float)
m = rot_y(6) @ rot_x(-6) @ rot_z(-20)
for side in (-1, 1):
    a = m @ np.array([side * 2.3, -0.42, 1.2]) * 0.95 + np.array([1.8, 1.3, 1.0])
    for t0, t1 in ((0.10, 0.34), (0.52, 0.78)):
        pa, pb = a + (target - a) * t0, a + (target - a) * t1
        (x0, y0), (x1, y1) = p2(pa), p2(pb)
        gd.line([(x0, y0), (x1, y1)], fill=(60, 255, 90), width=9)
        gd.line([(x0, y0), (x1, y1)], fill=(220, 255, 220), width=3)
ex, ey = p2(target)
for k in range(14, 0, -1):
    rr = k * 3.2
    gd.ellipse([ex - rr, ey - rr, ex + rr, ey + rr], fill=(min(255, 30 + 16 * (14 - k)), 12 * (14 - k), 4 * (14 - k)))
for i in range(14):
    a = random.uniform(0, 2 * math.pi)
    rr = random.uniform(18, 46)
    gd.line([(ex + math.cos(a) * rr * 0.4, ey + math.sin(a) * rr * 0.4),
             (ex + math.cos(a) * rr, ey + math.sin(a) * rr)], fill=(255, 200, 90), width=3)
glow_img = gimg.filter(ImageFilter.GaussianBlur(SS * 1.2))
scene = np.clip(rgb + np.asarray(glow_img, np.float32) + np.asarray(gimg, np.float32) * 0.6, 0, 255)

# ---------------------------------------------------------------- title text
def text_mask(text, font, size, spacing=0, stretch=1.0):
    f = ImageFont.truetype(font, size)
    w = sum(f.getbbox(ch)[2] for ch in text) + spacing * (len(text) - 1) + size
    img = Image.new("L", (int(w), int(size * 1.4)))
    d = ImageDraw.Draw(img)
    x = size // 4
    for ch in text:
        d.text((x, 0), ch, font=f, fill=255)
        x += f.getbbox(ch)[2] + spacing
    img = img.crop(img.getbbox())
    return img.resize((int(img.width * stretch), img.height), Image.LANCZOS)


star = text_mask("STAR", FONT_TITLE, 40 * SS // 1, spacing=-SS, stretch=1.18)
if star.width > SW - 8 * SS:
    star = star.resize((SW - 8 * SS, int(star.height * (SW - 8 * SS) / star.width)), Image.LANCZOS)
sx, sy = (SW - star.width) // 2, 5 * SS
m = np.asarray(star, np.float32) / 255
h = star.height
t = np.linspace(0, 1, h)[:, None]
# Chrome sunset: ice sky, dark horizon at 55%, magenta to gold below.
top = np.stack([185 + 70 * t, 225 + 30 * t, 255 + 0 * t], -1) * np.ones((1, star.width, 1))
tb = np.clip((t - 0.55) / 0.45, 0, 1)
bot = np.stack([255 + 0 * tb, 40 + 190 * tb, 170 - 130 * tb], -1) * np.ones((1, star.width, 1))
fill = np.where((t < 0.55)[..., None], top, bot)
horizon = np.exp(-((t - 0.55) ** 2) / 0.0012)
fill = fill * (1 - 0.75 * horizon[..., None])
# Slits through the lower half, widening downwards.
rows = np.arange(h)[:, None]
slit = np.zeros((h, 1), bool)
for c, wd in ((0.68, 0.035), (0.80, 0.05), (0.91, 0.065)):
    slit |= np.abs(rows / h - c) < wd / 2
alpha = m * (~slit)

mimg = Image.fromarray((alpha * 255).astype(np.uint8))
outline = np.asarray(mimg.filter(ImageFilter.MaxFilter(2 * SS + 1)), np.float32) / 255
halo = np.asarray(mimg.filter(ImageFilter.MaxFilter(2 * SS + 1)).filter(ImageFilter.GaussianBlur(SS * 2.5)),
                  np.float32) / 255

region = scene[sy:sy + h, sx:sx + star.width]
region += halo[..., None] * np.array([150, 40, 170], np.float32) * 0.9
region[:] = region * (1 - outline[..., None]) + np.array([20, 8, 40], np.float32) * outline[..., None]
region[:] = region * (1 - alpha[..., None]) + fill * alpha[..., None]
scene[sy:sy + h, sx:sx + star.width] = np.clip(region, 0, 255)

# A glint on the S.
gx, gy = sx + int(star.width * 0.16), sy + int(h * 0.12)
gl = Image.new("RGB", (SW, SH))
gld = ImageDraw.Draw(gl)
gld.line([(gx - 14 * SS, gy), (gx + 14 * SS, gy)], fill=(255, 255, 255), width=SS)
gld.line([(gx, gy - 7 * SS), (gx, gy + 7 * SS)], fill=(255, 255, 255), width=SS)
gld.ellipse([gx - 2 * SS, gy - 2 * SS, gx + 2 * SS, gy + 2 * SS], fill=(255, 255, 255))
gl = gl.filter(ImageFilter.GaussianBlur(SS * 0.7))
scene = np.clip(scene + np.asarray(gl, np.float32) * 1.4, 0, 255)

# "F L U X": small, spaced, between two thin rules.
flux = text_mask("FLUX", FONT_SUB, 9 * SS, spacing=5 * SS)
fx, fy = (SW - flux.width) // 2, sy + h + 3 * SS
fm = np.asarray(flux, np.float32) / 255
fglow = np.asarray(flux.filter(ImageFilter.MaxFilter(SS + 1)).filter(ImageFilter.GaussianBlur(SS * 1.5)),
                   np.float32) / 255
scene[fy:fy + flux.height, fx:fx + flux.width] += fglow[..., None] * np.array([40, 200, 255], np.float32) * 0.9
reg = scene[fy:fy + flux.height, fx:fx + flux.width]
reg[:] = reg * (1 - fm[..., None]) + np.array([255, 255, 255], np.float32) * fm[..., None]
ry = fy + flux.height // 2
for x0, x1 in ((fx - 34 * SS, fx - 4 * SS), (fx + flux.width + 4 * SS, fx + flux.width + 34 * SS)):
    xs = np.arange(max(0, x0), min(SW, x1))
    fade = 1 - np.abs((xs - (x0 + x1) / 2) / ((x1 - x0) / 2))
    scene[ry - SS // 2:ry + SS // 2, xs] = np.maximum(scene[ry - SS // 2:ry + SS // 2, xs],
                                                      (np.array([60, 210, 255])[None, :] * fade[:, None])[None])
scene = np.clip(scene, 0, 255)

# ---------------------------------------------------------------- reduce, strip, RGB565
small = to_img(scene).resize((W, H), Image.LANCZOS)
sd = ImageDraw.Draw(small)
sd.rectangle([0, STRIP_Y, W - 1, H - 1], fill=(0, 0, 0))
sd.line([(0, STRIP_Y), (W - 1, STRIP_Y)], fill=(230, 60, 150))

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
        if x + 1 < W:
            a[y, x + 1] += err * 7 / 16
        if y + 1 < H:
            if x > 0:
                a[y + 1, x - 1] += err * 3 / 16
            a[y + 1, x] += err * 5 / 16
            if x + 1 < W:
                a[y + 1, x + 1] += err * 1 / 16
        out565[y, x] = (int(q[0]) << 11) | (int(q[1]) << 5) | int(q[2])

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.join(root, "src", "games", "StarFlux", "assets", "StarTitleScreen.h")
os.makedirs(os.path.dirname(out), exist_ok=True)
rows_txt = ["    " + ", ".join(f"0x{int(v):04X}" for v in out565[y]) + "," for y in range(H)]
with open(out, "w") as f:
    f.write(f"""#ifndef STAR_TITLE_SCREEN_H
#define STAR_TITLE_SCREEN_H

#include <Arduino.h>

// Star Flux title screen: 160x128 RGB565, row-major, 40KB in flash.
// Rows from TITLE_STRIP_Y down are black: the game prints the start prompt
// and high score there each frame.
//
// GENERATED by tools/star_title_screen.py - change the scene there and
// re-run it; don't edit these numbers.

namespace starflux {{

inline constexpr int TITLE_W = {W};
inline constexpr int TITLE_H = {H};
inline constexpr int TITLE_STRIP_Y = {STRIP_Y};

inline const uint16_t TITLE_SCREEN[TITLE_W * TITLE_H] PROGMEM = {{
""" + "\n".join(rows_txt) + """
};

}  // namespace starflux

#endif  // STAR_TITLE_SCREEN_H
""")
print("wrote", out)

if "--preview" in sys.argv:
    to_img(a).resize((W * 4, H * 4), Image.NEAREST).save("star_title_preview.png")
    print("wrote star_title_preview.png")
