// GameBasiX - Namenspruefung fuer --check
#pragma once

#include <string>
#include <vector>

#include "ast.h"

struct LintIssue {
    SrcPos pos;
    std::string message;
};

// Meldet Namen, die das Programm nirgends definiert: unbekannte Befehle,
// Funktionen und Variablen. Prueft ausserdem die Argumentzahl bekannter
// Befehle, Funktionen und Methoden. Laeuft nach dem Parsen, damit die IDE
// schon beim Tippen Fehler sieht; zur Laufzeit bleibt GBX nachsichtig.
std::vector<LintIssue> lintProgram(const Program& prog);
