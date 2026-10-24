// GameBasiX - gemeinsame Typen und Fehlerbehandlung
#pragma once

#include <stdexcept>
#include <string>

#include "i18n.h"

struct SrcPos {
    std::string file;
    int line = 0;
};

// Ein Fehler im BASIC-Programm. compile=true bedeutet Syntax-/Semantikfehler,
// sonst Laufzeitfehler.
struct BasicError : std::runtime_error {
    SrcPos pos;
    bool compile;

    // Die Meldung wird beim Entstehen übersetzt, damit what() und der
    // Debugger dieselbe Sprache liefern wie die übrigen Ausgaben.
    BasicError(const std::string& msg, SrcPos p, bool isCompile = false)
        : std::runtime_error(i18n::tr(msg)), pos(std::move(p)), compile(isCompile) {}

    std::string location() const {
        std::string out = pos.file.empty() ? "<program>" : pos.file;
        if (pos.line > 0) out += ":" + std::to_string(pos.line);
        return out;
    }

    std::string fullMessage() const {
        return location() + ": " +
               (compile ? i18n::tr("Syntaxfehler: ") : i18n::tr("Laufzeitfehler: ")) + what();
    }
};
