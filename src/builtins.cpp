// GameBasiX - eingebaute Befehle und Funktionen
#include "builtins.h"

#include "i18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

// ------------------------------------------------------------------- BArgs

void BArgs::require(size_t n) const {
    if (vals.size() < n) fail("Zu wenige Argumente");
}

[[noreturn]] void BArgs::fail(const std::string& msg) const {
    throw BasicError(name + ": " + msg, pos, false);
}

double BArgs::num(size_t i) const {
    if (i >= vals.size()) fail("Argument " + std::to_string(i + 1) + " fehlt");
    return vals[i].asDouble();
}

int64_t BArgs::integer(size_t i) const {
    if (i >= vals.size()) fail("Argument " + std::to_string(i + 1) + " fehlt");
    return vals[i].asInt();
}

std::string BArgs::str(size_t i) const {
    if (i >= vals.size()) return "";
    return vals[i].text();
}

bool BArgs::truthy(size_t i) const {
    if (i >= vals.size()) return false;
    return vals[i].truthy();
}

Value* BArgs::out(size_t i) const {
    if (i >= refs.size()) return nullptr;
    return refs[i];
}

void BArgs::setOut(size_t i, Value v) const {
    Value* p = out(i);
    if (!p) fail("Ausgabevariable erwartet (Argument " + std::to_string(i + 1) + ")");
    *p = std::move(v);
}

