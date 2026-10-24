// GameBasiX - SDL3-Grafik-, Text- und Eingabeschicht
#include "graphics.h"

#include "i18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
#include "font8x8_basic.h"
#pragma GCC diagnostic pop

namespace {

#ifdef GBX_HAVE_TTF
FT_Library g_ftLibrary = nullptr;
#endif

// UTF-8 in Codepoints zerlegen (fuer TTF-Schriften)
std::vector<uint32_t> utf8Codepoints(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { ++i; continue; }
        if (i + static_cast<size_t>(extra) >= s.size()) break;
        for (int k = 1; k <= extra; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
            if ((cc & 0xC0) != 0x80) { cp = '?'; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        out.push_back(cp);
        i += static_cast<size_t>(extra) + 1;
    }
    return out;
}

inline uint8_t redOf(uint32_t c) { return static_cast<uint8_t>((c >> 16) & 0xFF); }
inline uint8_t greenOf(uint32_t c) { return static_cast<uint8_t>((c >> 8) & 0xFF); }
inline uint8_t blueOf(uint32_t c) { return static_cast<uint8_t>(c & 0xFF); }

std::string lowerExt(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    for (char& c : ext)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return ext;
}

}  // namespace

// -------------------------------------------------------------- Initialisierung

bool Gfx::init(int w, int h, const std::string& title, bool fullscreen, bool resizable) {
    width_ = std::max(1, w);
    height_ = std::max(1, h);
    title_ = title;
    fullscreen_ = fullscreen;
    resizable_ = resizable;
    startTicks_ = SDL_GetTicks();
    lastPresent_ = startTicks_;
    frameStart_ = startTicks_;

    if (headless) {
        keyDown_.assign(SDL_SCANCODE_COUNT, false);
        keyPressed_.assign(SDL_SCANCODE_COUNT, false);
        return true;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, i18n::tr("SDL_Init fehlgeschlagen: %s\n").c_str(), SDL_GetError());
        return false;
    }
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (resizable_) flags |= SDL_WINDOW_RESIZABLE;
    if (fullscreen_) flags |= SDL_WINDOW_FULLSCREEN;

    window_ = SDL_CreateWindow(title_.c_str(), width_, height_, flags);
    if (!window_) {
        std::fprintf(stderr, i18n::tr("Fenster konnte nicht erstellt werden: %s\n").c_str(),
                     SDL_GetError());
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        std::fprintf(stderr, i18n::tr("Renderer konnte nicht erstellt werden: %s\n").c_str(),
                     SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer_, 1);
    vsync_ = true;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_StartTextInput(window_);
    SDL_HideCursor();
    cursorVisible_ = false;

    keys_ = SDL_GetKeyboardState(nullptr);
    keyDown_.assign(SDL_SCANCODE_COUNT, false);
    keyPressed_.assign(SDL_SCANCODE_COUNT, false);
    ensureDefaultFont();
    clearScreen(clearColor_);
    return true;
}

void Gfx::shutdown() {
    if (headless) return;
    for (auto& [id, s] : sprites_) {
        if (s.tex && s.ownsTexture) SDL_DestroyTexture(s.tex);
    }
    sprites_.clear();
    for (auto& [id, f] : fonts_) {
        if (f.atlas) SDL_DestroyTexture(f.atlas);
#ifdef GBX_HAVE_TTF
        if (f.face) FT_Done_Face(f.face);
#endif
    }
    fonts_.clear();
    for (auto& [k, g] : glyphCache_)
        if (g.tex) SDL_DestroyTexture(g.tex);
    glyphCache_.clear();
#ifdef GBX_HAVE_TTF
    if (g_ftLibrary) {
        FT_Done_FreeType(g_ftLibrary);
        g_ftLibrary = nullptr;
    }
#endif
    for (auto& [id, tex] : screens_) SDL_DestroyTexture(tex);
    screens_.clear();
    if (background_) SDL_DestroyTexture(background_);
    background_ = nullptr;
    if (renderer_) SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_Quit();
}

void Gfx::setScreen(int w, int h, int fullscreen) {
    int nw = w > 0 ? w : width_;
    int nh = h > 0 ? h : height_;
    bool fs = fullscreen >= 0 ? fullscreen != 0 : fullscreen_;
    if (headless) {
        width_ = nw;
        height_ = nh;
        fullscreen_ = fs;
        return;
    }
    if (!window_) return;
    width_ = nw;
    height_ = nh;
    SDL_SetWindowFullscreen(window_, fs);
    fullscreen_ = fs;
    if (!fs) SDL_SetWindowSize(window_, width_, height_);
    if (prescaleW_ > 0) setPrescaler(prescaleW_, prescaleH_);
}

void Gfx::setTitle(const std::string& title) {
    title_ = title;
    if (window_) SDL_SetWindowTitle(window_, title.c_str());
}

void Gfx::desktopSize(int& w, int& h) const {
    w = 1920;
    h = 1080;
    if (headless) return;
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(display);
    if (mode) {
        w = mode->w;
        h = mode->h;
    }
}

void Gfx::setPrescaler(int w, int h) {
    prescaleW_ = w;
    prescaleH_ = h;
    if (!renderer_) return;
    if (w > 0 && h > 0)
        SDL_SetRenderLogicalPresentation(renderer_, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    else
        SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    applyViewport();
}

void Gfx::setViewport(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) {
        haveViewport_ = false;
        viewport_ = SDL_Rect{0, 0, 0, 0};
    } else {
        haveViewport_ = true;
        viewport_ = SDL_Rect{x, y, w, h};
    }
    applyViewport();
}

void Gfx::getViewport(int& x, int& y, int& w, int& h) const {
    if (haveViewport_) {
        x = viewport_.x;
        y = viewport_.y;
        w = viewport_.w;
        h = viewport_.h;
    } else {
        x = 0;
        y = 0;
        w = width_;
        h = height_;
    }
}

int Gfx::nextSpriteId() const {
    for (int id = 0; id < 4096; ++id)
        if (sprites_.find(id) == sprites_.end()) return id;
    return -1;
}

int Gfx::nextFontId() const {
    for (int id = 0; id < 256; ++id)
        if (fonts_.find(id) == fonts_.end()) return id;
    return -1;
}

int Gfx::nextScreenId() const {
    for (int id = 0; id < 32; ++id)
        if (screens_.find(id) == screens_.end()) return id;
    return -1;
}

void Gfx::applyViewport() {
    if (!renderer_) return;
    if (haveViewport_)
        SDL_SetRenderViewport(renderer_, &viewport_);
    else
        SDL_SetRenderViewport(renderer_, nullptr);
}

void Gfx::setSmoothShading(bool smooth) {
    for (auto& [id, s] : sprites_) {
        if (s.tex) SDL_SetTextureScaleMode(s.tex, smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    }
    for (auto& [id, f] : fonts_) {
        if (f.atlas) SDL_SetTextureScaleMode(f.atlas, smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    }
    SDL_ScaleMode mode = smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
    for (auto& [key, g] : glyphCache_)
        if (g.tex) SDL_SetTextureScaleMode(g.tex, mode);
}

void Gfx::setLimitFps(double fps, bool vsync) {
    limitFps_ = fps;
    vsync_ = vsync;
    if (renderer_) SDL_SetRenderVSync(renderer_, vsync ? 1 : 0);
}

void Gfx::setClearColor(uint32_t color) {
    if (static_cast<int32_t>(color) == -1) {
        clearEnabled_ = false;
        return;
    }
    clearEnabled_ = true;
    clearColor_ = color & 0xFFFFFF;
}

void Gfx::applyBlend(uint8_t& alpha) const {
    if (!renderer_) return;
    if (alphaMode_ > 0.0) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
        alpha = 255;
    } else if (alphaMode_ < 0.0) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        double a = 1.0 + alphaMode_;
        if (a < 0) a = 0;
        alpha = static_cast<uint8_t>(std::lround(a * 255));
    } else {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        alpha = 255;
    }
}

void Gfx::clearScreen(uint32_t color) {
    clearColor_ = color & 0xFFFFFF;
    clearEnabled_ = true;
    if (headless || !renderer_) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, redOf(clearColor_), greenOf(clearColor_), blueOf(clearColor_), 255);
    SDL_RenderClear(renderer_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
}

void Gfx::blackScreen() {
    setClearColor(0x000000);
    if (background_) {
        SDL_DestroyTexture(background_);
        background_ = nullptr;
        bgW_ = bgH_ = 0;
    }
    clearScreen(0x000000);
}

void Gfx::beginFrameImpl() {
    if (headless || !renderer_) return;
    if (background_) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_FRect dst{0, 0, static_cast<float>(width_), static_cast<float>(height_)};
        SDL_RenderTexture(renderer_, background_, nullptr, &dst);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    } else if (clearEnabled_) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer_, redOf(clearColor_), greenOf(clearColor_), blueOf(clearColor_), 255);
        SDL_RenderClear(renderer_);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }
    applyViewport();
}

