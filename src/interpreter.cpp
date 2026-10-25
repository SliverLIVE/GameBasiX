// GameBasiX - Interpreter
#include "interpreter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>

#include "debugger.h"
#include "inline.h"
#include "lexer.h"
#include "parser.h"

namespace {

struct ReturnSignal { Value value; };
struct BreakSignal {};
struct ContinueSignal {};
struct EndSignal { int code = 0; };
struct GotoSignal { std::string label; };

constexpr int kMaxCallDepth = 256;
constexpr int kMaxRecordDepth = 24;
constexpr double kMaxLoopIterations = 100000000.0;

bool bothInt(const Value& a, const Value& b) { return a.type == VT::Int && b.type == VT::Int; }

// ------------------------------------------------------- Debugger-Ausgabe

struct DebugVar {
    std::string name;
    std::string type;
    std::string value;
};

std::string debugValueText(const Value& v) {
    if (v.isArray() && v.arr) {
        std::string out = "[";
        const size_t count = std::min<size_t>(v.arr->items.size(), 8);
        for (size_t i = 0; i < count; ++i) {
            if (i) out += ", ";
            out += debugValueText(v.arr->items[i]);
        }
        if (v.arr->items.size() > count) out += ", …";
        return out + "]";
    }
    if (v.isRecord() && v.rec && v.rec->def) return "[" + v.rec->def->name + "]";
    if (v.isStr()) return "\"" + v.s + "\"";
    return v.text();
}

std::string debugTypeName(const Value& v) {
    switch (v.type) {
        case VT::Int: return "INTEGER";
        case VT::Float: return "FLOAT";
        case VT::Str: return "STRING";
        case VT::Array:
            if (v.arr) return "ARRAY[" + std::to_string(v.arr->items.size()) + "]";
            return "ARRAY";
        case VT::Record:
            if (v.rec && v.rec->def) return v.rec->def->name;
            return "TYPE";
        case VT::Null: break;
    }
    return "";
}

// ------------------------------------------------- Detail fuer den Wert-Tooltip

constexpr size_t kDetailMaxItems = 12;
constexpr size_t kDetailMaxLines = 40;
constexpr int kDetailMaxDepth = 3;

// Kuerzt nach Zeichen, nicht nach Bytes: sonst zerlegt die Laengengrenze
// einen UTF-8-Folgesequenz und das JSON waere unlesbar.
std::string clipUtf8(const std::string& text, size_t maxChars) {
    size_t chars = 0;
    size_t i = 0;
    while (i < text.size() && chars < maxChars) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++chars;
        ++i;
    }
    if (i >= text.size()) return text;
    return text.substr(0, i) + "…";
}

std::string debugScalarText(const Value& v) {
    if (!v.isStr()) return v.text();
    std::string out;
    out.reserve(v.s.size() + 2);
    // Zeilenumbrueche wuerden den Tooltip in echte Zeilen zerlegen.
    for (char c : v.s) {
        if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return "\"" + clipUtf8(out, 60) + "\"";
}

struct DetailWriter {
    std::vector<std::string> lines;

    void add(const std::string& text) {
        if (lines.size() >= kDetailMaxLines) {
            if (lines.size() == kDetailMaxLines) lines.push_back("…");
            return;
        }
        lines.push_back(text);
    }
};

std::string detailPad(int indent) { return std::string(static_cast<size_t>(indent) * 2, ' '); }

// Fuehrender Name eines Ausdrucks in Grossschreibung ("spieler.px" → "SPIELER").
std::string leadingName(const std::string& text) {
    size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i >= text.size()) return std::string();
    if (!std::isalpha(static_cast<unsigned char>(text[i])) && text[i] != '_') return std::string();
    const size_t start = i;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (!std::isalnum(c) && c != '_' && c != '$' && c != '%' && c != '#') break;
        ++i;
    }
    std::string name = text.substr(start, i - start);
    for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return name;
}

// Klappt Arrays und TYPEs auf, damit im Tooltip nicht nur "[VEC]" steht.
void debugDetail(const Value& v, const std::string& label, int indent, int depth, DetailWriter& w) {
    const std::string pad = detailPad(indent);
    const std::string childPad = detailPad(indent + 1);
    if (v.isRecord() && v.rec && v.rec->def && depth < kDetailMaxDepth) {
        const TypeDef& def = *v.rec->def;
        if (!label.empty()) w.add(pad + label + " · TYPE " + def.name);
        const size_t count = std::min(def.fields.size(), v.rec->fields.size());
        for (size_t i = 0; i < count; ++i) {
            const Value& field = v.rec->fields[i];
            const std::string& name = def.fields[i].name;
            if (field.isRecord() || field.isArray()) debugDetail(field, name, indent + 1, depth + 1, w);
            else w.add(childPad + name + " = " + debugScalarText(field));
        }
        return;
    }
    if (v.isArray() && v.arr && depth < kDetailMaxDepth) {
        const std::vector<Value>& items = v.arr->items;
        if (!label.empty()) w.add(pad + label + " · ARRAY[" + std::to_string(items.size()) + "]");
        const size_t shown = std::min(items.size(), kDetailMaxItems);
        for (size_t i = 0; i < shown; ++i) {
            const std::string index = "[" + std::to_string(i) + "]";
            if (items[i].isRecord() || items[i].isArray()) debugDetail(items[i], index, indent + 1, depth + 1, w);
            else w.add(childPad + index + " = " + debugScalarText(items[i]));
        }
        if (items.size() > shown) w.add(childPad + "… +" + std::to_string(items.size() - shown));
        return;
    }
    if (v.isRecord() || v.isArray()) {
        w.add(pad + (label.empty() ? std::string("…") : label + " · …"));
        return;
    }
    w.add(label.empty() ? debugScalarText(v) : pad + label + " = " + debugScalarText(v));
}

std::string debugVarsJson(std::vector<DebugVar> vars) {
    std::sort(vars.begin(), vars.end(), [](const DebugVar& a, const DebugVar& b) {
        return a.name < b.name;
    });
    std::string out = "[";
    bool first = true;
    for (const DebugVar& var : vars) {
        if (!first) out += ",";
        first = false;
        out += "{\"name\":\"" + debugJsonEscape(var.name);
        out += "\",\"type\":\"" + debugJsonEscape(var.type);
        out += "\",\"value\":\"" + debugJsonEscape(var.value) + "\"}";
    }
    return out + "]";
}

}  // namespace

Interpreter::Interpreter(Program& prog, Runtime& rt) : prog_(prog), rt_(rt) {
    frame_ = &mainFrame_;
}

void Interpreter::runtimeError(const std::string& msg, const SrcPos& pos) const {
    throw BasicError(msg, pos, false);
}

// ---------------------------------------------------------------- Ausfuehrung

void Interpreter::run() {
    frame_ = &mainFrame_;
    mainFrame_.func.clear();
    try {
        execSection(prog_.main);
    } catch (EndSignal& e) {
        rt_.exitCode = e.code;
    } catch (ReturnSignal&) {
        // RETURN im Hauptprogramm beendet das Programm
    }
}

