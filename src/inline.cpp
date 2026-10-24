// GameBasiX - INLINE-Bloecke
//
// Der Interpreter uebersetzt die C++-Bloecke beim Start mit dem Systemcompiler
// (g++ aus MSYS2 bzw. dem PATH) in eine DLL/SO und laedt sie. Die Bloecke
// melden ihre Funktionen ueber GBX_INLINE_FN(...) an (inline/gbx_inline.h);
// danach sind sie ganz normale Befehle.
//
// Uebersetzt wird nur, wenn sich der Quelltext aendert: Der Name der Datei ist
// der FNV-Hash des erzeugten C++-Textes, sie liegt im Temp-Ordner.
#include "inline.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include "gbx_inline.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

// Im Browser gibt es weder Compiler noch dlopen. Fuer den HTML5-Export
// uebersetzt die IDE die Bloecke vorher mit Emscripten und linkt sie fest
// dazu (-DGBX_INLINE_STATIC); dann ruft der Interpreter sie direkt auf.
#if defined(__EMSCRIPTEN__) && defined(GBX_INLINE_STATIC)
#define GBX_INLINE_LINKED 1
extern "C" void gbx_inline_static_register(GbxHost* host);
extern "C" int gbx_inline_static_blocks();
extern "C" void gbx_inline_static_step(int index, GbxStep* host);
#endif

