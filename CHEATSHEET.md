# GameBasiX – CheatSheet

Vollständige Kurzreferenz für **GameBasiX**: Sprachsteuerung, Typen und alle
eingebauten Befehle (182 einschließlich der Sonderform `IIF`). Die
Liste stammt aus `src/builtins.cpp`, die Steuerung aus `src/parser.cpp` – es ist
also genau das, was der Interpreter versteht.

Zum Stand Oktober 2026: 133 Prüfungen in `tests/language_tests.gbx`, 84 in
`tests/inline_tests.gbx`, 79 in `tests/ui_tests.gbx` und 47 rund um Dateien in
`tests/file_tests.gbx`; die IDE prüft sich mit `ide: npm test`.

## 1. Programmgerüst

```basic
' Kommentar (alternativ: // Kommentar)
INCLUDE "lib.gbx"            ' Datei relativ zum Projektordner einbinden

CONSTANT BREITE% = 800
GLOBAL leben% = 3

SETSCREEN BREITE%, 600
LIMITFPS 60                  ' 60 Bilder/s, vsync aus

WHILE TRUE
  CLEARSCREEN RGB(0, 0, 0)
  PRINT "Hallo", 20, 20
  SHOWSCREEN
WEND
END
```

Mehrere Anweisungen pro Zeile trennt `:` – Marken (Sprungziele) stehen allein:
`start:`.

## 2. Werte, Typen, Operatoren

| Schreibweise | Bedeutung |
| --- | --- |
| `$` | String (`name$`) |
| `%` | Ganzzahl (`anzahl%`) |
| `#` | Kommazahl (`x#`) |
| ohne Suffix | **FLOAT** (Kommazahl) |
| `42`, `42%` | Ganzzahl-Literal |
| `3.5`, `1e-3` | Kommazahl-Literal |
| `0x1F` | Hexadezimal |
| `"Text"` | String; `""` innerhalb steht für `"`, Escapes `\n \t \r \\ \0` |
| `TRUE` / `FALSE` | `1` / `0` – jeder Wert ≠ 0 gilt als wahr |

Zuweisungen wandeln ins Zielformat: `x$ = 5` ergibt `"5"`, `n% = 3.9` ergibt `3`.

**Operatoren, von bindend nach locker:**

1. `( … )`, Aufruf `f(…)`, Index `a[…]`, Feld `p.x`
2. unäres `-`
3. `*` `/`
4. `+` `-` (mit String auf einer Seite: Verkettung)
5. `=` `<>` `<` `<=` `>` `>=`
6. `NOT`
7. `AND`
8. `OR`

`AND`/`OR` werten kurzschließend und liefern `1`/`0`. `NOT` bindet lockerer als
ein Vergleich: `NOT a = b` heißt `NOT (a = b)`. `/` liefert immer eine
Kommazahl, Division durch 0 ist ein Laufzeitfehler; `+ - *` bleiben ganzzahlig,
wenn beide Seiten Ganzzahlen sind.

## 3. Logische Steuerung

### IF

```basic
IF leben% <= 0 THEN GAME_OVER()          ' Einzeiler
IF x < 0 THEN x = 0 : y = 1              ' mehrere Anweisungen mit ':'
IF x < 0 THEN y = 1 ELSE y = 2           ' Einzeiler mit ELSE

IF punkte > 100                          ' Blockform, THEN ist optional
  PRINT "gewonnen", 10, 10
ELSEIF punkte > 50
  PRINT "fast", 10, 10
ELSE
  PRINT "weiter", 10, 10
ENDIF
```

### SELECT

```basic
SELECT wert%
CASE 1, 2, 3                 ' Liste
CASE 10 TO 20                ' Bereich (einschließlich)
CASE > 100                   ' Vergleich: >, >=, <, <=, <> oder =
DEFAULT
ENDSELECT
```

`CASE`-Zweige werden von oben nach unten geprüft, der erste Treffer gewinnt.

### Schleifen

