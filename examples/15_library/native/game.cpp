/* game.cpp - the C++ half of lib/libgame.a
 *
 * Plain C++ with no GBX types: nothing here knows that a BASIC program calls
 * it later. Build it with the same compiler that gbx uses for INLINE blocks:
 *
 *   GBX Studio:  Tools > Build library   (writes lib/libgame.a from native/)
 *   by hand:     g++ -O2 -c native/game.cpp -o game.o
 *                ar rcs lib/libgame.a game.o
 *
 * The archive is linked into the INLINE library of the project. On export it
 * ends up inside the finished game, so a player needs neither the archive nor
 * a compiler.
 */
#include "game.h"

#include <cmath>

namespace {

constexpr double kAlpha = 0.18;   /* filter coefficient: 0.18 of the new value */

double g_state = 0.0;
int g_calls = 0;

}  // namespace

double game_filter_step(double value) {
    g_state += (value - g_state) * kAlpha;
    ++g_calls;
    return g_state;
}

double game_filter_alpha() { return kAlpha; }

void game_filter_reset() {
    g_state = 0.0;
    g_calls = 0;
}

int game_filter_calls() { return g_calls; }

/* xorshift-style hash: cheap, deterministic, and good enough as noise. */
double game_noise(int seed) {
    unsigned int x = static_cast<unsigned int>(seed) * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return static_cast<double>(x) / 2147483647.5 - 1.0;
}

const char* game_word(double value) {
    const double size = std::fabs(value);
    if (size < 0.15) return "calm";
    if (size < 0.55) return "moving";
    return "lively";
}
