// GameBasiX - Lexer
#include "lexer.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

const char* tokenKindName(Tok k) {
    switch (k) {
        case Tok::Ident: return "Bezeichner";
        case Tok::Int: return "Ganzzahl";
        case Tok::Float: return "Kommazahl";
        case Tok::Str: return "String";
        case Tok::Sym: return "Symbol";
        case Tok::Newline: return "Zeilenende";
        case Tok::Eof: return "Dateiende";
        case Tok::Inline: return "INLINE-Block";
    }
    return "?";
}

namespace {

bool isIdentStart(unsigned char c) {
    return std::isalpha(c) || c == '_';
}

bool isIdentChar(unsigned char c) {
    return std::isalnum(c) || c == '_';
}

std::string toUpperAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

/** Zeilenanfang? Nur Leerraum und Kommentare duerfen davor stehen. */
bool onlySpaceBefore(size_t start, size_t p, const std::string& src) {
    for (size_t i = start; i < p; ++i) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v') continue;
        return false;
    }
    return true;
}

/** Rest der Zeile ist leer (Kommentar erlaubt)? `q` zeigt hinter das Wort. */
bool restOfLineEmpty(size_t& q, const std::string& src) {
    const size_t n = src.size();
    while (q < n && src[q] != '\n' && src[q] != '\r') {
        const char c = src[q];
        if (c == '\'' || (c == '/' && q + 1 < n && src[q + 1] == '/')) return true;
        if (c != ' ' && c != '\t' && c != '\f' && c != '\v') return false;
        ++q;
    }
    return true;
}

}  // namespace