namespace {

constexpr double kPi = 3.14159265358979323846;

Value ok() { return Value::mkInt(0); }

uint32_t colorOf(BArgs& a, size_t i) {
    return static_cast<uint32_t>(a.integer(i)) & 0xFFFFFF;
}

int clampi(int64_t v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return static_cast<int>(v);
}

int64_t rgbInt(int64_t r, int64_t g, int64_t b) {
    r = std::clamp<int64_t>(r, 0, 255);
    g = std::clamp<int64_t>(g, 0, 255);
    b = std::clamp<int64_t>(b, 0, 255);
    return (r << 16) | (g << 8) | b;
}

std::string lowerExt(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    for (char& c : ext)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return ext;
}

std::string upperAscii(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Ausgabepfad: relativ zum Projektverzeichnis
std::string outputPath(Runtime& rt, const std::string& file) {
    std::filesystem::path p(file);
    if (p.is_absolute()) return file;
    // Bei einem gepackten Spiel liegen die Quellen im Temp-Ordner; Bilder und
    // Speicherstaende gehoeren neben die EXE.
    const std::string& dir = rt.saveDir.empty() ? rt.cfg.projectDir : rt.saveDir;
    return (std::filesystem::path(dir) / p).string();
}

#define ASSET(i) a.rt.resolveAsset(a.str(i))

}  // namespace

std::unordered_map<std::string, Builtin>& builtins() {
    static std::unordered_map<std::string, Builtin> table = [] {
        std::unordered_map<std::string, Builtin> t;
        auto add = [&](const char* name, int mn, int mx, BuiltinFn fn) {
            t[name] = Builtin{mn, mx, std::move(fn)};
        };

        // ------------------------------------------------------------ System
        add("DEBUG", 0, 1, [](BArgs& a) {
            if (!a.vals.empty()) std::fputs(a.str(0).c_str(), stderr);
            std::fflush(stderr);
            return ok();
        });
        add("STDOUT", 0, 1, [](BArgs& a) {
            if (!a.vals.empty()) std::fputs(a.str(0).c_str(), stdout);
            std::fflush(stdout);
            return ok();
        });
        add("STDERR", 0, 1, [](BArgs& a) {
            if (!a.vals.empty()) std::fputs(a.str(0).c_str(), stderr);
            std::fflush(stderr);
            return ok();
        });
        add("STDIN$", 0, 0, [](BArgs& a) {
            std::string line;
            if (!std::getline(std::cin, line)) return Value::mkStr("");
            return Value::mkStr(line);
        });
        add("SLEEP", 1, 1, [](BArgs& a) {
            int ms = clampi(a.integer(0), 0, 3600000);
            if (!a.rt.headless) SDL_Delay(static_cast<Uint32>(ms));
            return ok();
        });
        add("GETTIMER", 0, 0, [](BArgs& a) { return Value::mkFloat(a.rt.gfx.timerFrame()); });
        add("GETTIMERALL", 0, 0, [](BArgs& a) { return Value::mkFloat(a.rt.gfx.timerAll()); });
        add("KEYWAIT", 0, 0, [](BArgs& a) {
            a.rt.gfx.waitKey();
            return ok();
        });
        add("MOUSEWAIT", 0, 0, [](BArgs& a) {
            a.rt.gfx.waitMouse();
            return ok();
        });
        add("GETCOMMANDLINE$", 0, 0, [](BArgs& a) { return Value::mkStr(a.rt.commandLine); });
        add("CODELINE", 0, 0, [](BArgs& a) { return Value::mkInt(a.pos.line); });
        add("CODEFILE$", 0, 0, [](BArgs& a) { return Value::mkStr(a.pos.file); });
        add("PLATFORMINFO$", 1, 1, [](BArgs& a) {
            std::string what = lowerAscii(a.str(0));
            if (what == "os" || what == "platform") {
                const char* p = SDL_GetPlatform();
                return Value::mkStr(p ? lowerAscii(p) : "unknown");
            }
            if (what == "version" || what == "sdl") return Value::mkStr("SDL3");
            if (what == "sdlversion") return Value::mkInt(SDL_GetVersion());
            if (what == "numdisplays") {
                int count = 0;
                SDL_DisplayID* displays = SDL_GetDisplays(&count);
                SDL_free(displays);
                return Value::mkInt(count);
            }
            if (what == "cpus" || what == "cpu") return Value::mkInt(SDL_GetNumLogicalCPUCores());
            return Value::mkStr("");
        });
        add("GETENV$", 1, 1, [](BArgs& a) {
            const char* v = std::getenv(a.str(0).c_str());
            return Value::mkStr(v ? v : "");
        });
        add("PUTENV", 2, 2, [](BArgs& a) {
#ifdef _WIN32
            _putenv_s(a.str(0).c_str(), a.str(1).c_str());
#else
            setenv(a.str(0).c_str(), a.str(1).c_str(), 1);
#endif
            return ok();
        });
        add("ASSERT", 1, 2, [](BArgs& a) {
            ++a.rt.assertCount;
            if (!a.truthy(0)) {
                ++a.rt.assertFailures;
                std::string msg = a.vals.size() > 1 ? a.str(1) : "";
                std::fprintf(stderr, i18n::tr("ASSERT FEHLGESCHLAGEN %s:%d%s%s\n").c_str(),
                             a.pos.file.c_str(),
                             a.pos.line, msg.empty() ? "" : ": ", msg.c_str());
            }
            return ok();
        });
        add("HIBERNATE", 0, 0, [](BArgs& a) {
            if (a.rt.headless) return ok();
            for (;;) {
                a.rt.gfx.present();
                if (a.rt.gfx.anyKeyHit() || a.rt.gfx.mouseHit(1) || a.rt.gfx.mouseHit(2) ||
                    a.rt.gfx.quitRequested())
                    break;
                if (a.rt.gfx.keyDown(SDL_SCANCODE_ESCAPE)) break;
                SDL_Delay(20);
            }
            return ok();
        });
        add("ALLOWESCAPE", 0, 1, [](BArgs& a) {
            a.rt.gfx.setAllowEscape(a.truthy(0));
            return ok();
        });
        add("SYSTEMPOINTER", 0, 1, [](BArgs& a) {
            a.rt.gfx.showCursor(a.truthy(0));
            return ok();
        });
        add("HUSH", 0, 0, [](BArgs& a) {
            a.rt.audio.hush();
            return ok();
        });

        // ----------------------------------------------------------- Fenster
        add("SETSCREEN", 1, 3, [](BArgs& a) {
            int w = static_cast<int>(a.integer(0));
            int h = a.vals.size() > 1 ? static_cast<int>(a.integer(1)) : 0;
            int fs = a.vals.size() > 2 ? static_cast<int>(a.integer(2)) : -1;
            a.rt.gfx.setScreen(w, h, fs);
            return ok();
        });
        add("SETWINDOWTITLE", 1, 1, [](BArgs& a) {
            a.rt.gfx.setTitle(a.str(0));
            return ok();
        });
        add("CLEARSCREEN", 0, 1, [](BArgs& a) {
            int64_t col = a.vals.empty() ? 0 : a.integer(0);
            if (col == -1) {
                a.rt.gfx.setClearColor(static_cast<uint32_t>(-1));
                return ok();
            }
            a.rt.gfx.clearScreen(static_cast<uint32_t>(col) & 0xFFFFFF);
            return ok();
        });
        add("BLACKSCREEN", 0, 0, [](BArgs& a) {
            a.rt.gfx.blackScreen();
            return ok();
        });
        add("SHOWSCREEN", 0, 0, [](BArgs& a) {
            a.rt.gfx.present();
            if (a.rt.gfx.quitRequested()) a.rt.quit = true;
            return ok();
        });
        add("LIMITFPS", 1, 2, [](BArgs& a) {
            a.rt.gfx.setLimitFps(a.num(0), a.vals.size() > 1 ? a.truthy(1) : false);
            return ok();
        });
        add("GETSCREENSIZE", 2, 2, [](BArgs& a) {
            a.setOut(0, Value::mkInt(a.rt.gfx.screenWidth()));
            a.setOut(1, Value::mkInt(a.rt.gfx.screenHeight()));
            return ok();
        });
        add("GETSCREENWIDTH", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.screenWidth()); });
        add("GETSCREENHEIGHT", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.screenHeight()); });
        add("GETDESKTOPSIZE", 2, 2, [](BArgs& a) {
            int w = 0, h = 0;
            a.rt.gfx.desktopSize(w, h);
            a.setOut(0, Value::mkInt(w));
            a.setOut(1, Value::mkInt(h));
            return ok();
        });
        add("ISFULLSCREEN", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.isFullscreen()); });
        add("VIEWPORT", 4, 4, [](BArgs& a) {
            a.rt.gfx.setViewport(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                                 static_cast<int>(a.integer(2)), static_cast<int>(a.integer(3)));
            return ok();
        });
        add("GETVIEWPORT", 4, 4, [](BArgs& a) {
            int x, y, w, h;
            a.rt.gfx.getViewport(x, y, w, h);
            a.setOut(0, Value::mkInt(x));
            a.setOut(1, Value::mkInt(y));
            a.setOut(2, Value::mkInt(w));
            a.setOut(3, Value::mkInt(h));
            return ok();
        });
        add("PRESCALER", 0, 2, [](BArgs& a) {
            if (a.vals.size() < 2) a.rt.gfx.setPrescaler(0, 0);
            else a.rt.gfx.setPrescaler(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)));
            return ok();
        });
        add("SMOOTHSHADING", 0, 1, [](BArgs& a) {
            a.rt.gfx.setSmoothShading(a.truthy(0));
            return ok();
        });
        add("ALPHAMODE", 0, 1, [](BArgs& a) {
            a.rt.gfx.setAlphaMode(a.vals.empty() ? 0.0 : a.num(0));
            return ok();
        });
        add("ALPHATESTING", 0, 1, [](BArgs& a) {
            a.rt.gfx.setAlphaTesting(a.vals.empty() ? 1.0 : a.num(0));
            return ok();
        });
        add("SETTRANSPARENCY", 1, 1, [](BArgs& a) {
            a.rt.gfx.setTransparency(colorOf(a, 0));
            return ok();
        });
        add("SETTEXTCOLOR", 1, 1, [](BArgs& a) {
            a.rt.textColor = colorOf(a, 0);
            return ok();
        });

        // ---------------------------------------------------------- Zeichnen
        add("RGB", 3, 3, [](BArgs& a) {
            return Value::mkInt(rgbInt(a.integer(0), a.integer(1), a.integer(2)));
        });
        add("DRAWLINE", 5, 5, [](BArgs& a) {
            a.rt.gfx.drawLine(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)),
                              static_cast<float>(a.num(2)), static_cast<float>(a.num(3)),
                              colorOf(a, 4));
            return ok();
        });
        add("DRAWRECT", 5, 5, [](BArgs& a) {
            a.rt.gfx.drawRect(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)),
                              static_cast<float>(a.num(2)), static_cast<float>(a.num(3)),
                              colorOf(a, 4));
            return ok();
        });
        add("DRAWBORDER", 5, 5, [](BArgs& a) {
            float x = static_cast<float>(a.num(0)), y = static_cast<float>(a.num(1));
            float w = static_cast<float>(a.num(2)), h = static_cast<float>(a.num(3));
            uint32_t c = colorOf(a, 4);
            a.rt.gfx.drawLine(x, y, x + w, y, c);
            a.rt.gfx.drawLine(x + w, y, x + w, y + h, c);
            a.rt.gfx.drawLine(x + w, y + h, x, y + h, c);
            a.rt.gfx.drawLine(x, y + h, x, y, c);
            return ok();
        });
        add("DRAWCIRCLE", 4, 5, [](BArgs& a) {
            uint32_t c = a.vals.size() > 3 ? colorOf(a, 3) : 0xFFFFFF;
            a.rt.gfx.drawCircle(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)),
                                static_cast<float>(a.num(2)), c, false);
            return ok();
        });
        add("DRAWFILLEDCIRCLE", 4, 5, [](BArgs& a) {
            uint32_t c = a.vals.size() > 3 ? colorOf(a, 3) : 0xFFFFFF;
            a.rt.gfx.drawCircle(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)),
                                static_cast<float>(a.num(2)), c, true);
            return ok();
        });
        add("SETPIXEL", 3, 3, [](BArgs& a) {
            a.rt.gfx.setPixel(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                              colorOf(a, 2));
            return ok();
        });
        add("GETPIXEL", 2, 2, [](BArgs& a) {
            return Value::mkInt(a.rt.gfx.getPixel(static_cast<int>(a.integer(0)),
                                                  static_cast<int>(a.integer(1))));
        });
        add("STARTPOLY", 0, 2, [](BArgs& a) {
            int sprite = a.vals.empty() ? -1 : static_cast<int>(a.integer(0));
            int mode = a.vals.size() > 1 ? static_cast<int>(a.integer(1)) : 0;
            a.rt.gfx.startPoly(sprite, mode);
            return ok();
        });
        add("POLYVECTOR", 2, 5, [](BArgs& a) {
            float tx = a.vals.size() > 2 ? static_cast<float>(a.num(2)) : 0.0f;
            float ty = a.vals.size() > 3 ? static_cast<float>(a.num(3)) : 0.0f;
            uint32_t c = a.vals.size() > 4 ? colorOf(a, 4) : 0xFFFFFF;
            a.rt.gfx.polyVector(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)), tx, ty, c);
            return ok();
        });
        add("POLYNEWSTRIP", 0, 0, [](BArgs& a) {
            a.rt.gfx.startPoly(-1, 2);
            return ok();
        });
        add("ENDPOLY", 0, 0, [](BArgs& a) {
            a.rt.gfx.endPoly();
            return ok();
        });

        // ------------------------------------------------------------ Sprites
        add("GENSPRITE", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.nextSpriteId()); });
        add("GENSOUND", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.audio.nextId()); });
        add("GENFONT", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.nextFontId()); });
        add("GENSCREEN", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.nextScreenId()); });
        add("LOADSPRITE", 2, 2, [](BArgs& a) {
            std::string err;
            if (!a.rt.gfx.loadSprite(ASSET(0), static_cast<int>(a.integer(1)), err)) a.fail(err);
            return ok();
        });
        add("LOADANIM", 4, 4, [](BArgs& a) {
            std::string err;
            int id = static_cast<int>(a.integer(1));
            if (!a.rt.gfx.loadSprite(ASSET(0), id, err)) a.fail(err);
            a.rt.gfx.setSpriteAnim(id, static_cast<int>(a.integer(2)), static_cast<int>(a.integer(3)));
            return ok();
        });
        add("SETSPRITEANIM", 3, 3, [](BArgs& a) {
            a.rt.gfx.setSpriteAnim(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                                   static_cast<int>(a.integer(2)));
            return ok();
        });
        add("DRAWSPRITE", 3, 3, [](BArgs& a) {
            a.rt.gfx.drawSprite(static_cast<int>(a.integer(0)), static_cast<float>(a.num(1)),
                                static_cast<float>(a.num(2)));
            return ok();
        });
        add("ROTOSPRITE", 4, 4, [](BArgs& a) {
            a.rt.gfx.drawSpriteRotozoom(static_cast<int>(a.integer(0)), static_cast<float>(a.num(1)),
                                        static_cast<float>(a.num(2)), a.num(3), 100.0f);
            return ok();
        });
        add("ROTOZOOMSPRITE", 5, 5, [](BArgs& a) {
            a.rt.gfx.drawSpriteRotozoom(static_cast<int>(a.integer(0)), static_cast<float>(a.num(1)),
                                        static_cast<float>(a.num(2)), a.num(3),
                                        static_cast<float>(a.num(4)));
            return ok();
        });
        add("ZOOMSPRITE", 5, 5, [](BArgs& a) {
            a.rt.gfx.drawSpriteZoom(static_cast<int>(a.integer(0)), static_cast<float>(a.num(1)),
                                    static_cast<float>(a.num(2)), static_cast<float>(a.num(3)),
                                    static_cast<float>(a.num(4)));
            return ok();
        });
        add("STRETCHSPRITE", 5, 5, [](BArgs& a) {
            int id = static_cast<int>(a.integer(0));
            a.rt.gfx.drawSpriteZoom(id, static_cast<float>(a.num(1)), static_cast<float>(a.num(2)),
                                    100.0f * static_cast<float>(a.num(3)) /
                                        std::max(1, a.rt.gfx.spriteWidth(id)),
                                    100.0f * static_cast<float>(a.num(4)) /
                                        std::max(1, a.rt.gfx.spriteHeight(id)));
            return ok();
        });
        add("DRAWANIM", 4, 4, [](BArgs& a) {
            a.rt.gfx.drawAnim(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                              static_cast<float>(a.num(2)), static_cast<float>(a.num(3)));
            return ok();
        });
        add("ROTOZOOMANIM", 6, 6, [](BArgs& a) {
            a.rt.gfx.drawAnimRotozoom(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                                      static_cast<float>(a.num(2)), static_cast<float>(a.num(3)),
                                      a.num(4), static_cast<float>(a.num(5)));
            return ok();
        });
        add("STRETCHANIM", 6, 6, [](BArgs& a) {
            a.rt.gfx.drawAnimStretch(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                                     static_cast<float>(a.num(2)), static_cast<float>(a.num(3)),
                                     static_cast<float>(a.num(4)), static_cast<float>(a.num(5)));
            return ok();
        });
        add("GETSPRITESIZE", 3, 3, [](BArgs& a) {
            int w = 0, h = 0;
            a.rt.gfx.spriteInfo(static_cast<int>(a.integer(0)), w, h);
            a.setOut(1, Value::mkInt(w));
            a.setOut(2, Value::mkInt(h));
            return ok();
        });
        add("GRABSPRITE", 5, 5, [](BArgs& a) {
            std::string err;
            if (!a.rt.gfx.grabSprite(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                                     static_cast<int>(a.integer(2)), static_cast<int>(a.integer(3)),
                                     static_cast<int>(a.integer(4)), err))
                a.fail(err);
            return ok();
        });
        add("SAVESPRITE", 2, 2, [](BArgs& a) {
            std::string err;
            if (!a.rt.gfx.saveSprite(outputPath(a.rt, a.str(0)), static_cast<int>(a.integer(1)), err))
                a.fail(err);
            return ok();
        });
        add("SPRCOLL", 6, 6, [](BArgs& a) {
            return Value::mkInt(a.rt.gfx.spriteCollision(
                static_cast<int>(a.integer(0)), static_cast<float>(a.num(1)),
                static_cast<float>(a.num(2)), static_cast<int>(a.integer(3)),
                static_cast<float>(a.num(4)), static_cast<float>(a.num(5))));
        });
        add("ANIMCOLL", 8, 8, [](BArgs& a) {
            return Value::mkInt(a.rt.gfx.animCollision(
                static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                static_cast<float>(a.num(2)), static_cast<float>(a.num(3)),
                static_cast<int>(a.integer(4)), static_cast<int>(a.integer(5)),
                static_cast<float>(a.num(6)), static_cast<float>(a.num(7))));
        });
        add("BOXCOLL", 8, 8, [](BArgs& a) {
            double x1 = a.num(0), y1 = a.num(1), w1 = a.num(2), h1 = a.num(3);
            double x2 = a.num(4), y2 = a.num(5), w2 = a.num(6), h2 = a.num(7);
            bool hit = x1 < x2 + w2 && x2 < x1 + w1 && y1 < y2 + h2 && y2 < y1 + h1;
            return Value::mkInt(hit ? 1 : 0);
        });

        // -------------------------------------------------------- Bildpuffer
        add("LOADBMP", 1, 1, [](BArgs& a) {
            std::string err;
            if (!a.rt.gfx.loadBackground(ASSET(0), err)) a.fail(err);
            return ok();
        });
        add("SAVEBMP", 1, 1, [](BArgs& a) {
            std::string err;
            if (!a.rt.gfx.saveBmp(outputPath(a.rt, a.str(0)), err)) a.fail(err);
            return ok();
        });
        add("USEASBMP", 0, 0, [](BArgs& a) {
            a.rt.gfx.useAsBackground();
            return ok();
        });
        add("BLENDSCREEN", 1, 2, [](BArgs& a) {
            a.rt.gfx.blendScreen(ASSET(0), a.vals.size() > 1 ? static_cast<int>(a.integer(1)) : 1000);
            return ok();
        });
        add("CREATESCREEN", 4, 4, [](BArgs& a) {
            a.rt.gfx.createScreen(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                                  static_cast<int>(a.integer(2)), static_cast<int>(a.integer(3)));
            return ok();
        });
        add("USESCREEN", 1, 1, [](BArgs& a) {
            a.rt.gfx.useScreen(static_cast<int>(a.integer(0)));
            return ok();
        });

        // ------------------------------------------------------------- Text
        add("PRINT", 3, 4, [](BArgs& a) {
            a.rt.gfx.drawText(a.str(0), static_cast<float>(a.num(1)), static_cast<float>(a.num(2)),
                              a.rt.textColor);
            return ok();
        });
        add("DRAWTEXT", 3, 4, [](BArgs& a) {
            uint32_t c = a.vals.size() > 3 ? colorOf(a, 3) : a.rt.textColor;
            a.rt.gfx.drawText(a.str(0), static_cast<float>(a.num(1)), static_cast<float>(a.num(2)), c);
            return ok();
        });
        add("LOADFONT", 2, 3, [](BArgs& a) {
            std::string file = a.str(0);
            int id = static_cast<int>(a.integer(1));
            int size = a.vals.size() > 2 ? static_cast<int>(a.integer(2)) : 0;
            std::string ext = lowerExt(file);
            std::string err;
            bool ttf = ext == "ttf" || ext == "otf" || (ext.empty() && size > 0);
            if (ttf) {
                std::string path = ASSET(0);
                if (ext.empty()) path = a.rt.resolveAsset(file + ".ttf");
                int pts = size > 0 ? size : 16;
                if (!a.rt.gfx.loadTtfFont(path, id, pts, err)) a.fail(err);
            } else {
                if (!a.rt.gfx.loadBitmapFont(ASSET(0), id, err)) a.fail(err);
            }
            return ok();
        });
        add("SETFONT", 1, 3, [](BArgs& a) {
            a.rt.gfx.setFont(static_cast<int>(a.integer(0)));
            if (a.vals.size() > 1) a.rt.gfx.setFontScale(static_cast<int>(a.integer(1)));
            if (a.vals.size() > 2) a.rt.textColor = colorOf(a, 2);
            return ok();
        });
        add("GETFONTSIZE", 2, 2, [](BArgs& a) {
            int w = 0, h = 0;
            a.rt.gfx.fontCharSize(w, h);
            a.setOut(0, Value::mkInt(w));
            a.setOut(1, Value::mkInt(h));
            return ok();
        });
        add("GETCURRENTFONT", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.currentFont()); });
        add("INPUT", 1, 3, [](BArgs& a) {
            Value* target = a.out(0);
            if (!target) a.fail("erwartet eine Variable als erstes Argument");
            float x = a.vals.size() > 1 ? static_cast<float>(a.num(1)) : 0.0f;
            float y = a.vals.size() > 2 ? static_cast<float>(a.num(2)) : 0.0f;
            std::string buf = target->isStr() ? target->s : target->text();
            for (;;) {
                a.rt.gfx.drawText(buf + "_", x, y, a.rt.textColor);
                a.rt.gfx.present();
                if (a.rt.gfx.quitRequested()) break;
                std::string in = a.rt.gfx.takeInkey();
                if (in.empty()) in = a.rt.gfx.takeTextInput();
                if (in.empty()) continue;
                bool done = false;
                for (char ch : in) {
                    if (ch == '\n') {
                        done = true;
                        break;
                    }
                    if (ch == '\b') {
                        if (!buf.empty()) buf.pop_back();
                        continue;
                    }
                    if (static_cast<unsigned char>(ch) >= 32) buf.push_back(ch);
                }
                if (done) break;
            }
            if (target->isStr() || target->type == VT::Null)
                *target = Value::mkStr(buf);
            else
                *target = Value::mkFloat(std::atof(buf.c_str()));
            return ok();
        });

        // ----------------------------------------------------------- Eingabe
        add("KEY", 1, 1, [](BArgs& a) {
            return Value::mkInt(a.rt.gfx.keyDown(static_cast<int>(a.integer(0))) ? 1 : 0);
        });
        add("KEYHIT", 1, 1, [](BArgs& a) {
            return Value::mkInt(a.rt.gfx.keyHit(static_cast<int>(a.integer(0))) ? 1 : 0);
        });
        add("INKEY$", 0, 0, [](BArgs& a) { return Value::mkStr(a.rt.gfx.takeInkey()); });
        add("MOUSESTATE", 4, 4, [](BArgs& a) {
            float x, y;
            bool l, r, m;
            a.rt.gfx.mouseState(x, y, l, r, m);
            a.setOut(0, Value::mkInt(static_cast<int64_t>(x)));
            a.setOut(1, Value::mkInt(static_cast<int64_t>(y)));
            a.setOut(2, Value::mkInt(l ? 1 : 0));
            a.setOut(3, Value::mkInt(r ? 1 : 0));
            return ok();
        });
        add("MOUSEAXIS", 1, 1, [](BArgs& a) {
            float x, y;
            bool l, r, m;
            a.rt.gfx.mouseState(x, y, l, r, m);
            switch (static_cast<int>(a.integer(0))) {
                case 0: return Value::mkInt(static_cast<int64_t>(x));
                case 1: return Value::mkInt(static_cast<int64_t>(y));
                case 2: return Value::mkInt(a.rt.gfx.mouseWheel());
                case 3: return Value::mkInt(l ? 1 : 0);
                case 4: return Value::mkInt(r ? 1 : 0);
                case 5: return Value::mkInt(m ? 1 : 0);
                default: return Value::mkInt(0);
            }
        });
        add("MOUSEHIT", 1, 1, [](BArgs& a) {
            return Value::mkInt(a.rt.gfx.mouseHit(static_cast<int>(a.integer(0))) ? 1 : 0);
        });
        add("GETMOUSECOUNT", 0, 0, [](BArgs&) { return Value::mkInt(1); });
        add("SETMOUSE", 2, 2, [](BArgs& a) {
            a.rt.gfx.setMouse(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)));
            return ok();
        });

        // ------------------------------------------------------------ Sound
        add("LOADSOUND", 2, 3, [](BArgs& a) {
            std::string err;
            int buffers = a.vals.size() > 2 ? static_cast<int>(a.integer(2)) : 1;
            if (!a.rt.audio.loadSound(ASSET(0), static_cast<int>(a.integer(1)), buffers, err))
                a.fail(err);
            return ok();
        });
        add("PLAYSOUND", 1, 3, [](BArgs& a) {
            double pan = a.vals.size() > 1 ? a.num(1) : 0.0;
            double vol = a.vals.size() > 2 ? a.num(2) : 1.0;
            return Value::mkInt(a.rt.audio.playSound(static_cast<int>(a.integer(0)), pan, vol));
        });
        add("STOPSOUND", 1, 1, [](BArgs& a) {
            a.rt.audio.stopChannel(static_cast<int>(a.integer(0)));
            return ok();
        });
        add("SOUNDPLAYING", 1, 1, [](BArgs& a) {
            return Value::mkInt(a.rt.audio.channelPlaying(static_cast<int>(a.integer(0))) ? 1 : 0);
        });
        add("PLAYMUSIC", 1, 2, [](BArgs& a) {
            std::string err;
            bool loop = a.vals.size() > 1 ? a.truthy(1) : true;
            if (!a.rt.audio.playMusic(ASSET(0), loop, err)) a.fail(err);
            return ok();
        });
        add("STOPMUSIC", 0, 0, [](BArgs& a) {
            a.rt.audio.stopMusic();
            return ok();
        });
        add("PAUSEMUSIC", 1, 1, [](BArgs& a) {
            a.rt.audio.pauseMusic(a.truthy(0));
            return ok();
        });
        add("ISMUSICPLAYING", 0, 0, [](BArgs& a) {
            return Value::mkInt(a.rt.audio.musicPlaying() ? 1 : 0);
        });
        add("MUSICVOLUME", 1, 1, [](BArgs& a) {
            a.rt.audio.setMusicVolume(a.num(0));
            return ok();
        });

        // ------------------------------------------------------------- Mathe
        add("ABS", 1, 1, [](BArgs& a) {
            Value v = a.vals[0];
            if (v.type == VT::Int) return Value::mkInt(v.i < 0 ? -v.i : v.i);
            return Value::mkFloat(std::fabs(v.asDouble()));
        });
        add("SGN", 1, 1, [](BArgs& a) {
            double v = a.num(0);
            return Value::mkInt(v > 0 ? 1 : (v < 0 ? -1 : 0));
        });
        add("SQR", 1, 1, [](BArgs& a) {
            double v = a.num(0);
            if (v < 0) a.fail("SQR: negative Zahl");
            return Value::mkFloat(std::sqrt(v));
        });
        add("SIN", 1, 1, [](BArgs& a) { return Value::mkFloat(std::sin(a.num(0) * kPi / 180.0)); });
        add("COS", 1, 1, [](BArgs& a) { return Value::mkFloat(std::cos(a.num(0) * kPi / 180.0)); });
        add("TAN", 1, 1, [](BArgs& a) { return Value::mkFloat(std::tan(a.num(0) * kPi / 180.0)); });
        add("ASIN", 1, 1, [](BArgs& a) {
            return Value::mkFloat(std::asin(a.num(0)) * 180.0 / kPi);
        });
        add("ACOS", 1, 1, [](BArgs& a) {
            return Value::mkFloat(std::acos(a.num(0)) * 180.0 / kPi);
        });
        add("ATAN", 1, 2, [](BArgs& a) {
            double dy = a.num(0);
            double dx = a.vals.size() > 1 ? a.num(1) : 1.0;
            return Value::mkFloat(std::atan2(dy, dx) * 180.0 / kPi);
        });
        add("FLOOR", 1, 1, [](BArgs& a) { return Value::mkFloat(std::floor(a.num(0))); });
        add("CEIL", 1, 1, [](BArgs& a) { return Value::mkFloat(std::ceil(a.num(0))); });
        add("POW", 2, 2, [](BArgs& a) {
            return Value::mkFloat(std::pow(a.num(0), a.num(1)));
        });
        add("LOGN", 1, 1, [](BArgs& a) { return Value::mkFloat(std::log(a.num(0))); });
        add("MOD", 2, 2, [](BArgs& a) {
            int64_t b = a.integer(1);
            if (b == 0) a.fail("Division durch Null");
            return Value::mkInt(a.integer(0) % b);
        });
        add("FMOD", 2, 2, [](BArgs& a) {
            double b = a.num(1);
            if (b == 0) a.fail("Division durch Null");
            return Value::mkFloat(std::fmod(a.num(0), b));
        });
        add("MIN", 2, 2, [](BArgs& a) {
            if (a.vals[0].isStr() || a.vals[1].isStr())
                return Value::mkStr(std::min(a.vals[0].text(), a.vals[1].text()));
            if (a.vals[0].type == VT::Int && a.vals[1].type == VT::Int)
                return Value::mkInt(std::min(a.vals[0].i, a.vals[1].i));
            return Value::mkFloat(std::min(a.num(0), a.num(1)));
        });
        add("MAX", 2, 2, [](BArgs& a) {
            if (a.vals[0].isStr() || a.vals[1].isStr())
                return Value::mkStr(std::max(a.vals[0].text(), a.vals[1].text()));
            if (a.vals[0].type == VT::Int && a.vals[1].type == VT::Int)
                return Value::mkInt(std::max(a.vals[0].i, a.vals[1].i));
            return Value::mkFloat(std::max(a.num(0), a.num(1)));
        });
        add("RND", 1, 1, [](BArgs& a) {
            int64_t mx = a.integer(0);
            if (mx <= 0) return Value::mkInt(0);
            std::uniform_int_distribution<int64_t> dist(0, mx);
            return Value::mkInt(dist(a.rt.rng));
        });
        add("SEEDRND", 1, 1, [](BArgs& a) {
            a.rt.rng.seed(static_cast<uint64_t>(a.integer(0)));
            return ok();
        });
        add("INTEGER", 1, 1, [](BArgs& a) {
            return Value::mkInt(static_cast<int64_t>(a.num(0)));
        });
        add("INTEGER64", 1, 1, [](BArgs& a) {
            return Value::mkInt(static_cast<int64_t>(a.num(0)));
        });
        add("NUMBER", 1, 1, [](BArgs& a) {
            if (a.vals[0].isNum()) return Value::mkFloat(a.num(0));
            return Value::mkFloat(std::atof(a.str(0).c_str()));
        });
        add("ASL", 2, 2, [](BArgs& a) {
            return Value::mkInt(static_cast<int64_t>(static_cast<uint64_t>(a.integer(0))
                                                     << (a.integer(1) & 63)));
        });
        add("ASR", 2, 2, [](BArgs& a) {
            return Value::mkInt(a.integer(0) >> (a.integer(1) & 63));
        });
        add("BAND", 2, 2, [](BArgs& a) { return Value::mkInt(a.integer(0) & a.integer(1)); });
        add("BOR", 2, 2, [](BArgs& a) { return Value::mkInt(a.integer(0) | a.integer(1)); });
        add("BXOR", 2, 2, [](BArgs& a) { return Value::mkInt(a.integer(0) ^ a.integer(1)); });
        add("BNOT", 1, 1, [](BArgs& a) { return Value::mkInt(~a.integer(0)); });

        // ----------------------------------------------------------- Strings
        add("LEN", 1, 2, [](BArgs& a) {
            if (a.vals[0].isArray() && a.vals[0].arr)
                return Value::mkInt(static_cast<int64_t>(a.vals[0].arr->items.size()));
            std::string s = a.str(0);
            if (a.vals.size() > 1 && a.truthy(1)) {
                int w = 0, h = 0;
                a.rt.gfx.textSize(s, w, h);
                return Value::mkInt(w);
            }
            return Value::mkInt(static_cast<int64_t>(s.size()));
        });
        add("DIMDATA", 1, -1, [](BArgs& a) {
            auto arr = std::make_shared<ArrayObj>();
            VT elemVT = VT::Float;
            if (a.vals.size() > 1) {
                const Value& first = a.vals[1];
                if (first.isStr()) elemVT = VT::Str;
                else if (first.type == VT::Int) elemVT = VT::Int;
            }
            arr->elemVT = elemVT;
            for (size_t i = 1; i < a.vals.size(); ++i) {
                Value v = a.vals[i];
                if (!v.isArray() && !v.isRecord()) v = coerceTo(v, elemVT);
                arr->items.push_back(std::move(v));
            }
            a.setOut(0, Value::mkArray(arr));
            return Value::mkInt(static_cast<int64_t>(arr->items.size()));
        });
        add("BOUNDS", 2, 2, [](BArgs& a) {
            int64_t dim = a.integer(1);
            Value v = a.vals[0];
            int64_t size = 0;
            for (int64_t d = 0; d <= dim; ++d) {
                if (!v.isArray() || !v.arr) return Value::mkInt(0);
                size = static_cast<int64_t>(v.arr->items.size());
                if (d < dim) {
                    if (v.arr->items.empty()) return Value::mkInt(0);
                    v = v.arr->items[0];
                }
            }
            return Value::mkInt(size);
        });
        add("ASC", 1, 2, [](BArgs& a) {
            std::string s = a.str(0);
            int64_t idx = a.vals.size() > 1 ? a.integer(1) : 0;
            if (idx < 0 || idx >= static_cast<int64_t>(s.size())) return Value::mkInt(0);
            return Value::mkInt(static_cast<unsigned char>(s[static_cast<size_t>(idx)]));
        });
        add("CHR$", 1, 1, [](BArgs& a) {
            int64_t c = a.integer(0) & 0xFF;
            if (c == 0) return Value::mkStr("");
            return Value::mkStr(std::string(1, static_cast<char>(c)));
        });
        add("HEX$", 1, 1, [](BArgs& a) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(a.integer(0)));
            return Value::mkStr(buf);
        });
        add("LEFT$", 2, 2, [](BArgs& a) {
            std::string s = a.str(0);
            int64_t n = a.integer(1);
            if (n <= 0) return Value::mkStr("");
            if (n > static_cast<int64_t>(s.size())) n = static_cast<int64_t>(s.size());
            return Value::mkStr(s.substr(0, static_cast<size_t>(n)));
        });
        add("RIGHT$", 2, 2, [](BArgs& a) {
            std::string s = a.str(0);
            int64_t n = a.integer(1);
            if (n <= 0) return Value::mkStr("");
            if (n > static_cast<int64_t>(s.size())) n = static_cast<int64_t>(s.size());
            return Value::mkStr(s.substr(s.size() - static_cast<size_t>(n)));
        });
        add("MID$", 2, 3, [](BArgs& a) {
            std::string s = a.str(0);
            int64_t start = a.integer(1);
            int64_t len = a.vals.size() > 2 ? a.integer(2) : -1;
            if (start < 0) start = 0;
            if (start >= static_cast<int64_t>(s.size())) return Value::mkStr("");
            if (len < 0 || start + len > static_cast<int64_t>(s.size()))
                len = static_cast<int64_t>(s.size()) - start;
            return Value::mkStr(s.substr(static_cast<size_t>(start), static_cast<size_t>(len)));
        });
        add("INSTR", 2, 3, [](BArgs& a) {
            std::string s = a.str(0), f = a.str(1);
            int64_t start = a.vals.size() > 2 ? a.integer(2) : 0;
            if (start < 0) start = 0;
            if (start > static_cast<int64_t>(s.size())) return Value::mkInt(-1);
            size_t pos = s.find(f, static_cast<size_t>(start));
            if (pos == std::string::npos) return Value::mkInt(-1);
            return Value::mkInt(static_cast<int64_t>(pos));
        });
        add("REVINSTR", 2, 3, [](BArgs& a) {
            std::string s = a.str(0), f = a.str(1);
            int64_t start = a.vals.size() > 2 ? a.integer(2) : -1;
            if (start < 0 || start > static_cast<int64_t>(s.size()))
                start = static_cast<int64_t>(s.size());
            size_t pos = s.rfind(f, static_cast<size_t>(start));
            if (pos == std::string::npos) return Value::mkInt(-1);
            return Value::mkInt(static_cast<int64_t>(pos));
        });
        add("REPLACE$", 3, 3, [](BArgs& a) {
            std::string s = a.str(0), from = a.str(1), to = a.str(2);
            if (from.empty()) return Value::mkStr(s);
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.size(), to);
                pos += to.size();
            }
            return Value::mkStr(s);
        });
        add("SPLITSTR", 2, 4, [](BArgs& a) {
            std::string text = a.str(0);
            std::string sep = a.vals.size() > 2 ? a.str(2) : ",";
            bool dropEmpty = a.vals.size() > 3 ? a.truthy(3) : true;
            auto arr = std::make_shared<ArrayObj>();
            arr->elemVT = VT::Str;
            if (!sep.empty()) {
                size_t start = 0;
                while (start <= text.size()) {
                    size_t p = text.find(sep, start);
                    std::string part =
                        text.substr(start, p == std::string::npos ? std::string::npos : p - start);
                    if (!dropEmpty || !part.empty()) arr->items.push_back(Value::mkStr(part));
                    if (p == std::string::npos) break;
                    start = p + sep.size();
                }
            } else {
                for (char c : text) arr->items.push_back(Value::mkStr(std::string(1, c)));
            }
            a.setOut(1, Value::mkArray(arr));
            return Value::mkInt(static_cast<int64_t>(arr->items.size()));
        });
        add("UCASE$", 1, 1, [](BArgs& a) { return Value::mkStr(upperAscii(a.str(0))); });
        add("LCASE$", 1, 1, [](BArgs& a) { return Value::mkStr(lowerAscii(a.str(0))); });
        add("LTRIM$", 1, 2, [](BArgs& a) {
            std::string s = a.str(0);
            std::string set = a.vals.size() > 1 ? a.str(1) : " \t\r\n";
            size_t p = s.find_first_not_of(set);
            return Value::mkStr(p == std::string::npos ? "" : s.substr(p));
        });
        add("RTRIM$", 1, 2, [](BArgs& a) {
            std::string s = a.str(0);
            std::string set = a.vals.size() > 1 ? a.str(1) : " \t\r\n";
            size_t p = s.find_last_not_of(set);
            return Value::mkStr(p == std::string::npos ? "" : s.substr(0, p + 1));
        });
        add("TRIM$", 1, 2, [](BArgs& a) {
            std::string s = a.str(0);
            std::string set = a.vals.size() > 1 ? a.str(1) : " \t\r\n";
            size_t b = s.find_first_not_of(set);
            if (b == std::string::npos) return Value::mkStr("");
            size_t e = s.find_last_not_of(set);
            return Value::mkStr(s.substr(b, e - b + 1));
        });
        add("FORMAT$", 3, 3, [](BArgs& a) {
            int width = static_cast<int>(a.integer(0));
            int dec = clampi(a.integer(1), 0, 18);
            double v = a.num(2);
            char buf[128];
            if (width > 0)
                std::snprintf(buf, sizeof(buf), "%*.*f", width, dec, v);
            else
                std::snprintf(buf, sizeof(buf), "%.*f", dec, v);
            return Value::mkStr(buf);
        });
        // Ergaenzungen, die die Sprache abrunden
        add("STR$", 1, 1, [](BArgs& a) { return Value::mkStr(a.vals[0].text()); });
        add("VAL", 1, 1, [](BArgs& a) { return Value::mkFloat(std::atof(a.str(0).c_str())); });

        return t;
    }();
    return table;
}
