#!/usr/bin/env python3
"""Prueft den Export eines Spiels als einzelne EXE (gbx --export).

Quellen und die fertig uebersetzte INLINE-Bibliothek muessen im Programm
stecken; Assets bleiben draussen und werden daneben kopiert. Danach wird der
Projektordner geloescht und die EXE gestartet.

Aufruf:  python tools/check_export.py [pfad/zu/gbx.exe]
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GBX = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "gbx.exe")
ASSET = os.path.join(ROOT, "examples", "03_sprites", "assets", "ball.png")

failures = 0


def check(ok, name, detail=""):
    global failures
    print(f"{'OK  ' if ok else 'FEHL'} {name}" + (f": {detail}" if detail else ""))
    if not ok:
        failures += 1


def packaged_names(exe):
    """Liest den Dateiindex aus dem Paket am Ende der EXE."""
    data = open(exe, "rb").read()
    if len(data) < 24 or data[-24:-16] != b"GBXPKG01":
        return None
    index_size, data_size = struct.unpack_from("<QQ", data, len(data) - 16)
    start = len(data) - 24 - index_size - data_size
    index = data[start:start + index_size]
    count = struct.unpack_from("<I", index, 0)[0]
    names = []
    pos = 4
    for _ in range(count):
        length = struct.unpack_from("<I", index, pos)[0]
        pos += 4
        names.append(index[pos:pos + length].decode("utf-8"))
        pos += length + 8
    return names


def main():
    if not os.path.exists(GBX):
        print(f"FEHL Interpreter fehlt: {GBX}")
        return 1

    env = dict(os.environ, GBX_LANG="de")
    with tempfile.TemporaryDirectory(prefix="gbx-export-") as work:
        print(f"Spiel-Export ({work})")
        source = os.path.join(work, "quelle")
        target = os.path.join(work, "spiel")
        os.makedirs(os.path.join(source, "assets"))
        os.makedirs(target)
        shutil.copyfile(ASSET, os.path.join(source, "assets", "ball.png"))

        entry = os.path.join(source, "main.gbx")
        with open(entry, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(
                """INLINE
#include <gbx_inline.h>
static void dreifach(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, gbxinline::argNum(args, argc, 0) * 3);
}
GBX_INLINE_FN("DREIFACH", 1, 1, dreifach);
ENDINLINE

ASSERT DREIFACH(14) = 42, "INLINE aus dem Paket"

GLOBAL zaehler
zaehler = 0
FUNCTION schritt
  INLINE
    zaehler = zaehler + 2;
  ENDINLINE
ENDFUNCTION
schritt()
ASSERT zaehler = 2, "INLINE-Schritt aus dem Paket"
LOADSPRITE "assets/ball.png", 1
GETSPRITESIZE 1, w, h
ASSERT w > 0 AND h > 0, "Asset neben der EXE"
LOADSPRITE "ball.png", 2
GETSPRITESIZE 2, w, h
ASSERT w > 0 AND h > 0, "Asset ueber den Asset-Ordner"
STDOUT "SCREEN " + GETSCREENWIDTH() + "x" + GETSCREENHEIGHT() + CHR$(10)
"""
            )

        # Die Projektdatei muss mit: Nur so kommen Fenstergroesse und Titel an.
        project = os.path.join(source, "demo.gbxproj")
        with open(project, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(
                """; Testprojekt
[project]
name = "Fenstertest"
main = "main.gbx"

[window]
title = "Testtitel"
width = 333
height = 222
resizable = 1

[graphics]
fps = 30
vsync = 1

[assets]
root = assets
"""
            )

        exe = os.path.join(target, "spiel.exe" if sys.platform == "win32" else "spiel")
        proc = subprocess.run(
            [GBX, "--export", exe, project],
            cwd=source,
            env=env,
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=180,
        )
        check(proc.returncode == 0, "Export laeuft", (proc.stderr or proc.stdout).strip())
        if not os.path.exists(exe):
            check(False, "EXE existiert", exe)
            print("GESAMT:", f"{failures} Fehler")
            return 1
        check(True, "EXE existiert", f"{os.path.getsize(exe)} Byte")

        names = packaged_names(exe) or []
        check("main.gbx" in names and "demo.gbxproj" in names, "Quellen stecken im Paket", ", ".join(names))
        check(
            not any(name.startswith("assets/") for name in names),
            "Assets stecken nicht im Paket",
            ", ".join(name for name in names if name.startswith("assets/")),
        )
        check(
            any(name.endswith("gbx_inline.dll") or name.endswith("gbx_inline.so") for name in names),
            "INLINE-Bibliothek eingebettet",
            ", ".join(names),
        )
        check(os.path.exists(os.path.join(target, "assets", "ball.png")), "Asset liegt neben der EXE")

        # Ohne Projektordner muss die EXE allein laufen (und ohne Compiler).
        shutil.rmtree(source)
        run = subprocess.run(
            [exe, "--tests"],
            cwd=target,
            env=env,
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=180,
        )
        output = (run.stdout + run.stderr).strip()
        check(
            run.returncode == 0 and "0 fehlgeschlagen" in run.stdout,
            "EXE laeuft ohne Quellen",
            output or f"Rueckgabe {run.returncode}",
        )
        check("SCREEN 333x222" in run.stdout, "Fenstergroesse aus der .gbxproj", run.stdout.strip())
        again = subprocess.run(
            [exe, "--tests"],
            cwd=target,
            env=env,
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=180,
        )
        check(again.returncode == 0, "zweiter Start", (again.stderr or again.stdout).strip())

    print("GESAMT:", "OK" if failures == 0 else f"{failures} Fehler")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
