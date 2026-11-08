/* box2d/joint.h - point-to-point joints
 *
 * Box2D-Lite has one joint type: both bodies keep an anchor point together.
 * That is enough for chains, bridges, ragdolls and rope-like structures.
 */
#ifndef GBX_MODULE_BOX2D_JOINT_H
#define GBX_MODULE_BOX2D_JOINT_H

#include "common.h"

namespace box2dgbx {

/* Point-to-point joint: both bodies keep the anchor point together. */
inline int addJoint(int idA, int idB, double ax, double ay) {
    Actor* bodyA = live(idA);
    Actor* bodyB = live(idB);
    if (!bodyA || !bodyB) return 0;
    ensureWorld();
    State& s = state();
    JointEntry entry;
    entry.joint = std::make_unique<Joint>();
    entry.joint->Set(bodyA->body.get(), bodyB->body.get(), Vec2(static_cast<float>(ax), static_cast<float>(ay)));
    s.world->Add(entry.joint.get());
    s.joints.push_back(std::move(entry));
    return static_cast<int>(s.joints.size());
}

inline int removeJoint(int id) {
    JointEntry* entry = jointAt(id);
    if (!entry || !entry->alive) return 0;
    entry->alive = false;
    entry->joint.reset();
    rebuildWorld();
    return 1;
}

inline int countJoints() {
    int total = 0;
    for (const auto& entry : state().joints) {
        if (entry.alive && entry.joint) ++total;
    }
    return total;
}

/* Both world anchor points of a joint. */
inline void jointAnchors(const Joint& joint, double& ax, double& ay, double& bx, double& by) {
    const Mat22 rotA(joint.body1->rotation);
    const Mat22 rotB(joint.body2->rotation);
    const Vec2 pa = joint.body1->position + rotA * joint.localAnchor1;
    const Vec2 pb = joint.body2->position + rotB * joint.localAnchor2;
    ax = pa.x;
    ay = pa.y;
    bx = pb.x;
    by = pb.y;
}

/* which: 0 = x of the first body's anchor, 1 = y of it, 2/3 the same for the
 * second body. */
inline double jointAnchor(int id, int which) {
    Joint* joint = liveJoint(id);
    if (!joint) return 0.0;
    double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
    jointAnchors(*joint, ax, ay, bx, by);
    switch (which) {
        case 0: return ax;
        case 1: return ay;
        case 2: return bx;
        default: return by;
    }
}

inline int setJointSoftness(int id, double value) {
    Joint* joint = liveJoint(id);
    if (!joint) return 0;
    joint->softness = static_cast<float>(std::max(0.0, value));
    return 1;
}

inline int setJointBias(int id, double value) {
    Joint* joint = liveJoint(id);
    if (!joint) return 0;
    joint->biasFactor = static_cast<float>(value);
    return 1;
}

/* Accumulated impulse of the last step; a rough measure of the load. */
inline double jointImpulse(int id, int axis) {
    Joint* joint = liveJoint(id);
    if (!joint) return 0.0;
    return axis == 1 ? joint->P.y : joint->P.x;
}

}  // namespace box2dgbx

#endif /* GBX_MODULE_BOX2D_JOINT_H */
