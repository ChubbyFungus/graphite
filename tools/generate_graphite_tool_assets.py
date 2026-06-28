from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "native" / "Graphite.EngineSlice" / "assets" / "ui"


def shadow(alpha: Image.Image, offset=(5, 7), blur=5, opacity=105) -> Image.Image:
    layer = Image.new("RGBA", alpha.size, (0, 0, 0, 0))
    a = alpha.filter(ImageFilter.GaussianBlur(blur))
    layer.putalpha(a.point(lambda v: min(opacity, v)))
    shifted = Image.new("RGBA", alpha.size, (0, 0, 0, 0))
    shifted.alpha_composite(layer, offset)
    return shifted


def add_handle(draw: ImageDraw.ImageDraw, cx: int, y0: int, y1: int, width: int = 9) -> None:
    draw.rounded_rectangle((cx - width // 2, y0, cx + width // 2, y1), radius=4, fill=(16, 18, 15, 255))
    draw.line((cx - 2, y0 + 6, cx - 3, y1 - 10), fill=(56, 82, 56, 220), width=2)
    draw.line((cx + 3, y0 + 8, cx + 2, y1 - 12), fill=(3, 6, 5, 180), width=2)
    draw.rounded_rectangle((cx - width // 2 - 1, y1 - 32, cx + width // 2 + 1, y1), radius=4, fill=(9, 10, 9, 255))


def add_ferrule(draw: ImageDraw.ImageDraw, cx: int, y0: int, y1: int, w0: int, w1: int) -> None:
    points = [(cx - w0, y0), (cx + w0, y0), (cx + w1, y1), (cx - w1, y1)]
    draw.polygon(points, fill=(184, 174, 154, 255))
    draw.line((cx - w0 + 3, y0 + 4, cx - w1 + 2, y1 - 4), fill=(245, 238, 218, 200), width=3)
    draw.line((cx + w0 - 4, y0 + 5, cx + w1 - 2, y1 - 5), fill=(80, 74, 65, 170), width=2)
    for y in range(y0 + 8, y1 - 2, 10):
        draw.line((cx - w0 + 4, y, cx + w0 - 4, y), fill=(118, 109, 94, 130), width=1)


def fan_brush() -> Image.Image:
    img = Image.new("RGBA", (145, 470), (0, 0, 0, 0))
    cx = 72
    bristles = Image.new("RGBA", img.size, (0, 0, 0, 0))
    bd = ImageDraw.Draw(bristles)
    bd.polygon([(cx, 180), (35, 135), (28, 94), (42, 66), (72, 54), (102, 66), (116, 94), (109, 135)], fill=(218, 183, 138, 238))
    bd.arc((29, 42, 115, 166), 198, 342, fill=(244, 213, 169, 210), width=17)
    for i in range(36):
        t = i / 35
        edge_x = 33 + t * 78
        edge_y = 104 - abs(t - 0.5) * 54
        bd.line((cx, 178, edge_x, edge_y), fill=(246, 218, 174, 145), width=2)
    bd.arc((29, 42, 115, 166), 198, 342, fill=(40, 34, 28, 130), width=3)
    bristles = bristles.filter(ImageFilter.GaussianBlur(0.35))
    img = Image.alpha_composite(img, shadow(bristles.getchannel("A"), offset=(4, 6), blur=5, opacity=72))
    img = Image.alpha_composite(img, bristles)
    d = ImageDraw.Draw(img)
    add_ferrule(d, cx, 164, 224, 15, 9)
    add_handle(d, cx, 218, 442, 9)
    return img


def powder_brush() -> Image.Image:
    img = Image.new("RGBA", (135, 470), (0, 0, 0, 0))
    cx = 67
    bristles = Image.new("RGBA", img.size, (0, 0, 0, 0))
    bd = ImageDraw.Draw(bristles)
    bd.ellipse((38, 52, 96, 154), fill=(76, 61, 43, 240))
    bd.ellipse((45, 40, 90, 118), fill=(109, 88, 62, 232))
    bd.ellipse((54, 52, 82, 126), fill=(35, 29, 24, 105))
    for i in range(30):
        t = i / 29
        x = 43 + t * 48
        bd.line((x, 58, cx + (t - 0.5) * 12, 168), fill=(151, 123, 83, 76), width=1)
    bd.ellipse((39, 54, 95, 156), outline=(26, 22, 19, 130), width=3)
    bristles = bristles.filter(ImageFilter.GaussianBlur(0.35))
    img = Image.alpha_composite(img, shadow(bristles.getchannel("A"), offset=(4, 7), blur=5, opacity=80))
    img = Image.alpha_composite(img, bristles)
    d = ImageDraw.Draw(img)
    add_ferrule(d, cx, 154, 224, 15, 9)
    add_handle(d, cx, 218, 442, 9)
    return img


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    fan_brush().save(OUT_DIR / "fan-brush-upright.png")
    powder_brush().save(OUT_DIR / "powder-brush-upright.png")
    print(OUT_DIR / "fan-brush-upright.png")
    print(OUT_DIR / "powder-brush-upright.png")


if __name__ == "__main__":
    main()
