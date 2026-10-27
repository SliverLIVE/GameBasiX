/* spline.h - plain C++, the interface of lib/libcurve.a
 *
 * The example links this archive differently on purpose: libgame.a is named
 * with its path in LIBS, libcurve.a comes in as -lcurve from LIBDIRS + FLAGS.
 */
#ifndef GBX_EXAMPLE_SPLINE_H
#define GBX_EXAMPLE_SPLINE_H

/* Smooth curve through five control points, u runs 0 … 1 and wraps around. */
double spline_point(double u);

#endif /* GBX_EXAMPLE_SPLINE_H */
