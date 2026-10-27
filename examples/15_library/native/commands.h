/* commands.h - the GBX side of lib/libgame.a and lib/libcurve.a
 *
 * One header for the whole example, because GBX only needs to see this file:
 *
 *   game.h, spline.h   plain declarations, used by the .cpp files
 *   this file          turns the functions into GBX commands
 *
 * The top INLINE block of main.gbx includes this header. The commands come
 * from the registrations here, the code from the two archives - the linker
 * joins both halves into one INLINE library.
 *
 * GAIN arrives as -DGAIN=1.6 from the [native] section of the project file.
 * The archives are already built when the block is compiled, so the flag can
 * only change this header - and that is exactly what happens: SPLINE gets
 * scaled here and GAIN() reports the value.
 *
 * An INLINE step does not need a registered command at all: it can call
 * game_filter_step() directly, because the block is compiled with this header
 * and linked against the archive.
 */
#ifndef GBX_EXAMPLE_COMMANDS_H
#define GBX_EXAMPLE_COMMANDS_H

#include <gbx_inline.h>

#include "game.h"
#include "spline.h"

#ifndef GAIN
#define GAIN 1.0
#endif

/* --------------------------------------------------------- libgame.a */

inline void fnFilterStep(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, game_filter_step(gbxinline::argNum(args, argc, 0)));
}

inline void fnFilterAlpha(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, game_filter_alpha());
}

inline void fnFilterReset(const GbxValue*, int, GbxValue* ret) {
    game_filter_reset();
    gbxinline::retNum(ret, 0);
}

inline void fnFilterCalls(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, game_filter_calls());
}

inline void fnNoise(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, game_noise(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnWord(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retText(ret, game_word(gbxinline::argNum(args, argc, 0)));
}

/* -------------------------------------------------------- libcurve.a */

inline void fnSpline(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, spline_point(gbxinline::argNum(args, argc, 0)) * GAIN);
}

inline void fnGain(const GbxValue*, int, GbxValue* ret) { gbxinline::retNum(ret, GAIN); }

/* Name, minimum arguments, maximum arguments, function. */
GBX_INLINE_FN("FILTER_STEP", 1, 1, fnFilterStep);
GBX_INLINE_FN("FILTER_ALPHA", 0, 0, fnFilterAlpha);
GBX_INLINE_FN("FILTER_RESET", 0, 0, fnFilterReset);
GBX_INLINE_FN("FILTER_CALLS", 0, 0, fnFilterCalls);
GBX_INLINE_FN("NOISE", 1, 1, fnNoise);
GBX_INLINE_FN("WORD$", 1, 1, fnWord);
GBX_INLINE_FN("SPLINE", 1, 1, fnSpline);
GBX_INLINE_FN("GAIN", 0, 0, fnGain);

#endif /* GBX_EXAMPLE_COMMANDS_H */
