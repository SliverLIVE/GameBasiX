// GameBasiX - Parser
#include "parser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string upperWord(const std::string& text) {
    std::string out = text;
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

ExprP makeInt(SrcPos p, int64_t v) {
    auto e = std::make_unique<Expr>();
    e->kind = EK::IntLit;
    e->pos = std::move(p);
    e->ival = v;
    return e;
}

bool isTerminator(const Token& t, const std::vector<std::string>& names) {
    if (t.kind != Tok::Ident) return false;
    for (const auto& n : names)
        if (t.text == n) return true;
    return false;
}

// Aehlicher bekannter Name - nur bei eindeutigem Tippfehler.
int editDistance(const std::string& a, const std::string& b) {
    std::vector<int> previous(b.size() + 1);
    std::vector<int> current(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) previous[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        current[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
        }
        previous = current;
    }
    return previous[b.size()];
}

std::string closestWord(const std::vector<std::string>& names, const std::string& want) {
    if (want.size() < 3) return "";
    const int limit = want.size() > 4 ? 2 : 1;
    std::string best;
    int bestScore = limit + 1;
    for (const auto& name : names) {
        if (std::abs(static_cast<int>(name.size()) - static_cast<int>(want.size())) > limit) continue;
        const int score = editDistance(want, name);
        if (score < bestScore) {
            best = name;
            bestScore = score;
        }
    }
    return best;
}

std::string readFileOrThrow(const std::string& path, SrcPos pos) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw BasicError("File not found: " + path, std::move(pos), true);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

Parser::Parser(std::string path, std::vector<Token> tokens, Program& program)
    : path_(std::move(path)), toks_(std::move(tokens)), prog_(program) {}

const Token& Parser::peek(int n) const {
    size_t idx = pos_ + static_cast<size_t>(n);
    if (idx >= toks_.size()) return toks_.back();
    return toks_[idx];
}

void Parser::advance() {
    if (pos_ + 1 < toks_.size()) ++pos_;
}

bool Parser::isSym(const char* s) const {
    return cur().kind == Tok::Sym && cur().text == s;
}

bool Parser::isIdent(const char* s) const {
    return cur().kind == Tok::Ident && cur().text == s;
}

