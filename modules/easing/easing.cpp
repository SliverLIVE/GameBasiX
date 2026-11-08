#include "easing.h"

#include <cmath>

namespace {

double clamped(double t) {
    return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
}

}  // namespace

extern "C" double easing_value(double start, double end, double t, int mode) {
    const double k = clamped(t);
    double eased = k;
    switch (mode) {
        case 1:  // smoothstep
            eased = k * k * (3.0 - 2.0 * k);
            break;
        case 2:  // cubic in/out
            eased = k < 0.5 ? 4.0 * k * k * k
                            : 1.0 - std::pow(-2.0 * k + 2.0, 3.0) / 2.0;
            break;
        case 3: {  // back out
            const double q = k - 1.0;
            eased = 1.0 + 2.70158 * q * q * q + 1.70158 * q * q;
            break;
        }
        default:  // linear
            break;
    }
    return start + (end - start) * eased;
}
