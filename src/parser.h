// GameBasiX - Parser
#pragma once

#include <string>
#include <vector>

#include "ast.h"
#include "lexer.h"

// Loest `PHASE.STOP` in allen Ausdruecken zu einer Konstanten auf; laeuft,
// wenn das ganze Programm geparst ist. Meldet unbekannte Werte mit Hinweis.
void resolveEnumRefs(Program& program);

class Parser {
public:
    Parser(std::string path, std::vector<Token> tokens, Program& program);

    // Parst die gesamte Datei. INCLUDE-Anweisungen werden relativ zum
    // Verzeichnis der Datei aufgeloest und sofort geparst.
    void parseFile();

    // Liest genau einen Ausdruck (Bedingungen im Debugger).
    ExprP parseExpression();

private:
    const Token& cur() const { return toks_[pos_]; }
    const Token& peek(int n = 1) const;
    void advance();
    bool isSym(const char* s) const;
    bool isIdent(const char* s) const;
    bool acceptSym(const char* s);
    bool acceptIdent(const char* s);
    void expectSym(const char* s);
    void skipNewlines();
    void expectLineEnd();
    [[noreturn]] void error(const std::string& msg);

    // Anweisungen
    void parseTopLevel();
    StmtP parseStatement();
    // INLINE-Block einsammeln; liefert bei einem Schritt-Block die Anweisung.
    StmtP takeInlineBlock(bool topLevel);
    StmtP parseIf();
    StmtP parseFor();
    StmtP parseWhile();
    StmtP parseRepeat();
    StmtP parseSelect();
    StmtP parseDim(bool redim);
    StmtP parseDecl(DeclScope scope);
    StmtP parseConstant();
    StmtP parseEnum();
    StmtP parseIncDec(int delta);
    StmtP parsePush(bool del);
    StmtP parseForeach();
    StmtP parseTry();
    StmtP parseSwap();
    StmtP parseAssignmentOrCall();
    void parseFunction(bool isSub);
    void parseParamList(std::vector<Param>& params);
    void parseMethod(const std::string& typeName);
    void parseType();
    void parseInclude();
    std::vector<StmtP> parseBlock(const std::vector<std::string>& terminators);

    // Ausdruecke
    ExprP parseExpr();
    ExprP parseOr();
    ExprP parseAnd();
    ExprP parseNot();
    ExprP parseCompare();
    ExprP parseAdd();
    ExprP parseMul();
    ExprP parseUnary();
    ExprP parsePostfix();
    ExprP parsePrimary();
    ExprP parseLValue();
    std::vector<ExprP> parseArgList(bool parens);

    TypeHint suffixHint(const std::string& name) const;
    std::string stripSuffix(const std::string& name) const;
    std::string parseTypeNameIfPresent();
    std::shared_ptr<TypeDef> findOrCreateType(const std::string& name);

    std::string path_;
    std::vector<Token> toks_;
    size_t pos_ = 0;
    Program& prog_;
    // Aktuelle FUNCTION/SUB/METHOD fuer die Variablen eines Schritt-Blocks.
    std::string scope_;
    std::string scopeType_;
};
