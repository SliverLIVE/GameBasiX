// GameBasiX - INLINE-Bloecke (C++ im BASIC-Programm)
//
// Die Bloecke werden beim Start einmal uebersetzt (g++), als DLL/SO geladen
// und ihre Funktionen angemeldet. Danach sind sie ganz normale Befehle.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ast.h"
#include "value.h"

namespace gbxinline_runtime {

/** Gibt es INLINE-Bloecke? Ohne Bloecke wird nie ein Compiler gestartet. */
bool present(const Program& prog);

/** Gibt es Schritt-Bloecke (INLINE mitten im Code)? */
bool hasSteps(const Program& prog);

/** Zugriff auf die BASIC-Variablen an der Stelle eines Schritt-Blocks.
 *  Der Interpreter setzt das um; inline.cpp reicht es an die DLL weiter. */
struct StepScope {
    virtual ~StepScope() = default;
    /** Variable (Name wie im Interpreter, ohne Typ-Suffix); legt sie bei
     *  Bedarf im passenden Bereich an. nullptr nur, wenn das nicht geht. */
    virtual Value* var(const std::string& name) = 0;
    /** Bleibt der erste Fehler; der Interpreter wirft ihn nach dem Schritt. */
    virtual void fail(const std::string& message) = 0;

    /* Arrays aendern. item == nullptr bedeutet "Standardwert". Die
     * Rueckgabe meldet Erfolg; der Grund steht bei false schon in fail(). */
    virtual bool arrayPush(Value* array, const Value* item) = 0;
    virtual bool arrayInsert(Value* array, int64_t index, const Value* item) = 0;
    virtual bool arrayRemove(Value* array, int64_t index) = 0;
    virtual bool arrayClear(Value* array) = 0;
    virtual bool arrayResize(Value* array, const std::vector<int>& sizes) = 0;
    virtual int arrayRank(Value* array) = 0;
    virtual int arraySize(Value* array, int dim) = 0;

    /** Ruft einen eingebauten GBX-Befehl mit fertigen Werten auf. refs[i] ist
     *  gesetzt, wenn Argument i eine Variable ist (BYREF-Ziel). */
    virtual bool basicCall(const std::string& name, std::vector<Value>& args,
                           std::vector<Value*>& refs, Value& result) = 0;
};

/** Fuehrt den Schritt-Block mit diesem Index aus (er muss uebersetzt sein). */
void runStep(const Program& prog, size_t index, StepScope& scope);

/**
 * Zusaetzliche C++-Bibliotheken aus der Projektdatei ([native]): Archive
 * (.a/.o), Suchordner (-L) und weitere Flags. Sie landen in der Kommandozeile
 * des Compilers, der die INLINE-Bloecke uebersetzt, und gehen in den
 * Cache-Namen ein. Pfade muessen absolut sein; main.cpp loest die Angaben der
 * Projektdatei gegen den Projektordner auf.
 */
void setNativeOptions(std::vector<std::string> libs, std::vector<std::string> libDirs,
                      std::vector<std::string> flags);

/** Uebersetzt die Bloecke (mit Cache) und meldet ihre Funktionen an.
 *  Wirft BasicError mit der Compilermeldung, wenn das Uebersetzen scheitert.
 *  Ist eine fertige Bibliothek im Spielpaket (neben den Quellen), wird sie
 *  benutzt - ein ausgeliefertes Spiel braucht dann keinen Compiler. */
void load(const Program& prog);

/** Namen, die ein Block per GBX_INLINE_FN(...) anmeldet - fuer --check. */
std::vector<std::string> registeredNames(const Program& prog);

/** Kennung des erzeugten C++-Textes. Aendert sich ein Block, passt eine
 *  mitgelieferte Bibliothek nicht mehr und wird neu uebersetzt. */
std::string sourceSignature(const Program& prog);

/** Uebersetzt die Bloecke fuer ein Spielpaket und liefert den Pfad der DLL/SO.
 *  Wirft BasicError, wenn kein Compiler da ist oder das Uebersetzen scheitert. */
std::string compileForExport(const Program& prog);

/** Erzeugter C++-Text aller Bloecke (fuer den HTML5-Export, der ihn mit
 *  Emscripten fest dazulinkt). Ohne Bloecke bleibt er leer. */
std::string generatedCpp(const Program& prog);

/** Ist der Name nach dem Laden eine INLINE-Funktion? */
bool has(const std::string& name);

/** Ruft eine INLINE-Funktion mit BASIC-Werten auf. */
Value call(const std::string& name, const std::vector<Value>& args, const SrcPos& pos);

}  // namespace gbxinline_runtime
