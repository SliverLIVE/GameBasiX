// GameBasiX - SDL3-Grafik-, Text- und Eingabeschicht
#include "graphics.h"

#include "gbx_icon_png.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

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

inline uint8_t redOf(Color c) { return static_cast<uint8_t>((c >> 16) & 0xFF); }
inline uint8_t greenOf(Color c) { return static_cast<uint8_t>((c >> 8) & 0xFF); }
inline uint8_t blueOf(Color c) { return static_cast<uint8_t>(c & 0xFF); }

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

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (resizable_) flags |= SDL_WINDOW_RESIZABLE;
    if (fullscreen_) flags |= SDL_WINDOW_FULLSCREEN;

    window_ = SDL_CreateWindow(title_.c_str(), width_, height_, flags);
    if (!window_) {
        std::fprintf(stderr, "Window could not be created: %s\n",
                     SDL_GetError());
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        std::fprintf(stderr, "Renderer could not be created: %s\n",
                     SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer_, 1);
    vsync_ = true;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_StartTextInput(window_);
    // Der Systemzeiger ist standardmaessig sichtbar. Ein Programm, das ihn
    // nicht will, ruft SET_SYSTEM_POINTER FALSE auf.
    SDL_ShowCursor();
    cursorVisible_ = true;

    keys_ = SDL_GetKeyboardState(nullptr);
    keyDown_.assign(SDL_SCANCODE_COUNT, false);
    keyPressed_.assign(SDL_SCANCODE_COUNT, false);
    openGamepads();
    ensureDefaultFont();
    clearScreen(clearColor_);
    return true;
}

namespace {

uint16_t icoU16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t icoU32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/* Ein .ico enthaelt mehrere Groessen. Genommen wird die groesste; sie liegt
   entweder als PNG (Vista und spaeter) oder als BMP ohne Dateikopf vor. */
bool loadIcoPixels(const std::string& file, std::vector<uint8_t>& rgba, int& w, int& h) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
    if (data.size() < 22 || data[0] != 0 || data[1] != 0 || data[2] != 1 || data[3] != 0) return false;
    const size_t count = icoU16(&data[4]);
    const unsigned char* best = nullptr;
    size_t bestSize = 0;
    int bestW = 0;
    for (size_t i = 0; i < count; ++i) {
        const size_t entry = 6 + i * 16;
        if (entry + 16 > data.size()) break;
        const int entryW = data[entry] == 0 ? 256 : data[entry];
        const size_t offset = icoU32(&data[entry + 12]);
        const size_t size = icoU32(&data[entry + 8]);
        if (size == 0 || offset + size > data.size()) continue;
        if (entryW > bestW) {
            bestW = entryW;
            best = &data[offset];
            bestSize = size;
        }
    }
    if (!best) return false;
    static const unsigned char kPng[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (bestSize >= 8 && std::memcmp(best, kPng, 8) == 0) {
        int comp = 0;
        unsigned char* pixels =
            stbi_load_from_memory(best, static_cast<int>(bestSize), &w, &h, &comp, 4);
        if (!pixels) return false;
        rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
        stbi_image_free(pixels);
        return true;
    }
    // BMP im ICO: BITMAPINFOHEADER, Zeilen von unten, 24 oder 32 Bit.
    if (bestSize < 40) return false;
    const uint32_t header = icoU32(best);
    if (header < 40 || header + 8 > bestSize) return false;
    const int32_t bitmapW = static_cast<int32_t>(icoU32(best + 4));
    const int32_t bitmapH = static_cast<int32_t>(icoU32(best + 8));
    const uint16_t bpp = icoU16(best + 14);
    const uint32_t compression = icoU32(best + 16);
    if (bitmapW <= 0 || bitmapH <= 0 || (bpp != 32 && bpp != 24)) return false;
    if (compression != 0 && compression != 3) return false;
    w = bitmapW;
    h = bitmapH / 2;  // die Hoehe zaehlt Deck- und Datenbild
    if (h <= 0) return false;
    const size_t stride = ((static_cast<size_t>(w) * bpp + 31) / 32) * 4;
    const size_t pixelsAt = header;
    if (pixelsAt + stride * static_cast<size_t>(h) > bestSize) return false;
    rgba.assign(static_cast<size_t>(w) * h * 4, 255);
    bool hasAlpha = false;
    for (int y = 0; y < h; ++y) {
        const unsigned char* row = best + pixelsAt + stride * static_cast<size_t>(h - 1 - y);
        for (int x = 0; x < w; ++x) {
            const size_t src = static_cast<size_t>(x) * (bpp / 8);
            const size_t dst = (static_cast<size_t>(y) * w + x) * 4;
            rgba[dst + 0] = row[src + 2];
            rgba[dst + 1] = row[src + 1];
            rgba[dst + 2] = row[src + 0];
            if (bpp == 32) {
                rgba[dst + 3] = row[src + 3];
                if (row[src + 3]) hasAlpha = true;
            }
        }
    }
    // Aeltere Werkzeuge schreiben 32 Bit ohne Alphakanal: dann ist das Bild
    // deckend und nicht komplett durchsichtig.
    if (bpp == 32 && !hasAlpha) {
        for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    }
    return true;
}

}  // namespace

