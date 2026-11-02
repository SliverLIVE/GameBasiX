#!/usr/bin/env python3
"""Tauscht ein Asset des Hot-Reload-Beispiels aus, waehrend das Spiel laeuft.

Das Spiel merkt die Aenderung selbst (etwa alle 200 ms) und ersetzt Grafik,
Schrift oder Sound unter derselben Nummer - genau das, was auch passiert, wenn
du die Datei in einem Editor speicherst.

Aufruf:
  python tools/swap-asset.py tile              tauscht zwischen 32x32 und 48x48
  python tools/swap-asset.py font              tauscht zwischen 8x8 und 12x8
  python tools/swap-asset.py beep              tauscht zwischen 440 Hz und 660 Hz
  python tools/swap-asset.py tile --random     schreibt eine zufaellige Grafik
  python tools/swap-asset.py beep --random     schreibt einen zufaelligen Ton
  python tools/swap-asset.py tile --dir <ordner>   anderes Projektverzeichnis
"""
import argparse
import math
import os
import pathlib
import random
import shutil
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_DIR = ROOT / "examples" / "20_hotreload"

ASSETS = {
    "tile": ("tile.png", "tile-alt.png", "tile-orig.png"),
    "font": ("font.png", "font-alt.png", "font-orig.png"),
    "beep": ("beep.wav", "beep-alt.wav", "beep-orig.wav"),
}


def write_png(path, w, h, pixels):
    raw = b"".join(b"\x00" + bytes(pixels[y]) for y in range(h))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    tmp = path.with_suffix(path.suffix + ".new")
    tmp.write_bytes(data)
    os.replace(tmp, path)  # wie ein Editor: neue Datei, dann umbenennen


def random_image(w, h, rgb):
    """Ein Muster aus Rauten und Ringen in einer Zufallsfarbe."""
    px = [[0] * (w * 4) for _ in range(h)]
    cx = cy = (w - 1) / 2
    for y in range(h):
        for x in range(w):
            d = abs(x - cx) + abs(y - cy)
            ring = math.hypot(x - cx, y - cy)
            if d <= w * 0.42 or (w * 0.18 <= ring <= w * 0.3):
                px[y][x * 4:x * 4 + 4] = [rgb[0], rgb[1], rgb[2], 255]
    return px


def write_wav(path, freq, ms, rate=48000):
    frames = int(rate * ms / 1000)
    data = bytearray()
    for i in range(frames):
        fade = min(1.0, i / (rate * 0.01), (frames - i) / (rate * 0.02))
        value = int(12000 * fade * math.sin(2 * math.pi * freq * i / rate))
        data += struct.pack("<hh", value, value)
    header = (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE" +
              b"fmt " + struct.pack("<IHHIIHH", 16, 1, 2, rate, rate * 4, 4, 16) +
              b"data" + struct.pack("<I", len(data)))
    tmp = path.with_suffix(path.suffix + ".new")
    tmp.write_bytes(header + bytes(data))
    os.replace(tmp, path)


def swap(folder, kind, randomize):
    # Das Beispiel legt seine Dateien im Asset-Ordner ab; ohne einen solchen
    # gilt der Projektordner selbst.
    asset_dir = folder / "assets"
    if not asset_dir.is_dir():
        asset_dir = folder
    live_name, alt_name, orig_name = ASSETS[kind]
    live = asset_dir / live_name
    alt = asset_dir / alt_name
    orig = asset_dir / orig_name
    if not alt.exists() or not orig.exists():
        print(f"Fehlt: {alt.name} oder {orig.name} in {asset_dir}")
        return 1

    if randomize:
        if kind == "font":
            print("Eine Schrift braucht einen Atlas aus 16x8 Zeichen - dafuer gibt es "
                  "kein Zufallsmuster. Nutze 'font' ohne --random.")
            return 1
        if kind == "tile":
            size = random.choice([20, 28, 36, 44, 56, 72])
            write_png(live, size, size, random_image(size, size, (
                random.randrange(80, 256), random.randrange(80, 256), random.randrange(80, 256))))
            print(f"{live_name}: zufaellige Grafik {size}x{size} px")
        else:
            freq = random.randrange(220, 880, 20)
            ms = random.randrange(120, 400, 20)
            write_wav(live, freq, ms)
            print(f"{live_name}: zufaelliger Ton {freq} Hz, {ms} ms")
        return 0

    if live.exists() and live.read_bytes() == alt.read_bytes():
        shutil.copyfile(orig, live)
        print(f"{live_name}: zurueck auf die Originalfassung ({orig_name})")
    else:
        shutil.copyfile(alt, live)
        print(f"{live_name}: ersetzt durch {alt_name}")
    try:
        os.utime(live, None)  # Zeitstempel sicher neu, auch bei gleicher Groesse
    except OSError:
        pass
    return 0


def main():
    parser = argparse.ArgumentParser(description="Tauscht ein Asset des Hot-Reload-Beispiels.")
    parser.add_argument("kind", choices=sorted(ASSETS), help="tile, font oder beep")
    parser.add_argument("--random", action="store_true", help="neue Zufallsdatei statt Tausch")
    parser.add_argument("--dir", default=str(DEFAULT_DIR), help="Projektordner (Standard: %(default)s)")
    args = parser.parse_args()

    folder = pathlib.Path(args.dir).resolve()
    if not folder.is_dir():
        print(f"Ordner nicht gefunden: {folder}")
        return 1
    code = swap(folder, args.kind, args.random)
    if code == 0:
        print("Laeuft das Spiel, ist die Aenderung in der naechsten Sekunde sichtbar "
              "(oder sofort mit R).")
    return code


if __name__ == "__main__":
    sys.exit(main())
