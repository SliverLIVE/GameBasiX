#!/usr/bin/env python3
"""Prueft Hot Reload: eine geaenderte Datei landet im laufenden Programm.

Drei Durchlaeufe:
  1. tile.png (Grafik) wird zweimal ausgetauscht - beide Male muss die neue
     Groesse im Bericht stehen.
  2. Dasselbe mit HOT_RELOAD off: die Datei bleibt unberuehrt.
  3. font.png (Bitmap-Schrift) und beep.wav (Sound) werden ersetzt.

Getauscht wird wie beim Speichern: neue Datei schreiben, dann umbenennen.
Aufruf:  python tools/check_hotreload.py [pfad/zu/gbx.exe]
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GBX = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "gbx.exe"))

LIVE = """' Hot Reload: tile.png wird waehrend des Laufs ausgetauscht.
SPRITE_LOAD "tile.png", 1
SPRITE_SIZE 1, w, h
DEBUG "start " + STR$(w) + "x" + STR$(h) + CHR$(10)
lastw = w
lasth = h
FOR f = 1 TO 160
  SPRITE_SIZE 1, w, h
  IF w <> lastw OR h <> lasth
    DEBUG "size " + STR$(w) + "x" + STR$(h) + CHR$(10)
    lastw = w
    lasth = h
  ENDIF
  SLEEP 20
  SCREEN_SHOW
NEXT
DEBUG "ende " + STR$(lastw) + "x" + STR$(lasth) + CHR$(10)
END
"""

QUIET = """' Dasselbe mit abgeschaltetem Nachladen: es bleibt bei der ersten Datei.
HOT_RELOAD 0
SPRITE_LOAD "tile.png", 1
SPRITE_SIZE 1, w, h
DEBUG "start " + STR$(w) + "x" + STR$(h) + CHR$(10)
FOR f = 1 TO 80
  SLEEP 20
  SCREEN_SHOW
NEXT
SPRITE_SIZE 1, w, h
DEBUG "ende " + STR$(w) + "x" + STR$(h) + CHR$(10)
END
"""

OTHER = """' Schrift und Sound: beide werden unter derselben Nummer ersetzt.
FONT_LOAD "font.png", 2
FONT_SET 2
SOUND_LOAD "beep.wav", 3
FOR f = 1 TO 100
  SLEEP 20
  SCREEN_SHOW
NEXT
END
"""

failures = 0


def check(ok, name, detail=""):
    global failures
    print(f"{'OK  ' if ok else 'FEHL'} {name}" + (f": {detail}" if detail else ""))
    if not ok:
        failures += 1


def write_png(path, w, h, rgba=(210, 120, 60, 255)):
    """Schreibt ein einfarbiges PNG - ohne Pillow, damit der Test ueberall laeuft."""
    raw = b"".join(b"\x00" + bytes(rgba) * w for _ in range(h))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    tmp = path + ".new"
    with open(tmp, "wb") as handle:
        handle.write(data)
    os.replace(tmp, path)  # wie ein Editor: neue Datei, dann umbenennen


def write_wav(path, ms=100, rate=48000):
    """Kurzes Stereo-WAV (Stille) in der Laenge ms."""
    frames = int(rate * ms / 1000)
    data = b"\x00\x00\x00\x00" * frames
    header = (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE" +
              b"fmt " + struct.pack("<IHHIIHH", 16, 1, 2, rate, rate * 4, 4, 16) +
              b"data" + struct.pack("<I", len(data)))
    tmp = path + ".new"
    with open(tmp, "wb") as handle:
        handle.write(header + data)
    os.replace(tmp, path)


def start(folder, source, program=None):
    if program is not None:
        with open(os.path.join(folder, source), "w", encoding="utf-8") as handle:
            handle.write(program)
    env = dict(os.environ)
    env["SDL_VIDEODRIVER"] = "dummy"
    env["SDL_AUDIODRIVER"] = "dummy"
    return subprocess.Popen([GBX, source], cwd=folder, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            encoding="utf-8", errors="replace")


def case_sprite(folder, program, exchanges):
    proc = start(folder, "main.gbx", program)
    for delay, width, height in exchanges:
        time.sleep(delay)
        write_png(os.path.join(folder, "tile.png"), width, height)
    out, _ = proc.communicate(timeout=120)
    return proc.returncode, out


def case_font_sound(folder):
    write_png(os.path.join(folder, "font.png"), 128, 64, (255, 255, 255, 255))
    write_wav(os.path.join(folder, "beep.wav"), 100)
    proc = start(folder, "other.gbx", OTHER)
    time.sleep(1.0)
    write_png(os.path.join(folder, "font.png"), 128, 64, (200, 200, 60, 255))
    write_wav(os.path.join(folder, "beep.wav"), 300)
    out, _ = proc.communicate(timeout=120)
    return proc.returncode, out


def main():
    if not os.path.exists(GBX):
        print("FEHLER: gbx nicht gefunden:", GBX)
        return 1

    folder = tempfile.mkdtemp(prefix="gbx_hot_")
    try:
        write_png(os.path.join(folder, "tile.png"), 20, 20)
        code, out = case_sprite(folder, LIVE, [(1.0, 32, 24), (1.0, 12, 40)])
        check(code == 0, "Programm bis zum Ende gelaufen", f"Rueckgabe {code}")
        check("start 20x20" in out, "erste Datei geladen")
        check("size 32x24" in out, "erste Aenderung nachgeladen")
        check("size 12x40" in out, "zweite Aenderung nachgeladen")
        check("Hot Reload: Grafik tile.png neu geladen" in out, "Meldung im Konsolenfenster")

        write_png(os.path.join(folder, "tile.png"), 20, 20)
        code, out = case_sprite(folder, QUIET, [(1.2, 32, 24)])
        check(code == 0, "Programm mit HOT_RELOAD off gelaufen", f"Rueckgabe {code}")
        check("start 20x20" in out and "ende 20x20" in out,
              "HOT_RELOAD off laesst die Datei in Ruhe", out.strip().replace("\n", " | "))
        check("size 32x24" not in out, "keine Meldung ohne Nachladen")

        code, out = case_font_sound(folder)
        check(code == 0, "Programm mit Schrift und Sound gelaufen", f"Rueckgabe {code}")
        check("Hot Reload: Schrift font.png neu geladen" in out, "Bitmap-Schrift nachgeladen")
        check("Hot Reload: Sound beep.wav neu geladen" in out, "Sound nachgeladen")
    finally:
        shutil.rmtree(folder, ignore_errors=True)

    print()
    print(f"{'OK' if failures == 0 else 'FEHLER'}: {failures} Problem(e)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
