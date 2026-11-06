// GameBasiX - Namenspruefung
//
// Der Parser kennt nur die Struktur, nicht die Bedeutung. Hier bekommt jeder
// Name eine Bedeutung: Befehle, Funktionen, Konstanten, Variablen und Marken
// muessen irgendwo im Programm vorkommen. Was nirgends auftaucht, ist ein
// Tippfehler und wird gemeldet - mit Datei und Zeile, damit die IDE es
// anstreichen kann.
//
// Laufende Programme bleiben bewusst nachsichtig: eine nie zugewiesene
// Variable liefert dort weiter den Standardwert. Nur --check ist streng.
#include "lint.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "builtins.h"
#include "inline.h"
#include "runtime.h"

namespace {
std::string upperWord(const std::string& text) {
    std::string out = text;
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}
}  // namespace

namespace {

/** Bezeichner ohne Typ-Suffix ($ % #), so wie der Parser Variablen speichert. */
std::string withoutSuffix(const std::string& name) {
    if (!name.empty() && (name.back() == '$' || name.back() == '%' || name.back() == '#')) {
        return name.substr(0, name.size() - 1);
    }
    return name;
}

/** Abstand zweier Woerter als Anzahl Einfuegungen, Loeschungen, Ersetzungen. */
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

/** Aehlicher bekannter Name - nur bei eindeutigem Tippfehler. */
std::string closestName(const std::vector<std::string>& names, const std::string& want) {
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

/**
 * Befehle mit Ausgabeparametern: eine Variable, die dort steht, wird erst
 * angelegt. Ohne diese Liste waere `INPUT name$` ein unbekannter Name.
 */
const std::unordered_set<std::string>& outputCommands() {
    static const std::unordered_set<std::string> names = {
        "SCREEN_SIZE", "DESKTOP_SIZE", "VIEWPORT", "CAMERA_SCREEN_TO_WORLD",
        "CAMERA_WORLD_TO_SCREEN", "SPRITE_SIZE",
        "FONT_SIZE",    "INPUT",         "MOUSE_STATE",  "DIMDATA",
        "SPLIT_STRING",       "FILE_READ_BYTE",      "FILE_READ_UBYTE",   "FILE_READ_WORD",
        "FILE_READ_UWORD",      "FILE_READ_LONG",      "FILE_READ_ULONG",   "FILE_READ_DOUBLE",
        "FILE_READ_FLOAT",  "FILE_READ_STR",       "FILE_READ_LINE"};
    return names;
}

class Linter {
public:
    explicit Linter(const Program& prog) : prog_(prog) {}

    std::vector<LintIssue> run() {
        for (const auto& [name, fn] : prog_.funcs) {
            funcs_.insert(name);
            funcs_.insert(withoutSuffix(name));
            for (const auto& param : fn.params) {
                defined_.insert(param.name);
                declareType(param.name, param.typeName);
                collectExpr(param.def.get());
            }
        }
        for (const auto& entry : prog_.typeDefaults) {
            for (const auto& expr : entry.second) collectExpr(expr.get());
        }
        for (const auto& [name, value] : builtinConstants()) constants_.insert(name);
        // Was ein INLINE-Block per GBX_INLINE_FN(...) anmeldet, ist ein
        // bekannter Befehl - auch ohne dass der Block uebersetzt wird.
        for (const auto& name : gbxinline_runtime::registeredNames(prog_)) funcs_.insert(name);

        collectBlock(prog_.main);
        for (const auto& [name, fn] : prog_.funcs) collectBlock(fn.body);
        // `q = p` erbt den TYPE von p; zwei Durchlaeufe reichen fuer Ketten.
        for (int pass = 0; pass < 2; ++pass) {
            propagateTypes(prog_.main);
            for (const auto& [name, fn] : prog_.funcs) propagateTypes(fn.body);
        }

        checkBlock(prog_.main);
        for (const auto& [name, fn] : prog_.funcs) {
            // In einer METHOD sind die Felder des eigenen TYPEs bekannte Namen.
            std::vector<std::string> injected;
            if (fn.isMethod()) {
                if (defined_.insert("SELF").second) injected.push_back("SELF");
                auto type = prog_.types.find(fn.ownerType);
                if (type != prog_.types.end() && type->second) {
                    for (const auto& field : type->second->fields) {
                        if (defined_.insert(field.name).second) injected.push_back(field.name);
                    }
                }
            }
            currentOwner_ = fn.ownerType;
            checkBlock(fn.body);
            currentOwner_.clear();
            for (const auto& field : injected) defined_.erase(field);
        }
        for (const auto& entry : prog_.typeDefaults) {
            for (const auto& expr : entry.second) checkExpr(expr.get());
        }
        return issues_;
    }

private:
    const Program& prog_;
    std::unordered_set<std::string> defined_;   // Variablen, Parameter, CATCH-Variablen
    std::unordered_set<std::string> constants_; // TRUE/FALSE, KEY_*, CONST ...
    std::unordered_set<std::string> labels_;
    std::unordered_set<std::string> funcs_;
    std::string currentOwner_;                              // TYPE der laufenden METHOD
    std::unordered_map<std::string, std::string> varType_;  // Variable -> TYPE
    std::unordered_set<std::string> mixedTypes_;            // mehrdeutig: nicht pruefen
    std::vector<LintIssue> issues_;
    std::unordered_set<std::string> seen_;

    /** Merkt sich den TYPE einer Variablen; widerspruechliche Angaben werden ignoriert. */
    void declareType(const std::string& name, const std::string& typeName) {
        if (name.empty() || typeName.empty() || mixedTypes_.count(name)) return;
        auto it = varType_.find(name);
        if (it == varType_.end()) {
            varType_[name] = typeName;
        } else if (it->second != typeName) {
            varType_.erase(it);
            mixedTypes_.insert(name);
        }
    }

    /** Statischer TYPE eines Ausdrucks, sofern eindeutig bekannt. */
    std::string typeOf(const Expr* e) const {
        if (!e) return "";
        if (e->kind == EK::Var) {
            if (upperWord(e->text) == "SELF" && !currentOwner_.empty()) return currentOwner_;
            auto it = varType_.find(e->text);
            return it == varType_.end() ? "" : it->second;
        }
        if (e->kind == EK::Index) return typeOf(e->a.get());
        if (e->kind == EK::Field) {
            const std::string base = typeOf(e->a.get());
            if (base.empty()) return "";
            auto type = prog_.types.find(base);
            if (type == prog_.types.end() || !type->second) return "";
            for (const auto& field : type->second->fields) {
                if (field.name == e->text) return field.typeName;
            }
        }
        return "";
    }

    void propagateTypes(const std::vector<StmtP>& body) {
        for (const auto& stmt : body) {
            if (stmt->kind == SK::Assign && stmt->e1 && stmt->e1->kind == EK::Var) {
                declareType(stmt->e1->text, typeOf(stmt->e2.get()));
            }
            propagateTypes(stmt->body);
            propagateTypes(stmt->body2);
            propagateTypes(stmt->finallyBody);
            for (const auto& clause : stmt->catches) propagateTypes(clause.body);
            for (const auto& block : stmt->cases) propagateTypes(block.body);
        }
    }

    bool knownCallable(const std::string& name) const {
        const std::string base = withoutSuffix(name);
        if (upperWord(name) == "IIF") return true;
        if (funcs_.count(name) || funcs_.count(base) || funcs_.count(upperWord(name))) return true;
        // Ein Name, der als Variable vorkommt, kann eine FUNCTION halten
        // (`aktion = @doppelt`) und wird ueber den Namen aufgerufen.
        if (defined_.count(name) || defined_.count(base)) return true;
        // In einer METHOD sind auch die eigenen Methoden ohne Objekt aufrufbar.
        if (!currentOwner_.empty()) {
            auto type = prog_.types.find(currentOwner_);
            if (type != prog_.types.end() && type->second && type->second->hasMethod(base)) return true;
        }
        return builtins().count(upperWord(name)) > 0 || builtins().count(upperWord(base)) > 0;
    }

    /** Kleinst- und Groesstzahl der Argumente einer eigenen Funktion. */
    static std::pair<int, int> arityOf(const FuncDef& def) {
        int minimum = 0;
        for (size_t i = 0; i < def.params.size(); ++i) {
            if (!def.params[i].def) minimum = static_cast<int>(i) + 1;
        }
        return {minimum, static_cast<int>(def.params.size())};
    }

    static std::string expectedCount(int minimum, int maximum) {
        if (maximum < 0) return "at least " + std::to_string(minimum);
        if (minimum == maximum) return std::to_string(minimum);
        return std::to_string(minimum) + " to " + std::to_string(maximum);
    }

    void checkArity(const std::string& name, size_t count, const SrcPos& pos,
                    int minimum, int maximum) {
        const int actual = static_cast<int>(count);
        if (actual >= minimum && (maximum < 0 || actual <= maximum)) return;
        issue(pos, "Wrong number of arguments for " + name + " (expected " +
                      expectedCount(minimum, maximum) + ", got " + std::to_string(actual) + ")");
    }

    /** Eigene FUNCTION/SUB, aber keine METHOD (die hat einen TYPE.PREFIX). */
    const FuncDef* userFunction(const std::string& name) const {
        auto it = prog_.funcs.find(name);
        if (it != prog_.funcs.end() && !it->second.isMethod()) return &it->second;
        const std::string base = withoutSuffix(name);
        if (base != name) {
            it = prog_.funcs.find(base);
            if (it != prog_.funcs.end() && !it->second.isMethod()) return &it->second;
        }
        return nullptr;
    }

    /** METHOD in der TYPE-Kette, inklusive geerbter METHODen. */
    const FuncDef* methodDef(const TypeDef* type, const std::string& name) const {
        auto lookup = [&](const std::string& methodName) -> const FuncDef* {
            for (const TypeDef* t = type; t; t = t->base) {
                auto it = prog_.funcs.find(t->name + "." + methodName);
                if (it != prog_.funcs.end() && it->second.isMethod()) return &it->second;
            }
            return nullptr;
        };
        if (const FuncDef* hit = lookup(name)) return hit;
        const std::string base = withoutSuffix(name);
        return base == name ? nullptr : lookup(base);
    }

    /** Prueft einen Aufruf, sofern die Argumentzahl statisch bekannt ist. */
    void checkCallArity(const std::string& name, size_t count, const SrcPos& pos) {
        const std::string upper = upperWord(name);
        // IIF ist kein normaler Befehl: es wertet genau drei Argumente aus.
        if (upper == "IIF") {
            checkArity(name, count, pos, 3, 3);
            return;
        }
        if (const FuncDef* fn = userFunction(name)) {
            const auto range = arityOf(*fn);
            checkArity(name, count, pos, range.first, range.second);
            return;
        }
        // In einer METHOD sind die eigenen Methoden ohne Objekt aufrufbar.
        if (!currentOwner_.empty()) {
            auto type = prog_.types.find(currentOwner_);
            if (type != prog_.types.end() && type->second) {
                if (const FuncDef* method = methodDef(type->second.get(), name)) {
                    const auto range = arityOf(*method);
                    checkArity(name, count, pos, range.first, range.second);
                    return;
                }
            }
        }
        auto bit = builtins().find(upper);
        if (bit == builtins().end()) bit = builtins().find(upperWord(withoutSuffix(name)));
        if (bit != builtins().end()) {
            checkArity(name, count, pos, bit->second.minArgs, bit->second.maxArgs);
        }
    }

    void issue(const SrcPos& pos, const std::string& message) {
        const std::string key = pos.file + "|" + std::to_string(pos.line) + "|" + message;
        if (!seen_.insert(key).second) return;
        issues_.push_back(LintIssue{pos, message});
    }

    /** Linke Seite einer Zuweisung: Variable selbst oder Basis von Feld/Index. */
    void collectTarget(const Expr* e) {
        if (!e) return;
        if (e->kind == EK::Var) {
            defined_.insert(e->text);
            return;
        }
        collectTarget(e->a.get());
    }

    void collectExpr(const Expr* e) {
        if (!e) return;
        collectExpr(e->a.get());
        collectExpr(e->b.get());
        for (const auto& arg : e->args) collectExpr(arg.get());
        if (e->kind == EK::Call && outputCommands().count(upperWord(withoutSuffix(e->text)))) {
            for (const auto& arg : e->args) {
                if (arg && arg->kind == EK::Var) defined_.insert(arg->text);
            }
        }
    }

    void collectBlock(const std::vector<StmtP>& body) {
        for (const auto& stmt : body) collectStmt(*stmt);
    }

    void collectStmt(const Stmt& s) {
        switch (s.kind) {
            case SK::Assign:
                collectTarget(s.e1.get());
                break;
            case SK::Dim:
            case SK::Redim:
            case SK::Decl:
            case SK::ConstDecl:
            case SK::For:
            case SK::Foreach:
                defined_.insert(s.name);
                declareType(s.name, s.typeName);
                break;
            case SK::IncDec:
            case SK::Push:
            case SK::DimDel:
                collectTarget(s.e1.get());
                break;
            case SK::Swap:
                collectTarget(s.e1.get());
                collectTarget(s.e2.get());
                break;
            case SK::Try:
                for (const auto& clause : s.catches) defined_.insert(clause.name);
                break;
            case SK::Label:
                labels_.insert(s.name);
                break;
            case SK::Call:
                if (outputCommands().count(upperWord(withoutSuffix(s.name)))) {
                    for (const auto& arg : s.exprs) {
                        if (arg && arg->kind == EK::Var) defined_.insert(arg->text);
                    }
                }
                break;
            default:
                break;
        }
        collectExpr(s.e1.get());
        collectExpr(s.e2.get());
        collectExpr(s.e3.get());
        for (const auto& arg : s.exprs) collectExpr(arg.get());
        collectBlock(s.body);
        collectBlock(s.body2);
        for (const auto& clause : s.catches) {
            collectExpr(clause.guard.get());
            collectBlock(clause.body);
        }
        collectBlock(s.finallyBody);
        for (const auto& block : s.cases) {
            for (const auto& test : block.tests) {
                collectExpr(test.lo.get());
                collectExpr(test.hi.get());
            }
            collectBlock(block.body);
        }
    }

    void checkBlock(const std::vector<StmtP>& body) {
        for (const auto& stmt : body) checkStmt(*stmt);
    }

    void checkStmt(const Stmt& s) {
        switch (s.kind) {
            case SK::Call:
                if (!knownCallable(s.name)) {
                    issue(s.pos, "Unknown command: " + withoutSuffix(s.name));
                } else {
                    checkCallArity(s.name, s.exprs.size(), s.pos);
                }
                break;
            case SK::Goto:
                if (!labels_.count(s.name)) issue(s.pos, "Unknown label: " + s.name);
                break;
            case SK::Gosub:
                // GOSUB springt in ein Unterprogramm (SUB), nicht auf eine Marke.
                if (!funcs_.count(s.name) && !funcs_.count(withoutSuffix(s.name))) {
                    issue(s.pos, "Unknown subroutine: " + s.name);
                }
                break;
            default:
                break;
        }
        checkExpr(s.e1.get());
        checkExpr(s.e2.get());
        checkExpr(s.e3.get());
        for (const auto& arg : s.exprs) checkExpr(arg.get());
        checkBlock(s.body);
        checkBlock(s.body2);
        for (const auto& clause : s.catches) {
            checkExpr(clause.guard.get());
            checkBlock(clause.body);
        }
        checkBlock(s.finallyBody);
        for (const auto& block : s.cases) {
            for (const auto& test : block.tests) {
                checkExpr(test.lo.get());
                checkExpr(test.hi.get());
            }
            checkBlock(block.body);
        }
    }

    /** Prueft, ob der TYPE der Basis diese Methode kennt (`p.flaeche()`). */
    void checkMethodCall(const Expr* e) {
        const std::string base = typeOf(e->a.get());
        if (base.empty()) return;
        auto type = prog_.types.find(base);
        if (type == prog_.types.end() || !type->second) return;
        if (const FuncDef* method = methodDef(type->second.get(), e->text)) {
            const auto range = arityOf(*method);
            checkArity(e->text, e->args.size(), e->pos, range.first, range.second);
            return;
        }
        const std::string hint = closestName(type->second->methods, e->text);
        issue(e->pos, "Unknown method: " + e->text + " (" + base + ")" +
                          (hint.empty() ? "" : " - did you mean " + hint + "?"));
    }

    /** Prueft, ob der TYPE der Basis ein solches Feld kennt (`p.nam$`). */
    void checkField(const Expr* e) {
        const std::string base = typeOf(e->a.get());
        if (base.empty()) return;
        auto type = prog_.types.find(base);
        if (type == prog_.types.end() || !type->second) return;
        std::vector<std::string> names;
        for (const auto& field : type->second->fields) {
            if (field.name == e->text) return;
            names.push_back(field.name);
        }
        const std::string hint = closestName(names, e->text);
        issue(e->pos, "Unknown field: " + e->text + " (" + base + ")" +
                          (hint.empty() ? "" : " - did you mean " + hint + "?"));
    }

    void checkExpr(const Expr* e) {
        if (!e) return;
        if (e->kind == EK::Var) {
            if (!(upperWord(e->text) == "SELF" && !currentOwner_.empty()) &&
                !defined_.count(e->text) && !constants_.count(e->text) &&
                !constants_.count(upperWord(e->text))) {
                // Ein ENUM-Wert heisst immer PHASE.STOP - der kurze Name allein
                // ist genau der Fehler, den dieser Hinweis erklaert.
                const auto value = prog_.enumValues.find(e->text);
                std::string hint;
                if (value != prog_.enumValues.end() && !value->second.empty()) {
                    hint = " - did you mean " + value->second.front() + "." + e->text + "?";
                }
                issue(e->pos, "Unknown name: " + e->text + hint);
            }
        } else if (e->kind == EK::Call) {
            if (!knownCallable(e->text)) {
                issue(e->pos, "Unknown function: " + withoutSuffix(e->text));
            } else {
                checkCallArity(e->text, e->args.size(), e->pos);
            }
        } else if (e->kind == EK::FuncRef) {
            if (!funcs_.count(e->text) && !funcs_.count(withoutSuffix(e->text)))
                issue(e->pos, "Unknown function: " + withoutSuffix(e->text));
        } else if (e->kind == EK::Field) {
            checkField(e);
        } else if (e->kind == EK::MethodCall) {
            checkMethodCall(e);
        }
        checkExpr(e->a.get());
        checkExpr(e->b.get());
        for (const auto& arg : e->args) checkExpr(arg.get());
    }
};

}  // namespace

std::vector<LintIssue> lintProgram(const Program& prog) { return Linter(prog).run(); }