bool Gfx::setWindowIcon(const std::string& file) {
    if (headless || !window_ || file.empty()) return false;
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    std::string err;
    bool ok = false;
    if (lowerExt(file) == "ico") {
        ok = loadIcoPixels(file, rgba, w, h);
    } else {
        ok = loadImagePixels(file, rgba, w, h, err);
    }
    if (!ok || w <= 0 || h <= 0 || rgba.empty()) {
        std::fprintf(stderr, "%s\n",
                     ("Game icon could not be loaded: " + file).c_str());
        return false;
    }
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, rgba.data(), w * 4);
    if (!surface) return false;
    SDL_SetWindowIcon(window_, surface);
    SDL_DestroySurface(surface);
    return true;
}

void Gfx::setDefaultWindowIcon() {
    if (headless || !window_) return;
    int w = 0, h = 0, comp = 0;
    unsigned char* pixels = stbi_load_from_memory(
        kGbxDefaultIconPng, static_cast<int>(sizeof(kGbxDefaultIconPng)), &w, &h, &comp, 4);
    if (!pixels) return;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4);
    if (surface) {
        SDL_SetWindowIcon(window_, surface);
        SDL_DestroySurface(surface);
    }
    stbi_image_free(pixels);
}


void Gfx::shutdown() {
    if (headless) return;
    closeGamepads();
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
    if (videoTex_) SDL_DestroyTexture(videoTex_);
    videoTex_ = nullptr;
    videoW_ = videoH_ = 0;
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

bool Gfx::beginCamera(float x, float y, float zoom, double angle, std::string& err) {
    if (cameraActive_) {
        err = "CAMERA_BEGIN: a camera is already active";
        return false;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(zoom) || !std::isfinite(angle)) {
        err = "CAMERA_BEGIN: values must be finite";
        return false;
    }
    if (zoom <= 0.0f) {
        err = "CAMERA_BEGIN: zoom must be greater than 0";
        return false;
    }
    cameraX_ = x;
    cameraY_ = y;
    cameraZoom_ = zoom;
    cameraAngle_ = angle;
    cameraActive_ = true;
    return true;
}

void Gfx::endCamera() {
    cameraActive_ = false;
    cameraX_ = cameraY_ = 0.0f;
    cameraZoom_ = 1.0f;
    cameraAngle_ = 0.0;
}

void Gfx::cameraViewCenter(float& cx, float& cy) const {
    if (targetScreen_ >= 0) {
        auto it = screens_.find(targetScreen_);
        if (it != screens_.end()) {
            float tw = 0.0f, th = 0.0f;
            SDL_GetTextureSize(it->second, &tw, &th);
            cx = tw / 2.0f;
            cy = th / 2.0f;
            return;
        }
    }
    if (haveViewport_) {
        cx = static_cast<float>(viewport_.x) + static_cast<float>(viewport_.w) / 2.0f;
        cy = static_cast<float>(viewport_.y) + static_cast<float>(viewport_.h) / 2.0f;
        // SET_VIEWPORT rechnet bei aktivem SCREEN_SCALE in Fensterkoordinaten;
        // gezeichnet wird aber in der logischen Aufloesung.
        if (prescaleW_ > 0 && prescaleH_ > 0 && width_ > 0 && height_ > 0) {
            cx *= static_cast<float>(prescaleW_) / static_cast<float>(width_);
            cy *= static_cast<float>(prescaleH_) / static_cast<float>(height_);
        }
        return;
    }
    if (prescaleW_ > 0 && prescaleH_ > 0) {
        cx = static_cast<float>(prescaleW_) / 2.0f;
        cy = static_cast<float>(prescaleH_) / 2.0f;
    } else {
        cx = static_cast<float>(width_) / 2.0f;
        cy = static_cast<float>(height_) / 2.0f;
    }
}

void Gfx::cameraWorldToScreen(float wx, float wy, float& sx, float& sy) const {
    if (!cameraActive_) {
        sx = wx;
        sy = wy;
        return;
    }
    float cx = 0.0f, cy = 0.0f;
    cameraViewCenter(cx, cy);
    const float dx = (wx - cameraX_) * cameraZoom_;
    const float dy = (wy - cameraY_) * cameraZoom_;
    const double rad = -cameraAngle_ * 3.14159265358979323846 / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    sx = cx + static_cast<float>(dx * c - dy * s);
    sy = cy + static_cast<float>(dx * s + dy * c);
}

void Gfx::cameraScreenToWorld(float sx, float sy, float& wx, float& wy) const {
    if (!cameraActive_) {
        wx = sx;
        wy = sy;
        return;
    }
    float cx = 0.0f, cy = 0.0f;
    cameraViewCenter(cx, cy);
    const float dx = sx - cx;
    const float dy = sy - cy;
    const double rad = -cameraAngle_ * 3.14159265358979323846 / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    wx = cameraX_ + static_cast<float>(dx * c + dy * s) / cameraZoom_;
    wy = cameraY_ + static_cast<float>(-dx * s + dy * c) / cameraZoom_;
}

void Gfx::drawTextureRect(SDL_Texture* tex, const SDL_FRect& src, float x, float y,
                          float w, float h, double angle) {
    if (!tex || !renderer_ || headless) return;
    if (!cameraActive_) {
        SDL_FRect dst{x, y, w, h};
        if (angle == 0.0) {
            SDL_RenderTexture(renderer_, tex, &src, &dst);
        } else {
            SDL_FPoint center{w / 2.0f, h / 2.0f};
            SDL_RenderTextureRotated(renderer_, tex, &src, &dst, -angle, &center, SDL_FLIP_NONE);
        }
        return;
    }
    float cx = 0.0f, cy = 0.0f;
    cameraWorldToScreen(x + w / 2.0f, y + h / 2.0f, cx, cy);
    const float sw = w * cameraZoom_;
    const float sh = h * cameraZoom_;
    SDL_FRect dst{cx - sw / 2.0f, cy - sh / 2.0f, sw, sh};
    SDL_FPoint center{sw / 2.0f, sh / 2.0f};
    SDL_RenderTextureRotated(renderer_, tex, &src, &dst, -(angle + cameraAngle_), &center,
                             SDL_FLIP_NONE);
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

void Gfx::setClearColor(Color color) {
    if (color == static_cast<Color>(-1)) {
        clearEnabled_ = false;
        return;
    }
    clearEnabled_ = true;
    clearColor_ = colorRgb(color);
}

void Gfx::applyBlend(uint8_t& alpha, Color color) const {
    if (!renderer_) return;
    double a = colorAlpha(color) / 255.0;
    if (alphaMode_ > 0.0) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
    } else if (alphaMode_ < 0.0) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        a *= 1.0 + alphaMode_;
        if (a < 0) a = 0;
    } else {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }
    if (a > 1.0) a = 1.0;
    alpha = static_cast<uint8_t>(std::lround(a * 255));
}