bool Parser::acceptSym(const char* s) {
    if (isSym(s)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::acceptIdent(const char* s) {
    if (isIdent(s)) {
        advance();
        return true;
    }
    return false;
}

void Parser::expectSym(const char* s) {
    if (!acceptSym(s)) error(std::string("'") + s + "' expected");
}

void Parser::skipNewlines() {
    while (cur().kind == Tok::Newline) advance();
}

void Parser::expectLineEnd() {
    if (cur().kind == Tok::Newline) {
        advance();
        return;
    }
    if (cur().kind == Tok::Eof) return;
    error("Line end expected, found: '" + cur().text + "'");
}

void Parser::error(const std::string& msg) {
    throw BasicError(msg, SrcPos{path_, cur().line}, true);
}

TypeHint Parser::suffixHint(const std::string& name) const {
    if (name.empty()) return TypeHint::None;
    switch (name.back()) {
        case '$': return TypeHint::Str;
        case '%': return TypeHint::Int;
        case '#': return TypeHint::Float;
        default: return TypeHint::None;
    }
}

std::string Parser::stripSuffix(const std::string& name) const {
    if (!name.empty() && (name.back() == '$' || name.back() == '%' || name.back() == '#'))
        return name.substr(0, name.size() - 1);
    return name;
}

std::shared_ptr<TypeDef> Parser::findOrCreateType(const std::string& name) {
    auto it = prog_.types.find(name);
    if (it != prog_.types.end()) return it->second;
    auto def = std::make_shared<TypeDef>();
    def->name = name;
    prog_.types[name] = def;
    return def;
}

std::string Parser::parseTypeNameIfPresent() {
    if (!acceptIdent("AS")) return "";
    if (cur().kind != Tok::Ident) error("Type name expected after AS");
    std::string name = stripSuffix(cur().raw);
    advance();
    return name;
}

// ---------------------------------------------------------------- Ausdruecke

ExprP Parser::parseExpr() { return parseOr(); }

ExprP Parser::parseExpression() {
    skipNewlines();
    ExprP expr = parseExpr();
    skipNewlines();
    if (cur().kind != Tok::Eof) error("Unexpected text after the expression");
    return expr;
}

ExprP Parser::parseOr() {
    ExprP left = parseAnd();
    while (isIdent("OR")) {
        SrcPos p{path_, cur().line};
        advance();
        ExprP right = parseAnd();
        auto e = std::make_unique<Expr>();
        e->kind = EK::Binary;
        e->pos = p;
        e->bin = BinOp::Or;
        e->a = std::move(left);
        e->b = std::move(right);
        left = std::move(e);
    }
    return left;
}

ExprP Parser::parseAnd() {
    ExprP left = parseNot();
    while (isIdent("AND")) {
        SrcPos p{path_, cur().line};
        advance();
        ExprP right = parseNot();
        auto e = std::make_unique<Expr>();
        e->kind = EK::Binary;
        e->pos = p;
        e->bin = BinOp::And;
        e->a = std::move(left);
        e->b = std::move(right);
        left = std::move(e);
    }
    return left;
}

ExprP Parser::parseNot() {
    if (isIdent("NOT")) {
        SrcPos p{path_, cur().line};
        advance();
        auto e = std::make_unique<Expr>();
        e->kind = EK::Unary;
        e->pos = p;
        e->un = UnOp::Not;
        e->a = parseNot();
        return e;
    }
    return parseCompare();
}

ExprP Parser::parseCompare() {
    ExprP left = parseAdd();
    for (;;) {
        BinOp op;
        if (isSym("=")) op = BinOp::Eq;
        else if (isSym("<>")) op = BinOp::Ne;
        else if (isSym("<=")) op = BinOp::Le;
        else if (isSym(">=")) op = BinOp::Ge;
        else if (isSym("<")) op = BinOp::Lt;
        else if (isSym(">")) op = BinOp::Gt;
        else break;
        SrcPos p{path_, cur().line};
        advance();
        ExprP right = parseAdd();
        auto e = std::make_unique<Expr>();
        e->kind = EK::Binary;
        e->pos = p;
        e->bin = op;
        e->a = std::move(left);
        e->b = std::move(right);
        left = std::move(e);
    }
    return left;
}

ExprP Parser::parseAdd() {
    ExprP left = parseMul();
    for (;;) {
        BinOp op;
        if (isSym("+")) op = BinOp::Add;
        else if (isSym("-")) op = BinOp::Sub;
        else break;
        SrcPos p{path_, cur().line};
        advance();
        ExprP right = parseMul();
        auto e = std::make_unique<Expr>();
        e->kind = EK::Binary;
        e->pos = p;
        e->bin = op;
        e->a = std::move(left);
        e->b = std::move(right);
        left = std::move(e);
    }
    return left;
}

ExprP Parser::parseMul() {
    ExprP left = parseUnary();
    for (;;) {
        BinOp op;
        if (isSym("*")) op = BinOp::Mul;
        else if (isSym("/")) op = BinOp::Div;
        else break;
        SrcPos p{path_, cur().line};
        advance();
        ExprP right = parseUnary();
        auto e = std::make_unique<Expr>();
        e->kind = EK::Binary;
        e->pos = p;
        e->bin = op;
        e->a = std::move(left);
        e->b = std::move(right);
        left = std::move(e);
    }
    return left;
}

ExprP Parser::parseUnary() {
    if (isSym("-")) {
        SrcPos p{path_, cur().line};
        advance();
        auto e = std::make_unique<Expr>();
        e->kind = EK::Unary;
        e->pos = p;
        e->un = UnOp::Neg;
        e->a = parseUnary();
        return e;
    }
    if (isSym("+")) {
        SrcPos p{path_, cur().line};
        advance();
        auto e = std::make_unique<Expr>();
        e->kind = EK::Unary;
        e->pos = p;
        e->un = UnOp::Plus;
        e->a = parseUnary();
        return e;
    }
    return parsePostfix();
}

ExprP Parser::parsePostfix() {
    ExprP e = parsePrimary();
    for (;;) {
        if (isSym("[")) {
            SrcPos p{path_, cur().line};
            advance();
            if (acceptSym("]")) {
                // "a[]" - Zugriff auf das komplette Array (ARRAY_BOUNDS, LEN, FOREACH ...)
                continue;
            }
            // map[x, y] ist die Kurzform von map[x][y]: jede Dimension bekommt
            // einen eigenen Index-Knoten, damit Auswertung und Zuweisung
            // unveraendert bleiben.
            for (ExprP& idx : parseBracketList()) {
                auto node = std::make_unique<Expr>();
                node->kind = EK::Index;
                node->pos = p;
                node->a = std::move(e);
                node->b = std::move(idx);
                e = std::move(node);
            }
            continue;
        }
        if (isSym(".")) {
            SrcPos p{path_, cur().line};
            advance();
            if (cur().kind != Tok::Ident) error("Field name expected after '.'");
            std::string field = stripSuffix(cur().raw);
            advance();
            auto node = std::make_unique<Expr>();
            node->pos = p;
            node->text = field;
            node->a = std::move(e);
            // p.flaeche(...) ruft eine Methode des TYPEs auf.
            if (isSym("(")) {
                node->kind = EK::MethodCall;
                node->args = parseArgList(true);
            } else {
                node->kind = EK::Field;
            }
            e = std::move(node);
            continue;
        }
        break;
    }
    return e;
}

ExprP Parser::parsePrimary() {
    const Token& t = cur();
    SrcPos p{path_, t.line};

    // @name: Verweis auf eine FUNCTION als Wert (Rueckruf).
    if (isSym("@")) {
        advance();
        if (cur().kind != Tok::Ident) error("Function name expected after @");
        auto e = std::make_unique<Expr>();
        e->kind = EK::FuncRef;
        e->pos = p;
        e->text = stripSuffix(cur().raw);
        advance();
        return e;
    }

    if (t.kind == Tok::Int) {
        advance();
        return makeInt(p, t.ival);
    }
    if (t.kind == Tok::Float) {
        advance();
        auto e = std::make_unique<Expr>();
        e->kind = EK::FloatLit;
        e->pos = p;
        e->fval = t.fval;
        return e;
    }
    if (t.kind == Tok::Str) {
        advance();
        auto e = std::make_unique<Expr>();
        e->kind = EK::StrLit;
        e->pos = p;
        e->text = t.text;
        return e;
    }
    if (isSym("(")) {
        advance();
        ExprP e = parseExpr();
        expectSym(")");
        return e;
    }
    if (t.kind == Tok::Ident) {
        std::string name = stripSuffix(t.raw);
        advance();
        if (upperWord(name) == "TRUE") return makeInt(p, 1);
        if (upperWord(name) == "FALSE") return makeInt(p, 0);
        if (isSym("(")) {
            auto e = std::make_unique<Expr>();
            e->kind = EK::Call;
            e->pos = p;
            e->text = t.raw;  // Suffix bleibt: STR$(), MID$(), ...
            e->args = parseArgList(true);
            return e;
        }
        auto e = std::make_unique<Expr>();
        e->kind = EK::Var;
        e->pos = p;
        e->text = name;
        e->hint = suffixHint(t.text);
        return e;
    }
    error(std::string("Expression expected, found: ") + tokenKindName(t.kind) +
          (t.text.empty() ? "" : " '" + t.text + "'"));
}

std::vector<ExprP> Parser::parseArgList(bool parens) {
    std::vector<ExprP> args;
    if (parens) {
        expectSym("(");
        if (acceptSym(")")) return args;
        for (;;) {
            args.push_back(parseExpr());
            if (acceptSym(",")) continue;
            expectSym(")");
            break;
        }
        return args;
    }
    if (cur().kind == Tok::Newline || cur().kind == Tok::Eof) return args;
    for (;;) {
        args.push_back(parseExpr());
        if (acceptSym(",")) continue;
        break;
    }
    return args;
}

// Nach einer geoeffneten Klammer: die Werte bis zur schliessenden Klammer.
// `[x, y]` liefert zwei Ausdruecke, `[x][y]` bleibt bei zwei Klammern.
std::vector<ExprP> Parser::parseBracketList() {
    std::vector<ExprP> values;
    for (;;) {
        values.push_back(parseExpr());
        if (acceptSym(",")) continue;
        expectSym("]");
        break;
    }
    return values;
}

ExprP Parser::parseLValue() {
    if (cur().kind != Tok::Ident) error("Variable expected");
    SrcPos p{path_, cur().line};
    auto e = std::make_unique<Expr>();
    e->kind = EK::Var;
    e->pos = p;
    e->text = stripSuffix(cur().raw);
    e->hint = suffixHint(cur().text);
    advance();
    for (;;) {
        if (isSym("[")) {
            advance();
            // "a[] = 5" hat kein Ziel: das waere das ganze Array als Wert.
            if (acceptSym("]"))
                error("Index missing: '" + e->text + "[]' ist kein Zuweisungsziel");
            for (ExprP& idx : parseBracketList()) {
                auto node = std::make_unique<Expr>();
                node->kind = EK::Index;
                node->pos = p;
                node->a = std::move(e);
                node->b = std::move(idx);
                e = std::move(node);
            }
            continue;
        }
        if (isSym(".")) {
            advance();
            if (cur().kind != Tok::Ident) error("Field name expected after '.'");
            std::string field = stripSuffix(cur().raw);
            advance();
            auto node = std::make_unique<Expr>();
            node->pos = p;
            node->text = field;
            node->a = std::move(e);
            // p.flaeche(...) ruft eine Methode des TYPEs auf.
            if (isSym("(")) {
                node->kind = EK::MethodCall;
                node->args = parseArgList(true);
            } else {
                node->kind = EK::Field;
            }
            e = std::move(node);
            continue;
        }
        break;
    }
    return e;
}

// ---------------------------------------------------------------- Anweisungen

void Parser::parseFile() {
    // Diese Datei steht ab jetzt auf dem Stapel: ein INCLUDE, das wieder
    // hierher zurueckfuehrt, ist ein Zyklus und keine Dublette.
    prog_.includeStack.push_back(path_);
    struct StackGuard {
        std::vector<std::string>* stack;
        ~StackGuard() { stack->pop_back(); }
    } guard{&prog_.includeStack};
    while (cur().kind != Tok::Eof) {
        if (cur().kind == Tok::Newline) {
            advance();
            continue;
        }
        parseTopLevel();
    }
}

void Parser::parseTopLevel() {
    if (isIdent("FUNCTION")) {
        parseFunction(false);
        return;
    }
    if (isIdent("SUB")) {
        parseFunction(true);
        return;
    }
    if (isIdent("TASK")) {
        parseTask();
        return;
    }
    if (isIdent("TYPE")) {
        parseType();
        return;
    }
    if (isIdent("INTERFACE")) {
        parseInterface();
        return;
    }
    if (isIdent("INCLUDE")) {
        parseInclude();
        return;
    }
    if (cur().kind == Tok::Inline) {
        // Der Lexer hat den C++-Block schon wortgetreu eingesammelt.
        if (StmtP step = takeInlineBlock(true)) prog_.main.push_back(std::move(step));
        return;
    }
    // Alles andere ist Teil des Hauptprogramms.
    prog_.main.push_back(parseStatement());
    if (cur().kind == Tok::Newline) advance();
    else if (isSym(":")) advance();
    else if (cur().kind != Tok::Eof) error("Line end expected");
}

StmtP Parser::takeInlineBlock(bool topLevel) {
    InlineBlock block;
    block.pos = SrcPos{path_, cur().line};
    block.code = cur().text;
    block.scope = scope_;
    block.scopeType = scopeType_;
    const int mode = cur().mode;  // 0 = INLINE, 1 = INLINE DEFINE, 2 = INLINE RUN
    // Position entscheidet: oberste Ebene sind Definitionen, im Code laeuft
    // der Block an seiner Stelle. DEFINE/RUN heben das ausdruecklich auf.
    block.step = mode == 2 || (mode == 0 && !topLevel);
    const int index = static_cast<int>(prog_.inlines.size());
    prog_.inlines.push_back(std::move(block));
    advance();
    if (cur().kind == Tok::Newline) advance();
    else if (isSym(":")) advance();
    else if (cur().kind != Tok::Eof) error("Line end expected");

    if (!prog_.inlines[static_cast<size_t>(index)].step) return nullptr;
    auto s = std::make_unique<Stmt>();
    s->kind = SK::InlineStep;
    s->pos = prog_.inlines[static_cast<size_t>(index)].pos;
    s->inlineIndex = index;
    return s;
}

void Parser::parseInclude() {
    SrcPos p{path_, cur().line};
    advance();  // INCLUDE
    if (cur().kind != Tok::Str) error("File name as string expected");
    std::string rel = cur().text;
    advance();

    fs::path base = fs::path(path_).parent_path();
    fs::path target = fs::path(rel);
    if (target.is_relative()) target = base / target;
    std::string full = target.lexically_normal().string();

    std::error_code ec;
    // Zyklus: die Zieldatei wird gerade selbst geparst (a.gbx -> main.gbx).
    for (const auto& active : prog_.includeStack) {
        const bool same = active == full ||
                          (fs::exists(active, ec) && fs::exists(full, ec) &&
                           fs::equivalent(active, full, ec));
        if (!same) continue;
        std::string chain;
        for (const auto& step : prog_.includeStack) {
            if (!chain.empty()) chain += " -> ";
            chain += fs::path(step).filename().string();
        }
        chain += " -> " + fs::path(full).filename().string();
        throw BasicError("Cyclic INCLUDE: " + chain, p, true);
    }
    for (const auto& seen : prog_.includedFiles) {
        if (seen == full) return;
        if (fs::exists(seen, ec) && fs::exists(full, ec) && fs::equivalent(seen, full, ec))
            return;  // bereits eingebunden
    }

    std::string src = readFileOrThrow(full, p);
    prog_.includedFiles.push_back(full);
    Parser sub(full, lexFile(full, src), prog_);
    sub.parseFile();
}

std::vector<StmtP> Parser::parseBlock(const std::vector<std::string>& terminators) {
    std::vector<StmtP> out;
    for (;;) {
        skipNewlines();
        if (cur().kind == Tok::Eof) {
            error(std::string("Block not closed, '") + terminators.back() + "' missing");
        }
        if (isTerminator(cur(), terminators)) break;
        if (cur().kind == Tok::Inline) {
            // Im Code wird der Block zu einem Schritt, der hier laeuft;
            // INLINE DEFINE hebt ihn wieder auf die Dateiebene.
            if (StmtP step = takeInlineBlock(false)) out.push_back(std::move(step));
            continue;
        }
        if (isSym(":")) {
            advance();
            continue;
        }
        out.push_back(parseStatement());
        if (cur().kind == Tok::Newline) {
            advance();
        } else if (isSym(":")) {
            advance();
        } else if (isTerminator(cur(), terminators) || cur().kind == Tok::Eof) {
            // ok
        } else {
            error("Line end expected, found: '" + cur().text + "'");
        }
    }
    return out;
}

StmtP Parser::parseStatement() {
    if (cur().kind != Tok::Ident) {
        if (cur().kind == Tok::Sym && cur().text == "?")
            error("Preprocessor statements (?IF, ?DEFINE ...) are not supported");
        if (cur().kind == Tok::Inline) error("INLINE must start its own line");
        error("Statement expected, found: " + std::string(tokenKindName(cur().kind)));
    }

    const std::string kw = cur().text;

    auto mk = [&](SK kind) {
        auto s = std::make_unique<Stmt>();
        s->kind = kind;
        s->pos = SrcPos{path_, cur().line};
        return s;
    };

    if (kw == "METHOD" || kw == "ENDMETHOD")
        error("METHOD belongs inside a TYPE (TYPE ... METHOD name ... ENDMETHOD ... ENDTYPE)");
    if (kw == "TASK" || kw == "ENDTASK")
        error("TASK may only appear at top level");
    if (kw == "AWAIT") {
        auto s = mk(SK::Await);
        advance();
        if (isIdent("FRAME")) {
            s->name = "FRAME";
            advance();
        } else if (cur().kind != Tok::Newline && cur().kind != Tok::Eof && !isSym(":")) {
            s->e1 = parseExpr();
        }
        return s;
    }
    if (kw == "OPTION") {
        auto s = mk(SK::Nop);
        advance();
        if (cur().kind != Tok::Ident) error("STRICT expected after OPTION");
        const std::string what = stripSuffix(cur().raw);
        if (upperWord(what) != "STRICT")
            error("OPTION " + what + " is not supported (only OPTION STRICT)");
        advance();
        prog_.strictOption = true;
        return s;
    }
    if (kw == "IF") return parseIf();
    if (kw == "FOR") return parseFor();
    if (kw == "WHILE") return parseWhile();
    if (kw == "REPEAT") return parseRepeat();
    if (kw == "SELECT") return parseSelect();
    if (kw == "DIM") return parseDim(false);
    if (kw == "REDIM") return parseDim(true);
    if (kw == "GLOBAL") return parseDecl(DeclScope::Global);
    if (kw == "LOCAL") return parseDecl(DeclScope::Local);
    if (kw == "STATIC") return parseDecl(DeclScope::Static);
    if (kw == "CONSTANT" || kw == "CONST") return parseConstant();
    if (kw == "ENUM") return parseEnum();
    if (kw == "INC") return parseIncDec(+1);
    if (kw == "DEC") return parseIncDec(-1);
    if (kw == "DIMPUSH") return parsePush(false);
    if (kw == "DIMDEL") return parsePush(true);
    if (kw == "FOREACH") return parseForeach();
    if (kw == "TRY") return parseTry();
    if (kw == "SWAP") return parseSwap();
    if (kw == "THROW") {
        auto s = mk(SK::Throw);
        advance();
        s->e1 = parseExpr();
        return s;
    }
    if (kw == "RETURN") {
        auto s = mk(SK::Return);
        advance();
        if (cur().kind != Tok::Newline && cur().kind != Tok::Eof && !isSym(":"))
            s->e1 = parseExpr();
        return s;
    }
    if (kw == "EXIT") {  // vorzeitiges Verlassen von FUNCTION/SUB
        auto s = mk(SK::Return);
        advance();
        return s;
    }
    if (kw == "END") {
        auto s = mk(SK::End);
        advance();
        if (cur().kind != Tok::Newline && cur().kind != Tok::Eof && !isSym(":"))
            s->e1 = parseExpr();
        return s;
    }
    if (kw == "BREAK") {
        auto s = mk(SK::Break);
        advance();
        return s;
    }
    if (kw == "CONTINUE") {
        auto s = mk(SK::Continue);
        advance();
        return s;
    }
    if (kw == "GOTO" || kw == "GOSUB") {
        auto s = mk(kw == "GOTO" ? SK::Goto : SK::Gosub);
        advance();
        if (cur().kind != Tok::Ident) error("Label or SUB name expected");
        s->name = stripSuffix(cur().raw);
        advance();
        return s;
    }
    if (kw == "LET") {
        advance();
        return parseAssignmentOrCall();
    }
    if (kw == "FUNCTION" || kw == "SUB" || kw == "TYPE" || kw == "INTERFACE") {
        error(kw + " may only appear at top level");
    }
    if (kw == "INCLUDE") {
        error("INCLUDE must be at top level");
    }
    if (kw == "ELSE" || kw == "ELSEIF" || kw == "ENDIF" || kw == "WEND" ||
        kw == "NEXT" || kw == "UNTIL" || kw == "CASE" || kw == "DEFAULT" ||
        kw == "ENDSELECT" || kw == "ENDFUNCTION" || kw == "ENDSUB" ||
        kw == "ENDTYPE" || kw == "CATCH" || kw == "FINALLY" || kw == "ENDTRY" ||
        kw == "ENDENUM" || kw == "ENDINTERFACE" || kw == "TO" || kw == "STEP") {
        error("Unexpected '" + kw + "'");
    }
    if (kw == "AS" || kw == "BYREF" || kw == "IN") {
        error("Unexpected '" + kw + "'");
    }

    return parseAssignmentOrCall();
}

StmtP Parser::parseAssignmentOrCall() {
    size_t save = pos_;
    bool saved = false;
    ExprP target;
    if (cur().kind == Tok::Ident) {
        // Marke: "name:" am Zeilenanfang
        if (peek(1).kind == Tok::Sym && peek(1).text == ":" &&
            (peek(2).kind == Tok::Newline || peek(2).kind == Tok::Eof)) {
            auto s = std::make_unique<Stmt>();
            s->kind = SK::Label;
            s->pos = SrcPos{path_, cur().line};
            s->name = stripSuffix(cur().raw);
            advance();
            advance();
            return s;
        }
        target = parseLValue();
        saved = true;
        if (isSym("=")) {
            auto s = std::make_unique<Stmt>();
            s->kind = SK::Assign;
            s->pos = target->pos;
            advance();
            s->e1 = std::move(target);
            s->e2 = parseExpr();
            return s;
        }
        // Kurzform: x += 1, x -= 1, x *= 2, x /= 2
        if (isSym("+") || isSym("-") || isSym("*") || isSym("/")) {
            const std::string op = cur().text;
            const Token& after = peek(1);
            if (after.kind == Tok::Sym && after.text == "=") {
                auto s = std::make_unique<Stmt>();
                s->kind = SK::Assign;
                s->compound = true;
                s->bin = op == "+" ? BinOp::Add
                         : op == "-" ? BinOp::Sub
                         : op == "*" ? BinOp::Mul
                                     : BinOp::Div;
                s->pos = target->pos;
                advance();  // Operator
                advance();  // =
                s->e1 = std::move(target);
                s->e2 = parseExpr();
                return s;
            }
        }
        // p.methode(...) als Anweisung (ohne Zuweisung).
        if (target->kind == EK::MethodCall) {
            auto s = std::make_unique<Stmt>();
            s->kind = SK::MethodCall;
            s->pos = target->pos;
            s->e1 = std::move(target);
            return s;
        }
    }

    if (saved) pos_ = save;

    if (cur().kind != Tok::Ident) error("Statement expected");
    auto s = std::make_unique<Stmt>();
    s->kind = SK::Call;
    s->pos = SrcPos{path_, cur().line};
    s->name = cur().raw;
    const std::string cmd = s->name;
    advance();

    // "BEFEHL(arg, ...)" nur dann als Klammeraufruf werten, wenn die
    // schliessende Klammer am Zeilenende steht. Sonst ist "BEFEHL (a+b)*c"
    // ein Befehl, dessen erster Ausdruck mit einer Klammer beginnt.
    bool parenCall = false;
    if (isSym("(")) {
        int depth = 0;
        size_t i = pos_;
        for (; i < toks_.size(); ++i) {
            if (toks_[i].kind == Tok::Sym && toks_[i].text == "(") ++depth;
            else if (toks_[i].kind == Tok::Sym && toks_[i].text == ")") {
                --depth;
                if (depth == 0) break;
            } else if (toks_[i].kind == Tok::Eof) {
                break;
            }
        }
        if (i + 1 < toks_.size()) {
            const Token& after = toks_[i + 1];
            if (after.kind == Tok::Newline || after.kind == Tok::Eof ||
                (after.kind == Tok::Sym && after.text == ":"))
                parenCall = true;
        }
    }
    if (parenCall) {
        s->exprs = parseArgList(true);
        return s;
    }
    (void)cmd;
    s->exprs = parseArgList(false);
    return s;
}

StmtP Parser::parseIf() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::If;
    s->pos = SrcPos{path_, cur().line};
    advance();  // IF
    s->e1 = parseExpr();
    acceptIdent("THEN");

    if (cur().kind != Tok::Newline) {
        // Einzeiler: IF ... THEN anweisung [ELSE anweisung]
        s->body.push_back(parseStatement());
        while (acceptSym(":")) {
            if (cur().kind == Tok::Newline || cur().kind == Tok::Eof) break;
            s->body.push_back(parseStatement());
        }
        if (acceptIdent("ELSE")) {
            s->body2.push_back(parseStatement());
            while (acceptSym(":")) {
                if (cur().kind == Tok::Newline || cur().kind == Tok::Eof) break;
                s->body2.push_back(parseStatement());
            }
        }
        return s;
    }

    advance();  // Zeilenende
    s->body = parseBlock({"ELSE", "ELSEIF", "ENDIF"});

    // ELSEIF-Zweige als Kette verschachtelter IF-Statements im Sonst-Zweig.
    Stmt* tail = s.get();
    while (isIdent("ELSEIF")) {
        SrcPos p{path_, cur().line};
        advance();  // ELSEIF
        auto nested = std::make_unique<Stmt>();
        nested->kind = SK::If;
        nested->pos = p;
        nested->e1 = parseExpr();
        acceptIdent("THEN");
        if (cur().kind != Tok::Newline) error("ELSEIF must be followed by a new line");
        advance();
        nested->body = parseBlock({"ELSE", "ELSEIF", "ENDIF"});
        tail->body2.push_back(std::move(nested));
        tail = tail->body2.back().get();
    }

    if (acceptIdent("ELSE")) {
        advance();  // Zeilenende
        tail->body2 = parseBlock({"ENDIF"});
    }
    if (!acceptIdent("ENDIF")) error("ENDIF expected");
    return s;
}

StmtP Parser::parseFor() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::For;
    s->pos = SrcPos{path_, cur().line};
    advance();  // FOR
    if (cur().kind != Tok::Ident) error("Loop variable expected");
    s->name = stripSuffix(cur().raw);
    s->hint = suffixHint(cur().text);
    advance();
    expectSym("=");
    s->e1 = parseExpr();
    if (!acceptIdent("TO")) error("TO expected");
    s->e2 = parseExpr();
    if (acceptIdent("STEP")) s->e3 = parseExpr();
    expectLineEnd();
    s->body = parseBlock({"NEXT"});
    if (!acceptIdent("NEXT")) error("NEXT expected");
    if (cur().kind == Tok::Ident) advance();  // optionaler Zaehlername
    return s;
}

