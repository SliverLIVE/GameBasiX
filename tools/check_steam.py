#!/usr/bin/env python3
"""Prueft die STEAM_*-Befehle gegen eine Mock-steam_api.

Baut tools/mock_steam_api.cpp zu einer Bibliothek, auf die GBX_STEAM_LIB
zeigt, und laesst tests/steam_tests.gbx dagegen laufen. Damit ist der
komplette Weg abgedeckt - Bibliothek laden, Flat-API binden, Identitaet,
Freundesliste und P2P-Pakete - ohne dass Steam installiert sein muss.

Aufruf:  python tools/check_steam.py [pfad/zu/gbx.exe]
Umgebung: GBX_CXX (Compiler, sonst g++/clang++)
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GBX = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "gbx.exe"))
MOCK = os.path.join(ROOT, "tools", "mock_steam_api.cpp")
SCRIPT = os.path.join(ROOT, "tests", "steam_tests.gbx")

failures = 0


def check(ok, name, detail=""):
    global failures
    print(f"{'OK  ' if ok else 'FEHL'} {name}" + (f": {detail}" if detail else ""))
    if not ok:
        failures += 1


def compiler():
    for candidate in (os.environ.get("GBX_CXX"), "g++", "clang++", "c++"):
        if not candidate:
            continue
        path = shutil.which(candidate) or (candidate if os.path.exists(candidate) else None)
        if path:
            return path
    return None


def main():
    if not os.path.exists(GBX):
        print(f"FEHL Interpreter fehlt: {GBX}")
        return 1
    cxx = compiler()
    if not cxx:
        print("FEHL Kein C++-Compiler gefunden (GBX_CXX setzen)")
        return 1

    with tempfile.TemporaryDirectory(prefix="gbx-steam-") as work:
        print(f"Steam-Mock ({work})")
        lib = os.path.join(work, "steam_api64.dll" if os.name == "nt" else "libsteam_api.so")
        build = subprocess.run(
            [cxx, "-shared", "-O2", "-std=c++17",
             "-static-libgcc", "-static-libstdc++",
             "-o", lib, MOCK],
            cwd=ROOT, capture_output=True, text=True,
        )
        check(build.returncode == 0, "Mock-Bibliothek gebaut",
              (build.stderr or build.stdout).strip()[-400:])
        if build.returncode != 0:
            return 1

        env = dict(os.environ, GBX_STEAM_LIB=lib)
        run = subprocess.run(
            [GBX, "--headless", "--tests", SCRIPT],
            cwd=ROOT, env=env, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=120,
        )
        output = run.stdout + run.stderr
        check(run.returncode == 0, "Durchlauf", f"Rueckgabe {run.returncode}")
        check("ASSERT FEHLGESCHLAGEN" not in output, "keine fehlgeschlagenen Asserts",
              "\n".join(line for line in output.splitlines() if "FEHLGESCHLAGEN" in line))
        check("STEAMTEST FERTIG" in run.stdout, "Programm bis zum Ende gelaufen")
        counted = [line for line in run.stdout.splitlines() if "fehlgeschlagen" in line]
        check(bool(counted) and "0 fehlgeschlagen" in counted[-1],
              "Pruefungen", counted[-1] if counted else output.strip()[-300:])

    print()
    print(f"{'OK' if failures == 0 else 'FEHLER'}: {failures} Problem(e)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
