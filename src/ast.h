// GameBasiX - Syntaxbaum
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "value.h"

enum class TypeHint { None, Int, Float, Str };

enum class BinOp { Add, Sub, Mul, Div, Eq, Ne, Lt, Le, Gt, Ge, And, Or };
enum class UnOp { Neg, Plus, Not };

// MethodCall: p.flaeche(1) - a ist das Objekt, text der Methodenname.
enum class EK { IntLit, FloatLit, StrLit, Var, Unary, Binary, Call, Index, Field, MethodCall };

struct Expr;
using ExprP = std::unique_ptr<Expr>;

struct Expr {
    EK kind = EK::IntLit;
    SrcPos pos;
    int64_t ival = 0;
    double fval = 0.0;
    std::string text;  // Stringinhalt, Variablenname, Feldname oder Funktionsname
    TypeHint hint = TypeHint::None;  // aus Typ-Suffix ($ % #)
    BinOp bin = BinOp::Add;
    UnOp un = UnOp::Neg;
    ExprP a, b;
    std::vector<ExprP> args;
};

enum class SK {
    Assign,
    Call,
    If,
    For,
    While,
    Repeat,
    Select,
    Return,
    Break,
    Continue,
    End,
    Dim,
    Redim,
    Decl,
    ConstDecl,
    Swap,
    IncDec,
    Push,
    DimDel,
    Foreach,
    Gosub,
    Goto,
    Label,
    Try,
    Throw,
    MethodCall,  // Aufruf einer Methode als Anweisung: e1 ist EK::MethodCall
    InlineStep,  // INLINE-Block mitten im Code: wird hier ausgefuehrt
    Nop
};

enum class DeclScope { Global, Local, Static };

struct Stmt;
using StmtP = std::unique_ptr<Stmt>;

struct CaseTest {
    int cmp = 0;  // 0 ==, 1 <, 2 <=, 3 >, 4 >=, 5 <>
    ExprP lo;
    ExprP hi;     // nur bei "CASE a TO b"
};

struct CaseBlock {
    std::vector<CaseTest> tests;
    std::vector<StmtP> body;
};

struct Stmt {
    SK kind = SK::Nop;
    SrcPos pos;
    std::string name;
    std::string typeName;
    TypeHint hint = TypeHint::None;
    DeclScope scope = DeclScope::Local;
    bool isSub = false;

    ExprP e1, e2, e3;                  // Assignment: e1=Ziel, e2=Wert
    std::vector<ExprP> exprs;          // DIM-Groessen, Argumente
    std::vector<StmtP> body, body2;    // then/else, Schleifenkoerper, TRY/CATCH
    std::vector<CaseBlock> cases;
    int delta = 0;                     // INC/DEC
    int inlineIndex = -1;              // SK::InlineStep: Index in Program::inlines
};

struct Param {
    std::string name;
    TypeHint hint = TypeHint::None;
    std::string typeName;
    bool byref = false;
    ExprP def;
};

struct FuncDef {
    std::string name;
    std::string ownerType;  // gesetzt bei METHOD: der TYPE, zu dem sie gehoert
    std::vector<Param> params;
    std::vector<StmtP> body;
    bool isSub = false;
    SrcPos pos;

    bool isMethod() const { return !ownerType.empty(); }
};

// Ein INLINE … ENDINLINE-Block. Der Text geht unveraendert an den C++-Compiler;
// `pos.line` ist die erste Codezeile in der BASIC-Datei.
//
// step = true: Der Block steht mitten im Code (FUNCTION, IF, Schleife oder
// INLINE RUN) und wird dort ausgefuehrt. Sonst sind es Definitionen, die beim
// Start mit uebersetzt werden. `scope`/`scopeType` beschreiben, welche
// Variablen in einem Schritt sichtbar sind.
struct InlineBlock {
    SrcPos pos;
    std::string code;
    bool step = false;
    std::string scope;      // FUNCTION/SUB/METHOD-Schluessel, leer = Hauptprogramm
    std::string scopeType;  // TYPE bei METHOD, sonst leer
};

struct Program {
    std::vector<StmtP> main;
    std::vector<InlineBlock> inlines;
    std::unordered_map<std::string, FuncDef> funcs;
    std::unordered_map<std::string, std::shared_ptr<TypeDef>> types;
    // Standardwerte der TYPE-Felder in Feldreihenfolge; leer = ohne Angabe.
    // Getrennt von TypeDef, weil value.h keinen Syntaxbaum kennen soll.
    std::unordered_map<std::string, std::vector<ExprP>> typeDefaults;
    std::string mainFile;
    std::string baseDir;
    std::vector<std::string> includedFiles;  // bereits eingebundene Dateien
    std::vector<std::string> includeStack;   // gerade in Arbeit (Zyklen)
};
