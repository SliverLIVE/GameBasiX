// GameBasiX - Lexer
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common.h"

enum class Tok { Ident, Int, Float, Str, Sym, Newline, Eof, Inline };

struct Token {
    Tok kind = Tok::Eof;
    std::string text;   // Identifier (in Grossbuchstaben), Symbol oder Stringinhalt
    int64_t ival = 0;
    double fval = 0.0;
    int line = 0;
    // Bei Tok::Inline: 0 = INLINE, 1 = INLINE DEFINE, 2 = INLINE RUN
    int mode = 0;
};

// Ein INLINE … ENDINLINE-Block: C++-Quelltext, der unveraendert an den
// Compiler geht. `line` ist die erste Codezeile in der .gbx-Datei.

// Zerlegt eine Quelldatei in Tokens. Wirft BasicError bei Lexerfehlern.
std::vector<Token> lexFile(const std::string& path, const std::string& src);

const char* tokenKindName(Tok k);
