"""Draws the Telescreen icon: an orange-framed screen on a billboard post showing a play button.

  python Tools/make_icon.py

Writes Docs/Images/icon-1080.png (full resolution), Docs/Images/icon-512.png (mod page) and Resources/Icon128.png
(shown in the game's Mods menu). Designed on a 512-unit grid and rendered with supersampling.
"""
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GRID = 512
FULL = 1080
SUPER = 2
K = FULL * SUPER / GRID

TOP, BOTTOM = (38, 44, 54), (22, 26, 32)
ORANGE, ORANGE_DARK = (250, 149, 73), (196, 104, 40)
SCREEN_TOP, SCREEN_BOTTOM = (36, 60, 92), (14, 24, 42)
GREY, GREY_DARK = (92, 98, 108), (64, 69, 77)
WHITE = (238, 240, 242)


def s(v):
    return int(v * K)


def main():
    size = FULL * SUPER
    img = Image.new('RGB', (size, size))
    d = ImageDraw.Draw(img)
    for y in range(size):
        t = y / size
        d.line([(0, y), (size, y)], fill=tuple(int(TOP[i] + (BOTTOM[i] - TOP[i]) * t) for i in range(3)))
    # Post
    d.rectangle([s(236), s(330), s(276), s(440)], fill=GREY)
    d.rectangle([s(236), s(330), s(252), s(440)], fill=GREY_DARK)
    d.rectangle([s(196), s(432), s(316), s(452)], fill=GREY_DARK)
    # Frame
    d.rounded_rectangle([s(56), s(96), s(456), s(340)], radius=s(18), fill=ORANGE_DARK)
    d.rounded_rectangle([s(60), s(96), s(456), s(334)], radius=s(18), fill=ORANGE)
    # Screen
    x0, y0, x1, y1 = 82, 118, 430, 312
    for y in range(s(y0), s(y1)):
        t = (y - s(y0)) / (s(y1) - s(y0))
        d.line([(s(x0), y), (s(x1), y)], fill=tuple(int(SCREEN_TOP[i] + (SCREEN_BOTTOM[i] - SCREEN_TOP[i]) * t) for i in range(3)))
    # Play button
    cx, cy, r = 256, 215, 56
    d.ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], fill=WHITE)
    d.polygon([(s(cx - 16), s(cy - 30)), (s(cx - 16), s(cy + 30)), (s(cx + 34), s(cy))], fill=ORANGE_DARK)
    # Glint
    d.polygon([(s(x0), s(y0)), (s(x0 + 110), s(y0)), (s(x0), s(y0 + 70))], fill=(58, 88, 128))
    img = img.resize((FULL, FULL), Image.LANCZOS)
    os.makedirs(os.path.join(ROOT, 'Docs', 'Images'), exist_ok=True)
    img.save(os.path.join(ROOT, 'Docs', 'Images', 'icon-1080.png'))
    img.resize((512, 512), Image.LANCZOS).save(os.path.join(ROOT, 'Docs', 'Images', 'icon-512.png'))
    img.resize((128, 128), Image.LANCZOS).save(os.path.join(ROOT, 'Resources', 'Icon128.png'))
    print('icons written')


if __name__ == '__main__':
    main()