```basic
FOR i% = 1 TO 10             ' aufsteigend
FOR i% = 10 TO 1 STEP -2     ' STEP auch negativ oder gebrochen
NEXT i%                      ' Zählername optional: NEXT

WHILE bedingung
  ...
WEND

REPEAT                       ' läuft mindestens einmal
  ...
UNTIL bedingung

FOREACH wert IN feld[]       ' Elemente eines Arrays
  wert = wert * 2            ' Zuweisung ändert das Element im Array
NEXT
```

`BREAK` verlässt die innerste Schleife, `CONTINUE` springt zum nächsten
Durchlauf; in `SELECT` beendet `BREAK` das `SELECT`.

### Einzelanweisungen

```basic
INC x%              ' x% = x% + 1
INC x%, 5           ' um 5 erhöhen
DEC leben%, 1       ' um 1 verringern
SWAP a, b           ' Werte tauschen (auch Felder/Array-Elemente)
LET x = 5           ' LET ist optional
x = 5 : y = 7       ' mehrere Anweisungen pro Zeile
```

### Sprünge

```basic
start:                       ' Marke: Name + Doppelpunkt, allein in der Zeile
GOTO start
GOSUB hilfe                  ' ruft SUB hilfe auf
```

Sprünge gelten nur innerhalb desselben Abschnitts (Hauptprogramm bzw. eine
Funktion/SUB); über Abschnittsgrenzen hinweg meldet der Parser einen Fehler.
`GOSUB` springt in eine `SUB`, deren `ENDSUB` zurückkehrt.

### Fehler

```basic
TRY
  risky()
CATCH err$                   ' err$ = Meldung des Fehlers
  DEBUG "Fehler: " + err$ + CHR$(10)
FINALLY                      ' beendet den Block; alternativ: ENDTRY
```

`THROW "text"` wirft selbst einen Laufzeitfehler. Den Block beendet entweder
`FINALLY` oder `ENDTRY` – beides zusammen ist ein Syntaxfehler. Code zwischen
den beiden gibt es nicht, `FINALLY` ist nur das Blockende.

## 4. Funktionen, SUBs, TYPE

```basic
FUNCTION laenge(a AS FLOAT, b AS FLOAT = 0, BYREF rest AS STRING)
  STATIC aufrufe%            ' bleibt zwischen Aufrufen erhalten
  INC aufrufe%
  IF a < 0 THEN EXIT         ' vorzeitig raus (wie RETURN ohne Wert)
  rest = "ok"
  RETURN SQR(a * a + b * b)
ENDFUNCTION

FUNCTION summe: a, b         ' Doppelpunkt-Form, Parameter ohne Klammern
  RETURN a + b
ENDFUNCTION

SUB hilfe                    ' SUBs haben keine Parameter
  PRINT "Hilfe", 10, 10
ENDSUB
```

* Parameter: `name`, `name AS TYP`, Suffix (`a%`), Vorgabewert `= ausdruck`,
  `BYREF` für Referenz (auch Array-Elemente und TYPE-Felder).
* `RETURN wert` in `FUNCTION`, `EXIT` verlässt Funktion oder SUB sofort.
* `STATIC` deklariert eine lokale Variable mit dauerhaftem Wert.

```basic
TYPE PUNKT
  x AS FLOAT                 ' ohne AS: FLOAT
  name AS STRING             ' $ macht ebenfalls String
  mitte AS PUNKT             ' TYPE in TYPE
  leben% = 3                 ' Standardwert für jede neue Instanz
  beschriftung$ = "Punkt"
ENDTYPE

GLOBAL spieler AS PUNKT
LOCAL p AS PUNKT
DIM route[5] AS PUNKT
p.x = 10 : p.name = "Start"
```

```basic
TYPE KISTE
  w AS FLOAT
  h AS FLOAT

  METHOD flaeche             ' Felder sind im Rumpf wie Variablen sichtbar
    RETURN w * h
  ENDMETHOD

  METHOD skalieren: faktor   ' Parameter wie bei FUNCTION
    w = w * faktor
    h = h * faktor
  ENDMETHOD
ENDTYPE

LOCAL k AS KISTE
k.w = 4 : k.h = 2
PRINT k.flaeche(), 0, 0     ' 8
k.skalieren(3)              ' Aufruf als Anweisung
```

