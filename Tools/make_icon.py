"""Draws the Telescreen icon: a billboard screen on its post showing a play button and a pointer, with sound waves
on both sides and two pioneers watching in front.

  python Tools/make_icon.py [output folder]

Writes Docs/Images/icon-1080.png (full resolution), Docs/Images/icon-512.png (mod page) and Resources/Icon128.png
(shown in the game's Mods menu); with an output folder, the three files go there instead (for previews). Designed on
a 512-unit grid and rendered with supersampling.
"""
import os
import sys
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GRID = 512
FULL = 1080
SUPER = 3
K = FULL * SUPER / GRID

TOP, BOTTOM = (38, 44, 54), (20, 23, 29)
ORANGE, ORANGE_DARK = (250, 149, 73), (196, 104, 40)
FRAME, FRAME_DARK = (84, 90, 100), (58, 63, 71)
SCREEN_TOP, SCREEN_BOTTOM = (40, 92, 140), (18, 34, 62)
GREY, GREY_DARK = (92, 98, 108), (64, 69, 77)
WHITE = (238, 240, 242)
PIONEER, PIONEER_DARK = (104, 110, 120), (74, 79, 88)


def s(v):
    return int(round(v * K))


def box(x0, y0, x1, y1):
    return [s(x0), s(y0), s(x1), s(y1)]


def gradient(d, x0, y0, x1, y1, top, bottom):
    for y in range(s(y0), s(y1)):
        t = (y - s(y0)) / max(1, s(y1) - s(y0))
        d.line([(s(x0), y), (s(x1), y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))


def pioneer(d, cx, base):
    # Seen from behind: rounded shoulders and a helmet with its orange lamp.
    d.ellipse(box(cx - 60, base - 34, cx + 60, base + 86), fill=PIONEER_DARK)
    d.ellipse(box(cx - 30, base - 96, cx + 30, base - 36), fill=PIONEER)
    d.chord(box(cx - 30, base - 96, cx + 30, base - 36), 20, 160, fill=(90, 96, 106))
    d.ellipse(box(cx + 12, base - 92, cx + 26, base - 78), fill=ORANGE)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else None
    size = FULL * SUPER
    img = Image.new('RGB', (size, size))
    d = ImageDraw.Draw(img)
    gradient(d, 0, 0, GRID, GRID, TOP, BOTTOM)

    # Post and foot.
    d.rectangle(box(234, 300, 278, 470), fill=GREY)
    d.rectangle(box(234, 300, 252, 470), fill=GREY_DARK)
    d.rounded_rectangle(box(196, 456, 316, 474), radius=s(4), fill=GREY_DARK)

    # Billboard frame: dark grey with an orange top rail, like the game's signs.
    d.rounded_rectangle(box(70, 66, 442, 316), radius=s(14), fill=FRAME_DARK)
    d.rounded_rectangle(box(70, 62, 442, 310), radius=s(14), fill=FRAME)
    d.rounded_rectangle(box(70, 62, 442, 86), radius=s(14), fill=ORANGE)
    d.rectangle(box(70, 76, 442, 86), fill=ORANGE)

    # Screen with a soft glow.
    x0, y0, x1, y1 = 88, 96, 424, 294
    gradient(d, x0, y0, x1, y1, SCREEN_TOP, SCREEN_BOTTOM)
    d.polygon([(s(x0), s(y0)), (s(x0 + 130), s(y0)), (s(x0), s(y0 + 80))], fill=(58, 112, 162))

    # Play button.
    cx, cy, r = 256, 192, 56
    d.ellipse(box(cx - r, cy - r, cx + r, cy + r), fill=WHITE)
    d.polygon([(s(cx - 18), s(cy - 32)), (s(cx - 18), s(cy + 32)), (s(cx + 36), s(cy))], fill=ORANGE_DARK)

    # Pointer (shared browsers and desktops).
    px, py = 334, 218
    arrow = [(0, 0), (0, 52), (13, 40), (23, 62), (33, 57), (23, 36), (40, 36)]
    outline = [(-5, -9), (-5, 62), (12, 47), (21, 70), (40, 61), (31, 42), (52, 40)]
    d.polygon([(s(px + x), s(py + y)) for x, y in outline], fill=(18, 22, 30))
    d.polygon([(s(px + x), s(py + y)) for x, y in arrow], fill=WHITE)

    # Sound waves on both sides of the billboard.
    for side in (-1, 1):
        for i, radius in enumerate((22, 44)):
            cxw = 256 + side * 200
            arc_box = box(cxw - radius, 192 - radius, cxw + radius, 192 + radius)
            start, end = (-50, 50) if side > 0 else (130, 230)
            d.arc(arc_box, start, end, fill=ORANGE, width=s(11 - i * 2))

    # Two pioneers watching.
    pioneer(d, 142, 462)
    pioneer(d, 372, 470)

    img = img.resize((FULL, FULL), Image.LANCZOS)
    images = os.path.join(ROOT, 'Docs', 'Images') if not out else out
    small = os.path.join(ROOT, 'Resources') if not out else out
    os.makedirs(images, exist_ok=True)
    img.save(os.path.join(images, 'icon-1080.png'))
    img.resize((512, 512), Image.LANCZOS).save(os.path.join(images, 'icon-512.png'))
    img.resize((128, 128), Image.LANCZOS).save(os.path.join(small, 'Icon128.png'))
    print('icons written to', images)


if __name__ == '__main__':
    main()
