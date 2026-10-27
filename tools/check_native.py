#!/usr/bin/env python3
"""Prueft fremde C++-Bibliotheken fuer die INLINE-Bloecke ([native]).

Baut zwei kleine C-Bibliotheken (libcalc.a, libextra.a), bindet sie ueber die
Projektdatei ein und prueft, was daraus folgen muss:

  * LIBS: die Archive landen hinter der Quelldatei in der Compilerzeile, die
    zweite Bibliothek liefert ein Symbol fuer die erste.
  * LIBDIRS + FLAGS: -L und -l sowie eigene -D-Flags kommen beim Uebersetzen an.
  * Cache: eine neu gebaute .a wird erkannt (Groesse/Zeitstempel im Namen).
  * Fehlende Bibliothek: klare Meldung statt Linkerfehler.
  * Ohne Eintrag: der Link scheitert wie erwartet.
  * Export: die Bibliothek steckt fest in der DLL, danach braucht das Spiel
    keinen Compiler mehr.

Aufruf:  python tools/check_native.py [pfad/zu/gbx.exe]
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GBX = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "gbx.exe"))
EXE = ".exe" if sys.platform == "win32" else ""

failures = 0


def check(ok, name, detail=""):
    global failures
    print(f"{'OK  ' if ok else 'FEHL'} {name}" + (f": {detail}" if detail else ""))
    if not ok:
        failures += 1


def find_compiler():
    """Der Compiler, der auch gbx baut: GBX_CXX, sonst g++/c++/clang++."""
    candidates = [os.environ.get("GBX_CXX"), "g++", "c++", "clang++"]
    for candidate in candidates:
        if not candidate:
            continue
        found = candidate if os.path.exists(candidate) else shutil.which(candidate)
        if not found:
            continue
        try:
            done = subprocess.run([found, "--version"], capture_output=True, text=True, timeout=60)
        except OSError:
            continue
        if done.returncode == 0:
            return found
    return ""


def find_ar(compiler):
    """ar gehoert zum Compiler; er kennt seinen eigenen Pfad."""
    try:
        done = subprocess.run(
            [compiler, "-print-prog-name=ar"], capture_output=True, text=True, timeout=60
        )
        name = (done.stdout or "").strip()
        if name and os.path.exists(name):
            return name
    except OSError:
        pass
    return shutil.which("ar") or ""


def write(file, text):
    os.makedirs(os.path.dirname(file), exist_ok=True)
    with open(file, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def build_library(compiler, ar, work, name, source, body):
    """C-Quelle uebersetzen (ohne Namensmanipulation) und als .a ablegen."""
    origin = os.path.join(work, "lib", source)
    write(origin, body)
    obj = os.path.join(work, "lib", f"{name}.o")
    archive = os.path.join(work, "lib", f"lib{name}.a")
    done = subprocess.run(
        [compiler, "-x", "c", "-c", origin, "-o", obj], capture_output=True, text=True, timeout=120
    )
    if done.returncode != 0:
        return False, (done.stderr or done.stdout).strip()
    done = subprocess.run([ar, "rcs", archive, obj], capture_output=True, text=True, timeout=120)
    if done.returncode != 0:
        return False, (done.stderr or done.stdout).strip()
    return True, archive


HEADER = """\
/* Header der fremden Bibliothek. */
#ifndef CALC_H
#define CALC_H

#include <gbx_inline.h>

extern "C" int calc_double(int value);

