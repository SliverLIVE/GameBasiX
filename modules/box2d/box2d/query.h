/* box2d/query.h - point and ray queries
 *
 * Both work on the rotated boxes themselves, not on bounding circles: a
 * point counts as inside when it is inside the oriented box, and the ray is
 * clipped against the four sides in the body's local frame, so the normal is
 * the exact face that was hit.
 */
#ifndef GBX_MODULE_BOX2D_QUERY_H
#define GBX_MODULE_BOX2D_QUERY_H

#include "common.h"

namespace box2dgbx {

inline bool pointInside(const Body& body, double px, double py) {
    const double dx = px - body.position.x;
    const double dy = py - body.position.y;
    const double c = std::cos(-body.rotation);
    const double s = std::sin(-body.rotation);
    const double lx = c * dx - s * dy;
    const double ly = s * dx + c * dy;
    return std::fabs(lx) <= body.width.x * 0.5 && std::fabs(ly) <= body.width.y * 0.5;
}

/* The oldest body under a point, or 0. Static bodies are skipped when
 * includeStatic is 0. */
inline int bodyAt(double px, double py, int includeStatic) {
    const auto& actors = state().actors;
    for (std::size_t i = 0; i < actors.size(); ++i) {
        Actor* actor = actors[i].get();
        if (!actor || !actor->alive || !actor->body) continue;
        if (!includeStatic && actor->isStatic) continue;
        if (pointInside(*actor->body, px, py)) return static_cast<int>(i) + 1;
    }
    return 0;
}

/* Ray against every body: origin, direction vector, largest t (default 1).
 * Stores the hit for BOX2D_HIT_* and returns the body ID or 0. */
inline int raycast(double ox, double oy, double dx, double dy, double maxT) {
    State& s = state();
    s.hit = RayHit();
    double best = maxT <= 0.0 ? 1.0 : maxT;
    const auto& actors = s.actors;

    for (std::size_t i = 0; i < actors.size(); ++i) {
        Actor* actor = actors[i].get();
        if (!actor || !actor->alive || !actor->body) continue;
        const Body& body = *actor->body;
        const double hw = body.width.x * 0.5;
        const double hh = body.width.y * 0.5;
        const double c = std::cos(-body.rotation);
        const double sn = std::sin(-body.rotation);
        const double rx = ox - body.position.x;
        const double ry = oy - body.position.y;
        const double lx = c * rx - sn * ry;
        const double ly = sn * rx + c * ry;
        const double ldx = c * dx - sn * dy;
        const double ldy = sn * dx + c * dy;

        double enter = -1.0e30;
        double exit = 1.0e30;
        int enterAxis = -1;
        double enterSign = 0.0;

        if (std::fabs(ldx) < 1.0e-9) {
            if (lx < -hw || lx > hw) continue;
        } else {
            double t1 = (-hw - lx) / ldx;
            double t2 = (hw - lx) / ldx;
            const double sign = ldx > 0.0 ? -1.0 : 1.0;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > enter) {
                enter = t1;
                enterAxis = 0;
                enterSign = sign;
            }
            if (t2 < exit) exit = t2;
        }
        if (std::fabs(ldy) < 1.0e-9) {
            if (ly < -hh || ly > hh) continue;
        } else {
            double t1 = (-hh - ly) / ldy;
            double t2 = (hh - ly) / ldy;
            const double sign = ldy > 0.0 ? -1.0 : 1.0;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > enter) {
                enter = t1;
                enterAxis = 1;
                enterSign = sign;
            }
            if (t2 < exit) exit = t2;
        }
        if (enter > exit) continue;
        double t = enter;
        if (t < 0.0) t = 0.0;  /* origin inside the box */
        if (t > best) continue;

        double nx = 0.0;
        double ny = 0.0;
        if (enterAxis >= 0) {
            const double localX = enterAxis == 0 ? enterSign : 0.0;
            const double localY = enterAxis == 1 ? enterSign : 0.0;
            const double rotC = std::cos(body.rotation);
            const double rotS = std::sin(body.rotation);
            nx = rotC * localX - rotS * localY;
            ny = rotS * localX + rotC * localY;
        }

        best = t;
        s.hit.body = static_cast<int>(i) + 1;
        s.hit.x = ox + dx * t;
        s.hit.y = oy + dy * t;
        s.hit.t = t;
        s.hit.nx = nx;
        s.hit.ny = ny;
    }
    return s.hit.body;
}

/* which: 0/1 point, 2 t, 3/4 normal. */
inline double hitValue(int which) {
    switch (which) {
        case 0: return state().hit.x;
        case 1: return state().hit.y;
        case 2: return state().hit.t;
        case 3: return state().hit.nx;
        case 4: return state().hit.ny;
        default: return 0.0;
    }
}

}  // namespace box2dgbx

#endif /* GBX_MODULE_BOX2D_QUERY_H */
