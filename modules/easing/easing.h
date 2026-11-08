#ifndef GBX_EASING_H
#define GBX_EASING_H

/*
 * C interface of the EASING module library.
 *
 * The archive lib/libeasing.a is linked into desktop INLINE builds. The HTML5
 * player cannot use a Windows archive, so EASING.gbx compiles the same
 * algorithm directly for the browser.
 */
#ifdef __cplusplus
extern "C" {
#endif

/*
 * @tooltip EASE_VALUE
 * **EASE_VALUE start, end, t[, mode]**
 *
 * Interpolates from `start` to `end`; `t` is clamped to 0…1. The optional
 * `mode` picks the curve:
 *
 * | mode | curve |
 * | --- | --- |
 * | 0 | linear (default) |
 * | 1 | smoothstep |
 * | 2 | cubic in/out |
 * | 3 | back out |
 *
 * ```gbx
 * x = EASE_VALUE(0, 320, t, 1)
 * ```
 * @endtooltip
 */
double easing_value(double start, double end, double t, int mode);

#ifdef __cplusplus
}
#endif

#endif /* GBX_EASING_H */
