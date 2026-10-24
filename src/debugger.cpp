// GameBasiX - Laufzeit-Debugger
#include "debugger.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

namespace {

// Nur eine Debugger-Sitzung je Prozess; die Ausgabe wird nicht verschraenkt.
std::mutex g_writeMutex;

const char* kPrefix = "@@gbxdbg ";

std::vector<std::string> splitTabs(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == '\t') {
            parts.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current += c;
        }
    }
    parts.push_back(current);
    return parts;
}

std::string lowerPath(std::string text) {
    for (char& c : text) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

}  // namespace

std::string debugJsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

Debugger::Debugger() = default;

Debugger::~Debugger() { shutdown(); }

void Debugger::start() {
    enabled_ = true;
    emitJson("{\"t\":\"ready\"}");
    reader_ = std::thread([this] { readerLoop(); });
    // Der Lesethread wartet auf Eingaben; er wird beim Beenden nicht
    // eingesammelt, damit ein blockiertes Lesen den Prozess nicht aufhaelt.
    reader_.detach();
}

void Debugger::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = false;
        quit_ = true;
        paused_ = false;
    }
    cv_.notify_all();
}

void Debugger::readerLoop() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!enabled_) return;
        handleCommand(line);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
        paused_ = false;
    }
    cv_.notify_all();
}

std::string Debugger::keyFor(const SrcPos& pos) {
    return lowerPath(pos.file) + "|" + std::to_string(pos.line);
}

const std::string* Debugger::breakpointFor(const SrcPos& pos) const {
    auto it = breakpoints_.find(keyFor(pos));
    return it == breakpoints_.end() ? nullptr : &it->second;
}

void Debugger::setBreakpoint(const SrcPos& pos, bool on, const std::string& condition) {
    std::string key = keyFor(pos);
    if (on) breakpoints_[std::move(key)] = condition;
    else breakpoints_.erase(key);
}

void Debugger::emitNote(const std::string& text) {
    emitJson("{\"t\":\"note\",\"level\":\"warning\",\"text\":\"" + debugJsonEscape(text) + "\"}");
}