namespace {

struct InlineFn {
    GbxInlineFn fn = nullptr;
    int minArgs = 0;
    int maxArgs = -1;
};

std::unordered_map<std::string, InlineFn> g_functions;
std::string g_returnText;  // Ziel fuer set_string, gueltig bis zum Rueckgabewert
// Der Block merkt sich den Zeiger auf diesen Host: er muss so lange leben wie
// das Programm, deshalb statisch und nicht als lokale Kopie.
GbxHost g_host;
bool g_hostReady = false;

std::string upperAscii(std::string text) {
    for (char& c : text)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return text;
}

/** Namen ohne Typ-Suffix ($ % #): GBXARRAY_JOIN$(…) findet GBXARRAY_JOIN. */
std::string withoutSuffix(const std::string& name) {
    if (!name.empty() && (name.back() == '$' || name.back() == '%' || name.back() == '#')) {
        return name.substr(0, name.size() - 1);
    }
    return name;
}

// ------------------------------------------------------------ Host-Aufrufe

void hostRegister(GbxHost*, const char* name, int minArgs, int maxArgs, GbxInlineFn fn) {
    if (!name || !*name || !fn) return;
    // Typ-Suffixe ($ % #) gehoeren zum Aufruf, nicht zum Namen: Wer
    // GBX_INLINE_FN("ALS$", …) anmeldet, ist als ALS und ALS$ erreichbar.
    g_functions[upperAscii(withoutSuffix(name))] = InlineFn{fn, minArgs, maxArgs};
}

void hostSetString(GbxHost*, GbxValue* ret, const char* text) {
    if (!ret) return;
    g_returnText = text ? text : "";
    ret->type = 1;
    ret->text = g_returnText.c_str();
}

void hostLog(GbxHost*, const char* text) {
    if (!text) return;
    std::printf("%s\n", text);
    std::fflush(stdout);
}

GbxHost makeHost() {
    GbxHost host;
    host.version = GBX_INLINE_VERSION;
    host.register_function = hostRegister;
    host.set_string = hostSetString;
    host.log = hostLog;
    host.user = nullptr;
    return host;
}

// ------------------------------------------------ Schritte: Host-Aufrufe

// Der Interpreter setzt den Bereich fuer die Dauer eines Schritts.
gbxinline_runtime::StepScope* g_stepScope = nullptr;
GbxStep g_stepHost{};
bool g_stepHostReady = false;

Value* asValue(void* raw) { return static_cast<Value*>(raw); }

void stepFail(void*, const char* message) {
    if (g_stepScope) g_stepScope->fail(message ? message : "Fehler im INLINE-Block");
}

void* stepVariable(void*, const char* name) {
    if (!g_stepScope || !name) return nullptr;
    return g_stepScope->var(name);
}

void* stepField(void* value, const char* name) {
    Value* v = asValue(value);
    if (!v || !v->isRecord() || !v->rec || !v->rec->def || !name) return nullptr;
    const int index = v->rec->def->fieldIndex(name);
    if (index < 0) {
        stepFail(nullptr, ("TYPE " + v->rec->def->name + " hat kein Feld " + name).c_str());
        return nullptr;
    }
    return &v->rec->fields[static_cast<size_t>(index)];
}

void* stepItem(void* value, int index) {
    Value* v = asValue(value);
    if (!v || !v->isArray() || !v->arr) {
        stepFail(nullptr, "Array erwartet");
        return nullptr;
    }
    int64_t at = index;
    const int64_t size = static_cast<int64_t>(v->arr->items.size());
    if (at < 0) at += size;
    if (at < 0 || at >= size) {
        stepFail(nullptr, ("Array-Index " + std::to_string(index) + " ausserhalb von 0.." +
                   std::to_string(size - 1))
                      .c_str());
        return nullptr;
    }
    return &v->arr->items[static_cast<size_t>(at)];
}

int stepCount(void* value) {
    Value* v = asValue(value);
    return v && v->isArray() && v->arr ? static_cast<int>(v->arr->items.size()) : 0;
}

int stepValueType(void* value) {
    Value* v = asValue(value);
    if (!v) return -1;
    switch (v->type) {
        case VT::Int:
        case VT::Float: return 0;
        case VT::Str: return 1;
        case VT::Array: return 2;
        case VT::Record: return 3;
        default: return -1;
    }
}

/** Zahl lesen: wie BASIC, ein Text mit Zahl wird zur Zahl. */
double stepGetNum(void* value) {
    Value* v = asValue(value);
    return v ? v->asDouble() : 0.0;
}

/** Zahl schreiben: ganze Werte bleiben INTEGER, sonst FLOAT. */
void stepSetNum(void* value, double number) {
    Value* v = asValue(value);
    if (!v) return;
    const bool integral = std::isfinite(number) && number == std::floor(number) &&
                          std::fabs(number) < 9.0e18;
    if (v->type == VT::Int && integral) {
        *v = Value::mkInt(static_cast<int64_t>(number));
    } else if (v->type == VT::Str) {
        *v = Value::mkStr(formatNumber(number));
    } else {
        *v = Value::mkFloat(number);
    }
}

const char* stepGetText(void* value) {
    Value* v = asValue(value);
    return v && v->isStr() ? v->s.c_str() : nullptr;
}

void stepSetText(void* value, const char* text) {
    Value* v = asValue(value);
    if (v) *v = Value::mkStr(text ? text : "");
}

/** GbxElem in einen BASIC-Wert uebersetzen; false, wenn er leer ist. */
bool elementValue(const GbxElem* item, Value& out) {
    if (!item) return false;
    if (item->ref) {
        out = *asValue(item->ref);
        return true;
    }
    if (item->type == 1) {
        out = Value::mkStr(item->text ? item->text : "");
        return true;
    }
    out = Value::mkFloat(item->num);
    return true;
}

int stepArrayRank(void* value) {
    return g_stepScope ? g_stepScope->arrayRank(asValue(value)) : 0;
}

int stepArraySize(void* value, int dim) {
    return g_stepScope ? g_stepScope->arraySize(asValue(value), dim) : 0;
}

int stepArrayResize(void* value, const int* sizes, int dims) {
    if (!g_stepScope) return 0;
    if (!sizes || dims <= 0 || dims > 4) {
        g_stepScope->fail("resize: 1 bis 4 Dimensionen");
        return 0;
    }
    std::vector<int> list(sizes, sizes + dims);
    return g_stepScope->arrayResize(asValue(value), list) ? 1 : 0;
}

int stepArrayPush(void* value, const GbxElem* item) {
    if (!g_stepScope) return 0;
    Value converted;
    const Value* source = nullptr;
    if (item) {
        if (!elementValue(item, converted)) {
            g_stepScope->fail("push: Wert fehlt");
            return 0;
        }
        source = &converted;
    }
    return g_stepScope->arrayPush(asValue(value), source) ? 1 : 0;
}

int stepArrayInsert(void* value, int index, const GbxElem* item) {
    if (!g_stepScope) return 0;
    Value converted;
    const Value* source = nullptr;
    if (item) {
        if (!elementValue(item, converted)) {
            g_stepScope->fail("insert: Wert fehlt");
            return 0;
        }
        source = &converted;
    }
    return g_stepScope->arrayInsert(asValue(value), index, source) ? 1 : 0;
}

int stepArrayRemove(void* value, int index) {
    return g_stepScope && g_stepScope->arrayRemove(asValue(value), index) ? 1 : 0;
}

int stepArrayClear(void* value) {
    return g_stepScope && g_stepScope->arrayClear(asValue(value)) ? 1 : 0;
}

/** Text einer Befehlsrueckgabe; gilt bis zum naechsten Aufruf. */
std::string g_basicResultText;

int stepBasicCall(void* /*scope*/, const char* name, const GbxElem* args, int argc, GbxValue* ret) {
    if (ret) {
        ret->type = 0;
        ret->num = 0.0;
        ret->text = nullptr;
    }
    if (!g_stepScope) return 0;
    if (!name || !*name) {
        g_stepScope->fail("GBX-Befehl ohne Namen");
        return 0;
    }

    std::vector<Value> values;
    std::vector<Value*> refs;
    if (argc > 0) {
        values.reserve(static_cast<size_t>(argc));
        refs.reserve(static_cast<size_t>(argc));
    }
    for (int i = 0; i < argc; ++i) {
        const GbxElem& entry = args[i];
        if (entry.ref) {
            Value* value = asValue(entry.ref);
            refs.push_back(value);
            values.push_back(*value);
        } else {
            refs.push_back(nullptr);
            values.push_back(entry.type == 1 ? Value::mkStr(entry.text ? entry.text : "")
                                             : Value::mkFloat(entry.num));
        }
    }

    Value result;
    if (!g_stepScope->basicCall(name, values, refs, result)) return 0;
    if (ret) {
        if (result.isStr()) {
            g_basicResultText = result.s;
            ret->type = 1;
            ret->text = g_basicResultText.c_str();
        } else {
            ret->type = 0;
            ret->num = result.asDouble();
        }
    }
    return 1;
}

void stepLog(const char* text) {
    if (text) hostLog(nullptr, text);
}

GbxStep makeStepHost() {
    GbxStep host{};
    host.version = GBX_INLINE_VERSION;
    host.scope = nullptr;
    host.variable = stepVariable;
    host.field = stepField;
    host.item = stepItem;
    host.count = stepCount;
    host.value_type = stepValueType;
    host.get_num = stepGetNum;
    host.set_num = stepSetNum;
    host.get_text = stepGetText;
    host.set_text = stepSetText;
    host.log = stepLog;
    host.fail = stepFail;
    host.rank = stepArrayRank;
    host.size_dim = stepArraySize;
    host.resize = stepArrayResize;
    host.push = stepArrayPush;
    host.insert = stepArrayInsert;
    host.remove = stepArrayRemove;
    host.clear = stepArrayClear;
    host.basic_call = stepBasicCall;
    return host;
}

// --------------------------------------------------------------- Werkzeug

uint64_t fnv1a(const std::string& text) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hexHash(uint64_t value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buf);
}

fs::path executableDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return fs::current_path();
    return fs::path(std::string(buf, len)).parent_path();
#else
    char buf[4096];
    const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return fs::current_path();
    buf[len] = '\0';
    return fs::path(buf).parent_path();
#endif
}

/** Ordner mit gbx_inline.h: neben gbx.exe, eins hoeher (build/), im Projekt. */
std::string inlineIncludeDir() {
    std::vector<fs::path> candidates;
    if (const char* env = std::getenv("GBX_INLINE_INCLUDE")) candidates.emplace_back(env);
    const fs::path exeDir = executableDir();
    const fs::path cwd = fs::current_path();
    candidates.push_back(exeDir / "inline");
    candidates.push_back(exeDir / ".." / "inline");
    candidates.push_back(cwd / "inline");
    candidates.push_back(cwd / ".." / "inline");
    std::error_code ec;
    for (const auto& dir : candidates) {
        if (fs::exists(dir / "gbx_inline.h", ec)) {
            return fs::absolute(dir, ec).lexically_normal().string();
        }
    }
    return std::string();
}