StmtP Parser::parseWhile() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::While;
    s->pos = SrcPos{path_, cur().line};
    advance();
    s->e1 = parseExpr();
    expectLineEnd();
    s->body = parseBlock({"WEND", "ENDWHILE"});
    if (!acceptIdent("WEND") && !acceptIdent("ENDWHILE")) error("WEND expected");
    return s;
}

StmtP Parser::parseRepeat() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::Repeat;
    s->pos = SrcPos{path_, cur().line};
    advance();
    expectLineEnd();
    s->body = parseBlock({"UNTIL"});
    if (!acceptIdent("UNTIL")) error("UNTIL expected");
    s->e1 = parseExpr();
    return s;
}

StmtP Parser::parseSelect() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::Select;
    s->pos = SrcPos{path_, cur().line};
    advance();
    s->e1 = parseExpr();
    expectLineEnd();

    bool sawDefault = false;
    for (;;) {
        skipNewlines();
        if (isIdent("ENDSELECT")) {
            advance();
            break;
        }
        if (cur().kind == Tok::Eof) error("ENDSELECT missing");
        CaseBlock cb;
        if (acceptIdent("DEFAULT")) {
            sawDefault = true;
            expectLineEnd();
            cb.body = parseBlock({"CASE", "DEFAULT", "ENDSELECT"});
            s->cases.push_back(std::move(cb));
            continue;
        }
        if (!acceptIdent("CASE")) error("CASE or DEFAULT expected");
        if (sawDefault) error("CASE after DEFAULT is not allowed");
        for (;;) {
            CaseTest t;
            SrcPos p{path_, cur().line};
            (void)p;
            if (isSym(">")) { t.cmp = 3; advance(); }
            else if (isSym("<")) { t.cmp = 1; advance(); }
            else if (isSym(">=")) { t.cmp = 4; advance(); }
            else if (isSym("<=")) { t.cmp = 2; advance(); }
            else if (isSym("<>")) { t.cmp = 5; advance(); }
            else if (isSym("=")) { t.cmp = 0; advance(); }
            t.lo = parseExpr();
            if (acceptIdent("TO")) {
                if (t.cmp != 0) error("TO is only allowed in 'CASE a TO b'");
                t.hi = parseExpr();
            }
            cb.tests.push_back(std::move(t));
            if (acceptSym(",")) continue;
            break;
        }
        expectLineEnd();
        cb.body = parseBlock({"CASE", "DEFAULT", "ENDSELECT"});
        s->cases.push_back(std::move(cb));
    }
    return s;
}

