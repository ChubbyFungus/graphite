from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
GEN_DIR = Path(r"C:\Users\ftbal\projects\EG3\.codex\generated_images\019e2dd5-3aa5-7d93-8c66-3fcd79fbc648")
OUT = ROOT / "native" / "Graphite.EngineSlice" / "assets" / "ui"
ROLL_WIDTH = 900
ROLL_HEIGHT = 520

HOLDER_SRC = GEN_DIR / "ig_0f5d3fe115e363b0016a09086dd4048195a97ad7eff3a44f59.png"
STRAPS_SRC = GEN_DIR / "ig_0f5d3fe115e363b0016a09026c08688195978cbf640d9cd52c.png"
TOOLS_SRC = GEN_DIR / "ig_0f5d3fe115e363b0016a0908b0ead88195bd43bc5cba10e4e2.png"
CONTAINER_AND_ELECTRIC_SRC = GEN_DIR / "ig_056328704ecb2359016a0950cb48648193bda38e694c7eafe3.png"


def key_alpha(img: Image.Image) -> Image.Image:
    img = img.convert("RGBA")
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            magenta_score = ((r + b) // 2) - g
            if r > 150 and b > 140 and magenta_score > 46:
                px[x, y] = (r, g, b, 0)
            elif r > 120 and b > 115 and magenta_score > 28:
                alpha = max(0, min(a, 255 - magenta_score * 4))
                px[x, y] = (min(r, 230), max(g, 210), min(b, 235), alpha)
    return img


def save_crop(src: Image.Image, box, name: str, pad=12, rotate_degrees: int = 0) -> None:
    crop = key_alpha(src.crop(box))
    alpha_box = crop.getchannel("A").getbbox()
    if alpha_box:
        x0, y0, x1, y1 = alpha_box
        crop = crop.crop((max(0, x0 - pad), max(0, y0 - pad), min(crop.width, x1 + pad), min(crop.height, y1 + pad)))
    if rotate_degrees:
        crop = crop.rotate(rotate_degrees, expand=True)
    crop.save(OUT / name)


def save_retainer_strap(name: str, size: tuple[int, int], orientation: str) -> None:
    w, h = size
    img = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    rect = (2, 2, w - 3, h - 3)
    d.rounded_rectangle(rect, radius=max(3, min(w, h) // 7), fill=(80, 42, 28, 246), outline=(148, 88, 54, 220), width=1)
    if orientation == "horizontal":
        for y in range(4, h - 3, 3):
            d.line((4, y, w - 5, y), fill=(122, 70, 44, 95), width=1)
        for x in range(8, w - 6, 18):
            d.line((x, 3, x, h - 4), fill=(31, 14, 9, 85), width=1)
    else:
        for x in range(4, w - 3, 3):
            d.line((x, 4, x, h - 5), fill=(122, 70, 44, 95), width=1)
        for y in range(8, h - 6, 16):
            d.line((3, y, w - 4, y), fill=(31, 14, 9, 85), width=1)
    img = img.filter(ImageFilter.UnsharpMask(radius=1.0, percent=90, threshold=2))
    img.save(OUT / name)


def font(size: int):
    for path in [Path("C:/Windows/Fonts/CascadiaMono.ttf"), Path("C:/Windows/Fonts/consolab.ttf"), Path("C:/Windows/Fonts/consola.ttf")]:
        if path.exists():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


def leather_surface(width: int, height: int) -> Image.Image:
    img = Image.new("RGBA", (width, height), (21, 19, 16, 255))
    px = img.load()
    for y in range(height):
        for x in range(width):
            grain = ((x * 31 + y * 17) % 23) - 11
            fine = ((x * 7 + y * 5) % 9) - 4
            edge = int(18 * abs(x - width / 2) / (width / 2) + 12 * y / height)
            v = max(0, min(255, 28 + grain + fine - edge // 3))
            px[x, y] = (v, max(0, v - 4), max(0, v - 9), 255)
    return img.filter(ImageFilter.GaussianBlur(0.34))


def inset(draw: ImageDraw.ImageDraw, rect, radius: int = 12) -> None:
    x0, y0, x1, y1 = rect
    draw.rounded_rectangle((x0 - 3, y0 - 3, x1 + 3, y1 + 3), radius=radius + 3, fill=(4, 4, 4, 170))
    draw.rounded_rectangle(rect, radius=radius, fill=(17, 16, 14, 245), outline=(79, 66, 45, 120), width=1)
    draw.rounded_rectangle((x0 + 6, y0 + 6, x1 - 6, y1 - 6), radius=max(4, radius - 5), fill=(9, 9, 8, 236), outline=(0, 0, 0, 190), width=2)
    draw.line((x0 + 14, y0 + 7, x1 - 14, y0 + 7), fill=(220, 200, 150, 35), width=1)
    draw.line((x0 + 14, y1 - 7, x1 - 14, y1 - 7), fill=(0, 0, 0, 135), width=2)


def flat_pad(draw: ImageDraw.ImageDraw, rect, radius: int = 12) -> None:
    x0, y0, x1, y1 = rect
    draw.rounded_rectangle((x0 - 2, y0 - 2, x1 + 2, y1 + 2), radius=radius + 2, fill=(4, 4, 4, 120))
    draw.rounded_rectangle(rect, radius=radius, fill=(14, 13, 11, 238), outline=(75, 62, 42, 105), width=1)
    draw.line((x0 + 16, y0 + 8, x1 - 16, y0 + 8), fill=(230, 210, 164, 24), width=1)
    draw.line((x0 + 16, y1 - 8, x1 - 16, y1 - 8), fill=(0, 0, 0, 105), width=2)
    draw.line((300, y0 + 18, 300, y1 - 18), fill=(0, 0, 0, 80), width=1)


def engraved_text(draw: ImageDraw.ImageDraw, xy, text: str, size: int = 13, anchor: str = "la") -> None:
    f = font(size)
    x, y = xy
    draw.text((x + 1, y + 1), text, font=f, anchor=anchor, fill=(0, 0, 0, 205))
    draw.text((x, y), text, font=f, anchor=anchor, fill=(168, 147, 104, 230))


def draw_tool_tray() -> Image.Image:
    out = leather_surface(ROLL_WIDTH, ROLL_HEIGHT)
    d = ImageDraw.Draw(out)

    # Open roll case: black fabric backing, brown leather pockets, long strap.
    d.rounded_rectangle((18, 18, 882, 502), radius=28, fill=(66, 36, 24, 255), outline=(122, 78, 52, 180), width=2)
    d.rounded_rectangle((34, 34, 866, 486), radius=20, fill=(10, 10, 10, 246), outline=(0, 0, 0, 210), width=3)

    for x in range(48, 856, 34):
        d.line((x, 46, x + 12, 470), fill=(35, 34, 31, 80), width=1)
    for x in [278, 338, 398, 458, 518, 578, 608, 638, 668, 698, 728, 758, 788, 818, 848]:
        d.line((x, 56, x, 456), fill=(0, 0, 0, 105), width=1)
        d.line((x + 1, 56, x + 1, 456), fill=(80, 72, 65, 50), width=1)

    # Left block pockets for erasers.
    for rect in [(62, 92, 174, 246), (188, 92, 300, 246)]:
        d.rounded_rectangle(rect, radius=8, fill=(74, 38, 25, 255), outline=(143, 83, 50, 160), width=1)
        d.rectangle((rect[0] + 5, rect[1] + 82, rect[2] - 5, rect[3] - 5), fill=(37, 19, 13, 225))
        d.line((rect[0] + 8, rect[1] + 83, rect[2] - 8, rect[1] + 83), fill=(186, 108, 63, 130), width=1)

    # Lower leather pocket band for long tools.
    d.rounded_rectangle((250, 348, 850, 470), radius=10, fill=(77, 39, 25, 255), outline=(120, 70, 44, 145), width=1)
    for x in [278, 338, 398, 458, 518, 578, 608, 638, 668, 698, 728, 758, 788, 818]:
        d.line((x, 350, x, 468), fill=(25, 12, 8, 165), width=2)
        d.line((x + 2, 352, x + 2, 466), fill=(137, 80, 50, 90), width=1)

    # Retaining strap, explicitly holding electric eraser and every other long tool.
    d.rounded_rectangle((38, 252, 862, 314), radius=12, fill=(94, 48, 31, 255), outline=(160, 91, 55, 190), width=2)
    d.line((48, 262, 852, 262), fill=(197, 117, 72, 95), width=1)
    d.line((48, 304, 852, 304), fill=(28, 13, 8, 170), width=2)
    d.ellipse((822, 267, 848, 293), fill=(132, 101, 64, 255), outline=(221, 177, 110, 140), width=1)

    d.rounded_rectangle((18, 18, 882, 502), radius=28, outline=(234, 214, 165, 36), width=1)
    return out


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)

    imagegen_roll_case = OUT / "roll-case-nostrap-imagegen-source.png"
    if imagegen_roll_case.exists():
        pass
    else:
        draw_tool_tray().save(OUT / "graphite-fitted-tool-tray.png")

    tools = Image.open(TOOLS_SRC).convert("RGBA")
    pencil_boxes = [
        (160, 25, 230, 540),
        (280, 25, 350, 540),
        (398, 25, 468, 540),
        (520, 25, 590, 540),
        (640, 25, 710, 540),
        (760, 25, 830, 540),
        (880, 25, 950, 540),
        (1000, 25, 1070, 540),
        (1120, 25, 1190, 540),
        (1240, 25, 1310, 540),
    ]
    pencil_names = ["4h", "3h", "2h", "h", "hb", "b", "2b", "4b", "6b", "8b"]
    for box, name in zip(pencil_boxes, pencil_names):
        save_crop(tools, box, f"pencil-{name}.png", pad=6)

    if (OUT / "tortillon-vertical.png").exists():
        Image.open(OUT / "tortillon-vertical.png").save(OUT / "tortillon-horizontal.png")
    else:
        save_crop(tools, (330, 560, 1165, 660), "tortillon-horizontal.png", pad=8, rotate_degrees=-90)
    save_crop(tools, (245, 655, 470, 815), "vinyl-eraser.png", pad=10)
    container_tools = Image.open(CONTAINER_AND_ELECTRIC_SRC).convert("RGBA")
    boxed_kneaded = OUT / "kneaded-eraser-boxed.png"
    if boxed_kneaded.exists():
        Image.open(boxed_kneaded).save(OUT / "kneaded-eraser.png")
    else:
        save_crop(container_tools, (120, 190, 660, 700), "kneaded-eraser.png", pad=10)
    if (OUT / "electric-eraser-vertical.png").exists():
        Image.open(OUT / "electric-eraser-vertical.png").save(OUT / "electric-eraser-horizontal.png")
    else:
        save_crop(container_tools, (790, 360, 1705, 545), "electric-eraser-horizontal.png", pad=10, rotate_degrees=-90)
    if (OUT / "fan-brush-vertical.png").exists():
        Image.open(OUT / "fan-brush-vertical.png").save(OUT / "fan-brush-upright.png")
    else:
        save_crop(tools, (145, 835, 720, 990), "fan-brush-upright.png", pad=10, rotate_degrees=-90)
    if (OUT / "powder-brush-vertical.png").exists():
        Image.open(OUT / "powder-brush-vertical.png").save(OUT / "powder-brush-upright.png")
    else:
        save_crop(tools, (785, 815, 1360, 990), "powder-brush-upright.png", pad=10, rotate_degrees=-90)

    if (OUT / "leather-retaining-strap.png").exists():
        Image.open(OUT / "leather-retaining-strap.png").save(OUT / "strap-pencil.png")
    else:
        save_retainer_strap("strap-pencil.png", (900, 34), "horizontal")
    save_retainer_strap("strap-tortillon-left.png", (24, 58), "vertical")
    save_retainer_strap("strap-tortillon-right.png", (24, 58), "vertical")
    save_retainer_strap("strap-vinyl-eraser.png", (120, 34), "horizontal")
    save_retainer_strap("strap-electric-eraser.png", (24, 58), "vertical")
    save_retainer_strap("strap-fan-brush.png", (22, 56), "vertical")
    save_retainer_strap("strap-powder-brush.png", (22, 56), "vertical")

    print(OUT)


if __name__ == "__main__":
    main()