void Interpreter::execSection(const std::vector<StmtP>& body) {
    std::unordered_map<std::string, size_t> labels;
    for (size_t i = 0; i < body.size(); ++i)
        if (body[i]->kind == SK::Label) labels[body[i]->name] = i;

    size_t i = 0;
    while (i < body.size()) {
        if (rt_.quit) throw EndSignal{rt_.exitCode};
        const Stmt& s = *body[i];
        if (s.kind == SK::Label) {
            ++i;
            continue;
        }
        try {
            execStmt(s);
        } catch (GotoSignal& g) {
            auto it = labels.find(g.label);
            if (it == labels.end())
                throw BasicError("Marke nicht gefunden: " + g.label, s.pos, false);
            i = it->second;
            continue;
        }
        ++i;
    }
}

void Interpreter::execBlock(const std::vector<StmtP>& body) {
    for (const auto& sp : body) {
        if (rt_.quit) throw EndSignal{rt_.exitCode};
        execStmt(*sp);
    }
}

// Der INLINE-Block bekommt die Variablen der aktuellen Stelle: lokale
// Variablen und Parameter, Felder des eigenen TYPEs (METHOD), sonst globale.
class Interpreter::InlineStepScope final : public gbxinline_runtime::StepScope {
public:
    InlineStepScope(Interpreter& interp, const SrcPos& pos) : interp_(interp), pos_(pos) {}

    Value* var(const std::string& name) override {
        if (Slot* local = interp_.findLocalSlot(name)) return &local->v;
        if (Value* field = interp_.selfField(name)) return field;
        if (Slot* slot = interp_.findSlot(name)) return &slot->v;
        Slot* created = interp_.createAssignmentSlot(name, TypeHint::None);
        return created ? &created->v : nullptr;
    }

    void fail(const std::string& message) override {
        if (error.empty()) error = message;
    }

    bool arrayPush(Value* array, const Value* item) override {
        ArrayObj* arr = arrayObject(array, "push");
        if (!arr) return false;
        if (isMatrix(*arr)) {
            fail("push: nur fuer eindimensionale Arrays (mehrdimensionale mit resize aendern)");
            return false;
        }
        arr->items.push_back(makeElement(*arr, item));
        return true;
    }

    bool arrayInsert(Value* array, int64_t index, const Value* item) override {
        ArrayObj* arr = arrayObject(array, "insert");
        if (!arr) return false;
        if (isMatrix(*arr)) {
            fail("insert: nur fuer eindimensionale Arrays (mehrdimensionale mit resize aendern)");
            return false;
        }
        const int64_t size = static_cast<int64_t>(arr->items.size());
        int64_t at = index;
        if (at < 0) at += size + 1;
        if (at < 0 || at > size) {
            fail("insert: Index " + std::to_string(index) + " ausserhalb des Arrays");
            return false;
        }
        arr->items.insert(arr->items.begin() + static_cast<std::ptrdiff_t>(at), makeElement(*arr, item));
        return true;
    }

    bool arrayRemove(Value* array, int64_t index) override {
        ArrayObj* arr = arrayObject(array, "remove");
        if (!arr) return false;
        if (isMatrix(*arr)) {
            fail("remove: nur fuer eindimensionale Arrays (mehrdimensionale mit resize aendern)");
            return false;
        }
        const int64_t size = static_cast<int64_t>(arr->items.size());
        int64_t at = index;
        if (at < 0) at += size;
        if (at < 0 || at >= size) {
            fail("remove: Index " + std::to_string(index) + " ausserhalb des Arrays");
            return false;
        }
        arr->items.erase(arr->items.begin() + static_cast<std::ptrdiff_t>(at));
        return true;
    }

    bool arrayClear(Value* array) override {
        ArrayObj* arr = arrayObject(array, "clear");
        if (!arr) return false;
        if (isMatrix(*arr)) {
            fail("clear: nur fuer eindimensionale Arrays (mehrdimensionale mit resize aendern)");
            return false;
        }
        arr->items.clear();
        return true;
    }

    bool arrayResize(Value* array, const std::vector<int>& sizes) override {
        ArrayObj* arr = arrayObject(array, "resize");
        if (!arr) return false;
        if (sizes.empty() || sizes.size() > 4) {
            fail("resize: 1 bis 4 Dimensionen");
            return false;
        }
        for (int size : sizes) {
            if (size < 0) {
                fail("resize: Groesse darf nicht negativ sein");
                return false;
            }
            if (size > 100000000) {
                fail("resize: Array zu gross");
                return false;
            }
        }
        auto fresh = interp_.makeArray(sizes, 0, arr->elemVT,
                                       arr->isTypeArray ? arr->elemType : std::string());
        interp_.copyArrayData(*fresh, *arr);
        *array = Value::mkArray(std::move(fresh));
        return true;
    }

    int arrayRank(Value* array) override {
        ArrayObj* arr = arrayObject(array, "rank");
        if (!arr) return 0;
        int rank = 1;
        const ArrayObj* inner = arr;
        while (!inner->items.empty() && inner->items[0].isArray() && inner->items[0].arr) {
            inner = inner->items[0].arr.get();
            ++rank;
        }
        return rank;
    }

    int arraySize(Value* array, int dim) override {
        ArrayObj* arr = arrayObject(array, "size");
        if (!arr) return 0;
        if (dim < 0) {
            fail("size: Dimension muss 0 oder groesser sein");
            return 0;
        }
        const ArrayObj* inner = arr;
        int64_t size = 0;
        for (int d = 0; d <= dim; ++d) {
            size = static_cast<int64_t>(inner->items.size());
            if (d < dim) {
                if (inner->items.empty() || !inner->items[0].isArray() || !inner->items[0].arr) return 0;
                inner = inner->items[0].arr.get();
            }
        }
        return static_cast<int>(size);
    }

    bool basicCall(const std::string& name, std::vector<Value>& args,
                   std::vector<Value*>& refs, Value& result) override {
        auto it = builtins().find(name);
        if (it == builtins().end()) {
            fail("Unbekannter GBX-Befehl: " + name);
            return false;
        }
        const Builtin& fn = it->second;
        const int count = static_cast<int>(args.size());
        if (count < fn.minArgs || (fn.maxArgs >= 0 && count > fn.maxArgs)) {
            fail("Falsche Anzahl Argumente fuer " + name);
            return false;
        }
        try {
            BArgs call{interp_.rt_, name, args, refs, pos_};
            result = fn.fn(call);
            return true;
        } catch (const BasicError& error) {
            fail(error.what());
            return false;
        }
    }

    std::string error;

private:
    /** Array-Wert pruefen und als ArrayObj liefern; meldet sonst den Fehler. */
    ArrayObj* arrayObject(Value* array, const char* what) {
        if (!array || !array->isArray() || !array->arr) {
            fail(std::string(what) + ": Array erwartet");
            return nullptr;
        }
        return array->arr.get();
    }

