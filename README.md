# GameBasiX

Ein BASIC-Interpreter für 2D-Spiele, gebaut auf **SDL3**. Alles rund um 3D,
Netzwerk, C++-Inline-Code und Datei-Streams ist bewusst nicht enthalten.

* Sprache: BASIC mit `$`/`%`/`#`-Suffixen, `FUNCTION`/`SUB`, `TYPE` mit
  `METHOD`en, Arrays, `SELECT CASE`, `TRY`/`THROW`
* Laufzeit: C++17-Interpreter (Lexer → Parser → AST), kein Compiler-Zwischenschritt
* Grafik: SDL3-Renderer, PNG/JPG/BMP-Sprites, Rotation/Zoom, Polygone,
  Render-Targets, Software-Alphamodus
* Text: eingebaute 8×8-Bitmap-Schrift, Bitmap-Fonts im 16×8-Raster und
  **TrueType über FreeType** (optional)
* Sound: kleiner Software-Mixer für WAV-Sounds und Musik
* Fenster, Tastatur, Maus, Timer, Spielschleife

## Bauen

Voraussetzungen (Windows, MSYS2 UCRT64): `g++`, `cmake`, `ninja`, Internet beim
ersten Konfigurieren (SDL3 wird per `FetchContent` geholt).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Ergebnis: `build/gbx.exe` und `GBXStudio.exe` im Projektordner. Das kleine
Startprogramm öffnet den Editor aus `ide/` per Doppelklick (siehe [IDE](#ide)).

TTF-Schriften werden über FreeType eingebunden, wenn es gefunden wird
(MSYS2: `pacman -S mingw-w64-ucrt-x86_64-freetype`). Fehlt FreeType, baut alles
trotzdem, nur ohne TTF:

```bash
cmake -S . -B build -G Ninja -DGBX_WITH_TTF=OFF
```

Ein bereits installiertes SDL3 lässt sich mit `-DGBX_USE_SYSTEM_SDL=ON` nutzen.

Für eine Weitergabe ohne Beigaben baut `-DGBX_STATIC_RUNTIME=ON` die MinGW-Laufzeit
und FreeType statisch ein. `gbx.exe` und `GBXStudio.exe` brauchen dann keine
`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll` und keine
`libfreetype-6.dll` mehr – nur Windows 10/11:

```bash
cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release -DGBX_STATIC_RUNTIME=ON
cmake --build build-static
```

## Starten

```bash
build/gbx.exe examples/01_hello/hello.gbxproj   # Projekt starten
build/gbx.exe examples/02_breakout/main.gbx     # einzelne Quelldatei
build/gbx.exe --headless tests/language_tests.gbx
build/gbx.exe --lang en --check main.gbx        # Meldungen auf Englisch
```

Meldungen (Syntaxfehler, Laufzeitfehler, Namensprüfung, Hilfe) gibt es auf
Deutsch und Englisch: `--lang de|en` oder die Umgebungsvariable `GBX_LANG`.
Die IDE setzt `GBX_LANG` selbst, passend zur eingestellten Sprache.

| Option | Wirkung |
| --- | --- |
| `--headless` | ohne Fenster und Ton; Grafikbefehle werden zu No-Ops |
| `--tests` | wie `--headless`, zählt `ASSERT` und gibt eine Zusammenfassung aus |
| `--check` | parsen und Namen prüfen, nichts ausführen |
| `--syntax-only` | wie `--check`, aber ohne Namensprüfung (für einzelne Teildateien) |
| `--stdin` | Quelle aus der Standardeingabe lesen (nur mit `--check`) |
| `--base <ordner>`, `--name <datei>` | Bezugsordner für `INCLUDE` und Dateiname in Meldungen bei `--stdin` |
| `--inline-cpp <datei>` | erzeugtes C++ der `INLINE`-Blöcke schreiben (der HTML5-Export linkt es mit Emscripten dazu) |
| `--debug` | Laufzeit-Debugger über stdin/stdout (Haltepunkte, Einzelschritte, Variablen) |
| `--version`, `-h/--help` | Version bzw. Hilfe |

Rückgabecodes: `0` ok, `1` fehlgeschlagene Asserts, `2` Syntax-/Laufzeitfehler,
`3` interner Fehler.

`--check` prüft nicht nur die Syntax, sondern auch die Namen: Was das Programm
nirgends definiert, ist ein Fehler. Gemeldet werden unbekannte Befehle
(`bla`), Funktionen (`x = NOPE(1)`), Variablen (`PRINT foo`), Marken (`GOTO
spaeter`) und Unterprogramme (`GOSUB fehlt`). Bekannt ist alles, was im
Programm zugewiesen, deklariert, als Parameter geführt oder eingebaut ist –
auch Definitionen aus `INCLUDE`-Dateien. Ausgabeparameter wie `INPUT name$`
legen ihre Variable dabei selbst an. Beim Ausführen bleibt GBX nachsichtig:
Eine nie zugewiesene Variable liefert dort weiter den Standardwert.

Auch die Felder von TYPEs werden geprüft: Ist der TYPE einer Variablen
bekannt (`LOCAL p AS PUNKT`, `DIM feld[2] AS PUNKT`, Parameter `p AS PUNKT`,
auch über `q = p` und verschachtelte TYPEs), muss `p.x` ein Feld dieses TYPEs
sein. `p.nam$` ergibt dann `Unbekanntes Feld: NAM (PUNKT) – meintest du
NAME?`. Ist der TYPE nicht eindeutig – etwa weil derselbe Name in zwei
Funktionen unterschiedlich deklariert ist – bleibt die Prüfung still, statt
zu raten.

## Eigenes Dateiformat

Statt `.gbas` benutzt GBX zwei klar getrennte Dateien:

* **`.gbx`** – Quelltext (UTF-8, eine Anweisung pro Zeile, `'` oder `//` als Kommentar)
* **`.gbxproj`** – Projektdatei im INI-Stil: Fenster, Einstiegspunkt, FPS, Asset-Ordner

Beispiel `hello.gbxproj`:

```ini
[project]
name = "Hallo Welt"
main = "main.gbx"          ; Einstiegspunkt relativ zur Projektdatei

[window]
title = "Mein Spiel"
width = 800
height = 600
fullscreen = 0
resizable = 1

[graphics]
fps = 60                   ; 0 = unbegrenzt
vsync = 1

[audio]
enabled = 1
volume = 1.0

[assets]
root = "assets"            ; zusaetzlicher Suchpfad fuer Bilder/Sounds
```

Asset-Pfade werden in dieser Reihenfolge gesucht: Projektverzeichnis,
`Projektverzeichnis/assets`, aktuelles Verzeichnis. TTF-Namen ohne Pfad werden
zusätzlich in `%WINDIR%\Fonts` gesucht (`"arial.ttf"` funktioniert also direkt).

Ein Projekt kann aus mehreren Dateien bestehen: `INCLUDE "lib.gbx"` bindet
weitere Quellen ein (Pfad relativ zur einbindenden Datei, Mehrfach-Include wird
ignoriert).

## Sprache in Kürze

```basic
' Kommentar
CONSTANT BREITE = 800

TYPE PUNKT
  x AS FLOAT
  y AS FLOAT
  leben% = 3                 ' Standardwert je Instanz
ENDTYPE

GLOBAL punkte AS PUNKT

FUNCTION laenge: a AS PUNKT, b AS PUNKT
  dx = b.x - a.x
  dy = b.y - a.y
  RETURN SQR(dx * dx + dy * dy)
ENDFUNCTION

DIM feld[10]              ' Indizes 0..9
feld[0] = 5
feld[-1] = 7              ' letztes Element

FOR i = 10 TO 1 STEP -2
  feld[i / 2] = i
NEXT

SELECT feld[0]
CASE 1, 2
  DEBUG "klein" + CHR$(10)
CASE 3 TO 9
  DEBUG "mittel" + CHR$(10)
DEFAULT
  DEBUG "gross" + CHR$(10)
ENDSELECT
```

**Werte und Typen.** Zahlen sind standardmäßig `FLOAT`, ganzzahlig mit `%`,
Strings mit `$`; `#` erzwingt `FLOAT`. Bei der Zuweisung wandelt das Suffix um
(`x$ = 5` ergibt `"5"`). `TRUE`/`FALSE` sind `1`/`0`.

**Operatoren.** `+ - * /`, Vergleiche `= <> < <= > >=`, `AND`, `OR`, `NOT`.
`/` liefert immer eine Kommazahl, `MOD(a,b)` den ganzzahligen Rest.

**Kontrollfluss.** `IF/THEN/ELSE/ELSEIF/ENDIF` (auch einzeilig), `FOR ... TO ...
STEP`, `WHILE/WEND`, `REPEAT/UNTIL`, `SELECT/CASE/DEFAULT/ENDSELECT`, `BREAK`,
`CONTINUE`, `GOTO marke:` innerhalb eines Abschnitts, `GOSUB sub`.

**Funktionen.** `FUNCTION name: p1, p2 = 0 … ENDFUNCTION` mit `RETURN`, `EXIT`
für vorzeitiges Verlassen, `STATIC` für persistente Locals, `BYREF` für
Referenzparameter (auch auf Array-Elemente und TYPE-Felder). `SUB name: …
ENDSUB` ist eine parameterlose Prozedur für `GOSUB`.

**Arrays.** `DIM a[10]`, mehrdimensional bis 4 Dimensionen, `DIM a[10] AS TYP`,
`REDIM` (erhält Daten; bei TYPE-Arrays `REDIM a[10] AS TYP`), `DIMPUSH`,
`DIMDEL`, `DIMDATA`, `BOUNDS(a[], dim)`,
`LEN(a[])`, `FOREACH x IN a[] … NEXT` (BYREF auf das Element).

**TYPE.** Felder ohne Suffix sind `FLOAT`, `$` macht Strings, `AS AndererTyp`
verschachtelt. `LOCAL v AS TYP`, `GLOBAL v AS TYP`, `DIM arr[5] AS TYP`.
Felder dürfen einen Standardwert tragen: `leben% = 3`, `name$ = "neu"`, auch
`gesamt% = a + b` oder eine Konstante. Der Ausdruck läuft bei jeder neuen
Instanz – für `LOCAL`, `GLOBAL` und jedes Element eines TYPE-Arrays.
`TYPE`-Werte werden als Referenz geteilt (Felder und Arrays sind Zeiger).

**Methoden.** Ein `TYPE` kann `METHOD`en tragen. Sie stehen im TYPE-Block und
werden mit `objekt.name(...)` aufgerufen:

```basic
TYPE KISTE
  w AS FLOAT
  h AS FLOAT

  METHOD flaeche
    RETURN w * h
  ENDMETHOD

  METHOD skalieren: faktor
    w = w * faktor
    h = h * faktor
  ENDMETHOD
ENDTYPE

LOCAL k AS KISTE
k.w = 4
k.h = 2
PRINT k.flaeche(), 0, 0     ' 8
k.skalieren(3)
PRINT k.w, 0, 0             ' 12
```

Im Rumpf sind die Felder des eigenen TYPEs wie Variablen sichtbar; Zuweisungen
schreiben direkt in das Objekt. `self` steht für das Objekt selbst, also
`self.x` und `self.flaeche()` – praktisch, um das eigene Objekt an eine andere
Funktion zu übergeben. Parameter und `LOCAL` verdecken ein Feld mit demselben
Namen. Ohne Objekt geschriebene Methodennamen gelten als `self.method(...)` –
`skalieren(2)` ruft also die eigene Methode auf. Methoden funktionieren auf
Variablen, TYPE-Feldern und Elementen von TYPE-Arrays. `FUNCTION` und `SUB`
innerhalb eines TYPEs lehnt der Parser mit einem Hinweis auf `METHOD` ab.

**Fehler.** `TRY … CATCH err$ … FINALLY` fängt Laufzeitfehler; `THROW "text"`
wirft selbst einen. `FINALLY` beendet den Block (es gibt keinen eigenen
FINALLY-Code).

## Befehlsreferenz

Eine einseitige Kurzfassung mit allen Befehlen und der kompletten
Sprachsteuerung steht in [CHEATSHEET.md](CHEATSHEET.md). Dieselbe Übersicht als
Bild, nach Befehlart gruppiert: [CHEATSHEET.png](CHEATSHEET.png) (englisch:
[CHEATSHEET.en.png](CHEATSHEET.en.png)), erzeugt mit
[`python tools/make_cheatsheet.py`](tools/make_cheatsheet.py) aus dem
Befehlsdatensatz der IDE.

Mit `*` markierte Befehle sind Ergänzungen, die über den Grundbestand der
Sprache hinausgehen.

**Fenster und Bildschirm**
`SETSCREEN w,h,fullscreen` · `CLEARSCREEN [farbe]` (`-1` schaltet das
Automatisch-Löschen ab) · `BLACKSCREEN` · `SHOWSCREEN` · `LIMITFPS fps[,vsync]` ·
`GETSCREENSIZE w,h` · `GETDESKTOPSIZE w,h` · `ISFULLSCREEN()` ·
`SETWINDOWTITLE "text"`* · `GETSCREENWIDTH()`* · `GETSCREENHEIGHT()`* ·
`VIEWPORT x,y,w,h` · `GETVIEWPORT x,y,w,h` · `PRESCALER w,h` ·
`SMOOTHSHADING an` · `ALPHAMODE a` · `ALPHATESTING wert` ·
`SETTRANSPARENCY farbe`

**Zeichnen**
`RGB(r,g,b)` · `DRAWLINE x1,y1,x2,y2,farbe` · `DRAWRECT x,y,w,h,farbe` (gefüllt) ·
`DRAWBORDER x,y,w,h,farbe`* · `DRAWCIRCLE x,y,r[,farbe]`* ·
`DRAWFILLEDCIRCLE x,y,r[,farbe]`* · `SETPIXEL x,y,farbe` · `GETPIXEL(x,y)` ·
`STARTPOLY sprite,mode` · `POLYVECTOR x,y,u,v,farbe` (u/v 0…1) · `POLYNEWSTRIP` ·
`ENDPOLY`

**Sprites**
`LOADSPRITE "bild.png", id` · `GENSPRITE()` · `DRAWSPRITE id,x,y` ·
`ROTOSPRITE id,x,y,winkel` · `ROTOZOOMSPRITE id,x,y,winkel,zoom%` ·
`ZOOMSPRITE id,x,y,fx%,fy%` · `STRETCHSPRITE id,x,y,w,h` ·
`LOADANIM "sheet.png",id,kachelw,kachelh` · `SETSPRITEANIM id,w,h` ·
`DRAWANIM id,frame,x,y` · `ROTOZOOMANIM id,frame,x,y,winkel,zoom%` ·
`STRETCHANIM id,frame,x,y,w,h` · `GETSPRITESIZE id,w,h` ·
`GRABSPRITE id,x,y,w,h` · `SAVESPRITE "datei.png",id` ·
`SPRCOLL(id1,x1,y1,id2,x2,y2)` · `ANIMCOLL(...)` · `BOXCOLL(x1,y1,w1,h1,x2,y2,w2,h2)`

**Bildpuffer und Text**
`LOADBMP "bild.png"` (Hintergrund) · `SAVEBMP "datei.bmp"` · `USEASBMP` ·
`BLENDSCREEN "bild.png"[,ms]` · `CREATESCREEN screen,sprite,w,h` ·
`USESCREEN id` · `PRINT "text",x,y[,kerning]` · `DRAWTEXT "text",x,y[,farbe]`* ·
`SETTEXTCOLOR farbe`* · `LOADFONT "datei",id[,groesse]` · `SETFONT id[,scale[,farbe]]` ·
`GETFONTSIZE w,h` · `GETCURRENTFONT()` · `INPUT var,x,y` · `LEN(text[,pixel])`

**Eingabe**
`KEY(scancode)` · `KEYHIT(scancode)`* · `INKEY$` · `MOUSESTATE mx,my,links,rechts` ·
`MOUSEAXIS(achse)` · `MOUSEHIT(taste)`* · `GETMOUSECOUNT()` ·
`SETMOUSE x,y` · `MOUSEWAIT` · `KEYWAIT` ·
`SYSTEMPOINTER an` · `ALLOWESCAPE an`

Fertige Tastencodes: `KEY_ESC`, `KEY_ENTER`, `KEY_SPACE`, `KEY_TAB`,
`KEY_BACKSPACE`, `KEY_DELETE`, `KEY_HOME`, `KEY_END`, `KEY_PGUP`, `KEY_PGDN`,
`KEY_UP/DOWN/LEFT/RIGHT`, `KEY_LSHIFT/RSHIFT`, `KEY_LCTRL/RCTRL`,
`KEY_LALT/RALT`, `KEY_F1`…`KEY_F12`, `KEY_A`…`KEY_Z`, `KEY_0`…`KEY_9`.

**Sound**
`LOADSOUND "sound.wav",id[,buffer]` · `PLAYSOUND(id,pan,volume)` → Kanal ·
`STOPSOUND kanal` · `SOUNDPLAYING(kanal)` · `HUSH` · `GENSOUND()` ·
`PLAYMUSIC "musik.wav",loop` · `STOPMUSIC` · `PAUSEMUSIC an` ·
`MUSICVOLUME v` · `ISMUSICPLAYING()`

**Mathe**
`ABS` · `SGN` · `SQR` · `SIN` · `COS` · `TAN` (Grad) · `ASIN` · `ACOS` ·
`ATAN(dy,dx)` (Grad) · `FLOOR` · `CEIL` · `POW(a,b)` · `LOGN` ·
`MOD(a,b)` · `FMOD(a,b)` · `MIN` · `MAX` · `RND(max)` (0…max, inklusiv) ·
`SEEDRND n` · `INTEGER` · `INTEGER64` · `NUMBER` · `ASL` · `ASR` ·
`BAND` · `BOR` · `BXOR` · `BNOT` · `IIF(bedingung, a, b)`

**Strings**
`LEN` · `LEFT$` · `RIGHT$` · `MID$(text,start[,laenge])` (0-basiert) ·
`INSTR(text,suche[,start])` (0-basiert, `-1` = nicht gefunden) · `REVINSTR` ·
`REPLACE$` · `SPLITSTR(text, feld$[], trenner[, leer])` · `UCASE$` · `LCASE$` ·
`TRIM$` · `LTRIM$` · `RTRIM$` · `ASC(text[,index])` · `CHR$` · `HEX$` ·
`FORMAT$(stellen,nachkomma,zahl)` · `STR$`* · `VAL`*

**Laufzeit und Debug**
`DEBUG` · `STDOUT` · `STDERR` · `STDIN$` · `SLEEP ms` · `GETTIMER()` ·
`GETTIMERALL()` · `GETCOMMANDLINE$()` · `CODELINE()` · `CODEFILE$()` ·
`PLATFORMINFO$("os")` · `GETENV$` · `PUTENV` · `ASSERT bedingung[,text]` ·
`END` · `HIBERNATE`

## Sprachdetails

* **Tastencodes**: `KEY()`/`KEYHIT()` nehmen SDL3-Scancodes; die `KEY_*`-Konstanten
  ersparen das Nachschlagen.
* **Formate**: `MID$`, `ASC` und `INSTR` sind 0-basiert und `RND(max)` ist
  inklusiv. `SIN/COS/TAN` und `ATAN` rechnen in Grad.
* **Arrays**: `DIM a[n]` ergibt die Indizes `0…n-1`, negative Indizes zählen von
  hinten, maximal 4 Dimensionen, `DIM a[0]` gibt den Speicher frei.
* **GOTO/GOSUB**: `GOTO` springt nur innerhalb des aktuellen Abschnitts
  (Hauptprogramm, `SUB` oder `FUNCTION`) und nur zu Marken auf dessen oberster
  Ebene. `GOSUB` ruft `SUB`s auf; Rücksprungmarken gibt es nicht.
* **TRY/CATCH**: `FINALLY` beendet den `TRY`-Block (kein eigener FINALLY-Code).
* **Musik**: nur WAV (kein MP3/OGG), da kein Decoder mitgeliefert wird.
  `PLAYMUSIC` nutzt denselben Mixer.
* **Schriften**: `LOADFONT` lädt `.bmp`-Fonts im Raster mit 16 Zeichen pro
  Zeile und 8 Zeilen (Zeichen 0–127) oder – bei `.ttf`/`.otf` bzw. Angabe einer
  Größe – eine TrueType-Schrift über FreeType. `SETFONT id,scale` skaliert.
  Der Kerning-Parameter von `PRINT` wird ignoriert (Bitmap-Fonts sind
  monospaced).
* **ALPHAMODE** ist angenähert: `> 0` additiv, `< 0` Alpha `1+wert`, `0` normal.
* **`ALPHATESTING`/`SETTRANSPARENCY`** wirken beim Laden von Bildern, nicht
  rückwirkend auf bereits geladene Sprites.
* **`SAVEBMP`/`GRABSPRITE`** brauchen einen echten Renderer; im `--headless`-Modus
  melden sie einen Fehler.
* **PRESCALER** nutzt SDL-Logical-Presentation; `VIEWPORT` ist dann in
  Fensterkoordinaten zu verstehen.
* **`INLINE`** übersetzt die C++-Blöcke mit `g++` (im Paket liegt einer bei):
  Definitionen werden beim Start als DLL geladen, Schritte laufen mitten im
  Code (siehe [INLINE](#inline-c-im-basic-programm)). `IMPORT` und `REQUIRE`
  gibt es nicht.

## INLINE: C++ im BASIC-Programm

`INLINE … ENDINLINE` stellt C++-Code direkt ins Programm. Die Position
entscheidet, was der Block ist:

* **Definitionen** – auf oberster Ebene, oder mit `INLINE DEFINE` auch mitten
  im Code. Sie werden beim Start einmal übersetzt und als DLL geladen; die mit
  `GBX_INLINE_FN` angemeldeten Funktionen sind danach normale GBX-Befehle.
* **Schritte** – in einer `FUNCTION`/`SUB`/`METHOD`, einem `IF` oder einer
  Schleife, auf oberster Ebene mit `INLINE RUN`. Der C++-Code läuft genau an
  dieser Stelle und sieht die BASIC-Variablen des Orts.

```basic
INLINE
#include "gbxarray.h"      ' Definition: Header liegt neben der .gbx-Datei
ENDINLINE

GLOBAL zaehler
zaehler = 0
INLINE RUN
  zaehler = zaehler + 41;
ENDINLINE
PRINT zaehler, 10, 10        ' 41

FUNCTION doppelt: wert
  ergebnis = 0
  INLINE
    ergebnis = wert * 2;
  ENDINLINE
  RETURN ergebnis
ENDFUNCTION
```

* **Namen**: BASIC-Variablen heißen in C++ klein geschrieben (`zaehler`,
  `ergebnis`). Texte bekommen `_str` (`name$` → `name_str`), Arrays werden
  `liste[i]` (auch mehrdimensional), TYPE-Variablen `p` mit den Feldern
  `p.x` und `p.name_str`, TYPE-Arrays `punkte[i].x`. INTEGER-Variablen bleiben
  INTEGER, wenn der Schritt etwas schreibt.
* **Sichtbar** sind in einer `FUNCTION`/`SUB` die Parameter, die lokalen und
  die globalen Variablen; in einer `METHOD` zusätzlich die Felder des eigenen
  TYPEs; im Hauptprogramm die globalen und die `LOCAL` des Hauptprogramms.
  Konstanten sind schreibgeschützt.
* **Definitionen** binden Header mit `#include "…"` ein (relativ zur
  `.gbx`-Datei und zum Projektordner) und melden Funktionen mit
  `GBX_INLINE_FN("NAME", minArgs, maxArgs, funktion)` an. Zahlen kommen als
  `double` (`gbxinline::argNum`), Texte als UTF-8 (`gbxinline::argText`);
  Arrays/TYPEs lassen sich dabei nicht übergeben.
* **Arrays** lassen sich im Schritt auch in der Größe ändern – wie `GBXArray`,
  nur direkt auf dem BASIC-Array: `liste.push(42)`, `liste.insert(0, 7)`,
  `liste.remove(-1)`, `liste.clear()` und `liste.resize(20)` (wie `REDIM`,
  vorhandene Werte bleiben). `liste.size(1)` und `liste.rank()` lesen die
  Form. Das gilt für Zahlen-, Text- und TYPE-Arrays; bei TYPE-Arrays legt
  `push()` einen neuen Datensatz an, `push(p)` hängt `p` an (wie `DIMPUSH`:
  beide teilen den Datensatz). Mehrdimensionale Arrays ändert `resize(a, b)`,
  eine einzelne Zeile `feld[i].asArray()`.
* **GBX-Befehle** dürfen im Schritt direkt aufgerufen werden, wie in BASIC:
  `PRINT(text_str, x, y)`, `DRAWLINE(x1, y1, x2, y2, farbe)`, `GETTIMER()`.
  Alle eingebauten Befehle ohne `$` im Namen stehen als C++-Funktion bereit;
  Befehle mit `$` (`CHR$`, `LEFT$`) ruft man über `call("LEFT$", …)` auf,
  dessen Ergebnis `.num()` oder `.text()` liefert. Variablen gehen als
  BYREF-Ziel mit (`GETSPRITESIZE(id, w, h)`), Zahlen und Texte als Wert.
  `RGB` heißt in C++ `rgb(r, g, b)` (das Windows-Makro belegt den Namen).
* **Grenzen**: Eigene `FUNCTION`s, `SUB`s und `METHODen` lassen sich aus einem
  Schritt nicht aufrufen, `RETURN`/`GOTO` nicht auslösen; GBX-Befehle nur in
  Schritten, nicht in mit `GBX_INLINE_FN` angemeldeten Funktionen.
  `push`/`insert`/`remove`/`clear` gelten nur für eindimensionale Arrays;
  mehrdimensionale ändert man mit `resize`. Nach einer Größenänderung sind
  vorher geholte Elemente (`Element`-Proxys) ungültig. Fehler im Schritt – etwa
  ein Index außerhalb des Arrays – erscheinen als Laufzeitfehler mit der Zeile
  im Block.
* **Compiler**: gesucht wird zuerst `..\compiler\bin\g++.exe` neben `gbx.exe`
  (das Paket bringt einen mit), dann `g++` im `PATH` und in den MSYS2-Ordnern;
  `GBX_CXX` setzt einen anderen Pfad, `GBX_INLINE_INCLUDE` einen anderen Ordner
  für `gbx_inline.h`. Die DLLs werden mit `-static` gelinkt und brauchen
  deshalb keine Compiler-DLLs auf dem Zielrechner.
* **Cache**: Die DLL heißt nach dem Hash des erzeugten C++-Textes und liegt in
  `%TEMP%\gbx-inline`; unveränderte Blöcke werden nicht neu übersetzt.
  Übersetzungsfehler erscheinen mit Datei und Zeile aus der `.gbx`-Datei.
* **Migration**: Blöcke in einer `FUNCTION` oder einem `IF`, die nur
  Definitionen enthielten, brauchen jetzt `INLINE DEFINE` – ohne das laufen sie
  als Schritt und müssen C++-Anweisungen sein.
* **Export**: Beim Packen einer EXE werden die Blöcke einmal übersetzt und
  mitgeliefert (`__inline/gbx_inline.dll` mit Kennung); das fertige Spiel
  braucht keinen Compiler mehr.

## Spiel als EXE weitergeben

`--export` packt ein Projekt in eine einzelne EXE. Darin stecken die Quellen
und die fertig übersetzte INLINE-Bibliothek; **Assets bleiben draußen** und
werden daneben kopiert.

```bash
build/gbx.exe --export spiel.exe examples/04_gbxarray/gbxarray.gbxproj
```

* Die EXE hängt ihr Paket beim Start in `%TEMP%\gbx-run\<hash>` aus und läuft
  von dort. Doppelklick öffnet kein Konsolenfenster mehr (das Windows-Subsystem
  wird beim Export auf GUI gestellt); Fehler erscheinen als Meldungsfenster.
* **Assets** (alles außer `.gbx`, `.gbxproj`, `.h`, `.hpp`, `.inl`) landen neben
  der EXE, in derselben Ordnerstruktur. Sie lassen sich dort austauschen, ohne
  neu zu packen; `SAVEBMP`/`SAVESPRITE` schreiben ebenfalls dorthin.
* **INLINE** wird beim Export übersetzt und eingebettet – das Spiel braucht
  keinen C++-Compiler.
* **Ohne fremde DLLs** läuft die EXE nur, wenn `gbx.exe` selbst statisch
  gelinkt ist: `-DGBX_STATIC_RUNTIME=ON`, also `build-static/gbx.exe`. Ein
  dynamisch gelinkter Interpreter erzeugt eine EXE, die libstdc++ und FreeType
  braucht; der Export weist darauf hin.
* **In der IDE**: Plattform **Windows-EXE** im Dropdown neben **Bauen**
  (Strg+E) – dort lässt sich der Speicherort wählen, und das Protokoll zeigt
  die eingebetteten Dateien.

## Spiel im Browser (HTML5)

Der Interpreter läuft auch als WebAssembly. Ein Export schreibt einen Ordner,
den jeder Browser direkt öffnen kann – **Doppelklick auf `index.html` genügt**,
ein Webserver ist nicht nötig.

```
spiel-web/
  index.html   Seite mit Canvas, Tastatur, Maus und Audio
  gbx.js       Interpreter als wasm, in eine JS-Datei eingebettet (-sSINGLE_FILE)
  game.js      Quellen und Assets des Projekts (Base64)
```

* **Einmalig bauen**: `em++` übersetzt `src/` nach `html5/gbx.js`
  (`-sUSE_SDL=3 -sUSE_FREETYPE=1 -DGBX_HAVE_TTF=1 -sASYNCIFY -sSINGLE_FILE=1
  -fexceptions`). Das dauert rund 30 s und braucht das Emscripten SDK. In der IDE: **Einstellungen →
  Allgemein → HTML5**, Knopf **Herunterladen …** holt das SDK in den Ordner
  `emsdk` neben der Editor-EXE, **Prüfen** zeigt die Version, **Player bauen**
  übersetzt den Interpreter.
* **Export**: In der Werkzeugleiste rechts neben **Bauen** die Zielplattform
  wählen (**HTML5 (Browser)**, Strg+E baut dann das Spiel). Die Quellen und
  Assets landen in `game.js`, der Player wird daneben kopiert; fertig ist der
  Ordner `<projekt>-web`.
* **INLINE** funktioniert auch im Browser, aber anders: dort gibt es weder
  Compiler noch `dlopen`. Deshalb übersetzt der Export die Blöcke einmalig mit
  Emscripten und linkt sie fest in `gbx.js` des Spiels (rund 30 s, braucht das
  SDK). Der Player aus **Player bauen** bleibt ohne Blöcke; er meldet INLINE
  mit einem Hinweis.
* **Ändert sich ein INLINE-Block oder ein eingebundener Header**, einfach neu
  exportieren – das Spiel bringt seinen Player selbst mit.
* **Zwischenspeicher**: Der fertige Player liegt im Temp-Ordner
  (`gbx-inline-player`), benannt nach einem Hash aus Blöcken, Projekt-Headern,
  Interpreter-Quellen und emsdk. Ohne Änderung ist der nächste Export nach
  Sekunden fertig; die letzten acht Player bleiben liegen.
* **Was funktioniert**: Grafik über SDL3 (WebGL), Tastatur/Maus, Audio nach der
  ersten Nutzergeste, `LIMITFPS` (über `emscripten_sleep`), `SAVEBMP` in die
  virtuelle Dateiablage, TrueType-Schriften.
* **Schriften**: `LOADFONT "schrift.ttf", id, groesse` läuft im Browser über den
  FreeType-Port von Emscripten (`-sUSE_FREETYPE=1`), mit Kerning und
  Kantenglättung wie auf dem Desktop. Die Datei muss im Projekt liegen (Ordner
  `assets/` oder ein Pfad in der `.gbxproj`); das Windows-Schriftverzeichnis
  gibt es im Browser nicht, ein `LOADFONT "arial.ttf"` ohne Datei im Projekt
  scheitert dort also. `TRY`/`CATCH` mit `SETFONT 0` als Rückfall fängt das ab.
* **Hinweis**: Der Interpreter läuft im Browser, ist also langsamer als nativ;
  für die Beispiele reicht es dicke. Die BASIC-Quellen liegen lesbar in
  `game.js` – bei einer interpretierten Sprache unvermeidbar.

## Schriften aus Google Fonts

**Werkzeuge → Google Fonts …** öffnet eine Liste aller Schriften von
fonts.google.com. Die Suche filtert nach Name und Kategorie, die Vorschau zeigt
den Text in der echten Schrift, mit allen vorhandenen Schnitten (Gewicht und
kursiv) und einstellbarer Größe. **Herunterladen** legt die `.ttf` in den
Asset-Ordner des Projekts (`assets/` oder der Pfad aus der `.gbxproj`) und zeigt
die passende Zeile:

```basic
LOADFONT "assets/Roboto-Bold.ttf", 1, 28
SETFONT 1
```

Die Datei liegt damit im Projekt und wandert beim HTML5-Export mit – im Browser
gibt es keine installierten Systemschriften. Ohne API-Key nutzt die IDE die
öffentliche Fontliste; unter **Einstellungen → Allgemein** lässt sich ein
Google-Fonts-API-Key hinterlegen, dann kommt die Liste über die offizielle
Webfonts-API. Der Key wird nur lokal gespeichert.

## Nicht enthalten (bewusst)

3D (`X_*`), C++-Vorlagen einbinden (`IMPORT`, `REQUIRE`), Präprozessor (`?IF` …),
Datei-Streams (`OPENFILE`, `READLINE`, …), Netzwerk (`NET*`, `SOCK_*`),
Joystick/Gamepad, Filme, `MEM2SPRITE`/`SPRITE2MEM`/`MEM2FONT`/`FONT2MEM`,
`SORTARRAY`, `FINDPATH`, `FILEREQUEST`, `SHELLCMD`, `ADDRESSOF`/`CALLBYNAME`,
`ENCRYPT$`/`DECRYPT$`, `SETSHOEBOX`. Der Bedarf dafür ist entweder 3D-, 
Plattform- oder sicherheitskritisch; der 2D-Kern ist vollständig.

## Beispiele

| Ordner | Inhalt |
| --- | --- |
| `examples/01_hello` | Formen, Text, TTF mit Fallback auf die Bitmap-Schrift |
| `examples/02_breakout` | spielbares Breakout: Maus/Pfeiltasten, Leben, Punkte, Neustart |
| `examples/03_sprites` | Sprites laden, rotieren/zoomen, Animationen (`LOADANIM`) |
| `examples/04_gbxarray` | `INLINE`-Block mit eigenem Header: dynamische Arrays (`GBXArray`) in C++ |
| `examples/05_inline` | `INLINE` an beliebiger Stelle: Definitionen aus `curve.h` und Schritte in `FUNCTION`/Schleife mit direkten Variablen |
| `examples/06_sparks` | Feuerwerk: C++-Schritte bewegen ein `TYPE`-Array mit 900 Funken, `GLOWCOLOR` kommt aus dem Definitionsblock |
| `examples/07_methods` | `METHOD` mit `INLINE`: `PRINT` und `DRAWFILLEDCIRCLE` direkt aus C++ aufrufen |
| `examples/08_box2d` | fremde Bibliothek (Box2D-Lite) per `INLINE` einbinden: Wrapper-Header, Aufrufe aus `FUNCTION` und `METHOD`, registrierte `PHYSICS_*`-Befehle, texturierte Polygone mit `STARTPOLY`/`POLYVECTOR` |
| `examples/09_ui` | fertige Oberfläche in reinem BASIC: `UI.gbx` mit Fenstern, Buttons, Schaltern, Reglern, Textfeldern, Dropdowns und Listen, dazu ein Beispielprojekt mit Themes |

`UI.gbx` ist eine eigenständige Bibliothek: Datei neben `main.gbx` legen,
`INCLUDE "UI.gbx"`, Widgets einmal anlegen und pro Frame `app.run()` aufrufen.
Die Kopfzeile der Datei beschreibt die komplette API; die Widgets sind
`TYPE`s mit `METHOD`s, die ihren Zustand in zwei globalen Arrays halten.

```bash
build/gbx.exe examples/02_breakout/breakout.gbxproj
```

## IDE

Im Ordner `ide/` liegt **GBX Studio**, eine IDE für GameBasiX mit
Syntaxhervorhebung, Autovervollständigung, Befehlsreferenz, Syntaxprüfung
(F7), direktem Start über `F5`, einem Laufzeit-Debugger (F6) und dem Export
als einzelne EXE (Knopf **Bauen** mit Plattform **Windows-EXE**, Strg+E):

Schon beim Tippen prüft die IDE den **ungespeicherten** Code: Nach einer
halben Sekunde Pause geht der Editorinhalt über `gbx --check --stdin` an den
Interpreter, Fehler erscheinen als Marker in der Zeile und als Anzahl in der
Statusleiste. Gespeichert werden muss dafür nichts, und es entstehen keine
temporären Dateien – die Prüfung liest den Puffer und wirft ihn weg.

Angestrichen wird auch jeder **unbekannte Name** – genau das Wort, nicht die
ganze Zeile; bei Tippfehlern in Befehlen und TYPE-Feldern schlägt die IDE den
richtigen Namen vor.
Teildateien, die per `INCLUDE` eingebunden werden, prüft die Live-Prüfung nur
auf Syntax (`--syntax-only`), weil sie die Namen der anderen Dateien nicht
kennen; die vollständige Namensprüfung liefert `F7` über die Einstiegsdatei.

```bash
cd ide
npm install      # einmalig
npm start        # Electron-Fenster; alternativ: npm run web
```

Nach dem einmaligen `npm install` genügt im Projektordner ein Doppelklick auf
**`GBXStudio.exe`**: Die schmale EXE (kein eigener Editor, nur ein Startprogramm)
findet `ide/node_modules/electron` neben sich und öffnet das Editorfenster.
Fehlt Electron, startet sie stattdessen den Webserver und öffnet den Browser.

Das Icon (dunkler Grund, grüne Initialen „GBX“, darunter „STUDIO“) liegt als
`ide/assets/icon.ico` bei und steckt in der EXE, im Editorfenster und im
Paketbau. Neu erzeugen lässt es sich mit `python tools/make_icon.py` (braucht
Pillow).

Die IDE sucht `build/gbx.exe` neben dem IDE-Ordner; der Pfad lässt sich in den
Einstellungen ändern. Details stehen in [ide/README.md](ide/README.md).

## Tests

```bash
./build/gbx.exe --tests tests/language_tests.gbx     # 133 Sprachtests
ctest --test-dir build --output-on-failure           # ueber CTest
python tools/check_render.py                         # Pixelpruefung offscreen
python tools/check_commands.py                       # jeder Befehl einmal
python tools/check_debug.py                          # Debugger-Protokoll
python tools/check_export.py                         # Spiel als einzelne EXE
./build/gbx.exe --tests tests/inline_tests.gbx        # 84 INLINE-Prüfungen
```

`tests/render_check.gbx` zeichnet Rechteck, Linie, Kreis, Rahmen, Sprite und
Text und speichert das Ergebnis als BMP. `tools/check_render.py` startet das
Programm mit `SDL_VIDEODRIVER=dummy` und prüft einzelne Pixel sowie die
Textdarstellung.

`tests/command_tests.gbx` ruft jeden eingebauten Befehl mindestens einmal mit
minimalen Argumenten auf und prüft feststehende Ergebnisse mit `ASSERT`.
`tools/check_commands.py` startet es zweimal – headless und mit Dummy-Renderer –
und vergleicht die Befehlsliste aus `src/builtins.cpp` mit der Testdatei. Ein
neuer Befehl ohne Test lässt den CTest-Lauf `commands` fehlschlagen; `INPUT`
ist als nicht automatisierbar hinterlegt, weil es auf Tastatureingaben wartet.

## Projektstruktur

```
src/lexer.cpp       Tokens (Kommentare, Strings, Zahlen, Suffixe)
src/parser.cpp      rekursiver Abstieg -> AST, INCLUDE, Blöcke
src/interpreter.cpp Ausführung, Variablen, Arrays, TYPE mit METHODen, BYREF
src/inline.cpp      INLINE-Blöcke: g++ aufrufen, DLL cachen und laden, Werte tauschen
src/package.cpp     Spielpaket: Quellen + INLINE-DLL an die EXE hängen, beim Start auspacken
inline/gbx_inline.h Schnittstelle für INLINE-Code (landet beim Bauen neben gbx.exe)
src/lint.cpp        Namensprüfung für --check (Befehle, Variablen, Felder, Methoden)
src/debugger.cpp    Laufzeit-Debugger (Haltepunkte, Schritte, Werte)
src/builtins.cpp    alle Befehle/Funktionen
src/graphics.cpp    SDL3-Renderer, Sprites, Text (Bitmap + FreeType), Eingabe
src/audio.cpp       WAV-Laden und Software-Mixer
src/project.cpp     .gbxproj lesen
src/main.cpp        Kommandozeile, Start, Konstanten
src/studio_launcher.cpp  GBXStudio.exe: startet den Editor aus ide/
third_party/        stb_image, stb_image_write, font8x8 (gemeinfrei/MIT)
```

## Drittanbieter

* SDL3 (zlib-Lizenz), per CMake `FetchContent`
* FreeType (FTL/GPLv2), optional für TTF
* `stb_image.h`, `stb_image_write.h` (Public Domain/MIT)
* `font8x8_basic.h` von Daniel Hepper (Public Domain)

## Änderungsstand

**Oktober 2026**

* **HTML5 mit TrueType**: Der wasm-Player wird mit Emscriptens FreeType-Port
  gebaut (`-sUSE_FREETYPE=1 -DGBX_HAVE_TTF=1`); `-sSINGLE_FILE=1` legt das wasm
  in `gbx.js`, sodass `index.html` per Doppelklick läuft. `LOADFONT` mit
  `.ttf` funktioniert damit auch im Browser.
* **Google Fonts in der IDE**: **Werkzeuge → Google Fonts …** sucht Schriften,
  zeigt sie in echter Schrift und lädt die `.ttf` in den Asset-Ordner des
  Projekts; mit einem API-Key in den Einstellungen über die Webfonts-API,
  sonst über die öffentliche Fontliste.
* **Bitmap-Font-Creator entfernt**: `LOADFONT` liest Bitmap-Fonts weiterhin,
  im Editor gibt es nur keinen Generator mehr dafür.
* **Name**: Die Sprache heißt jetzt **GameBasiX**; Compiler (`gbx.exe`),
  Projektdateien (`.gbx`, `.gbxproj`) und **GBX Studio** behalten ihre Namen.
  Alle Bezüge auf das alte SDK sind aus Code, IDE und Doku entfernt.
* **Aufgeräumt**: `STDCOLOR`, `AUTOPAUSE`, `BUSY`, `GENFILE` und
  `SETACTIVEMOUSE` waren ohne Funktion und sind gestrichen – samt Tests,
  Referenz und Datensatz.
* **IDE auf Englisch**: Die Oberfläche startet englisch; Deutsch lässt sich in
  den Einstellungen oder über den Sprachschalter wählen. Der Wechsel gilt
  sofort auch für Servermeldungen und Job-Logs – ohne Neustart.
* **Teststand**: 133 Sprachprüfungen, 84 INLINE-Prüfungen, 24 Prüfungen der
  IDE (`ide: npm test`), 151 eingebaute Befehle/Funktionen.