StmtP Parser::parseDim(bool redim) {
    auto s = std::make_unique<Stmt>();
    s->kind = redim ? SK::Redim : SK::Dim;
    s->pos = SrcPos{path_, cur().line};
    advance();
    if (cur().kind != Tok::Ident) error("Array name expected");
    s->name = stripSuffix(cur().raw);
    s->hint = suffixHint(cur().text);
    advance();
    // DIM p.items[10]: ein Feld-Array darf auch direkt angesprochen werden. Die
    // Klammern dahinter sind die Groessen, nicht ein Index.
    ExprP member;
    while (isSym(".")) {
        advance();
        if (cur().kind != Tok::Ident) error("Field name expected after '.'");
        auto node = std::make_unique<Expr>();
        node->kind = EK::Field;
        node->pos = s->pos;
        node->text = stripSuffix(cur().raw);
        if (member) node->a = std::move(member);
        else {
            auto root = std::make_unique<Expr>();
            root->kind = EK::Var;
            root->pos = s->pos;
            root->text = s->name;
            node->a = std::move(root);
        }
        member = std::move(node);
        advance();
    }
    s->e1 = std::move(member);
    while (acceptSym("[")) {
        if (acceptSym("]")) error("Array size expected (e.g. DIM a[10])");
        for (ExprP& size : parseBracketList()) s->exprs.push_back(std::move(size));
    }
    std::string tn = parseTypeNameIfPresent();
    if (!tn.empty()) {
        TypeHint h = TypeHint::None;
        if (upperWord(tn) == "INTEGER" || upperWord(tn) == "INT" || upperWord(tn) == "LONG") h = TypeHint::Int;
        else if (upperWord(tn) == "FLOAT" || upperWord(tn) == "DOUBLE" || upperWord(tn) == "NUMBER") h = TypeHint::Float;
        else if (upperWord(tn) == "STRING") h = TypeHint::Str;
        if (h != TypeHint::None) s->hint = h;
        else s->typeName = tn;
    }
    return s;
}