    /** Mehrdimensionale Arrays haben Arrays als Elemente (Zeilen). */
    static bool isMatrix(const ArrayObj& arr) {
        return !arr.items.empty() && arr.items[0].isArray();
    }

    /** Neues Element wie DIMPUSH: Text/Zahl wird angepasst, TYPE-Arrays
     *  bekommen einen frischen Datensatz, wenn kein TYPE uebergeben wurde. */
    Value makeElement(const ArrayObj& arr, const Value* item) {
        if (arr.isTypeArray) {
            if (item && item->isRecord()) return *item;
            return interp_.makeRecord(arr.elemType);
        }
        return coerceTo(item ? *item : defaultValueFor(arr.elemVT), arr.elemVT);
    }

    Interpreter& interp_;
    SrcPos pos_;
};

void Interpreter::execStmt(const Stmt& s) {
    rt_.currentFile = s.pos.file;
    rt_.currentLine = s.pos.line;
    if (rt_.dbg) rt_.dbg->atStatement(s.pos, callDepth_, frame_ ? frame_->func : std::string());

    switch (s.kind) {
        case SK::Assign:
            assign(*s.e1, eval(*s.e2));
            break;
        case SK::Call:
            evalCall(s.name, s.exprs, s.pos);
            break;
        case SK::MethodCall:
            callMethod(*s.e1);
            break;
        case SK::InlineStep: {
            // C++ aus einem INLINE-Block: laeuft genau hier und sieht die
            // BASIC-Variablen dieser Stelle.
            InlineStepScope scope(*this, s.pos);
            const size_t index = static_cast<size_t>(s.inlineIndex);
            gbxinline_runtime::runStep(prog_, index, scope);
            if (!scope.error.empty()) runtimeError(scope.error, s.pos);
            break;
        }
        case SK::If:
            if (eval(*s.e1).truthy())
                execBlock(s.body);
            else
                execBlock(s.body2);
            break;
        case SK::For:
            execFor(s);
            break;
        case SK::While: {
            double guard = 0;
            while (eval(*s.e1).truthy()) {
                if (++guard > kMaxLoopIterations)
                    runtimeError("Schleife laeuft zu lange (WHILE)", s.pos);
                try {
                    execBlock(s.body);
                } catch (BreakSignal&) {
                    break;
                } catch (ContinueSignal&) {
                    continue;
                }
            }
            break;
        }
        case SK::Repeat: {
            double guard = 0;
            for (;;) {
                if (++guard > kMaxLoopIterations)
                    runtimeError("Schleife laeuft zu lange (REPEAT)", s.pos);
                try {
                    execBlock(s.body);
                } catch (BreakSignal&) {
                    break;
                } catch (ContinueSignal&) {
                    // weiter zur Abbruchbedingung
                }
                if (eval(*s.e1).truthy()) break;
            }
            break;
        }
        case SK::Select:
            execSelect(s);
            break;
        case SK::Return: {
            Value v = s.e1 ? eval(*s.e1) : Value::mkInt(0);
            throw ReturnSignal{std::move(v)};
        }
        case SK::Break:
            throw BreakSignal{};
        case SK::Continue:
            throw ContinueSignal{};
        case SK::End:
            throw EndSignal{s.e1 ? static_cast<int>(eval(*s.e1).asInt()) : 0};
        case SK::Dim:
            execDim(s, false);
            break;
        case SK::Redim:
            execDim(s, true);
            break;
        case SK::Decl:
            if (s.scope == DeclScope::Global) declareGlobal(s);
            else if (s.scope == DeclScope::Static) declareStatic(s);
            else declareLocal(s);
            break;
        case SK::ConstDecl: {
            Slot slot;
            slot.hint = s.hint;
            slot.isConst = true;
            Value v = eval(*s.e1);
            if (slot.hint != TypeHint::None) v = coerceTo(v, hintToVT(slot.hint));
            slot.v = std::move(v);
            rt_.globals[s.name] = std::move(slot);
            break;
        }
        case SK::Swap: {
            // Beide Ziele als Speicherstellen aufloesen
            Value va = eval(*s.e1);
            Value vb = eval(*s.e2);
            assign(*s.e1, vb);
            assign(*s.e2, va);
            break;
        }
        case SK::IncDec:
            execIncDec(s);
            break;
        case SK::Push: {
            Value arrVal = eval(*s.e1);
            if (!arrVal.isArray() || !arrVal.arr)
                runtimeError("DIMPUSH: '" + (s.e1->kind == EK::Var ? s.e1->text : std::string("Ausdruck")) +
                                 "' ist kein Array", s.pos);
            Value v = eval(*s.e2);
            if (arrVal.arr->isTypeArray) {
                if (!v.isRecord()) v = makeRecord(arrVal.arr->elemType);
            } else {
                v = coerceTo(v, arrVal.arr->elemVT);
            }
            arrVal.arr->items.push_back(std::move(v));
            break;
        }
        case SK::DimDel: {
            Value arrVal = eval(*s.e1);
            if (!arrVal.isArray() || !arrVal.arr) runtimeError("DIMDEL: kein Array", s.pos);
            auto& items = arrVal.arr->items;
            int64_t idx = eval(*s.e2).asInt();
            if (idx < 0) idx += static_cast<int64_t>(items.size());
            if (idx < 0 || idx >= static_cast<int64_t>(items.size()))
                runtimeError("DIMDEL: Index ausserhalb des Arrays", s.pos);
            items.erase(items.begin() + idx);
            break;
        }
        case SK::Foreach:
            execForeach(s);
            break;
        case SK::Gosub: {
            auto it = prog_.funcs.find(s.name);
            if (it == prog_.funcs.end())
                runtimeError("GOSUB: SUB '" + s.name + "' nicht gefunden", s.pos);
            std::vector<ExprP> none;
            callUserFunction(it->second, none, s.pos);
            break;
        }
        case SK::Goto:
            throw GotoSignal{s.name};
        case SK::Label:
            break;
        case SK::Try:
            execTry(s);
            break;
        case SK::Throw: {
            Value v = eval(*s.e1);
            throw BasicError(v.text(), s.pos, false);
        }
        case SK::Nop:
            execBlock(s.body);
            break;
    }
}

void Interpreter::execFor(const Stmt& s) {
    double start = eval(*s.e1).asDouble();
    double end = eval(*s.e2).asDouble();
    double step = s.e3 ? eval(*s.e3).asDouble() : 1.0;
    if (step == 0.0) runtimeError("FOR: STEP darf nicht 0 sein", s.pos);

    Slot* counter = findSlot(s.name);
    if (!counter) counter = createAssignmentSlot(s.name, s.hint);
    TypeHint hint = s.hint != TypeHint::None ? s.hint : counter->hint;

    double guard = 0;
    double v = start;
    bool broken = false;
    for (; step > 0 ? v <= end : v >= end; v += step) {
        if (++guard > kMaxLoopIterations) runtimeError("FOR-Schleife laeuft zu lange", s.pos);
        if (hint == TypeHint::Int)
            counter->v = Value::mkInt(static_cast<int64_t>(v));
        else
            counter->v = Value::mkFloat(v);
        try {
            execBlock(s.body);
        } catch (BreakSignal&) {
            broken = true;
            break;
        } catch (ContinueSignal&) {
            continue;
        }
    }
    // Nach normalem Ende steht der Zaehler auf dem ersten Wert hinter dem
    // Bereich (wie in klassischem BASIC).
    if (!broken) {
        if (hint == TypeHint::Int)
            counter->v = Value::mkInt(static_cast<int64_t>(v));
        else
            counter->v = Value::mkFloat(v);
    }
}

