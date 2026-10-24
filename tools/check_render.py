#!/usr/bin/env python3
"""Rendert tests/render_check.gbx offscreen (SDL dummy) und prueft Pixel.

Aufruf:  python tools/check_render.py [pfad/zu/gbx.exe]
"""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GBX = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "gbx.exe")
SCRIPT = os.path.join(ROOT, "tests", "render_check.gbx")
BMP = os.path.join(ROOT, "tests", "out_render.bmp")


def read_bmp(path):
    data = open(path, "rb").read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    stride = ((width * bpp // 8) + 3) // 4 * 4

    def pixel(x, y):
        row = height - 1 - y if height > 0 else y
        i = offset + row * stride + x * (bpp // 8)
        b, g, r = data[i], data[i + 1], data[i + 2]
        return (r, g, b)

    return width, abs(height), pixel


def main():
    if os.path.exists(BMP):
        os.remove(BMP)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    proc = subprocess.run([GBX, SCRIPT], cwd=ROOT, env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        print(proc.stdout, proc.stderr)
        print("FEHLER: Programm endete mit", proc.returncode)
        return 1
    if not os.path.exists(BMP):
        print("FEHLER: SAVEBMP hat keine Datei erzeugt")
        return 1

    width, height, px = read_bmp(BMP)
    if (width, height) != (320, 240):
        print("FEHLER: unerwartete Groesse", width, height)
        return 1

    checks = {
        "rotes Rechteck": ((50, 30), (255, 0, 0)),
        "gruene Linie": ((100, 0), (0, 255, 0)),
        "blauer Kreis": ((160, 130), (0, 128, 255)),
        "gelber Rahmen": ((300, 10), (255, 255, 0)),
        "Hintergrund": ((5, 235), (0, 0, 64)),
    }
    failures = 0
    for name, (xy, expected) in checks.items():
        got = px(*xy)
        ok = got == expected
        print(f"{'OK  ' if ok else 'FEHL'} {name}: {got}" + ("" if ok else f" (erwartet {expected})"))
        failures += 0 if ok else 1

    white = sum(1 for y in range(190, 240) for x in range(12, 160) if px(x, y) == (255, 255, 255))
    print(f"OK   Textpixel: {white}" if white > 100 else f"FEHL Textpixel: {white}")
    if white <= 100:
        failures += 1

    sprite = px(262, 182)
    sprite_ok = sum(sprite) > 300
    print(f"{'OK  ' if sprite_ok else 'FEHL'} Sprite gezeichnet: {sprite}")
    if not sprite_ok:
        failures += 1

    print("GESAMT:", "OK" if failures == 0 else f"{failures} Fehler")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