// ------------------------------------------------------------------- Zeichnen

void Gfx::drawLine(float x1, float y1, float x2, float y2, uint32_t color) {
    if (headless || !renderer_) return;
    uint8_t a = 255;
    applyBlend(a);
    SDL_SetRenderDrawColor(renderer_, redOf(color), greenOf(color), blueOf(color), a);
    SDL_RenderLine(renderer_, x1, y1, x2, y2);
}

void Gfx::drawRect(float x, float y, float w, float h, uint32_t color) {
    if (headless || !renderer_) return;
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    uint8_t a = 255;
    applyBlend(a);
    SDL_SetRenderDrawColor(renderer_, redOf(color), greenOf(color), blueOf(color), a);
    SDL_FRect r{x, y, w, h};
    SDL_RenderFillRect(renderer_, &r);
}

void Gfx::drawCircle(float cx, float cy, float r, uint32_t color, bool filled) {
    if (headless || !renderer_) return;
    if (r <= 0) return;
    uint8_t a = 255;
    applyBlend(a);
    SDL_SetRenderDrawColor(renderer_, redOf(color), greenOf(color), blueOf(color), a);
    if (filled) {
        for (int dy = static_cast<int>(-r); dy <= static_cast<int>(r); ++dy) {
            float dx = std::sqrt(std::max(0.0f, r * r - static_cast<float>(dy) * dy));
            SDL_RenderLine(renderer_, cx - dx, cy + dy, cx + dx, cy + dy);
        }
    } else {
        const int seg = std::max(24, static_cast<int>(r * 2));
        float px = cx + r, py = cy;
        for (int i = 1; i <= seg; ++i) {
            float ang = static_cast<float>(i) * 6.28318530718f / static_cast<float>(seg);
            float nx = cx + r * std::cos(ang);
            float ny = cy + r * std::sin(ang);
            SDL_RenderLine(renderer_, px, py, nx, ny);
            px = nx;
            py = ny;
        }
    }
}

void Gfx::setPixel(int x, int y, uint32_t color) {
    if (headless || !renderer_) return;
    uint8_t a = 255;
    applyBlend(a);
    SDL_SetRenderDrawColor(renderer_, redOf(color), greenOf(color), blueOf(color), a);
    SDL_RenderPoint(renderer_, static_cast<float>(x), static_cast<float>(y));
}

uint32_t Gfx::getPixel(int x, int y) {
    if (headless || !renderer_) return 0;
    SDL_Rect rect{x, y, 1, 1};
    SDL_Surface* surf = SDL_RenderReadPixels(renderer_, &rect);
    if (!surf) return 0;
    SDL_Surface* conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!conv) return 0;
    uint32_t out = 0;
    if (conv->pixels) {
        const uint8_t* p = static_cast<const uint8_t*>(conv->pixels);
        out = (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
    }
    SDL_DestroySurface(conv);
    return out;
}

// ------------------------------------------------------------------- Polygone

void Gfx::startPoly(int spriteId, int mode) {
    polyActive_ = true;
    polySprite_ = spriteId;
    polyMode_ = mode;
    polyVerts_.clear();
}

void Gfx::polyVector(float x, float y, float u, float v, uint32_t color) {
    if (!polyActive_) return;
    PolyVertex pv;
    pv.x = x;
    pv.y = y;
    pv.u = u;
    pv.v = v;
    pv.color = color;
    polyVerts_.push_back(pv);
}

