# GBX Studio

Die IDE für **GameBasiX** – ein Editor mit Befehlsreferenz, Autovervollständigung,
Syntaxprüfung und direktem Start des Interpreters. Projekte bestehen aus
`.gbx`-Quellen und einer `.gbxproj`-Datei; gestartet wird mit `gbx.exe`,
eine Lizenzprüfung gibt es nicht.

## Starten

Voraussetzungen: Node.js 20 oder neuer, der gebaute Interpreter
`../build/gbx.exe` (siehe [README im Projektordner](../README.md)) und einmalig
installierte Abhängigkeiten:

```bash
cd ide
npm install
npm start          # Desktop-Fenster (Electron)
```

Danach genügt ein Doppelklick auf **`GBXStudio.exe`** im Projektordner: Die
schmale EXE ist nur ein Startprogramm, sucht Electron neben sich und öffnet
das Editorfenster. Fehlt Electron, startet sie den Webserver und öffnet den
Browser. Sie baut mit dem übrigen Projekt (`cmake --build build`) und landet
direkt im Projektordner.

Ohne Electron im Browser:

```bash
npm run web        # öffnet http://127.0.0.1:3417/
```

Beim ersten Start sucht die IDE `build/gbx.exe` neben dem IDE-Ordner. Ein
anderer Pfad lässt sich unter **⚙ Einstellungen → Allgemein** eintragen und mit
**Prüfen** testen. Dort steht auch der C++-Compiler für `INLINE`. Beide Felder
zeigen ihren Standard als relativen Pfad ab dem Ordner der Editor-EXE
(`build\gbx.exe`, `compiler\bin\g++.exe`) und darunter den aufgelösten Pfad; im
Paket liegt der Compiler also unter `compiler\bin\g++.exe`, ohne weitere
Installation. Absolute Pfade gehen genauso. Der Server gibt den Compiler als
`GBX_CXX` an jeden Start des Interpreters weiter.

Ordner lassen sich beim Öffnen und beim Anlegen eines Projekts direkt im
Dialog durchsuchen (Pfeil nach oben, Laufwerke, Doppelklick auf eine
`.gbxproj`-Datei öffnet sie). Im Desktop-Fenster übernimmt das der
Windows-Dialog.

**HTML5**: Zielplattform **HTML5 (Browser)** im Dropdown neben **Bauen** wählen
und bauen – das schreibt einen Ordner `<projekt>-web` mit `index.html`, `gbx.js`
(Interpreter als WebAssembly) und `game.js` (Quellen und Assets). Ein Doppelklick auf `index.html` startet das
Spiel im Browser, ein Webserver geht auch. Voraussetzung ist der einmalige
wasm-Build des Interpreters; dafür steht in **Einstellungen → Allgemein →
HTML5** ein eigener Abschnitt bereit: **Herunterladen …** holt das Emscripten
SDK in den Ordner `emsdk` neben der Editor-EXE (einige hundert MB, dauert ein
paar Minuten), **Prüfen** zeigt die Version, **Player bauen** übersetzt `src/`
nach `html5/gbx.js`. Liegt `html5/gbx.js` schon im Paket, exportiert die IDE
auch ohne SDK. Projekte mit `INLINE`-Blöcken bekommen beim Export einen
eigenen Player: die Blöcke werden dort mit Emscripten übersetzt und fest
dazugelinkt (dauert rund 30 s, braucht das SDK). Im Browser selbst gibt es
weder Compiler noch `dlopen`.

**Google Fonts**: **Werkzeuge → Google Fonts …** durchsucht die Schriften von
fonts.google.com, zeigt sie im gewählten Schnitt und in der eingestellten
Größe und lädt die `.ttf` in den Asset-Ordner des Projekts (Vorgabe `assets/`).
Der Dialog nennt Dateinamen und passende `LOADFONT`-Zeile; der Befehl lässt sich
kopieren. Ohne API-Key liest die IDE die öffentliche Fontliste und holt die
Datei über die CSS-Schnittstelle von Google. Ein Key aus der Google-Cloud-
Konsole lässt sich unter **Einstellungen → Allgemein → Google Fonts (API-Key)**
eintragen; dann kommt die Liste über die Webfonts-API. Der Key bleibt in den
lokalen Einstellungen (`config.json`) und wird nicht an die Oberfläche
zurückgegeben. Die Lizenz einer Schrift (meist OFL oder Apache) steht auf ihrer
Seite bei Google Fonts. TrueType-Schriften (`LOADFONT "x.ttf", …`) laufen
über den FreeType-Port von Emscripten; die Datei muss dafür im Projekt liegen
(Ordner `assets/` oder Pfad in der `.gbxproj`), weil es im Browser kein
Windows-Schriftverzeichnis gibt.

## Arbeiten mit der IDE

