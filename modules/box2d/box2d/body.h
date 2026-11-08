/* box2d/body.h - creating, reading and changing boxes
 *
 * x/y is always the centre, sizes are full extents, angles are radians.
 * Corner indices run clockwise from the unrotated top left, so a textured
 * polygon can be drawn straight from them.
 */
#ifndef GBX_MODULE_BOX2D_BODY_H
#define GBX_MODULE_BOX2D_BODY_H

#include "common.h"

namespace box2dgbx {

/* x/y is the centre, width/height are full sizes, angle is in radians.
 * mass 0 or less means "from the area": a 60x60 box weighs 1. A static
 * body ignores both friction and mass. Returns the new ID or 0. */
inline int createBox(double x, double y, double width, double height, double angle, int isStatic,
                     double friction, double mass) {
    ensureWorld();
    State& s = state();

    auto actor = std::make_unique<Actor>();
    actor->halfWidth = static_cast<float>(std::max(2.0, width)) * 0.5f;
    actor->halfHeight = static_cast<float>(std::max(2.0, height)) * 0.5f;
    actor->isStatic = isStatic != 0;

    const double area = static_cast<double>(actor->halfWidth * 2.0f) *
                        static_cast<double>(actor->halfHeight * 2.0f);
    double massValue = mass;
    if (actor->isStatic) massValue = FLT_MAX;
    else if (!(massValue > 0.0)) massValue = std::max(0.05, area / 3600.0);

    actor->body = std::make_unique<Body>();
    actor->body->Set(Vec2(actor->halfWidth * 2.0f, actor->halfHeight * 2.0f),
                     static_cast<float>(massValue));
    actor->body->position.Set(static_cast<float>(x), static_cast<float>(y));
    actor->body->rotation = static_cast<float>(angle);
    actor->body->friction = static_cast<float>(std::max(0.0, friction));

    s.world->Add(actor->body.get());
    s.actors.push_back(std::move(actor));
    return static_cast<int>(s.actors.size());
}

/* Removes a body with its joints and contacts. The ID stays reserved. */
inline int removeBody(int id) {
    Actor* actor = live(id);
    if (!actor) return 0;
    State& s = state();
    Body* body = actor->body.get();

    std::vector<JointEntry> keep;
    keep.reserve(s.joints.size());
    for (auto& entry : s.joints) {
        if (entry.joint && (entry.joint->body1 == body || entry.joint->body2 == body)) continue;
        keep.push_back(std::move(entry));
    }
    s.joints = std::move(keep);

    for (auto it = s.world->arbiters.begin(); it != s.world->arbiters.end();) {
        if (it->first.body1 == body || it->first.body2 == body) it = s.world->arbiters.erase(it);
        else ++it;
    }

    actor->alive = false;
    actor->body.reset();
    rebuildWorld();
    s.contacts.clear();
    return 1;
}

/* ------------------------------------------------------------ reading */

inline double x(int id) {
    Actor* actor = live(id);
    return actor ? actor->body->position.x : 0.0;
}

inline double y(int id) {
    Actor* actor = live(id);
    return actor ? actor->body->position.y : 0.0;
}

/* Radians, like Box2D-Lite itself. */
inline double angle(int id) {
    Actor* actor = live(id);
    return actor ? actor->body->rotation : 0.0;
}

inline double degrees(int id) {
    return angle(id) * 180.0 / 3.14159265358979323846;
}

inline double velocityX(int id) {
    Actor* actor = live(id);
    return actor ? actor->body->velocity.x : 0.0;
}

inline double velocityY(int id) {
    Actor* actor = live(id);
    return actor ? actor->body->velocity.y : 0.0;
}

inline double speed(int id) {
    Actor* actor = live(id);
    return actor ? std::sqrt(static_cast<double>(actor->body->velocity.x * actor->body->velocity.x +
                                                 actor->body->velocity.y * actor->body->velocity.y))
                 : 0.0;
}

/* Radians per second. */
inline double spin(int id) {
    Actor* actor = live(id);
    return actor ? actor->body->angularVelocity : 0.0;
}

inline double width(int id) {
    Actor* actor = live(id);
    return actor ? actor->body->width.x : 0.0;
}

inline double height(int id) {
    Actor* actor = live(id);
    return actor ? actor->body->width.y : 0.0;
}

inline double halfWidth(int id) {
    Actor* actor = live(id);
    return actor ? actor->halfWidth : 0.0;
}

inline double halfHeight(int id) {
    Actor* actor = live(id);
    return actor ? actor->halfHeight : 0.0;
}

/* 0 for static bodies: they have infinite mass. */
inline double mass(int id) {
    Actor* actor = live(id);
    if (!actor || actor->isStatic) return 0.0;
    return actor->body->mass;
}

inline double friction(int id) {
    Actor* actor = live(id);
    return actor ? actor->body->friction : 0.0;
}

inline int isStatic(int id) {
    Actor* actor = live(id);
    return actor && actor->isStatic ? 1 : 0;
}

inline int alive(int id) {
    return live(id) ? 1 : 0;
}

/* ----------------------------------------------------------- changing */

inline int place(int id, double px, double py) {
    Actor* actor = live(id);
    if (!actor) return 0;
    actor->body->position.Set(static_cast<float>(px), static_cast<float>(py));
    return 1;
}

inline int turn(int id, double radians) {
    Actor* actor = live(id);
    if (!actor) return 0;
    actor->body->rotation = static_cast<float>(radians);
    return 1;
}

inline int setVelocity(int id, double vx, double vy) {
    Actor* actor = live(id);
    if (!actor || actor->isStatic) return 0;
    actor->body->velocity.Set(static_cast<float>(vx), static_cast<float>(vy));
    return 1;
}

inline int setSpin(int id, double value) {
    Actor* actor = live(id);
    if (!actor || actor->isStatic) return 0;
    actor->body->angularVelocity = static_cast<float>(value);
    return 1;
}

inline int setFriction(int id, double value) {
    Actor* actor = live(id);
    if (!actor) return 0;
    actor->body->friction = static_cast<float>(std::max(0.0, value));
    return 1;
}

/* Changes the mass and the inertia of a running body. A value of 0 or less
 * turns it static, exactly like the last argument of BOX2D_BOX. */
inline int setMass(int id, double value) {
    Actor* actor = live(id);
    if (!actor) return 0;
    actor->isStatic = !(value > 0.0);
    applyMass(*actor->body, static_cast<float>(actor->isStatic ? FLT_MAX : value));
    return 1;
}

/* Force and torque apply to the next BOX2D_STEP only. */
inline int addForce(int id, double fx, double fy) {
    Actor* actor = live(id);
    if (!actor || actor->isStatic) return 0;
    actor->body->AddForce(Vec2(static_cast<float>(fx), static_cast<float>(fy)));
    return 1;
}

inline int addTorque(int id, double torque) {
    Actor* actor = live(id);
    if (!actor || actor->isStatic) return 0;
    actor->body->torque += static_cast<float>(torque);
    return 1;
}

/* An impulse changes the velocity at once. */
inline int applyImpulse(int id, double ix, double iy) {
    Actor* actor = live(id);
    if (!actor || actor->isStatic) return 0;
    Body* body = actor->body.get();
    body->velocity += body->invMass * Vec2(static_cast<float>(ix), static_cast<float>(iy));
    return 1;
}

/* Impulse at a world point: also spins the body. */
inline int applyImpulseAt(int id, double ix, double iy, double wx, double wy) {
    Actor* actor = live(id);
    if (!actor || actor->isStatic) return 0;
    Body* body = actor->body.get();
    const Vec2 impulse(static_cast<float>(ix), static_cast<float>(iy));
    const Vec2 r(static_cast<float>(wx - body->position.x), static_cast<float>(wy - body->position.y));
    body->velocity += body->invMass * impulse;
    body->angularVelocity += body->invI * box2dlite::Cross(r, impulse);
    return 1;
}

inline int applyAngularImpulse(int id, double value) {
    Actor* actor = live(id);
    if (!actor || actor->isStatic) return 0;
    actor->body->angularVelocity += actor->body->invI * static_cast<float>(value);
    return 1;
}

/* Corner 0 is top left, 1 top right, 2 bottom right, 3 bottom left in the
 * unrotated body; the result is already rotated and moved into the world. */
inline void cornerOf(const Body& body, int index, double& outX, double& outY) {
    const double hw = body.width.x * 0.5;
    const double hh = body.width.y * 0.5;
    const double ex = (index == 1 || index == 2) ? hw : -hw;
    const double ey = (index >= 2) ? hh : -hh;
    const double c = std::cos(body.rotation);
    const double s = std::sin(body.rotation);
    outX = body.position.x + c * ex - s * ey;
    outY = body.position.y + s * ex + c * ey;
}

inline double cornerX(int id, int index) {
    Actor* actor = live(id);
    if (!actor) return 0.0;
    double cx = 0.0;
    double cy = 0.0;
    cornerOf(*actor->body, index, cx, cy);
    return cx;
}

inline double cornerY(int id, int index) {
    Actor* actor = live(id);
    if (!actor) return 0.0;
    double cx = 0.0;
    double cy = 0.0;
    cornerOf(*actor->body, index, cx, cy);
    return cy;
}

}  // namespace box2dgbx

#endif /* GBX_MODULE_BOX2D_BODY_H */