void Gfx::endPoly() {
    if (!polyActive_) return;
    polyActive_ = false;
    if (headless || !renderer_ || polyVerts_.size() < 3) return;

    std::vector<SDL_Vertex> verts;
    verts.reserve(polyVerts_.size());
    double mul = alphaMode_ < 0 ? 1.0 + alphaMode_ : 1.0;
    for (const auto& p : polyVerts_) {
        SDL_Vertex v;
        v.position = SDL_FPoint{p.x, p.y};
        v.tex_coord = SDL_FPoint{p.u, p.v};
        v.color = SDL_FColor{redOf(p.color) / 255.0f, greenOf(p.color) / 255.0f,
                             blueOf(p.color) / 255.0f, static_cast<float>(mul)};
        verts.push_back(v);
    }

    std::vector<int> indices;
    if (polyMode_ == 1) {
        for (size_t i = 0; i + 2 < verts.size(); i += 3) {
            indices.push_back(static_cast<int>(i));
            indices.push_back(static_cast<int>(i + 1));
            indices.push_back(static_cast<int>(i + 2));
        }
    } else if (polyMode_ == 2) {
        for (size_t i = 0; i + 2 < verts.size(); ++i) {
            indices.push_back(static_cast<int>(i));
            indices.push_back(static_cast<int>(i + 1));
            indices.push_back(static_cast<int>(i + 2));
        }
    } else {
        for (size_t i = 1; i + 1 < verts.size(); ++i) {
            indices.push_back(0);
            indices.push_back(static_cast<int>(i));
            indices.push_back(static_cast<int>(i + 1));
        }
    }

    SDL_Texture* tex = nullptr;
    if (polySprite_ >= 0) {
        const Sprite* s = findSprite(polySprite_);
        if (s) tex = s->tex;
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(renderer_, tex, verts.data(), static_cast<int>(verts.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

// -------------------------------------------------------------------- Texturen

SDL_Texture* Gfx::textureFromPixels(const uint8_t* rgba, int w, int h) {
    if (!renderer_) return nullptr;
    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) return nullptr;
    SDL_UpdateTexture(tex, nullptr, rgba, w * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    return tex;
}

void Gfx::prepareTexture(SDL_Texture* tex) {
    if (!tex) return;
    SDL_SetTextureBlendMode(tex, alphaMode_ > 0 ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    int a = 255;
    if (alphaMode_ < 0) {
        double f = 1.0 + alphaMode_;
        if (f < 0) f = 0;
        a = static_cast<int>(std::lround(f * 255));
    }
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(a));
    SDL_SetTextureColorMod(tex, 255, 255, 255);
}

SDL_Texture* Gfx::makeTexture(const std::vector<uint8_t>& rgba, int w, int h) {
    if (rgba.empty()) return nullptr;
    return textureFromPixels(rgba.data(), w, h);
}

bool Gfx::loadImagePixels(const std::string& file, std::vector<uint8_t>& out, int& w, int& h,
                          std::string& err) {
    int comp = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* data = stbi_load(file.c_str(), &w, &h, &comp, 4);
    if (!data) {
        err = "Bild konnte nicht geladen werden: " + file;
        return false;
    }
    out.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    return true;
}

const Sprite* Gfx::findSprite(int id) const {
    auto it = sprites_.find(id);
    return it == sprites_.end() ? nullptr : &it->second;
}

bool Gfx::loadSprite(const std::string& file, int id, std::string& err) {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!loadImagePixels(file, rgba, w, h, err)) return false;

    // Farbtransparenz anwenden
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        uint32_t c = (static_cast<uint32_t>(rgba[i]) << 16) |
                     (static_cast<uint32_t>(rgba[i + 1]) << 8) | rgba[i + 2];
        if (c == transparencyKey_) rgba[i + 3] = 0;
        if (alphaTesting_ < 1.0 && rgba[i + 3] < static_cast<uint8_t>(alphaTesting_ * 255.0))
            rgba[i + 3] = 0;
    }

    Sprite sp;
    sp.w = w;
    sp.h = h;
    sp.pixels = rgba;
    sp.hasPixels = true;
    if (!headless) {
        sp.tex = makeTexture(rgba, w, h);
        if (!sp.tex) {
            err = "Textur konnte nicht erstellt werden: " + file;
            return false;
        }
    }

    auto it = sprites_.find(id);
    if (it != sprites_.end() && it->second.tex && it->second.ownsTexture)
        SDL_DestroyTexture(it->second.tex);
    sprites_[id] = std::move(sp);
    return true;
}

void Gfx::deleteSprite(int id) {
    auto it = sprites_.find(id);
    if (it == sprites_.end()) return;
    if (it->second.tex && it->second.ownsTexture && !headless) SDL_DestroyTexture(it->second.tex);
    sprites_.erase(it);
}

bool Gfx::grabSprite(int id, int x, int y, int w, int h, std::string& err) {
    if (headless || !renderer_) {
        err = "GRABSPRITE ist im Headless-Modus nicht verfuegbar";
        return false;
    }
    if (w <= 0 || h <= 0) {
        err = "GRABSPRITE: ungueltige Groesse";
        return false;
    }
    SDL_Rect rect{x, y, w, h};
    SDL_Surface* surf = SDL_RenderReadPixels(renderer_, &rect);
    if (!surf) {
        err = std::string("GRABSPRITE fehlgeschlagen: ") + SDL_GetError();
        return false;
    }
    SDL_Surface* conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!conv) {
        err = "GRABSPRITE: Konvertierung fehlgeschlagen";
        return false;
    }
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    const uint8_t* src = static_cast<const uint8_t*>(conv->pixels);
    for (int row = 0; row < h; ++row) {
        std::memcpy(rgba.data() + static_cast<size_t>(row) * w * 4,
                    src + static_cast<size_t>(row) * conv->pitch, static_cast<size_t>(w) * 4);
    }
    SDL_DestroySurface(conv);
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        uint32_t c = (static_cast<uint32_t>(rgba[i]) << 16) |
                     (static_cast<uint32_t>(rgba[i + 1]) << 8) | rgba[i + 2];
        if (c == transparencyKey_) rgba[i + 3] = 0;
    }
    Sprite sp;
    sp.w = w;
    sp.h = h;
    sp.pixels = rgba;
    sp.hasPixels = true;
    sp.tex = makeTexture(rgba, w, h);
    auto it = sprites_.find(id);
    if (it != sprites_.end() && it->second.tex && it->second.ownsTexture)
        SDL_DestroyTexture(it->second.tex);
    sprites_[id] = std::move(sp);
    return true;
}

bool Gfx::saveSprite(const std::string& file, int id, std::string& err) {
    const Sprite* s = findSprite(id);
    if (!s || !s->hasPixels) {
        err = "SAVESPRITE: Sprite " + std::to_string(id) + " hat keine Bilddaten";
        return false;
    }
    std::string ext = lowerExt(file);
    bool ok = false;
    if (ext == "png")
        ok = stbi_write_png(file.c_str(), s->w, s->h, 4, s->pixels.data(), s->w * 4) != 0;
    else
        ok = stbi_write_bmp(file.c_str(), s->w, s->h, 4, s->pixels.data()) != 0;
    if (!ok) err = "SAVESPRITE: Schreiben fehlgeschlagen: " + file;
    return ok;
}