StmtP Parser::parseDecl(DeclScope scope) {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::Decl;
    s->scope = scope;
    s->pos = SrcPos{path_, cur().line};
    advance();
    // Mehrere Deklarationen durch Komma getrennt werden zu mehreren Statements;
    // hier wird nur die erste zurueckgegeben und der Rest als Block angehaengt.
    std::vector<StmtP> extra;
    for (;;) {
        if (cur().kind != Tok::Ident) error("Variable name expected");
        auto d = std::make_unique<Stmt>();
        d->kind = SK::Decl;
        d->scope = scope;
        d->pos = SrcPos{path_, cur().line};
        d->name = stripSuffix(cur().raw);
        d->hint = suffixHint(cur().text);
        advance();
        // GLOBAL/LOCAL/STATIC kennen auch die Array-Form: die Klammern tun
        // dasselbe wie bei DIM, nur in einem festen Bereich. Mehrere Werte in
        // einer Klammer sind mehrere Dimensionen (feld[3, 4] = feld[3][4]).
        while (acceptSym("[")) {
            if (acceptSym("]")) {
                // `GLOBAL a[] AS T`: offenes Array, das leer startet und erst
                // durch DIMPUSH Elemente bekommt. Der leere Ausdruck steht
                // fuer die Groesse 0.
                d->exprs.push_back(ExprP{});
                continue;
            }
            for (ExprP& size : parseBracketList()) d->exprs.push_back(std::move(size));
        }
        if (!d->exprs.empty()) {
            d->kind = SK::Dim;
            d->scopeExplicit = true;
        }
        std::string tn = parseTypeNameIfPresent();
        if (!tn.empty()) {
            if (upperWord(tn) == "INTEGER" || upperWord(tn) == "INT" || upperWord(tn) == "LONG") d->hint = TypeHint::Int;
            else if (upperWord(tn) == "FLOAT" || upperWord(tn) == "DOUBLE" || upperWord(tn) == "NUMBER") d->hint = TypeHint::Float;
            else if (upperWord(tn) == "STRING") d->hint = TypeHint::Str;
            else d->typeName = tn;
        }
        if (isSym("=")) {
            // Ein Array bekommt keine Anfangswerte - dafuer gibt es DIMDATA.
            if (d->kind == SK::Dim)
                error("Array without initial value: '" + d->name + "[...]' has no '=' (see DIMDATA)");
            advance();
            d->e1 = parseExpr();
        }
        extra.push_back(std::move(d));
        if (acceptSym(",")) continue;
        break;
    }
    // Rueckgabe: erstes Statement, Rest in einem Nop-Block (wird als
    // Anweisungsfolge ausgefuehrt).
    if (extra.size() == 1) return std::move(extra[0]);
    auto holder = std::make_unique<Stmt>();
    holder->kind = SK::Nop;
    holder->pos = s->pos;
    holder->body = std::move(extra);
    return holder;
}

StmtP Parser::parseConstant() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::ConstDecl;
    s->pos = SrcPos{path_, cur().line};
    advance();
    if (cur().kind != Tok::Ident) error("Constant name expected");
    s->name = stripSuffix(cur().raw);
    s->hint = suffixHint(cur().text);
    advance();
    std::string tn = parseTypeNameIfPresent();
    if (!tn.empty()) {
        if (upperWord(tn) == "INTEGER" || upperWord(tn) == "INT") s->hint = TypeHint::Int;
        else if (upperWord(tn) == "FLOAT" || upperWord(tn) == "DOUBLE" || upperWord(tn) == "NUMBER") s->hint = TypeHint::Float;
        else if (upperWord(tn) == "STRING") s->hint = TypeHint::Str;
    }
    expectSym("=");
    s->e1 = parseExpr();
    return s;
}

// ENUM … ENDENUM: jeder Wert wird eine Konstante (GLOBAL, wie CONSTANT).
// Ohne Zuweisung zaehlt der Wert eins weiter als der vorige, der erste ist 0.
StmtP Parser::parseEnum() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::EnumDecl;
    s->pos = SrcPos{path_, cur().line};
    advance();  // ENUM
    if (cur().kind != Tok::Ident) error("Name for the ENUM expected");
    s->name = stripSuffix(cur().raw);
    advance();
    expectLineEnd();

    // Jeder Wert heisst "PHASE.STOP": nur so koennen zwei ENUMs denselben
    // kurzen Namen anbieten. Den Punkt loest resolveEnumRefs() in jedem
    // Ausdruck wieder auf.
    std::vector<std::string>& values = prog_.enums[s->name];
    std::string previous;
    for (;;) {
        skipNewlines();
        if (cur().kind == Tok::Eof)
            error("Block not closed, 'ENDENUM' missing (ENUM '" + s->name + "')");
        if (isIdent("ENDENUM")) {
            advance();
            break;
        }
        if (cur().kind != Tok::Ident) error("Name of an ENUM value expected");

        auto member = std::make_unique<Stmt>();
        member->kind = SK::ConstDecl;
        member->pos = SrcPos{path_, cur().line};
        const std::string bare = stripSuffix(cur().raw);
        member->name = s->name + "." + bare;
        member->hint = TypeHint::Int;
        if (std::find(values.begin(), values.end(), bare) == values.end()) {
            values.push_back(bare);
            prog_.enumValues[bare].push_back(s->name);
        }
        advance();

        if (acceptSym("=")) {
            member->e1 = parseExpr();
        } else if (previous.empty()) {
            member->e1 = makeInt(member->pos, 0);
        } else {
            // voriger Wert + 1 - zur Laufzeit, damit auch = 0xFF weiterzaehlt.
            auto var = std::make_unique<Expr>();
            var->kind = EK::Var;
            var->pos = member->pos;
            var->text = previous;
            auto sum = std::make_unique<Expr>();
            sum->kind = EK::Binary;
            sum->bin = BinOp::Add;
            sum->pos = member->pos;
            sum->a = std::move(var);
            sum->b = makeInt(member->pos, 1);
            member->e1 = std::move(sum);
        }
        previous = member->name;
        s->body.push_back(std::move(member));

        if (cur().kind == Tok::Newline) advance();
        else if (isSym(":")) advance();
        else if (isIdent("ENDENUM")) continue;
        else if (cur().kind != Tok::Eof) error("Line end expected, found: '" + cur().text + "'");
    }
    if (s->body.empty()) error("ENUM without values: " + s->name);
    return s;
}

