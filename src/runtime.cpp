// GameBasiX - Laufzeitumgebung
#include "runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

VT hintToVT(TypeHint h) {
    switch (h) {
        case TypeHint::Int: return VT::Int;
        case TypeHint::Float: return VT::Float;
        case TypeHint::Str: return VT::Str;
        default: return VT::Null;
    }
}

TypeHint vtToHint(VT t) {
    switch (t) {
        case VT::Int: return TypeHint::Int;
        case VT::Float: return TypeHint::Float;
        case VT::Str: return TypeHint::Str;
        default: return TypeHint::None;
    }
}

bool Runtime::fileExists(const std::string& p) const {
    std::error_code ec;
    return fs::exists(fs::path(p), ec) && !fs::is_directory(fs::path(p), ec);
}

std::string Runtime::resolveAsset(const std::string& rel) const {
    if (rel.empty()) return rel;
    fs::path p(rel);
    if (p.is_absolute()) return fileExists(rel) ? rel : rel;

    std::vector<fs::path> candidates;
    candidates.push_back(fs::path(cfg.projectDir) / p);
    if (!cfg.assetDir.empty()) candidates.push_back(fs::path(cfg.projectDir) / cfg.assetDir / p);
    // Bei einem Spielpaket liegen die Assets neben der EXE, nicht bei den
    // ausgehaengten Quellen - auch der Asset-Ordner aus der .gbxproj.
    if (!saveDir.empty()) {
        if (!cfg.assetDir.empty()) candidates.push_back(fs::path(saveDir) / cfg.assetDir / p);
        candidates.push_back(fs::path(saveDir) / p);
    }
    candidates.push_back(p);

    // Schriften: auch im Windows-Schriftverzeichnis suchen
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".ttf" || ext == ".otf") {
        const char* windir = std::getenv("WINDIR");
        std::string fonts = windir ? std::string(windir) + "/Fonts/" : std::string("C:/Windows/Fonts/");
        candidates.push_back(fs::path(fonts) / p.filename());
    }

    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec) && !fs::is_directory(c, ec)) return c.string();
    }
    // Nicht gefunden: ersten sinnvollen Pfad zurueckgeben (Fehlermeldung)
    return candidates.front().string();
}

void Runtime::warnOnce(const std::string& msg) {
    for (const auto& w : warned)
        if (w == msg) return;
    warned.push_back(msg);
    std::fprintf(stderr, "Warnung: %s\n", msg.c_str());
}

std::vector<std::pair<std::string, int64_t>> builtinConstants() {
    std::vector<std::pair<std::string, int64_t>> out;
    auto put = [&](std::string name, int64_t value) {
        out.emplace_back(std::move(name), value);
    };
    put("TRUE", 1);
    put("FALSE", 0);

    put("KEY_ESC", SDL_SCANCODE_ESCAPE);
    put("KEY_ESCAPE", SDL_SCANCODE_ESCAPE);
    put("KEY_ENTER", SDL_SCANCODE_RETURN);
    put("KEY_RETURN", SDL_SCANCODE_RETURN);
    put("KEY_SPACE", SDL_SCANCODE_SPACE);
    put("KEY_TAB", SDL_SCANCODE_TAB);
    put("KEY_BACKSPACE", SDL_SCANCODE_BACKSPACE);
    put("KEY_DELETE", SDL_SCANCODE_DELETE);
    put("KEY_INSERT", SDL_SCANCODE_INSERT);
    put("KEY_HOME", SDL_SCANCODE_HOME);
    put("KEY_END", SDL_SCANCODE_END);
    put("KEY_PGUP", SDL_SCANCODE_PAGEUP);
    put("KEY_PGDN", SDL_SCANCODE_PAGEDOWN);
    put("KEY_UP", SDL_SCANCODE_UP);
    put("KEY_DOWN", SDL_SCANCODE_DOWN);
    put("KEY_LEFT", SDL_SCANCODE_LEFT);
    put("KEY_RIGHT", SDL_SCANCODE_RIGHT);
    put("KEY_LSHIFT", SDL_SCANCODE_LSHIFT);
    put("KEY_RSHIFT", SDL_SCANCODE_RSHIFT);
    put("KEY_LCTRL", SDL_SCANCODE_LCTRL);
    put("KEY_RCTRL", SDL_SCANCODE_RCTRL);
    put("KEY_LALT", SDL_SCANCODE_LALT);
    put("KEY_RALT", SDL_SCANCODE_RALT);
    put("KEY_MINUS", SDL_SCANCODE_MINUS);
    put("KEY_EQUALS", SDL_SCANCODE_EQUALS);
    put("KEY_COMMA", SDL_SCANCODE_COMMA);
    put("KEY_PERIOD", SDL_SCANCODE_PERIOD);

    for (int i = 0; i < 12; ++i)
        put("KEY_F" + std::to_string(i + 1), SDL_SCANCODE_F1 + i);

    for (char c = 'A'; c <= 'Z'; ++c)
        put(std::string("KEY_") + c, SDL_SCANCODE_A + (c - 'A'));
    const SDL_Scancode digits[10] = {SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2,
                                     SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
                                     SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8,
                                     SDL_SCANCODE_9};
    for (int i = 0; i < 10; ++i) put("KEY_" + std::to_string(i), digits[i]);
    return out;
}