void Interpreter::execSelect(const Stmt& s) {
    Value sel = eval(*s.e1);
    size_t defaultIndex = static_cast<size_t>(-1);

    for (size_t ci = 0; ci < s.cases.size(); ++ci) {
        const CaseBlock& cb = s.cases[ci];
        if (cb.tests.empty()) {
            if (defaultIndex == static_cast<size_t>(-1)) defaultIndex = ci;
            continue;
        }
        bool matched = false;
        for (const CaseTest& t : cb.tests) {
            Value lo = eval(*t.lo);
            if (t.hi) {
                double v = sel.asDouble();
                double a = lo.asDouble();
                double b = eval(*t.hi).asDouble();
                matched = v >= a && v <= b;
            } else {
                switch (t.cmp) {
                    case 0: matched = valueEquals(sel, lo); break;
                    case 1: matched = sel.asDouble() < lo.asDouble(); break;
                    case 2: matched = sel.asDouble() <= lo.asDouble(); break;
                    case 3: matched = sel.asDouble() > lo.asDouble(); break;
                    case 4: matched = sel.asDouble() >= lo.asDouble(); break;
                    case 5: matched = !valueEquals(sel, lo); break;
                }
            }
            if (matched) break;
        }
        if (matched) {
            execBlock(cb.body);
            return;
        }
    }
    if (defaultIndex != static_cast<size_t>(-1)) execBlock(s.cases[defaultIndex].body);
}

void Interpreter::execDim(const Stmt& s, bool redim) {
    std::vector<int> sizes;
    for (const auto& e : s.exprs) {
        int64_t len = eval(*e).asInt();
        if (len < 0) len = 0;
        if (len > 100000000) runtimeError("DIM: Array zu gross", s.pos);
        sizes.push_back(static_cast<int>(len));
    }
    if (sizes.empty()) {
        if (!s.typeName.empty()) {
            Slot slot;
            slot.v = makeRecord(s.typeName);
            if (frame_ && !frame_->func.empty())
                frame_->locals[s.name] = std::move(slot);
            else
                rt_.globals[s.name] = std::move(slot);
        } else {
            runtimeError("DIM: Array-Groesse fehlt (z.B. DIM a[10])", s.pos);
        }
        return;
    }
    if (sizes.size() > 4) runtimeError("DIM: hoechstens 4 Dimensionen", s.pos);

    VT elemVT = hintToVT(s.hint);
    if (elemVT == VT::Null) elemVT = VT::Float;
    auto arr = makeArray(sizes, 0, elemVT, s.typeName);

    if (redim) {
        Slot* old = findSlot(s.name);
        if (old && old->v.isArray() && old->v.arr) copyArrayData(*arr, *old->v.arr);
    }
    assignArray(s.name, std::move(arr));
}

void Interpreter::execIncDec(const Stmt& s) {
    Value* target = nullptr;
    // Felder des eigenen TYPEs laufen ueber eval/assign (siehe unten).
    const bool selfFieldVar =
        s.e1->kind == EK::Var && !findSlot(s.e1->text) && selfField(s.e1->text) != nullptr;
    if (s.e1->kind == EK::Var && !selfFieldVar) {
        Slot* slot = findSlot(s.e1->text);
        if (!slot) slot = createAssignmentSlot(s.e1->text, s.e1->hint);
        if (slot->isConst) runtimeError("Konstante kann nicht geaendert werden", s.pos);
        target = &slot->v;
    } else {
        // Array-Element oder TYPE-Feld: ueber assign() schreiben
        Value* dummy = nullptr;
        (void)dummy;
        Value cur = eval(*s.e1);
        double amount = s.e2 ? eval(*s.e2).asDouble() : 1.0;
        Value nv;
        if (cur.type == VT::Int && amount == std::floor(amount))
            nv = Value::mkInt(cur.i + static_cast<int64_t>(s.delta) * static_cast<int64_t>(amount));
        else
            nv = Value::mkFloat(cur.asDouble() + s.delta * amount);
        assign(*s.e1, nv);
        return;
    }

    double amount = s.e2 ? eval(*s.e2).asDouble() : 1.0;
    if (target->type == VT::Int && amount == std::floor(amount))
        target->i += static_cast<int64_t>(s.delta) * static_cast<int64_t>(amount);
    else
        *target = Value::mkFloat(target->asDouble() + s.delta * amount);
}

void Interpreter::execForeach(const Stmt& s) {
    Value arrVal = eval(*s.e1);
    if (!arrVal.isArray() || !arrVal.arr) runtimeError("FOREACH: kein Array", s.pos);
    ArrayObj* arr = arrVal.arr.get();

    Slot& iterSlot = frame_->locals[s.name];
    Slot* previousAlias = nullptr;
    auto aliasIt = frame_->aliases.find(s.name);
    if (aliasIt != frame_->aliases.end()) previousAlias = aliasIt->second;
    frame_->aliases[s.name] = &iterSlot;

    try {
        for (size_t i = 0; i < arr->items.size(); ++i) {
            iterSlot.v = arr->items[i];
            try {
                execBlock(s.body);
            } catch (BreakSignal&) {
                break;
            } catch (ContinueSignal&) {
                // weiter
            }
            arr->items[i] = iterSlot.v;
        }
    } catch (...) {
        frame_->aliases.erase(s.name);
        if (previousAlias) frame_->aliases[s.name] = previousAlias;
        throw;
    }
    frame_->aliases.erase(s.name);
    if (previousAlias) frame_->aliases[s.name] = previousAlias;
}

void Interpreter::execTry(const Stmt& s) {
    try {
        execBlock(s.body);
    } catch (BasicError& e) {
        if (e.compile) throw;
        if (s.name.empty()) return;
        Slot* slot = findSlot(s.name);
        if (!slot) slot = createAssignmentSlot(s.name, TypeHint::Str);
        slot->v = Value::mkStr(e.what());
        slot->hint = TypeHint::Str;
        execBlock(s.body2);
    }
}

// ---------------------------------------------------------------- Variablen

Slot* Interpreter::findLocalSlot(const std::string& name) {
    if (!frame_) return nullptr;
    auto a = frame_->aliases.find(name);
    if (a != frame_->aliases.end()) return a->second;
    auto l = frame_->locals.find(name);
    if (l != frame_->locals.end()) return &l->second;
    return nullptr;
}