| Aktion | Taste |
| --- | --- |
| Syntax prüfen | `F7` |
| Programm starten | `F5` |
| Laufendes Programm beenden | `Shift+F5` |
| Debugger starten | `F6` |
| Weiter bis zum Haltepunkt | `F8` |
| Haltepunkt setzen/entfernen | `F9` oder Klick in den Rand |
| Nächste Zeile (Step over) | `F10` |
| In Funktion springen (Step into) | `F11` |
| Aus Funktion heraus (Step out) | `Shift+F11` |
| Debug-Sitzung beenden | `Shift+F6` |
| Speichern | `Strg+S` |
| Projekt öffnen | `Strg+O` |
| Neue Datei | `Strg+N` |
| Vervollständigung | `Strg+Leertaste` |
| Referenz ein-/ausblenden | `Strg+B` |
| Spiel bauen (gewählte Plattform) | `Strg+E` |

**Prüfen** ruft `gbx --check` auf. Es wird kein C++ übersetzt: gbx liest die
Quellen bei jedem Start neu. Meldungen erscheinen unter **Probleme** und als
Marker im Editor; ein Klick springt in die Zeile. **Starten** speichert alle
Dateien und startet den Interpreter im Projektordner. Ausgaben von `DEBUG`,
`STDOUT` und `STDERR` sowie Laufzeitfehler stehen in der **Konsole**.

**Bauen** (oder `Strg+E`) baut für die im Dropdown gewählte Plattform. Bei
**Windows-EXE** stecken Quellen und die übersetzte INLINE-Bibliothek in einer
einzelnen EXE, **Assets werden daneben kopiert**.
Im Desktop-Fenster fragt die IDE nach dem Speicherort, im Browser entsteht die
Datei im Projektordner. Für eine EXE ohne fremde DLLs in den Einstellungen auf
`build-static/gbx.exe` zeigen (gebaut mit `-DGBX_STATIC_RUNTIME=ON`).

Ein Projekt lässt sich als Ordner, als `.gbxproj` oder als einzelne `.gbx`
öffnen. Weitere Quelldateien werden im Dateibaum angelegt und mit
`INCLUDE "lib.gbx"` eingebunden.

