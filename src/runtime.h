// GameBasiX - Laufzeitumgebung
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ast.h"
#include "audio.h"
#include "fileio.h"
#include "graphics.h"
#include "hotreload.h"
#include "net.h"
#include "project.h"
#include "steam.h"
#include "value.h"

struct Debugger;
class Interpreter;

struct Slot {
    Value v;
    TypeHint hint = TypeHint::None;
    bool isConst = false;
};

struct Runtime {
    ProjectConfig cfg;
    Gfx gfx;
    Audio audio;
    HotReload hot;  // Assets, die im laufenden Programm nachgeladen werden
    FileSet files;
    Net net;
    Steam steam;
    std::string netError;  // letzter Fehler von NET_ERROR$
    std::string netFrom;   // Absender des letzten UDP-Pakets (UDP_FROM$)
    Program* prog = nullptr;
    Debugger* dbg = nullptr;  // gesetzt bei --debug
    Interpreter* interp = nullptr;  // fuer TASK_START und SPAWN
    bool worker = false;            // true: reine Rechenfunktion auf einem Job-Thread

    bool headless = false;
    bool testsMode = false;
    // OPTION STRICT / --strict: eine nie zugewiesene Variable ist ein Fehler
    // statt still 0.
    bool strict = false;
    bool quit = false;
    int exitCode = 0;

    int assertCount = 0;
    int assertFailures = 0;
    bool assertPrinted = false;

    std::unordered_map<std::string, Slot> globals;
    std::vector<std::string> warned;
    std::mt19937 rng{std::random_device{}()};
    std::string commandLine;
    std::string saveDir;  // Ziel fuer SCREEN_SAVE_BMP/SPRITE_SAVE (bei Paketen neben der EXE)
    std::string currentFile;
    int currentLine = 0;
    Color textColor = 0xFFFFFF;

    std::string resolveAsset(const std::string& rel) const;
    void warnOnce(const std::string& msg);
    bool fileExists(const std::string& p) const;
};

VT hintToVT(TypeHint h);
TypeHint vtToHint(VT t);

// Konstanten, die der Interpreter selbst bereitstellt (TRUE, FALSE, KEY_A ...).
// Wird beim Start registriert und von der Namenspruefung (--check) gebraucht.
std::vector<std::pair<std::string, int64_t>> builtinConstants();