void Debugger::emitJson(const std::string& body) {
    std::lock_guard<std::mutex> lock(g_writeMutex);
    std::fwrite(kPrefix, 1, std::char_traits<char>::length(kPrefix), stdout);
    std::fwrite(body.data(), 1, body.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void Debugger::emitStopEvent(const SrcPos& pos, int depth, const std::string& func,
                             const char* reason, const std::string& cond,
                             const std::string& condError) {
    std::string body = "{\"t\":\"stop\",\"reason\":\"";
    body += reason;
    body += "\",\"file\":\"";
    body += debugJsonEscape(pos.file);
    body += "\",\"line\":";
    body += std::to_string(pos.line);
    body += ",\"func\":\"";
    body += debugJsonEscape(func);
    body += "\",\"depth\":";
    body += std::to_string(depth);
    if (!cond.empty()) body += ",\"condition\":\"" + debugJsonEscape(cond) + "\"";
    if (!condError.empty()) body += ",\"conditionError\":\"" + debugJsonEscape(condError) + "\"";
    body += ",\"scope\":";
    body += scope ? scope() : std::string("null");
    body += "}";
    emitJson(body);
}

void Debugger::emitScope() {
    // Nur sinnvoll, solange der Interpreter steht.
    if (!paused_) return;
    std::string body = "{\"t\":\"scope\",\"scope\":";
    body += scope ? scope() : std::string("null");
    body += "}";
    emitJson(body);
}

void Debugger::stopAt(std::unique_lock<std::mutex>& lock, const SrcPos& pos, int depth,
                      const std::string& func, const char* reason, const std::string& cond,
                      const std::string& condError) {
    paused_ = true;
    pauseRequested_ = false;
    mode_ = Mode::Run;
    stepDepth_ = depth;
    lastFile_ = pos.file;
    lastLine_ = pos.line;
    emitStopEvent(pos, depth, func, reason, cond, condError);

    while (paused_ && !quit_) cv_.wait(lock);
    if (quit_) {
        paused_ = false;
        lock.unlock();
        throw DebugQuit{};
    }
}

void Debugger::atStatement(const SrcPos& pos, int depth, const std::string& func) {
    if (!enabled_ || pos.line <= 0) return;

    std::unique_lock<std::mutex> lock(mutex_);
    if (quit_) throw DebugQuit{};
    if (paused_) return;  // wird bereits angehalten

    // Die ersten Anweisungen laufen erst, wenn die IDE ihre Haltepunkte
    // geschickt und die Sitzung mit "run" freigegeben hat. So haengt kein
    // Haltepunkt von der Startgeschwindigkeit ab.
    if (!started_) {
        started_ = true;
        stopAt(lock, pos, depth, func, "start", "", "");
        return;
    }

    const std::string* bp = breakpointFor(pos);
    std::string condText = bp ? *bp : std::string();
    std::string condError;
    bool hit = bp != nullptr;

    // Bedingte Haltepunkte: nur anhalten, wenn der Ausdruck wahr ist.
    if (hit && !condText.empty()) {
        bool ok = false;
        bool value = false;
        // Die Bedingung laeuft im Interpreter und darf Variablen lesen. Der
        // Mutex bleibt dabei offen, weil aufgerufener Code erneut anhalten kann.
        lock.unlock();
        if (condition) {
            value = condition(condText, ok, condError);
        } else {
            ok = false;
            condError = "Bedingungen sind nicht verfuegbar";
        }
        lock.lock();
        if (quit_) throw DebugQuit{};
        if (!ok) {
            // Eine kaputte Bedingung soll nicht stillschweigend nie greifen.
            if (condError.empty()) condError = "Bedingung konnte nicht gelesen werden";
            emitNote(std::string("Bedingung des Haltepunkts ist fehlerhaft: ") + condError);
            hit = true;
        } else {
            hit = value;
        }
    }

    bool pause = pauseRequested_;
    pauseRequested_ = false;
    bool step = false;
    if (mode_ == Mode::StepIn) step = true;
    else if (mode_ == Mode::StepOver) step = depth <= stepDepth_;
    else if (mode_ == Mode::StepOut) step = depth < stepDepth_;

    if (!hit && !step && !pause) return;

    // Mehrere Anweisungen einer Zeile (z. B. nach IF ... THEN) sind fuer den
    // Einzelschritt ein Haltepunkt, nicht mehrere.
    bool sameSpot = pos.file == lastFile_ && pos.line == lastLine_;
    if (step && !hit && !pause && sameSpot) return;

    const char* reason = hit ? "breakpoint" : (pause ? "pause" : "step");
    stopAt(lock, pos, depth, func, reason, hit ? condText : "", condError);
}

void Debugger::setMode(Mode mode, int depth) {
    mode_ = mode;
    stepDepth_ = depth;
    paused_ = false;
    cv_.notify_all();
}

void Debugger::resume() {
    mode_ = Mode::Run;
    paused_ = false;
    cv_.notify_all();
}

void Debugger::handleCommand(const std::string& line) {
    std::vector<std::string> parts = splitTabs(line);
    if (parts.empty() || parts[0].empty()) return;
    const std::string& command = parts[0];

    if (command == "bp" || command == "bpc") {
        if (parts.size() < 3) return;
        SrcPos pos;
        pos.file = parts[1];
        pos.line = std::atoi(parts[2].c_str());
        if (pos.line <= 0) return;
        const std::string cond = parts.size() > 3 ? parts[3] : std::string();
        std::lock_guard<std::mutex> lock(mutex_);
        setBreakpoint(pos, command == "bp", cond);
        return;
    }
    if (command == "bpcall") {
        std::lock_guard<std::mutex> lock(mutex_);
        breakpoints_.clear();
        return;
    }
    if (command == "quit") {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
            paused_ = false;
        }
        cv_.notify_all();
        return;
    }
    if (command == "vars") {
        std::lock_guard<std::mutex> lock(mutex_);
        emitScope();
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (command == "pause") {
        if (!paused_) pauseRequested_ = true;
    } else if (command == "run" || command == "continue") {
        started_ = true;
        resume();
    } else if (command == "step") {
        setMode(Mode::StepIn, stepDepth_);
    } else if (command == "next") {
        setMode(Mode::StepOver, stepDepth_);
    } else if (command == "out") {
        setMode(Mode::StepOut, stepDepth_);
    }
}

void Debugger::reportError(const BasicError& error) {
    std::string body = "{\"t\":\"error\",\"compile\":";
    body += error.compile ? "true" : "false";
    body += ",\"file\":\"";
    body += debugJsonEscape(error.pos.file);
    body += "\",\"line\":";
    body += std::to_string(error.pos.line);
    body += ",\"message\":\"";
    body += debugJsonEscape(error.what());
    body += "\",\"scope\":";
    body += scope ? scope() : std::string("null");
    body += "}";
    emitJson(body);
    reportExit(2);
}

void Debugger::reportExit(int code) {
    emitJson("{\"t\":\"exit\",\"code\":" + std::to_string(code) + "}");
    shutdown();
}