StmtP Parser::parseIncDec(int delta) {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::IncDec;
    s->delta = delta;
    s->pos = SrcPos{path_, cur().line};
    advance();
    s->e1 = parseLValue();
    if (acceptSym(",")) s->e2 = parseExpr();
    return s;
}

StmtP Parser::parsePush(bool del) {
    auto s = std::make_unique<Stmt>();
    s->kind = del ? SK::DimDel : SK::Push;
    s->pos = SrcPos{path_, cur().line};
    advance();
    if (cur().kind != Tok::Ident) error("Array name expected");
    auto var = std::make_unique<Expr>();
    var->kind = EK::Var;
    var->pos = s->pos;
    var->text = stripSuffix(cur().raw);
    advance();
    // DIMPUSH p.items[], x: ein Feld-Array waechst genauso.
    while (isSym(".")) {
        advance();
        if (cur().kind != Tok::Ident) error("Field name expected after '.'");
        auto node = std::make_unique<Expr>();
        node->kind = EK::Field;
        node->pos = s->pos;
        node->text = stripSuffix(cur().raw);
        node->a = std::move(var);
        var = std::move(node);
        advance();
    }
    if (acceptSym("[")) expectSym("]");
    s->e1 = std::move(var);
    expectSym(",");
    s->e2 = parseExpr();
    return s;
}

StmtP Parser::parseForeach() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::Foreach;
    s->pos = SrcPos{path_, cur().line};
    advance();
    if (cur().kind != Tok::Ident) error("Loop variable expected");
    s->name = stripSuffix(cur().raw);
    s->hint = suffixHint(cur().text);
    advance();
    if (!acceptIdent("IN")) error("IN expected");
    s->e1 = parseExpr();
    expectLineEnd();
    s->body = parseBlock({"NEXT"});
    if (!acceptIdent("NEXT")) error("NEXT expected");
    return s;
}

StmtP Parser::parseTry() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::Try;
    s->pos = SrcPos{path_, cur().line};
    advance();
    expectLineEnd();
    s->body = parseBlock({"CATCH", "FINALLY", "ENDTRY"});

    // Ein oder mehrere CATCH-Zweige. Ein Zweig ohne WHEN faengt alles, ein
    // Zweig mit WHEN nur die Fehler, deren Bedingung wahr ist.
    while (isIdent("CATCH")) {
        CatchClause clause;
        advance();
        if (cur().kind != Tok::Ident) error("Error variable expected after CATCH");
        clause.name = stripSuffix(cur().raw);
        advance();
        if (acceptIdent("WHEN")) clause.guard = parseExpr();
        expectLineEnd();
        clause.body = parseBlock({"CATCH", "FINALLY", "ENDTRY"});
        s->catches.push_back(std::move(clause));
    }
    if (acceptIdent("FINALLY")) {
        if (finallyStartsBody()) {
            s->finallyBody = parseFinallyBody();
            acceptIdent("ENDTRY");
        }
        // Ohne ENDTRY bleibt FINALLY das alte Blockende ohne eigenen Code.
        return s;
    }
    if (!acceptIdent("ENDTRY")) error("ENDTRY expected");
    return s;
}

StmtP Parser::parseSwap() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::Swap;
    s->pos = SrcPos{path_, cur().line};
    advance();
    s->e1 = parseLValue();
    expectSym(",");
    s->e2 = parseLValue();
    return s;
}

/**
 * Parameterliste einer Funktion oder Methode: `NAME: a, b$` oder `NAME(a, b)`.
 * Ein optionales ':' davor wird hier geschluckt.
 */
// Der Doppelpunkt hinter dem Namen ist Pflicht und wurde vom Aufrufer schon
// gelesen - er gehoert damit zur Kopfzeile von FUNCTION, SUB und METHOD.
void Parser::parseParamList(std::vector<Param>& params) {
    const bool parens = acceptSym("(");
    if (parens && acceptSym(")")) return;
    if (!parens && (cur().kind == Tok::Newline || cur().kind == Tok::Eof)) return;
    for (;;) {
        Param p;
        p.byref = acceptIdent("BYREF");
        if (cur().kind != Tok::Ident) error("Parameter name expected");
        p.name = stripSuffix(cur().raw);
        p.hint = suffixHint(cur().text);
        advance();
        std::string tn = parseTypeNameIfPresent();
        if (!tn.empty()) {
            if (upperWord(tn) == "INTEGER" || upperWord(tn) == "INT") p.hint = TypeHint::Int;
            else if (upperWord(tn) == "FLOAT" || upperWord(tn) == "DOUBLE" || upperWord(tn) == "NUMBER") p.hint = TypeHint::Float;
            else if (upperWord(tn) == "STRING") p.hint = TypeHint::Str;
            else p.typeName = tn;
        }
        if (acceptSym("=")) p.def = parseExpr();
        params.push_back(std::move(p));
        if (acceptSym(",")) continue;
        if (parens) expectSym(")");
        break;
    }
}

/**
 * METHOD innerhalb eines TYPE. Der Rumpf sieht die Felder des TYPEs als
 * normale Variablen; `p.flaeche()` bindet p beim Aufruf als Objekt.
 */
void Parser::parseMethod(const std::string& typeName) {
    FuncDef def;
    def.ownerType = typeName;
    def.pos = SrcPos{path_, cur().line};
    advance();  // METHOD
    if (cur().kind != Tok::Ident) error("Method name expected");
    const std::string methodName = stripSuffix(cur().raw);
    def.name = methodName;
    advance();
    if (!acceptSym(":"))
        error("':' expected after the method name - METHOD name: parameter");
    parseParamList(def.params);
    expectLineEnd();
    const std::string key = typeName + "." + methodName;
    const std::string previousScope = scope_;
    const std::string previousType = scopeType_;
    scope_ = key;
    scopeType_ = typeName;
    def.body = parseBlock({"ENDMETHOD"});
    scope_ = previousScope;
    scopeType_ = previousType;
    if (!acceptIdent("ENDMETHOD")) error("ENDMETHOD expected");

    if (prog_.funcs.find(key) != prog_.funcs.end()) {
        throw BasicError("Method '" + methodName + "' is already defined in TYPE '" + typeName +
                             "' is already defined",
                         def.pos, true);
    }
    prog_.funcs[key] = std::move(def);
    auto type = prog_.types.find(typeName);
    if (type != prog_.types.end() && type->second) type->second->methods.push_back(methodName);
}

void Parser::parseTask() {
    const SrcPos p{path_, cur().line};
    advance();  // TASK
    if (cur().kind != Tok::Ident) error("Task name expected");
    const std::string name = stripSuffix(cur().raw);
    advance();
    if (isSym(":")) advance();
    if (cur().kind != Tok::Newline && cur().kind != Tok::Eof)
        error("Line end expected after TASK name");
    expectLineEnd();

    TaskDef def;
    def.name = name;
    def.pos = p;
    def.body = parseBlock({"ENDTASK"});
    if (!acceptIdent("ENDTASK")) error("ENDTASK expected");

    if (prog_.tasks.count(name))
        throw BasicError("TASK already defined: " + name, p, true);
    prog_.tasks[name] = std::move(def);
}

