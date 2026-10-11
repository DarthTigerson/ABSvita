#!/usr/bin/env python3
"""Generates the LiveArea art in sce_sys/ (8-bit palette PNGs, as the Vita requires).
Needs Pillow."""
import os
from PIL import Image, ImageDraw, ImageFont

BG = (15, 17, 21)
AMBER = (245, 165, 36)
INK = (24, 18, 8)
WHITE = (242, 242, 245)
SUB = (154, 160, 174)
FONT = "assets/fonts/Manrope-700.ttf"
FONT_REG = "assets/fonts/Manrope-500.ttf"


def book(d, cx, cy, s, fill):
    """open book, as in the app's icon set (unit space -1..1)"""
    def P(x, y):
        return (cx + x * s, cy + y * s)
    d.polygon([P(-0.04, -0.42), P(-0.66, -0.56), P(-0.66, 0.46), P(-0.04, 0.58)], fill=fill)
    d.polygon([P(0.04, -0.42), P(0.66, -0.56), P(0.66, 0.46), P(0.04, 0.58)], fill=fill)


def save(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.convert("RGB").quantize(colors=255, method=Image.Quantize.MEDIANCUT).save(path, optimize=True)


def icon():
    S = 4
    img = Image.new("RGB", (128 * S, 128 * S), BG)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, 128 * S - 1, 128 * S - 1], radius=26 * S, fill=AMBER)
    book(d, 64 * S, 66 * S, 44 * S, INK)
    return img.resize((128, 128), Image.LANCZOS)


def bg():
    W, H = 840, 500
    img = Image.new("RGB", (W, H), BG)
    px = img.load()
    for y in range(H):
        t = y / H
        c = tuple(int(BG[i] * t + (40, 30, 14)[i] * (1 - t)) for i in range(3))
        for x in range(W):
            px[x, y] = c
    d = ImageDraw.Draw(img)
    S = 3
    big = Image.new("RGBA", (200 * S, 200 * S), (0, 0, 0, 0))
    bd = ImageDraw.Draw(big)
    bd.ellipse([0, 0, 200 * S - 1, 200 * S - 1], fill=AMBER)
    book(bd, 100 * S, 102 * S, 70 * S, INK)
    big = big.resize((200, 200), Image.LANCZOS)
    img.paste(big, (W - 250, 130), big)
    # the system draws the start gate over x 280..560, y 140..300: keep text below it
    f1 = ImageFont.truetype(FONT, 56)
    f2 = ImageFont.truetype(FONT_REG, 24)
    d.text((60, 340), "ABSvita", font=f1, fill=WHITE)
    d.text((62, 414), "Audiobookshelf for PS Vita", font=f2, fill=SUB)
    return img


def startup():
    W, H = 280, 158
    S = 3
    img = Image.new("RGB", (W * S, H * S), BG)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, W * S - 1, H * S - 1], radius=18 * S, fill=(26, 29, 36))
    d.ellipse([28 * S, 39 * S, 108 * S, 119 * S], fill=AMBER)
    book(d, 68 * S, 80 * S, 28 * S, INK)
    f = ImageFont.truetype(FONT, 30 * S)
    d.text((124 * S, 58 * S), "ABSvita", font=f, fill=WHITE)
    return img.resize((W, H), Image.LANCZOS)


save(icon(), "sce_sys/icon0.png")
save(bg(), "sce_sys/livearea/contents/bg.png")
save(startup(), "sce_sys/livearea/contents/startup.png")
print("art written to sce_sys/")
