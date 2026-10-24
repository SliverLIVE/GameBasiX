// GameBasiX - SDL3-Grafik-, Text- und Eingabeschicht
#pragma once

#include <SDL3/SDL.h>

#ifdef GBX_HAVE_TTF
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct Sprite {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
    int frameW = 0, frameH = 0;      // >0: Animation (Kachelgroesse)
    std::vector<uint8_t> pixels;      // RGBA8888, fuer Pixelkollision
    bool hasPixels = false;
    bool ownsTexture = true;
};

struct GlyphInfo {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
    int bearingX = 0, bearingY = 0;
    int advance = 0;
};

struct FontSlot {
    bool isTtf = false;
    bool valid = false;
    int baseSize = 8;      // Zellhoehe in Pixeln bzw. TTF-Punktgroesse
    int cellW = 8, cellH = 8;
    int scale = 1;
    SDL_Texture* atlas = nullptr;
    int atlasCols = 16;
    #ifdef GBX_HAVE_TTF
    FT_Face face = nullptr;
    int ascent = 0;
    int lineHeight = 0;
    #endif
};

struct PolyVertex {
    float x = 0, y = 0;
    float u = 0, v = 0;
    uint32_t color = 0xFFFFFF;
};

class Gfx {
public:
    bool headless = false;

    bool init(int w, int h, const std::string& title, bool fullscreen, bool resizable);
    void shutdown();

    // Fenster / Bildschirm
    void setScreen(int w, int h, int fullscreen);
    void setTitle(const std::string& title);
    int screenWidth() const { return width_; }
    int screenHeight() const { return height_; }
    bool isFullscreen() const { return fullscreen_; }
    void desktopSize(int& w, int& h) const;
    void setPrescaler(int w, int h);
    void setViewport(int x, int y, int w, int h);
    void getViewport(int& x, int& y, int& w, int& h) const;
    int nextSpriteId() const;
    int nextFontId() const;
    int nextScreenId() const;
    void clearScreen(uint32_t color);
    void setClearColor(uint32_t color);
    void blackScreen();
    void present();
    void setLimitFps(double fps, bool vsync);
    void setSmoothShading(bool smooth);

    // Zeichnen
    void setAlphaMode(double alpha) { alphaMode_ = alpha; }
    void drawLine(float x1, float y1, float x2, float y2, uint32_t color);
    void drawRect(float x, float y, float w, float h, uint32_t color);
    void drawCircle(float cx, float cy, float r, uint32_t color, bool filled);
    void setPixel(int x, int y, uint32_t color);
    uint32_t getPixel(int x, int y);

    // Polygone
    void startPoly(int spriteId, int mode);
    void polyVector(float x, float y, float u, float v, uint32_t color);
    void endPoly();

    // Sprites
    bool loadSprite(const std::string& file, int id, std::string& err);
    bool grabSprite(int id, int x, int y, int w, int h, std::string& err);
    bool saveSprite(const std::string& file, int id, std::string& err);
    bool spriteInfo(int id, int& w, int& h) const;
    void setSpriteAnim(int id, int frameW, int frameH);
    void drawSprite(int id, float x, float y);
    void drawSpriteZoom(int id, float x, float y, float sx, float sy);
    void drawSpriteRotozoom(int id, float x, float y, double angle, float scale);
    void drawAnim(int id, int frame, float x, float y);
    void drawAnimRotozoom(int id, int frame, float x, float y, double angle, float scale);
    void drawAnimStretch(int id, int frame, float x, float y, float w, float h);
    int spriteWidth(int id) const;
    int spriteHeight(int id) const;
    int animFrameCount(int id) const;
    bool spriteCollision(int id1, float x1, float y1, int id2, float x2, float y2) const;
    bool animCollision(int id1, int frame1, float x1, float y1, int id2, int frame2, float x2, float y2) const;
    void deleteSprite(int id);

    // Bildschirmfotos / Hintergrund
    bool saveBmp(const std::string& file, std::string& err);
    bool loadBackground(const std::string& file, std::string& err);
    void useAsBackground();
    void blendScreen(const std::string& file, int ms);

    // Virtuelle Bildschirme
    void createScreen(int screenId, int spriteId, int w, int h);
    void useScreen(int screenId);

    // Transparenz / Alpha-Test
    void setTransparency(uint32_t color) {
        transparencyKey_ = color & 0xFFFFFF;
        transparencySet_ = true;
    }
    void setAlphaTesting(double v) { alphaTesting_ = v; }
    void setAlphaModeRaw(double v) { alphaMode_ = v; }
    uint32_t transparencyKey() const { return transparencyKey_; }
    double alphaTesting() const { return alphaTesting_; }

