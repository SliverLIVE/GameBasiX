#!/usr/bin/env python3
"""Prueft jeden eingebauten Befehl ueber tests/command_tests.gbx.

Zwei Durchlaeufe:
  1. --headless --tests  (ohne Fenster, ASSERTs werden gezaehlt)
  2. SDL_VIDEODRIVER=dummy ohne --tests (mit Renderer, damit GRABSPRITE,
     SAVEBMP und die Bildschirm-Funktionen wirklich laufen)

Ausserdem: jeder Befehl aus src/builtins.cpp muss im Testskript vorkommen,
damit ein neuer Befehl nicht unbemerkt ungetestet bleibt.

Aufruf:  python tools/check_commands.py [pfad/zu/gbx.exe]
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GBX = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "gbx.exe")
BUILTINS = os.path.join(ROOT, "src", "builtins.cpp")
SCRIPT = os.path.join(ROOT, "tests", "command_tests.gbx")
OUTPUTS = [
    os.path.join(ROOT, "tests", "out_commands.bmp"),
    os.path.join(ROOT, "tests", "out_commands.png"),
]

# Nicht automatisierbar: INPUT wartet im Fenster auf Tastatureingaben und
# kehrt headless nie zurueck. Grund wird mit ausgegeben.
EXEMPT = {
    "INPUT": "wartet auf Tastatureingaben im Fenster",
}

failures = 0


def check(ok, name, detail=""):
    global failures
    print(f"{'OK  ' if ok else 'FEHL'} {name}" + (f": {detail}" if detail else ""))
    if not ok:
        failures += 1


def strip_comments(source):
    """Entfernt ' -Kommentare, ohne Anfuehrungszeichen anzutasten."""
    out = []
    for line in source.splitlines():
        result = []
        in_string = False
        for ch in line:
            if ch == '"':
                in_string = not in_string
            if ch == "'" and not in_string:
                break
            result.append(ch)
        out.append("".join(result))
    return "\n".join(out)


def builtin_names():
    source = open(BUILTINS, encoding="utf-8").read()
    return re.findall(r'add\("([^"]+)"', source)


def used_names():
    source = strip_comments(open(SCRIPT, encoding="utf-8").read())
    return {token.upper() for token in re.findall(r"[A-Za-z][A-Za-z0-9_]*\$?", source)}


def run(args, env_extra):
    env = dict(os.environ)
    env.pop("SDL_VIDEODRIVER", None)
    env.pop("SDL_AUDIODRIVER", None)
    env.update(env_extra)
    return subprocess.run(
        [GBX] + args,
        cwd=ROOT,
        env=env,
        input="testzeile\n",
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        timeout=120,
    )


def main():
    if not os.path.exists(GBX):
        print("FEHLER: gbx nicht gefunden:", GBX)
        return 1

    # ------------------------------------------------------------ Abdeckung
    names = builtin_names()
    used = used_names()
    missing = [name for name in names if name not in used and name not in EXEMPT]
    tested = [name for name in names if name not in EXEMPT]
    check(not missing, f"Abdeckung: {len(tested)} von {len(names)} Befehlen getestet",
          ", ".join(missing))
    for name, reason in EXEMPT.items():
        print(f"AUSN {name}: nicht automatisierbar ({reason})")

    # ------------------------------------------------------------- Headless
    headless = run(["--headless", "--tests", SCRIPT], {"GBX_TEST_STDIN": "1"})
    output = headless.stdout + headless.stderr
    match = re.search(r"(\d+) Pruefungen, (\d+) fehlgeschlagen", headless.stdout)
    check(headless.returncode == 0, "Headless-Durchlauf",
          "" if headless.returncode == 0 else f"Rueckgabe {headless.returncode}")
    check("ASSERT FEHLGESCHLAGEN" not in output, "Headless: keine fehlgeschlagenen Asserts",
          "\n".join(line for line in output.splitlines() if "FEHLGESCHLAGEN" in line))
    check(bool(match), "Headless: Pruefungen gezaehlt")
    if match:
        total, failed = int(match.group(1)), int(match.group(2))
        check(total > 0 and failed == 0, f"Headless: {total} Pruefungen, {failed} fehlgeschlagen")

    # -------------------------------------------------------------- Renderer
    render = run([SCRIPT], {
        "GBX_TEST_STDIN": "1",
        "SDL_VIDEODRIVER": "dummy",
        "SDL_AUDIODRIVER": "dummy",
    })
    output = render.stdout + render.stderr
    check(render.returncode == 0, "Durchlauf mit Renderer",
          "" if render.returncode == 0 else f"Rueckgabe {render.returncode}")
    check("ASSERT FEHLGESCHLAGEN" not in output, "Renderer: keine fehlgeschlagenen Asserts",
          "\n".join(line for line in output.splitlines() if "FEHLGESCHLAGEN" in line))
    check("BEFEHLSTEST FERTIG" in render.stdout, "Renderer: Programm bis zum Ende gelaufen")

    for path in OUTPUTS:
        if os.path.exists(path):
            os.remove(path)

    print()
    print(f"{'OK' if failures == 0 else 'FEHLER'}: {failures} Problem(e)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
