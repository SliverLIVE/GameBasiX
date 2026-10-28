#!/usr/bin/env python3
"""Prueft die Namenspruefung von `gbx --check`.

Alles, was das Programm nirgends definiert, muss ein Fehler sein: unbekannte
Befehle, Funktionen und Variablen. Bekannte Dinge - Befehle, Konstanten,
eigene Funktionen, Ausgabeparameter, Marken - duerfen nicht anschlagen.

Aufruf:  python tools/check_lint.py [pfad/zu/gbx.exe]
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GBX = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "gbx.exe")

failures = 0


def check(ok, name, detail=""):
    global failures
    print(f"{'OK  ' if ok else 'FEHL'} {name}" + (f": {detail}" if detail else ""))
    if not ok:
        failures += 1


def run_check(work, source, name="main.gbx", stdin=False, base=None):
    """Schreibt (oder schickt) ein Programm und liefert Rueckgabe plus Meldungen."""
    if stdin:
        result = subprocess.run(
            [GBX, "--check", "--stdin", "--base", base or work, "--name", name],
            cwd=work,
            input=source,
            text=True,
            encoding="utf-8",
            capture_output=True,
        )
        return result.returncode, result.stderr, os.path.join(base or work, name)
    path = os.path.join(work, name)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source)
    result = subprocess.run(
        [GBX, "--check", path], cwd=work, text=True, encoding="utf-8", capture_output=True
    )
    return result.returncode, result.stderr, path


def expect_clean(work, title, source):
    code, stderr, _ = run_check(work, source)
    check(code == 0 and not stderr.strip(), title, stderr.strip() or f"Rueckgabe {code}")


def expect_error(work, title, source, line, text):
    code, stderr, path = run_check(work, source)
    wanted = f"{path}:{line}: Fehler: {text}"
    hits = [row for row in stderr.splitlines() if wanted in row.replace("\\", os.sep)]
    check(code == 2 and hits, title, stderr.strip() or f"Rueckgabe {code}")


def main():
    if not os.path.exists(GBX):
        print(f"FEHL Interpreter fehlt: {GBX}")
        return 1

    with tempfile.TemporaryDirectory(prefix="gbx-lint-") as work:
        print(f"Namenspruefung ({work})")

        # ---------------------------------------------------- unbekannte Namen
        expect_error(work, "unbekannter Befehl", "bla\n", 1, "Unbekannter Befehl: BLA")
        expect_error(work, "unbekannte Variable", 'PRINT foo, 0, 0\nEND\n', 1, "Unbekannter Name: FOO")
        expect_error(work, "unbekannte Funktion", "x = NOPE(1)\nEND\n", 1, "Unbekannte Funktion: NOPE")
        expect_error(work, "Tippfehler in der Mitte", "x = 1\ny = x + zzz\nEND\n", 2, "Unbekannter Name: ZZZ")
        expect_error(work, "unbekannte Marke", "GOTO spaeter\nEND\n", 1, "Unbekannte Marke: SPAETER")
        expect_error(
            work, "unbekanntes Unterprogramm", "GOSUB gibtEsNicht\nEND\n", 1, "Unbekanntes Unterprogramm: GIBTESNICHT"
        )

        # ------------------------------------------------- Include-Zyklen
        def write_file(name, text):
            with open(os.path.join(work, name), "w", encoding="utf-8", newline="\n") as handle:
                handle.write(text)

        write_file("ring_a.gbx", 'INCLUDE "ring_b.gbx"\nEND\n')
        write_file("ring_b.gbx", 'INCLUDE "ring_a.gbx"\nEND\n')
        result = subprocess.run(
            [GBX, "--check", os.path.join(work, "ring_a.gbx")],
            cwd=work,
            text=True,
            encoding="utf-8",
            capture_output=True,
        )
        chain = "ring_a.gbx -> ring_b.gbx -> ring_a.gbx"
        check(
            result.returncode == 2 and "Zyklischer INCLUDE: " + chain in result.stderr,
            "Include-Zyklus wird gemeldet",
            result.stderr.strip(),
        )

        write_file("selbst.gbx", 'INCLUDE "selbst.gbx"\nEND\n')
        result = subprocess.run(
            [GBX, "--check", os.path.join(work, "selbst.gbx")],
            cwd=work,
            text=True,
            encoding="utf-8",
            capture_output=True,
        )
        check(
            result.returncode == 2 and "Zyklischer INCLUDE: selbst.gbx -> selbst.gbx" in result.stderr,
            "Include auf sich selbst wird gemeldet",
            result.stderr.strip(),
        )

        # ------------------------------------------- bekannte Namen bleiben ruhig
        expect_clean(
            work,
            "Befehle, Konstanten und eigene Funktionen",
            """' Bekanntes darf nicht anschlagen