bool Gfx::saveBmp(const std::string& file, std::string& err) {
    if (headless || !renderer_) {
        err = "SAVEBMP ist im Headless-Modus nicht verfuegbar";
        return false;
    }
    SDL_Surface* surf = SDL_RenderReadPixels(renderer_, nullptr);
    if (!surf) {
        err = std::string("SAVEBMP: Bildschirm konnte nicht gelesen werden: ") + SDL_GetError();
        return false;
    }
    SDL_Surface* conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!conv) {
        err = "SAVEBMP: Konvertierung fehlgeschlagen";
        return false;
    }
    std::vector<uint8_t> rgba(static_cast<size_t>(conv->w) * conv->h * 4);
    const uint8_t* src = static_cast<const uint8_t*>(conv->pixels);
    for (int row = 0; row < conv->h; ++row) {
        std::memcpy(rgba.data() + static_cast<size_t>(row) * conv->w * 4,
                    src + static_cast<size_t>(row) * conv->pitch, static_cast<size_t>(conv->w) * 4);
    }
    int w = conv->w, h = conv->h;
    SDL_DestroySurface(conv);

    std::string ext = lowerExt(file);
    bool ok;
    if (ext == "png")
        ok = stbi_write_png(file.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
    else
        ok = stbi_write_bmp(file.c_str(), w, h, 4, rgba.data()) != 0;
    if (!ok) err = "SAVEBMP: Schreiben fehlgeschlagen: " + file;
    return ok;
}

bool Gfx::spriteInfo(int id, int& w, int& h) const {
    const Sprite* s = findSprite(id);
    if (!s) {
        w = h = 0;
        return false;
    }
    w = s->w;
    h = s->h;
    return true;
}

int Gfx::spriteWidth(int id) const {
    const Sprite* s = findSprite(id);
    return s ? s->w : 0;
}

int Gfx::spriteHeight(int id) const {
    const Sprite* s = findSprite(id);
    return s ? s->h : 0;
}

int Gfx::animFrameCount(int id) const {
    const Sprite* s = findSprite(id);
    if (!s || s->frameW <= 0 || s->frameH <= 0) return 0;
    int cols = s->w / s->frameW;
    int rows = s->h / s->frameH;
    return cols * rows;
}

void Gfx::setSpriteAnim(int id, int frameW, int frameH) {
    auto it = sprites_.find(id);
    if (it == sprites_.end()) return;
    if (frameW > 0 && frameH > 0 && frameW <= it->second.w && frameH <= it->second.h) {
        it->second.frameW = frameW;
        it->second.frameH = frameH;
    } else {
        it->second.frameW = 0;
        it->second.frameH = 0;
    }
}

static void frameRect(const Sprite& s, int frame, SDL_FRect& src, int& fw, int& fh) {
    fw = s.frameW > 0 ? s.frameW : s.w;
    fh = s.frameH > 0 ? s.frameH : s.h;
    int cols = fw > 0 ? std::max(1, s.w / fw) : 1;
    int f = frame;
    if (f < 0) f = 0;
    src.x = static_cast<float>((f % cols) * fw);
    src.y = static_cast<float>((f / cols) * fh);
    src.w = static_cast<float>(fw);
    src.h = static_cast<float>(fh);
}

void Gfx::drawSprite(int id, float x, float y) {
    const Sprite* s = findSprite(id);
    if (!s || !s->tex || headless || !renderer_) return;
    prepareTexture(s->tex);
    SDL_FRect src{0, 0, static_cast<float>(s->w), static_cast<float>(s->h)};
    SDL_FRect dst{x, y, static_cast<float>(s->w), static_cast<float>(s->h)};
    SDL_RenderTexture(renderer_, s->tex, &src, &dst);
}

void Gfx::drawSpriteZoom(int id, float x, float y, float sx, float sy) {
    const Sprite* s = findSprite(id);
    if (!s || !s->tex || headless || !renderer_) return;
    prepareTexture(s->tex);
    float fx = sx / 100.0f;
    float fy = sy / 100.0f;
    float w = s->w * fx;
    float h = s->h * fy;
    SDL_FRect src{0, 0, static_cast<float>(s->w), static_cast<float>(s->h)};
    SDL_FRect dst{x + (s->w - w) / 2.0f, y + (s->h - h) / 2.0f, w, h};
    SDL_RenderTexture(renderer_, s->tex, &src, &dst);
}

void Gfx::drawSpriteRotozoom(int id, float x, float y, double angle, float scale) {
    const Sprite* s = findSprite(id);
    if (!s || !s->tex || headless || !renderer_) return;
    prepareTexture(s->tex);
    float f = scale / 100.0f;
    SDL_FRect src{0, 0, static_cast<float>(s->w), static_cast<float>(s->h)};
    SDL_FRect dst{x, y, s->w * f, s->h * f};
    SDL_FPoint center{dst.w / 2.0f, dst.h / 2.0f};
    SDL_RenderTextureRotated(renderer_, s->tex, &src, &dst, static_cast<double>(-angle), &center,
                             SDL_FLIP_NONE);
}

void Gfx::drawAnim(int id, int frame, float x, float y) {
    const Sprite* s = findSprite(id);
    if (!s || !s->tex || headless || !renderer_) return;
    prepareTexture(s->tex);
    SDL_FRect src;
    int fw = 0, fh = 0;
    frameRect(*s, frame, src, fw, fh);
    SDL_FRect dst{x, y, static_cast<float>(fw), static_cast<float>(fh)};
    SDL_RenderTexture(renderer_, s->tex, &src, &dst);
}

void Gfx::drawAnimRotozoom(int id, int frame, float x, float y, double angle, float scale) {
    const Sprite* s = findSprite(id);
    if (!s || !s->tex || headless || !renderer_) return;
    prepareTexture(s->tex);
    SDL_FRect src;
    int fw = 0, fh = 0;
    frameRect(*s, frame, src, fw, fh);
    float f = scale / 100.0f;
    SDL_FRect dst{x, y, fw * f, fh * f};
    SDL_FPoint center{dst.w / 2.0f, dst.h / 2.0f};
    SDL_RenderTextureRotated(renderer_, s->tex, &src, &dst, static_cast<double>(-angle), &center,
                             SDL_FLIP_NONE);
}

void Gfx::drawAnimStretch(int id, int frame, float x, float y, float w, float h) {
    const Sprite* s = findSprite(id);
    if (!s || !s->tex || headless || !renderer_) return;
    prepareTexture(s->tex);
    SDL_FRect src;
    int fw = 0, fh = 0;
    frameRect(*s, frame, src, fw, fh);
    SDL_FRect dst{x, y, w, h};
    SDL_RenderTexture(renderer_, s->tex, &src, &dst);
}

bool Gfx::spriteCollision(int id1, float x1, float y1, int id2, float x2, float y2) const {
    return animCollision(id1, 0, x1, y1, id2, 0, x2, y2);
}

