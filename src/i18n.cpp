// GameBasiX - Meldungen auf Deutsch und Englisch
#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace i18n {
namespace {

bool gEnglish = false;

// Deutsche Bausteine und ihre englische Fassung. Die Reihenfolge im Feld spielt
// keine Rolle: tr() probiert die längsten zuerst, damit „Konstante 'x' kann
// nicht geaendert werden“ nicht am kürzeren Baustein hängen bleibt.
const std::pair<const char*, const char*> kTable[] = {
    // ------------------------------------------------------ Parser
    {"Zeilenende erwartet, gefunden: '", "Line end expected, found: '"},
    {"Praeprozessor-Anweisungen (?IF, ?DEFINE ...) werden nicht unterstuetzt",
     "Preprocessor statements (?IF, ?DEFINE ...) are not supported"},
    {"METHOD gehoert in einen TYPE (TYPE ... METHOD name ... ENDMETHOD ... ENDTYPE)",
     "METHOD belongs inside a TYPE (TYPE ... METHOD name ... ENDMETHOD ... ENDTYPE)"},
    {"Nach ELSEIF wird eine neue Zeile erwartet", "ELSEIF must be followed by a new line"},
    {"In einem TYPE heissen Funktionen METHOD - schreibe: ",
     "Inside a TYPE these are called METHOD - replace "},
    {" durch METHOD ersetzen, Block mit ENDMETHOD beenden",
     " with METHOD and end the block with ENDMETHOD"},
    {"CASE nach DEFAULT ist nicht erlaubt", "CASE after DEFAULT is not allowed"},
    {"Unerwarteter Text nach dem Ausdruck", "Unexpected text after the expression"},
    {"Anweisung erwartet, gefunden: ", "Statement expected, found: "},
    {"TO ist nur bei 'CASE a TO b' erlaubt", "TO is only allowed in 'CASE a TO b'"},
    {"Fehlervariable nach CATCH erwartet", "Error variable after CATCH expected"},
    {"Marken- oder SUB-Name erwartet", "Label or SUB name expected"},
    {"INCLUDE muss auf oberster Ebene stehen", "INCLUDE must be at top level"},
    {"Zyklischer INCLUDE: ", "Cyclic INCLUDE: "},
    {"Ausdruck erwartet, gefunden: ", "Expression expected, found: "},
    {"Dateiname als String erwartet", "File name as string expected"},
    {"Block nicht beendet, '", "Block not closed, '"},
    {"Typname nach AS erwartet", "Type name expected after AS"},
    {"CASE oder DEFAULT erwartet", "CASE or DEFAULT expected"},
    {"Feldname nach '.' erwartet", "Field name expected after '.'"},
    {" darf nur auf oberster Ebene stehen", " may only appear at top level"},
    {"Unerwartetes '", "Unexpected '"},
    {"Zaehlvariable erwartet", "Loop variable expected"},
    {"Laufvariable erwartet", "Loop variable expected"},
    {"Anweisung erwartet", "Statement expected"},
    {"Variablenname erwartet", "Variable name expected"},
    {"Konstantenname erwartet", "Constant name expected"},
    {"Parametername erwartet", "Parameter name expected"},
    {"Nach dem Funktionsnamen wird ':' erwartet - FUNCTION name: parameter",
     "':' expected after the function name - FUNCTION name: parameter"},
    {"Nach dem SUB-Namen wird ':' erwartet - SUB name:",
     "':' expected after the SUB name - SUB name:"},
    {"Nach dem Methodennamen wird ':' erwartet - METHOD name: parameter",
     "':' expected after the method name - METHOD name: parameter"},
    {"Methodenname erwartet", "Method name expected"},
    {"Funktionsname erwartet", "Function name expected"},
    {"Array-Name erwartet", "Array name expected"},
    {"Feldname erwartet", "Field name expected"},
    {"Typname erwartet", "Type name expected"},
    {"Variable erwartet", "Variable expected"},
    {"Zeilenende erwartet", "Line end expected"},
    {"ENDIF erwartet", "ENDIF expected"},
    {"ENDSUB erwartet", "ENDSUB expected"},
    {"ENDFUNCTION erwartet", "ENDFUNCTION expected"},
    {"ENDMETHOD erwartet", "ENDMETHOD expected"},
    {"ENDSELECT fehlt", "ENDSELECT missing"},
    {"ENDTYPE fehlt", "ENDTYPE missing"},
    {"TO erwartet", "TO expected"},
    {"NEXT erwartet", "NEXT expected"},
    {"WEND erwartet", "WEND expected"},
    {"UNTIL erwartet", "UNTIL expected"},
    {"IN erwartet", "IN expected"},
    {"FINALLY erwartet", "FINALLY expected"},
    {"' erwartet", "' expected"},

    // ------------------------------------------------------------ Lexer
    {"String wurde nicht geschlossen", "String is not terminated"},
    {"Ungueltige Hexadezimalzahl", "Invalid hexadecimal number"},
    {"Unerwartetes Zeichen '", "Unexpected character '"},

    // ------------------------------------------------------- Interpreter
    {"Zu viele verschachtelte Funktionsaufrufe (Rekursion)",
     "Too many nested function calls (recursion)"},
    {"Methodenaufruf: links vom Punkt steht kein TYPE",
     "Method call: left of the dot is not a TYPE"},
    {"Schleife laeuft zu lange (WHILE)", "Loop runs too long (WHILE)"},
    {"Schleife laeuft zu lange (REPEAT)", "Loop runs too long (REPEAT)"},
    {"FOR-Schleife laeuft zu lange", "FOR loop runs too long"},
    {"DIM: Array-Groesse fehlt (z.B. DIM a[10])", "DIM: array size missing (e.g. DIM a[10])"},
    {"BYREF-Argument muss eine Variable sein", "BYREF argument must be a variable"},
    {"Zuweisung: Ausdruck ist kein Array", "Assignment: expression is not an array"},
    {"Zuweisung: Ausdruck ist kein TYPE", "Assignment: expression is not a TYPE"},
    {"Unbekannter Befehl oder Funktion: ", "Unknown command or function: "},
    {"DIMDEL: Index ausserhalb des Arrays", "DIMDEL: index outside the array"},
    {"DIM: hoechstens 4 Dimensionen", "DIM: at most 4 dimensions"},
    {"FOR: STEP darf nicht 0 sein", "FOR: STEP must not be 0"},
    {"Konstante kann nicht geaendert werden", "Constant cannot be changed"},
    {"Falsche Anzahl Argumente fuer ", "Wrong number of arguments for "},
    {"IIF erwartet 3 Argumente", "IIF expects 3 arguments"},
    {"TYPE-Schachtelung zu tief: ", "TYPE nesting too deep: "},
    {"Ungueltiges Zuweisungsziel", "Invalid assignment target"},
    {"Marke nicht gefunden: ", "Label not found: "},
    {"DIMDEL: kein Array", "DIMDEL: not an array"},
    {"FOREACH: kein Array", "FOREACH: not an array"},
    {"DIM: Array zu gross", "DIM: array too large"},
    {"Division durch Null", "Division by zero"},
    {"Ausdruck ist kein Array", "Expression is not an array"},
    {"Ausdruck ist kein TYPE", "Expression is not a TYPE"},
    {"Unbekannter TYPE: ", "Unknown TYPE: "},
    {"Array-Index ", "Array index "},
    {" ausserhalb von ", " outside of "},
    {"' hat kein Feld '", "' has no field '"},
    {"' hat keine Methode '", "' has no method '"},
    {"' kann nicht geaendert werden", "' cannot be changed"},
    {"' ist kein Array", "' is not an array"},
    {"' nicht gefunden", "' not found"},
    {"push: nur fuer eindimensionale Arrays (mehrdimensionale mit resize aendern)",
     "push: only for one-dimensional arrays (use resize for multi-dimensional ones)"},
    {"insert: nur fuer eindimensionale Arrays (mehrdimensionale mit resize aendern)",
     "insert: only for one-dimensional arrays (use resize for multi-dimensional ones)"},
    {"remove: nur fuer eindimensionale Arrays (mehrdimensionale mit resize aendern)",
     "remove: only for one-dimensional arrays (use resize for multi-dimensional ones)"},
    {"clear: nur fuer eindimensionale Arrays (mehrdimensionale mit resize aendern)",
     "clear: only for one-dimensional arrays (use resize for multi-dimensional ones)"},
    {"resize: 1 bis 4 Dimensionen", "resize: 1 to 4 dimensions"},
    {"resize: Groesse darf nicht negativ sein", "resize: size must not be negative"},
    {"resize: Array zu gross", "resize: array too large"},
    {"insert: Index ", "insert: index "},
    {"remove: Index ", "remove: index "},
    {" ausserhalb des Arrays", " outside the array"},
    {"push: Wert fehlt", "push: value missing"},
    {"insert: Wert fehlt", "insert: value missing"},
    {"Array erwartet", "array expected"},
    {"size: Dimension muss 0 oder groesser sein", "size: dimension must be 0 or greater"},
    {"Unbekannter GBX-Befehl: ", "Unknown GBX command: "},
    {"GBX-Befehl ohne Namen", "GBX command without a name"},
    {"GBX-Befehle gibt es nur in einem INLINE-Schritt",
     "GBX commands are only available in an INLINE step"},

    // ---------------------------------------------------------- Builtins
    {"Ausgabevariable erwartet (Argument ", "Output variable expected (argument "},
    {"erwartet eine Variable als erstes Argument", "expects a variable as first argument"},
    {"Zu wenige Argumente", "Too few arguments"},
    {"SQR: negative Zahl", "SQR: negative number"},
    {"ASSERT FEHLGESCHLAGEN", "ASSERT FAILED"},

    // -------------------------------------------------------------- Lint
    {"Unbekannter Befehl: ", "Unknown command: "},
    {"Unbekannte Funktion: ", "Unknown function: "},
    {"Unbekannte Methode: ", "Unknown method: "},
    {"Unbekanntes Unterprogramm: ", "Unknown subroutine: "},
    {"Unbekanntes Feld: ", "Unknown field: "},
    {"Unbekannte Marke: ", "Unknown label: "},
    {"Unbekannter Name: ", "Unknown name: "},
    {" - meintest du ", " - did you mean "},

    // ----------------------------------------------------------- Projekt
    {"Projektdatei nicht gefunden: ", "Project file not found: "},
    {"Ungueltige Sektion in Projektdatei", "Invalid section in project file"},
    {"Zeile ohne '=' in Projektdatei: ", "Line without '=' in project file: "},
    {"Quelldatei nicht gefunden: ", "Source file not found: "},

    // ------------------------------------------------------------- Main
    {"--stdin ist nur zusammen mit --check moeglich.",
     "--stdin only works together with --check."},
    {"Nur eine Datei kann gestartet werden.", "Only one file can be started."},
    {"Unbekannte Option: ", "Unknown option: "},
    {" braucht einen Wert.", " needs a value."},
    {"Interner Fehler: ", "Internal error: "},
    {"Grafik konnte nicht initialisiert werden.", "Graphics could not be initialised."},
    {"Syntaxfehler: ", "Syntax error: "},
    {"Laufzeitfehler: ", "Runtime error: "},
    {"Fehler: ", "Error: "},
    {" Hauptanweisungen, ", " main statements, "},
    {" Funktionen, ", " functions, "},
    {" Pruefungen, ", " checks, "},
    {" fehlgeschlagen", " failed"},
    {" fehlt", " missing"},

    // ------------------------------------------------------ Grafik/Audio
    {"TTF-Unterstuetzung ist in diesem Build nicht aktiviert",
     "TTF support is not enabled in this build"},
    {"SPRITE_GRAB ist im Headless-Modus nicht verfuegbar",
     "SPRITE_GRAB is not available in headless mode"},
    {"SCREEN_SAVE_BMP ist im Headless-Modus nicht verfuegbar",
     "SCREEN_SAVE_BMP is not available in headless mode"},
    {"SPRITE_GRAB: Konvertierung fehlgeschlagen", "SPRITE_GRAB: conversion failed"},
    {"SCREEN_SAVE_BMP: Konvertierung fehlgeschlagen", "SCREEN_SAVE_BMP: conversion failed"},
    {"SPRITE_GRAB: ungueltige Groesse", "SPRITE_GRAB: invalid size"},
    {"SCREEN_SAVE_BMP: Bildschirm konnte nicht gelesen werden: ",
     "SCREEN_SAVE_BMP: screen could not be read: "},
    {"SPRITE_SAVE: Schreiben fehlgeschlagen: ", "SPRITE_SAVE: writing failed: "},
    {"SCREEN_SAVE_BMP: Schreiben fehlgeschlagen: ", "SCREEN_SAVE_BMP: writing failed: "},
    {"SPRITE_SAVE: Sprite ", "SPRITE_SAVE: sprite "},
    {" hat keine Bilddaten", " has no image data"},
    {"BACKGROUND_LOAD: Textur konnte nicht erstellt werden", "BACKGROUND_LOAD: texture could not be created"},
    {"Bild konnte nicht geladen werden: ", "Image could not be loaded: "},
    {"Textur konnte nicht erstellt werden: ", "Texture could not be created: "},
    {"SPRITE_GRAB fehlgeschlagen: ", "SPRITE_GRAB failed: "},
    {"TTF-Schrift konnte nicht geladen werden: ", "TTF font could not be loaded: "},
    {"TTF-Schriftgroesse nicht unterstuetzt: ", "TTF font size not supported: "},
    {"FreeType konnte nicht initialisiert werden", "FreeType could not be initialised"},
    {"Musik konnte nicht geladen werden (nur WAV): ",
     "Music could not be loaded (WAV only): "},
    {"Musikdatei enthaelt keine Audiodaten: ", "Music file contains no audio data: "},
    {"Sound enthaelt keine Audiodaten: ", "Sound contains no audio data: "},
    {"Sound konnte nicht geladen werden: ", "Sound could not be loaded: "},
    {"Audiokonvertierung fehlgeschlagen", "Audio conversion failed"},
    {"Audio konnte nicht initialisiert werden: ", "Audio could not be initialised: "},
    {"Audiogeraet konnte nicht geoeffnet werden: ", "Audio device could not be opened: "},
    {"SDL_Init fehlgeschlagen: ", "SDL_Init failed: "},
    {"Fenster konnte nicht erstellt werden: ", "Window could not be created: "},
    {"Renderer konnte nicht erstellt werden: ", "Renderer could not be created: "},
};

// Längste Bausteine zuerst, damit spezielle Texte vor allgemeinen greifen.
const std::vector<std::pair<std::string, std::string>>& table() {
    static const std::vector<std::pair<std::string, std::string>> entries = [] {
        std::vector<std::pair<std::string, std::string>> list;
        for (const auto& [de, en] : kTable) list.emplace_back(de, en);
        std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
            return a.first.size() > b.first.size();
        });
        return list;
    }();
    return entries;
}

}  // namespace

void setLanguage(const std::string& code) {
    std::string lower = code;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    gEnglish = lower.rfind("en", 0) == 0 || lower == "english";
}

bool english() { return gEnglish; }

std::string tr(const std::string& text) {
    if (!gEnglish || text.empty()) return text;
    std::string out = text;
    for (const auto& [de, en] : table()) {
        size_t pos = 0;
        while ((pos = out.find(de, pos)) != std::string::npos) {
            out.replace(pos, de.size(), en);
            pos += en.size();
        }
    }
    return out;
}

}  // namespace i18n
