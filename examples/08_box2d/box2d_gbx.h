/* box2d_gbx.h - a small bridge between GameBasiX and Box2D-Lite
 *
 * The INLINE block in main.gbx includes only this file. It contains:
 *
 *   1. a thin C++ layer (namespace gbxbox2d) around the Box2D-Lite world.
 *      It owns the bodies plus the data needed for drawing (half extents,
 *      color) and offers x(), y(), angle() etc. for INLINE steps.
 *
 *   2. registered GBX commands (PHYSICS_...). They make the physics usable
 *      from ordinary FUNCTIONs, without any INLINE step.
 *
 * The steps in main.gbx use both: the commands for creating bodies, and the
 * C++ accessors plus DRAW_LINE/POLY_POINT inside a METHOD. That shows the
 * whole path: BASIC -> C++ step -> third-party library -> drawing.
 *
 * Box2D-Lite itself lives in box2d_lite.h (MIT license, see
 * box2d-lite-LICENSE.txt).
 */
#ifndef GBX_EXAMPLE_BOX2D_GBX_H
#define GBX_EXAMPLE_BOX2D_GBX_H

#include <gbx_inline.h>

#include "box2d_lite.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace gbxbox2d {

/* One body in the physics world plus the data used for drawing. The
 * Box2D-Lite body stays at a fixed address (unique_ptr) so pointers held by
 * the World remain valid when more bodies are added. */
struct Actor {
    std::unique_ptr<box2dlite::Body> body;
    float halfWidth = 0.5f;
    float halfHeight = 0.5f;
    int color = 0xFFFFFF;
    bool isStatic = false;
};

/* All bodies in creation order. Index 0 is the ground; IDs exposed to BASIC
 * are 1-based (id == index + 1). */
inline std::vector<Actor>& actors() {
    static std::vector<Actor> list;
    return list;
}

inline std::unique_ptr<box2dlite::World>& world() {
    static std::unique_ptr<box2dlite::World> instance;
    return instance;
}

/* Build a fresh world: ground with its top edge at y = 630, gravity down. */
inline void reset() {
    actors().clear();
    world() = std::make_unique<box2dlite::World>(box2dlite::Vec2(0.0f, 900.0f), 12);

    Actor ground;
    ground.body = std::make_unique<box2dlite::Body>();
    ground.halfWidth = 680.0f;
    ground.halfHeight = 30.0f;
    ground.body->Set(box2dlite::Vec2(ground.halfWidth * 2.0f, ground.halfHeight * 2.0f), FLT_MAX);
    ground.body->position.Set(480.0f, 660.0f);  /* top edge at y = 630 */
    ground.body->friction = 0.7f;
    ground.color = 0x202838;
    ground.isStatic = true;
    box2dlite::Body* raw = ground.body.get();
    actors().push_back(std::move(ground));
    world()->Add(raw);
}

/* Create a body. x/y is the center, width/height are full sizes in pixels,
 * angle is in radians. Returns the 1-based ID, or 0 on error. */
inline int create(double x, double y, double width, double height, double angle, int isStatic,
                  int color) {
    if (!world()) reset();

    Actor actor;
    actor.body = std::make_unique<box2dlite::Body>();
    actor.halfWidth = static_cast<float>(std::max(2.0, width)) * 0.5f;
    actor.halfHeight = static_cast<float>(std::max(2.0, height)) * 0.5f;
    actor.color = color;
    actor.isStatic = isStatic != 0;

    /* Mass proportional to area: a 60x60 box weighs 1. */
    const double area = static_cast<double>(actor.halfWidth * actor.halfHeight * 4.0f);
    const float mass = actor.isStatic ? FLT_MAX : static_cast<float>(std::max(0.05, area / 3600.0));

    actor.body->Set(box2dlite::Vec2(actor.halfWidth * 2.0f, actor.halfHeight * 2.0f), mass);
    actor.body->position.Set(static_cast<float>(x), static_cast<float>(y));
    actor.body->rotation = static_cast<float>(angle);
    actor.body->friction = actor.isStatic ? 0.7f : 0.4f;

    box2dlite::Body* raw = actor.body.get();
    actors().push_back(std::move(actor));
    world()->Add(raw);
    return static_cast<int>(actors().size());  /* ID */
}