* Ein ohne Objekt geschriebener Methodenname meint die eigene Methode:
  `skalieren(2)` in einer anderen `METHOD` ruft `self.skalieren(2)`.
* `self` ist das eigene Objekt: `self.x` liest ein Feld, `self.flaeche()` ruft
  eine Methode auf, `andere(self)` übergibt das Objekt als `AS KISTE`-Parameter.
* Parameter und `LOCAL` verdecken ein Feld gleichen Namens.
* `FUNCTION`/`SUB` innerhalb eines TYPEs ist ein Fehler – dort heißt es
  `METHOD` … `ENDMETHOD`.

`GLOBAL` (überall sichtbar), `LOCAL` (nur im Abschnitt), `STATIC` (persistent),
`CONSTANT`/`CONST` (unveränderlich). Mehrere Namen mit Komma:
`GLOBAL a%, b$, c AS PUNKT`.

**Standardwerte.** Ein Feld darf hinter `=` einen Startwert bekommen
(`x% = 0`, `name$ = "neu"`, auch `total% = 2 + 3` oder eine `CONSTANT`). Der
Ausdruck wird bei jeder neuen Instanz ausgewertet – für `LOCAL`, `GLOBAL` und
jedes Element eines `DIM … AS TYP`-Arrays. Ohne Angabe bleibt es beim Nullwert
(0 bzw. leerer String).

## 5. Arrays

```basic
DIM feld[10]                 ' Indizes 0 … 9, letztes Element: feld[-1]
DIM map[20][15]              ' mehrdimensional (bis 4 Dimensionen)
DIM namen$[5]
DIM punkte[8] AS PUNKT
REDIM feld[20]               ' neue Größe, vorhandene Daten bleiben
REDIM punkte[8] AS PUNKT     ' TYPE-Array: Elementtyp mit angeben
DIMPUSH feld[], 42           ' Wert anhängen
DIMDEL feld[], 0             ' Element an Index 0 entfernen (negativ = von hinten)
DIMDATA zahlen[], 3, 5, 8    ' Array aus Werten füllen
n% = BOUNDS(feld[], 0)       ' Größe der Dimension (0 = erste)
n% = LEN(feld[])             ' Anzahl Elemente
```

## 6. Befehle nach Gebiet

Alle eingebauten Befehle (182 einschließlich `IIF`). `[…]` = optional,
`→` = liefert einen Wert. Farben sind `RGB(r,g,b)`-Werte; Winkel in **Grad**.

### Fenster und Bildschirm (18)

```basic
SETSCREEN w, h[, vollbild]        ' Vollbild: TRUE/FALSE
SETWINDOWTITLE "Titel"
CLEARSCREEN [farbe]               ' -1 schaltet das Auto-Löschen ab
BLACKSCREEN                       ' nach jedem SHOWSCREEN schwarz löschen
SHOWSCREEN                        ' Backbuffer anzeigen, Frame-Ende
LIMITFPS fps[, vsync]
GETSCREENSIZE w, h                ' Ausgabevariablen
GETSCREENWIDTH()  GETSCREENHEIGHT()
GETDESKTOPSIZE w, h
ISFULLSCREEN()                    ' → TRUE/FALSE
VIEWPORT x, y, w, h               ' Zeichenbereich einschränken
GETVIEWPORT x, y, w, h
PRESCALER w, h                    ' alles so skalieren, als wäre der Screen w×h
SMOOTHSHADING an                  ' weich (TRUE) / pixelig (FALSE)
ALPHAMODE [modus]                 ' Transparenz-Modus für weitere Ausgaben
ALPHATESTING [wert]               ' Pixel unter diesem Alpha verwerfen
SETTRANSPARENCY farbe             ' transparente Farbe für LOADSPRITE/LOADFONT/GRABSPRITE
```

### Zeichnen (12)