PRINT "Hallo", 0, 0
x = 3
IF KEY_PRESSED(KEY_SPACE) THEN x = x + ABS(-2)
SCREEN_SET 640, 480
y = spaeter(2)
END

FUNCTION spaeter: wert
  RETURN wert * 2
ENDFUNCTION
""",
        )
        expect_clean(
            work,
            "Ausgabeparameter legen die Variable an",
            """INPUT name$
PRINT name$, 0, 0
MOUSE_STATE mx, my, mb, mbb
SCREEN_SIZE w, h
DIM teile$[8]
SPLIT_STRING("a,b", teile$[], ",")
END
""",
        )
        expect_clean(
            work,
            "Schleifen, TYPE, Marken und GOSUB",
            """TYPE PUNKT
  x AS FLOAT
  y AS FLOAT
ENDTYPE

LOCAL p AS PUNKT
p.x = 1
DIM punkte[2] AS PUNKT
punkte[0].y = 2

FOR i = 1 TO 3
  gesamt = gesamt + i
NEXT

summe = 0
GOSUB zaehle
GOTO weiter
weiter:
PRINT gesamt, summe, p.x, punkte[0].y, 0, 0
END

SUB zaehle:
  summe = summe + 1
ENDSUB
""",
        )
        expect_clean(
            work,
            "eigene Konstante und Array",
            """CONST BREITE% = 10
DIM feld[BREITE%]
feld[0] = BREITE%
PRINT feld[0], 0, 0
END
""",
        )
        # ------------------------------------------------------- TYPE-Felder
        types_source = """TYPE PUNKT
  x AS FLOAT
  y AS FLOAT
  name$
ENDTYPE

TYPE RAHMEN
  links AS PUNKT
  rechts AS PUNKT
ENDTYPE

LOCAL p AS PUNKT
p.x = 1
p.name$ = "ok"

LOCAL r AS RAHMEN
r.links.name$ = "ok"

DIM feld[2] AS PUNKT
feld[0].y = 2
PRINT p.x, r.links.x, feld[0].y, 0, 0
END
"""
        expect_clean(work, "TYPE-Felder: bekannte Namen", types_source)

        expect_error(
            work,
            "unbekanntes Feld mit Vorschlag",
            types_source.replace('p.name$ = "ok"', 'p.nam$ = "tippfehler"'),
            14,
            "Unbekanntes Feld: NAM (PUNKT) - meintest du NAME?",
        )
        expect_error(
            work,
            "unbekanntes Feld in verschachteltem TYPE",
            types_source.replace('r.links.name$ = "ok"', 'r.links.nam$ = "x"'),
            17,
            "Unbekanntes Feld: NAM (PUNKT)",
        )
        expect_error(
            work,
            "unbekanntes Feld in einem TYPE-Array",
            types_source.replace("feld[0].y = 2", "feld[0].yy = 2"),
            20,
            "Unbekanntes Feld: YY (PUNKT)",
        )
        expect_clean(
            work,
            "TYPE über Funktionsparameter und Zuweisung",
            """TYPE KISTE
  breite AS FLOAT
ENDTYPE

FUNCTION flaeche: k AS KISTE
  RETURN k.breite * k.breite
ENDFUNCTION

LOCAL k AS KISTE
k.breite = 2
kopie = k
PRINT flaeche(kopie), 0, 0
END
""",
        )
        expect_clean(
            work,
            "mehrdeutige Variable wird nicht geprüft",
            """TYPE A
  ax AS FLOAT
ENDTYPE
TYPE B
  bx AS FLOAT
ENDTYPE

FUNCTION eins:
  LOCAL p AS A
  p.ax = 1
  RETURN 1
ENDFUNCTION

FUNCTION zwei:
  LOCAL p AS B
  p.bx = 2
  RETURN 2
ENDFUNCTION

PRINT eins() + zwei(), 0, 0
END
""",
        )

        # ----------------------------------------------------------- METHODen
        method_source = """TYPE KISTE
  w AS FLOAT
  h AS FLOAT

  METHOD flaeche:
    RETURN w * h
  ENDMETHOD

  METHOD skalieren: faktor
    w = w * faktor
    h = h * faktor
  ENDMETHOD

  METHOD doppelt:
    skalieren(2)
    RETURN flaeche()
  ENDMETHOD
ENDTYPE