Slot* Interpreter::findSlot(const std::string& name) {
    if (Slot* local = findLocalSlot(name)) return local;
    auto g = rt_.globals.find(name);
    if (g != rt_.globals.end()) return &g->second;
    return nullptr;
}

// In einer METHOD sind die Felder des eigenen TYPEs wie Variablen sichtbar.
Value* Interpreter::selfField(const std::string& name) {
    if (!frame_ || !frame_->selfVal.isRecord() || !frame_->selfVal.rec) return nullptr;
    RecordObj* rec = frame_->selfVal.rec.get();
    if (!rec->def) return nullptr;
    int idx = rec->def->fieldIndex(name);
    if (idx < 0) return nullptr;
    return &rec->fields[static_cast<size_t>(idx)];
}

Slot* Interpreter::createAssignmentSlot(const std::string& name, TypeHint hint) {
    if (frame_) {
        auto a = frame_->aliases.find(name);
        if (a != frame_->aliases.end()) return a->second;
        auto l = frame_->locals.find(name);
        if (l != frame_->locals.end()) return &l->second;
    }
    auto g = rt_.globals.find(name);
    if (g != rt_.globals.end()) return &g->second;

    Slot slot;
    slot.hint = hint;
    if (frame_ && !frame_->func.empty()) {
        auto res = frame_->locals.emplace(name, slot);
        return &res.first->second;
    }
    auto res = rt_.globals.emplace(name, slot);
    return &res.first->second;
}

void Interpreter::declareGlobal(const Stmt& s) {
    Slot slot;
    slot.hint = s.hint;
    if (!s.typeName.empty())
        slot.v = makeRecord(s.typeName);
    else if (s.hint != TypeHint::None)
        slot.v = defaultValueFor(hintToVT(s.hint));
    if (s.e1) {
        Value v = eval(*s.e1);
        if (slot.hint != TypeHint::None && !v.isArray() && !v.isRecord())
            v = coerceTo(v, hintToVT(slot.hint));
        slot.v = std::move(v);
    }
    rt_.globals[s.name] = std::move(slot);
}

void Interpreter::declareLocal(const Stmt& s) {
    if (!frame_) return;
    Slot slot;
    slot.hint = s.hint;
    if (!s.typeName.empty())
        slot.v = makeRecord(s.typeName);
    else if (s.hint != TypeHint::None)
        slot.v = defaultValueFor(hintToVT(s.hint));
    if (s.e1) {
        Value v = eval(*s.e1);
        if (slot.hint != TypeHint::None && !v.isArray() && !v.isRecord())
            v = coerceTo(v, hintToVT(slot.hint));
        slot.v = std::move(v);
    }
    frame_->locals[s.name] = std::move(slot);
}

void Interpreter::declareStatic(const Stmt& s) {
    std::string key = frame_->func + "|" + s.name;
    auto it = statics_.find(key);
    if (it == statics_.end()) {
        Slot slot;
        slot.hint = s.hint;
        if (!s.typeName.empty())
            slot.v = makeRecord(s.typeName);
        else if (s.hint != TypeHint::None)
            slot.v = defaultValueFor(hintToVT(s.hint));
        if (s.e1) {
            Value v = eval(*s.e1);
            if (slot.hint != TypeHint::None && !v.isArray() && !v.isRecord())
                v = coerceTo(v, hintToVT(slot.hint));
            slot.v = std::move(v);
        }
        it = statics_.emplace(key, std::move(slot)).first;
    }
    frame_->aliases[s.name] = &it->second;
}

void Interpreter::assign(const Expr& target, Value v) {
    if (target.kind == EK::Var) {
        if (Slot* local = findLocalSlot(target.text)) {
            if (local->isConst)
                runtimeError("Konstante '" + target.text + "' kann nicht geaendert werden", target.pos);
            if (local->hint == TypeHint::None && target.hint != TypeHint::None)
                local->hint = target.hint;
            if (local->hint != TypeHint::None && !v.isArray() && !v.isRecord())
                v = coerceTo(v, hintToVT(local->hint));
            local->v = std::move(v);
            return;
        }
        // Feld des eigenen TYPEs: `w = w * 2` innerhalb einer METHOD.
        if (Value* field = selfField(target.text)) {
            const int idx = frame_->selfVal.rec->def->fieldIndex(target.text);
            const FieldDef& fd = frame_->selfVal.rec->def->fields[static_cast<size_t>(idx)];
            if (fd.type == VT::Record && fd.typeName.size() && !v.isRecord())
                v = makeRecord(fd.typeName);
            else if (fd.type != VT::Record && !v.isArray() && !v.isRecord())
                v = coerceTo(v, fd.type);
            *field = std::move(v);
            return;
        }
        Slot* slot = findSlot(target.text);
        if (!slot) slot = createAssignmentSlot(target.text, target.hint);
        if (slot->isConst)
            runtimeError("Konstante '" + target.text + "' kann nicht geaendert werden", target.pos);
        if (slot->hint == TypeHint::None && target.hint != TypeHint::None)
            slot->hint = target.hint;
        if (slot->hint != TypeHint::None && !v.isArray() && !v.isRecord())
            v = coerceTo(v, hintToVT(slot->hint));
        slot->v = std::move(v);
        return;
    }
    if (target.kind == EK::Index) {
        Value base = eval(*target.a);
        if (!base.isArray() || !base.arr)
            runtimeError("Zuweisung: Ausdruck ist kein Array", target.pos);
        ArrayObj* arr = base.arr.get();
        size_t idx = static_cast<size_t>(toIndex(*target.b, arr->items.size(), target.pos));
        if (arr->isTypeArray) {
            if (!v.isRecord()) v = makeRecord(arr->elemType);
        } else if (!v.isArray() && !v.isRecord()) {
            v = coerceTo(v, arr->elemVT);
        }
        arr->items[idx] = std::move(v);
        return;
    }
    if (target.kind == EK::Field) {
        Value base = eval(*target.a);
        if (!base.isRecord() || !base.rec)
            runtimeError("Zuweisung: Ausdruck ist kein TYPE", target.pos);
        RecordObj* rec = base.rec.get();
        int idx = rec->def->fieldIndex(target.text);
        if (idx < 0) runtimeError("TYPE '" + rec->def->name + "' hat kein Feld '" + target.text + "'",
                                  target.pos);
        const FieldDef& fd = rec->def->fields[static_cast<size_t>(idx)];
        if (fd.type == VT::Record && fd.typeName.size() && !v.isRecord())
            v = makeRecord(fd.typeName);
        else if (fd.type != VT::Record && !v.isArray() && !v.isRecord())
            v = coerceTo(v, fd.type);
        rec->fields[static_cast<size_t>(idx)] = std::move(v);
        return;
    }
    runtimeError("Ungueltiges Zuweisungsziel", target.pos);
}