    // Text
    bool loadBitmapFont(const std::string& file, int id, std::string& err);
    bool loadTtfFont(const std::string& file, int id, int points, std::string& err);
    bool hasTtf() const;
    void setFont(int id);
    void setFontScale(int scale);
    int currentFont() const { return currentFont_; }
    void fontCharSize(int& w, int& h) const;
    void drawText(const std::string& text, float x, float y, uint32_t color);
    void textSize(const std::string& text, int& w, int& h) const;
    void drawTextCentered(const std::string& text, float cx, float y, uint32_t color);

    // Eingabe
    void pump();
    bool keyDown(int scancode) const;
    bool keyHit(int scancode) const;
    bool anyKeyHit() const { return anyKeyHit_; }
    std::string takeTextInput();
    std::string takeInkey();
    void mouseState(float& x, float& y, bool& left, bool& right, bool& middle) const;
    int mouseButton(int n) const;
    int mouseWheel() const { return wheel_; }
    bool mouseHit(int n) const;
    void setMouse(float x, float y);
    void showCursor(bool on);
    void waitMouse();
    void waitKey();
    bool quitRequested() const { return quit_; }
    void requestQuit() { quit_ = true; }
    void setAllowEscape(bool v) { allowEscape_ = v; }
    bool allowEscape() const { return allowEscape_; }

    // Zeit
    double timerFrame() const;  // ms seit letztem SHOWSCREEN
    double timerAll() const;    // ms seit Programmstart
    void resetFrameTimer();

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    int width_ = 640, height_ = 640 / 2;
    bool fullscreen_ = false;
    std::string title_ = "GameBasiX";
    bool resizable_ = true;

    uint32_t clearColor_ = 0x000000;
    bool clearEnabled_ = true;
    double alphaMode_ = 0.0;
    double alphaTesting_ = 1.0;
    uint32_t transparencyKey_ = 0xFF0080;
    bool transparencySet_ = false;

    SDL_Texture* background_ = nullptr;
    int bgW_ = 0, bgH_ = 0;

    int prescaleW_ = 0, prescaleH_ = 0;
    SDL_Rect viewport_{};
    bool haveViewport_ = false;

    int targetScreen_ = -1;
    std::unordered_map<int, SDL_Texture*> screens_;

    std::unordered_map<int, Sprite> sprites_;
    std::unordered_map<int, FontSlot> fonts_;
    std::unordered_map<std::string, GlyphInfo> glyphCache_;
    int currentFont_ = 0;
    void ensureDefaultFont();
    const FontSlot* activeFont() const;

    bool polyActive_ = false;
    int polySprite_ = -1;
    int polyMode_ = 0;
    std::vector<PolyVertex> polyVerts_;

    // Eingabe
    const bool* keys_ = nullptr;
    std::vector<bool> keyDown_;
    std::vector<bool> keyPressed_;
    bool anyKeyHit_ = false;
    std::string textInput_;
    std::string inkey_;
    float mouseX_ = 0, mouseY_ = 0;
    bool mbLeft_ = false, mbRight_ = false, mbMiddle_ = false;
    bool mbLeftHit_ = false, mbRightHit_ = false, mbMiddleHit_ = false;
    int wheel_ = 0;
    bool quit_ = false;
    bool allowEscape_ = true;
    bool cursorVisible_ = true;

    // Zeit / FPS
    Uint64 startTicks_ = 0;
    Uint64 lastPresent_ = 0;
    Uint64 frameStart_ = 0;
    double limitFps_ = 0.0;
    bool vsync_ = false;
    Uint64 lastFrameDuration_ = 16;

    void applyViewport();
    void applyBlend(uint8_t& alpha) const;
    void beginFrameImpl();
    void pumpEventsImpl();
    SDL_Texture* textureFromPixels(const uint8_t* rgba, int w, int h);
    void prepareTexture(SDL_Texture* tex);
    SDL_Texture* makeTexture(const std::vector<uint8_t>& rgba, int w, int h);
    bool loadImagePixels(const std::string& file, std::vector<uint8_t>& out, int& w, int& h,
                         std::string& err);
    void drawGlyphs(const std::string& text, float x, float y, uint32_t color);
    const GlyphInfo* glyphFor(int fontId, const FontSlot& f, uint32_t cp);
    const Sprite* findSprite(int id) const;
};