LOCAL k AS KISTE
k.w = 2
k.skalieren(3)
PRINT k.flaeche(), k.doppelt(), 0, 0
END
"""
        expect_clean(work, "METHODen und Aufrufe", method_source)

        expect_error(
            work,
            "unbekannte Methode mit Vorschlag",
            method_source.replace("k.skalieren(3)", "k.skaliern(3)"),
            22,
            "Unbekannte Methode: SKALIERN (KISTE) - meintest du SKALIEREN?",
        )
        expect_error(
            work,
            "SELF ausserhalb einer METHOD",
            "TYPE T\n  x AS FLOAT\nENDTYPE\nLOCAL t AS T\nPRINT self.x, 0, 0\nEND\n",
            5,
            "Unbekannter Name: SELF",
        )
        expect_error(
            work,
            "unbekanntes Feld ueber SELF",
            method_source.replace("RETURN w * h", "RETURN self.nam"),
            6,
            "Unbekanntes Feld: NAM (KISTE)",
        )

        # ------------------------------------------------------------- ENUM
        enum_source = """ENUM PHASE
  STOP
  GET_READY
  GO
ENDENUM

GLOBAL phase%
phase% = PHASE.STOP
IF phase% = PHASE.GO THEN phase% = PHASE.GET_READY
PRINT STR$(phase%), 0, 0
END
"""
        expect_clean(work, "ENUM-Werte nur mit ENUM davor", enum_source)
        expect_error(
            work,
            "nackter ENUM-Wert mit Vorschlag",
            enum_source.replace("phase% = PHASE.STOP", "phase% = STOP"),
            8,
            "Unbekannter Name: STOP - meintest du PHASE.STOP?",
        )
        expect_clean(
            work,
            "gleiche Wertnamen in zwei ENUMs",
            """ENUM PHASE
  STOP
  GO
ENDENUM
ENUM AMPEL
  STOP = 5
  GO = 6
ENDENUM
PRINT STR$(PHASE.STOP) + STR$(AMPEL.STOP) + STR$(AMPEL.GO), 0, 0
END
""",
        )

        code, stderr, path = run_check(
            work, "TYPE T\n  x AS FLOAT\n  FUNCTION f:\n    RETURN 1\n  ENDFUNCTION\nENDTYPE\nEND\n"
        )
        check(
            code == 2 and f"{path}:3: Syntaxfehler:" in stderr and "METHOD" in stderr,
            "FUNCTION in einem TYPE verweist auf METHOD",
            stderr.strip() or f"Rueckgabe {code}",
        )

        # ------------------------------------------------------ mehrere Meldungen
        code, stderr, path = run_check(work, "a = b\nc = d\nEND\n")
        rows = [row for row in stderr.splitlines() if ": Fehler:" in row]
        check(
            len(rows) == 2 and code == 2,
            "alle unbekannten Namen werden gemeldet",
            f"{len(rows)} Meldungen, Rueckgabe {code}",
        )

        # ------------------------------------------------------------ INCLUDE
        with open(os.path.join(work, "werte.gbx"), "w", encoding="utf-8", newline="\n") as handle:
            handle.write("GLOBAL guthaben\nGLOBAL punkte\n")
        expect_clean(
            work,
            "INCLUDE liefert Definitionen",
            'INCLUDE "werte.gbx"\nguthaben = 3\nPRINT guthaben + punkte, 0, 0\nEND\n',
        )

        # ------------------------------------------------------------- INLINE
        expect_clean(
            work,
            "INLINE-Bloecke in FUNCTION und IF",
            """INLINE
GBX_INLINE_FN("INLINE_HALB", 1, 1, halb);
ENDINLINE

FUNCTION doppelt: wert
  INLINE DEFINE
GBX_INLINE_FN("INLINE_DOPPELT", 1, 1, doppelt);
  ENDINLINE
  RETURN INLINE_DOPPELT(wert) + INLINE_HALB(wert)
ENDFUNCTION

IF 1 = 1
  INLINE DEFINE
GBX_INLINE_FN("INLINE_EINS", 0, 0, eins);
  ENDINLINE
ENDIF

PRINT doppelt(2) + INLINE_EINS(), 0, 0
END
""",
        )

        expect_clean(
            work,
            "INLINE-Schritte im Code",
            """GLOBAL zaehler
zaehler = 0
INLINE RUN
  zaehler = zaehler + 1;
ENDINLINE

FUNCTION schritt:
  LOCAL wert
  wert = 0
  INLINE
    wert = zaehler * 2;
  ENDINLINE
  RETURN wert
ENDFUNCTION

PRINT schritt(), 0, 0
END
""",
        )

        # -------------------------------------------------------------- stdin
        code, stderr, path = run_check(work, "nurEinTippfehler\n", name="live.gbx", stdin=True)
        check(
            code == 2 and f"{path}:1: Fehler: Unbekannter Befehl: NUREINTIPPFEHLER" in stderr,
            "stdin nennt die echte Datei",
            stderr.strip() or f"Rueckgabe {code}",
        )

    print("GESAMT:", "OK" if failures == 0 else f"{failures} Fehler")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
