// GameBasiX - gemeinsame Typen und Fehlerbehandlung
#pragma once

#include <stdexcept>
#include <string>

struct SrcPos {
    std::string file;
    int line = 0;
};

// Ein Fehler im BASIC-Programm. compile=true bedeutet Syntax-/Semantikfehler,
// sonst Laufzeitfehler.
struct BasicError : std::runtime_error {
    SrcPos pos;
    bool compile;

    // Meldungen stehen im Quelltext auf Englisch; what() und der Debugger
    // geben sie unverändert weiter.
    BasicError(const std::string& msg, SrcPos p, bool isCompile = false)
        : std::runtime_error(msg), pos(std::move(p)), compile(isCompile) {}

    std::string location() const {
        std::string out = pos.file.empty() ? "<program>" : pos.file;
        if (pos.line > 0) out += ":" + std::to_string(pos.line);
        return out;
    }

    std::string fullMessage() const {
        return location() + ": " +
               (compile ? "Syntax error: " : "Runtime error: ") + what();
    }
};