```basic
RGB(r, g, b)                      ' → Farbwert
DRAWLINE x1, y1, x2, y2, farbe
DRAWRECT x, y, w, h, farbe        ' gefüllt
DRAWBORDER x, y, w, h, farbe      ' nur Rahmen
DRAWCIRCLE x, y, r[, farbe]       ' Umriss
DRAWFILLEDCIRCLE x, y, r[, farbe]
SETPIXEL x, y, farbe
GETPIXEL(x, y)                    ' → Farbwert
STARTPOLY [sprite][, modus]       ' Polygon beginnen (ohne Sprite: Farbflächen)
POLYVECTOR x, y[, u, v[, farbe]]  ' Eckpunkt; u/v = Texturkoordinate 0 … 1
POLYNEWSTRIP                      ' Strip unterbrechen (unsichtbare Dreiecke)
ENDPOLY                           ' Polygon abschließen
```

### Sprites (18)

```basic
GENSPRITE()                       ' → freie Sprite-ID
LOADSPRITE "bild.png", id
LOADANIM "sheet.png", id, kachelw, kachelh
SETSPRITEANIM id, w, h            ' Kachelgröße für DRAWANIM setzen
DRAWSPRITE id, x, y
ROTOSPRITE id, x, y, winkel
ROTOZOOMSPRITE id, x, y, winkel, zoom
ZOOMSPRITE id, x, y, zoomx, zoomy
STRETCHSPRITE id, x, y, w, h
DRAWANIM id, frame, x, y
ROTOZOOMANIM id, frame, x, y, winkel, zoom
STRETCHANIM id, frame, x, y, w, h
GETSPRITESIZE id, w, h
GRABSPRITE id, x, y, w, h         ' aus dem Backbuffer ein Sprite machen
SAVESPRITE "datei.bmp", id
SPRCOLL(id1, x1, y1, id2, x2, y2)              ' → Kollision (Pixelgenau)
ANIMCOLL(id1, f1, x1, y1, id2, f2, x2, y2)      ' → Kollision zweier Frames
BOXCOLL(x1, y1, w1, h1, x2, y2, w2, h2)         ' → Rechtecke überlappen sich
```

### Bildpuffer und Text (15)

```basic
SETTEXTCOLOR farbe                ' für PRINT und INPUT
LOADBMP "bild.png"                ' Hintergrundbild
SAVEBMP "datei.bmp"
USEASBMP                          ' aktuellen Backbuffer als Hintergrund
BLENDSCREEN "bild.png"[, ms]      ' Backbuffer in das Bild einblenden
CREATESCREEN screen, sprite, w, h ' virtuellen Bildschirm anlegen (Ausgabe in Sprite)
USESCREEN id                      ' dorthin umleiten; -1 = Hauptbildschirm
PRINT "text", x, y[, kerning]     ' kerning wird derzeit ignoriert
DRAWTEXT "text", x, y[, farbe]    ' ändert die Textfarbe nicht
LOADFONT "font.png", id[, größe]
SETFONT id[, scale[, farbe]]
GETFONTSIZE w, h
GETCURRENTFONT()                  ' → Font-ID
INPUT var[, x, y]                 ' Eingabezeile mit Cursor
LEN(text[, pixel])                ' Zeichenanzahl oder Pixelbreite
```

### Eingabe (12)

```basic
KEY(scancode)                     ' → 1, solange Taste gedrückt
KEYHIT(scancode)                  ' → 1 nur im Frame des Tastendrucks
INKEY$                            ' → Zeichen der letzten Eingabe
MOUSESTATE mx, my, links, rechts  ' Ausgabevariablen
MOUSEAXIS(achse)                  ' 0=x, 1=y, 2=Rad, 3=links, 4=rechts, 5=Mitte
MOUSEHIT(taste)                   ' 1=links, 2=rechts, 3=Mitte; nur im Klick-Frame
GETMOUSECOUNT()                   ' → Anzahl Mäuse
SETMOUSE x, y                     ' Mauszeiger setzen
MOUSEWAIT                         ' auf Klick oder Taste warten
KEYWAIT                           ' auf Tastendruck warten
SYSTEMPOINTER an                  ' System-Mauszeiger ein-/ausblenden
ALLOWESCAPE an                    ' Abbruch mit ESC erlauben/verbieten
```

### Sound (11)

