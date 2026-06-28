from __future__ import annotations

import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "native" / "Graphite.EngineSlice" / "assets" / "ui" / "graphite-fitted-tool-tray.png"
WIDTH = 600
HEIGHT = 980


def font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = [
        Path("C:/Windows/Fonts/CascadiaMono.ttf"),
        Path("C:/Windows/Fonts/consola.ttf"),
        Path("C:/Windows/Fonts/segoeuib.ttf"),
        Path("C:/Windows/Fonts/arial.ttf"),
    ]
    for path in candidates:
        if path.exists():
            return ImageFont.truetype(str(path), size=size)
    return ImageFont.load_default()


def rounded(draw: ImageDraw.ImageDraw, rect, radius, fill, outline=None, width=1):
    draw.rounded_rectangle(rect, radius=radius, fill=fill, outline=outline, width=width)


def leather_texture() -> Image.Image:
    random.seed(42)
    img = Image.new("RGBA", (WIDTH, HEIGHT), (20, 17, 14, 255))
    px = img.load()
    for y in range(HEIGHT):
        vertical = int(14 * y / HEIGHT)
        for x in range(WIDTH):
            grain = random.randint(-12, 10)
            long_grain = ((x * 17 + y * 3) % 31) - 15
            v = max(0, min(255, 27 + grain + long_grain // 3 + vertical))
            px[x, y] = (v, max(0, v - 5), max(0, v - 11), 255)
    img = img.filter(ImageFilter.GaussianBlur(0.42))
    overlay = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
    d = ImageDraw.Draw(overlay)
    for x in range(24, WIDTH - 24, 7):
        alpha = 9 + (x % 7) * 2
        d.line([(x, 0), (x + 26, HEIGHT)], fill=(242, 210, 160, alpha), width=1)
    for y in range(18, HEIGHT, 13):
        d.line((0, y, WIDTH, y + 5), fill=(0, 0, 0, 10), width=1)
    return Image.alpha_composite(img, overlay)


def emboss_text(draw: ImageDraw.ImageDraw, center, text: str, size=20):
    f = font(size)
    bbox = draw.textbbox((0, 0), text, font=f)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    x = center[0] - w / 2
    y = center[1] - h / 2 - 1
    draw.text((x - 1, y - 1), text, font=f, fill=(232, 215, 168, 85))
    draw.text((x + 1, y + 1), text, font=f, fill=(0, 0, 0, 185))
    draw.text((x, y), text, font=f, fill=(164, 145, 104, 235))


def recess(draw: ImageDraw.ImageDraw, rect, radius=10):
    x0, y0, x1, y1 = rect
    rounded(draw, (x0 - 2, y0 - 2, x1 + 2, y1 + 2), radius + 2, (5, 5, 4, 210), None, 1)
    rounded(draw, rect, radius, (48, 47, 41, 242), (178, 165, 125, 130), 2)
    rounded(draw, (x0 + 4, y0 + 5, x1 - 4, y1 - 4), max(2, radius - 4), (34, 33, 29, 236), (7, 7, 6, 180), 2)
    draw.line((x0 + 9, y0 + 5, x1 - 9, y0 + 5), fill=(228, 211, 158, 78), width=2)
    draw.line((x0 + 8, y1 - 5, x1 - 8, y1 - 5), fill=(0, 0, 0, 175), width=3)
    draw.line((x0 + 5, y0 + 9, x0 + 5, y1 - 9), fill=(0, 0, 0, 115), width=2)
    draw.line((x1 - 5, y0 + 9, x1 - 5, y1 - 9), fill=(226, 210, 160, 38), width=1)


def strap(draw: ImageDraw.ImageDraw, rect, vertical=False):
    x0, y0, x1, y1 = rect
    rounded(draw, rect, 3, (5, 5, 4, 230), (86, 77, 57, 145), 1)
    if vertical:
        step = 3
        x = x0 + 3
        while x < x1 - 2:
            draw.line((x, y0 + 2, x, y1 - 2), fill=(31, 30, 26, 170), width=1)
            x += step
    else:
        step = 3
        y = y0 + 3
        while y < y1 - 2:
            draw.line((x0 + 2, y, x1 - 2, y), fill=(31, 30, 26, 170), width=1)
            y += step


def main() -> None:
    img = leather_texture()
    d = ImageDraw.Draw(img)

    # Outer molded holder.
    rounded(d, (36, 18, 564, 964), 18, (26, 23, 18, 246), (144, 124, 84, 115), 2)
    rounded(d, (46, 28, 554, 954), 14, (14, 13, 11, 235), (80, 68, 45, 170), 2)
    rounded(d, (56, 42, 544, 940), 10, (31, 29, 24, 210), (5, 5, 4, 210), 2)

    # Section recesses sized for the actual tool assets.
    recess(d, (66, 84, 534, 360), 9)
    for i, cx in enumerate([88, 135, 182, 229, 276, 323, 370, 417, 464, 511]):
        groove = (cx - 18, 94, cx + 18, 350)
        recess(d, groove, 5)
    strap(d, (72, 244, 528, 262), vertical=False)

    recess(d, (72, 382, 528, 438), 8)
    strap(d, (183, 397, 198, 427), vertical=True)
    strap(d, (403, 397, 418, 427), vertical=True)

    recess(d, (76, 466, 272, 592), 8)
    recess(d, (328, 466, 524, 592), 8)

    recess(d, (102, 624, 498, 732), 9)
    strap(d, (276, 676, 324, 689), vertical=False)

    recess(d, (88, 742, 262, 948), 9)
    recess(d, (338, 742, 512, 948), 9)
    strap(d, (155, 894, 197, 906), vertical=False)
    strap(d, (405, 894, 447, 906), vertical=False)

    # Embedded labels. These are not UI buttons; they are tray surface markings.
    emboss_text(d, (300, 64), "GRAPHITE PENCILS", 21)
    emboss_text(d, (300, 374), "TORTILLON", 19)
    emboss_text(d, (174, 458), "VINYL ERASER", 18)
    emboss_text(d, (426, 458), "KNEADED ERASER", 18)
    emboss_text(d, (300, 616), "ELECTRIC ERASER", 18)
    emboss_text(d, (176, 724), "FAN BRUSH", 19)
    emboss_text(d, (426, 724), "POWDER BRUSH", 19)

    # Molded lip highlights.
    d.rounded_rectangle((36, 18, 564, 964), radius=18, outline=(232, 214, 168, 35), width=1)
    d.rounded_rectangle((52, 38, 548, 944), radius=12, outline=(0, 0, 0, 170), width=2)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    img.save(OUT)
    print(OUT)


if __name__ == "__main__":
    main()