std::string quote(const std::string& text) { return "\"" + text + "\""; }

/** Ein Programm starten und seine Ausgabe (stdout+stderr) einsammeln. */
bool runTool(const std::string& program, const std::string& args, std::string& output) {
    output.clear();
#ifdef _WIN32
    // Getrennte Haende statt cmd.exe: Pfade mit Leerzeichen bleiben so heil.
    std::error_code ec;
    const fs::path log = fs::temp_directory_path(ec) /
                         ("gbx-inline-" + std::to_string(GetCurrentProcessId()) + "-" +
                          hexHash(fnv1a(program + args)) + ".log");
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE file = CreateFileA(log.string().c_str(), GENERIC_WRITE | GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    STARTUPINFOA start{};
    start.cb = sizeof(start);
    start.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    start.wShowWindow = SW_HIDE;
    start.hStdOutput = file;
    start.hStdError = file;
    start.hStdInput = nullptr;
    PROCESS_INFORMATION process{};
    std::string commandLine = "\"" + program + "\" " + args;
    std::vector<char> mutableLine(commandLine.begin(), commandLine.end());
    mutableLine.push_back('\0');
    const BOOL started = CreateProcessA(nullptr, mutableLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &start, &process);
    if (!started) {
        CloseHandle(file);
        DeleteFileA(log.string().c_str());
        return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    SetFilePointer(file, 0, nullptr, FILE_BEGIN);
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &got, nullptr) && got > 0)
        output.append(buffer, got);
    CloseHandle(file);
    DeleteFileA(log.string().c_str());
    return code == 0;
#else
    std::string command = quote(program) + " " + args + " 2>&1";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return false;
    char buffer[4096];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) output.append(buffer, got);
    return pclose(pipe) == 0;
#endif
}

/** g++ finden: GBX_CXX, PATH, typische MSYS2-Ordner. */
std::string findCompiler() {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("GBX_CXX")) candidates.emplace_back(env);
#ifdef _WIN32
    // Ein mitgelieferter Compiler liegt neben gbx.exe: build\gbx.exe ->
    // ..\compiler\bin\g++.exe, eine flache Ablage auch unter .\compiler.
    const fs::path exeDir = executableDir();
    candidates.push_back((exeDir / ".." / "compiler" / "bin" / "g++.exe").string());
    candidates.push_back((exeDir / "compiler" / "bin" / "g++.exe").string());
#endif
    candidates.emplace_back("g++");
    candidates.emplace_back("c++");
    candidates.emplace_back("clang++");
#ifdef _WIN32
    candidates.emplace_back("C:/msys64/ucrt64/bin/g++.exe");
    candidates.emplace_back("C:/msys64/mingw64/bin/g++.exe");
    candidates.emplace_back("C:/mingw64/bin/g++.exe");
#endif
    for (const auto& candidate : candidates) {
        std::string out;
        if (runTool(candidate, "--version", out)) return candidate;
    }
    return std::string();
}