/* Advance the simulation. dt is clamped so a long frame cannot tear the
 * simulation apart. Returns the number of bodies including the ground. */
inline int step(double dt) {
    if (!world()) return 0;
    const float clamped = static_cast<float>(std::clamp(dt, 1.0 / 240.0, 1.0 / 30.0));
    world()->Step(clamped);
    return static_cast<int>(actors().size());
}

inline const Actor* get(int id) {
    if (id < 1 || id > static_cast<int>(actors().size())) return nullptr;
    return &actors()[static_cast<std::size_t>(id - 1)];
}

inline int count() { return static_cast<int>(actors().size()); }
inline int groundId() { return 1; }

inline double x(int id) {
    const Actor* actor = get(id);
    return actor && actor->body ? actor->body->position.x : 0.0;
}

inline double y(int id) {
    const Actor* actor = get(id);
    return actor && actor->body ? actor->body->position.y : 0.0;
}

/* Rotation in radians (Box2D-Lite uses radians; BASIC draws in degrees). */
inline double angle(int id) {
    const Actor* actor = get(id);
    return actor && actor->body ? actor->body->rotation : 0.0;
}

inline double halfWidth(int id) {
    const Actor* actor = get(id);
    return actor ? actor->halfWidth : 0.0;
}

inline double halfHeight(int id) {
    const Actor* actor = get(id);
    return actor ? actor->halfHeight : 0.0;
}

inline int color(int id) {
    const Actor* actor = get(id);
    return actor ? actor->color : 0xFFFFFF;
}

/* ------------------------------------------------------ GBX commands
 * After including this header these are normal GBX commands:
 * id = PHYSICS_BOX(...), PHYSICS_STEP(dt) and so on. */
namespace call {

inline void fnReset(const GbxValue*, int, GbxValue* ret) {
    reset();
    gbxinline::retNum(ret, static_cast<double>(count()));
}

inline void fnBox(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, create(gbxinline::argNum(args, argc, 0),
                                  gbxinline::argNum(args, argc, 1),
                                  gbxinline::argNum(args, argc, 2, 40.0),
                                  gbxinline::argNum(args, argc, 3, 40.0),
                                  gbxinline::argNum(args, argc, 4, 0.0),
                                  static_cast<int>(gbxinline::argNum(args, argc, 5, 0.0)),
                                  static_cast<int>(gbxinline::argNum(args, argc, 6, 0xFFFFFF))));
}

inline void fnStep(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, step(gbxinline::argNum(args, argc, 0, 1.0 / 60.0)));
}

inline void fnCount(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, count());
}

inline void fnX(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, x(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnY(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, y(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnAngle(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, angle(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnStatic(const GbxValue* args, int argc, GbxValue* ret) {
    const Actor* actor = get(static_cast<int>(gbxinline::argNum(args, argc, 0)));
    gbxinline::retNum(ret, actor && actor->isStatic ? 1.0 : 0.0);
}

}  // namespace call

}  // namespace gbxbox2d

GBX_INLINE_FN("PHYSICS_RESET", 0, 0, gbxbox2d::call::fnReset);
GBX_INLINE_FN("PHYSICS_BOX", 2, 7, gbxbox2d::call::fnBox);
GBX_INLINE_FN("PHYSICS_STEP", 0, 1, gbxbox2d::call::fnStep);
GBX_INLINE_FN("PHYSICS_COUNT", 0, 0, gbxbox2d::call::fnCount);
GBX_INLINE_FN("PHYSICS_X", 1, 1, gbxbox2d::call::fnX);
GBX_INLINE_FN("PHYSICS_Y", 1, 1, gbxbox2d::call::fnY);
GBX_INLINE_FN("PHYSICS_ANGLE", 1, 1, gbxbox2d::call::fnAngle);
GBX_INLINE_FN("PHYSICS_STATIC", 1, 1, gbxbox2d::call::fnStatic);

#endif /* GBX_EXAMPLE_BOX2D_GBX_H */