inline void fnCalcDouble(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, calc_double(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}
GBX_INLINE_FN("CALC_DOUBLE", 1, 1, fnCalcDouble);

#ifdef CALC_EXTRA
inline void fnCalcExtra(const GbxValue*, int, GbxValue* ret) { gbxinline::retNum(ret, 7); }
GBX_INLINE_FN("CALC_EXTRA", 0, 0, fnCalcExtra);
#endif

#endif /* CALC_H */
"""

PROGRAM = """\
' Testprogramm: CALC_DOUBLE kommt aus libcalc.a
INLINE
#include "calc.h"
ENDINLINE

ASSERT CALC_DOUBLE(21) = %d, "Funktion aus der Bibliothek"
"""

PROGRAM_FLAGS = """\
' Testprogramm: -l ueber FLAGS, -D ueber FLAGS
INLINE
#include "calc.h"
ENDINLINE

ASSERT CALC_DOUBLE(21) = %d, "Bibliothek ueber -l"
ASSERT CALC_EXTRA() = 7, "Flag -DCALC_EXTRA"
"""


def project_file(name, libs=None, libdirs=None, flags=None):
    lines = ["; Testprojekt", "[project]", f"name = {name}", "main = main.gbx", ""]
    if libs or libdirs or flags:
        lines.append("[native]")
        if libs:
            lines.append(f"libs = {libs}")
        if libdirs:
            lines.append(f"libdirs = {libdirs}")
        if flags:
            lines.append(f"flags = {flags}")
    return "\n".join(lines) + "\n"


def detail_lines(text, count=2):
    """Die letzten Zeilen einer Meldung fuer die Ausgabe des Tests."""
    lines = [line.strip() for line in (text or "").strip().splitlines() if line.strip()]
    return " / ".join(lines[-count:])


def run_gbx(entry, cwd, env, extra=()):
    return subprocess.run(
        [GBX, *extra, entry],
        cwd=cwd,
        env=env,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=300,
    )


def main():
    if not os.path.exists(GBX):
        print(f"FEHL Interpreter fehlt: {GBX}")
        return 1
    compiler = find_compiler()
    if not compiler:
        print("FEHL Kein C++-Compiler gefunden (GBX_CXX setzen)")
        return 1
    ar = find_ar(compiler)
    if not ar:
        print("FEHL Kein ar gefunden (gehoert zu diesem Compiler)")
        return 1

    env = dict(os.environ, GBX_LANG="de", GBX_CXX=compiler)
    with tempfile.TemporaryDirectory(prefix="gbx-native-") as work:
        print(f"Fremde Bibliotheken ({work})")
        print(f"  Compiler: {compiler}")
        print(f"  ar:       {ar}")

        ok, detail = build_library(compiler, ar, work, "extra", "extra.c", "int extra_scale(int v) { return v; }\n")
        check(ok, "libextra.a gebaut", detail)
        ok, detail = build_library(
            compiler, ar, work, "calc", "calc.c",
            "int extra_scale(int v);\nint calc_double(int v) { return extra_scale(v) * 2; }\n",
        )
        check(ok, "libcalc.a gebaut", detail)

        # ---------------------------------------------------------- LIBS
        proj = os.path.join(work, "proj")
        write(os.path.join(proj, "calc.h"), HEADER)
        write(os.path.join(proj, "main.gbx"), PROGRAM % 42)
        # Absichtlich die falsche Reihenfolge: erst libextra.a, dann libcalc.a.
        # Ohne --start-group bliebe das Symbol offen.
        write(
            os.path.join(proj, "demo.gbxproj"),
            project_file("Nativ", libs="../lib/libextra.a ../lib/libcalc.a"),
        )
        done = run_gbx("demo.gbxproj", proj, env, ["--headless", "--tests"])
        check(
            done.returncode == 0 and "0 fehlgeschlagen" in done.stdout,
            "Bibliothek aus LIBS wird mitgelinkt",
            (done.stderr or done.stdout).strip().splitlines()[-1] if (done.stderr or done.stdout) else "",
        )

        # -------------------------------------------------------- Cache
        ok, detail = build_library(
            compiler, ar, work, "calc", "calc.c",
            "int extra_scale(int v);\nint calc_double(int v) { return extra_scale(v) * 3; }\n",
        )
        check(ok, "libcalc.a neu gebaut", detail)
        write(os.path.join(proj, "main.gbx"), PROGRAM % 63)
        done = run_gbx("demo.gbxproj", proj, env, ["--headless", "--tests"])
        check(
            done.returncode == 0 and "0 fehlgeschlagen" in done.stdout,
            "neue .a wird erkannt (Cache)",
            (done.stderr or done.stdout).strip().splitlines()[-1] if (done.stderr or done.stdout) else "",
        )

        # ---------------------------------------------- LIBDIRS und FLAGS
        flags_proj = os.path.join(work, "flags")
        write(os.path.join(flags_proj, "calc.h"), HEADER)
        write(os.path.join(flags_proj, "main.gbx"), PROGRAM_FLAGS % 63)
        write(
            os.path.join(flags_proj, "flags.gbxproj"),
            project_file("Flags", libdirs="../lib", flags="-lcalc -lextra -DCALC_EXTRA"),
        )
        done = run_gbx("flags.gbxproj", flags_proj, env, ["--headless", "--tests"])
        check(
            done.returncode == 0 and "0 fehlgeschlagen" in done.stdout,
            "LIBDIRS und FLAGS kommen beim Compiler an",
            detail_lines(done.stderr + done.stdout),
        )

        # ------------------------------------------- fehlende Bibliothek
        missing = os.path.join(work, "missing")
        write(os.path.join(missing, "calc.h"), HEADER)
        write(os.path.join(missing, "main.gbx"), PROGRAM % 42)
        write(
            os.path.join(missing, "fehlt.gbxproj"),
            project_file("Fehlt", libs="../lib/libgibtsnicht.a"),
        )
        done = run_gbx("fehlt.gbxproj", missing, env, ["--headless", "--tests"])
        check(
            done.returncode != 0 and "Bibliothek nicht gefunden" in (done.stderr + done.stdout),
            "fehlende Bibliothek wird gemeldet",
            (done.stderr or done.stdout).strip().splitlines()[-1] if (done.stderr or done.stdout) else "",
        )

        # --------------------------------------------- ohne [native]
        plain = os.path.join(work, "plain")
        write(os.path.join(plain, "calc.h"), HEADER)
        write(os.path.join(plain, "main.gbx"), PROGRAM % 42)
        write(os.path.join(plain, "plain.gbxproj"), project_file("Plain"))
        done = run_gbx("plain.gbxproj", plain, env, ["--headless", "--tests"])
        output = done.stderr + done.stdout
        check(
            done.returncode != 0 and "calc_double" in output,
            "ohne Eintrag scheitert der Link",
            output.strip().splitlines()[-1] if output.strip() else "",
        )

        # -------------------------------------------------------- Export
        target = os.path.join(work, "spiel")
        os.makedirs(target, exist_ok=True)
        game = os.path.join(target, f"spiel{EXE}")
        done = run_gbx("demo.gbxproj", proj, env, ["--export", game])
        check(done.returncode == 0 and os.path.exists(game), "Export mit Bibliothek", (done.stderr or "").strip())
        if os.path.exists(game):
            # Kein Compiler, keine Quellen: die Bibliothek steckt in der DLL.
            away = dict(os.environ, GBX_LANG="de", GBX_CXX=os.path.join(work, "kein-compiler"))
            shutil.rmtree(proj)
            done = subprocess.run(
                [game, "--tests"],
                cwd=target,
                env=away,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=300,
            )
            check(
                done.returncode == 0 and "0 fehlgeschlagen" in done.stdout,
                "exportiertes Spiel ohne Compiler",
                (done.stderr or done.stdout).strip().splitlines()[-1] if (done.stderr or done.stdout) else "",
            )

    print("GESAMT:", "OK" if failures == 0 else f"{failures} Fehler")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