void Parser::parseFunction(bool isSub) {
    FuncDef def;
    def.isSub = isSub;
    def.pos = SrcPos{path_, cur().line};
    advance();  // FUNCTION / SUB
    if (cur().kind != Tok::Ident) error("Function name expected");
    def.name = stripSuffix(cur().raw);
    advance();
    // Kopfzeile immer mit Doppelpunkt: FUNCTION name: ... / SUB name: ...
    if (isSub) {
        if (!acceptSym(":")) error("':' expected after the SUB name - SUB name:");
        parseParamList(def.params);  // SUBs haben keine Parameter
    } else {
        if (!acceptSym(":"))
            error("':' expected after the function name - FUNCTION name: parameter");
        parseParamList(def.params);
    }

    expectLineEnd();
    const std::string previousScope = scope_;
    const std::string previousType = scopeType_;
    scope_ = def.name;
    scopeType_.clear();
    def.body = parseBlock({isSub ? "ENDSUB" : "ENDFUNCTION"});
    scope_ = previousScope;
    scopeType_ = previousType;
    if (!acceptIdent(isSub ? "ENDSUB" : "ENDFUNCTION"))
        error(isSub ? "ENDSUB expected" : "ENDFUNCTION expected");

    auto prev = prog_.funcs.find(def.name);
    if (prev != prog_.funcs.end())
        throw BasicError("Function '" + def.name + "' is already defined", def.pos, true);
    prog_.funcs[def.name] = std::move(def);
}

void Parser::parseType() {
    SrcPos p{path_, cur().line};
    advance();  // TYPE
    if (cur().kind != Tok::Ident) error("Type name expected");
    std::string name = stripSuffix(cur().raw);
    advance();
    auto def = findOrCreateType(name);
    parseTypeTail(def);
    def->fields.clear();
    // `x% = 0`: Standardwert je Feld; nullptr steht für „ohne Angabe“.
    std::vector<ExprP> defaults;
    // Groessen der Feld-Arrays, parallel zu den Feldern.
    std::vector<std::vector<ExprP>> arrayDims;
    expectLineEnd();

    for (;;) {
        skipNewlines();
        if (isIdent("ENDTYPE")) {
            advance();
            break;
        }
        if (cur().kind == Tok::Eof) error("ENDTYPE missing");
        if (isIdent("METHOD")) {
            parseMethod(name);
            continue;
        }
        if (isIdent("FUNCTION") || isIdent("SUB")) {
            error("Inside a TYPE these are called METHOD - replace " + cur().text +
                  " with METHOD and end the block with ENDMETHOD");
        }
        if (cur().kind != Tok::Ident) error("Field name expected");
        std::string fname = stripSuffix(cur().raw);
        TypeHint hint = suffixHint(cur().text);
        advance();
        FieldDef fd;
        fd.name = fname;
        // Feld-Array: `items[4] AS ITEM`, `nums[3, 3]` oder `states[]` für ein
        // leeres Array, das mit DIMPUSH waechst. Die Klammern stehen wie bei
        // DIM direkt hinter dem Namen; jeder Wert ist eine Dimension, innerhalb
        // einer Klammer durch Komma getrennt oder in eigenen Klammern.
        std::vector<ExprP> dims;
        int rank = 0;
        while (acceptSym("[")) {
            if (acceptSym("]")) {
                // `tiles[]` bzw. `tiles[,]`: jede leere Klammer ist eine
                // Dimension ohne feste Groesse, also ein Array, das waechst.
                dims.push_back(ExprP{});
                rank += 1;
                continue;
            }
            for (ExprP& size : parseBracketList()) {
                dims.push_back(std::move(size));
                rank += 1;
            }
        }
        fd.isArray = rank > 0;
        fd.rank = rank;
        std::string tn = parseTypeNameIfPresent();
        if (!tn.empty()) {
            if (upperWord(tn) == "INTEGER" || upperWord(tn) == "INT" || upperWord(tn) == "LONG") { hint = TypeHint::Int; tn.clear(); }
            else if (upperWord(tn) == "FLOAT" || upperWord(tn) == "DOUBLE" || upperWord(tn) == "NUMBER") { hint = TypeHint::Float; tn.clear(); }
            else if (upperWord(tn) == "STRING") { hint = TypeHint::Str; tn.clear(); }
        }
        if (!tn.empty()) {
            fd.type = VT::Record;
            fd.typeName = tn;
        } else {
            switch (hint) {
                case TypeHint::Int: fd.type = VT::Int; break;
                case TypeHint::Str: fd.type = VT::Str; break;
                case TypeHint::Float:
                default: fd.type = VT::Float; break;  // Standardtyp: FLOAT
            }
        }
        def->fields.push_back(fd);
        arrayDims.push_back(std::move(dims));
        if (fd.isArray && isSym("="))
            error("Field array without initial value: '" + fname + "[...]' has no '=' (see DIMPUSH)");
        defaults.push_back(acceptSym("=") ? parseExpr() : ExprP{});
        if (cur().kind == Tok::Newline) advance();
        else if (acceptSym(",")) continue;
        else if (isIdent("ENDTYPE")) continue;
        else if (cur().kind == Tok::Eof) error("ENDTYPE missing");
        else error("Line end expected");
    }
    prog_.typeDefaults[name] = std::move(defaults);
    prog_.typeArrayDims[name] = std::move(arrayDims);
    (void)p;
}

// ------------------------------------------------- ENUM-Werte aufloesen
//
// `PHASE.STOP` sieht wie ein Feldzugriff aus; welcher Zugriff ein ENUM-Wert
// ist, weiss der Parser aber erst, wenn alle Dateien gelesen sind - ein Wert
// darf auch vor seiner ENUM-Deklaration stehen. Deshalb laeuft dieser Durchlauf
// ueber das ganze Programm, sobald es vollstaendig ist.

namespace {

void resolveExprEnums(const Program& prog, ExprP& slot) {
    if (!slot) return;
    Expr& e = *slot;
    resolveExprEnums(prog, e.a);
    resolveExprEnums(prog, e.b);
    for (auto& arg : e.args) resolveExprEnums(prog, arg);

    // Nur `NAME.WERT` mit einem einfachen Namen davor - `p.x` bleibt ein Feld.
    if (e.kind != EK::Field || !e.a || e.a->kind != EK::Var || e.a->a) return;
    auto table = prog.enums.find(e.a->text);
    if (table == prog.enums.end()) return;

    const std::string enumName = e.a->text;
    const std::string value = e.text;
    if (std::find(table->second.begin(), table->second.end(), value) == table->second.end()) {
        const std::string hint = closestWord(table->second, value);
        throw BasicError(enumName + " has no value " + value +
                             (hint.empty() ? "" : " - did you mean " + enumName + "." + hint + "?"),
                         e.pos, true);
    }
    auto ref = std::make_unique<Expr>();
    ref->kind = EK::Var;
    ref->pos = e.pos;
    ref->text = enumName + "." + value;
    ref->hint = TypeHint::Int;
    slot = std::move(ref);
}

void resolveStmtEnums(const Program& prog, Stmt& s) {
    resolveExprEnums(prog, s.e1);
    resolveExprEnums(prog, s.e2);
    resolveExprEnums(prog, s.e3);
    for (auto& arg : s.exprs) resolveExprEnums(prog, arg);
    for (auto& inner : s.body) resolveStmtEnums(prog, *inner);
    for (auto& inner : s.body2) resolveStmtEnums(prog, *inner);
    for (auto& inner : s.finallyBody) resolveStmtEnums(prog, *inner);
    for (auto& clause : s.catches) {
        resolveExprEnums(prog, clause.guard);
        for (auto& inner : clause.body) resolveStmtEnums(prog, *inner);
    }
    for (auto& branch : s.cases) {
        for (auto& test : branch.tests) {
            resolveExprEnums(prog, test.lo);
            resolveExprEnums(prog, test.hi);
        }
        for (auto& inner : branch.body) resolveStmtEnums(prog, *inner);
    }
}

void resolveBlockEnums(const Program& prog, std::vector<StmtP>& body) {
    for (auto& stmt : body) resolveStmtEnums(prog, *stmt);
}

}  // namespace

void resolveEnumRefs(Program& prog) {
    resolveBlockEnums(prog, prog.main);
    for (auto& entry : prog.funcs) resolveBlockEnums(prog, entry.second.body);
    for (auto& entry : prog.typeDefaults) {
        for (auto& expr : entry.second) resolveExprEnums(prog, expr);
    }
}

// ------------------------------------------- FINALLY-Rumpf und INTERFACE

