// GameBasiX - Parser
#include "parser.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

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

std::string readFileOrThrow(const std::string& path, SrcPos pos) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw BasicError("Datei nicht gefunden: " + path, std::move(pos), true);
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
    if (!acceptSym(s)) error(std::string("'") + s + "' erwartet");
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
    error("Zeilenende erwartet, gefunden: '" + cur().text + "'");
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
    if (cur().kind != Tok::Ident) error("Typname nach AS erwartet");
    std::string name = stripSuffix(cur().text);
    advance();
    return name;
}

// ---------------------------------------------------------------- Ausdruecke

ExprP Parser::parseExpr() { return parseOr(); }

ExprP Parser::parseExpression() {
    skipNewlines();
    ExprP expr = parseExpr();
    skipNewlines();
    if (cur().kind != Tok::Eof) error("Unerwarteter Text nach dem Ausdruck");
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
                // "a[]" - Zugriff auf das komplette Array (BOUNDS, LEN, FOREACH ...)
                continue;
            }
            ExprP idx = parseExpr();
            expectSym("]");
            auto node = std::make_unique<Expr>();
            node->kind = EK::Index;
            node->pos = p;
            node->a = std::move(e);
            node->b = std::move(idx);
            e = std::move(node);
            continue;
        }
        if (isSym(".")) {
            SrcPos p{path_, cur().line};
            advance();
            if (cur().kind != Tok::Ident) error("Feldname nach '.' erwartet");
            std::string field = stripSuffix(cur().text);
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
        std::string name = stripSuffix(t.text);
        advance();
        if (name == "TRUE") return makeInt(p, 1);
        if (name == "FALSE") return makeInt(p, 0);
        if (isSym("(")) {
            auto e = std::make_unique<Expr>();
            e->kind = EK::Call;
            e->pos = p;
            e->text = t.text;  // Suffix bleibt: STR$(), MID$(), ...
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
    error(std::string("Ausdruck erwartet, gefunden: ") + tokenKindName(t.kind) +
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

ExprP Parser::parseLValue() {
    if (cur().kind != Tok::Ident) error("Variable erwartet");
    SrcPos p{path_, cur().line};
    auto e = std::make_unique<Expr>();
    e->kind = EK::Var;
    e->pos = p;
    e->text = stripSuffix(cur().text);
    e->hint = suffixHint(cur().text);
    advance();
    for (;;) {
        if (isSym("[")) {
            advance();
            ExprP idx = parseExpr();
            expectSym("]");
            auto node = std::make_unique<Expr>();
            node->kind = EK::Index;
            node->pos = p;
            node->a = std::move(e);
            node->b = std::move(idx);
            e = std::move(node);
            continue;
        }
        if (isSym(".")) {
            advance();
            if (cur().kind != Tok::Ident) error("Feldname nach '.' erwartet");
            std::string field = stripSuffix(cur().text);
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
    if (isIdent("TYPE")) {
        parseType();
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
    else if (cur().kind != Tok::Eof) error("Zeilenende erwartet");
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
    else if (cur().kind != Tok::Eof) error("Zeilenende erwartet");

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
    if (cur().kind != Tok::Str) error("Dateiname als String erwartet");
    std::string rel = cur().text;
    advance();

    fs::path base = fs::path(path_).parent_path();
    fs::path target = fs::path(rel);
    if (target.is_relative()) target = base / target;
    std::string full = target.lexically_normal().string();

    std::error_code ec;
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
            error(std::string("Block nicht beendet, '") + terminators.back() + "' fehlt");
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
            error("Zeilenende erwartet, gefunden: '" + cur().text + "'");
        }
    }
    return out;
}

StmtP Parser::parseStatement() {
    if (cur().kind != Tok::Ident) {
        if (cur().kind == Tok::Sym && cur().text == "?")
            error("Praeprozessor-Anweisungen (?IF, ?DEFINE ...) werden nicht unterstuetzt");
        if (cur().kind == Tok::Inline) error("INLINE muss eine eigene Zeile beginnen");
        error("Anweisung erwartet, gefunden: " + std::string(tokenKindName(cur().kind)));
    }

    const std::string kw = cur().text;

    auto mk = [&](SK kind) {
        auto s = std::make_unique<Stmt>();
        s->kind = kind;
        s->pos = SrcPos{path_, cur().line};
        return s;
    };

    if (kw == "METHOD" || kw == "ENDMETHOD")
        error("METHOD gehoert in einen TYPE (TYPE ... METHOD name ... ENDMETHOD ... ENDTYPE)");
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
        if (cur().kind != Tok::Ident) error("Marken- oder SUB-Name erwartet");
        s->name = stripSuffix(cur().text);
        advance();
        return s;
    }
    if (kw == "LET") {
        advance();
        return parseAssignmentOrCall();
    }
    if (kw == "FUNCTION" || kw == "SUB" || kw == "TYPE") {
        error(kw + " darf nur auf oberster Ebene stehen");
    }
    if (kw == "INCLUDE") {
        error("INCLUDE muss auf oberster Ebene stehen");
    }
    if (kw == "ELSE" || kw == "ELSEIF" || kw == "ENDIF" || kw == "WEND" ||
        kw == "NEXT" || kw == "UNTIL" || kw == "CASE" || kw == "DEFAULT" ||
        kw == "ENDSELECT" || kw == "ENDFUNCTION" || kw == "ENDSUB" ||
        kw == "ENDTYPE" || kw == "CATCH" || kw == "FINALLY" || kw == "ENDTRY" ||
        kw == "TO" || kw == "STEP") {
        error("Unerwartetes '" + kw + "'");
    }
    if (kw == "AS" || kw == "BYREF" || kw == "IN") {
        error("Unerwartetes '" + kw + "'");
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
            s->name = stripSuffix(cur().text);
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

    if (cur().kind != Tok::Ident) error("Anweisung erwartet");
    auto s = std::make_unique<Stmt>();
    s->kind = SK::Call;
    s->pos = SrcPos{path_, cur().line};
    s->name = cur().text;
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
        if (cur().kind != Tok::Newline) error("Nach ELSEIF wird eine neue Zeile erwartet");
        advance();
        nested->body = parseBlock({"ELSE", "ELSEIF", "ENDIF"});
        tail->body2.push_back(std::move(nested));
        tail = tail->body2.back().get();
    }

    if (acceptIdent("ELSE")) {
        advance();  // Zeilenende
        tail->body2 = parseBlock({"ENDIF"});
    }
    if (!acceptIdent("ENDIF")) error("ENDIF erwartet");
    return s;
}

StmtP Parser::parseFor() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::For;
    s->pos = SrcPos{path_, cur().line};
    advance();  // FOR
    if (cur().kind != Tok::Ident) error("Zaehlvariable erwartet");
    s->name = stripSuffix(cur().text);
    s->hint = suffixHint(cur().text);
    advance();
    expectSym("=");
    s->e1 = parseExpr();
    if (!acceptIdent("TO")) error("TO erwartet");
    s->e2 = parseExpr();
    if (acceptIdent("STEP")) s->e3 = parseExpr();
    expectLineEnd();
    s->body = parseBlock({"NEXT"});
    if (!acceptIdent("NEXT")) error("NEXT erwartet");
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
    if (!acceptIdent("WEND") && !acceptIdent("ENDWHILE")) error("WEND erwartet");
    return s;
}

StmtP Parser::parseRepeat() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::Repeat;
    s->pos = SrcPos{path_, cur().line};
    advance();
    expectLineEnd();
    s->body = parseBlock({"UNTIL"});
    if (!acceptIdent("UNTIL")) error("UNTIL erwartet");
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
        if (cur().kind == Tok::Eof) error("ENDSELECT fehlt");
        CaseBlock cb;
        if (acceptIdent("DEFAULT")) {
            sawDefault = true;
            expectLineEnd();
            cb.body = parseBlock({"CASE", "DEFAULT", "ENDSELECT"});
            s->cases.push_back(std::move(cb));
            continue;
        }
        if (!acceptIdent("CASE")) error("CASE oder DEFAULT erwartet");
        if (sawDefault) error("CASE nach DEFAULT ist nicht erlaubt");
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
                if (t.cmp != 0) error("TO ist nur bei 'CASE a TO b' erlaubt");
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
    if (cur().kind != Tok::Ident) error("Array-Name erwartet");
    s->name = stripSuffix(cur().text);
    s->hint = suffixHint(cur().text);
    advance();
    while (acceptSym("[")) {
        s->exprs.push_back(parseExpr());
        expectSym("]");
    }
    std::string tn = parseTypeNameIfPresent();
    if (!tn.empty()) {
        TypeHint h = TypeHint::None;
        if (tn == "INTEGER" || tn == "INT" || tn == "LONG") h = TypeHint::Int;
        else if (tn == "FLOAT" || tn == "DOUBLE" || tn == "NUMBER") h = TypeHint::Float;
        else if (tn == "STRING") h = TypeHint::Str;
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
        if (cur().kind != Tok::Ident) error("Variablenname erwartet");
        auto d = std::make_unique<Stmt>();
        d->kind = SK::Decl;
        d->scope = scope;
        d->pos = SrcPos{path_, cur().line};
        d->name = stripSuffix(cur().text);
        d->hint = suffixHint(cur().text);
        advance();
        std::string tn = parseTypeNameIfPresent();
        if (!tn.empty()) {
            if (tn == "INTEGER" || tn == "INT" || tn == "LONG") d->hint = TypeHint::Int;
            else if (tn == "FLOAT" || tn == "DOUBLE" || tn == "NUMBER") d->hint = TypeHint::Float;
            else if (tn == "STRING") d->hint = TypeHint::Str;
            else d->typeName = tn;
        }
        if (acceptSym("=")) d->e1 = parseExpr();
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
    if (cur().kind != Tok::Ident) error("Konstantenname erwartet");
    s->name = stripSuffix(cur().text);
    s->hint = suffixHint(cur().text);
    advance();
    std::string tn = parseTypeNameIfPresent();
    if (!tn.empty()) {
        if (tn == "INTEGER" || tn == "INT") s->hint = TypeHint::Int;
        else if (tn == "FLOAT" || tn == "DOUBLE" || tn == "NUMBER") s->hint = TypeHint::Float;
        else if (tn == "STRING") s->hint = TypeHint::Str;
    }
    expectSym("=");
    s->e1 = parseExpr();
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
    if (cur().kind != Tok::Ident) error("Array-Name erwartet");
    auto var = std::make_unique<Expr>();
    var->kind = EK::Var;
    var->pos = s->pos;
    var->text = stripSuffix(cur().text);
    advance();
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
    if (cur().kind != Tok::Ident) error("Laufvariable erwartet");
    s->name = stripSuffix(cur().text);
    s->hint = suffixHint(cur().text);
    advance();
    if (!acceptIdent("IN")) error("IN erwartet");
    s->e1 = parseExpr();
    expectLineEnd();
    s->body = parseBlock({"NEXT"});
    if (!acceptIdent("NEXT")) error("NEXT erwartet");
    return s;
}

StmtP Parser::parseTry() {
    auto s = std::make_unique<Stmt>();
    s->kind = SK::Try;
    s->pos = SrcPos{path_, cur().line};
    advance();
    expectLineEnd();
    s->body = parseBlock({"CATCH", "FINALLY", "ENDTRY"});
    if (acceptIdent("CATCH")) {
        if (cur().kind != Tok::Ident) error("Fehlervariable nach CATCH erwartet");
        s->name = stripSuffix(cur().text);
        advance();
        expectLineEnd();
        s->body2 = parseBlock({"FINALLY", "ENDTRY"});
    }
    // FINALLY beendet den Block und hat keinen eigenen Code
    if (!acceptIdent("FINALLY") && !acceptIdent("ENDTRY")) {
        error("FINALLY erwartet");
    }
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
void Parser::parseParamList(std::vector<Param>& params) {
    acceptSym(":");
    const bool parens = acceptSym("(");
    if (parens && acceptSym(")")) return;
    if (!parens && (cur().kind == Tok::Newline || cur().kind == Tok::Eof)) return;
    for (;;) {
        Param p;
        p.byref = acceptIdent("BYREF");
        if (cur().kind != Tok::Ident) error("Parametername erwartet");
        p.name = stripSuffix(cur().text);
        p.hint = suffixHint(cur().text);
        advance();
        std::string tn = parseTypeNameIfPresent();
        if (!tn.empty()) {
            if (tn == "INTEGER" || tn == "INT") p.hint = TypeHint::Int;
            else if (tn == "FLOAT" || tn == "DOUBLE" || tn == "NUMBER") p.hint = TypeHint::Float;
            else if (tn == "STRING") p.hint = TypeHint::Str;
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
    if (cur().kind != Tok::Ident) error("Methodenname erwartet");
    const std::string methodName = stripSuffix(cur().text);
    def.name = methodName;
    advance();
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
    if (!acceptIdent("ENDMETHOD")) error("ENDMETHOD erwartet");

    if (prog_.funcs.find(key) != prog_.funcs.end()) {
        throw BasicError("Methode '" + methodName + "' ist in TYPE '" + typeName +
                             "' bereits definiert",
                         def.pos, true);
    }
    prog_.funcs[key] = std::move(def);
    auto type = prog_.types.find(typeName);
    if (type != prog_.types.end() && type->second) type->second->methods.push_back(methodName);
}

void Parser::parseFunction(bool isSub) {
    FuncDef def;
    def.isSub = isSub;
    def.pos = SrcPos{path_, cur().line};
    advance();  // FUNCTION / SUB
    if (cur().kind != Tok::Ident) error("Funktionsname erwartet");
    def.name = stripSuffix(cur().text);
    advance();
    if (isSub) {
        acceptSym(":");  // SUBs haben keine Parameter
    } else {
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
        error(isSub ? "ENDSUB erwartet" : "ENDFUNCTION erwartet");

    auto prev = prog_.funcs.find(def.name);
    if (prev != prog_.funcs.end())
        throw BasicError("Funktion '" + def.name + "' ist bereits definiert", def.pos, true);
    prog_.funcs[def.name] = std::move(def);
}

void Parser::parseType() {
    SrcPos p{path_, cur().line};
    advance();  // TYPE
    if (cur().kind != Tok::Ident) error("Typname erwartet");
    std::string name = stripSuffix(cur().text);
    advance();
    auto def = findOrCreateType(name);
    def->fields.clear();
    // `x% = 0`: Standardwert je Feld; nullptr steht für „ohne Angabe“.
    std::vector<ExprP> defaults;
    expectLineEnd();

    for (;;) {
        skipNewlines();
        if (isIdent("ENDTYPE")) {
            advance();
            break;
        }
        if (cur().kind == Tok::Eof) error("ENDTYPE fehlt");
        if (isIdent("METHOD")) {
            parseMethod(name);
            continue;
        }
        if (isIdent("FUNCTION") || isIdent("SUB")) {
            error("In einem TYPE heissen Funktionen METHOD - schreibe: " + cur().text +
                  " durch METHOD ersetzen, Block mit ENDMETHOD beenden");
        }
        if (cur().kind != Tok::Ident) error("Feldname erwartet");
        std::string fname = stripSuffix(cur().text);
        TypeHint hint = suffixHint(cur().text);
        advance();
        FieldDef fd;
        fd.name = fname;
        std::string tn = parseTypeNameIfPresent();
        if (!tn.empty()) {
            if (tn == "INTEGER" || tn == "INT" || tn == "LONG") { hint = TypeHint::Int; tn.clear(); }
            else if (tn == "FLOAT" || tn == "DOUBLE" || tn == "NUMBER") { hint = TypeHint::Float; tn.clear(); }
            else if (tn == "STRING") { hint = TypeHint::Str; tn.clear(); }
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
        defaults.push_back(acceptSym("=") ? parseExpr() : ExprP{});
        if (cur().kind == Tok::Newline) advance();
        else if (acceptSym(",")) continue;
        else if (isIdent("ENDTYPE")) continue;
        else if (cur().kind == Tok::Eof) error("ENDTYPE fehlt");
        else error("Zeilenende erwartet");
    }
    prog_.typeDefaults[name] = std::move(defaults);
    (void)p;
}
