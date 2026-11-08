/* box2d/common.h - state and shared helpers of the BOX2D module
 *
 * Everything the parts of the module share: the world, the bodies, the joint
 * slots, the contact snapshot and the debug colours. The functions here are
 * inline, so the whole module still compiles into one INLINE block.
 */
#ifndef GBX_MODULE_BOX2D_COMMON_H
#define GBX_MODULE_BOX2D_COMMON_H

#include <gbx_inline.h>

#include "../box2d_lite.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <memory>
#include <vector>

namespace box2dgbx {

using box2dlite::Arbiter;
using box2dlite::ArbiterKey;
using box2dlite::Body;
using box2dlite::Joint;
using box2dlite::Mat22;
using box2dlite::Vec2;
using box2dlite::World;

/* One body plus the flags the engine does not store itself. The Body lives
 * behind a unique_ptr so its address stays valid while the world keeps raw
 * pointers to it. */
struct Actor {
    std::unique_ptr<Body> body;
    float halfWidth = 0.5f;
    float halfHeight = 0.5f;
    bool isStatic = false;
    bool alive = true;
};

/* A joint slot. Removed joints stay in the list so the IDs of the others do
 * not change. */
struct JointEntry {
    std::unique_ptr<Joint> joint;
    bool alive = true;
};

/* One contact point, ready for gameplay code and the debug overlay. */
struct ContactInfo {
    int a = 0;
    int b = 0;
    double x = 0.0;
    double y = 0.0;
    double nx = 0.0;
    double ny = 0.0;
    double separation = 0.0;
    double force = 0.0;
};

/* Result of the last BOX2D_RAY call. */
struct RayHit {
    int body = 0;
    double x = 0.0;
    double y = 0.0;
    double t = 0.0;
    double nx = 0.0;
    double ny = 0.0;
};

/* Bits for BOX2D_DEBUG_FLAGS. */
enum DebugBit {
    kShape = 1,
    kCenter = 2,
    kVelocity = 4,
    kContact = 8,
    kNormal = 16,
    kJoint = 32,
    kBounds = 64
};

struct State {
    std::unique_ptr<World> world;
    std::vector<std::unique_ptr<Actor>> actors;
    std::vector<JointEntry> joints;
    std::vector<ContactInfo> contacts;
    RayHit hit;

    double gravityX = 0.0;
    double gravityY = 900.0;
    int iterations = 12;

    int debugFlags = kShape | kCenter | kContact | kNormal | kJoint;
    int shapeColor = 0x4EA3FF;
    int staticColor = 0x64748B;
    int contactColor = 0xFF5D5D;
    int jointColor = 0xF0C040;
};

inline State& state() {
    static State instance;
    return instance;
}

/* ------------------------------------------------------------- world */

inline void resetWorld(double gx, double gy, int iterations) {
    State& s = state();
    s.gravityX = gx;
    s.gravityY = gy;
    s.iterations = iterations < 1 ? 1 : iterations;
    s.world = std::make_unique<World>(Vec2(static_cast<float>(gx), static_cast<float>(gy)), s.iterations);
    s.actors.clear();
    s.joints.clear();
    s.contacts.clear();
    s.hit = RayHit();
}

inline void ensureWorld() {
    if (!state().world) resetWorld(state().gravityX, state().gravityY, state().iterations);
}

/* Rebuilds the raw pointer lists of the world from our owning containers.
 * Called after a body or joint was removed. */
inline void rebuildWorld() {
    State& s = state();
    if (!s.world) return;
    s.world->bodies.clear();
    for (const auto& actor : s.actors) {
        if (actor && actor->alive && actor->body) s.world->bodies.push_back(actor->body.get());
    }
    s.world->joints.clear();
    for (const auto& entry : s.joints) {
        if (entry.alive && entry.joint) s.world->joints.push_back(entry.joint.get());
    }
}

inline Actor* actorAt(int id) {
    if (id < 1 || id > static_cast<int>(state().actors.size())) return nullptr;
    return state().actors[static_cast<std::size_t>(id - 1)].get();
}

/* Live actor or null. Dead IDs stay reserved, so other IDs never shift. */
inline Actor* live(int id) {
    Actor* actor = actorAt(id);
    return (actor && actor->alive) ? actor : nullptr;
}

inline JointEntry* jointAt(int id) {
    if (id < 1 || id > static_cast<int>(state().joints.size())) return nullptr;
    return &state().joints[static_cast<std::size_t>(id - 1)];
}

inline Joint* liveJoint(int id) {
    JointEntry* entry = jointAt(id);
    return (entry && entry->alive && entry->joint) ? entry->joint.get() : nullptr;
}

inline int idOf(const Body* body) {
    if (!body) return 0;
    const auto& actors = state().actors;
    for (std::size_t i = 0; i < actors.size(); ++i) {
        if (actors[i] && actors[i]->body.get() == body) return static_cast<int>(i) + 1;
    }
    return 0;
}

/* Mass and inertia from the box size. A mass of 0 or less makes the body
 * static, which is what Box2D-Lite does with FLT_MAX. */
inline void applyMass(Body& body, float mass) {
    if (mass <= 0.0f || mass >= FLT_MAX) {
        body.mass = FLT_MAX;
        body.invMass = 0.0f;
        body.I = FLT_MAX;
        body.invI = 0.0f;
        return;
    }
    body.mass = mass;
    body.invMass = 1.0f / mass;
    body.I = mass * (body.width.x * body.width.x + body.width.y * body.width.y) / 12.0f;
    body.invI = body.I > 0.0f ? 1.0f / body.I : 0.0f;
}

inline int countBodies() {
    int total = 0;
    for (const auto& actor : state().actors) {
        if (actor && actor->alive) ++total;
    }
    return total;
}

}  // namespace box2dgbx

#endif /* GBX_MODULE_BOX2D_COMMON_H */
