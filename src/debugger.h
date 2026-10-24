// GameBasiX - Laufzeit-Debugger
//
// Der Debugger haengt an der Ausfuehrung des Interpreters und haelt vor
// Anweisungen an, fuer die ein Haltepunkt gesetzt ist oder ein Einzelschritt
// laeuft. Gesteuert wird er ueber die Standardeingabe, Meldungen gehen als
// JSON-Zeilen auf die Standardausgabe.
//
// Das Programm selbst schreibt mit PRINT auf dieselbe Ausgabe. Damit sich
// beides trennen laesst, traegt jede Debug-Zeile die Vorsilbe "@@gbxdbg ".
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "common.h"

// Wird geworfen, wenn der Debugger die Sitzung beendet (Befehl "quit").
struct DebugQuit {};

// Maskiert einen Text fuer JSON (Anfuehrungszeichen, Steuerzeichen, UTF-8
// bleibt unveraendert).
std::string debugJsonEscape(const std::string& text);

struct Debugger {
    Debugger();
    ~Debugger();

    // Startet den Lesethread fuer die Befehle.
    void start();
    void shutdown();

    // Liefert den aktuellen Bereich (Funktion, Tiefe, lokale und globale
    // Variablen) als fertiges JSON-Objekt. Wird vom Interpreter gesetzt.
    std::function<std::string()> scope;

    // Wertet die Bedingung eines Haltepunkts aus. Setzt ok=false und error,
    // wenn der Ausdruck nicht lesbar ist.
    std::function<bool(const std::string&, bool&, std::string&)> condition;

    // Vor jeder Anweisung aufrufen. Blockiert, solange angehalten wird.
    void atStatement(const SrcPos& pos, int depth, const std::string& func);

    // Meldet einen Syntax- oder Laufzeitfehler und beendet die Sitzung.
    void reportError(const BasicError& error);

    void reportExit(int code);

    bool enabled() const { return enabled_; }

private:
    enum class Mode { Run, StepIn, StepOver, StepOut };

    void readerLoop();
    void handleCommand(const std::string& line);

    // Erwartet, dass der Aufrufer mutex_ haelt.
    void stopAt(std::unique_lock<std::mutex>& lock, const SrcPos& pos, int depth,
                const std::string& func, const char* reason, const std::string& cond,
                const std::string& condError);
    void emitJson(const std::string& body);
    void emitStopEvent(const SrcPos& pos, int depth, const std::string& func, const char* reason,
                       const std::string& cond, const std::string& condError);
    void emitNote(const std::string& text);
    void emitScope();
    void setMode(Mode mode, int depth);
    void resume();
    const std::string* breakpointFor(const SrcPos& pos) const;
    void setBreakpoint(const SrcPos& pos, bool on, const std::string& condition);
    static std::string keyFor(const SrcPos& pos);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread reader_;
    std::atomic<bool> enabled_{false};
    bool paused_ = false;
    bool pauseRequested_ = false;
    bool quit_ = false;
    bool started_ = false;
    bool modeConsumed_ = true;
    Mode mode_ = Mode::Run;
    int stepDepth_ = 0;
    std::string lastFile_;
    int lastLine_ = 0;
    std::map<std::string, std::string> breakpoints_;  // Schluessel -> Bedingung
};