```basic
HUSH                              ' alle Sounds sofort stoppen
GENSOUND()                        ' → freie Sound-ID
LOADSOUND "sound.wav", id[, buffer]
PLAYSOUND id[, pan[, volume]]     ' → Kanal; pan -1 … 1, volume 0 … 1
STOPSOUND kanal
SOUNDPLAYING(kanal)               ' → TRUE/FALSE
PLAYMUSIC "musik.wav"[, loop]
STOPMUSIC
PAUSEMUSIC an                     ' TRUE pausiert, FALSE setzt fort
ISMUSICPLAYING()                  ' → TRUE/FALSE
MUSICVOLUME v
```

### Mathe (29)

```basic
ABS(x)  SGN(x)  SQR(x)
SIN(w)  COS(w)  TAN(w)            ' Grad
ASIN(x)  ACOS(x)  ATAN(dy[, dx])  ' → Grad
FLOOR(x)  CEIL(x)  POW(a, b)  LOGN(x)
MOD(a, b)                         ' ganzzahliger Rest
FMOD(a, b)                        ' Rest mit Nachkommastellen
MIN(a, b)  MAX(a, b)
RND(max)                          ' → 0 … max (einschließlich)
SEEDRND n                         ' Zufallsgenerator setzen
INTEGER(x)  INTEGER64(x)  NUMBER(x)
ASL(n, bits)  ASR(n, bits)        ' Bitverschiebung
BAND(a, b)  BOR(a, b)  BXOR(a, b)  BNOT(a)
IIF(bedingung, a, b)              ' wertet nur den gewählten Zweig aus
```

### Strings (18)

```basic
ASC(text[, index])                ' → Zeichencode (index 0-basiert)
CHR$(code)                        ' → Zeichen
HEX$(n)                           ' → Hexadezimaltext
LEFT$(text, n)  RIGHT$(text, n)
MID$(text, start[, länge])        ' start 0-basiert
INSTR(text, suche[, start])       ' → Position, 0-basiert, -1 = nicht gefunden
REVINSTR(text, suche[, start])    ' rückwärts suchen
REPLACE$(text, suche, ersatz)     ' alle Treffer ersetzen
SPLITSTR(text$, feld$[]           ' zerlegt in Array (BYREF-Ergebnis)
        [, trenner$[, leer_weglassen]])  ' Standard: "," / leere weglassen
UCASE$(text)  LCASE$(text)
TRIM$(text[, zeichen])            ' beide Seiten
LTRIM$(text[, zeichen])  RTRIM$(text[, zeichen])
FORMAT$(stellen, nachkomma, zahl) ' Breite 0 = ohne Auffüllen
STR$(wert)  VAL(text)
```

### Dateien (31)

```basic
OPENFILE kanal, "datei", modus    ' 1 lesen, 0 schreiben, -1 anhängen → 1/0
CLOSEFILE kanal
READBYTE kanal, b%                ' 1 Byte, vorzeichenbehaftet
READUBYTE kanal, b%               ' 0 … 255
READWORD kanal, w%                ' 2 Byte
READUWORD kanal, w%
READLONG kanal, l%                ' 4 Byte
READULONG kanal, l%
READIEEE kanal, d#                ' 8 Byte double
READSHORTIEEE kanal, f#           ' 4 Byte float
READSTR kanal, text$, anzahl
READLINE kanal, zeile$
WRITEBYTE kanal, wert
WRITEUBYTE kanal, wert
WRITEWORD kanal, wert
WRITEUWORD kanal, wert
WRITELONG kanal, wert
WRITEULONG kanal, wert
WRITEIEEE kanal, wert#
WRITESHORTIEEE kanal, wert#
WRITESTR kanal, text$
WRITELINE kanal, zeile$           ' hängt CRLF an
FILESEEK kanal, offset, ursprung  ' 0 Anfang, 1 Position, -1 Ende
FILEPOSITION(kanal)               ' → Byte-Position
FILEEOF(kanal)                    ' → 1 am Dateiende
FILEEXISTS("datei")               ' → 1, wenn vorhanden
FILESIZE("datei")                 ' → Bytes, -1 wenn nicht vorhanden
COPYFILE quelle$, ziel$
RENAMEFILE quelle$, ziel$
DELETEFILE "datei"
MAKEDIR "ordner"                  ' legt auch fehlende Elternordner an
```