void Interpreter::assignArray(const std::string& name, std::shared_ptr<ArrayObj> arr) {
    Value v = Value::mkArray(std::move(arr));
    Slot* slot = findSlot(name);
    if (!slot) slot = createAssignmentSlot(name, TypeHint::None);
    if (slot->isConst) runtimeError("Konstante '" + name + "' kann nicht geaendert werden", SrcPos{});
    slot->hint = TypeHint::None;
    slot->v = std::move(v);
}

Value Interpreter::readVar(const Expr& e) {
    // SELF ist in einer METHOD das eigene Objekt: self.x, self.methode().
    if (e.text == "SELF" && frame_ && frame_->selfVal.isRecord()) return frame_->selfVal;
    if (Slot* local = findLocalSlot(e.text)) return local->v;
    if (Value* field = selfField(e.text)) return *field;
    Slot* slot = findSlot(e.text);
    if (!slot) {
        rt_.warnOnce("Variable ohne Deklaration: " + e.text + " (wird als 0 angenommen)");
        Slot s;
        s.hint = e.hint;
        s.v = e.hint != TypeHint::None ? defaultValueFor(hintToVT(e.hint)) : Value::mkInt(0);
        auto res = rt_.globals.emplace(e.text, std::move(s));
        return res.first->second.v;
    }
    return slot->v;
}

uint32_t Interpreter::toIndex(const Expr& e, size_t size, const SrcPos& pos) {
    int64_t raw = eval(e).asInt();
    int64_t idx = raw;
    if (idx < 0) idx += static_cast<int64_t>(size);
    if (idx < 0 || idx >= static_cast<int64_t>(size)) {
        std::string range = size == 0 ? std::string("0..-1") :
                            "0.." + std::to_string(static_cast<long long>(size) - 1);
        runtimeError("Array-Index " + std::to_string(raw) + " ausserhalb von " + range, pos);
    }
    return static_cast<uint32_t>(idx);
}

// ---------------------------------------------------------------- Datentypen

Value Interpreter::defaultElement(VT elemVT, const std::string& typeName) {
    if (!typeName.empty()) return makeRecord(typeName);
    return defaultValueFor(elemVT);
}

std::shared_ptr<ArrayObj> Interpreter::makeArray(const std::vector<int>& sizes, size_t dim,
                                                 VT elemVT, const std::string& typeName) {
    auto arr = std::make_shared<ArrayObj>();
    arr->elemVT = elemVT;
    arr->isTypeArray = !typeName.empty();
    arr->elemType = typeName;
    arr->items.resize(static_cast<size_t>(sizes[dim]));
    if (dim + 1 < sizes.size()) {
        for (auto& item : arr->items)
            item = Value::mkArray(makeArray(sizes, dim + 1, elemVT, typeName));
    } else {
        for (auto& item : arr->items) item = defaultElement(elemVT, typeName);
    }
    return arr;
}

Value Interpreter::makeRecord(const std::string& typeName, int depth) {
    if (depth > kMaxRecordDepth) runtimeError("TYPE-Schachtelung zu tief: " + typeName, SrcPos{});
    auto it = prog_.types.find(typeName);
    if (it == prog_.types.end())
        runtimeError("Unbekannter TYPE: " + typeName, SrcPos{});
    const TypeDef* def = it->second.get();
    auto rec = std::make_shared<RecordObj>();
    rec->def = def;
    rec->fields.resize(def->fields.size());
    for (size_t i = 0; i < def->fields.size(); ++i) {
        const FieldDef& fd = def->fields[i];
        if (fd.type == VT::Record && !fd.typeName.empty())
            rec->fields[i] = makeRecord(fd.typeName, depth + 1);
        else
            rec->fields[i] = defaultValueFor(fd.type);
    }
    // Standardwerte aus der TYPE-Definition ("x% = 0") überschreiben die
    // Nullwerte. Sie werden pro Aufruf neu ausgewertet, damit jede Instanz
    // ihren eigenen Wert bekommt.
    auto init = prog_.typeDefaults.find(typeName);
    if (init != prog_.typeDefaults.end()) {
        const auto& exprs = init->second;
        for (size_t i = 0; i < rec->fields.size() && i < exprs.size(); ++i) {
            if (!exprs[i]) continue;
            Value v = eval(*exprs[i]);
            const FieldDef& fd = def->fields[i];
            if (fd.type != VT::Record) v = coerceTo(v, fd.type);
            rec->fields[i] = std::move(v);
        }
    }
    return Value::mkRecord(std::move(rec));
}

void Interpreter::copyArrayData(ArrayObj& dst, const ArrayObj& src) {
    size_t n = std::min(dst.items.size(), src.items.size());
    for (size_t i = 0; i < n; ++i) {
        if (dst.items[i].isArray() && src.items[i].isArray() && dst.items[i].arr && src.items[i].arr)
            copyArrayData(*dst.items[i].arr, *src.items[i].arr);
        else
            dst.items[i] = src.items[i];
    }
}

// ---------------------------------------------------------------- Ausdruecke

