// GameBasiX - eingebaute Befehle und Funktionen
#include "builtins.h"
#include "interpreter.h"
#include "jobs.h"


#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

// ------------------------------------------------------------------- BArgs

void BArgs::require(size_t n) const {
    if (vals.size() < n) fail("Too few arguments");
}

[[noreturn]] void BArgs::fail(const std::string& msg) const {
    throw BasicError(name + ": " + msg, pos, false);
}

double BArgs::num(size_t i) const {
    if (i >= vals.size()) fail("Argument " + std::to_string(i + 1) + " missing");
    return vals[i].asDouble();
}

int64_t BArgs::integer(size_t i) const {
    if (i >= vals.size()) fail("Argument " + std::to_string(i + 1) + " missing");
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
    if (!p) fail("Output variable expected (argument " + std::to_string(i + 1) + ")");
    *p = std::move(v);
}

namespace {

constexpr double kPi = 3.14159265358979323846;

Value ok() { return Value::mkInt(0); }

// Dictionary-Argument pruefen; der Aufruf von fail() bricht ab.
DictObj* dictArg(BArgs& a, size_t i) {
    if (i >= a.vals.size() || !a.vals[i].isDict() || !a.vals[i].dict)
        a.fail("Dictionary als Argument " + std::to_string(i + 1) + " expected");
    return a.vals[i].dict.get();
}

Color colorOf(BArgs& a, size_t i) {
    return static_cast<Color>(a.integer(i));
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

// Jede geladene Grafik wird beobachtet: aendert sich die Datei, ersetzt der
// Interpreter sie unter derselben Nummer (Hot Reload).
void watchSprite(Runtime& rt, const std::string& path, const std::string& label, int id) {
    AssetWatch watch;
    watch.kind = AssetWatch::Kind::Sprite;
    watch.path = path;
    watch.label = label;
    watch.id = id;
    rt.hot.watch(watch);
}

// --------------------------------------------------------------- Dateien

int channelArg(BArgs& a, size_t i) {
    int64_t ch = a.integer(i);
    if (ch <= 0) a.fail("Channel must be greater than 0");
    return static_cast<int>(ch);
}

int readChannel(BArgs& a, size_t i) {
    int ch = channelArg(a, i);
    if (!a.rt.files.readable(ch))
        a.fail("Channel " + std::to_string(ch) + " is not open for reading");
    return ch;
}

int writeChannel(BArgs& a, size_t i) {
    int ch = channelArg(a, i);
    if (!a.rt.files.writable(ch))
        a.fail("Channel " + std::to_string(ch) + " is not open for writing");
    return ch;
}

// Liest count Bytes als vorzeichenlose Zahl (kleine Endian-Reihenfolge).
uint64_t readBits(BArgs& a, int ch, size_t count) {
    unsigned char buf[8] = {0};
    size_t got = 0;
    a.rt.files.readBytes(ch, buf, count, got);
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i)
        value |= static_cast<uint64_t>(buf[i]) << (8 * i);
    return value;
}

void writeBits(BArgs& a, int ch, int64_t value, size_t count) {
    unsigned char buf[8];
    uint64_t bits = static_cast<uint64_t>(value);
    for (size_t i = 0; i < count; ++i)
        buf[i] = static_cast<unsigned char>((bits >> (8 * i)) & 0xFF);
    if (!a.rt.files.writeBytes(ch, buf, count)) a.fail("Write failed");
}

std::string readText(BArgs& a, int ch, size_t count) {
    std::string out(count, '\0');
    size_t got = 0;
    a.rt.files.readBytes(ch, out.data(), count, got);
    out.resize(got);
    return out;
}

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
        add("DELTA_TIME", 0, 0, [](BArgs& a) { return Value::mkFloat(a.rt.gfx.timerFrame()); });
        add("ELAPSED_TIME", 0, 0, [](BArgs& a) { return Value::mkFloat(a.rt.gfx.timerAll()); });
        add("KEY_WAIT", 0, 0, [](BArgs& a) {
            a.rt.gfx.waitKey();
            return ok();
        });
        add("MOUSE_WAIT", 0, 0, [](BArgs& a) {
            a.rt.gfx.waitMouse();
            return ok();
        });
        add("COMMAND_LINE$", 0, 0, [](BArgs& a) { return Value::mkStr(a.rt.commandLine); });
        add("SOURCE_LINE", 0, 0, [](BArgs& a) { return Value::mkInt(a.pos.line); });
        add("SOURCE_FILE$", 0, 0, [](BArgs& a) { return Value::mkStr(a.pos.file); });
        add("PLATFORM_INFO$", 1, 1, [](BArgs& a) {
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
        add("ENV$", 1, 1, [](BArgs& a) {
            const char* v = std::getenv(a.str(0).c_str());
            return Value::mkStr(v ? v : "");
        });
        add("SET_ENV", 2, 2, [](BArgs& a) {
#ifdef _WIN32
            _putenv_s(a.str(0).c_str(), a.str(1).c_str());
#else
            setenv(a.str(0).c_str(), a.str(1).c_str(), 1);
#endif
            return ok();
        });
        add("TASK_START", 1, 1, [](BArgs& a) {
            if (!a.rt.interp) a.fail("no interpreter");
            return Value::mkInt(a.rt.interp->startTask(a.str(0), a.pos));
        });
        add("TASK_STOP", 1, 1, [](BArgs& a) {
            if (!a.rt.interp) a.fail("no interpreter");
            return Value::mkInt(a.rt.interp->stopTask(static_cast<int>(a.integer(0))) ? 1 : 0);
        });
        add("TASK_RUNNING", 1, 1, [](BArgs& a) {
            if (!a.rt.interp) return Value::mkInt(0);
            return Value::mkInt(a.rt.interp->taskRunning(static_cast<int>(a.integer(0))) ? 1 : 0);
        });
        add("TASK_COUNT", 0, 0, [](BArgs& a) {
            if (!a.rt.interp) return Value::mkInt(0);
            return Value::mkInt(a.rt.interp->taskCount());
        });
        add("ASSERT", 1, 2, [](BArgs& a) {
            ++a.rt.assertCount;
            if (!a.truthy(0)) {
                ++a.rt.assertFailures;
                std::string msg = a.vals.size() > 1 ? a.str(1) : "";
                std::fprintf(stderr, "ASSERT FAILED %s:%d%s%s\n",
                             a.pos.file.c_str(),
                             a.pos.line, msg.empty() ? "" : ": ", msg.c_str());
            }
            return ok();
        });
        add("WAIT_INPUT", 0, 0, [](BArgs& a) {
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
        add("ALLOW_ESCAPE", 0, 1, [](BArgs& a) {
            a.rt.gfx.setAllowEscape(a.truthy(0));
            return ok();
        });
        add("SET_SYSTEM_POINTER", 0, 1, [](BArgs& a) {
            // Ohne Argument bleibt der Zeiger sichtbar; FALSE versteckt ihn.
            a.rt.gfx.showCursor(a.vals.empty() ? true : a.truthy(0));
            return ok();
        });
        add("SOUND_STOP_ALL", 0, 0, [](BArgs& a) {
            a.rt.audio.hush();
            return ok();
        });

        // ----------------------------------------------------------- Fenster
        add("SCREEN_SET", 1, 3, [](BArgs& a) {
            int w = static_cast<int>(a.integer(0));
            int h = a.vals.size() > 1 ? static_cast<int>(a.integer(1)) : 0;
            int fs = a.vals.size() > 2 ? static_cast<int>(a.integer(2)) : -1;
            a.rt.gfx.setScreen(w, h, fs);
            return ok();
        });
        add("WINDOW_TITLE", 1, 1, [](BArgs& a) {
            a.rt.gfx.setTitle(a.str(0));
            return ok();
        });
        add("SCREEN_CLEAR", 0, 1, [](BArgs& a) {
            int64_t col = a.vals.empty() ? 0 : a.integer(0);
            if (col == -1) {
                a.rt.gfx.setClearColor(static_cast<Color>(-1));
                return ok();
            }
            a.rt.gfx.clearScreen(static_cast<Color>(col));
            return ok();
        });
        add("SCREEN_BLACK", 0, 0, [](BArgs& a) {
            a.rt.gfx.blackScreen();
            return ok();
        });
        add("SCREEN_SHOW", 0, 0, [](BArgs& a) {
            if (a.rt.interp && a.rt.interp->inTask())
                a.fail("SCREEN_SHOW is not allowed inside a TASK; use AWAIT FRAME");
            a.rt.gfx.present();
            if (a.rt.gfx.quitRequested()) a.rt.quit = true;
            return ok();
        });
        add("LIMIT_FPS", 1, 2, [](BArgs& a) {
            a.rt.gfx.setLimitFps(a.num(0), a.vals.size() > 1 ? a.truthy(1) : false);
            return ok();
        });
        add("SCREEN_SIZE", 2, 2, [](BArgs& a) {
            a.setOut(0, Value::mkInt(a.rt.gfx.screenWidth()));
            a.setOut(1, Value::mkInt(a.rt.gfx.screenHeight()));
            return ok();
        });
        add("SCREEN_WIDTH", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.screenWidth()); });
        add("SCREEN_HEIGHT", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.screenHeight()); });
        add("DESKTOP_SIZE", 2, 2, [](BArgs& a) {
            int w = 0, h = 0;
            a.rt.gfx.desktopSize(w, h);
            a.setOut(0, Value::mkInt(w));
            a.setOut(1, Value::mkInt(h));
            return ok();
        });
        add("SCREEN_FULLSCREEN", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.isFullscreen()); });
        add("SET_VIEWPORT", 4, 4, [](BArgs& a) {
            a.rt.gfx.setViewport(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                                 static_cast<int>(a.integer(2)), static_cast<int>(a.integer(3)));
            return ok();
        });
        add("VIEWPORT", 4, 4, [](BArgs& a) {
            int x, y, w, h;
            a.rt.gfx.getViewport(x, y, w, h);
            a.setOut(0, Value::mkInt(x));
            a.setOut(1, Value::mkInt(y));
            a.setOut(2, Value::mkInt(w));
            a.setOut(3, Value::mkInt(h));
            return ok();
        });
        add("SCREEN_SCALE", 0, 2, [](BArgs& a) {
            if (a.vals.size() < 2) a.rt.gfx.setPrescaler(0, 0);
            else a.rt.gfx.setPrescaler(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)));
            return ok();
        });
        add("CAMERA_BEGIN", 2, 4, [](BArgs& a) {
            const float zoom = a.vals.size() > 2 ? static_cast<float>(a.num(2)) : 1.0f;
            const double angle = a.vals.size() > 3 ? a.num(3) : 0.0;
            std::string err;
            if (!a.rt.gfx.beginCamera(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)),
                                      zoom, angle, err))
                a.fail(err);
            return ok();
        });
        add("CAMERA_END", 0, 0, [](BArgs& a) {
            a.rt.gfx.endCamera();
            return ok();
        });
        add("CAMERA_SCREEN_TO_WORLD", 4, 4, [](BArgs& a) {
            float wx = 0.0f, wy = 0.0f;
            a.rt.gfx.cameraScreenToWorld(static_cast<float>(a.num(0)),
                                         static_cast<float>(a.num(1)), wx, wy);
            a.setOut(2, Value::mkFloat(wx));
            a.setOut(3, Value::mkFloat(wy));
            return ok();
        });
        add("CAMERA_WORLD_TO_SCREEN", 4, 4, [](BArgs& a) {
            float sx = 0.0f, sy = 0.0f;
            a.rt.gfx.cameraWorldToScreen(static_cast<float>(a.num(0)),
                                         static_cast<float>(a.num(1)), sx, sy);
            a.setOut(2, Value::mkFloat(sx));
            a.setOut(3, Value::mkFloat(sy));
            return ok();
        });
        add("SET_SMOOTHING", 0, 1, [](BArgs& a) {
            a.rt.gfx.setSmoothShading(a.truthy(0));
            return ok();
        });
        add("SET_ALPHA_MODE", 0, 1, [](BArgs& a) {
            a.rt.gfx.setAlphaMode(a.vals.empty() ? 0.0 : a.num(0));
            return ok();
        });
        add("SET_ALPHA_TEST", 0, 1, [](BArgs& a) {
            a.rt.gfx.setAlphaTesting(a.vals.empty() ? 1.0 : a.num(0));
            return ok();
        });
        add("SET_COLOR_KEY", 1, 1, [](BArgs& a) {
            a.rt.gfx.setTransparency(colorOf(a, 0));
            return ok();
        });
        add("TEXT_COLOR", 1, 1, [](BArgs& a) {
            a.rt.textColor = colorOf(a, 0);
            return ok();
        });

        // ---------------------------------------------------------- Zeichnen
        add("RGB", 3, 4, [](BArgs& a) {
            const int64_t rgb = rgbInt(a.integer(0), a.integer(1), a.integer(2));
            if (a.vals.size() < 4) return Value::mkInt(rgb);
            const uint8_t alpha = static_cast<uint8_t>(clampi(a.integer(3), 0, 255));
            return Value::mkInt(static_cast<int64_t>(makeColor(static_cast<uint32_t>(rgb), alpha)));
        });
        add("DRAW_LINE", 5, 5, [](BArgs& a) {
            a.rt.gfx.drawLine(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)),
                              static_cast<float>(a.num(2)), static_cast<float>(a.num(3)),
                              colorOf(a, 4));
            return ok();
        });
        add("DRAW_RECT", 5, 5, [](BArgs& a) {
            a.rt.gfx.drawRect(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)),
                              static_cast<float>(a.num(2)), static_cast<float>(a.num(3)),
                              colorOf(a, 4));
            return ok();
        });
        add("DRAW_RECT_BORDER", 5, 5, [](BArgs& a) {
            float x = static_cast<float>(a.num(0)), y = static_cast<float>(a.num(1));
            float w = static_cast<float>(a.num(2)), h = static_cast<float>(a.num(3));
            Color c = colorOf(a, 4);
            a.rt.gfx.drawLine(x, y, x + w, y, c);
            a.rt.gfx.drawLine(x + w, y, x + w, y + h, c);
            a.rt.gfx.drawLine(x + w, y + h, x, y + h, c);
            a.rt.gfx.drawLine(x, y + h, x, y, c);
            return ok();
        });
        add("DRAW_CIRCLE", 4, 5, [](BArgs& a) {
            Color c = a.vals.size() > 3 ? colorOf(a, 3) : 0xFFFFFF;
            a.rt.gfx.drawCircle(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)),
                                static_cast<float>(a.num(2)), c, false);
            return ok();
        });
        add("DRAW_CIRCLE_FILLED", 4, 5, [](BArgs& a) {
            Color c = a.vals.size() > 3 ? colorOf(a, 3) : 0xFFFFFF;
            a.rt.gfx.drawCircle(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)),
                                static_cast<float>(a.num(2)), c, true);
            return ok();
        });
        add("PIXEL_SET", 3, 3, [](BArgs& a) {
            a.rt.gfx.setPixel(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                              colorOf(a, 2));
            return ok();
        });
        add("PIXEL_GET", 2, 2, [](BArgs& a) {
            return Value::mkInt(a.rt.gfx.getPixel(static_cast<int>(a.integer(0)),
                                                  static_cast<int>(a.integer(1))));
        });
        add("POLY_BEGIN", 0, 2, [](BArgs& a) {
            int sprite = a.vals.empty() ? -1 : static_cast<int>(a.integer(0));
            int mode = a.vals.size() > 1 ? static_cast<int>(a.integer(1)) : 0;
            a.rt.gfx.startPoly(sprite, mode);
            return ok();
        });
        add("POLY_POINT", 2, 5, [](BArgs& a) {
            float tx = a.vals.size() > 2 ? static_cast<float>(a.num(2)) : 0.0f;
            float ty = a.vals.size() > 3 ? static_cast<float>(a.num(3)) : 0.0f;
            Color c = a.vals.size() > 4 ? colorOf(a, 4) : 0xFFFFFF;
            a.rt.gfx.polyVector(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)), tx, ty, c);
            return ok();
        });
        add("POLY_STRIP", 0, 0, [](BArgs& a) {
            a.rt.gfx.startPoly(-1, 2);
            return ok();
        });
        add("POLY_END", 0, 0, [](BArgs& a) {
            a.rt.gfx.endPoly();
            return ok();
        });

        // ------------------------------------------------------------ Sprites
        add("SPRITE_NEW", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.nextSpriteId()); });
        add("SOUND_NEW", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.audio.nextId()); });
        add("FONT_NEW", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.nextFontId()); });
        add("SCREEN_NEW", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.nextScreenId()); });
        add("SPRITE_LOAD", 2, 2, [](BArgs& a) {
            std::string err;
            const std::string path = ASSET(0);
            const int id = static_cast<int>(a.integer(1));
            if (!a.rt.gfx.loadSprite(path, id, err)) a.fail(err);
            watchSprite(a.rt, path, a.str(0), id);
            return ok();
        });
        add("ANIM_LOAD", 4, 4, [](BArgs& a) {
            std::string err;
            const std::string path = ASSET(0);
            int id = static_cast<int>(a.integer(1));
            if (!a.rt.gfx.loadSprite(path, id, err)) a.fail(err);
            a.rt.gfx.setSpriteAnim(id, static_cast<int>(a.integer(2)), static_cast<int>(a.integer(3)));
            watchSprite(a.rt, path, a.str(0), id);
            return ok();
        });
        add("ANIM_SET_TILES", 3, 3, [](BArgs& a) {
            a.rt.gfx.setSpriteAnim(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                                   static_cast<int>(a.integer(2)));
            return ok();
        });
        add("SPRITE_DRAW", 3, 3, [](BArgs& a) {
            a.rt.gfx.drawSprite(static_cast<int>(a.integer(0)), static_cast<float>(a.num(1)),
                                static_cast<float>(a.num(2)));
            return ok();
        });
        add("SPRITE_DRAW_ROTATED", 4, 4, [](BArgs& a) {
            a.rt.gfx.drawSpriteRotozoom(static_cast<int>(a.integer(0)), static_cast<float>(a.num(1)),
                                        static_cast<float>(a.num(2)), a.num(3), 100.0f);
            return ok();
        });
        add("SPRITE_DRAW_ROTATED_SCALED", 5, 5, [](BArgs& a) {
            a.rt.gfx.drawSpriteRotozoom(static_cast<int>(a.integer(0)), static_cast<float>(a.num(1)),
                                        static_cast<float>(a.num(2)), a.num(3),
                                        static_cast<float>(a.num(4)));
            return ok();
        });
        add("SPRITE_DRAW_SCALED", 5, 5, [](BArgs& a) {
            a.rt.gfx.drawSpriteZoom(static_cast<int>(a.integer(0)), static_cast<float>(a.num(1)),
                                    static_cast<float>(a.num(2)), static_cast<float>(a.num(3)),
                                    static_cast<float>(a.num(4)));
            return ok();
        });
        add("SPRITE_DRAW_STRETCHED", 5, 5, [](BArgs& a) {
            int id = static_cast<int>(a.integer(0));
            a.rt.gfx.drawSpriteZoom(id, static_cast<float>(a.num(1)), static_cast<float>(a.num(2)),
                                    100.0f * static_cast<float>(a.num(3)) /
                                        std::max(1, a.rt.gfx.spriteWidth(id)),
                                    100.0f * static_cast<float>(a.num(4)) /
                                        std::max(1, a.rt.gfx.spriteHeight(id)));
            return ok();
        });
        add("ANIM_DRAW", 4, 4, [](BArgs& a) {
            a.rt.gfx.drawAnim(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                              static_cast<float>(a.num(2)), static_cast<float>(a.num(3)));
            return ok();
        });
        add("ANIM_DRAW_ROTATED_SCALED", 6, 6, [](BArgs& a) {
            a.rt.gfx.drawAnimRotozoom(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                                      static_cast<float>(a.num(2)), static_cast<float>(a.num(3)),
                                      a.num(4), static_cast<float>(a.num(5)));
            return ok();
        });
        add("ANIM_DRAW_STRETCHED", 6, 6, [](BArgs& a) {
            a.rt.gfx.drawAnimStretch(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                                     static_cast<float>(a.num(2)), static_cast<float>(a.num(3)),
                                     static_cast<float>(a.num(4)), static_cast<float>(a.num(5)));
            return ok();
        });
        add("SPRITE_SIZE", 3, 3, [](BArgs& a) {
            int w = 0, h = 0;
            a.rt.gfx.spriteInfo(static_cast<int>(a.integer(0)), w, h);
            a.setOut(1, Value::mkInt(w));
            a.setOut(2, Value::mkInt(h));
            return ok();
        });
        add("SPRITE_GRAB", 5, 5, [](BArgs& a) {
            std::string err;
            if (!a.rt.gfx.grabSprite(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                                     static_cast<int>(a.integer(2)), static_cast<int>(a.integer(3)),
                                     static_cast<int>(a.integer(4)), err))
                a.fail(err);
            return ok();
        });
        add("SPRITE_SAVE", 2, 2, [](BArgs& a) {
            std::string err;
            if (!a.rt.gfx.saveSprite(outputPath(a.rt, a.str(0)), static_cast<int>(a.integer(1)), err))
                a.fail(err);
            return ok();
        });
        add("SPRITE_COLLIDE", 6, 6, [](BArgs& a) {
            return Value::mkInt(a.rt.gfx.spriteCollision(
                static_cast<int>(a.integer(0)), static_cast<float>(a.num(1)),
                static_cast<float>(a.num(2)), static_cast<int>(a.integer(3)),
                static_cast<float>(a.num(4)), static_cast<float>(a.num(5))));
        });
        add("ANIM_COLLIDE", 8, 8, [](BArgs& a) {
            return Value::mkInt(a.rt.gfx.animCollision(
                static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                static_cast<float>(a.num(2)), static_cast<float>(a.num(3)),
                static_cast<int>(a.integer(4)), static_cast<int>(a.integer(5)),
                static_cast<float>(a.num(6)), static_cast<float>(a.num(7))));
        });
        add("RECT_COLLIDE", 8, 8, [](BArgs& a) {
            double x1 = a.num(0), y1 = a.num(1), w1 = a.num(2), h1 = a.num(3);
            double x2 = a.num(4), y2 = a.num(5), w2 = a.num(6), h2 = a.num(7);
            bool hit = x1 < x2 + w2 && x2 < x1 + w1 && y1 < y2 + h2 && y2 < y1 + h1;
            return Value::mkInt(hit ? 1 : 0);
        });

        // -------------------------------------------------------- Bildpuffer
        add("BACKGROUND_LOAD", 1, 1, [](BArgs& a) {
            std::string err;
            const std::string path = ASSET(0);
            if (!a.rt.gfx.loadBackground(path, err)) a.fail(err);
            AssetWatch watch;
            watch.kind = AssetWatch::Kind::Background;
            watch.path = path;
            watch.label = a.str(0);
            a.rt.hot.watch(watch);
            return ok();
        });
        add("SCREEN_SAVE_BMP", 1, 1, [](BArgs& a) {
            std::string err;
            if (!a.rt.gfx.saveBmp(outputPath(a.rt, a.str(0)), err)) a.fail(err);
            return ok();
        });
        add("SCREEN_TO_BACKGROUND", 0, 0, [](BArgs& a) {
            a.rt.gfx.useAsBackground();
            return ok();
        });
        add("SCREEN_FADE", 1, 2, [](BArgs& a) {
            a.rt.gfx.blendScreen(ASSET(0), a.vals.size() > 1 ? static_cast<int>(a.integer(1)) : 1000);
            return ok();
        });
        add("SCREEN_CREATE", 4, 4, [](BArgs& a) {
            a.rt.gfx.createScreen(static_cast<int>(a.integer(0)), static_cast<int>(a.integer(1)),
                                  static_cast<int>(a.integer(2)), static_cast<int>(a.integer(3)));
            return ok();
        });
        add("SCREEN_USE", 1, 1, [](BArgs& a) {
            a.rt.gfx.useScreen(static_cast<int>(a.integer(0)));
            return ok();
        });

        // ------------------------------------------------------------- Text
        add("PRINT", 3, 4, [](BArgs& a) {
            a.rt.gfx.drawText(a.str(0), static_cast<float>(a.num(1)), static_cast<float>(a.num(2)),
                              a.rt.textColor);
            return ok();
        });
        add("TEXT_DRAW", 3, 4, [](BArgs& a) {
            Color c = a.vals.size() > 3 ? colorOf(a, 3) : a.rt.textColor;
            a.rt.gfx.drawText(a.str(0), static_cast<float>(a.num(1)), static_cast<float>(a.num(2)), c);
            return ok();
        });
        add("FONT_LOAD", 2, 3, [](BArgs& a) {
            std::string file = a.str(0);
            int id = static_cast<int>(a.integer(1));
            int size = a.vals.size() > 2 ? static_cast<int>(a.integer(2)) : 0;
            std::string ext = lowerExt(file);
            std::string err;
            bool ttf = ext == "ttf" || ext == "otf" || (ext.empty() && size > 0);
            std::string path = ASSET(0);
            int points = 0;
            if (ttf) {
                if (ext.empty()) path = a.rt.resolveAsset(file + ".ttf");
                points = size > 0 ? size : 16;
                if (!a.rt.gfx.loadTtfFont(path, id, points, err)) a.fail(err);
            } else {
                if (!a.rt.gfx.loadBitmapFont(path, id, err)) a.fail(err);
            }
            AssetWatch watch;
            watch.kind = AssetWatch::Kind::Font;
            watch.path = path;
            watch.label = file;
            watch.id = id;
            watch.points = points;
            a.rt.hot.watch(watch);
            return ok();
        });
        add("FONT_SET", 1, 3, [](BArgs& a) {
            a.rt.gfx.setFont(static_cast<int>(a.integer(0)));
            if (a.vals.size() > 1) a.rt.gfx.setFontScale(static_cast<int>(a.integer(1)));
            if (a.vals.size() > 2) a.rt.textColor = colorOf(a, 2);
            return ok();
        });
        add("FONT_SIZE", 2, 2, [](BArgs& a) {
            int w = 0, h = 0;
            a.rt.gfx.fontCharSize(w, h);
            a.setOut(0, Value::mkInt(w));
            a.setOut(1, Value::mkInt(h));
            return ok();
        });
        add("FONT_CURRENT", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.currentFont()); });
        add("TEXT_WIDTH", 1, 1, [](BArgs& a) {
            int w = 0, h = 0;
            a.rt.gfx.textSize(a.str(0), w, h);
            return Value::mkInt(w);
        });
        add("TEXT_HEIGHT", 1, 1, [](BArgs& a) {
            int w = 0, h = 0;
            a.rt.gfx.textSize(a.str(0), w, h);
            return Value::mkInt(h);
        });
        add("INPUT", 1, 3, [](BArgs& a) {
            Value* target = a.out(0);
            if (!target) a.fail("expects a variable as first argument");
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
        add("KEY_DOWN", 1, 1, [](BArgs& a) {
            return Value::mkInt(a.rt.gfx.keyDown(static_cast<int>(a.integer(0))) ? 1 : 0);
        });
        add("KEY_PRESSED", 1, 1, [](BArgs& a) {
            return Value::mkInt(a.rt.gfx.keyHit(static_cast<int>(a.integer(0))) ? 1 : 0);
        });
        add("INKEY$", 0, 0, [](BArgs& a) { return Value::mkStr(a.rt.gfx.takeInkey()); });
        add("MOUSE_STATE", 4, 4, [](BArgs& a) {
            float x, y;
            bool l, r, m;
            a.rt.gfx.mouseState(x, y, l, r, m);
            a.setOut(0, Value::mkInt(static_cast<int64_t>(x)));
            a.setOut(1, Value::mkInt(static_cast<int64_t>(y)));
            a.setOut(2, Value::mkInt(l ? 1 : 0));
            a.setOut(3, Value::mkInt(r ? 1 : 0));
            return ok();
        });
        add("MOUSE_AXIS", 1, 1, [](BArgs& a) {
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
        add("MOUSE_PRESSED", 1, 1, [](BArgs& a) {
            return Value::mkInt(a.rt.gfx.mouseHit(static_cast<int>(a.integer(0))) ? 1 : 0);
        });
        add("MOUSE_COUNT", 0, 0, [](BArgs&) { return Value::mkInt(1); });
        add("MOUSE_MOVE", 2, 2, [](BArgs& a) {
            a.rt.gfx.setMouse(static_cast<float>(a.num(0)), static_cast<float>(a.num(1)));
            return ok();
        });
        add("JOY_COUNT", 0, 0, [](BArgs& a) { return Value::mkInt(a.rt.gfx.joyCount()); });
        add("JOY_AXIS", 2, 2, [](BArgs& a) {
            return Value::mkFloat(a.rt.gfx.joyAxis(static_cast<int>(a.integer(0)),
                                                   static_cast<int>(a.integer(1))));
        });
        add("JOY_BUTTON", 2, 2, [](BArgs& a) {
            int pad = static_cast<int>(a.integer(0)), button = static_cast<int>(a.integer(1));
            return Value::mkInt(a.rt.gfx.joyDown(pad, button) ? 1 : 0);
        });
        add("JOY_PRESSED", 2, 2, [](BArgs& a) {
            int pad = static_cast<int>(a.integer(0)), button = static_cast<int>(a.integer(1));
            return Value::mkInt(a.rt.gfx.joyHit(pad, button) ? 1 : 0);
        });

        // ------------------------------------------------------------ Sound
        add("SOUND_LOAD", 2, 3, [](BArgs& a) {
            std::string err;
            const std::string path = ASSET(0);
            const int id = static_cast<int>(a.integer(1));
            int buffers = a.vals.size() > 2 ? static_cast<int>(a.integer(2)) : 1;
            if (!a.rt.audio.loadSound(path, id, buffers, err)) a.fail(err);
            AssetWatch watch;
            watch.kind = AssetWatch::Kind::Sound;
            watch.path = path;
            watch.label = a.str(0);
            watch.id = id;
            watch.buffers = buffers;
            a.rt.hot.watch(watch);
            return ok();
        });
        add("SOUND_PLAY", 1, 3, [](BArgs& a) {
            double pan = a.vals.size() > 1 ? a.num(1) : 0.0;
            double vol = a.vals.size() > 2 ? a.num(2) : 1.0;
            return Value::mkInt(a.rt.audio.playSound(static_cast<int>(a.integer(0)), pan, vol));
        });
        add("SOUND_STOP", 1, 1, [](BArgs& a) {
            a.rt.audio.stopChannel(static_cast<int>(a.integer(0)));
            return ok();
        });
        add("SOUND_PLAYING", 1, 1, [](BArgs& a) {
            return Value::mkInt(a.rt.audio.channelPlaying(static_cast<int>(a.integer(0))) ? 1 : 0);
        });
        add("MUSIC_PLAY", 1, 2, [](BArgs& a) {
            std::string err;
            const std::string path = ASSET(0);
            bool loop = a.vals.size() > 1 ? a.truthy(1) : true;
            if (!a.rt.audio.playMusic(path, loop, err)) a.fail(err);
            AssetWatch watch;
            watch.kind = AssetWatch::Kind::Music;
            watch.path = path;
            watch.label = a.str(0);
            watch.loop = loop;
            a.rt.hot.watch(watch);
            return ok();
        });
        add("MUSIC_STOP", 0, 0, [](BArgs& a) {
            a.rt.audio.stopMusic();
            return ok();
        });
        add("MUSIC_PAUSE", 1, 1, [](BArgs& a) {
            a.rt.audio.pauseMusic(a.truthy(0));
            return ok();
        });
        add("MUSIC_PLAYING", 0, 0, [](BArgs& a) {
            return Value::mkInt(a.rt.audio.musicPlaying() ? 1 : 0);
        });
        add("MUSIC_VOLUME", 1, 1, [](BArgs& a) {
            a.rt.audio.setMusicVolume(a.num(0));
            return ok();
        });

        // ------------------------------------------------------------ Assets
        // Geaenderte Dateien laden: von selbst zwischen zwei Bildern, oder
        // sofort mit ASSET_RELOAD(). HOT_RELOAD off schaltet das Nachladen ab.
        add("ASSET_RELOAD", 0, 0, [](BArgs& a) {
            return Value::mkInt(a.rt.hot.reloadAll(a.rt));
        });
        add("HOT_RELOAD", 1, 1, [](BArgs& a) {
            a.rt.hot.setEnabled(a.truthy(0));
            return ok();
        });
        add("HOT_RELOAD_ON", 0, 0, [](BArgs& a) {
            return Value::mkInt(a.rt.hot.enabled() ? 1 : 0);
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
            if (v < 0) a.fail("SQR: negative number");
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
            if (b == 0) a.fail("Division by zero");
            return Value::mkInt(a.integer(0) % b);
        });
        add("FMOD", 2, 2, [](BArgs& a) {
            double b = a.num(1);
            if (b == 0) a.fail("Division by zero");
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
        add("RND_SEED", 1, 1, [](BArgs& a) {
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
        add("SHL", 2, 2, [](BArgs& a) {
            return Value::mkInt(static_cast<int64_t>(static_cast<uint64_t>(a.integer(0))
                                                     << (a.integer(1) & 63)));
        });
        add("SHR", 2, 2, [](BArgs& a) {
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
            if (a.vals[0].isDict() && a.vals[0].dict)
                return Value::mkInt(static_cast<int64_t>(a.vals[0].dict->size()));
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
        add("ARRAY_BOUNDS", 2, 2, [](BArgs& a) {
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

        // ------------------------------------------------------ Arrays ordnen
        // ARRAY_SORT(feld [, richtung [, feldname$]])
        // Sortiert an Ort und Stelle: 1 aufsteigend (Vorgabe), -1 absteigend.
        // Bei einem TYPE-Array nennt feldname$ das Vergleichsfeld.
        add("ARRAY_SORT", 1, 3, [](BArgs& a) {
            if (!a.vals[0].isArray() || !a.vals[0].arr)
                a.fail("array expected as first argument");
            auto arr = a.vals[0].arr;
            const bool desc = a.vals.size() > 1 && a.integer(1) < 0;
            const std::string key = a.vals.size() > 2 ? a.str(2) : std::string();
            auto field = [&](const Value& v) -> Value {
                if (key.empty()) return v;
                if (!v.isRecord() || !v.rec || !v.rec->def)
                    a.fail("ARRAY_SORT mit Feldname braucht ein TYPE-Array");
                // Feldnamen stehen im TYPE in Grossbuchstaben; der Aufrufer
                // darf sie trotzdem klein schreiben.
                const std::string want = upperAscii(key);
                int idx = -1;
                for (size_t fi = 0; fi < v.rec->def->fields.size(); ++fi)
                    if (upperAscii(v.rec->def->fields[fi].name) == want) {
                        idx = static_cast<int>(fi);
                        break;
                    }
                if (idx < 0) {
                    std::string known;
                    for (const auto& f : v.rec->def->fields) {
                        if (!known.empty()) known += ", ";
                        known += f.name;
                    }
                    a.fail("TYPE '" + v.rec->def->name + "' has no field '" + key +
                           (known.empty() ? "' (der TYPE hat keine Felder)" : "' (Felder: " + known + ")"));
                }
                return v.rec->fields[static_cast<size_t>(idx)];
            };
            auto less = [&](const Value& x, const Value& y) -> bool {
                const Value l = field(x), r = field(y);
                if (l.isStr() || r.isStr()) return l.text() < r.text();
                if (l.isNum() && r.isNum()) return l.asDouble() < r.asDouble();
                a.fail("ARRAY_SORT compares numbers and text only");
                return false;
            };
            std::stable_sort(arr->items.begin(), arr->items.end(),
                             [&](const Value& x, const Value& y) {
                                 return desc ? less(y, x) : less(x, y);
                             });
            return Value::mkInt(static_cast<int64_t>(arr->items.size()));
        });
        // ARRAY_FIND(feld, wert [, start]) -> Index oder -1
        add("ARRAY_FIND", 2, 3, [](BArgs& a) {
            if (!a.vals[0].isArray() || !a.vals[0].arr)
                a.fail("array expected as first argument");
            const auto& items = a.vals[0].arr->items;
            int64_t i = a.vals.size() > 2 ? a.integer(2) : 0;
            if (i < 0) i += static_cast<int64_t>(items.size());
            if (i < 0) i = 0;
            for (; i < static_cast<int64_t>(items.size()); ++i)
                if (valueEquals(items[static_cast<size_t>(i)], a.vals[1])) return Value::mkInt(i);
            return Value::mkInt(-1);
        });

        // -------------------------------------------------------- Dictionary
        add("DICT_NEW", 0, 0, [](BArgs&) {
            return Value::mkDict(std::make_shared<DictObj>());
        });
        add("DICT_SET", 3, 3, [](BArgs& a) {
            dictArg(a, 0)->set(a.str(1), a.vals[2]);
            return a.vals[0];
        });
        add("DICT_GET", 2, 3, [](BArgs& a) {
            DictObj* d = dictArg(a, 0);
            auto it = d->map.find(a.str(1));
            if (it != d->map.end()) return it->second;
            if (a.vals.size() > 2) return a.vals[2];
            return Value::mkInt(0);
        });
        add("DICT_HAS", 2, 2, [](BArgs& a) {
            return Value::mkInt(dictArg(a, 0)->has(a.str(1)) ? 1 : 0);
        });
        add("DICT_DELETE", 2, 2, [](BArgs& a) {
            DictObj* d = dictArg(a, 0);
            const std::string k = a.str(1);
            const bool had = d->has(k);
            d->erase(k);
            return Value::mkInt(had ? 1 : 0);
        });
        add("DICT_COUNT", 1, 1, [](BArgs& a) {
            return Value::mkInt(static_cast<int64_t>(dictArg(a, 0)->size()));
        });
        add("DICT_CLEAR", 1, 1, [](BArgs& a) {
            dictArg(a, 0)->clear();
            return ok();
        });
        add("DICT_KEY$", 2, 2, [](BArgs& a) {
            DictObj* d = dictArg(a, 0);
            int64_t i = a.integer(1);
            if (i < 0) i += static_cast<int64_t>(d->keys.size());
            if (i < 0 || i >= static_cast<int64_t>(d->keys.size()))
                a.fail("DICT_KEY$: Index ausserhalb");
            return Value::mkStr(d->keys[static_cast<size_t>(i)]);
        });
        add("DICT_VALUE", 2, 2, [](BArgs& a) {
            DictObj* d = dictArg(a, 0);
            int64_t i = a.integer(1);
            if (i < 0) i += static_cast<int64_t>(d->keys.size());
            if (i < 0 || i >= static_cast<int64_t>(d->keys.size()))
                a.fail("DICT_VALUE: Index ausserhalb");
            auto it = d->map.find(d->keys[static_cast<size_t>(i)]);
            return it == d->map.end() ? Value::mkNull() : it->second;
        });
        add("DICT_KEYS", 1, 1, [](BArgs& a) {
            DictObj* d = dictArg(a, 0);
            auto arr = std::make_shared<ArrayObj>();
            arr->elemVT = VT::Str;
            for (const auto& k : d->keys) arr->items.push_back(Value::mkStr(k));
            return Value::mkArray(arr);
        });
        add("DICT_VALUES", 1, 1, [](BArgs& a) {
            DictObj* d = dictArg(a, 0);
            auto arr = std::make_shared<ArrayObj>();
            for (const auto& k : d->keys) {
                auto it = d->map.find(k);
                if (it != d->map.end()) arr->items.push_back(it->second);
            }
            return Value::mkArray(arr);
        });

        // -------------------------------------------------- Typen zur Laufzeit
        // ISA(objekt, "NAME"): TYPE selbst oder eine seiner Basen.
        add("ISA", 2, 2, [](BArgs& a) {
            const std::string want = upperAscii(a.str(1));
            const Value& v = a.vals[0];
            if (v.isRecord() && v.rec && v.rec->def) {
                for (const TypeDef* t = v.rec->def; t; t = t->base) {
                    if (upperAscii(t->name) == want) return Value::mkInt(1);
                    // Ein INTERFACE gilt ebenfalls als erfuellt.
                    for (const auto& iface : t->interfaces)
                        if (upperAscii(iface) == want) return Value::mkInt(1);
                }
                return Value::mkInt(0);
            }
            if (want == "ARRAY") return Value::mkInt(v.isArray() ? 1 : 0);
            if (want == "DICT" || want == "DICTIONARY") return Value::mkInt(v.isDict() ? 1 : 0);
            if (want == "FUNCTION") return Value::mkInt(v.isFunc() ? 1 : 0);
            if (want == "STRING") return Value::mkInt(v.isStr() ? 1 : 0);
            if (want == "INTEGER") return Value::mkInt(v.type == VT::Int ? 1 : 0);
            if (want == "FLOAT" || want == "NUMBER") return Value::mkInt(v.type == VT::Float ? 1 : 0);
            return Value::mkInt(0);
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
        add("INSTR_REVERSE", 2, 3, [](BArgs& a) {
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
        add("SPLIT_STRING", 2, 4, [](BArgs& a) {
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
        // Gegenstueck zu SPLIT_STRING: Elemente mit einem Trenner verbinden.
        add("JOIN$", 2, 4, [](BArgs& a) {
            if (!a.vals[0].isArray() || !a.vals[0].arr)
                a.fail("array expected as first argument");
            const std::string sep = a.str(1);
            const auto& items = a.vals[0].arr->items;
            int64_t start = a.vals.size() > 2 ? a.integer(2) : 0;
            if (start < 0) start += static_cast<int64_t>(items.size());
            if (start < 0) start = 0;
            if (start > static_cast<int64_t>(items.size())) start = static_cast<int64_t>(items.size());
            int64_t end = static_cast<int64_t>(items.size());
            if (a.vals.size() > 3) {
                const int64_t count = a.integer(3);
                if (count >= 0 && start + count < end) end = start + count;
            }
            std::string out;
            for (int64_t i = start; i < end; ++i) {
                if (i > start) out += sep;
                out += items[static_cast<size_t>(i)].text();
            }
            return Value::mkStr(out);
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
        // Zwei Formen:
        //   FORMAT$(breite, nachkommastellen, zahl)  - Zahl mit fester Breite
        //   FORMAT$("tpl {0} von {1}", a, b)         - Vorlage mit Platzhaltern
        // Welche gilt, entscheidet der Typ des ersten Arguments. {{ und }}
        // stehen fuer eine geschweifte Klammer.
        // maxArgs -1: eine Vorlage darf beliebig viele Werte aufnehmen.
        add("FORMAT$", 1, -1, [](BArgs& a) {
            if (a.vals[0].isStr()) {
                const std::string tpl = a.str(0);
                std::string out;
                out.reserve(tpl.size() + 16);
                for (size_t i = 0; i < tpl.size(); ++i) {
                    const char c = tpl[i];
                    if (c == '{' && i + 1 < tpl.size() && tpl[i + 1] == '{') {
                        out += '{';
                        ++i;
                        continue;
                    }
                    if (c == '}' && i + 1 < tpl.size() && tpl[i + 1] == '}') {
                        out += '}';
                        ++i;
                        continue;
                    }
                    if (c == '{') {
                        size_t j = i + 1;
                        size_t index = 0;
                        bool digits = false;
                        while (j < tpl.size() && std::isdigit(static_cast<unsigned char>(tpl[j]))) {
                            index = index * 10 + static_cast<size_t>(tpl[j] - '0');
                            ++j;
                            digits = true;
                        }
                        if (digits && j < tpl.size() && tpl[j] == '}') {
                            if (index + 1 >= a.vals.size())
                                a.fail("placeholder {" + std::to_string(index) +
                                       "} has no argument");
                            out += a.vals[index + 1].text();
                            i = j;
                            continue;
                        }
                    }
                    out += c;
                }
                return Value::mkStr(out);
            }
            if (a.vals.size() < 3)
                a.fail("either a template with {0}, {1} ... or width, decimals and number");
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

        // ----------------------------------------------------------- Dateien
        add("FILE_OPEN", 3, 3, [](BArgs& a) {
            int ch = static_cast<int>(a.integer(0));
            std::string file = a.str(1);
            int mode = static_cast<int>(a.integer(2));
            if (ch <= 0) a.fail("Channel must be greater than 0");
            if (mode != 1 && mode != 0 && mode != -1)
                a.fail("Mode must be 1 (read), 0 (write) or -1 (append)");
            std::string path = mode == 1 ? a.rt.resolveAsset(file) : outputPath(a.rt, file);
            std::string err;
            if (!a.rt.files.open(ch, path, mode, err)) {
                a.rt.warnOnce("FILE_OPEN: " + err);
                return Value::mkInt(0);
            }
            return Value::mkInt(1);
        });
        add("FILE_CLOSE", 1, 1, [](BArgs& a) {
            int ch = channelArg(a, 0);
            return Value::mkInt(a.rt.files.close(ch) ? 1 : 0);
        });
        add("FILE_SEEK", 3, 3, [](BArgs& a) {
            int ch = channelArg(a, 0);
            if (!a.rt.files.isOpen(ch))
                a.fail("Channel " + std::to_string(ch) + " is not open");
            int origin = static_cast<int>(a.integer(2));
            if (origin < -1 || origin > 1)
                a.fail("Origin must be -1 (end), 0 (start) or 1 (position)");
            if (!a.rt.files.seek(ch, a.integer(1), origin)) a.fail("FILE_SEEK failed");
            return ok();
        });
        add("FILE_POSITION", 1, 1, [](BArgs& a) {
            int ch = channelArg(a, 0);
            if (!a.rt.files.isOpen(ch))
                a.fail("Channel " + std::to_string(ch) + " is not open");
            return Value::mkInt(a.rt.files.position(ch));
        });
        add("FILE_EOF", 1, 1, [](BArgs& a) {
            int ch = channelArg(a, 0);
            if (!a.rt.files.isOpen(ch))
                a.fail("Channel " + std::to_string(ch) + " is not open");
            return Value::mkInt(a.rt.files.atEnd(ch) ? 1 : 0);
        });

        add("FILE_READ_BYTE", 2, 2, [](BArgs& a) {
            int ch = readChannel(a, 0);
            int8_t v = static_cast<int8_t>(readBits(a, ch, 1) & 0xFF);
            a.setOut(1, Value::mkInt(v));
            return ok();
        });
        add("FILE_READ_UBYTE", 2, 2, [](BArgs& a) {
            int ch = readChannel(a, 0);
            a.setOut(1, Value::mkInt(static_cast<int64_t>(readBits(a, ch, 1))));
            return ok();
        });
        add("FILE_READ_WORD", 2, 2, [](BArgs& a) {
            int ch = readChannel(a, 0);
            int16_t v = static_cast<int16_t>(readBits(a, ch, 2));
            a.setOut(1, Value::mkInt(v));
            return ok();
        });
        add("FILE_READ_UWORD", 2, 2, [](BArgs& a) {
            int ch = readChannel(a, 0);
            a.setOut(1, Value::mkInt(static_cast<int64_t>(readBits(a, ch, 2))));
            return ok();
        });
        add("FILE_READ_LONG", 2, 2, [](BArgs& a) {
            int ch = readChannel(a, 0);
            int32_t v = static_cast<int32_t>(readBits(a, ch, 4));
            a.setOut(1, Value::mkInt(v));
            return ok();
        });
        add("FILE_READ_ULONG", 2, 2, [](BArgs& a) {
            int ch = readChannel(a, 0);
            a.setOut(1, Value::mkInt(static_cast<int64_t>(readBits(a, ch, 4))));
            return ok();
        });
        add("FILE_READ_DOUBLE", 2, 2, [](BArgs& a) {
            int ch = readChannel(a, 0);
            unsigned char buf[8] = {0};
            size_t got = 0;
            a.rt.files.readBytes(ch, buf, sizeof(buf), got);
            double v = 0.0;
            std::memcpy(&v, buf, sizeof(buf));
            a.setOut(1, Value::mkFloat(v));
            return ok();
        });
        add("FILE_READ_FLOAT", 2, 2, [](BArgs& a) {
            int ch = readChannel(a, 0);
            unsigned char buf[4] = {0};
            size_t got = 0;
            a.rt.files.readBytes(ch, buf, sizeof(buf), got);
            float v = 0.0f;
            std::memcpy(&v, buf, sizeof(buf));
            a.setOut(1, Value::mkFloat(v));
            return ok();
        });
        add("FILE_READ_STR", 3, 3, [](BArgs& a) {
            int ch = readChannel(a, 0);
            int64_t count = a.integer(2);
            if (count < 0) a.fail("Length must be 0 or greater");
            if (count > 64 * 1024 * 1024) a.fail("Length is too large");
            a.setOut(1, Value::mkStr(readText(a, ch, static_cast<size_t>(count))));
            return ok();
        });
        add("FILE_READ_LINE", 2, 2, [](BArgs& a) {
            int ch = readChannel(a, 0);
            std::string line;
            a.rt.files.readLine(ch, line);
            a.setOut(1, Value::mkStr(line));
            return ok();
        });

        add("FILE_WRITE_BYTE", 2, 2, [](BArgs& a) {
            int ch = writeChannel(a, 0);
            writeBits(a, ch, a.integer(1), 1);
            return ok();
        });
        add("FILE_WRITE_UBYTE", 2, 2, [](BArgs& a) {
            int ch = writeChannel(a, 0);
            writeBits(a, ch, a.integer(1), 1);
            return ok();
        });
        add("FILE_WRITE_WORD", 2, 2, [](BArgs& a) {
            int ch = writeChannel(a, 0);
            writeBits(a, ch, a.integer(1), 2);
            return ok();
        });
        add("FILE_WRITE_UWORD", 2, 2, [](BArgs& a) {
            int ch = writeChannel(a, 0);
            writeBits(a, ch, a.integer(1), 2);
            return ok();
        });
        add("FILE_WRITE_LONG", 2, 2, [](BArgs& a) {
            int ch = writeChannel(a, 0);
            writeBits(a, ch, a.integer(1), 4);
            return ok();
        });
        add("FILE_WRITE_ULONG", 2, 2, [](BArgs& a) {
            int ch = writeChannel(a, 0);
            writeBits(a, ch, a.integer(1), 4);
            return ok();
        });
        add("FILE_WRITE_DOUBLE", 2, 2, [](BArgs& a) {
            int ch = writeChannel(a, 0);
            double v = a.num(1);
            unsigned char buf[8];
            std::memcpy(buf, &v, sizeof(buf));
            if (!a.rt.files.writeBytes(ch, buf, sizeof(buf))) a.fail("Write failed");
            return ok();
        });
        add("FILE_WRITE_FLOAT", 2, 2, [](BArgs& a) {
            int ch = writeChannel(a, 0);
            float v = static_cast<float>(a.num(1));
            unsigned char buf[4];
            std::memcpy(buf, &v, sizeof(buf));
            if (!a.rt.files.writeBytes(ch, buf, sizeof(buf))) a.fail("Write failed");
            return ok();
        });
        add("FILE_WRITE_STR", 2, 2, [](BArgs& a) {
            int ch = writeChannel(a, 0);
            std::string text = a.str(1);
            if (!a.rt.files.writeBytes(ch, text.data(), text.size())) a.fail("Write failed");
            return ok();
        });
        add("FILE_WRITE_LINE", 2, 2, [](BArgs& a) {
            int ch = writeChannel(a, 0);
            std::string text = a.str(1) + "\r\n";
            if (!a.rt.files.writeBytes(ch, text.data(), text.size())) a.fail("Write failed");
            return ok();
        });

        // ------------------------------------------------- Jobs und async I/O
        add("SPAWN", 1, 32, [](BArgs& a) {
            if (!jobs::available()) a.fail("Background jobs are not available in this build");
            if (!a.rt.prog) a.fail("no program");
            const std::string target = a.vals[0].isFunc() ? a.vals[0].s : a.str(0);
            std::vector<Value> args(a.vals.begin() + 1, a.vals.end());
            return Value::mkInt(jobs::spawnFunction(*a.rt.prog, target, std::move(args),
                                                    a.rt.globals, a.pos));
        });
        add("JOB_DONE", 1, 1, [](BArgs& a) {
            return Value::mkInt(jobs::done(static_cast<int>(a.integer(0))) ? 1 : 0);
        });
        add("JOB_VALUE", 1, 1, [](BArgs& a) {
            std::string error;
            Value value = jobs::value(static_cast<int>(a.integer(0)), error);
            if (!error.empty()) a.fail(error);
            return value;
        });
        add("JOB_ERROR$", 1, 1, [](BArgs& a) {
            return Value::mkStr(jobs::error(static_cast<int>(a.integer(0))));
        });
        add("JOB_FREE", 1, 1, [](BArgs& a) {
            return Value::mkInt(jobs::release(static_cast<int>(a.integer(0))) ? 1 : 0);
        });
        add("FILE_READ_ASYNC", 1, 1, [](BArgs& a) {
            if (!jobs::available()) a.fail("Background jobs are not available in this build");
            const std::string path = a.rt.resolveAsset(a.str(0));
            const int id = jobs::submit([path]() {
                std::ifstream in(path, std::ios::binary);
                if (!in) throw BasicError("File not found: " + path, SrcPos{path, 0}, false);
                std::ostringstream text;
                text << in.rdbuf();
                if (!in && !in.eof()) throw BasicError("Read failed: " + path, SrcPos{path, 0}, false);
                return Value::mkStr(text.str());
            });
            if (id < 0) a.fail("Too many background jobs");
            return Value::mkInt(id);
        });
        add("FILE_WRITE_ASYNC", 2, 2, [](BArgs& a) {
            if (!jobs::available()) a.fail("Background jobs are not available in this build");
            const std::string path = outputPath(a.rt, a.str(0));
            std::string text = a.str(1);
            const int id = jobs::submit([path, text = std::move(text)]() {
                std::ofstream out(path, std::ios::binary);
                if (!out) throw BasicError("File cannot be written: " + path, SrcPos{path, 0}, false);
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
                if (!out) throw BasicError("Write failed: " + path, SrcPos{path, 0}, false);
                return Value::mkInt(static_cast<int64_t>(text.size()));
            });
            if (id < 0) a.fail("Too many background jobs");
            return Value::mkInt(id);
        });

        add("FILE_EXISTS", 1, 1, [](BArgs& a) {
            std::string path = a.rt.resolveAsset(a.str(0));
            std::error_code ec;
            return Value::mkInt(std::filesystem::is_regular_file(std::filesystem::path(path), ec) ? 1 : 0);
        });
        add("FILE_SIZE", 1, 1, [](BArgs& a) {
            std::string path = a.rt.resolveAsset(a.str(0));
            std::error_code ec;
            if (!std::filesystem::is_regular_file(std::filesystem::path(path), ec)) return Value::mkInt(-1);
            auto size = std::filesystem::file_size(std::filesystem::path(path), ec);
            if (ec) return Value::mkInt(-1);
            return Value::mkInt(static_cast<int64_t>(size));
        });
        add("FILE_COPY", 2, 2, [](BArgs& a) {
            std::string src = a.rt.resolveAsset(a.str(0));
            std::string dst = outputPath(a.rt, a.str(1));
            std::error_code ec;
            if (!std::filesystem::is_regular_file(std::filesystem::path(src), ec))
                a.fail("File not found: " + a.str(0));
            std::filesystem::copy_file(std::filesystem::path(src), std::filesystem::path(dst),
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) a.fail("Kopieren failed: " + ec.message());
            return Value::mkInt(1);
        });
        add("FILE_RENAME", 2, 2, [](BArgs& a) {
            std::string src = a.rt.resolveAsset(a.str(0));
            std::string dst = outputPath(a.rt, a.str(1));
            std::error_code ec;
            if (!std::filesystem::is_regular_file(std::filesystem::path(src), ec))
                a.fail("File not found: " + a.str(0));
            std::filesystem::rename(std::filesystem::path(src), std::filesystem::path(dst), ec);
            if (ec) a.fail("Umbenennen failed: " + ec.message());
            return Value::mkInt(1);
        });
        add("FILE_DELETE", 1, 1, [](BArgs& a) {
            std::string path = a.rt.resolveAsset(a.str(0));
            std::error_code ec;
            if (!std::filesystem::is_regular_file(std::filesystem::path(path), ec))
                a.fail("File not found: " + a.str(0));
            std::filesystem::remove(std::filesystem::path(path), ec);
            if (ec) a.fail("Loeschen failed: " + ec.message());
            return Value::mkInt(1);
        });
        add("DIR_MAKE", 1, 1, [](BArgs& a) {
            std::string path = outputPath(a.rt, a.str(0));
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(path), ec);
            if (ec) a.fail("Folder cannot be created: " + ec.message());
            return Value::mkInt(1);
        });

        // ------------------------------------------------------------ Netzwerk
        auto netName = [](Net::Kind kind) -> const char* {
            switch (kind) {
                case Net::Kind::Listener: return "Server";
                case Net::Kind::Stream: return "connection";
                case Net::Kind::Datagram: return "UDP-Socket";
            }
            return "Handle";
        };
        auto netWant = [netName](BArgs& a, Net::Kind want) -> int {
            const int handle = static_cast<int>(a.integer(0));
            if (!a.rt.net.known(handle))
                a.fail("unknown network handle " + std::to_string(handle));
            if (a.rt.net.kindOf(handle) != want)
                a.fail(std::string("Handle ") + std::to_string(handle) + " ist kein " +
                       netName(want));
            return handle;
        };

        add("NET_LISTEN", 1, 1, [](BArgs& a) {
            int handle = 0;
            std::string err;
            const int port = clampi(a.integer(0), 0, 65535);
            if (!a.rt.net.listen(port, handle, err)) {
                a.rt.netError = err;
                return Value::mkInt(0);
            }
            return Value::mkInt(handle);
        });
        add("NET_ACCEPT", 1, 1, [netWant](BArgs& a) {
            const int listener = netWant(a, Net::Kind::Listener);
            std::string err;
            const int handle = a.rt.net.accept(listener, err);
            if (handle < 0) {
                a.rt.netError = err;
                return Value::mkInt(-1);
            }
            return Value::mkInt(handle);
        });
        add("NET_CONNECT", 2, 3, [](BArgs& a) {
            const int port = clampi(a.integer(1), 1, 65535);
            const int timeout = a.vals.size() > 2 ? clampi(a.integer(2), 0, 60000) : 3000;
            int handle = 0;
            std::string err;
            if (!a.rt.net.connect(a.str(0), port, timeout, handle, err)) {
                a.rt.netError = err;
                return Value::mkInt(0);
            }
            return Value::mkInt(handle);
        });
        add("NET_SEND", 2, 2, [netWant](BArgs& a) {
            const int handle = netWant(a, Net::Kind::Stream);
            int sent = 0;
            std::string err;
            const bool ok = a.rt.net.sendData(handle, a.str(1), sent, err);
            if (!ok && sent == 0) {
                a.rt.netError = err;
                return Value::mkInt(0);
            }
            return Value::mkInt(sent);
        });
        add("NET_RECV$", 1, 1, [netWant](BArgs& a) {
            const int handle = netWant(a, Net::Kind::Stream);
            std::string err;
            const std::string data = a.rt.net.receive(handle, err);
            if (!err.empty()) a.rt.netError = err;
            return Value::mkStr(data);
        });
        add("NET_PENDING", 1, 1, [netWant](BArgs& a) {
            return Value::mkInt(a.rt.net.pending(netWant(a, Net::Kind::Stream)));
        });
        add("NET_CONNECTED", 1, 1, [netWant](BArgs& a) {
            return Value::mkInt(a.rt.net.connected(netWant(a, Net::Kind::Stream)) ? 1 : 0);
        });
        add("NET_CLOSE", 1, 1, [](BArgs& a) {
            const int handle = static_cast<int>(a.integer(0));
            if (!a.rt.net.known(handle)) {
                a.rt.netError = "unknown network handle " + std::to_string(handle);
                return Value::mkInt(0);
            }
            if (a.rt.net.kindOf(handle) == Net::Kind::Datagram) {
                a.rt.netError = "UDP-Sockets schliessen mit UDP_CLOSE";
                return Value::mkInt(0);
            }
            return Value::mkInt(a.rt.net.closeHandle(handle) ? 1 : 0);
        });
        add("NET_PEER$", 1, 1, [netWant](BArgs& a) {
            return Value::mkStr(a.rt.net.peerOf(netWant(a, Net::Kind::Stream)));
        });
        add("NET_PORT", 1, 1, [](BArgs& a) {
            const int handle = static_cast<int>(a.integer(0));
            if (!a.rt.net.known(handle)) {
                a.rt.netError = "unknown network handle " + std::to_string(handle);
                return Value::mkInt(0);
            }
            return Value::mkInt(a.rt.net.portOf(handle));
        });
        add("NET_IP$", 0, 0, [](BArgs& a) { return Value::mkStr(a.rt.net.localAddress()); });
        add("NET_ERROR$", 0, 0, [](BArgs& a) { return Value::mkStr(a.rt.netError); });

        add("UDP_OPEN", 1, 1, [](BArgs& a) {
            int handle = 0;
            std::string err;
            const int port = clampi(a.integer(0), 0, 65535);
            if (!a.rt.net.openDatagram(port, handle, err)) {
                a.rt.netError = err;
                return Value::mkInt(0);
            }
            return Value::mkInt(handle);
        });
        add("UDP_SEND", 4, 4, [netWant](BArgs& a) {
            const int handle = netWant(a, Net::Kind::Datagram);
            const int port = clampi(a.integer(2), 1, 65535);
            std::string err;
            if (!a.rt.net.sendDatagram(handle, a.str(1), port, a.str(3), err)) {
                a.rt.netError = err;
                return Value::mkInt(0);
            }
            return Value::mkInt(1);
        });
        add("UDP_RECV$", 1, 1, [netWant](BArgs& a) {
            const int handle = netWant(a, Net::Kind::Datagram);
            std::string from;
            std::string err;
            const std::string data = a.rt.net.receiveDatagram(handle, from, err);
            if (!err.empty()) a.rt.netError = err;
            if (!from.empty()) a.rt.netFrom = from;
            return Value::mkStr(data);
        });
        add("UDP_FROM$", 1, 1, [netWant](BArgs& a) {
            netWant(a, Net::Kind::Datagram);
            return Value::mkStr(a.rt.netFrom);
        });
        add("UDP_PORT", 1, 1, [netWant](BArgs& a) {
            return Value::mkInt(a.rt.net.portOf(netWant(a, Net::Kind::Datagram)));
        });
        add("UDP_CLOSE", 1, 1, [netWant](BArgs& a) {
            return Value::mkInt(a.rt.net.closeHandle(netWant(a, Net::Kind::Datagram)) ? 1 : 0);
        });

        return t;
    }();
    return table;
}
