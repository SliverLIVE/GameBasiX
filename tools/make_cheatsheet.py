#!/usr/bin/env python3
"""Erzeugt CHEATSHEET.png: die Befehle von GameBasiX nach Art gruppiert.

Quelle ist ide/data/gbx-dataset.json, also derselbe Datensatz wie Referenz und
Vervollständigung der IDE. Aufruf:

    python tools/make_cheatsheet.py [--lang de|en] [--out CHEATSHEET.png]
"""
from __future__ import annotations

import argparse
import json
import pathlib

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATASET = ROOT / "ide" / "data" / "gbx-dataset.json"

TITLES = {
    "de": ("GameBasiX", "Befehlsübersicht", "nach Art gruppiert"),
    "en": ("GameBasiX", "Command cheat sheet", "grouped by category"),
}

# Reihenfolge der Gruppen; alles andere folgt aus dem Datensatz.
ORDER = [
    "Fenster und Bildschirm",
    "Zeichnen",
    "Sprites",
    "Bildpuffer und Text",
    "Eingabe",
    "Sound",
    "Mathe",
    "Strings",
    "Dateien",
    "Arrays",
    "Laufzeit und Debug",
    "Sprache",
    "Konstanten",
]

CATEGORY_EN = {
    "Fenster und Bildschirm": "Window and screen",
    "Zeichnen": "Drawing",
    "Sprites": "Sprites",
    "Bildpuffer und Text": "Screen buffer and text",
    "Eingabe": "Input",
    "Sound": "Sound",
    "Mathe": "Maths",
    "Strings": "Strings",
    "Dateien": "Files",
    "Arrays": "Arrays",
    "Laufzeit und Debug": "Runtime and debug",
    "Sprache": "Language",
    "Konstanten": "Constants",
}

BACKGROUND = (20, 23, 28)
PANEL = (30, 35, 42)
HEAD = (40, 46, 55)
ACCENT = (126, 209, 128)
TEXT = (228, 233, 240)
MUTED = (150, 160, 172)

WIDTH = 2700
MARGIN = 70
COLUMNS = 3
GAP = 46
PAD = 22
FONT_SIZE = 21
LINE_GAP = 9
ITEM_GAP = 16
HEAD_SIZE = 27

FONT_CANDIDATES = {
    "body": ["C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/DejaVuSansMono.ttf"],
    "head": ["C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/segoeui.ttf"],
    "title": ["C:/Windows/Fonts/seguibl.ttf", "C:/Windows/Fonts/segoeuib.ttf"],
}


def load_font(kind: str, size: int) -> ImageFont.FreeTypeFont:
    for path in FONT_CANDIDATES[kind]:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default(size)


def compact_constants(names: list[str]) -> list[str]:
    """KEY_A … KEY_Z und KEY_0 … KEY_9 als Bereich zusammenfassen."""
    letters = [n for n in names if len(n) == 5 and n.startswith("KEY_") and n[4].isalpha()]
    digits = [n for n in names if len(n) == 5 and n.startswith("KEY_") and n[4].isdigit()]
    rest = sorted(n for n in names if n not in letters and n not in digits)
    # Sprechende Konstanten zuerst, dann die Tasten, zuletzt die Bereiche.
    named = [n for n in rest if not n.startswith("KEY_")]
    keys = [n for n in rest if n.startswith("KEY_")]
    out = named + keys
    if letters:
        out.append("KEY_A … KEY_Z")
    if digits:
        out.append("KEY_0 … KEY_9")
    return out


def collect(lang: str):
    data = json.loads(DATASET.read_text(encoding="utf-8"))
    groups: dict[str, list[tuple[str, str]]] = {}
    for entry in data["commands"]:
        if entry["kind"] == "overview":
            continue
        groups.setdefault(entry["category"], []).append((entry["name"], entry["kind"]))

    ordered = ORDER + [c for c in groups if c not in ORDER]
    result = []
    for category in ordered:
        if category not in groups:
            continue
        items = sorted(groups[category], key=lambda item: item[0].upper())
        names = [name for name, _ in items]
        if category == "Konstanten":
            names = compact_constants(names)
        label = CATEGORY_EN.get(category, category) if lang == "en" else category
        result.append((label, names))
    return result


def wrap(draw: ImageDraw.ImageDraw, names: list[str], font, box: int) -> list[list[str]]:
    """Verteilt die Namen auf Zeilen; jeder Name bleibt ungeteilt."""
    rows: list[list[str]] = []
    row: list[str] = []
    width = 0.0
    for name in names:
        w = draw.textlength(name, font=font)
        need = w if not row else w + ITEM_GAP
        if row and width + need > box:
            rows.append(row)
            row, width = [name], w
        else:
            row.append(name)
            width += need
    if row:
        rows.append(row)
    return rows