### Laufzeit und Debug (15)

```basic
DEBUG text                        ' auf stderr, ohne Zeilenumbruch
STDOUT text  STDERR text          ' Standardausgabe/-fehler
STDIN$                            ' → gelesene Zeile
SLEEP ms
GETTIMER()                        ' → ms seit dem letzten SHOWSCREEN
GETTIMERALL()                     ' → ms seit Programmstart
GETCOMMANDLINE$()
CODELINE()  CODEFILE$()           ' → aktuelle Zeile / Datei
PLATFORMINFO$("os")               ' auch "version", "sdlversion",
                                  ' "numdisplays", "cpus"
GETENV$("NAME")                   ' → Umgebungsvariable
PUTENV name$, wert$               ' Variable für Kindprozesse setzen
ASSERT bedingung[, text]          ' zählt Fehlschläge im Testlauf
HIBERNATE                         ' schlafen, bis der Benutzer etwas tut
```

### Sonstige (4)

```basic
GENFONT()                         ' → freie Font-ID
GENSCREEN()                       ' → freie Screen-ID für CREATESCREEN
DIMDATA arr[], w1[, w2, …]        ' Array aus Werten füllen (BYREF-Ziel)
BOUNDS(arr[], dimension)          ' → Größe der Dimension (0 = erste)
```

## 7. Tastencodes

`KEY_ESC`/`KEY_ESCAPE`, `KEY_ENTER`/`KEY_RETURN`, `KEY_SPACE`, `KEY_TAB`,
`KEY_BACKSPACE`, `KEY_DELETE`, `KEY_INSERT`, `KEY_HOME`, `KEY_END`, `KEY_PGUP`,
`KEY_PGDN`, `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`, `KEY_LSHIFT`,
`KEY_RSHIFT`, `KEY_LCTRL`, `KEY_RCTRL`, `KEY_LALT`, `KEY_RALT`, `KEY_MINUS`,
`KEY_EQUALS`, `KEY_COMMA`, `KEY_PERIOD`, `KEY_F1` … `KEY_F12`, `KEY_A` … `KEY_Z`,
`KEY_0` … `KEY_9`.

```basic
IF KEYHIT(KEY_SPACE) THEN sprung = 1
IF KEY(KEY_LEFT) THEN x = x - 2
```

## 8. Typische Spielschleife

```basic
SETSCREEN 800, 600
LIMITFPS 60
x# = 400 : y# = 300

WHILE TRUE
  IF KEY(KEY_LEFT)  THEN x# = x# - 4
  IF KEY(KEY_RIGHT) THEN x# = x# + 4
  IF KEY(KEY_ESCAPE) THEN BREAK

  CLEARSCREEN RGB(16, 20, 26)
  DRAWFILLEDCIRCLE x#, y#, 20, RGB(143, 206, 39)
  PRINT "x = " + FORMAT$(4, 1, x#), 10, 10
  SHOWSCREEN
WEND
END
```

## 9. Häufige Stolpersteine

* `MID$`, `ASC`, `INSTR` zählen **ab 0**; `INSTR` liefert `-1`, wenn nichts passt.
* `RND(max)` liefert `0 … max` einschließlich.
* `TRIM$` & Co. nehmen eine Zeichenmenge (Standard: Leerraum).
* `KEY`/`KEYHIT` erwarten SDL3-Scancodes – dafür gibt es die `KEY_*`-Konstanten.
* `FINALLY` bzw. `ENDTRY` beendet den `TRY`-Block (nur eines von beiden);
  es gibt keinen Code dazwischen.
* Debug-Ausgaben (`DEBUG`) erscheinen auf stderr und ohne automatischen
  Zeilenumbruch – `+ CHR$(10)` anhängen.

## 10. C++ mit INLINE

