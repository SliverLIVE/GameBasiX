/* curve.h - small C++ helpers for the INLINE example
 *
 * The top block in main.gbx includes this file:
 *
 *     INLINE
 *     #include "curve.h"
 *     ENDINLINE
 *
 * After that ROSE_X, ROSE_Y, WOBBLE and HUE are ordinary GBX commands.
 * Everything here is plain C++ - the file sits next to the .gbx file,
 * so the compiler finds it without an extra include path.
 */
#ifndef GBX_EXAMPLE_CURVE_H
#define GBX_EXAMPLE_CURVE_H

#include <gbx_inline.h>

#include <algorithm>
#include <cmath>

namespace gbxcurve {

constexpr double kPi = 3.14159265358979323846;

inline double num(const GbxValue* args, int argc, int index) {
    return gbxinline::argNum(args, argc, index);
}

/* Rose curve r = cos(petals * theta). u = 0 … 1 is one full turn.
 * Even petal counts produce twice as many leaves - that is how rose
 * curves work, and it is easy to see in the example. */
inline double roseX(double u, double radius, double petals) {
    const double theta = u * 2.0 * kPi;
    return std::cos(theta) * std::cos(petals * theta) * radius;
}

inline double roseY(double u, double radius, double petals) {
    const double theta = u * 2.0 * kPi;
    return std::sin(theta) * std::cos(petals * theta) * radius;
}

inline void fnRoseX(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, roseX(num(args, argc, 0), num(args, argc, 1), num(args, argc, 2)));
}

inline void fnRoseY(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, roseY(num(args, argc, 0), num(args, argc, 1), num(args, argc, 2)));
}

/* Two sine waves added together: the result lies in -amount … +amount. */
inline void fnWobble(const GbxValue* args, int argc, GbxValue* ret) {
    const double u = num(args, argc, 0);
    const double amount = num(args, argc, 1);
    const double wave = std::sin(u * 2.0 * kPi * 1.7) * 0.6 +
                        std::sin(u * 2.0 * kPi * 3.9 + 1.0) * 0.4;
    gbxinline::retNum(ret, amount * wave);
}

/* Rainbow: u 0 … 1 -> 0xRRGGBB, brightness 0 … 1 darkens it.
 * The colours use the same order as RGB(r, g, b). */
inline void fnHue(const GbxValue* args, int argc, GbxValue* ret) {
    const double u = num(args, argc, 0);
    const double brightness = gbxinline::argNum(args, argc, 1, 1.0);
    const double angle = u * 2.0 * kPi;
    auto channel = [&](double phase) {
        const double value = (128.0 + 127.0 * std::sin(angle + phase)) * brightness;
        return std::clamp(static_cast<int>(std::lround(value)), 0, 255);
    };
    const int r = channel(0.0);
    const int g = channel(2.0 * kPi / 3.0);
    const int b = channel(4.0 * kPi / 3.0);
    gbxinline::retNum(ret, (r << 16) | (g << 8) | b);
}

}  // namespace gbxcurve

/* Register with GBX: name, min arguments, max arguments, function. */
GBX_INLINE_FN("ROSE_X", 3, 3, gbxcurve::fnRoseX);
GBX_INLINE_FN("ROSE_Y", 3, 3, gbxcurve::fnRoseY);
GBX_INLINE_FN("WOBBLE", 2, 2, gbxcurve::fnWobble);
GBX_INLINE_FN("HUE", 2, 2, gbxcurve::fnHue);

#endif /* GBX_EXAMPLE_CURVE_H */