bool Gfx::animCollision(int id1, int frame1, float x1, float y1, int id2, int frame2, float x2,
                        float y2) const {
    const Sprite* a = findSprite(id1);
    const Sprite* b = findSprite(id2);
    if (!a || !b) return false;
    SDL_FRect sa, sb;
    int aw, ah, bw, bh;
    frameRect(*a, frame1, sa, aw, ah);
    frameRect(*b, frame2, sb, bw, bh);
    if (x1 + aw <= x2 || x2 + bw <= x1 || y1 + ah <= y2 || y2 + bh <= y1) return false;
    if (!a->hasPixels || !b->hasPixels) return true;  // nur Box-Test moeglich

    int x0 = static_cast<int>(std::max(x1, x2));
    int x3 = static_cast<int>(std::min(x1 + aw, x2 + bw));
    int y0 = static_cast<int>(std::max(y1, y2));
    int y3 = static_cast<int>(std::min(y1 + ah, y2 + bh));
    for (int y = y0; y < y3; ++y) {
        for (int x = x0; x < x3; ++x) {
            int ax = static_cast<int>(x - x1) + static_cast<int>(sa.x);
            int ay = static_cast<int>(y - y1) + static_cast<int>(sa.y);
            int bx = static_cast<int>(x - x2) + static_cast<int>(sb.x);
            int by = static_cast<int>(y - y2) + static_cast<int>(sb.y);
            if (ax < 0 || ay < 0 || ax >= a->w || ay >= a->h) continue;
            if (bx < 0 || by < 0 || bx >= b->w || by >= b->h) continue;
            size_t ai = (static_cast<size_t>(ay) * a->w + ax) * 4 + 3;
            size_t bi = (static_cast<size_t>(by) * b->w + bx) * 4 + 3;
            if (a->pixels[ai] > 8 && b->pixels[bi] > 8) return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------- Bildpuffer

bool Gfx::loadBackground(const std::string& file, std::string& err) {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!loadImagePixels(file, rgba, w, h, err)) return false;
    if (headless) return true;
    SDL_Texture* tex = makeTexture(rgba, w, h);
    if (!tex) {
        err = "LOADBMP: Textur konnte nicht erstellt werden";
        return false;
    }
    if (background_) SDL_DestroyTexture(background_);
    background_ = tex;
    bgW_ = w;
    bgH_ = h;
    return true;
}

void Gfx::useAsBackground() {
    if (headless || !renderer_) return;
    SDL_Surface* surf = SDL_RenderReadPixels(renderer_, nullptr);
    if (!surf) return;
    SDL_Surface* conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!conv) return;
    std::vector<uint8_t> rgba(static_cast<size_t>(conv->w) * conv->h * 4);
    const uint8_t* src = static_cast<const uint8_t*>(conv->pixels);
    for (int row = 0; row < conv->h; ++row) {
        std::memcpy(rgba.data() + static_cast<size_t>(row) * conv->w * 4,
                    src + static_cast<size_t>(row) * conv->pitch, static_cast<size_t>(conv->w) * 4);
    }
    SDL_Texture* tex = makeTexture(rgba, conv->w, conv->h);
    bgW_ = conv->w;
    bgH_ = conv->h;
    SDL_DestroySurface(conv);
    if (!tex) return;
    if (background_) SDL_DestroyTexture(background_);
    background_ = tex;
}

void Gfx::blendScreen(const std::string& file, int ms) {
    if (headless || !renderer_) return;
    int target = ms > 0 ? ms : 1000;
    SDL_Surface* surf = SDL_RenderReadPixels(renderer_, nullptr);
    std::vector<uint8_t> oldPixels;
    int w = width_, h = height_;
    if (surf) {
        SDL_Surface* conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surf);
        if (conv) {
            w = conv->w;
            h = conv->h;
            oldPixels.resize(static_cast<size_t>(w) * h * 4);
            const uint8_t* src = static_cast<const uint8_t*>(conv->pixels);
            for (int row = 0; row < h; ++row) {
                std::memcpy(oldPixels.data() + static_cast<size_t>(row) * w * 4,
                            src + static_cast<size_t>(row) * conv->pitch,
                            static_cast<size_t>(w) * 4);
            }
            SDL_DestroySurface(conv);
        }
    }
    SDL_Texture* from = oldPixels.empty() ? nullptr : makeTexture(oldPixels, w, h);

    int iw = 0, ih = 0;
    std::vector<uint8_t> img;
    std::string err;
    SDL_Texture* to = nullptr;
    if (loadImagePixels(file, img, iw, ih, err)) to = makeTexture(img, iw, ih);
    if (!to) {
        if (from) SDL_DestroyTexture(from);
        return;
    }

    Uint64 start = SDL_GetTicks();
    for (;;) {
        Uint64 now = SDL_GetTicks();
        float t = static_cast<float>(now - start) / static_cast<float>(target);
        if (t > 1.0f) t = 1.0f;
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        if (from) SDL_RenderTexture(renderer_, from, nullptr, nullptr);
        SDL_SetTextureBlendMode(to, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(to, static_cast<Uint8>(t * 255));
        SDL_RenderTexture(renderer_, to, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
        pumpEventsImpl();
        if (t >= 1.0f) break;
        SDL_Delay(10);
    }
    SDL_SetTextureAlphaMod(to, 255);
    if (from) SDL_DestroyTexture(from);
    SDL_DestroyTexture(to);
}

// ---------------------------------------------------------- Virtuelle Screens

void Gfx::createScreen(int screenId, int spriteId, int w, int h) {
    if (headless || !renderer_) return;
    auto it = screens_.find(screenId);
    if (it != screens_.end()) {
        SDL_Texture* old = it->second;
        // Sprites, die auf diese Textur zeigen, duerfen nicht verwaisen
        for (auto& [sid, sp] : sprites_)
            if (sp.tex == old) {
                sp.tex = nullptr;
                sp.ownsTexture = true;
            }
        SDL_DestroyTexture(old);
        screens_.erase(it);
    }
    if (w <= 0 || h <= 0 || spriteId < 0) return;
    SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tex) return;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    screens_[screenId] = tex;
    auto sit = sprites_.find(spriteId);
    if (sit != sprites_.end() && sit->second.tex && sit->second.ownsTexture)
        SDL_DestroyTexture(sit->second.tex);
    Sprite sp;
    sp.tex = tex;
    sp.w = w;
    sp.h = h;
    sp.ownsTexture = false;
    sprites_[spriteId] = std::move(sp);
}

void Gfx::useScreen(int screenId) {
    targetScreen_ = screenId;
    if (headless || !renderer_) return;
    if (screenId < 0) {
        SDL_SetRenderTarget(renderer_, nullptr);
        applyViewport();
    } else {
        auto it = screens_.find(screenId);
        if (it != screens_.end()) {
            SDL_SetRenderTarget(renderer_, it->second);
            SDL_SetRenderViewport(renderer_, nullptr);
        }
    }
}

// ---------------------------------------------------------------------- Text

void Gfx::ensureDefaultFont() {
    auto& f = fonts_[0];
    if (f.valid) return;
    f.valid = true;
    f.isTtf = false;
    f.cellW = 8;
    f.cellH = 8;
    f.baseSize = 8;
    f.scale = 1;
    f.atlasCols = 128;
    if (headless) return;

    // Atlas: 128 Zeichen nebeneinander, je 8x8 Pixel
    const int cw = 8, ch = 8;
    std::vector<uint8_t> atlas(static_cast<size_t>(cw) * 128 * ch * 4, 0);
    for (int c = 0; c < 128; ++c) {
        for (int row = 0; row < 8; ++row) {
            unsigned char bits = static_cast<unsigned char>(font8x8_basic[c][row]);
            for (int col = 0; col < 8; ++col) {
                if (bits & (1u << col)) {
                    size_t x = static_cast<size_t>(c) * cw + col;
                    size_t y = static_cast<size_t>(row);
                    size_t idx = (y * (static_cast<size_t>(cw) * 128) + x) * 4;
                    atlas[idx + 0] = 255;
                    atlas[idx + 1] = 255;
                    atlas[idx + 2] = 255;
                    atlas[idx + 3] = 255;
                }
            }
        }
    }
    f.atlas = makeTexture(atlas, cw * 128, ch);
    if (f.atlas) SDL_SetTextureScaleMode(f.atlas, SDL_SCALEMODE_NEAREST);
}

const FontSlot* Gfx::activeFont() const {
    auto it = fonts_.find(currentFont_);
    if (it == fonts_.end() || !it->second.valid) {
        auto def = fonts_.find(0);
        if (def == fonts_.end()) return nullptr;
        return &def->second;
    }
    return &it->second;
}

bool Gfx::hasTtf() const {
#ifdef GBX_HAVE_TTF
    return true;
#else
    return false;
#endif
}

bool Gfx::loadBitmapFont(const std::string& file, int id, std::string& err) {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!loadImagePixels(file, rgba, w, h, err)) return false;
    // Farbtransparenz fuer Fonts: Standard ist Schwarz, ausser der Benutzer
    // hat mit SETTRANSPARENCY eine andere Farbe gesetzt.
    uint32_t fontKey = transparencySet_ ? transparencyKey_ : 0x000000;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        uint32_t c = (static_cast<uint32_t>(rgba[i]) << 16) |
                     (static_cast<uint32_t>(rgba[i + 1]) << 8) | rgba[i + 2];
        if (c == fontKey) rgba[i + 3] = 0;
    }
    FontSlot f;
    f.valid = true;
    f.isTtf = false;
    f.atlasCols = 16;
    f.cellW = std::max(1, w / 16);
    f.cellH = std::max(1, h / 8);
    f.baseSize = f.cellH;
    f.scale = 1;
    if (!headless) {
        f.atlas = makeTexture(rgba, w, h);
        if (f.atlas) SDL_SetTextureScaleMode(f.atlas, SDL_SCALEMODE_NEAREST);
    }
    auto it = fonts_.find(id);
    if (it != fonts_.end()) {
        if (it->second.atlas && !headless) SDL_DestroyTexture(it->second.atlas);
#ifdef GBX_HAVE_TTF
        if (it->second.face) FT_Done_Face(it->second.face);
#endif
    }
    fonts_[id] = std::move(f);
    return true;
}

