/* box2d/debug.h - DebugDraw
 *
 * Draws what the engine believes: body outlines, centres, velocities,
 * contacts, normals, joints and bounds. Everything goes through the normal
 * GBX drawing commands, so no second renderer and no extra window are needed.
 *
 * C++ can only call DRAW_LINE and friends from an INLINE step; BOX2D.gbx
 * therefore wraps debugDraw() in a FUNCTION (BOX2D_DEBUG_DRAW).
 */
#ifndef GBX_MODULE_BOX2D_DEBUG_H
#define GBX_MODULE_BOX2D_DEBUG_H

#include "body.h"
#include "joint.h"

namespace box2dgbx {

inline void debugLine(double x1, double y1, double x2, double y2, int color) {
    gbxinline::DRAW_LINE(x1, y1, x2, y2, color);
}

inline void debugDot(double px, double py, double radius, int color) {
    gbxinline::DRAW_CIRCLE_FILLED(px, py, radius, color);
}

/* Draws the current world with the normal GBX drawing commands. Call it after
 * BOX2D_STEP, inside the frame. */
inline void debugDraw() {
    State& s = state();
    if (!s.world) return;

    for (int i = 0; i < static_cast<int>(s.actors.size()); ++i) {
        Actor* actor = s.actors[static_cast<std::size_t>(i)].get();
        if (!actor || !actor->alive || !actor->body) continue;
        const Body& body = *actor->body;
        const int color = actor->isStatic ? s.staticColor : s.shapeColor;

        double cx[4];
        double cy[4];
        for (int c = 0; c < 4; ++c) cornerOf(body, c, cx[c], cy[c]);

        if (s.debugFlags & kShape) {
            for (int c = 0; c < 4; ++c) {
                const int n = (c + 1) % 4;
                debugLine(cx[c], cy[c], cx[n], cy[n], color);
            }
        }

        if (s.debugFlags & kBounds) {
            double minX = cx[0];
            double maxX = cx[0];
            double minY = cy[0];
            double maxY = cy[0];
            for (int c = 1; c < 4; ++c) {
                minX = std::min(minX, cx[c]);
                maxX = std::max(maxX, cx[c]);
                minY = std::min(minY, cy[c]);
                maxY = std::max(maxY, cy[c]);
            }
            debugLine(minX, minY, maxX, minY, color);
            debugLine(maxX, minY, maxX, maxY, color);
            debugLine(maxX, maxY, minX, maxY, color);
            debugLine(minX, maxY, minX, minY, color);
        }

        if (s.debugFlags & kCenter) {
            debugLine(body.position.x - 5.0, body.position.y, body.position.x + 5.0, body.position.y,
                      s.contactColor);
            debugLine(body.position.x, body.position.y - 5.0, body.position.x, body.position.y + 5.0,
                      s.contactColor);
        }

        if (s.debugFlags & kVelocity) {
            const double vx = body.velocity.x * 0.2;
            const double vy = body.velocity.y * 0.2;
            if (std::fabs(vx) + std::fabs(vy) > 0.5) {
                debugLine(body.position.x, body.position.y, body.position.x + vx, body.position.y + vy,
                          s.jointColor);
            }
        }
    }

    if (s.debugFlags & kJoint) {
        for (const auto& entry : s.joints) {
            if (!entry.alive || !entry.joint) continue;
            double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
            jointAnchors(*entry.joint, ax, ay, bx, by);
            debugLine(ax, ay, bx, by, s.jointColor);
            debugDot(ax, ay, 3.0, s.jointColor);
            debugDot(bx, by, 3.0, s.jointColor);
        }
    }

    if (s.debugFlags & kContact) {
        for (const ContactInfo& contact : s.contacts) {
            debugDot(contact.x, contact.y, 3.0, s.contactColor);
        }
    }

    if (s.debugFlags & kNormal) {
        for (const ContactInfo& contact : s.contacts) {
            debugLine(contact.x, contact.y, contact.x + contact.nx * 14.0, contact.y + contact.ny * 14.0,
                      s.contactColor);
        }
    }
}

}  // namespace box2dgbx

#endif /* GBX_MODULE_BOX2D_DEBUG_H */
