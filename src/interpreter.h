// GameBasiX - Interpreter
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "ast.h"
#include "builtins.h"
#include "runtime.h"

class Interpreter {
public:
    Interpreter(Program& prog, Runtime& rt);
    void run();

    Value evalCall(const std::string& name, const std::vector<ExprP>& args, const SrcPos& pos);
    // self ist das Objekt, wenn eine METHOD aufgerufen wird.
    Value callUserFunction(const FuncDef& def, const std::vector<ExprP>& args, const SrcPos& pos,
                           const Value* self = nullptr);

    // Aktueller Bereich fuer den Debugger (Funktion, Tiefe, Aufrufliste,
    // lokale und globale Variablen) als JSON-Objekt.
    std::string debugScope() const;

    // Wertet einen Ausdruck in der aktuellen Umgebung aus (Bedingungen von
    // Haltepunkten). Setzt ok=false und error, wenn der Text nicht lesbar ist.
    Value debugEval(const std::string& text, bool& ok, std::string& error);

    // Dasselbe fuer den Wert-Tooltip im Haltepunkt: liefert den JSON-Ausschnitt
    // {"type":…,"detail":…} mit aufgeklappten Arrays und TYPEs.
    std::string debugInspect(const std::string& text, bool& ok, std::string& error);

    // Wird von FOREACH benoetigt
    struct Frame {
        std::unordered_map<std::string, Slot> locals;
        std::unordered_map<std::string, Slot*> aliases;
        std::string func;
        Value selfVal;  // Objekt einer METHOD (teilt das Record mit dem Aufrufer)
    };

private:
    Program& prog_;
    Runtime& rt_;
    Frame mainFrame_;
    Frame* frame_ = nullptr;
    int callDepth_ = 0;
    std::unordered_map<std::string, Slot> statics_;
    std::vector<std::string> stack_;  // Aufrufliste fuer den Debugger

    // Zugriff auf die Variablen fuer INLINE-Schritte (C++ mitten im Code).
    class InlineStepScope;

    // Variablen
    Slot* findSlot(const std::string& name);
    Slot* findLocalSlot(const std::string& name);
    Value* selfField(const std::string& name);
    Value callMethod(const Expr& call);
    Slot* createAssignmentSlot(const std::string& name, TypeHint hint);
    void declareGlobal(const Stmt& s);
    void declareLocal(const Stmt& s);
    void declareStatic(const Stmt& s);
    void assign(const Expr& target, Value v);
    void assignArray(const std::string& name, std::shared_ptr<ArrayObj> arr);
    // Dasselbe, aber in einen festen Bereich (GLOBAL/LOCAL/STATIC vor [..]).
    void assignArrayIn(const std::string& name, std::shared_ptr<ArrayObj> arr, DeclScope scope,
                       const SrcPos& pos);
    Value readVar(const Expr& e);
    uint32_t toIndex(const Expr& e, size_t size, const SrcPos& pos);

    // Ausdruecke
    Value eval(const Expr& e);
    // Verknuepft zwei Werte wie ein Operator (+ - * / = <> < <= > >=).
    // Wird auch fuer "x += 1" gebraucht, damit beide Wege gleich rechnen.
    Value evalBinary(BinOp op, const Value& a, const Value& b, const SrcPos& pos);

    // Anweisungen
    void execSection(const std::vector<StmtP>& body);
    void execBlock(const std::vector<StmtP>& body);
    void execStmt(const Stmt& s);
    void execFor(const Stmt& s);
    void execSelect(const Stmt& s);
    void execDim(const Stmt& s, bool redim);
    void execIncDec(const Stmt& s);
    void execForeach(const Stmt& s);
    void execTry(const Stmt& s);

    // Datentypen
    std::shared_ptr<ArrayObj> makeArray(const std::vector<int>& sizes, size_t dim, VT elemVT,
                                        const std::string& typeName);
    Value makeRecord(const std::string& typeName, int depth = 0);
    // Array eines TYPE-Feldes: Groessen aus Program::typeArrayDims, Elementtyp
    // aus der Felddefinition. "items[]" startet leer.
    std::shared_ptr<ArrayObj> makeFieldArray(const std::string& typeName, size_t field,
                                             const FieldDef& fd);
    Value defaultElement(VT elemVT, const std::string& typeName);
    void copyArrayData(ArrayObj& dst, const ArrayObj& src);

    bool isTruthy(const Value& v) const { return v.truthy(); }
    [[noreturn]] void runtimeError(const std::string& msg, const SrcPos& pos) const;
};