Value Interpreter::evalCall(const std::string& name, const std::vector<ExprP>& args,
                            const SrcPos& pos) {
    if (name == "IIF") {
        if (args.size() != 3) runtimeError("IIF erwartet 3 Argumente", pos);
        return eval(*args[0]).truthy() ? eval(*args[1]) : eval(*args[2]);
    }

    auto fit = prog_.funcs.find(name);
    if (fit == prog_.funcs.end() && !name.empty() &&
        (name.back() == '$' || name.back() == '%' || name.back() == '#')) {
        fit = prog_.funcs.find(name.substr(0, name.size() - 1));
    }
    if (fit != prog_.funcs.end()) {
        return callUserFunction(fit->second, args, pos);
    }

    // In einer METHOD sind die Methoden des eigenen TYPEs ohne Objekt aufrufbar:
    // `skalieren(2)` meint `self.skalieren(2)`.
    if (frame_ && frame_->selfVal.isRecord() && frame_->selfVal.rec && frame_->selfVal.rec->def) {
        const std::string owner = frame_->selfVal.rec->def->name;
        auto mit = prog_.funcs.find(owner + "." + name);
        if (mit == prog_.funcs.end() && !name.empty() &&
            (name.back() == '$' || name.back() == '%' || name.back() == '#')) {
            mit = prog_.funcs.find(owner + "." + name.substr(0, name.size() - 1));
        }
        if (mit != prog_.funcs.end() && mit->second.isMethod()) {
            Value selfCopy = frame_->selfVal;  // teilt das Record
            return callUserFunction(mit->second, args, pos, &selfCopy);
        }
    }

    // Funktionen aus INLINE-Bloecken stehen zwischen eigenen Funktionen und
    // den eingebauten Befehlen.
    if (gbxinline_runtime::has(name)) {
        std::vector<Value> vals;
        vals.reserve(args.size());
        for (const auto& arg : args) vals.push_back(eval(*arg));
        return gbxinline_runtime::call(name, vals, pos);
    }

    auto bit = builtins().find(name);
    if (bit == builtins().end()) {
        runtimeError("Unbekannter Befehl oder Funktion: " + name, pos);
    }
    const Builtin& b = bit->second;
    if (static_cast<int>(args.size()) < b.minArgs ||
        (b.maxArgs >= 0 && static_cast<int>(args.size()) > b.maxArgs)) {
        runtimeError("Falsche Anzahl Argumente fuer " + name, pos);
    }

    std::vector<Value> vals;
    std::vector<Value*> refs;
    vals.reserve(args.size());
    refs.reserve(args.size());
    for (const auto& a : args) {
        if (a->kind == EK::Var) {
            Slot* slot = findSlot(a->text);
            if (!slot) slot = createAssignmentSlot(a->text, a->hint);
            refs.push_back(&slot->v);
            vals.push_back(slot->v);
        } else if (a->kind == EK::Index || a->kind == EK::Field) {
            // Referenz auf Array-Element / TYPE-Feld
            if (a->kind == EK::Index) {
                Value base = eval(*a->a);
                if (base.isArray() && base.arr) {
                    size_t idx = static_cast<size_t>(toIndex(*a->b, base.arr->items.size(), a->pos));
                    refs.push_back(&base.arr->items[idx]);
                    vals.push_back(base.arr->items[idx]);
                } else {
                    refs.push_back(nullptr);
                    vals.push_back(eval(*a));
                }
            } else {
                Value base = eval(*a->a);
                if (base.isRecord() && base.rec) {
                    int idx = base.rec->def->fieldIndex(a->text);
                    if (idx >= 0) {
                        refs.push_back(&base.rec->fields[static_cast<size_t>(idx)]);
                        vals.push_back(base.rec->fields[static_cast<size_t>(idx)]);
                    } else {
                        refs.push_back(nullptr);
                        vals.push_back(eval(*a));
                    }
                } else {
                    refs.push_back(nullptr);
                    vals.push_back(eval(*a));
                }
            }
        } else {
            refs.push_back(nullptr);
            vals.push_back(eval(*a));
        }
    }

    BArgs ba{rt_, name, vals, refs, pos};
    return b.fn(ba);
}

Value Interpreter::callUserFunction(const FuncDef& def, const std::vector<ExprP>& args,
                                    const SrcPos& pos, const Value* self) {
    if (++callDepth_ > kMaxCallDepth) {
        --callDepth_;
        runtimeError("Zu viele verschachtelte Funktionsaufrufe (Rekursion)", pos);
    }

    std::vector<Value> argVals(args.size());
    std::vector<Value*> backRefs(args.size(), nullptr);

    for (size_t i = 0; i < args.size(); ++i) {
        bool byref = i < def.params.size() && def.params[i].byref;
        if (byref) {
            const Expr& a = *args[i];
            if (a.kind == EK::Var) {
                Slot* slot = findSlot(a.text);
                if (!slot) slot = createAssignmentSlot(a.text, a.hint);
                backRefs[i] = &slot->v;
            } else if (a.kind == EK::Index) {
                Value base = eval(*a.a);
                if (base.isArray() && base.arr) {
                    size_t idx = static_cast<size_t>(toIndex(*a.b, base.arr->items.size(), a.pos));
                    backRefs[i] = &base.arr->items[idx];
                }
            } else if (a.kind == EK::Field) {
                Value base = eval(*a.a);
                if (base.isRecord() && base.rec) {
                    int idx = base.rec->def->fieldIndex(a.text);
                    if (idx >= 0) backRefs[i] = &base.rec->fields[static_cast<size_t>(idx)];
                }
            }
            if (!backRefs[i]) runtimeError("BYREF-Argument muss eine Variable sein", a.pos);
            argVals[i] = *backRefs[i];
        } else {
            argVals[i] = eval(*args[i]);
        }
    }

    Frame newFrame;
    newFrame.func = def.isMethod() ? def.ownerType + "." + def.name : def.name;
    if (self && self->isRecord() && self->rec) newFrame.selfVal = *self;
    for (size_t i = 0; i < def.params.size(); ++i) {
        const Param& p = def.params[i];
        Value v;
        if (i < args.size())
            v = argVals[i];
        else if (p.def)
            v = eval(*p.def);
        else if (!p.typeName.empty())
            v = makeRecord(p.typeName);
        else if (p.hint != TypeHint::None)
            v = defaultValueFor(hintToVT(p.hint));
        else
            v = Value::mkInt(0);

        if (!p.typeName.empty() && !v.isRecord()) v = makeRecord(p.typeName);
        if (p.hint != TypeHint::None && !v.isRecord() && !v.isArray())
            v = coerceTo(v, hintToVT(p.hint));

        Slot slot;
        slot.v = std::move(v);
        slot.hint = p.hint;
        newFrame.locals[p.name] = std::move(slot);
    }

    Frame* saved = frame_;
    frame_ = &newFrame;
    stack_.push_back(def.name);
    Value result = Value::mkInt(0);
    try {
        execSection(def.body);
    } catch (ReturnSignal& r) {
        result = std::move(r.value);
    } catch (...) {
        frame_ = saved;
        stack_.pop_back();
        --callDepth_;
        throw;
    }
    // BYREF-Parameter zurueckschreiben
    for (size_t i = 0; i < backRefs.size() && i < def.params.size(); ++i) {
        if (!backRefs[i] || !def.params[i].byref) continue;
        auto it = newFrame.locals.find(def.params[i].name);
        if (it != newFrame.locals.end()) *backRefs[i] = it->second.v;
    }

    frame_ = saved;
    stack_.pop_back();
    --callDepth_;
    return result;
}

Value Interpreter::callMethod(const Expr& call) {
    Value base = eval(*call.a);
    if (!base.isRecord() || !base.rec || !base.rec->def) {
        runtimeError("Methodenaufruf: links vom Punkt steht kein TYPE", call.pos);
    }
    RecordObj* rec = base.rec.get();
    auto it = prog_.funcs.find(rec->def->name + "." + call.text);
    if (it == prog_.funcs.end() || !it->second.isMethod()) {
        runtimeError("TYPE '" + rec->def->name + "' hat keine Methode '" + call.text + "'", call.pos);
    }
    return callUserFunction(it->second, call.args, call.pos, &base);
}

std::string Interpreter::debugScope() const {
    std::string out = "{\"func\":\"";
    out += debugJsonEscape(frame_ ? frame_->func : std::string());
    out += "\",\"depth\":" + std::to_string(callDepth_);

    // Aufrufliste von aussen nach innen.
    out += ",\"stack\":[";
    for (size_t i = stack_.size(); i-- > 0;) {
        out += "\"" + debugJsonEscape(stack_[i]) + "\"";
        if (i) out += ",";
    }
    out += "],";

    std::vector<DebugVar> locals;
    if (frame_) {
        for (const auto& entry : frame_->locals)
            locals.push_back({entry.first, debugTypeName(entry.second.v), debugValueText(entry.second.v)});
    }
    out += "\"locals\":" + debugVarsJson(std::move(locals)) + ",";

    std::vector<DebugVar> globals;
    for (const auto& entry : rt_.globals) {
        // Die Tastatur-Konstanten wuerden die Liste unbrauchbar machen.
        if (entry.first.rfind("KEY_", 0) == 0) continue;
        if (entry.first == "TRUE" || entry.first == "FALSE") continue;
        globals.push_back({entry.first, debugTypeName(entry.second.v), debugValueText(entry.second.v)});
    }
    out += "\"globals\":" + debugVarsJson(std::move(globals));
    return out + "}";
}