uint8_t Gfx::textureAlpha(Color color) const {
    double a = colorAlpha(color) / 255.0;
    if (alphaMode_ < 0.0) {
        a *= 1.0 + alphaMode_;
        if (a < 0) a = 0;
    }
    if (a > 1.0) a = 1.0;
    return static_cast<uint8_t>(std::lround(a * 255));
}

void Gfx::clearScreen(Color color) {
    clearColor_ = colorRgb(color);
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

void Gfx::drawLine(float x1, float y1, float x2, float y2, Color color) {
    if (headless || !renderer_) return;
    if (cameraActive_) {
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        cameraWorldToScreen(x1, y1, ax, ay);
        cameraWorldToScreen(x2, y2, bx, by);
        x1 = ax; y1 = ay; x2 = bx; y2 = by;
    }
    uint8_t a = 255;
    applyBlend(a, color);
    SDL_SetRenderDrawColor(renderer_, redOf(color), greenOf(color), blueOf(color), a);
    SDL_RenderLine(renderer_, x1, y1, x2, y2);
}

void Gfx::drawRect(float x, float y, float w, float h, Color color) {
    if (headless || !renderer_) return;
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    if (cameraActive_) {
        const SDL_FPoint corners[4] = {
            {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}
        };
        SDL_Vertex verts[4];
        uint8_t a = 255;
        applyBlend(a, color);
        for (int i = 0; i < 4; ++i) {
            float sx = 0.0f, sy = 0.0f;
            cameraWorldToScreen(corners[i].x, corners[i].y, sx, sy);
            verts[i].position = SDL_FPoint{sx, sy};
            verts[i].tex_coord = SDL_FPoint{0.0f, 0.0f};
            verts[i].color = SDL_FColor{redOf(color) / 255.0f, greenOf(color) / 255.0f,
                                        blueOf(color) / 255.0f, a / 255.0f};
        }
        const int indices[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(renderer_, nullptr, verts, 4, indices, 6);
        return;
    }
    uint8_t a = 255;
    applyBlend(a, color);
    SDL_SetRenderDrawColor(renderer_, redOf(color), greenOf(color), blueOf(color), a);
    SDL_FRect r{x, y, w, h};
    SDL_RenderFillRect(renderer_, &r);
}

void Gfx::blitPixels(const void* rgba, int w, int h, int stride) {
    if (headless || !renderer_ || !rgba || w <= 0 || h <= 0) return;
    if (stride <= 0) stride = w * 4;
    if (!videoTex_ || videoW_ != w || videoH_ != h) {
        if (videoTex_) SDL_DestroyTexture(videoTex_);
        videoTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STREAMING, w, h);
        videoW_ = w;
        videoH_ = h;
        if (videoTex_) {
            SDL_SetTextureBlendMode(videoTex_, SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(videoTex_, SDL_SCALEMODE_LINEAR);
        }
    }
    if (!videoTex_) return;
    if (!SDL_UpdateTexture(videoTex_, nullptr, rgba, stride)) return;

    SDL_FRect dst;
    if (haveViewport_) {
        dst.x = static_cast<float>(viewport_.x);
        dst.y = static_cast<float>(viewport_.y);
        dst.w = static_cast<float>(viewport_.w);
        dst.h = static_cast<float>(viewport_.h);
    } else if (targetScreen_ >= 0) {
        // Auf einem virtuellen Screen gilt dessen Groesse, nicht die des Fensters.
        float tw = static_cast<float>(width_), th = static_cast<float>(height_);
        auto it = screens_.find(targetScreen_);
        if (it != screens_.end()) SDL_GetTextureSize(it->second, &tw, &th);
        dst = SDL_FRect{0, 0, tw, th};
    } else {
        dst = SDL_FRect{0, 0, static_cast<float>(width_), static_cast<float>(height_)};
    }
    SDL_RenderTexture(renderer_, videoTex_, nullptr, &dst);
}

void Gfx::drawCircle(float cx, float cy, float r, Color color, bool filled) {
    if (headless || !renderer_) return;
    if (r <= 0) return;
    if (cameraActive_) {
        float sx = 0.0f, sy = 0.0f;
        cameraWorldToScreen(cx, cy, sx, sy);
        cx = sx;
        cy = sy;
        r *= cameraZoom_;
    }
    uint8_t a = 255;
    applyBlend(a, color);
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

void Gfx::setPixel(int x, int y, Color color) {
    if (headless || !renderer_) return;
    if (cameraActive_) {
        float sx = 0.0f, sy = 0.0f;
        cameraWorldToScreen(static_cast<float>(x), static_cast<float>(y), sx, sy);
        x = static_cast<int>(std::lround(sx));
        y = static_cast<int>(std::lround(sy));
    }
    uint8_t a = 255;
    applyBlend(a, color);
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

void Gfx::polyVector(float x, float y, float u, float v, Color color) {
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
        float sx = p.x, sy = p.y;
        if (cameraActive_) cameraWorldToScreen(p.x, p.y, sx, sy);
        v.position = SDL_FPoint{sx, sy};
        v.tex_coord = SDL_FPoint{p.u, p.v};
        const float alpha = static_cast<float>(colorAlpha(p.color) / 255.0) *
                            static_cast<float>(mul);
        v.color = SDL_FColor{redOf(p.color) / 255.0f, greenOf(p.color) / 255.0f,
                             blueOf(p.color) / 255.0f, alpha};
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
        err = "Image could not be loaded: " + file;
        return false;
    }
    out.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    return true;
}

bool Gfx::spritePixelData(int id, const void** rgba, int& w, int& h) const {
    const Sprite* sprite = findSprite(id);
    if (!sprite || !sprite->hasPixels || sprite->pixels.empty()) return false;
    if (rgba) *rgba = sprite->pixels.data();
    w = sprite->w;
    h = sprite->h;
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
            err = "Texture could not be created: " + file;
            return false;
        }
    }

    auto it = sprites_.find(id);
    if (it != sprites_.end() && it->second.tex && it->second.ownsTexture)
        SDL_DestroyTexture(it->second.tex);
    sprites_[id] = std::move(sp);
    return true;
}

bool Gfx::reloadSprite(const std::string& file, int id, std::string& err) {
    int frameW = 0, frameH = 0;
    auto it = sprites_.find(id);
    if (it != sprites_.end()) {
        frameW = it->second.frameW;
        frameH = it->second.frameH;
    }
    if (!loadSprite(file, id, err)) return false;
    if (frameW > 0 && frameH > 0) setSpriteAnim(id, frameW, frameH);
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
        err = "SPRITE_GRAB is not available in headless mode";
        return false;
    }
    if (w <= 0 || h <= 0) {
        err = "SPRITE_GRAB: invalid size";
        return false;
    }
    SDL_Rect rect{x, y, w, h};
    SDL_Surface* surf = SDL_RenderReadPixels(renderer_, &rect);
    if (!surf) {
        err = std::string("SPRITE_GRAB failed: ") + SDL_GetError();
        return false;
    }
    SDL_Surface* conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!conv) {
        err = "SPRITE_GRAB: conversion failed";
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
        err = "SPRITE_SAVE: sprite " + std::to_string(id) + " has no image data";
        return false;
    }
    std::string ext = lowerExt(file);
    bool ok = false;
    if (ext == "png")
        ok = stbi_write_png(file.c_str(), s->w, s->h, 4, s->pixels.data(), s->w * 4) != 0;
    else
        ok = stbi_write_bmp(file.c_str(), s->w, s->h, 4, s->pixels.data()) != 0;
    if (!ok) err = "SPRITE_SAVE: writing failed: " + file;
    return ok;
}