bool Gfx::loadTtfFont(const std::string& file, int id, int points, std::string& err) {
    if (!hasTtf()) {
        err = "TTF-Unterstuetzung ist in diesem Build nicht aktiviert";
        return false;
    }
#ifdef GBX_HAVE_TTF
    if (!g_ftLibrary && FT_Init_FreeType(&g_ftLibrary) != 0) {
        err = "FreeType konnte nicht initialisiert werden";
        return false;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(g_ftLibrary, file.c_str(), 0, &face) != 0) {
        err = "TTF-Schrift konnte nicht geladen werden: " + file;
        return false;
    }
    int pts = points > 0 ? points : 16;
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pts)) != 0) {
        FT_Done_Face(face);
        err = "TTF-Schriftgroesse nicht unterstuetzt: " + file;
        return false;
    }
    FontSlot f;
    f.valid = true;
    f.isTtf = true;
    f.face = face;
    f.baseSize = pts;
    f.scale = 1;
    f.ascent = static_cast<int>(face->size->metrics.ascender >> 6);
    f.lineHeight = static_cast<int>(face->size->metrics.height >> 6);
    if (f.lineHeight <= 0) f.lineHeight = pts;
    f.cellH = f.lineHeight;
    f.cellW = pts / 2;

    auto it = fonts_.find(id);
    if (it != fonts_.end()) {
        if (it->second.atlas && !headless) SDL_DestroyTexture(it->second.atlas);
        if (it->second.face) FT_Done_Face(it->second.face);
    }
    fonts_[id] = std::move(f);

    // Glyphen-Cache dieses Fonts verwerfen
    std::string prefix = std::to_string(id) + "|";
    for (auto i = glyphCache_.begin(); i != glyphCache_.end();) {
        if (i->first.rfind(prefix, 0) == 0) {
            if (i->second.tex) SDL_DestroyTexture(i->second.tex);
            i = glyphCache_.erase(i);
        } else {
            ++i;
        }
    }
    return true;
#else
    (void)file;
    (void)id;
    (void)points;
    err = "TTF-Unterstuetzung ist in diesem Build nicht aktiviert";
    return false;
#endif
}

void Gfx::setFont(int id) {
    if (fonts_.find(id) != fonts_.end()) currentFont_ = id;
}

void Gfx::setFontScale(int scale) {
    if (scale < 1) scale = 1;
    if (scale > 16) scale = 16;
    auto it = fonts_.find(currentFont_);
    if (it != fonts_.end()) it->second.scale = scale;
}

void Gfx::fontCharSize(int& w, int& h) const {
    const FontSlot* f = activeFont();
    if (!f) {
        w = h = 8;
        return;
    }
    if (f->isTtf) {
        w = static_cast<int>(f->baseSize * 0.6f * f->scale);
        h = f->cellH * f->scale;
    } else {
        w = f->cellW * f->scale;
        h = f->cellH * f->scale;
    }
}