/** Dateiname fuer eine #line-Zeile entschaerfen. */
std::string lineFileName(const std::string& path) {
    std::string out;
    out.reserve(path.size() + 8);
    for (char c : path) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// ------------------------------------------- Schritt-Bloecke: BASIC-Bindungen

/** Wie eine BASIC-Variable im C++-Code eines Schritts aussieht. */
enum class BindKind { Number, Text, Array, Record, RecordArray };

struct Binding {
    BindKind kind = BindKind::Number;
    std::string type;   // TYPE-Name bei Record/RecordArray
    std::string basic;  // Name im Interpreter (GROSS, ohne Typ-Suffix)
    std::string cpp;    // C++-Name (klein, Text mit _str)
    bool readOnly = false;
};

using BindMap = std::map<std::string, Binding>;

int kindStrength(BindKind kind) {
    switch (kind) {
        case BindKind::Record:
        case BindKind::RecordArray: return 3;
        case BindKind::Array: return 2;
        case BindKind::Text: return 1;
        case BindKind::Number: return 0;
    }
    return 0;
}

void putBinding(BindMap& map, const std::string& basic, BindKind kind, const std::string& type,
                bool readOnly) {
    if (basic.empty()) return;
    auto it = map.find(basic);
    if (it == map.end()) {
        Binding binding;
        binding.kind = kind;
        binding.type = type;
        binding.basic = basic;
        binding.readOnly = readOnly;
        map.emplace(basic, std::move(binding));
        return;
    }
    // Eine spaetere, genauere Deklaration gewinnt (DIM/TYPE vor Zuweisung).
    if (kindStrength(kind) >= kindStrength(it->second.kind)) {
        it->second.kind = kind;
        it->second.type = type;
    }
    it->second.readOnly = it->second.readOnly || readOnly;
}

void putAll(BindMap& target, const BindMap& source) {
    for (const auto& entry : source) target[entry.first] = entry.second;
}

BindKind declKind(const Stmt& st) {
    if (!st.typeName.empty()) return BindKind::Record;
    return st.hint == TypeHint::Str ? BindKind::Text : BindKind::Number;
}

void walkChildBlocks(const Stmt& st, void (*visit)(const std::vector<StmtP>&, BindMap&),
                     BindMap& out) {
    visit(st.body, out);
    visit(st.body2, out);
    for (const auto& branch : st.cases) visit(branch.body, out);
}

/** Im Hauptprogramm: GLOBAL-Deklarationen, Konstanten und alles, was durch
 *  Zuweisung, DIM oder FOR entsteht - das landet zur Laufzeit in den Globals. */
void walkMainGlobals(const std::vector<StmtP>& body, BindMap& out) {
    for (const auto& st : body) {
        if (!st) continue;
        switch (st->kind) {
            case SK::Decl:
                if (st->scope == DeclScope::Global)
                    putBinding(out, st->name, declKind(*st), st->typeName, false);
                break;
            case SK::ConstDecl:
                putBinding(out, st->name, st->hint == TypeHint::Str ? BindKind::Text : BindKind::Number,
                           std::string(), true);
                break;
            case SK::Dim:
            case SK::Redim:
                putBinding(out, st->name,
                           st->typeName.empty() ? BindKind::Array : BindKind::RecordArray,
                           st->typeName, false);
                break;
            case SK::Assign:
                if (st->e1 && st->e1->kind == EK::Var)
                    putBinding(out, st->e1->text,
                               st->e1->hint == TypeHint::Str ? BindKind::Text : BindKind::Number,
                               std::string(), false);
                break;
            case SK::For:
            case SK::IncDec:
                if (!st->name.empty())
                    putBinding(out, st->name, BindKind::Number, std::string(), false);
                break;
            default: break;
        }
        walkChildBlocks(*st, walkMainGlobals, out);
    }
}

/** LOCAL/STATIC im Hauptprogramm: die Laufzeit legt sie im Hauptbereich an. */
void walkMainLocals(const std::vector<StmtP>& body, BindMap& out) {
    for (const auto& st : body) {
        if (!st) continue;
        if (st->kind == SK::Decl && st->scope != DeclScope::Global)
            putBinding(out, st->name, declKind(*st), st->typeName, false);
        walkChildBlocks(*st, walkMainLocals, out);
    }
}

/** GLOBAL/Konstanten, die in Funktionen stehen, gelten ueberall. */
void walkGlobalDecls(const std::vector<StmtP>& body, BindMap& out) {
    for (const auto& st : body) {
        if (!st) continue;
        if (st->kind == SK::Decl && st->scope == DeclScope::Global) {
            putBinding(out, st->name, declKind(*st), st->typeName, false);
        } else if (st->kind == SK::ConstDecl) {
            putBinding(out, st->name, st->hint == TypeHint::Str ? BindKind::Text : BindKind::Number,
                       std::string(), true);
        }
        walkChildBlocks(*st, walkGlobalDecls, out);
    }
}

/** Lokale einer FUNCTION/SUB/METHOD: Parameter, Deklarationen, Zuweisungen. */
void walkFunctionLocals(const std::vector<StmtP>& body, BindMap& out) {
    for (const auto& st : body) {
        if (!st) continue;
        switch (st->kind) {
            case SK::Decl:
                if (st->scope != DeclScope::Global)
                    putBinding(out, st->name, declKind(*st), st->typeName, false);
                break;
            case SK::ConstDecl:
                putBinding(out, st->name, st->hint == TypeHint::Str ? BindKind::Text : BindKind::Number,
                           std::string(), true);
                break;
            case SK::Dim:
            case SK::Redim:
                putBinding(out, st->name,
                           st->typeName.empty() ? BindKind::Array : BindKind::RecordArray,
                           st->typeName, false);
                break;
            case SK::Assign:
                if (st->e1 && st->e1->kind == EK::Var)
                    putBinding(out, st->e1->text,
                               st->e1->hint == TypeHint::Str ? BindKind::Text : BindKind::Number,
                               std::string(), false);
                break;
            case SK::For:
            case SK::IncDec:
                if (!st->name.empty())
                    putBinding(out, st->name, BindKind::Number, std::string(), false);
                break;
            default: break;
        }
        walkChildBlocks(*st, walkFunctionLocals, out);
    }
}

std::string lowerAscii(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

bool isCppKeyword(const std::string& name) {
    static const std::set<std::string> words = {
        "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch",
        "char", "class", "const", "constexpr", "continue", "default", "delete", "do",
        "double", "else", "enum", "explicit", "export", "extern", "false", "float", "for",
        "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new",
        "noexcept", "not", "nullptr", "operator", "or", "private", "protected", "public",
        "register", "return", "short", "signed", "sizeof", "static", "struct", "switch",
        "template", "this", "throw", "true", "try", "typedef", "typename", "union",
        "unsigned", "using", "virtual", "void", "volatile", "while", "xor", "std"};
    return words.count(name) != 0;
}

std::string safeName(const std::string& name, const char* suffix) {
    std::string out = name;
    if (isCppKeyword(out)) out += "_" + std::string(suffix);
    return out;
}

/** Alle Variablen, die an dieser Stelle im C++-Block sichtbar sind. */
std::vector<Binding> bindingsFor(const Program& prog, const InlineBlock& block) {
    BindMap globals;
    walkMainGlobals(prog.main, globals);
    for (const auto& entry : prog.funcs) walkGlobalDecls(entry.second.body, globals);

    BindMap visible = globals;
    if (block.scope.empty()) {
        BindMap mainLocals;
        walkMainLocals(prog.main, mainLocals);
        putAll(visible, mainLocals);
    }

    // In einer METHOD sind die Felder des eigenen TYPEs wie Variablen sichtbar.
    if (!block.scopeType.empty()) {
        auto type = prog.types.find(block.scopeType);
        if (type != prog.types.end() && type->second) {
            BindMap fields;
            for (const auto& field : type->second->fields) {
                const BindKind kind = field.type == VT::Record ? BindKind::Record
                                     : field.type == VT::Str  ? BindKind::Text
                                     : field.type == VT::Array ? BindKind::Array
                                                               : BindKind::Number;
                putBinding(fields, field.name, kind, field.typeName, false);
            }
            putAll(visible, fields);
        }
    }

    if (!block.scope.empty()) {
        auto def = prog.funcs.find(block.scope);
        if (def != prog.funcs.end()) {
            BindMap locals;
            for (const auto& param : def->second.params) {
                const BindKind kind = !param.typeName.empty() ? BindKind::Record
                                     : param.hint == TypeHint::Str ? BindKind::Text
                                                                   : BindKind::Number;
                putBinding(locals, param.name, kind, param.typeName, false);
            }
            walkFunctionLocals(def->second.body, locals);
            putAll(visible, locals);
        }
    }

    std::vector<Binding> list;
    std::set<std::string> used;
    for (auto& entry : visible) {
        Binding binding = entry.second;
        std::string cpp = lowerAscii(binding.basic);
        if (binding.kind == BindKind::Text) cpp += "_str";
        if (cpp.empty() || std::isdigit(static_cast<unsigned char>(cpp[0])) || isCppKeyword(cpp))
            continue;
        if (!used.insert(cpp).second) continue;
        binding.cpp = cpp;
        list.push_back(std::move(binding));
    }
    std::sort(list.begin(), list.end(), [](const Binding& a, const Binding& b) {
        return a.cpp < b.cpp;
    });
    return list;
}

// ------------------------------------------------------ Erzeugter C++-Text

/* Die Attribute stehen als Makro in gbx_inline.h: so laesst sich derselbe
 * erzeugte Text auch mit Emscripten fuer den HTML5-Export uebersetzen. */
const char* exportAttribute() { return "GBX_INLINE_EXPORT"; }

std::string fieldMember(const FieldDef& field) {
    std::string name = lowerAscii(field.name);
    if (field.type == VT::Str) name += "_str";
    return safeName(name, "field");
}

std::string fieldTypeCpp(const Program& prog, const FieldDef& field, const std::set<std::string>& done) {
    switch (field.type) {
        case VT::Str: return "::gbxinline::Str";
        case VT::Array: return "::gbxinline::Array";
        case VT::Record:
            if (prog.types.count(field.typeName) && done.count(field.typeName))
                return "::gbxstep::t_" + field.typeName;
            return "::gbxinline::Record";
        default: return "::gbxinline::Num";
    }
}

void emitTypeProxy(const Program& prog, const std::string& name, std::set<std::string>& done,
                   std::set<std::string>& active, std::ostringstream& out) {
    if (done.count(name) || active.count(name)) return;
    auto it = prog.types.find(name);
    if (it == prog.types.end() || !it->second) return;
    active.insert(name);
    for (const auto& field : it->second->fields) {
        if (field.type == VT::Record && !field.typeName.empty())
            emitTypeProxy(prog, field.typeName, done, active, out);
    }
    active.erase(name);

    const TypeDef& def = *it->second;
    out << "struct t_" << name << " {\n";
    out << "    void* v = nullptr;\n";
    out << "    t_" << name << "(void* value = nullptr) : v(value)";
    for (const auto& field : def.fields) {
        out << ", " << fieldMember(field) << "(::gbxinline::field(v, \"" << field.name << "\"))";
    }
    out << " {}\n";
    for (const auto& field : def.fields) {
        out << "    " << fieldTypeCpp(prog, field, done) << " " << fieldMember(field) << ";\n";
    }
    out << "    t_" << name << "& operator=(const t_" << name << "&) = delete;\n";
    out << "};\n";

    out << "struct a_" << name << " {\n";
    out << "    void* v = nullptr;\n";
    out << "    a_" << name << "(void* value = nullptr) : v(value) {}\n";
    out << "    a_" << name << "& operator=(const a_" << name << "&) = delete;\n";
    out << "    t_" << name << " operator[](int index) const { return t_" << name
        << "(::gbxinline::item(v, index)); }\n";
    out << "    int size() const { return ::gbxinline::count(v); }\n";
    out << "    int size(int dim) const { return ::gbxinline::arraySize(v, dim); }\n";
    out << "    int rank() const { return ::gbxinline::arrayRank(v); }\n";
    out << "    bool resize(int a) { return ::gbxinline::arrayResize(v, a); }\n";
    out << "    bool resize(int a, int b) { return ::gbxinline::arrayResize(v, a, b); }\n";
    out << "    bool clear() { return ::gbxinline::arrayClear(v); }\n";
    out << "    bool remove(int index) { return ::gbxinline::arrayRemove(v, index); }\n";
    out << "    bool push() { return ::gbxinline::arrayPushDefault(v); }\n";
    out << "    bool push(const t_" << name << "& item) { return ::gbxinline::arrayPushRef(v, item.v); }\n";
    out << "    bool insert(int index, const t_" << name
        << "& item) { return ::gbxinline::arrayInsertRef(v, index, item.v); }\n";
    out << "};\n";
    done.insert(name);
}

/** #include-Zeilen eines Schritts nach oben holen: im Funktionsrumpf waeren
 *  die Deklarationen des Headers nicht erlaubt. Die Zeile bleibt als
 *  Leerzeile stehen, damit die #line-Nummern weiter stimmen. */
std::string stripStepIncludes(const std::string& code, std::vector<std::string>& includes) {
    std::string out;
    size_t pos = 0;
    while (pos <= code.size()) {
        size_t end = code.find('\n', pos);
        if (end == std::string::npos) end = code.size();
        const std::string line = code.substr(pos, end - pos);
        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
            trimmed.erase(trimmed.begin());
        if (trimmed.rfind("#include", 0) == 0)
            includes.push_back(trimmed);
        else
            out += line;
        if (end == code.size()) break;
        out += '\n';
        pos = end + 1;
    }
    return out;
}

std::string bindingCppType(const Program& prog, const Binding& binding) {
    switch (binding.kind) {
        case BindKind::Text: return "::gbxinline::Str";
        case BindKind::Array: return "::gbxinline::Array";
        case BindKind::Record:
            return prog.types.count(binding.type) ? "::gbxstep::t_" + binding.type
                                                  : "::gbxinline::Record";
        case BindKind::RecordArray:
            return prog.types.count(binding.type) ? "::gbxstep::a_" + binding.type
                                                  : "::gbxinline::Array";
        default: return "::gbxinline::Num";
    }
}

/** Kennung fuer mitgelieferte Bibliotheken: nur Inhalte, keine Pfade.
 *  Ein gepacktes Spiel liegt in einem anderen Ordner als das Projekt, die
 *  erzeugte DLL ist aber dieselbe. */
std::string signatureSource(const Program& prog) {
    std::ostringstream out;
    out << "v" << GBX_INLINE_VERSION << "\n";
    for (const auto& block : prog.inlines) {
        out << block.code.size() << ":" << block.code << "|" << (block.step ? 1 : 0) << "|"
            << block.scope << "|" << block.scopeType << "\n";
        if (block.step) {
            for (const auto& binding : bindingsFor(prog, block)) {
                out << binding.basic << "," << binding.cpp << "," << static_cast<int>(binding.kind)
                    << "," << binding.type << "," << (binding.readOnly ? 1 : 0) << "\n";
            }
        }
    }
    std::vector<std::string> names;
    for (const auto& entry : prog.types) names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        out << "TYPE " << name << "\n";
        auto it = prog.types.find(name);
        if (it == prog.types.end() || !it->second) continue;
        for (const auto& field : it->second->fields)
            out << field.name << ":" << static_cast<int>(field.type) << ":" << field.typeName << "\n";
    }
    return out.str();
}

/** Erzeugter C++-Text aus allen Bloecken. */
std::string generatedSource(const Program& prog) {
    std::ostringstream out;
    out << "// Erzeugt von gbx.exe aus INLINE-Bloecken - nicht bearbeiten.\n";
    out << "#include <gbx_inline.h>\n";

    // Header aus Schritt-Bloecken gehoeren auf die Dateiebene.
    std::vector<std::string> stepIncludes;
    for (const auto& block : prog.inlines) {
        if (!block.step) continue;
        std::vector<std::string> collected;
        stripStepIncludes(block.code, collected);
        for (const auto& include : collected) stepIncludes.push_back(include);
    }
    for (const auto& include : stepIncludes) out << include << "\n";
    out << "\n";

    // Definitionen (INLINE auf oberster Ebene bzw. INLINE DEFINE).
    for (const auto& block : prog.inlines) {
        if (block.step) continue;
        out << "#line " << block.pos.line << " \"" << lineFileName(block.pos.file) << "\"\n";
        out << block.code;
        if (!block.code.empty() && block.code.back() != '\n') out << "\n";
        out << "\n";
    }
    out << "#line 1 \"<inline>\"\n";

    // C++-Ersatz fuer die TYPEs: p.x, p.name_str, punkte[0].x ...
    out << "namespace gbxstep {\n";
    std::vector<std::string> names;
    for (const auto& entry : prog.types) names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    std::set<std::string> done;
    std::set<std::string> active;
    for (const auto& name : names) emitTypeProxy(prog, name, done, active, out);
    out << "}  // namespace gbxstep\n\n";

    // Schritte: je Block eine Funktion, die an der Stelle aufgerufen wird.
    for (size_t i = 0; i < prog.inlines.size(); ++i) {
        const InlineBlock& block = prog.inlines[i];
        if (!block.step) continue;
        out << exportAttribute() << " void gbx_step_" << i << "(GbxStep* gbxstep_host) {\n";
        out << "    ::gbxinline::enterStep(gbxstep_host);\n";
        out << "    using namespace ::gbxinline;\n";
        for (const Binding& binding : bindingsFor(prog, block)) {
            out << "    " << (binding.readOnly ? "const " : "") << bindingCppType(prog, binding)
                << " " << binding.cpp << "(::gbxinline::variable(\"" << binding.basic << "\"));\n";
        }
        out << "    {\n";
        out << "#line " << block.pos.line << " \"" << lineFileName(block.pos.file) << "\"\n";
        std::vector<std::string> unusedIncludes;
        out << stripStepIncludes(block.code, unusedIncludes);
        if (!block.code.empty() && block.code.back() != '\n') out << "\n";
        out << "    }\n";
        out << "#line 1 \"<inline>\"\n";
        out << "}\n\n";
    }

    out << exportAttribute() << " void gbx_inline_entry(GbxHost* host) { ::gbxinline::registerAll(host); }\n";

    // Fester Anschluss fuer den HTML5-Export: derselbe Text wird mit dem
    // Interpreter zusammengelinkt, statt als DLL geladen zu werden.
    out << "\n#ifdef GBX_INLINE_STATIC\n";
    out << "extern \"C\" void gbx_inline_static_register(GbxHost* host) { gbx_inline_entry(host); }\n";
    out << "extern \"C\" int gbx_inline_static_blocks() { return " << prog.inlines.size() << "; }\n";
    out << "extern \"C\" void gbx_inline_static_step(int index, GbxStep* host) {\n";
    out << "    switch (index) {\n";
    for (size_t i = 0; i < prog.inlines.size(); ++i) {
        if (!prog.inlines[i].step) continue;
        out << "        case " << i << ": gbx_step_" << i << "(host); break;\n";
    }
    out << "        default: break;\n";
    out << "    }\n";
    out << "}\n";
    out << "#endif\n";
    return out.str();
}

/** Pfad der DLL: Hash aus Quelltext, Compiler und Include-Ordner. */
fs::path libraryPath(const Program& prog, const std::string& compiler, const std::string& includeDir) {
    const std::string source = generatedSource(prog);
    // "build-2": Die Windows-Bloecke werden seit dieser Fassung mit -static
    // gelinkt. Der Zusatz macht alte DLLs im Cache ungueltig.
    const std::string key = source + "|" + compiler + "|" + includeDir + "|build-2";
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / "gbx-inline";
    fs::create_directories(dir, ec);
#ifdef _WIN32
    return dir / (hexHash(fnv1a(key)) + ".dll");
#else
    return dir / (hexHash(fnv1a(key)) + ".so");
#endif
}

/** Uebersetzt den Blocktext; liefert false und die Compilermeldung. */
bool build(const Program& prog, const std::string& compiler, const std::string& includeDir,
           const fs::path& library, std::string& report) {
    std::error_code ec;
    const fs::path work = library.parent_path();
    fs::create_directories(work, ec);
    const fs::path source = fs::path(library).replace_extension(".cpp");
    {
        std::ofstream file(source, std::ios::binary);
        if (!file) {
            report = "Arbeitsdatei " + source.string() + " laesst sich nicht schreiben";
            return false;
        }
        file << generatedSource(prog);
    }

    std::string command = "-std=c++17 -O2 -shared -w";
#ifndef _WIN32
    command += " -fPIC";
#endif
#ifdef _WIN32
    // -static bindet auch libwinpthread ein. Sonst haengt jede INLINE-DLL
    // an libwinpthread-1.dll und laeuft auf einem Rechner ohne MSYS2 nicht.
    command += " -static";
#endif
    command += " -I" + quote(includeDir);
    // Header neben der Quelldatei (z.B. #include "gbxarray.h").
    std::vector<std::string> dirs;
    if (!prog.baseDir.empty()) dirs.push_back(prog.baseDir);
    for (const auto& file : prog.includedFiles) {
        const std::string dir = fs::path(file).parent_path().string();
        if (!dir.empty()) dirs.push_back(dir);
    }
    if (!dirs.empty()) {
        command += " -I" + quote(dirs.front());
        for (size_t i = 1; i < dirs.size(); ++i)
            if (dirs[i] != dirs.front()) command += " -I" + quote(dirs[i]);
    }
#ifdef _WIN32
    command += " -static-libgcc -static-libstdc++";
#endif
    command += " " + quote(source.string()) + " -o " + quote(library.string());

    std::string output;
    const bool ok = runTool(compiler, command, output);
    fs::remove(source, ec);
    if (!ok || !fs::exists(library, ec)) {
        report = output.empty() ? "Compiler meldet einen Fehler ohne Ausgabe" : output;
        return false;
    }
    return true;
}

/** Bibliothek besorgen: aus dem Cache oder frisch uebersetzen. */
fs::path ensureLibrary(const Program& prog) {
    const SrcPos where = prog.inlines.front().pos;
    const std::string includeDir = inlineIncludeDir();
    if (includeDir.empty()) {
        throw BasicError("INLINE: gbx_inline.h wurde nicht gefunden (GBX_INLINE_INCLUDE setzen)", where);
    }
    const std::string compiler = findCompiler();
    if (compiler.empty()) {
        throw BasicError("INLINE: kein C++-Compiler gefunden - g++ installieren oder GBX_CXX setzen", where);
    }

    const fs::path library = libraryPath(prog, compiler, includeDir);
    std::error_code ec;
    if (!fs::exists(library, ec)) {
        std::string report;
        if (!build(prog, compiler, includeDir, library, report)) {
            throw BasicError("INLINE: Uebersetzen fehlgeschlagen:\n" + report, where, true);
        }
    }
    return library;
}

using StepFn = void (*)(GbxStep*);
std::unordered_map<size_t, StepFn> g_steps;

/** Symbol aus der geladenen Bibliothek holen. */
void* lookupSymbol(void* handle, const char* name) {
    if (!handle) return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

/** DLL laden und die Anmeldungen abholen. Gibt false bei Fehlern. */
bool open(const fs::path& library, std::string& report, void** handleOut = nullptr) {
    void* handle = nullptr;
#ifdef _WIN32
    handle = static_cast<void*>(LoadLibraryA(library.string().c_str()));
    if (!handle) {
        report = "Bibliothek " + library.string() + " laesst sich nicht laden";
        return false;
    }
    using Entry = void (*)(GbxHost*);
    auto entry = reinterpret_cast<Entry>(lookupSymbol(handle, "gbx_inline_entry"));
#else
    handle = dlopen(library.string().c_str(), RTLD_NOW);
    if (!handle) {
        report = std::string("Bibliothek laesst sich nicht laden: ") + (dlerror() ? dlerror() : "");
        return false;
    }
    using Entry = void (*)(GbxHost*);
    auto entry = reinterpret_cast<Entry>(lookupSymbol(handle, "gbx_inline_entry"));
#endif
    if (!entry) {
        report = "Einstiegspunkt gbx_inline_entry fehlt in " + library.string();
        return false;
    }
    if (!g_hostReady) {
        g_host = makeHost();
        g_hostReady = true;
    }
    entry(&g_host);
    if (handleOut) *handleOut = handle;
    return true;
}

/** Zeiger auf die Schritt-Funktionen derselben Bibliothek einsammeln. */
void bindSteps(const Program& prog, void* handle) {
    g_steps.clear();
    for (size_t i = 0; i < prog.inlines.size(); ++i) {
        if (!prog.inlines[i].step) continue;
        const std::string symbol = "gbx_step_" + std::to_string(i);
        if (void* raw = lookupSymbol(handle, symbol.c_str()))
            g_steps[i] = reinterpret_cast<StepFn>(raw);
    }
}

std::string readTextFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

/** Pfad einer fertig uebersetzten Bibliothek im Spielpaket. */
fs::path packagedLibrary(const Program& prog) {
#ifdef _WIN32
    return fs::path(prog.baseDir) / "__inline" / "gbx_inline.dll";
#else
    return fs::path(prog.baseDir) / "__inline" / "gbx_inline.so";
#endif
}

/**
 * Bibliothek aus dem Spielpaket laden: Sie liegt neben den ausgehaengten
 * Quellen, die Kennung in gbx_inline.sig muss zum Blocktext passen. Sonst
 * wird wie gewohnt uebersetzt.
 */
bool loadPackaged(const Program& prog) {
    if (prog.baseDir.empty()) return false;
    const fs::path library = packagedLibrary(prog);
    std::error_code ec;
    if (!fs::exists(library, ec)) return false;

    std::string signature = readTextFile(packagedLibrary(prog).parent_path() / "gbx_inline.sig");
    while (!signature.empty() && (signature.back() == '\n' || signature.back() == '\r' ||
                                  signature.back() == ' '))
        signature.pop_back();

    const std::string expected = gbxinline_runtime::sourceSignature(prog);
    if (signature != expected) return false;

    std::string report;
    void* handle = nullptr;
    if (!open(library, report, &handle))
        throw BasicError("INLINE: " + report, prog.inlines.front().pos);
    bindSteps(prog, handle);
    std::fprintf(stderr, "INLINE: Bibliothek aus dem Spielpaket geladen\n");
    return true;
}

/**
 * Namen aus GBX_INLINE_FN(...) einsammeln - auch aus Headern, die der Block
 * mit #include "..." einbindet (die Bibliothek selbst steckt dort drin).
 * `depth` begrenzt die Kette der Header.
 */
void scanNames(const std::string& code, const std::vector<fs::path>& searchDirs,
               std::vector<std::string>& names, int depth) {
    size_t pos = 0;
    while ((pos = code.find("GBX_INLINE_FN", pos)) != std::string::npos) {
        const size_t open = code.find('"', pos);
        if (open == std::string::npos) break;
        const size_t close = code.find('"', open + 1);
        if (close == std::string::npos) break;
        const std::string name = upperAscii(withoutSuffix(code.substr(open + 1, close - open - 1)));
        if (!name.empty()) names.push_back(name);
        pos = close + 1;
    }

    if (depth <= 0) return;
    size_t line = 0;
    while ((line = code.find("#include", line)) != std::string::npos) {
        size_t quote = line + 8;
        while (quote < code.size() && (code[quote] == ' ' || code[quote] == '\t')) ++quote;
        if (quote < code.size() && code[quote] == '"') {
            const size_t end = code.find('"', quote + 1);
            if (end != std::string::npos) {
                const fs::path relative = code.substr(quote + 1, end - quote - 1);
                for (const auto& dir : searchDirs) {
                    std::error_code ec;
                    const fs::path candidate = dir / relative;
                    if (!fs::exists(candidate, ec)) continue;
                    scanNames(readTextFile(candidate), {candidate.parent_path()}, names, depth - 1);
                    break;
                }
            }
        }
        line += 8;
    }
}

}  // namespace

namespace gbxinline_runtime {

bool present(const Program& prog) { return !prog.inlines.empty(); }

bool hasSteps(const Program& prog) {
    for (const auto& block : prog.inlines)
        if (block.step) return true;
    return false;
}

std::vector<std::string> registeredNames(const Program& prog) {
    std::vector<std::string> names;
    for (const auto& block : prog.inlines) {
        std::vector<fs::path> dirs;
        if (!block.pos.file.empty()) dirs.push_back(fs::path(block.pos.file).parent_path());
        if (!prog.baseDir.empty()) dirs.push_back(prog.baseDir);
        scanNames(block.code, dirs, names, 4);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

void load(const Program& prog) {
    g_functions.clear();
    g_steps.clear();
    if (!present(prog)) return;

#ifdef GBX_INLINE_LINKED
    // HTML5-Export: die Bloecke stecken schon in dieser wasm-Datei.
    if (gbx_inline_static_blocks() != static_cast<int>(prog.inlines.size())) {
        throw BasicError("INLINE: der HTML5-Player passt nicht zu diesem Programm - neu exportieren",
                         prog.inlines.front().pos);
    }
    if (!g_hostReady) {
        g_host = makeHost();
        g_hostReady = true;
    }
    gbx_inline_static_register(&g_host);
    return;
#else
#if defined(__EMSCRIPTEN__)
    throw BasicError("INLINE-Bloecke lassen sich im Browser nur ueber den HTML5-Export ausfuehren",
                     prog.inlines.front().pos);
#endif
    // Ein ausgeliefertes Spiel bringt seine Bibliothek mit.
    if (loadPackaged(prog)) return;

    const fs::path library = ensureLibrary(prog);
    std::string report;
    void* handle = nullptr;
    if (!open(library, report, &handle))
        throw BasicError("INLINE: " + report, prog.inlines.front().pos);
    bindSteps(prog, handle);
#endif
}

void runStep(const Program& prog, size_t index, StepScope& scope) {
    if (index >= prog.inlines.size())
        throw BasicError("INLINE-Schritt fehlt", SrcPos{}, true);
#ifdef GBX_INLINE_LINKED
    if (!g_stepHostReady) {
        g_stepHost = makeStepHost();
        g_stepHostReady = true;
    }
    g_stepScope = &scope;
    gbx_inline_static_step(static_cast<int>(index), &g_stepHost);
    g_stepScope = nullptr;
    return;
#else
    auto step = g_steps.find(index);
    if (step == g_steps.end())
        throw BasicError("INLINE-Schritt wurde nicht uebersetzt", prog.inlines[index].pos, true);
    if (!g_stepHostReady) {
        g_stepHost = makeStepHost();
        g_stepHostReady = true;
    }
    g_stepScope = &scope;
    step->second(&g_stepHost);
    g_stepScope = nullptr;
#endif
}

std::string sourceSignature(const Program& prog) {
    return hexHash(fnv1a(signatureSource(prog)));
}

std::string compileForExport(const Program& prog) {
    if (!present(prog)) return std::string();
#ifdef GBX_INLINE_LINKED
    // Im Browser gibt es nichts zu paketieren: die Bloecke sind fest gelinkt.
    (void)prog;
    return std::string();
#else
    return ensureLibrary(prog).string();
#endif
}

std::string generatedCpp(const Program& prog) { return generatedSource(prog); }

bool has(const std::string& name) {
    return g_functions.find(upperAscii(withoutSuffix(name))) != g_functions.end();
}

Value call(const std::string& name, const std::vector<Value>& args, const SrcPos& pos) {
    auto it = g_functions.find(upperAscii(withoutSuffix(name)));
    if (it == g_functions.end()) throw BasicError("Unbekannte Funktion: " + name, pos);
    const InlineFn& fn = it->second;
    const int count = static_cast<int>(args.size());
    if (count < fn.minArgs || (fn.maxArgs >= 0 && count > fn.maxArgs)) {
        throw BasicError("Falsche Anzahl Argumente fuer " + name, pos);
    }

    std::vector<GbxValue> values(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        const Value& value = args[i];
        GbxValue& out = values[i];
        out.num = 0.0;
        out.text = nullptr;
        if (value.isNum()) {
            out.type = 0;
            out.num = value.asDouble();
        } else if (value.isStr()) {
            out.type = 1;
            out.text = value.s.c_str();
        } else {
            throw BasicError("INLINE-Funktionen koennen keine Arrays oder TYPEs empfangen", pos);
        }
    }

    GbxValue result;
    result.type = 0;
    result.num = 0.0;
    result.text = nullptr;
    g_returnText.clear();
    fn.fn(values.empty() ? nullptr : values.data(), count, &result);

    if (result.type == 1) return Value::mkStr(g_returnText);
    if (std::isfinite(result.num) && result.num == std::floor(result.num) &&
        std::fabs(result.num) < 9.0e18) {
        return Value::mkInt(static_cast<int64_t>(result.num));
    }
    return Value::mkFloat(result.num);
}

}  // namespace gbxinline_runtime