namespace {

// Schluesselwoerter, die keinen Satz mehr beginnen. Ein altes FINALLY ohne
// ENDTRY soll so den Rest der Datei nicht verschlucken.
bool canStartStatement(const std::string& kw) {
    static const char* stop[] = {"ENDTRY",  "ENDIF",    "ELSE",        "ELSEIF",
                                 "ENDSELECT", "CASE",   "DEFAULT",     "NEXT",
                                 "WEND",    "UNTIL",    "ENDFUNCTION", "ENDSUB",
                                 "ENDMETHOD", "ENDTYPE", "ENDINTERFACE", "ENDENUM",
                                 "SUB",     "FUNCTION", "TYPE",        "INTERFACE",
                                 "INCLUDE"};
    for (const char* s : stop)
        if (kw == s) return false;
    return true;
}

}  // namespace

void Parser::parseTypeTail(const std::shared_ptr<TypeDef>& def) {
    if (acceptIdent("EXTENDS")) {
        if (cur().kind != Tok::Ident) error("Base TYPE expected after EXTENDS");
        def->baseName = stripSuffix(cur().raw);
        advance();
    }
    if (acceptIdent("IMPLEMENTS")) {
        for (;;) {
            if (cur().kind != Tok::Ident) error("INTERFACE name expected after IMPLEMENTS");
            def->interfaces.push_back(stripSuffix(cur().raw));
            advance();
            if (!acceptSym(",")) break;
        }
    }
}

void Parser::parseInterface() {
    advance();  // INTERFACE
    if (cur().kind != Tok::Ident) error("INTERFACE name expected");
    const std::string name = stripSuffix(cur().raw);
    advance();
    expectLineEnd();
    std::vector<std::string> methods;
    for (;;) {
        skipNewlines();
        if (isIdent("ENDINTERFACE")) {
            advance();
            break;
        }
        if (cur().kind == Tok::Eof) error("ENDINTERFACE missing");
        if (isIdent("SUB") || isIdent("FUNCTION") || isIdent("METHOD")) {
            advance();
            if (cur().kind != Tok::Ident) error("Method name expected");
            methods.push_back(stripSuffix(cur().raw));
            advance();
            // Wie bei FUNCTION/SUB: der Doppelpunkt gehoert zur Kopfzeile.
            acceptSym(":");
            std::vector<Param> ignored;
            parseParamList(ignored);
            parseTypeNameIfPresent();  // Rueckgabetyp einer FUNCTION-Signatur
            if (cur().kind == Tok::Newline) advance();
            continue;
        }
        error("An INTERFACE holds only SUB and FUNCTION lines");
    }
    prog_.interfaces[name] = std::move(methods);
}

bool Parser::finallyStartsBody() const {
    for (size_t i = pos_; i < toks_.size(); ++i) {
        const Token& t = toks_[i];
        if (t.kind == Tok::Eof) return false;
        if (t.kind != Tok::Ident) continue;
        if (t.text == "ENDTRY") return true;
        // Eine Definition auf Zeilenanfang beendet den TRY-Bereich sicher.
        if (i == 0 || toks_[i - 1].kind != Tok::Newline) continue;
        static const char* stop[] = {"SUB",         "FUNCTION",  "TYPE",       "INTERFACE",
                                     "ENDSUB",      "ENDFUNCTION", "ENDTYPE",  "ENDMETHOD",
                                     "ENDINTERFACE"};
        for (const char* s : stop)
            if (t.text == s) return false;
    }
    return false;
}

std::vector<StmtP> Parser::parseFinallyBody() {
    std::vector<StmtP> out;
    for (;;) {
        skipNewlines();
        if (cur().kind == Tok::Eof) break;
        if (isIdent("ENDTRY")) break;
        if (cur().kind == Tok::Ident && !canStartStatement(cur().text)) break;
        if (isSym(":")) {
            advance();
            continue;
        }
        if (cur().kind == Tok::Inline) {
            if (StmtP step = takeInlineBlock(false)) out.push_back(std::move(step));
            continue;
        }
        out.push_back(parseStatement());
        if (cur().kind == Tok::Newline) advance();
        else if (isSym(":")) advance();
        else break;
    }
    return out;
}

// ---------------------------------------------------- Vererbung aufloesen

namespace {

// Kopie eines Ausdrucks: geerbte Standardwerte und Feld-Array-Groessen werden
// je TYPE gebraucht, duerfen aber nicht geteilt werden (ExprP ist eindeutig).
ExprP cloneExpr(const Expr* src) {
    if (!src) return ExprP{};
    auto e = std::make_unique<Expr>();
    e->kind = src->kind;
    e->pos = src->pos;
    e->ival = src->ival;
    e->fval = src->fval;
    e->text = src->text;
    e->hint = src->hint;
    e->bin = src->bin;
    e->un = src->un;
    e->a = cloneExpr(src->a.get());
    e->b = cloneExpr(src->b.get());
    e->args.reserve(src->args.size());
    for (const auto& arg : src->args) e->args.push_back(cloneExpr(arg.get()));
    return e;
}

std::vector<ExprP> cloneList(const std::vector<ExprP>& src) {
    std::vector<ExprP> out;
    out.reserve(src.size());
    for (const auto& e : src) out.push_back(cloneExpr(e.get()));
    return out;
}

void flattenType(Program& prog, const std::string& name, std::vector<std::string>& visiting,
                 std::vector<std::string>& done) {
    for (const auto& d : done)
        if (d == name) return;
    for (const auto& v : visiting)
        if (v == name) throw BasicError("TYPE '" + name + "' erbt im Kreis", SrcPos{}, true);

    auto it = prog.types.find(name);
    if (it == prog.types.end()) return;
    const std::shared_ptr<TypeDef>& def = it->second;
    visiting.push_back(name);

    if (!def->baseName.empty()) {
        if (prog.types.find(def->baseName) == prog.types.end())
            throw BasicError("TYPE '" + name + "' extends unknown TYPE '" + def->baseName +
                                 "'",
                             SrcPos{}, true);
        flattenType(prog, def->baseName, visiting, done);
        const std::shared_ptr<TypeDef>& base = prog.types[def->baseName];
        def->base = base.get();

        // Felder: geerbte zuerst, dann die eigenen. Nur so passen die Indizes
        // in makeRecord zu typeDefaults und typeArrayDims.
        std::vector<FieldDef> merged = base->fields;
        for (const auto& f : def->fields) merged.push_back(f);
        def->fields = std::move(merged);

        std::vector<ExprP> defaults;
        auto bd = prog.typeDefaults.find(def->baseName);
        if (bd != prog.typeDefaults.end())
            defaults = cloneList(bd->second);
        else
            defaults.resize(base->fields.size());
        auto ownDef = prog.typeDefaults.find(name);
        if (ownDef != prog.typeDefaults.end()) {
            std::vector<ExprP> own = cloneList(ownDef->second);
            for (auto& e : own) defaults.push_back(std::move(e));
        }
        prog.typeDefaults[name] = std::move(defaults);

        std::vector<std::vector<ExprP>> dims;
        auto bdim = prog.typeArrayDims.find(def->baseName);
        if (bdim != prog.typeArrayDims.end()) {
            for (const auto& row : bdim->second) dims.push_back(cloneList(row));
        } else {
            dims.resize(base->fields.size());
        }
        auto ownDim = prog.typeArrayDims.find(name);
        if (ownDim != prog.typeArrayDims.end()) {
            for (const auto& row : ownDim->second) dims.push_back(cloneList(row));
        }
        prog.typeArrayDims[name] = std::move(dims);
    }

    // IMPLEMENTS: jede geforderte Methode muss die TYPE-Kette anbieten.
    for (const auto& iface : def->interfaces) {
        auto iit = prog.interfaces.find(iface);
        if (iit == prog.interfaces.end())
            throw BasicError("TYPE '" + name + "' names unknown INTERFACE '" + iface + "'",
                             SrcPos{}, true);
        for (const auto& m : iit->second) {
            if (def->hasMethod(m)) continue;
            throw BasicError("TYPE '" + name + "' is missing the method '" + m + "' from INTERFACE '" +
                                 iface + "'",
                             SrcPos{}, true);
        }
    }

    visiting.pop_back();
    done.push_back(name);
}

}  // namespace

void resolveTypes(Program& prog) {
    std::vector<std::string> visiting, done;
    for (const auto& entry : prog.types) flattenType(prog, entry.first, visiting, done);
}