void Gfx::textSize(const std::string& text, int& w, int& h) const {
    const FontSlot* f = activeFont();
    if (!f) {
        w = static_cast<int>(text.size()) * 8;
        h = 8;
        return;
    }
    if (f->isTtf) {
#ifdef GBX_HAVE_TTF
        int lineWidth = 0, maxWidth = 0;
        uint32_t prev = 0;
        for (uint32_t cp : utf8Codepoints(text)) {
            if (cp == '\n') {
                maxWidth = std::max(maxWidth, lineWidth);
                lineWidth = 0;
                prev = 0;
                continue;
            }
            if (prev && FT_HAS_KERNING(f->face)) {
                FT_Vector delta;
                if (FT_Get_Kerning(f->face, prev, cp, FT_KERNING_DEFAULT, &delta) == 0)
                    lineWidth += static_cast<int>(delta.x >> 6);
            }
            if (FT_Load_Char(f->face, cp, FT_LOAD_DEFAULT) == 0)
                lineWidth += static_cast<int>(f->face->glyph->advance.x >> 6);
            prev = cp;
        }
        maxWidth = std::max(maxWidth, lineWidth);
        w = maxWidth * f->scale;
        h = f->lineHeight * f->scale;
        return;
#endif
        w = static_cast<int>(text.size()) * static_cast<int>(f->baseSize * 0.6f) * f->scale;
        h = f->cellH * f->scale;
        return;
    }
    w = static_cast<int>(text.size()) * f->cellW * f->scale;
    h = f->cellH * f->scale;
}

void Gfx::drawGlyphs(const std::string& text, float x, float y, uint32_t color) {
    const FontSlot* f = activeFont();
    if (!f || !f->atlas || headless || !renderer_) return;
    SDL_SetTextureBlendMode(f->atlas, alphaMode_ > 0 ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(f->atlas, redOf(color), greenOf(color), blueOf(color));
    int a = 255;
    if (alphaMode_ < 0) {
        double m = 1.0 + alphaMode_;
        if (m < 0) m = 0;
        a = static_cast<int>(std::lround(m * 255));
    }
    SDL_SetTextureAlphaMod(f->atlas, static_cast<Uint8>(a));

    float cw = static_cast<float>(f->cellW * f->scale);
    float ch = static_cast<float>(f->cellH * f->scale);
    float px = x;
    for (unsigned char c : text) {
        if (c == '\n') {
            px = x;
            y += ch;
            continue;
        }
        if (c < 32) {
            px += cw;
            continue;
        }
        int idx = c;
        if (idx > 127) idx = '?';
        SDL_FRect src{static_cast<float>((idx % f->atlasCols) * f->cellW),
                      static_cast<float>((idx / f->atlasCols) * f->cellH),
                      static_cast<float>(f->cellW), static_cast<float>(f->cellH)};
        SDL_FRect dst{px, y, cw, ch};
        SDL_RenderTexture(renderer_, f->atlas, &src, &dst);
        px += cw;
    }
}

const GlyphInfo* Gfx::glyphFor(int fontId, const FontSlot& f, uint32_t cp) {
#ifdef GBX_HAVE_TTF
    std::string key = std::to_string(fontId) + "|" + std::to_string(cp);
    auto it = glyphCache_.find(key);
    if (it != glyphCache_.end()) return &it->second;
    if (!f.face) return nullptr;
    if (FT_Load_Char(f.face, cp, FT_LOAD_RENDER) != 0) return nullptr;
    FT_GlyphSlot slot = f.face->glyph;
    GlyphInfo g;
    g.w = static_cast<int>(slot->bitmap.width);
    g.h = static_cast<int>(slot->bitmap.rows);
    g.bearingX = slot->bitmap_left;
    g.bearingY = slot->bitmap_top;
    g.advance = static_cast<int>(slot->advance.x >> 6);
    if (g.w > 0 && g.h > 0 && slot->bitmap.buffer && !headless) {
        std::vector<uint8_t> rgba(static_cast<size_t>(g.w) * static_cast<size_t>(g.h) * 4, 0);
        for (int row = 0; row < g.h; ++row) {
            const uint8_t* src = slot->bitmap.buffer + static_cast<size_t>(row) * slot->bitmap.pitch;
            for (int col = 0; col < g.w; ++col) {
                size_t idx = (static_cast<size_t>(row) * static_cast<size_t>(g.w) +
                              static_cast<size_t>(col)) * 4;
                rgba[idx + 0] = 255;
                rgba[idx + 1] = 255;
                rgba[idx + 2] = 255;
                rgba[idx + 3] = src[col];
            }
        }
        g.tex = makeTexture(rgba, g.w, g.h);
        if (g.tex) SDL_SetTextureScaleMode(g.tex, SDL_SCALEMODE_LINEAR);
    }
    auto res = glyphCache_.emplace(key, g);
    return &res.first->second;
#else
    (void)fontId;
    (void)f;
    (void)cp;
    return nullptr;
#endif
}

void Gfx::drawText(const std::string& text, float x, float y, uint32_t color) {
    if (headless || !renderer_) return;
    const FontSlot* f = activeFont();
    if (!f) return;
    if (!f->isTtf) {
        drawGlyphs(text, x, y, color);
        return;
    }
#ifdef GBX_HAVE_TTF
    float pen = x;
    float baseline = y + static_cast<float>(f->ascent) * f->scale;
    uint32_t prev = 0;
    for (uint32_t cp : utf8Codepoints(text)) {
        if (cp == '\n') {
            pen = x;
            baseline += static_cast<float>(f->lineHeight) * f->scale;
            prev = 0;
            continue;
        }
        if (prev && FT_HAS_KERNING(f->face)) {
            FT_Vector delta;
            if (FT_Get_Kerning(f->face, prev, cp, FT_KERNING_DEFAULT, &delta) == 0)
                pen += static_cast<float>(delta.x >> 6) * f->scale;
        }
        const GlyphInfo* g = glyphFor(currentFont_, *f, cp);
        if (g && g->tex && g->w > 0 && g->h > 0) {
            prepareTexture(g->tex);
            SDL_SetTextureColorMod(g->tex, redOf(color), greenOf(color), blueOf(color));
            SDL_FRect dst{pen + static_cast<float>(g->bearingX) * f->scale,
                          baseline - static_cast<float>(g->bearingY) * f->scale,
                          static_cast<float>(g->w) * f->scale,
                          static_cast<float>(g->h) * f->scale};
            SDL_RenderTexture(renderer_, g->tex, nullptr, &dst);
        }
        if (g) pen += static_cast<float>(g->advance) * f->scale;
        prev = cp;
    }
#endif
}

void Gfx::drawTextCentered(const std::string& text, float cx, float y, uint32_t color) {
    int w = 0, h = 0;
    textSize(text, w, h);
    drawText(text, cx - w / 2.0f, y, color);
}

// ------------------------------------------------------------- Bildschirmausgabe

void Gfx::present() {
    Uint64 frameStart = SDL_GetTicks();
    pumpEventsImpl();
    if (!headless && renderer_) {
        SDL_RenderPresent(renderer_);
        beginFrameImpl();
    }
    Uint64 now = SDL_GetTicks();
    lastFrameDuration_ = now - frameStart_;
    frameStart_ = now;
    lastPresent_ = now;

    if (limitFps_ > 0.0 && !headless) {
        double target = 1000.0 / limitFps_;
        double elapsed = static_cast<double>(SDL_GetTicks() - frameStart);
        if (elapsed < target) SDL_Delay(static_cast<Uint32>(target - elapsed));
    }
}

double Gfx::timerFrame() const {
    if (headless) return 16.0;
    return static_cast<double>(SDL_GetTicks() - lastPresent_);
}

double Gfx::timerAll() const {
    if (headless) return static_cast<double>(SDL_GetTicks() - startTicks_);
    return static_cast<double>(SDL_GetTicks() - startTicks_);
}

void Gfx::resetFrameTimer() { lastPresent_ = SDL_GetTicks(); }

// -------------------------------------------------------------------- Eingabe

void Gfx::pumpEventsImpl() {
    if (!headless) {
        std::fill(keyPressed_.begin(), keyPressed_.end(), false);
        anyKeyHit_ = false;
        wheel_ = 0;
        mbLeftHit_ = mbRightHit_ = mbMiddleHit_ = false;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_QUIT:
                quit_ = true;
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                quit_ = true;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                width_ = e.window.data1;
                height_ = e.window.data2;
                break;
            case SDL_EVENT_KEY_DOWN: {
                SDL_Scancode sc = e.key.scancode;
                if (sc >= 0 && sc < SDL_SCANCODE_COUNT) {
                    keyDown_[sc] = true;
                    if (!e.key.repeat) {
                        keyPressed_[sc] = true;
                        anyKeyHit_ = true;
                    }
                }
                if (allowEscape_ && sc == SDL_SCANCODE_ESCAPE) quit_ = true;
                if (!e.key.repeat) {
                    // INKEY$: druckbare Zeichen aus dem Tastencode
                    SDL_Keycode kc = e.key.key;
                    if (kc >= 32 && kc < 127) {
                        inkey_.push_back(static_cast<char>(kc));
                    } else if (kc == SDLK_RETURN || kc == SDLK_KP_ENTER) {
                        inkey_.push_back('\n');
                    } else if (kc == SDLK_BACKSPACE) {
                        inkey_.push_back('\b');
                    }
                }
                break;
            }
            case SDL_EVENT_KEY_UP: {
                SDL_Scancode sc = e.key.scancode;
                if (sc >= 0 && sc < SDL_SCANCODE_COUNT) keyDown_[sc] = false;
                break;
            }
            case SDL_EVENT_TEXT_INPUT:
                textInput_ += e.text.text;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                mouseX_ = e.motion.x;
                mouseY_ = e.motion.y;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT) { mbLeft_ = true; mbLeftHit_ = true; }
                if (e.button.button == SDL_BUTTON_RIGHT) { mbRight_ = true; mbRightHit_ = true; }
                if (e.button.button == SDL_BUTTON_MIDDLE) { mbMiddle_ = true; mbMiddleHit_ = true; }
                mouseX_ = e.button.x;
                mouseY_ = e.button.y;
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT) mbLeft_ = false;
                if (e.button.button == SDL_BUTTON_RIGHT) mbRight_ = false;
                if (e.button.button == SDL_BUTTON_MIDDLE) mbMiddle_ = false;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                wheel_ += e.wheel.y > 0 ? 1 : (e.wheel.y < 0 ? -1 : 0);
                break;
            default:
                break;
        }
    }
    if (!headless) {
        float mx = 0, my = 0;
        SDL_MouseButtonFlags flags = SDL_GetMouseState(&mx, &my);
        // Nur uebernehmen, wenn keine Bewegung gemeldet wurde
        mouseX_ = mx;
        mouseY_ = my;
        mbLeft_ = (flags & SDL_BUTTON_LMASK) != 0;
        mbRight_ = (flags & SDL_BUTTON_RMASK) != 0;
        mbMiddle_ = (flags & SDL_BUTTON_MMASK) != 0;
    }
}