std::vector<Token> lexFile(const std::string& path, const std::string& src) {
    std::vector<Token> out;
    size_t p = 0;
    int line = 1;
    const size_t n = src.size();

    // UTF-8 BOM ueberspringen
    if (n >= 3 && static_cast<unsigned char>(src[0]) == 0xEF &&
        static_cast<unsigned char>(src[1]) == 0xBB &&
        static_cast<unsigned char>(src[2]) == 0xBF) {
        p = 3;
    }

    auto fail = [&](const std::string& msg, int atLine) {
        SrcPos pos{path, atLine};
        throw BasicError(msg, pos, true);
    };

    // Anfang der laufenden Zeile: ein INLINE-Block beginnt nur dort.
    size_t lineStart = p;

    // INLINE … ENDINLINE einsammeln. Der C++-Text geht wortgetreu an den
    // Compiler; `endLine` bekommt die Zeile des ENDINLINE zurueck (0, wenn
    // hier gar kein Block beginnt).
    auto scanInline = [&](size_t afterWord, int keywordLine, Token& block, int& endLine) -> size_t {
        size_t q = afterWord;
        while (q < n && (src[q] == ' ' || src[q] == '\t')) ++q;
        // INLINE DEFINE (Definitionen) und INLINE RUN (Schritt auf oberster
        // Ebene) stehen in derselben Zeile wie das Schluesselwort.
        int mode = 0;
        if (q < n && isIdentStart(static_cast<unsigned char>(src[q]))) {
            size_t end = q;
            while (end < n && isIdentChar(static_cast<unsigned char>(src[end]))) ++end;
            const std::string word = toUpperAscii(src.substr(q, end - q));
            if (word == "DEFINE") mode = 1;
            else if (word == "RUN") mode = 2;
            else return 0;
            q = end;
            while (q < n && (src[q] == ' ' || src[q] == '\t')) ++q;
        }
        if (!restOfLineEmpty(q, src)) return 0;
        if (q < n && src[q] == '\r') ++q;
        if (q < n && src[q] == '\n') ++q;
        block.kind = Tok::Inline;
        block.mode = mode;
        block.line = keywordLine + 1;
        std::string code;
        int atLine = keywordLine + 1;
        while (q < n) {
            size_t lineEnd = q;
            while (lineEnd < n && src[lineEnd] != '\n' && src[lineEnd] != '\r') ++lineEnd;
            const std::string raw = src.substr(q, lineEnd - q);
            size_t w = 0;
            while (w < raw.size() && (raw[w] == ' ' || raw[w] == '\t')) ++w;
            size_t wEnd = w;
            while (wEnd < raw.size() && isIdentChar(static_cast<unsigned char>(raw[wEnd]))) ++wEnd;
            if (toUpperAscii(raw.substr(w, wEnd - w)) == "ENDINLINE") {
                size_t rest = wEnd;
                if (restOfLineEmpty(rest, raw)) {
                    block.text = code;
                    endLine = atLine;
                    size_t next = lineEnd;
                    if (next < n && src[next] == '\r') ++next;
                    if (next < n && src[next] == '\n') ++next;
                    return next;
                }
            }
            code += raw;
            code += '\n';
            ++atLine;
            q = lineEnd;
            if (q < n && src[q] == '\r') ++q;
            if (q < n && src[q] == '\n') ++q;
        }
        fail("INLINE ohne ENDINLINE", keywordLine);
        return n;
    };

    while (p < n) {
        unsigned char c = static_cast<unsigned char>(src[p]);

        // Zeilenende
        if (c == '\n') {
            out.push_back(Token{Tok::Newline, "", 0, 0.0, line});
            ++p;
            ++line;
            lineStart = p;
            continue;
        }
        if (c == '\r') {
            ++p;
            if (p < n && src[p] == '\n') ++p;
            out.push_back(Token{Tok::Newline, "", 0, 0.0, line});
            ++line;
            lineStart = p;
            continue;
        }
        // Leerraum
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
            ++p;
            continue;
        }
        // Kommentare: ' und //
        if (c == '\'') {
            while (p < n && src[p] != '\n' && src[p] != '\r') ++p;
            continue;
        }
        if (c == '/' && p + 1 < n && src[p + 1] == '/') {
            while (p < n && src[p] != '\n' && src[p] != '\r') ++p;
            continue;
        }

        // Strings
        if (c == '"') {
            Token t;
            t.kind = Tok::Str;
            t.line = line;
            ++p;
            std::string val;
            bool closed = false;
            while (p < n) {
                char ch = src[p];
                if (ch == '"') {
                    if (p + 1 < n && src[p + 1] == '"') {  // "" -> "
                        val.push_back('"');
                        p += 2;
                        continue;
                    }
                    ++p;
                    closed = true;
                    break;
                }
                if (ch == '\\' && p + 1 < n) {
                    char e = src[p + 1];
                    switch (e) {
                        case 'n': val.push_back('\n'); p += 2; continue;
                        case 't': val.push_back('\t'); p += 2; continue;
                        case 'r': val.push_back('\r'); p += 2; continue;
                        case '\\': val.push_back('\\'); p += 2; continue;
                        case '"': val.push_back('"'); p += 2; continue;
                        case '0': val.push_back('\0'); p += 2; continue;
                        default: break;
                    }
                }
                if (ch == '\n') { ++line; }
                val.push_back(ch);
                ++p;
            }
            if (!closed) fail("String wurde nicht geschlossen", t.line);
            t.text = val;
            out.push_back(std::move(t));
            continue;
        }

        // Zahlen
        if (std::isdigit(c) || (c == '.' && p + 1 < n && std::isdigit(static_cast<unsigned char>(src[p + 1])))) {
            Token t;
            t.line = line;
            size_t start = p;
            bool isFloat = false;
            if (c == '0' && p + 1 < n && (src[p + 1] == 'x' || src[p + 1] == 'X')) {
                p += 2;
                size_t hs = p;
                while (p < n && std::isxdigit(static_cast<unsigned char>(src[p]))) ++p;
                if (p == hs) fail("Ungueltige Hexadezimalzahl", line);
                std::string hex = src.substr(hs, p - hs);
                t.kind = Tok::Int;
                t.ival = static_cast<int64_t>(std::strtoull(hex.c_str(), nullptr, 16));
                out.push_back(std::move(t));
                continue;
            }
            while (p < n && std::isdigit(static_cast<unsigned char>(src[p]))) ++p;
            if (p < n && src[p] == '.') {
                isFloat = true;
                ++p;
                while (p < n && std::isdigit(static_cast<unsigned char>(src[p]))) ++p;
            }
            if (p < n && (src[p] == 'e' || src[p] == 'E')) {
                size_t save = p;
                ++p;
                if (p < n && (src[p] == '+' || src[p] == '-')) ++p;
                if (p < n && std::isdigit(static_cast<unsigned char>(src[p]))) {
                    isFloat = true;
                    while (p < n && std::isdigit(static_cast<unsigned char>(src[p]))) ++p;
                } else {
                    p = save;
                }
            }
            std::string num = src.substr(start, p - start);
            if (isFloat) {
                t.kind = Tok::Float;
                t.fval = std::strtod(num.c_str(), nullptr);
            } else {
                t.kind = Tok::Int;
                t.ival = static_cast<int64_t>(std::strtoll(num.c_str(), nullptr, 10));
            }
            out.push_back(std::move(t));
            continue;
        }

        // Bezeichner (inkl. Typ-Suffix $ % #)
        if (isIdentStart(c)) {
            Token t;
            t.kind = Tok::Ident;
            t.line = line;
            size_t start = p;
            while (p < n && isIdentChar(static_cast<unsigned char>(src[p]))) ++p;
            if (p < n && (src[p] == '$' || src[p] == '%' || src[p] == '#')) ++p;
            t.text = toUpperAscii(src.substr(start, p - start));
            if (t.text == "INLINE" && onlySpaceBefore(lineStart, start, src)) {
                Token block;
                int endLine = 0;
                const size_t next = scanInline(p, line, block, endLine);
                if (next > 0) {
                    out.push_back(std::move(block));
                    // Zeilenende hinter ENDINLINE: die Anweisung ist zu Ende.
                    out.push_back(Token{Tok::Newline, "", 0, 0.0, endLine});
                    line = endLine + 1;
                    p = next;
                    lineStart = p;
                    continue;
                }
            }
            out.push_back(std::move(t));
            continue;
        }

        // Symbole
        if (c == '<' || c == '>') {
            Token t;
            t.kind = Tok::Sym;
            t.line = line;
            if (p + 1 < n && (src[p + 1] == '=' || (c == '<' && src[p + 1] == '>'))) {
                t.text = src.substr(p, 2);
                p += 2;
            } else {
                t.text = std::string(1, static_cast<char>(c));
                ++p;
            }
            out.push_back(std::move(t));
            continue;
        }
        static const char* singles = "+-*/=()[],.:;?^&|";
        if (std::strchr(singles, static_cast<char>(c)) != nullptr) {
            Token t;
            t.kind = Tok::Sym;
            t.text = std::string(1, static_cast<char>(c));
            t.line = line;
            ++p;
            out.push_back(std::move(t));
            continue;
        }

        fail(std::string("Unerwartetes Zeichen '") + static_cast<char>(c) + "'", line);
    }

    out.push_back(Token{Tok::Eof, "", 0, 0.0, line});
    return out;
}
