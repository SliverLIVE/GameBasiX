"""Erzeugt das GBX-Studio-Icon (ide/assets/icon.ico und icon-256.png).

Das Icon zeigt dunklen Grund, gruene Initialen und darunter klein "STUDIO".
Aufruf:

    python tools/make_icon.py

Schrift: Segoe UI Bold (Windows); fehlt sie, wird DejaVu Sans Bold benutzt.
"""

from __future__ import annotations

import pathlib

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSETS = ROOT / "ide" / "assets"

BG = (13, 18, 24, 255)        # #0D1218
ACCENT = (143, 206, 39, 255)  # #8FCE27
TEXT = (211, 220, 232, 255)   # #D3DCE8

SUPERSAMPLE = 4
SIZES = [256, 128, 64, 48, 32, 24, 16]

# Anteile am alten Logo (256er-Raster): Initialen 155x62 px, STUDIO 86x17 px
GBX_CAP = 62 / 256
GBX_CENTER = (65 + 126) / 2 / 256
GBX_MAX_WIDTH = 0.70
STUDIO_WIDTH = 86 / 256
STUDIO_CAP = 15 / 256
STUDIO_BASELINE = 220 / 256

FONT_CANDIDATES = [
    pathlib.Path("C:/Windows/Fonts/segoeuib.ttf"),
    pathlib.Path("C:/Windows/Fonts/arialbd.ttf"),
    pathlib.Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
    pathlib.Path("/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"),
]


def font_file() -> pathlib.Path:
    for candidate in FONT_CANDIDATES:
        if candidate.exists():
            return candidate
    raise SystemExit("Keine fette Schrift gefunden; bitte in FONT_CANDIDATES ergänzen.")


FONT_PATH = font_file()


def cap_ratio(px: int) -> float:
    """Höhe der Großbuchstaben im Verhältnis zur Schriftgröße."""
    font = ImageFont.truetype(str(FONT_PATH), px)
    left, top, right, bottom = font.getbbox("H")
    return (bottom - top) / px


def font_for_cap(cap_height: float) -> ImageFont.FreeTypeFont:
    px = max(1, round(cap_height / cap_ratio(100)))
    for _ in range(4):
        font = ImageFont.truetype(str(FONT_PATH), px)
        _, top, _, bottom = font.getbbox("H")
        measured = bottom - top
        if abs(measured - cap_height) <= 0.5:
            break
        px = max(1, round(px * cap_height / measured))
    return ImageFont.truetype(str(FONT_PATH), px)


def tracked_width(text: str, font: ImageFont.FreeTypeFont, tracking: float) -> float:
    return sum(font.getlength(ch) for ch in text) + tracking * (len(text) - 1)


def draw_tracked(draw: ImageDraw.ImageDraw, text: str, font: ImageFont.FreeTypeFont,
                 tracking: float, baseline_y: float, center_x: float,
                 color: tuple[int, int, int, int]) -> None:
    """Zeichnet Text mit Laufweite, waagerecht um center_x zentriert."""
    x = center_x - tracked_width(text, font, tracking) / 2
    for ch in text:
        draw.text((x, baseline_y), ch, font=font, fill=color, anchor="ls")
        x += font.getlength(ch) + tracking


def fit_initials(work: int, cap: float, max_width: float) -> tuple[ImageFont.FreeTypeFont, float, float]:
    """Schrift, Versalhoehe und Laufweite fuer "GBX" so, dass es passt.

    Die Laufweite bleibt positiv, damit sich die Buchstaben nicht beruehren.
    """
    font = font_for_cap(cap)
    natural = font.getlength("GBX")
    if natural > max_width:
        cap *= max_width / natural
        font = font_for_cap(cap)
    tracking = cap * 0.02
    while tracked_width("GBX", font, tracking) > max_width:
        cap *= 0.98
        font = font_for_cap(cap)
        tracking = cap * 0.02
    return font, cap, tracking


def compose(size: int) -> Image.Image:
    """Ein Icon in Zielgroesse; kleinere Groessen zeigen nur die Initialen."""
    work = size * SUPERSAMPLE
    image = Image.new("RGBA", (work, work), BG)
    draw = ImageDraw.Draw(image)

    if size < 64:
        # Nur "GBX", gross und mittig - bei kleinen Groessen ist der Zusatz
        # "STUDIO" nur noch ein Fleck.
        font, cap, tracking = fit_initials(work, work * 0.30, work * 0.80)
        center = work / 2
        draw_tracked(draw, "GBX", font, tracking, center + cap / 2, center, ACCENT)
    else:
        font, cap, tracking = fit_initials(work, work * GBX_CAP, work * GBX_MAX_WIDTH)
        draw_tracked(draw, "GBX", font, tracking, work * GBX_CENTER + cap / 2,
                     work / 2, ACCENT)
        studio_font = font_for_cap(work * STUDIO_CAP)
        studio_tracking = (work * STUDIO_WIDTH - studio_font.getlength("STUDIO")) / 4
        draw_tracked(draw, "STUDIO", studio_font, studio_tracking,
                     work * STUDIO_BASELINE, work / 2, TEXT)

    return image.resize((size, size), Image.LANCZOS)


def dib_bytes(image: Image.Image) -> bytes:
    """Ein Icon-Bild als unkomprimiertes 32-Bit-DIB (BGRA + AND-Maske)."""
    import struct

    size = image.width
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                         size * size * 4, 0, 0, 0, 0)
    rows = []
    for y in range(size - 1, -1, -1):
        row = bytearray()
        for x in range(size):
            r, g, b, a = image.getpixel((x, y))
            row += bytes((b, g, r, a))
        rows.append(bytes(row))
    mask_row = (size + 31) // 32 * 4
    mask = b"\x00" * (mask_row * size)
    return header + b"".join(rows) + mask


def write_ico(path: pathlib.Path, images: list[Image.Image]) -> None:
    """Schreibt ein ICO: grosse Groessen als PNG, kleine als DIB.

    Die Shell versteht beides; kleine DIB-Eintraege bleiben auch fuer aeltere
    Werkzeuge lesbar und kosten wenig Platz.
    """
    import io
    import struct

    entries = []
    for image in images:
        if image.width > 64:
            buffer = io.BytesIO()
            image.save(buffer, format="PNG")
            entries.append((image.width, buffer.getvalue()))
        else:
            entries.append((image.width, dib_bytes(image)))

    offset = 6 + 16 * len(entries)
    directory = struct.pack("<HHH", 0, 1, len(entries))
    payload = b""
    for size, data in entries:
        directory += struct.pack("<BBBBHHII", size if size < 256 else 0,
                                 size if size < 256 else 0, 0, 0, 1, 32,
                                 len(data), offset + len(payload))
        payload += data
    path.write_bytes(directory + payload)


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)
    images = [compose(size) for size in SIZES]

    compose(256).save(ASSETS / "icon-256.png")
    write_ico(ASSETS / "icon.ico", images)
    print(f"Schrift: {FONT_PATH.name}")
    print(f"{ASSETS / 'icon.ico'} ({', '.join(str(s) for s in SIZES)} px)")
    print(f"{ASSETS / 'icon-256.png'} (256 px)")


if __name__ == "__main__":
    main()