Zwischen `INLINE` und `ENDINLINE` steht C++ (siehe
[README im Projektordner](../README.md#inline-c-im-basic-programm)). Die IDE
hebt den Block als eigenen Bereich hervor und prüft seine Zeilen nicht als
BASIC; **Prüfen** sammelt die Namen ein, die der Block – auch über eingebundene
Header – mit `GBX_INLINE_FN` anmeldet. Ein Block im Code (`FUNCTION`, `IF`,
Schleife oder `INLINE RUN`) läuft beim **Starten** an seiner Stelle und sieht
die BASIC-Variablen dort (Namen klein, Texte mit `_str`). Arrays lassen sich
dort auch in der Größe ändern (`push`, `insert`, `remove`, `clear`, `resize`
– wie `GBXArray`, aber direkt auf dem BASIC-Array), und die eingebauten
GBX-Befehle (`PRINT`, `DRAWLINE`, `GETTIMER` …) ruft der Schritt direkt auf.

## Debugger

`F6` speichert das Projekt und startet den Interpreter mit `--debug`. Die
Debug-Leiste unter den Tabs zeigt den Zustand; bedient wird sie mit `F8`, `F10`,
`F11`, `Shift+F11` oder per Klick. Während das Programm läuft, lässt es sich mit
**Pause** beim nächsten Befehl anhalten.

* **Haltepunkte** setzt und entfernt man mit `F9` oder per Klick in den linken
  Rand. Sie gehören zum Projekt und bleiben über Sitzungen hinweg erhalten.
  Während einer Sitzung gesetzte Haltepunkte gelten sofort.
* **Bedingte Haltepunkte**: Das Stiftsymbol neben einem Haltepunkt im Panel
  öffnet ein Feld für einen GBX-Ausdruck (z. B. `LEBEN = 0` oder `I >= 4`). Der
  Haltepunkt hält dann nur an, wenn der Ausdruck im aktuellen Bereich wahr ist;
  bedingte Haltepunkte erscheinen als Raute. Ein nicht lesbarer Ausdruck wird
  gemeldet und hält an, statt still nie zu greifen.
* Das Panel **Debugger** zeigt beim Anhalten die lokalen Variablen der aktuellen
  Funktion, die globalen Variablen, die Aufrufliste und alle Haltepunkte des
  Projekts. Ein Klick auf einen Haltepunkt springt in die Zeile.
* Die angehaltene Zeile wird im Editor markiert; die Datei öffnet sich
  automatisch, auch wenn der Haltepunkt in einer eingebundenen Datei liegt.

Der Debugger hängt direkt am Interpreter: gbx hält vor jeder Anweisung an und
meldet Position und Werte als JSON-Zeilen auf der Standardausgabe (Vorsilbe
`@@gbxdbg `), Befehle kommen zeilenweise über die Standardeingabe. Ein eigenes
Debug-Backend oder GDB ist dafür nicht nötig.

**Syntaxfehler** fallen schon vor dem Start auf: Der Debugger läuft bereits
während des Parsens, meldet den Fehler mit Datei und Zeile an die IDE und
beendet die Sitzung. Die Meldung erscheint unter **Probleme** und als Marker im
Editor – mit derselben Aufbereitung wie bei `F7`, inklusive Hinweis auf einen
unbekannten Befehl.

## Projektdatei

Die IDE schreibt beim Speichern eine `.gbxproj` im INI-Stil. Sie ist dieselbe
Datei, die `gbx.exe` liest:

```ini
[project]
name = "Mein Spiel"
main = "main.gbx"

[window]
title = "Mein Spiel"
width = 800
height = 600
fullscreen = 0
resizable = 1

[graphics]
fps = 60
vsync = 1

[audio]
enabled = 1
volume = 1

[assets]
root = "assets"
```

Die Felder stehen auch unter **Einstellungen → Projekt** zur Verfügung. Der
Editorzustand (offene Reiter) liegt zusätzlich in `.gbxide/project.json`; dieser
Ordner gehört nicht ins Projektarchiv.

## Befehlsreferenz und Vervollständigung

`data/gbx-dataset.json` enthält alle von GBX unterstützten Befehle,
Schlüsselwörter und `KEY_*`-Konstanten mit Signatur, Beschreibung und Beispiel.

Jeder Eintrag beschreibt auf **Deutsch und Englisch**, was der Befehl tut; die
Texte stehen in `data/gbx-descriptions.json` und sind eigene Formulierungen,
keine Handbuchzitate. Angezeigt wird die Sprache, die gerade gewählt ist – im
DE/EN-Schalter des Referenzpanels, im Hover und in der Vervollständigung.
Der Schalter oben rechts (**DE**/**EN**) wechselt die ganze Oberfläche mit –
auch die Meldungen von Interpreter und IDE-Konsole, weil die IDE `GBX_LANG`
passend setzt. Auch die Argumentnamen der Signatur wechseln mit
(`farbe` → `colour`).

## Farbfelder an RGB()

Stehen bei `RGB(r, g, b)` drei Zahlen – auch `0x…` –, zeigt der Editor vor dem
Aufruf ein kleines Kästchen in genau dieser Farbe. Zeigen oder Klicken darauf
öffnet Monacos Farbwähler; die gewählte Farbe landet sofort im Quelltext, und
Strg+Z nimmt sie wieder zurück. Kommentare und Zeichenketten
bekommen kein Feld, ebenso `RGB(x, y, z)` mit Variablen – deren Farbe steht
erst beim Start des Programms fest.

Sprachdetails (0-basierte String-Funktionen, inklusives `RND`, SDL-Scancodes,
WAV-only-Musik, `PRESCALER`) sind im jeweiligen Eintrag und in der Übersicht
„Sprachdetails“ vermerkt.

Datensatz neu erzeugen (nach Änderungen an der Befehlsliste im Haupt-README):

```bash
npm run build:dataset
```

## Was gegenüber dem alten Studio entfallen ist

* **Premium-Lizenz** – GameBasiX hat keine Lizenzstufen, eine Lizenzdatei und die
  Lizenzseite sind vollständig entfernt.
* **Compiler-Anbindung** – kein SDK, kein `gpc`, kein Linken, keine
  Plattform-/`cmp`-/`lnk`-Flags.
* **KeyHit-Werkzeug** – `KEY()` nimmt SDL-Scancodes; die `KEY_*`-Konstanten
  stehen in der Referenz.

Erhalten geblieben sind Editor, Themes, Dateibaum, Vorlagen, Medienvorschau,
Referenzpanel, Outline und die Analyse eigener FUNCTION/TYPEs. Schriften holt
**Werkzeuge → Google Fonts …** als TrueType-Datei direkt ins Projekt; einen
eigenen Bitmap-Generator gibt es nicht mehr.

## Tests

```bash
npm test
```

Der Test startet den Server, legt ein Projekt an, prüft eine gültige und eine
fehlerhafte Datei, startet ein kurzes Programm headless und kontrolliert
Exit-Code und Ausgabe. Anschließend läuft eine Debug-Sitzung über die API:
Haltepunkt, lokale Werte, Einzelschritt und Sitzungsende. Voraussetzung ist ein
gebauter `../build/gbx.exe`.

Das Debug-Protokoll selbst prüft `tools/check_debug.py` (auch Teil von
`ctest`): Einstiegshalt, Haltepunkt, Einzelschritt, lokale und globale Werte,
Sitzungsende und die Meldung eines Syntaxfehlers.

## Aufbau

```
src/main/       Electron-Hülle (Fenster, Dialoge)
src/server/     HTTP-API, Projektverwaltung, gbx-Anbindung
src/renderer/   Oberfläche (Monaco, Docks, Referenz, Tools)
scripts/        Datensatz-Generator und Ende-zu-Ende-Test
data/           GBX-Befehlsdatensatz, eigene Beschreibungen (de/en) und Herkunftsseiten
```
