#!/usr/bin/env python3
"""Prueft den Laufzeit-Debugger von gbx (Haltepunkte, Einzelschritt, Werte).

Aufruf:  python tools/check_debug.py [pfad/zu/gbx.exe]
"""
import json
import os
import queue
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GBX = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "gbx.exe")
SAMPLE = os.path.join(ROOT, "tests", "debug_sample.gbx")
PREFIX = "@@gbxdbg "

failures = 0


def check(ok, name, detail=""):
    global failures
    print(f"{'OK  ' if ok else 'FEHL'} {name}" + (f": {detail}" if detail else ""))
    if not ok:
        failures += 1


class Session:
    """Startet den Interpreter und liest die Debug-Zeilen in einem Thread."""

    def __init__(self, script):
        self.proc = subprocess.Popen(
            [GBX, "--headless", "--debug", script],
            cwd=ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        self.lines = queue.Queue()
        self.stderr = []
        threading.Thread(target=self._read, args=(self.proc.stdout,), daemon=True).start()
        threading.Thread(target=self._read_err, daemon=True).start()

    def _read(self, stream):
        for line in stream:
            self.lines.put(line.rstrip("\r\n"))
        self.lines.put(None)

    def _read_err(self):
        for line in self.proc.stderr:
            self.stderr.append(line.rstrip())

    def send(self, command):
        try:
            self.proc.stdin.write(command + "\n")
            self.proc.stdin.flush()
        except OSError:
            pass

    def event(self, timeout=10.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                line = self.lines.get(timeout=0.2)
            except queue.Empty:
                continue
            if line is None:
                return None
            if not line.startswith(PREFIX):
                continue
            try:
                return json.loads(line[len(PREFIX):])
            except json.JSONDecodeError:
                continue
        return None

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait(timeout=10)


def wait_stop(session, timeout=10.0):
    """Wartet auf das naechste stop-Ereignis und ueberspringt Meldungen."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        event = session.event(max(0.1, deadline - time.time()))
        if event is None:
            return None
        if event.get("t") == "stop":
            return event
    return None


def variables(scope, kind):
    return {v["name"]: v["value"] for v in (scope or {}).get(kind, [])}


def test_session():
    session = Session(SAMPLE)
    try:
        ready = session.event()
        check(bool(ready and ready.get("t") == "ready"), "Debugger meldet sich", str(ready))

        # Ohne "run" haelt der Debugger an der ersten Anweisung an. Erst danach
        # freigeben, damit kein zweites "run" die Sitzung weiterlaufen laesst.
        session.send(f"bp\t{SAMPLE}\t7")
        stop = wait_stop(session)
        check(bool(stop and stop.get("reason") == "start"), "Einstieg haelt an", str(stop and stop.get("reason")))
        check(bool(stop) and stop.get("line") == 2, "Einstieg ist die erste Anweisung", str(stop and stop.get("line")))
        session.send("run")
        stop = wait_stop(session)

        check(bool(stop), "Haltepunkt wird erreicht", str(stop))
        if not stop:
            return
        check(stop.get("reason") == "breakpoint", "Grund ist der Haltepunkt", stop.get("reason"))
        check(stop.get("line") == 7, "Zeile des Haltepunkts", stop.get("line"))
        check((stop.get("scope") or {}).get("func") == "ADD", "Funktion heisst ADD",
              (stop.get("scope") or {}).get("func"))
        check(variables(stop.get("scope"), "locals").get("TOTAL") == "3",
              "lokale Variable TOTAL", str(variables(stop.get("scope"), "locals")))
        check(variables(stop.get("scope"), "globals").get("X") == "1",
              "globale Variable X", str(variables(stop.get("scope"), "globals")))

        # Einzelschritt ueber RETURN hinaus: naechste Anweisung im Hauptprogramm.
        session.send("next")
        step = wait_stop(session)
        check(bool(step and step.get("reason") == "step"), "Einzelschritt haelt an",
              str(step and step.get("reason")))
        check(bool(step) and step.get("line") == 11, "Einzelschritt landet in Zeile 11",
              str(step and step.get("line")))
        check(bool(step) and (step.get("scope") or {}).get("func") == "",
              "zurueck im Hauptprogramm", str(step and (step.get("scope") or {}).get("func")))

        session.send("continue")
        exit_event = session.event()
        check(bool(exit_event and exit_event.get("t") == "exit"), "Sitzung meldet Ende", str(exit_event))
        check(bool(exit_event) and exit_event.get("code") == 0, "Rueckgabecode 0",
              str(exit_event and exit_event.get("code")))
        check(session.proc.wait(timeout=10) == 0, "Prozess endet mit 0")
    finally:
        session.close()


def test_condition():
    session = Session(SAMPLE)
    try:
        session.event()
        # Bedingter Haltepunkt in der FOR-Schleife (Zeile 12).
        session.send(f"bp\t{SAMPLE}\t12\tI >= 2")
        session.send("run")

        stop = wait_stop(session)
        if stop and stop.get("reason") == "start":
            session.send("run")
            stop = wait_stop(session)
        check(bool(stop and stop.get("line") == 12), "Bedingter Haltepunkt greift", str(stop and stop.get("line")))
        check(bool(stop) and stop.get("condition") == "I >= 2", "Bedingung wird gemeldet",
              str(stop and stop.get("condition")))
        check(variables(stop.get("scope"), "globals").get("I") == "2",
              "erster Treffer erst bei I=2", str(variables(stop.get("scope"), "globals").get("I")))

        session.send("continue")
        second = wait_stop(session)
        check(bool(second and second.get("line") == 12), "zweiter Treffer", str(second and second.get("line")))
        check(bool(second) and variables(second.get("scope"), "globals").get("I") == "3",
              "zweiter Treffer bei I=3", str(variables(second.get("scope"), "globals").get("I")))

        session.send("continue")
        exit_event = session.event()
        check(bool(exit_event and exit_event.get("t") == "exit" and exit_event.get("code") == 0),
              "Sitzung endet nach der Schleife", str(exit_event))
    finally:
        session.close()


def test_broken_condition():
    session = Session(SAMPLE)
    try:
        session.event()
        session.send(f"bp\t{SAMPLE}\t12\tI >>> 2")
        session.send("run")
        stop = wait_stop(session)
        if stop and stop.get("reason") == "start":
            session.send("run")
            stop = wait_stop(session)
        check(bool(stop and stop.get("line") == 12), "fehlerhafte Bedingung haelt an",
              str(stop and stop.get("line")))
        check(bool(stop) and stop.get("conditionError"), "Fehler der Bedingung wird gemeldet",
              str(stop and stop.get("conditionError")))
        session.send("quit")
    finally:
        session.close()


INSPECT_SCRIPT = """' Wert-Tooltip im Haltepunkt (eval)\nTYPE PUNKT\n  px AS FLOAT\n  py AS FLOAT\n  name$ = "punkt"\nENDTYPE\n\nGLOBAL spieler AS PUNKT\nDIM zahlen[3]\nDIM leute[2] AS PUNKT\nzahlen[0] = 4\nzahlen[1] = 5\nzahlen[2] = 6\nspieler.px = 12.5\nspieler.py = 7\nleute[1].px = 99\nhalt = 1\n"""


def test_inspect():
    """Prueft den Wert-Tooltip: Variablen, Felder, Arrays und TYPEs."""
    handle, path = tempfile.mkstemp(suffix=".gbx", dir=os.path.join(ROOT, "tests"))
    os.close(handle)
    with open(path, "w", encoding="utf-8") as f:
        f.write(INSPECT_SCRIPT)
    session = Session(path)
    try:
        session.event()
        # Letzte Zeile: dort sind alle Variablen angelegt.
        session.send(f"bp\t{path}\t17")
        stop = wait_stop(session)
        if stop and stop.get("reason") == "start":
            session.send("run")
            stop = wait_stop(session)
        check(bool(stop and stop.get("line") == 17), "Haltepunkt fuer den Tooltip", str(stop and stop.get("line")))
        if not stop:
            return

        def evaluate(ident, expression):
            session.send(f"eval\t{ident}\t{expression}")
            deadline = time.time() + 10
            while time.time() < deadline:
                event = session.event(max(0.1, deadline - time.time()))
                if event is None:
                    return None
                if event.get("t") == "eval" and str(event.get("id")) == str(ident):
                    return event
            return None

        record = evaluate(1, "spieler")
        check(bool(record and record.get("ok")), "TYPE wird ausgewertet", str(record))
        check(bool(record) and record.get("type") == "PUNKT", "Typ des TYPE", str(record and record.get("type")))
        detail = (record or {}).get("detail", "")
        check("PX = 12.5" in detail and "PY = 7" in detail,
              "Felder des TYPE stehen im Tooltip", detail)
        check('NAME = "punkt"' in detail, "Stringfeld mit Anfuehrungszeichen", detail)

        array = evaluate(2, "zahlen")
        check(bool(array) and array.get("type") == "ARRAY[3]", "Array mit Laenge",
              str(array and array.get("type")))
        check("[0] = 4" in (array or {}).get("detail", "") and "[2] = 6" in (array or {}).get("detail", ""),
              "Array-Elemente stehen im Tooltip", str((array or {}).get("detail")))

        records = evaluate(3, "leute")
        detail = (records or {}).get("detail", "")
        check("[1] · TYPE PUNKT" in detail, "TYPE-Array wird aufgeklappt", detail)
        check("PX = 99" in detail, "Feld des zweiten Elements", detail)

        field_expr = evaluate(4, "spieler.px")
        check(bool(field_expr) and field_expr.get("detail") == "12.5", "Feld ueber Punkt gelesen",
              str(field_expr and field_expr.get("detail")))
        check(bool(field_expr) and field_expr.get("expr") == "spieler.px", "Ausdruck kommt zurueck",
              str(field_expr and field_expr.get("expr")))

        math_expr = evaluate(5, "zahlen[1] + 1")
        check(bool(math_expr) and math_expr.get("detail") == "6", "Ausdruck mit Index",
              str(math_expr and math_expr.get("detail")))

        unknown = evaluate(6, "quatsch")
        check(bool(unknown) and unknown.get("ok") is False, "unbekannter Name wird abgelehnt", str(unknown))
        keyword = evaluate(7, "PRINT")
        check(bool(keyword) and keyword.get("ok") is False, "Befehl wird nicht ausgewertet", str(keyword))

        # Ein Wert darf nicht als lokale Variable angelegt werden.
        session.send("vars")
        scope = None
        deadline = time.time() + 10
        while time.time() < deadline:
            event = session.event(max(0.1, deadline - time.time()))
            if event is None:
                break
            if event.get("t") == "scope":
                scope = event.get("scope")
                break
        check("QUATSCH" not in variables(scope, "globals") and "QUATSCH" not in variables(scope, "locals"),
              "unbekannter Name bleibt ohne Nebenwirkung", str(scope))
        session.send("quit")
    finally:
        session.close()
        try:
            os.remove(path)
        except OSError:
            pass


METHOD_SCRIPT = """' Felder einer METHOD im Tooltip\nTYPE BOX\n  w AS FLOAT\n  h AS FLOAT\n  METHOD area\n    flaeche = w * h\n    RETURN flaeche\n  ENDMETHOD\nENDTYPE\n\nLOCAL b AS BOX\nb.w = 4\nb.h = 2\nr = b.area()\nPRINT r, 0, 0\n"""


def test_inspect_method():
    """In einer METHOD sind die eigenen Felder wie Variablen sichtbar."""
    handle, path = tempfile.mkstemp(suffix=".gbx", dir=os.path.join(ROOT, "tests"))
    os.close(handle)
    with open(path, "w", encoding="utf-8") as f:
        f.write(METHOD_SCRIPT)
    session = Session(path)
    try:
        session.event()
        session.send(f"bp\t{path}\t6")
        stop = wait_stop(session)
        if stop and stop.get("reason") == "start":
            session.send("run")
            stop = wait_stop(session)
        check(bool(stop and (stop.get("scope") or {}).get("func") == "BOX.AREA"),
              "Haltepunkt in der METHOD", str(stop and (stop.get("scope") or {}).get("func")))
        if not stop:
            return

        def evaluate(ident, expression):
            session.send(f"eval\t{ident}\t{expression}")
            deadline = time.time() + 10
            while time.time() < deadline:
                event = session.event(max(0.1, deadline - time.time()))
                if event is None:
                    return None
                if event.get("t") == "eval" and str(event.get("id")) == str(ident):
                    return event
            return None

        field = evaluate(1, "w")
        check(bool(field) and field.get("detail") == "4", "Feld der METHOD als Variable", str(field))
        own = evaluate(2, "self")
        check(bool(own) and own.get("type") == "BOX", "self ist das eigene Objekt", str(own))
        check("H = 2" in (own or {}).get("detail", ""), "Felder des eigenen Objekts",
              str((own or {}).get("detail")))
        session.send("quit")
    finally:
        session.close()
        try:
            os.remove(path)
        except OSError:
            pass


def test_syntax_error():
    handle, path = tempfile.mkstemp(suffix=".gbx", dir=os.path.join(ROOT, "tests"))
    os.close(handle)
    with open(path, "w", encoding="utf-8") as f:
        f.write("x = \n")
    session = Session(path)
    try:
        ready = session.event()
        check(bool(ready and ready.get("t") == "ready"), "Debugger meldet sich (Fehlerfall)", str(ready))
        error = session.event()
        check(bool(error and error.get("t") == "error"), "Syntaxfehler wird gemeldet", str(error))
        check(bool(error) and error.get("compile") is True, "Fehler gilt als Syntaxfehler",
              str(error and error.get("compile")))
        check(bool(error) and error.get("line") == 1, "Fehlerzeile ist 1", str(error and error.get("line")))
        exit_event = session.event()
        check(bool(exit_event and exit_event.get("t") == "exit" and exit_event.get("code") == 2),
              "Fehlerfall endet mit Code 2", str(exit_event))
    finally:
        session.close()
        try:
            os.remove(path)
        except OSError:
            pass


def main():
    if not os.path.exists(GBX):
        print(f"FEHL Interpreter fehlt: {GBX}")
        return 1
    test_session()
    test_condition()
    test_broken_condition()
    test_inspect()
    test_inspect_method()
    test_syntax_error()
    print("GESAMT:", "OK" if failures == 0 else f"{failures} Fehler")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
