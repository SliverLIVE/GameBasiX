/* game.h - plain C++, the interface of lib/libgame.a
 *
 * No GBX types in here: the archive must stay usable from any C++ program.
 * The GBX commands that call these functions are registered in commands.h.
 */
#ifndef GBX_EXAMPLE_GAME_H
#define GBX_EXAMPLE_GAME_H

/* One-pole low pass. Keeps its state between calls, so a signal can be
 * smoothed across frames. */
double game_filter_step(double value);

/* Coefficient of the filter (0 = frozen, 1 = no filtering). */
double game_filter_alpha();

/* Forgets the state and the call counter. */
void game_filter_reset();

/* How often game_filter_step() ran since the reset. */
int game_filter_calls();

/* Hash noise: the same seed always gives the same value in -1 … 1. */
double game_noise(int seed);

/* Writes a signal value as text: "calm", "moving" or "lively". */
const char* game_word(double value);

#endif /* GBX_EXAMPLE_GAME_H */