bool Gfx::saveBmp(const std::string& file, std::string& err) {
    if (headless || !renderer_) {
        err = "SCREEN_SAVE_BMP is not available in headless mode";
        return false;
    }
    SDL_Surface* surf = SDL_RenderReadPixels(renderer_, nullptr);
    if (!surf) {
        err = std::string("SCREEN_SAVE_BMP: screen could not be read: ") + SDL_GetError();
        return false;
    }
    SDL_Surface* conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!conv) {
        err = "SCREEN_SAVE_BMP: conversion failed";
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
    if (!ok) err = "SCREEN_SAVE_BMP: writing failed: " + file;
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
    drawTextureRect(s->tex, src, x, y, static_cast<float>(s->w), static_cast<float>(s->h), 0.0);
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
    drawTextureRect(s->tex, src, x + (s->w - w) / 2.0f, y + (s->h - h) / 2.0f, w, h, 0.0);
}

void Gfx::drawSpriteRotozoom(int id, float x, float y, double angle, float scale) {
    const Sprite* s = findSprite(id);
    if (!s || !s->tex || headless || !renderer_) return;
    prepareTexture(s->tex);
    float f = scale / 100.0f;
    SDL_FRect src{0, 0, static_cast<float>(s->w), static_cast<float>(s->h)};
    drawTextureRect(s->tex, src, x, y, s->w * f, s->h * f, angle);
}

