/* spline.cpp - the C++ half of lib/libcurve.a
 *
 * A Catmull-Rom spline through five fixed control points. The curve wraps, so
 * SPLINE(u) can be sampled with any u - the example walks it forever.
 *
 *   g++ -O2 -c native/spline.cpp -o curve.o
 *   ar rcs lib/libcurve.a curve.o
 */
#include "spline.h"

#include <cmath>

namespace {

constexpr int kPoints = 5;
constexpr double kValues[kPoints] = {0.0, 0.85, -0.55, 0.35, -0.90};

double at(int index) {
    const int wrapped = ((index % kPoints) + kPoints) % kPoints;
    return kValues[wrapped];
}

}  // namespace

double spline_point(double u) {
    const double wrapped = u - std::floor(u);      /* any u -> 0 … 1 */
    const double scaled = wrapped * kPoints;
    const int index = static_cast<int>(scaled);
    const double f = scaled - index;

    const double p0 = at(index - 1);
    const double p1 = at(index);
    const double p2 = at(index + 1);
    const double p3 = at(index + 2);

    const double f2 = f * f;
    const double f3 = f2 * f;
    return 0.5 * ((2.0 * p1) + (-p0 + p2) * f + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * f2 +
                  (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * f3);
}