Value Interpreter::debugEval(const std::string& text, bool& ok, std::string& error) {
    ok = false;
    error.clear();
    try {
        // Der Ausdruck wird wie Quelltext gelesen, aber nur ausgewertet. Ein
        // eigenes Program-Objekt nimmt die (leeren) Include-/TYPE-Listen auf.
        Program scratch;
        const std::string path = rt_.currentFile.empty() ? prog_.mainFile : rt_.currentFile;
        Parser parser(path, lexFile(path, text), scratch);
        ExprP expr = parser.parseExpression();
        Value value = eval(*expr);
        ok = true;
        return value;
    } catch (BasicError& e) {
        error = e.what();
        return Value::mkNull();
    } catch (std::exception& e) {
        error = e.what();
        return Value::mkNull();
    }
}

std::string Interpreter::debugInspect(const std::string& text, bool& ok, std::string& error) {
    // Nur echte Variablen auswerten: Ein unbekannter Name legt sonst still
    // eine neue Variable mit 0 an, und der Tooltip zeigte fuer jedes Wort im
    // Text eine Zahl.
    const std::string head = leadingName(text);
    if (!head.empty()) {
        bool known = head == "SELF";
        if (!known && frame_) known = frame_->locals.count(head) > 0 || frame_->aliases.count(head) > 0;
        // In einer METHOD sind die Felder des eigenen Objekts wie Variablen sichtbar.
        if (!known) known = selfField(head) != nullptr;
        if (!known) known = rt_.globals.count(head) > 0;
        if (!known) {
            ok = false;
            error = "Unbekannter Name";
            return std::string();
        }
    }

    Value value = debugEval(text, ok, error);
    if (!ok) return std::string();

    DetailWriter w;
    debugDetail(value, "", 0, 0, w);
    // Ein leerer TYPE hat keine Felder; dann bleibt wenigstens die Kurzform.
    if (w.lines.empty()) w.add(debugValueText(value));
    std::string out = "{\"type\":\"" + debugJsonEscape(debugTypeName(value)) + "\",\"detail\":\"";
    for (size_t i = 0; i < w.lines.size(); ++i) {
        if (i) out += "\\n";
        out += debugJsonEscape(w.lines[i]);
    }
    return out + "\"}";
}

Value Interpreter::eval(const Expr& e) {
    switch (e.kind) {
        case EK::IntLit:
            return Value::mkInt(e.ival);
        case EK::FloatLit:
            return Value::mkFloat(e.fval);
        case EK::StrLit:
            return Value::mkStr(e.text);
        case EK::Var:
            return readVar(e);
        case EK::Unary: {
            Value v = eval(*e.a);
            switch (e.un) {
                case UnOp::Neg:
                    if (v.type == VT::Int) return Value::mkInt(-v.i);
                    return Value::mkFloat(-v.asDouble());
                case UnOp::Plus:
                    return v;
                case UnOp::Not:
                    return Value::mkInt(v.truthy() ? 0 : 1);
            }
            return Value::mkNull();
        }
        case EK::Binary: {
            if (e.bin == BinOp::And) {
                if (!eval(*e.a).truthy()) return Value::mkInt(0);
                return Value::mkInt(eval(*e.b).truthy() ? 1 : 0);
            }
            if (e.bin == BinOp::Or) {
                if (eval(*e.a).truthy()) return Value::mkInt(1);
                return Value::mkInt(eval(*e.b).truthy() ? 1 : 0);
            }
            Value a = eval(*e.a);
            Value b = eval(*e.b);
            switch (e.bin) {
                case BinOp::Add:
                    if (a.isStr() || b.isStr()) return Value::mkStr(a.text() + b.text());
                    if (bothInt(a, b)) return Value::mkInt(a.i + b.i);
                    return Value::mkFloat(a.asDouble() + b.asDouble());
                case BinOp::Sub:
                    if (bothInt(a, b)) return Value::mkInt(a.i - b.i);
                    return Value::mkFloat(a.asDouble() - b.asDouble());
                case BinOp::Mul:
                    if (bothInt(a, b)) return Value::mkInt(a.i * b.i);
                    return Value::mkFloat(a.asDouble() * b.asDouble());
                case BinOp::Div: {
                    double d = b.asDouble();
                    if (d == 0.0) runtimeError("Division durch Null", e.pos);
                    return Value::mkFloat(a.asDouble() / d);
                }
                case BinOp::Eq:
                    return Value::mkInt(valueEquals(a, b) ? 1 : 0);
                case BinOp::Ne:
                    return Value::mkInt(valueEquals(a, b) ? 0 : 1);
                case BinOp::Lt:
                case BinOp::Le:
                case BinOp::Gt:
                case BinOp::Ge: {
                    if (a.isNum() && b.isNum()) {
                        double x = a.asDouble(), y = b.asDouble();
                        if (e.bin == BinOp::Lt) return Value::mkInt(x < y);
                        if (e.bin == BinOp::Le) return Value::mkInt(x <= y);
                        if (e.bin == BinOp::Gt) return Value::mkInt(x > y);
                        return Value::mkInt(x >= y);
                    }
                    std::string x = a.text(), y = b.text();
                    if (e.bin == BinOp::Lt) return Value::mkInt(x < y);
                    if (e.bin == BinOp::Le) return Value::mkInt(x <= y);
                    if (e.bin == BinOp::Gt) return Value::mkInt(x > y);
                    return Value::mkInt(x >= y);
                }
                default:
                    break;
            }
            return Value::mkNull();
        }
        case EK::Call:
            return evalCall(e.text, e.args, e.pos);
        case EK::Index: {
            Value base = eval(*e.a);
            if (!base.isArray() || !base.arr)
                runtimeError("Ausdruck ist kein Array", e.pos);
            size_t idx = static_cast<size_t>(toIndex(*e.b, base.arr->items.size(), e.pos));
            return base.arr->items[idx];
        }
        case EK::MethodCall:
            return callMethod(e);
        case EK::Field: {
            Value base = eval(*e.a);
            if (!base.isRecord() || !base.rec)
                runtimeError("Ausdruck ist kein TYPE", e.pos);
            int idx = base.rec->def->fieldIndex(e.text);
            if (idx < 0)
                runtimeError("TYPE '" + base.rec->def->name + "' hat kein Feld '" + e.text + "'", e.pos);
            return base.rec->fields[static_cast<size_t>(idx)];
        }
    }
    return Value::mkNull();
}