void Gfx::drawAnim(int id, int frame, float x, float y) {
    const Sprite* s = findSprite(id);
    if (!s || !s->tex || headless || !renderer_) return;
    prepareTexture(s->tex);
    SDL_FRect src;
    int fw = 0, fh = 0;
    frameRect(*s, frame, src, fw, fh);
    drawTextureRect(s->tex, src, x, y, static_cast<float>(fw), static_cast<float>(fh), 0.0);
}

void Gfx::drawAnimRotozoom(int id, int frame, float x, float y, double angle, float scale) {
    const Sprite* s = findSprite(id);
    if (!s || !s->tex || headless || !renderer_) return;
    prepareTexture(s->tex);
    SDL_FRect src;
    int fw = 0, fh = 0;
    frameRect(*s, frame, src, fw, fh);
    float f = scale / 100.0f;
    drawTextureRect(s->tex, src, x, y, fw * f, fh * f, angle);
}

void Gfx::drawAnimStretch(int id, int frame, float x, float y, float w, float h) {
    const Sprite* s = findSprite(id);
    if (!s || !s->tex || headless || !renderer_) return;
    prepareTexture(s->tex);
    SDL_FRect src;
    int fw = 0, fh = 0;
    frameRect(*s, frame, src, fw, fh);
    drawTextureRect(s->tex, src, x, y, w, h, 0.0);
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
        err = "BACKGROUND_LOAD: texture could not be created";
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
    // hat mit SET_COLOR_KEY eine andere Farbe gesetzt.
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
        err = "TTF support is not enabled in this build";
        return false;
    }
