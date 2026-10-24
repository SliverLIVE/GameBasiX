/* sparks.h - C++ helpers for the sparks example
 *
 * The top INLINE block in main.gbx includes this file. Afterwards
 * GLOWCOLOR (and internally the helpers below) are available; the steps in
 * launch() and step() call the templates directly.
 *
 * The templates work on the BASIC TYPE array: sparks.size(),
 * sparks[i].x and so on are the generated C++ proxies from gbx_inline.h.
 * That is why they stay templates here - the TYPE only comes into being
 * while the BASIC program is compiled.
 */
#ifndef GBX_EXAMPLE_SPARKS_H
#define GBX_EXAMPLE_SPARKS_H

#include <gbx_inline.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace gbxsparks {

constexpr double kPi = 3.14159265358979323846;

/* Fixed seed: the same sequence every time, handy for experimenting.
 * For real randomness, seed `engine` with std::random_device. */
inline std::mt19937& generator() {
    static std::mt19937 engine(0xC0FFEEu);
    return engine;
}

inline double randomUnit() {
    static std::uniform_real_distribution<double> distribution(0.0, 1.0);
    return distribution(generator());
}

inline double randomRange(double from, double to) { return from + (to - from) * randomUnit(); }

/* HSV -> 0xRRGGBB like RGB(r, g, b); h, s and v range from 0 … 1. */
inline int hueColor(double h, double s, double v) {
    h = h - std::floor(h);
    const double six = h * 6.0;
    const int sector = static_cast<int>(std::floor(six)) % 6;
    const double rest = six - std::floor(six);
    const double p = v * (1.0 - s);
    const double q = v * (1.0 - s * rest);
    const double t = v * (1.0 - s * (1.0 - rest));
    double r = v;
    double g = t;
    double b = p;
    switch (sector) {
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
        default: break;
    }
    const auto channel = [](double value) {
        return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
    };
    return (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

inline void fnGlowColor(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, hueColor(gbxinline::argNum(args, argc, 0),
                                    gbxinline::argNum(args, argc, 1, 1.0),
                                    gbxinline::argNum(args, argc, 2, 1.0)));
}

/* Find a free slot starting at `from`. The caller remembers the position so
 * an explosion does not walk the array from the start for every spark. */
template <typename Array>
int freeSlot(Array& sparks, int& from) {
    const int count = sparks.size();
    for (int i = from; i < count; ++i) {
        if (sparks[i].life <= 0.0) {
            from = i + 1;
            return i;
        }
    }
    from = count;
    return -1;
}

/* Advance the hue by the golden angle: consecutive fireworks get red, blue,
 * green, magenta … instead of random similarities. */
inline double nextHue() {
    static int index = 0;
    const double tone = 0.02 + 0.6180339887 * static_cast<double>(++index);
    return tone - std::floor(tone);
}

/* Launch a rocket at the bottom at point x. life is the fuse time in seconds. */
template <typename Array>
void launch(Array& sparks, double x) {
    int from = 0;
    const int i = freeSlot(sparks, from);
    if (i < 0) return;
    auto s = sparks[i];
    s.x = x;
    s.y = 628.0;
    s.vx = randomRange(-26.0, 26.0);
    s.vy = -randomRange(360.0, 450.0);
    s.life = randomRange(0.75, 1.05);
    s.total = s.life;
    s.hue = nextHue();
    s.kind = 0;
}

/* A single spark, e.g. as a trail behind a rocket. */
template <typename Array>
void glow(Array& sparks, double x, double y, double hue) {
    int from = 0;
    const int i = freeSlot(sparks, from);
    if (i < 0) return;
    auto s = sparks[i];
    s.x = x;
    s.y = y;
    s.vx = randomRange(-18.0, 18.0);
    s.vy = randomRange(-10.0, 35.0);
    s.life = randomRange(0.25, 0.5);
    s.total = s.life;
    s.hue = hue;
    s.kind = 1;
}

/* Firework body: many sparks in all directions, a few of them faster. */
template <typename Array>
void explode(Array& sparks, double x, double y, double hue) {
    const int count = 80 + static_cast<int>(randomUnit() * 50.0);
    int from = 0;
    for (int n = 0; n < count; ++n) {
        const int i = freeSlot(sparks, from);
        if (i < 0) break;
        auto s = sparks[i];
        const double angle = randomRange(0.0, 2.0 * kPi);
        const double speed = randomRange(35.0, 250.0) * (n % 9 == 0 ? 1.3 : 1.0);
        s.x = x;
        s.y = y;
        s.vx = std::cos(angle) * speed;
        s.vy = std::sin(angle) * speed - 30.0;
        s.life = randomRange(1.3, 2.6);
        s.total = s.life;
        /* A few sparks in the opposite colour make the body livelier. */
        s.hue = hue + randomRange(-0.04, 0.04) + (n % 13 == 0 ? 0.5 : 0.0);
        s.kind = 1;
    }
}

}  // namespace gbxsparks

/* GLOWCOLOR(hue[, saturation[, brightness]]) - for drawing. */
GBX_INLINE_FN("GLOWCOLOR", 1, 3, gbxsparks::fnGlowColor);

#endif /* GBX_EXAMPLE_SPARKS_H */
