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
#include "graphics.h"
#include "project.h"
#include "value.h"

struct Debugger;

struct Slot {
    Value v;
    TypeHint hint = TypeHint::None;
    bool isConst = false;
};

struct Runtime {
    ProjectConfig cfg;
    Gfx gfx;
    Audio audio;
    Program* prog = nullptr;
    Debugger* dbg = nullptr;  // gesetzt bei --debug

    bool headless = false;
    bool testsMode = false;
    bool quit = false;
    int exitCode = 0;

    int assertCount = 0;
    int assertFailures = 0;
    bool assertPrinted = false;

    std::unordered_map<std::string, Slot> globals;
    std::vector<std::string> warned;
    std::mt19937 rng{std::random_device{}()};
    std::string commandLine;
    std::string saveDir;  // Ziel fuer SAVEBMP/SAVESPRITE (bei Paketen neben der EXE)
    std::string currentFile;
    int currentLine = 0;
    uint32_t textColor = 0xFFFFFF;

    std::string resolveAsset(const std::string& rel) const;
    void warnOnce(const std::string& msg);
    bool fileExists(const std::string& p) const;
};

VT hintToVT(TypeHint h);
TypeHint vtToHint(VT t);

// Konstanten, die der Interpreter selbst bereitstellt (TRUE, FALSE, KEY_A ...).
// Wird beim Start registriert und von der Namenspruefung (--check) gebraucht.
std::vector<std::pair<std::string, int64_t>> builtinConstants();