#ifdef GBX_HAVE_TTF
    if (!g_ftLibrary && FT_Init_FreeType(&g_ftLibrary) != 0) {
        err = "FreeType could not be initialised";
        return false;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(g_ftLibrary, file.c_str(), 0, &face) != 0) {
        err = "TTF font could not be loaded: " + file;
        return false;
    }
    int pts = points > 0 ? points : 16;
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pts)) != 0) {
        FT_Done_Face(face);
        err = "TTF font size not supported: " + file;
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
    err = "TTF support is not enabled in this build";
    return false;
#endif
}

bool Gfx::reloadFont(int id, const std::string& file, int points, std::string& err) {
    int scale = 1;
    auto it = fonts_.find(id);
    if (it != fonts_.end() && it->second.scale > 0) scale = it->second.scale;
    const bool ok = points > 0 ? loadTtfFont(file, id, points, err) : loadBitmapFont(file, id, err);
    if (!ok) return false;
    auto after = fonts_.find(id);
    if (after != fonts_.end()) after->second.scale = scale;
    return true;
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

// Breite der laengsten Zeile und Hoehe aller Zeilen. So passen TEXT_WIDTH
// und TEXT_HEIGHT auch bei mehrzeiligem Text zu dem, was TEXT_DRAW malt.
void Gfx::textSize(const std::string& text, int& w, int& h) const {
    int lines = 1;
    for (char c : text)
        if (c == '\n') ++lines;
    const FontSlot* f = activeFont();
    if (!f) {
        int lineWidth = 0, maxWidth = 0;
        for (char c : text) {
            if (c == '\n') {
                maxWidth = std::max(maxWidth, lineWidth);
                lineWidth = 0;
                continue;
            }
            lineWidth += 8;
        }
        maxWidth = std::max(maxWidth, lineWidth);
        w = maxWidth;
        h = 8 * lines;
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
        h = f->lineHeight * f->scale * lines;
        return;
#endif
        {
            int lineWidth = 0, maxWidth = 0;
            for (char c : text) {
                if (c == '\n') {
                    maxWidth = std::max(maxWidth, lineWidth);
                    lineWidth = 0;
                    continue;
                }
                lineWidth += static_cast<int>(f->baseSize * 0.6f);
            }
            maxWidth = std::max(maxWidth, lineWidth);
            w = maxWidth * f->scale;
            h = f->cellH * f->scale * lines;
        }
        return;
    }
    int lineWidth = 0, maxWidth = 0;
    for (char c : text) {
        if (c == '\n') {
            maxWidth = std::max(maxWidth, lineWidth);
            lineWidth = 0;
            continue;
        }
        lineWidth += f->cellW;
    }
    maxWidth = std::max(maxWidth, lineWidth);
    w = maxWidth * f->scale;
    h = f->cellH * f->scale * lines;
}

void Gfx::drawGlyphs(const std::string& text, float x, float y, Color color) {
    const FontSlot* f = activeFont();
    if (!f || !f->atlas || headless || !renderer_) return;
    SDL_SetTextureBlendMode(f->atlas, alphaMode_ > 0 ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(f->atlas, redOf(color), greenOf(color), blueOf(color));
    SDL_SetTextureAlphaMod(f->atlas, textureAlpha(color));

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
        drawTextureRect(f->atlas, src, dst.x, dst.y, dst.w, dst.h, 0.0);
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

void Gfx::drawText(const std::string& text, float x, float y, Color color) {
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
            SDL_SetTextureAlphaMod(g->tex, textureAlpha(color));
            SDL_FRect dst{pen + static_cast<float>(g->bearingX) * f->scale,
                          baseline - static_cast<float>(g->bearingY) * f->scale,
                          static_cast<float>(g->w) * f->scale,
                          static_cast<float>(g->h) * f->scale};
            SDL_FRect src{0, 0, static_cast<float>(g->w), static_cast<float>(g->h)};
            drawTextureRect(g->tex, src, dst.x, dst.y, dst.w, dst.h, 0.0);
        }
        if (g) pen += static_cast<float>(g->advance) * f->scale;
        prev = cp;
    }
#endif
}

void Gfx::drawTextCentered(const std::string& text, float cx, float y, Color color) {
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
    // Eine vergessene CAMERA_END darf nicht in das naechste Bild laufen;
    // HUD-Aufrufe nach SCREEN_SHOW bleiben dadurch normale Bildschirmzeichen.
    endCamera();
    if (onFrame) onFrame();
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

// ------------------------------------------------------------------- Gamepad

// Alle angeschlossenen Pads beim Start oeffnen. Neue Pads waehrend des Laufs
// kommen ueber SDL_EVENT_GAMEPAD_ADDED dazu (siehe pumpEventsImpl).
void Gfx::openGamepads() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) return;
    for (int i = 0; i < count; ++i) {
        SDL_Gamepad* handle = SDL_OpenGamepad(ids[i]);
        if (!handle) continue;
        JoyPad pad;
        pad.handle = handle;
        joyPads_.push_back(pad);
    }
    SDL_free(ids);
}