def measure(draw, groups, fonts, box):
    blocks = []
    for label, names in groups:
        rows = wrap(draw, names, fonts["body"], box)
        height = PAD + HEAD_SIZE + 18 + len(rows) * (FONT_SIZE + LINE_GAP) + PAD
        blocks.append((label, rows, height))
    return blocks


def balance(blocks, column_height):
    columns: list[list] = [[]]
    height = 0
    for block in blocks:
        if columns[-1] and height + block[2] > column_height:
            columns.append([])
            height = 0
        columns[-1].append(block)
        height += block[2] + PAD
    return columns


def render(lang: str, out: pathlib.Path) -> None:
    body = load_font("body", FONT_SIZE)
    head = load_font("head", HEAD_SIZE)
    title = load_font("title", 66)
    subtitle = load_font("head", 30)

    probe = ImageDraw.Draw(Image.new("RGB", (10, 10)))
    groups = collect(lang)
    col_width = (WIDTH - 2 * MARGIN - GAP * (COLUMNS - 1)) // COLUMNS
    box = col_width - 2 * PAD
    blocks = measure(probe, groups, {"body": body}, box)

    # Spaltenhöhe so wählen, dass die Gruppen gleichmäßig auf COLUMNS Spalten
    # verteilt werden, ohne dass eine Spalte überläuft.
    total = sum(block[2] + PAD for block in blocks)
    column_height = total / COLUMNS
    columns = balance(blocks, column_height)
    while len(columns) > COLUMNS:
        column_height *= 1.08
        columns = balance(blocks, column_height)
    while len(columns) < COLUMNS:
        column_height *= 0.9
        columns = balance(blocks, column_height)

    top = MARGIN + 150
    content_height = max(
        sum(block[2] + PAD for block in column) for column in columns
    )
    height = top + content_height + MARGIN + 50

    image = Image.new("RGB", (WIDTH, int(height)), BACKGROUND)
    draw = ImageDraw.Draw(image)

    brand, heading, note = TITLES[lang]
    draw.text((MARGIN, MARGIN - 6), brand, font=title, fill=ACCENT)
    brand_width = draw.textlength(brand, font=title)
    draw.text((MARGIN + brand_width + 26, MARGIN + 16), heading, font=subtitle, fill=TEXT)
    count = sum(len(names) for _, names in groups)
    draw.text(
        (MARGIN, MARGIN + 74),
        f"{count} {'Einträge' if lang == 'de' else 'entries'} · {note}",
        font=subtitle,
        fill=MUTED,
    )
    draw.line((MARGIN, MARGIN + 128, WIDTH - MARGIN, MARGIN + 128), fill=HEAD, width=2)

    draw.text(
        (MARGIN, height - MARGIN + 14),
        (
            "Erzeugt aus ide/data/gbx-dataset.json · python tools/make_cheatsheet.py"
            if lang == "de"
            else "Generated from ide/data/gbx-dataset.json · python tools/make_cheatsheet.py"
        ),
        font=subtitle,
        fill=MUTED,
    )

    for index, column in enumerate(columns):
        x = MARGIN + index * (col_width + GAP)
        y = top
        for label, rows, block_height in column:
            draw.rounded_rectangle((x, y, x + col_width, y + block_height), 14, fill=PANEL)
            draw.rounded_rectangle((x, y, x + col_width, y + PAD + HEAD_SIZE + 10), 14, fill=HEAD)
            draw.text((x + PAD, y + PAD - 2), label, font=head, fill=ACCENT)
            ty = y + PAD + HEAD_SIZE + 18
            for row in rows:
                tx = x + PAD
                for name in row:
                    draw.text((tx, ty), name, font=body, fill=TEXT)
                    tx += draw.textlength(name, font=body) + ITEM_GAP
                ty += FONT_SIZE + LINE_GAP
            y += block_height + PAD

    out.parent.mkdir(parents=True, exist_ok=True)
    image.save(out, optimize=True)
    print(f"{out} ({image.width}×{image.height}, {count} Einträge)")


def main() -> None:
    parser = argparse.ArgumentParser(description="Cheatsheet als PNG erzeugen")
    parser.add_argument("--lang", choices=["de", "en"], default="de")
    parser.add_argument("--out", default=str(ROOT / "CHEATSHEET.png"))
    args = parser.parse_args()
    render(args.lang, pathlib.Path(args.out))


if __name__ == "__main__":
    main()
