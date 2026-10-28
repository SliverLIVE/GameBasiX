// GameBasiX - Start und Kommandozeile
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

#include "common.h"
#include "debugger.h"
#include "i18n.h"
#include "inline.h"
#include "interpreter.h"
#include "lexer.h"
#include "lint.h"
#include "package.h"
#include "parser.h"
#include "project.h"
#include "runtime.h"

namespace fs = std::filesystem;

namespace {

// Kommt aus CMakeLists.txt (project(... VERSION ...)); der Ersatz gilt fuer
// Bauwege ohne CMake, etwa den HTML5-Build.
#ifndef GBX_VERSION
#define GBX_VERSION "1.2.3"
#endif

const char* kVersion = GBX_VERSION;

// Ein exportiertes Spiel erkennt sich beim Start am angehaengten Paket.
bool g_packaged = false;
// Bei --headless/--tests nie ein Fenster aufmachen: Automatische Laeufe
// wuerden sonst an einer Meldung haengen bleiben.
bool g_headless = false;

/** Fehler melden: auf der Konsole und - bei einem fertigen Spiel, das per
 *  Doppelklick ohne Konsole startet - zusaetzlich als Meldungsfenster. */
void reportFatal(const std::string& text) {
    std::fprintf(stderr, "%s\n", text.c_str());
#ifdef _WIN32
    if (g_packaged && !g_headless && GetConsoleWindow() == nullptr) {
        MessageBoxA(nullptr, text.c_str(), "GameBasiX", MB_OK | MB_ICONERROR);
    }
#endif
}

void printHelp() {
    if (i18n::english()) {
        std::printf(
            "GameBasiX %s - BASIC for 2D games on SDL3\n\n"
            "Usage:\n"
            "  gbx <file.gbxproj>         start a project\n"
            "  gbx <file.gbx>             start a single source file\n\n"
            "Options:\n"
            "  --headless                 run without a window and sound\n"
            "  --tests                    test run (headless, assertions count)\n"
            "  --check                    parse and check only, do not run\n"
            "  --stdin                    read the source from input (for --check)\n"
            "  --syntax-only              no name check (for single included files)\n"
            "  --base <folder>            base folder for --stdin (INCLUDE, messages)\n"
            "  --name <file.gbx>          file name in messages with --stdin\n"
            "  --debug                    runtime debugger (breakpoints, stepping)\n"
            "  --export <file.exe>        pack the project into one EXE (sources, assets)\n"
            "  --inline-cpp <file>        write the C++ of the INLINE blocks (HTML5 build)\n"
            "  --lang <de|en>             language of the messages\n"
            "  --version                  print the version\n"
            "  -h, --help                 this help\n",
            kVersion);
        return;
    }
    std::printf(
        "GameBasiX %s - BASIC fuer 2D-Spiele auf SDL3\n\n"
        "Aufruf:\n"
        "  gbx <datei.gbxproj>        Projekt starten\n"
        "  gbx <datei.gbx>            einzelne Quelldatei starten\n\n"
        "Optionen:\n"
        "  --headless                 ohne Fenster und Ton ausfuehren\n"
        "  --tests                    Testlauf (headless, Asserts zaehlen)\n"
        "  --check                    nur uebersetzen/pruefen, nichts ausfuehren\n"
        "  --stdin                    Quelle aus der Eingabe lesen (fuer --check)\n"
        "  --syntax-only              keine Namenspruefung (fuer einzelne Teildateien)\n"
        "  --base <ordner>            Bezugsordner fuer --stdin (INCLUDE, Meldungen)\n"
        "  --name <datei.gbx>         Dateiname in Meldungen bei --stdin\n"
        "  --debug                    Laufzeit-Debugger (Haltepunkte, Einzelschritte)\n"            "  --export <datei.exe>       Projekt als einzelne EXE packen (Quellen, Assets)\n"
            "  --inline-cpp <datei>       C++ der INLINE-Bloecke schreiben (HTML5-Build)\n"
        "  --lang <de|en>             Sprache der Meldungen\n"
        "  --version                  Version ausgeben\n"
        "  -h, --help                 diese Hilfe\n",
        kVersion);
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw BasicError("Quelldatei nicht gefunden: " + path, SrcPos{path, 0}, true);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Quelle fuer die Live-Pruefung der IDE: sie schickt den ungespeicherten
// Editorinhalt ueber die Standardeingabe, damit nichts auf der Platte landet.
std::string readStdin() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

void registerConstants(Runtime& rt) {
    for (const auto& [name, value] : builtinConstants()) {
        Slot s;
        s.v = Value::mkInt(value);
        s.hint = TypeHint::Int;
        s.isConst = true;
        rt.globals[name] = std::move(s);
    }
}

}  // namespace

int main(int argc, char** argv) {
    // Die IDE setzt GBX_LANG passend zur gewählten Oberflächensprache;
    // --lang hat Vorrang, weil es später in der Kommandozeile steht.
    if (const char* env = std::getenv("GBX_LANG")) i18n::setLanguage(env);

    std::string path;
    bool headless = false;
    bool tests = false;
    bool checkOnly = false;
    bool debugMode = false;
    bool fromStdin = false;
    bool syntaxOnly = false;
    std::string baseDir;      // --base: Bezugsordner fuer INCLUDE
    std::string stdinName;    // --name: Dateiname in den Meldungen
    std::string exportTarget; // --export: Zieldatei des Spielpakets
    std::string inlineCppTarget; // --inline-cpp: erzeugtes C++ der INLINE-Bloecke
    bool exportMode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printHelp();
            return 0;
        }
        if (arg == "--version") {
            std::printf("GameBasiX %s (SDL3)\n", kVersion);
            return 0;
        }
        if (arg == "--lang") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s", i18n::tr("--lang braucht einen Wert.\n").c_str());
                return 2;
            }
            i18n::setLanguage(argv[++i]);
            continue;
        }
        if (arg == "--headless") {
            headless = true;
            continue;
        }
        if (arg == "--tests") {
            tests = true;
            headless = true;
            continue;
        }
        if (arg == "--check") {
            checkOnly = true;
            continue;
        }
        if (arg == "--debug") {
            debugMode = true;
            continue;
        }
        if (arg == "--export") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s", i18n::tr("--export braucht einen Wert.\n").c_str());
                return 2;
            }
            exportMode = true;
            exportTarget = argv[++i];
            continue;
        }
        if (arg == "--inline-cpp") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s", i18n::tr("--inline-cpp braucht einen Wert.\n").c_str());
                return 2;
            }
            inlineCppTarget = argv[++i];
            continue;
        }
        if (arg == "--stdin") {
            fromStdin = true;
            continue;
        }
        if (arg == "--syntax-only") {
            syntaxOnly = true;
            continue;
        }
        if (arg == "--base" || arg == "--name") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, i18n::tr("%s braucht einen Wert.\n").c_str(), arg.c_str());
                return 2;
            }
            if (arg == "--base") baseDir = argv[++i];
            else stdinName = argv[++i];
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, i18n::tr("Unbekannte Option: %s\n").c_str(), arg.c_str());
            printHelp();
            return 2;
        }
        if (path.empty()) path = arg;
        else {
            std::fprintf(stderr, i18n::tr("Nur eine Datei kann gestartet werden.\n").c_str());
            return 2;
        }
    }

    if (fromStdin && !checkOnly) {
        std::fprintf(stderr, i18n::tr("--stdin ist nur zusammen mit --check moeglich.\n").c_str());
        return 2;
    }
    if (exportMode && (checkOnly || fromStdin || debugMode || !inlineCppTarget.empty())) {
        std::fprintf(stderr, "%s", i18n::tr("--export laesst sich nicht mit --check, --stdin oder --debug verbinden.\n").c_str());
        return 2;
    }

    g_headless = headless;

    if (path.empty() && !fromStdin) {
        if (exportMode) {
            std::fprintf(stderr, "%s", i18n::tr("Fuer --export wird eine Projektdatei gebraucht.\n").c_str());
            return 2;
        }
        // Ohne Dateiargument: Vielleicht steckt ein Spielpaket in dieser EXE.
        std::string mounted;
        std::string mountError;
        bool found = false;
        if (!gbxpackage::mountSelf(mounted, found, mountError)) {
            reportFatal("Spielpaket: " + mountError);
            return 2;
        }
        if (!found) {
            printHelp();
            return 1;
        }
        g_packaged = true;
        path = mounted;
    }

    ProjectConfig cfg;
    std::string entryPath;
    try {
        if (fromStdin) {
            // Der Aufrufer nennt den Ordner, in dem die Datei liegt; INCLUDE
            // und Meldungen beziehen sich damit auf die echte Quelldatei.
            fs::path base = baseDir.empty() ? fs::current_path() : fs::path(baseDir);
            base = fs::absolute(base).lexically_normal();
            cfg.projectDir = base.string();
            cfg.entry = stdinName.empty() ? "stdin.gbx" : stdinName;
            entryPath = (base / cfg.entry).lexically_normal().string();
        } else {
            cfg = configForEntry(path);
            fs::path entry = fs::path(cfg.entry);
            if (!entry.is_absolute()) entry = fs::path(cfg.projectDir) / entry;
            entryPath = entry.lexically_normal().string();
        }
    } catch (BasicError& e) {
        std::fprintf(stderr, "%s\n", e.fullMessage().c_str());
        return 2;
    }

    // Sektion [native] der Projektdatei: fremde C++-Bibliotheken (.a/.o) fuer
    // die INLINE-Bloecke. Relative Angaben gelten ab dem Projektordner.
    {
        const auto resolveAll = [&cfg](const std::vector<std::string>& items) {
            std::vector<std::string> out;
            out.reserve(items.size());
            for (const auto& item : items) {
                fs::path where(item);
                if (!where.is_absolute()) where = fs::path(cfg.projectDir) / where;
                std::error_code ec;
                const fs::path full = fs::absolute(where, ec);
                out.push_back((ec ? where : full).lexically_normal().string());
            }
            return out;
        };
        gbxinline_runtime::setNativeOptions(resolveAll(cfg.nativeLibs), resolveAll(cfg.nativeLibDirs),
                                           cfg.nativeFlags);
    }

    // Der Debugger laeuft schon waehrend des Uebersetzens, damit auch
    // Syntaxfehler als Debug-Meldung bei der IDE ankommen.
    std::unique_ptr<Debugger> dbg;
    if (debugMode && !checkOnly) {
        dbg = std::make_unique<Debugger>();
        dbg->start();
    }

    // -------------------------------------------------------- Uebersetzen
    Program prog;
    prog.mainFile = entryPath;
    prog.baseDir = cfg.projectDir;
    prog.includedFiles.push_back(entryPath);

    try {
        std::string src = fromStdin ? readStdin() : readFile(entryPath);
        Parser parser(entryPath, lexFile(entryPath, src), prog);
        parser.parseFile();
        // Erst wenn alle Dateien gelesen sind, ist klar, welche Punkte zu
        // einem ENUM gehoeren (PHASE.STOP).
        resolveEnumRefs(prog);
    } catch (BasicError& e) {
        reportFatal(e.fullMessage());
        if (dbg) dbg->reportError(e);
        return 2;
    }

    // C++ der INLINE-Bloecke ausgeben: der HTML5-Export uebersetzt diese
    // Datei mit Emscripten und linkt sie fest zum Interpreter dazu.
    if (!inlineCppTarget.empty()) {
        std::ofstream file(inlineCppTarget, std::ios::binary);
        if (!file) {
            reportFatal(i18n::tr("Die Datei " + inlineCppTarget + " laesst sich nicht schreiben."));
            return 2;
        }
        file << gbxinline_runtime::generatedCpp(prog);
        file.close();
        if (!file) {
            reportFatal(i18n::tr("Die Datei " + inlineCppTarget + " laesst sich nicht schreiben."));
            return 2;
        }
        return 0;
    }

    // Gepackt wird nur, was auch die Namenspruefung besteht - sonst faellt der
    // Fehler erst auf dem Rechner des Spielers auf.
    if (checkOnly || exportMode) {
        // Namen pruefen: was nirgends definiert ist, ist ein Tippfehler.
        // Teildateien kennen die Definitionen der anderen Dateien nicht;
        // dort prueft die IDE nur die Syntax.
        const std::vector<LintIssue> issues =
            (syntaxOnly && !exportMode) ? std::vector<LintIssue>{} : lintProgram(prog);
        for (const auto& issue : issues) {
            std::fprintf(stderr, "%s:%d: %s\n", issue.pos.file.c_str(), issue.pos.line,
                         i18n::tr(std::string("Fehler: ") + issue.message).c_str());
        }
        if (!issues.empty()) return 2;
        if (checkOnly) {
            std::printf("%s: %s", entryPath.c_str(),
                        i18n::tr("Syntax OK (" + std::to_string(prog.main.size()) +
                                 " Hauptanweisungen, " + std::to_string(prog.funcs.size()) +
                                 " Funktionen, " + std::to_string(prog.types.size()) + " TYPEs)\n")
                            .c_str());
            return 0;
        }
    }

    if (exportMode) {
        std::string report;
        std::string error;
        if (!gbxpackage::exportProject(prog, cfg, exportTarget, report, error)) {
            reportFatal(error);
            return 2;
        }
        std::printf("%s", report.c_str());
        return 0;
    }

    // ------------------------------------------------- INLINE-Bloecke bauen
    // Nur beim Ausfuehren: --check prueft die Namen aus dem Blocktext, ohne
    // einen Compiler zu starten.
    if (gbxinline_runtime::present(prog)) {
        try {
            gbxinline_runtime::load(prog);
        } catch (BasicError& e) {
            reportFatal(e.fullMessage());
            if (dbg) dbg->reportError(e);
            return 2;
        }
    }

    // ------------------------------------------------------------ Starten
    Runtime rt;
    rt.cfg = cfg;
    rt.headless = headless;
    rt.testsMode = tests;
    rt.prog = &prog;
    rt.dbg = dbg.get();
    rt.saveDir = cfg.projectDir;
    if (g_packaged) {
        // Ein fertiges Spiel schreibt Bilder neben seine EXE, nicht in den
        // Temp-Ordner mit den ausgehaengten Quellen.
        const fs::path exe = gbxpackage::executablePath();
        if (!exe.empty()) rt.saveDir = exe.parent_path().string();
    }
    rt.gfx.headless = headless;
    rt.audio.headless = headless;
    {
        std::string cl;
        for (int i = 1; i < argc; ++i) {
            if (!cl.empty()) cl += " ";
            cl += argv[i];
        }
        rt.commandLine = cl;
    }
    registerConstants(rt);

    if (!rt.gfx.init(cfg.width, cfg.height, cfg.title, cfg.fullscreen, cfg.resizable)) {
        reportFatal(i18n::tr("Grafik konnte nicht initialisiert werden.\n"));
        return 2;
    }
    rt.gfx.setLimitFps(cfg.fps, cfg.vsync);
    if (cfg.audio && !headless) {
        rt.audio.init();
        if (cfg.musicVolume != 1.0) rt.audio.setMusicVolume(cfg.musicVolume);
    }
    rt.gfx.clearScreen(0x000000);

    int result = 0;
    bool reported = false;
    Interpreter interp(prog, rt);
    if (dbg) {
        dbg->scope = [&interp] { return interp.debugScope(); };
        dbg->condition = [&interp](const std::string& text, bool& ok, std::string& error) {
            return interp.debugEval(text, ok, error).truthy();
        };
        dbg->inspect = [&interp](const std::string& text, bool& ok, std::string& error) {
            return interp.debugInspect(text, ok, error);
        };
    }
    try {
        interp.run();
        result = rt.exitCode;
    } catch (DebugQuit&) {
        result = 0;
    } catch (BasicError& e) {
        reportFatal(e.fullMessage());
        if (dbg) {
            dbg->reportError(e);
            reported = true;
        }
        result = 2;
    } catch (std::exception& e) {
        std::fprintf(stderr, i18n::tr("Interner Fehler: %s\n").c_str(), e.what());
        result = 3;
    }
    if (dbg && !reported) dbg->reportExit(result);

    rt.audio.shutdown();
    rt.gfx.shutdown();

    if (tests) {
        std::printf("%s", i18n::tr(std::to_string(rt.assertCount) + " Pruefungen, " +
                                    std::to_string(rt.assertFailures) + " fehlgeschlagen\n")
                             .c_str());
        if (rt.assertFailures > 0 && result == 0) result = 1;
    }
    return result;
}