void Gfx::closeGamepads() {
    for (auto& pad : joyPads_) {
        if (pad.handle) SDL_CloseGamepad(pad.handle);
        pad.handle = nullptr;
    }
    joyPads_.clear();
}

// Einmal pro Frame: Tastenzustand einsammeln und den Druck dieses Frames
// bilden. Was als Event kam, steht da schon in `hit`.
void Gfx::pollGamepads() {
    for (auto& pad : joyPads_) {
        if (!pad.handle) continue;
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
            const bool now = SDL_GetGamepadButton(pad.handle, static_cast<SDL_GamepadButton>(button));
            if (now && !pad.down[static_cast<size_t>(button)]) pad.hit[static_cast<size_t>(button)] = true;
            pad.down[static_cast<size_t>(button)] = now;
        }
    }
}

double Gfx::joyAxis(int pad, int axis) const {
    if (pad < 0 || pad >= static_cast<int>(joyPads_.size())) return 0.0;
    SDL_Gamepad* handle = joyPads_[static_cast<size_t>(pad)].handle;
    if (!handle || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) return 0.0;
    const Sint16 raw = SDL_GetGamepadAxis(handle, static_cast<SDL_GamepadAxis>(axis));
    return static_cast<double>(raw) / 32767.0;
}

bool Gfx::joyDown(int pad, int button) const {
    if (headless || pad < 0 || pad >= static_cast<int>(joyPads_.size())) return false;
    if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT) return false;
    return joyPads_[static_cast<size_t>(pad)].down[static_cast<size_t>(button)];
}