```basic
' Definitionen: beim Start einmal mit g++ uebersetzt
INLINE
#include "gbxarray.h"          // Header liegt neben der .gbx-Datei
static void doppelt(const GbxValue* a, int n, GbxValue* r) {
    gbxinline::retNum(r, gbxinline::argNum(a, n, 0) * 2);
}
GBX_INLINE_FN("DOPPELT", 1, 1, doppelt);
ENDINLINE

' Schritt: laeuft genau an dieser Stelle
GLOBAL x
x = 41
INLINE RUN
  x = x + 1;
ENDINLINE
PRINT x, 0, 0                  ' 42

FUNCTION verdoppeln: wert
  ergebnis = 0
  INLINE
    ergebnis = wert * 2;
  ENDINLINE
  RETURN ergebnis
ENDFUNCTION
```

* **Position entscheidet**: oberste Ebene = Definitionen, im Code (`FUNCTION`,
  `IF`, Schleife) = Schritt; `INLINE DEFINE` und `INLINE RUN` erzwingen das
  eine oder andere. Ein Schritt sieht die Variablen der Stelle: `x`, Texte
  klein mit `_str` (`name$` → `name_str`), Array `liste[i]`, TYPE `p.feld`,
  TYPE-Array `punkte[i].feld`. Konstanten sind nur lesbar, INTEGER bleibt
  INTEGER.
* Arrays im Schritt: `liste.push(wert)`, `liste.insert(i, wert)`,
  `liste.remove(i)`, `liste.clear()`, `liste.resize(n)` (wie `REDIM`, auch
  `resize(a, b)`), `liste.size(dim)`, `liste.rank()`. TYPE-Arrays:
  `punkte.push()` legt einen Datensatz an, `punkte.push(p)` hängt `p` an
  (wie `DIMPUSH`: beide teilen den Datensatz). `push`/`insert`/`remove`/`clear`
  nur eindimensional; nach einer Größenänderung sind vorher geholte
  `Element`-Proxys ungültig.
* GBX-Befehle im Schritt: `PRINT(text_str, x, y)`, `DRAWLINE(x1, y1, x2, y2, farbe)`,
  `GETTIMER()` … Alle eingebauten Namen ohne `$` stehen bereit; mit `$` über
  `call("LEFT$", …).text()`. Variablen gehen als BYREF mit
  (`GETSPRITESIZE(id, w, h)`), `RGB` heißt `rgb(r, g, b)`. Eigene FUNCTIONs,
  SUBs und METHODen sind aus dem Schritt nicht aufrufbar; Fehler nennen die
  Zeile im Block.
* Definitionen: `GBX_INLINE_FN("NAME", min, max, funktion)`, Header per
  `#include`, Zahlen als `double` (`gbxinline::argNum`), Texte als UTF-8
  (`gbxinline::argText`), zurück mit `gbxinline::retNum` / `retText`.
* `GBX_CXX` setzt den Compiler, `GBX_INLINE_INCLUDE` den Ordner für
  `gbx_inline.h`; die DLL liegt in `%TEMP%\gbx-inline` und wird nur bei
  geändertem Text neu gebaut.
* `examples/04_gbxarray/gbxarray.h` ist eine fertige kleine Bibliothek:
  `GBXARRAY_NEW`, `GBXARRAY_ADD`, `GBXARRAY_GET`, `GBXARRAY_SET`,
  `GBXARRAY_INSERT`, `GBXARRAY_REMOVE`, `GBXARRAY_COUNT`, `GBXARRAY_CLEAR`,
  `GBXARRAY_FIND`, `GBXARRAY_SORT`, `GBXARRAY_JOIN$`, `GBXARRAY_FREE`.

## 11. Spiel als EXE weitergeben

```bash
build-static/gbx.exe --export spiel.exe meinprojekt.gbxproj
```

Quellen und die übersetzte INLINE-Bibliothek stecken in der EXE – sie startet
ohne Projektordner. **Assets bleiben draußen**: Der Export kopiert sie neben die
EXE, dort lassen sie sich austauschen, ohne neu zu packen. Für fremde Rechner
`gbx.exe` mit `-DGBX_STATIC_RUNTIME=ON` bauen (also `build-static/gbx.exe`),
sonst braucht die EXE die C++-Laufzeit-DLLs. In der IDE: Knopf **EXE** oder
Strg+E.