void Gfx::pump() {
    if (headless) return;
    pumpEventsImpl();
}

bool Gfx::keyDown(int scancode) const {
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) return false;
    if (headless) return false;
    const bool* st = SDL_GetKeyboardState(nullptr);
    return st[scancode];
}

bool Gfx::keyHit(int scancode) const {
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) return false;
    return keyPressed_[scancode];
}

std::string Gfx::takeTextInput() {
    std::string out = textInput_;
    textInput_.clear();
    return out;
}

std::string Gfx::takeInkey() {
    if (inkey_.empty()) return "";
    std::string out(1, inkey_.front());
    inkey_.erase(inkey_.begin());
    return out;
}

void Gfx::mouseState(float& x, float& y, bool& left, bool& right, bool& middle) const {
    x = mouseX_;
    y = mouseY_;
    left = mbLeft_;
    right = mbRight_;
    middle = mbMiddle_;
}

int Gfx::mouseButton(int n) const {
    switch (n) {
        case 1: return mbLeft_ ? 1 : 0;
        case 2: return mbRight_ ? 1 : 0;
        case 3: return mbMiddle_ ? 1 : 0;
        default: return 0;
    }
}

bool Gfx::mouseHit(int n) const {
    switch (n) {
        case 1: return mbLeftHit_;
        case 2: return mbRightHit_;
        case 3: return mbMiddleHit_;
        default: return false;
    }
}

void Gfx::setMouse(float x, float y) {
    if (window_) SDL_WarpMouseInWindow(window_, x, y);
    mouseX_ = x;
    mouseY_ = y;
}

void Gfx::showCursor(bool on) {
    cursorVisible_ = on;
    if (headless) return;
    if (on) SDL_ShowCursor();
    else SDL_HideCursor();
}

void Gfx::waitMouse() {
    for (;;) {
        if (headless) return;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                quit_ = true;
                return;
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) return;
            if (e.type == SDL_EVENT_KEY_DOWN && allowEscape_ && e.key.scancode == SDL_SCANCODE_ESCAPE) {
                quit_ = true;
                return;
            }
        }
        SDL_Delay(10);
    }
}

void Gfx::waitKey() {
    for (;;) {
        if (headless) return;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                quit_ = true;
                return;
            }
            if (e.type == SDL_EVENT_KEY_DOWN) return;
        }
        SDL_Delay(10);
    }
}