bool Gfx::joyHit(int pad, int button) const {
    if (headless || pad < 0 || pad >= static_cast<int>(joyPads_.size())) return false;
    if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT) return false;
    return joyPads_[static_cast<size_t>(pad)].hit[static_cast<size_t>(button)];
}

// -------------------------------------------------------------------- Eingabe

void Gfx::pumpEventsImpl() {
    if (!headless) {
        std::fill(keyPressed_.begin(), keyPressed_.end(), false);
        anyKeyHit_ = false;
        wheel_ = 0;
        mbLeftHit_ = mbRightHit_ = mbMiddleHit_ = false;
        for (auto& pad : joyPads_) std::fill(pad.hit.begin(), pad.hit.end(), false);
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
            case SDL_EVENT_GAMEPAD_ADDED: {
                // Beim Start schon angeschlossene Pads meldet SDL erneut; sie
                // stehen dann bereits in der Liste.
                bool known = false;
                for (const auto& pad : joyPads_)
                    if (pad.handle && SDL_GetGamepadID(pad.handle) == e.gdevice.which) known = true;
                SDL_Gamepad* handle = known ? nullptr : SDL_OpenGamepad(e.gdevice.which);
                if (handle) {
                    JoyPad pad;
                    pad.handle = handle;
                    joyPads_.push_back(pad);
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_REMOVED: {
                for (auto it = joyPads_.begin(); it != joyPads_.end(); ++it) {
                    if (!it->handle || SDL_GetGamepadID(it->handle) != e.gdevice.which) continue;
                    SDL_CloseGamepad(it->handle);
                    joyPads_.erase(it);
                    break;
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
                for (auto& pad : joyPads_) {
                    if (!pad.handle || SDL_GetGamepadID(pad.handle) != e.gbutton.which) continue;
                    if (e.gbutton.button < SDL_GAMEPAD_BUTTON_COUNT)
                        pad.hit[static_cast<size_t>(e.gbutton.button)] = true;
                    break;
                }
                break;
            }
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
        pollGamepads();
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

// ------------------------------------------------- Fenster fuer Module
// host->window_handle / host->window_size / host->gl_context (gbx_inline.h).
// Damit koennen Module eigene Renderer auf dem GBX-Fenster aufsetzen.

void Gfx::windowSize(int& width, int& height) const {
    width = width_;
    height = height_;
    if (window_) {
        int w = width_;
        int h = height_;
        if (SDL_GetWindowSizeInPixels(window_, &w, &h)) {
            width = w;
            height = h;
        }
    }
}

void* Gfx::nativeWindowHandle() const {
    if (!window_) return nullptr;
    SDL_PropertiesID props = SDL_GetWindowProperties(window_);
    if (!props) return nullptr;
#if defined(_WIN32)
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(__EMSCRIPTEN__)
    return nullptr;
#else
    void* surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    if (surface) return surface;
    const Sint64 x11 = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    return x11 ? reinterpret_cast<void*>(static_cast<uintptr_t>(x11)) : nullptr;
#endif
}
