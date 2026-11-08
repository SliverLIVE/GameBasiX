/* box2d/commands.h - the BOX2D_* GBX commands
 *
 * Thin adapters: each one unpacks GbxValue arguments, calls the C++ function
 * of the matching part and returns a number. Registered at the end; after
 * including box2d.h every one of them is an ordinary GBX command.
 */
#ifndef GBX_MODULE_BOX2D_COMMANDS_H
#define GBX_MODULE_BOX2D_COMMANDS_H

#include "body.h"
#include "joint.h"
#include "query.h"
#include "world.h"

namespace box2dgbx {
namespace call {

inline void fnWorld(const GbxValue* args, int argc, GbxValue* ret) {
    resetWorld(gbxinline::argNum(args, argc, 0, 0.0), gbxinline::argNum(args, argc, 1, 900.0),
               static_cast<int>(gbxinline::argNum(args, argc, 2, 12.0)));
    gbxinline::retNum(ret, 0.0);
}

inline void fnClear(const GbxValue*, int, GbxValue* ret) {
    State& s = state();
    if (!s.world) resetWorld(s.gravityX, s.gravityY, s.iterations);
    else {
        s.actors.clear();
        s.joints.clear();
        s.contacts.clear();
        s.world->Clear();
    }
    gbxinline::retNum(ret, 0.0);
}

inline void fnGravity(const GbxValue* args, int argc, GbxValue* ret) {
    ensureWorld();
    State& s = state();
    s.gravityX = gbxinline::argNum(args, argc, 0, 0.0);
    s.gravityY = gbxinline::argNum(args, argc, 1, 0.0);
    s.world->gravity.Set(static_cast<float>(s.gravityX), static_cast<float>(s.gravityY));
    gbxinline::retNum(ret, 1.0);
}

inline void fnIterations(const GbxValue* args, int argc, GbxValue* ret) {
    ensureWorld();
    State& s = state();
    s.iterations = std::max(1, static_cast<int>(gbxinline::argNum(args, argc, 0, 12.0)));
    s.world->iterations = s.iterations;
    gbxinline::retNum(ret, 1.0);
}

inline void fnSolver(const GbxValue* args, int argc, GbxValue* ret) {
    World::warmStarting = gbxinline::argNum(args, argc, 0, 1.0) != 0.0;
    World::positionCorrection = gbxinline::argNum(args, argc, 1, 1.0) != 0.0;
    World::accumulateImpulses = gbxinline::argNum(args, argc, 2, 1.0) != 0.0;
    gbxinline::retNum(ret, 1.0);
}

inline void fnStep(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, step(gbxinline::argNum(args, argc, 0, 1.0 / 60.0)));
}

inline void fnCount(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, countBodies());
}

inline void fnBox(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, createBox(gbxinline::argNum(args, argc, 0), gbxinline::argNum(args, argc, 1),
                                     gbxinline::argNum(args, argc, 2, 40.0),
                                     gbxinline::argNum(args, argc, 3, 40.0),
                                     gbxinline::argNum(args, argc, 4, 0.0),
                                     static_cast<int>(gbxinline::argNum(args, argc, 5, 0.0)),
                                     gbxinline::argNum(args, argc, 6, 0.4),
                                     gbxinline::argNum(args, argc, 7, 0.0)));
}

inline void fnRemove(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, removeBody(static_cast<int>(gbxinline::argNum(args, argc, 0))));
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

inline void fnDegrees(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, degrees(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnVelocityX(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, velocityX(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnVelocityY(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, velocityY(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnSpeed(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, speed(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnSpin(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, spin(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnWidth(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, width(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnHeight(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, height(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnHalfWidth(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, halfWidth(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnHalfHeight(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, halfHeight(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnMass(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, mass(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnFriction(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, friction(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnStatic(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, isStatic(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnAlive(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, alive(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnPlace(const GbxValue* args, int argc, GbxValue* ret) {
    const int id = static_cast<int>(gbxinline::argNum(args, argc, 0));
    const int ok = place(id, gbxinline::argNum(args, argc, 1), gbxinline::argNum(args, argc, 2));
    if (ok && argc > 3) turn(id, gbxinline::argNum(args, argc, 3));
    gbxinline::retNum(ret, ok);
}

inline void fnTurn(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, turn(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                               gbxinline::argNum(args, argc, 1)));
}

inline void fnSetVelocity(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, setVelocity(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                      gbxinline::argNum(args, argc, 1), gbxinline::argNum(args, argc, 2)));
}

inline void fnSetSpin(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, setSpin(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                  gbxinline::argNum(args, argc, 1)));
}

inline void fnSetFriction(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, setFriction(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                      gbxinline::argNum(args, argc, 1)));
}

inline void fnSetMass(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, setMass(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                  gbxinline::argNum(args, argc, 1)));
}

inline void fnForce(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, addForce(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                   gbxinline::argNum(args, argc, 1), gbxinline::argNum(args, argc, 2)));
}

inline void fnTorque(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, addTorque(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                    gbxinline::argNum(args, argc, 1)));
}

inline void fnImpulse(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, applyImpulse(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                       gbxinline::argNum(args, argc, 1), gbxinline::argNum(args, argc, 2)));
}

inline void fnImpulseAt(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, applyImpulseAt(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                         gbxinline::argNum(args, argc, 1), gbxinline::argNum(args, argc, 2),
                                         gbxinline::argNum(args, argc, 3), gbxinline::argNum(args, argc, 4)));
}

inline void fnAngularImpulse(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, applyAngularImpulse(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                              gbxinline::argNum(args, argc, 1)));
}

inline void fnCornerX(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, cornerX(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                   static_cast<int>(gbxinline::argNum(args, argc, 1))));
}

inline void fnCornerY(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, cornerY(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                   static_cast<int>(gbxinline::argNum(args, argc, 1))));
}

inline void fnJoint(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, addJoint(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                    static_cast<int>(gbxinline::argNum(args, argc, 1)),
                                    gbxinline::argNum(args, argc, 2), gbxinline::argNum(args, argc, 3)));
}

inline void fnJointCount(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, countJoints());
}

inline void fnJointRemove(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, removeJoint(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnJointAX(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, jointAnchor(static_cast<int>(gbxinline::argNum(args, argc, 0)), 0));
}

inline void fnJointAY(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, jointAnchor(static_cast<int>(gbxinline::argNum(args, argc, 0)), 1));
}

inline void fnJointBX(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, jointAnchor(static_cast<int>(gbxinline::argNum(args, argc, 0)), 2));
}

inline void fnJointBY(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, jointAnchor(static_cast<int>(gbxinline::argNum(args, argc, 0)), 3));
}

inline void fnJointSoftness(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, setJointSoftness(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                            gbxinline::argNum(args, argc, 1)));
}

inline void fnJointBias(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, setJointBias(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                        gbxinline::argNum(args, argc, 1)));
}

inline void fnJointImpulse(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, jointImpulse(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                        static_cast<int>(gbxinline::argNum(args, argc, 1))));
}

inline void fnContactCount(const GbxValue*, int, GbxValue* ret) {
    gbxinline::retNum(ret, contactCount());
}

inline void fnContactBody(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, contactBody(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                       static_cast<int>(gbxinline::argNum(args, argc, 1))));
}

inline void fnContactValue(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, contactValue(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                        static_cast<int>(gbxinline::argNum(args, argc, 1))));
}

inline void fnTouching(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, touching(static_cast<int>(gbxinline::argNum(args, argc, 0)),
                                    static_cast<int>(gbxinline::argNum(args, argc, 1))));
}

inline void fnAt(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, bodyAt(gbxinline::argNum(args, argc, 0), gbxinline::argNum(args, argc, 1),
                                  static_cast<int>(gbxinline::argNum(args, argc, 2, 1.0))));
}

inline void fnRay(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, raycast(gbxinline::argNum(args, argc, 0), gbxinline::argNum(args, argc, 1),
                                   gbxinline::argNum(args, argc, 2), gbxinline::argNum(args, argc, 3),
                                   gbxinline::argNum(args, argc, 4, 1.0)));
}

inline void fnHit(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, hitValue(static_cast<int>(gbxinline::argNum(args, argc, 0))));
}

inline void fnDebugFlags(const GbxValue* args, int argc, GbxValue* ret) {
    state().debugFlags = static_cast<int>(gbxinline::argNum(args, argc, 0, 0.0));
    gbxinline::retNum(ret, state().debugFlags);
}

inline void fnDebugColors(const GbxValue* args, int argc, GbxValue* ret) {
    State& s = state();
    s.shapeColor = static_cast<int>(gbxinline::argNum(args, argc, 0, s.shapeColor));
    s.staticColor = static_cast<int>(gbxinline::argNum(args, argc, 1, s.staticColor));
    s.contactColor = static_cast<int>(gbxinline::argNum(args, argc, 2, s.contactColor));
    s.jointColor = static_cast<int>(gbxinline::argNum(args, argc, 3, s.jointColor));
    gbxinline::retNum(ret, 1.0);
}

}  // namespace call
}  // namespace box2dgbx

/* ---------------------------------------------------------- registration */
GBX_INLINE_FN("BOX2D_WORLD", 0, 3, box2dgbx::call::fnWorld);
GBX_INLINE_FN("BOX2D_CLEAR", 0, 0, box2dgbx::call::fnClear);
GBX_INLINE_FN("BOX2D_GRAVITY", 1, 2, box2dgbx::call::fnGravity);
GBX_INLINE_FN("BOX2D_ITERATIONS", 1, 1, box2dgbx::call::fnIterations);
GBX_INLINE_FN("BOX2D_SOLVER", 0, 3, box2dgbx::call::fnSolver);
GBX_INLINE_FN("BOX2D_STEP", 0, 1, box2dgbx::call::fnStep);
GBX_INLINE_FN("BOX2D_COUNT", 0, 0, box2dgbx::call::fnCount);

GBX_INLINE_FN("BOX2D_BOX", 2, 8, box2dgbx::call::fnBox);
GBX_INLINE_FN("BOX2D_REMOVE", 1, 1, box2dgbx::call::fnRemove);
GBX_INLINE_FN("BOX2D_X", 1, 1, box2dgbx::call::fnX);
GBX_INLINE_FN("BOX2D_Y", 1, 1, box2dgbx::call::fnY);
GBX_INLINE_FN("BOX2D_ANGLE", 1, 1, box2dgbx::call::fnAngle);
GBX_INLINE_FN("BOX2D_DEGREES", 1, 1, box2dgbx::call::fnDegrees);
GBX_INLINE_FN("BOX2D_VX", 1, 1, box2dgbx::call::fnVelocityX);
GBX_INLINE_FN("BOX2D_VY", 1, 1, box2dgbx::call::fnVelocityY);
GBX_INLINE_FN("BOX2D_SPEED", 1, 1, box2dgbx::call::fnSpeed);
GBX_INLINE_FN("BOX2D_SPIN", 1, 1, box2dgbx::call::fnSpin);
GBX_INLINE_FN("BOX2D_WIDTH", 1, 1, box2dgbx::call::fnWidth);
GBX_INLINE_FN("BOX2D_HEIGHT", 1, 1, box2dgbx::call::fnHeight);
GBX_INLINE_FN("BOX2D_HALF_WIDTH", 1, 1, box2dgbx::call::fnHalfWidth);
GBX_INLINE_FN("BOX2D_HALF_HEIGHT", 1, 1, box2dgbx::call::fnHalfHeight);
GBX_INLINE_FN("BOX2D_MASS", 1, 1, box2dgbx::call::fnMass);
GBX_INLINE_FN("BOX2D_FRICTION", 1, 1, box2dgbx::call::fnFriction);
GBX_INLINE_FN("BOX2D_IS_STATIC", 1, 1, box2dgbx::call::fnStatic);
GBX_INLINE_FN("BOX2D_ALIVE", 1, 1, box2dgbx::call::fnAlive);
GBX_INLINE_FN("BOX2D_PLACE", 2, 4, box2dgbx::call::fnPlace);
GBX_INLINE_FN("BOX2D_TURN", 2, 2, box2dgbx::call::fnTurn);
GBX_INLINE_FN("BOX2D_SET_VELOCITY", 3, 3, box2dgbx::call::fnSetVelocity);
GBX_INLINE_FN("BOX2D_SET_SPIN", 2, 2, box2dgbx::call::fnSetSpin);
GBX_INLINE_FN("BOX2D_SET_FRICTION", 2, 2, box2dgbx::call::fnSetFriction);
GBX_INLINE_FN("BOX2D_SET_MASS", 2, 2, box2dgbx::call::fnSetMass);
GBX_INLINE_FN("BOX2D_FORCE", 2, 3, box2dgbx::call::fnForce);
GBX_INLINE_FN("BOX2D_TORQUE", 2, 2, box2dgbx::call::fnTorque);
GBX_INLINE_FN("BOX2D_IMPULSE", 2, 3, box2dgbx::call::fnImpulse);
GBX_INLINE_FN("BOX2D_IMPULSE_AT", 4, 5, box2dgbx::call::fnImpulseAt);
GBX_INLINE_FN("BOX2D_ANGULAR_IMPULSE", 2, 2, box2dgbx::call::fnAngularImpulse);
GBX_INLINE_FN("BOX2D_CORNER_X", 2, 2, box2dgbx::call::fnCornerX);
GBX_INLINE_FN("BOX2D_CORNER_Y", 2, 2, box2dgbx::call::fnCornerY);

GBX_INLINE_FN("BOX2D_JOINT", 4, 4, box2dgbx::call::fnJoint);
GBX_INLINE_FN("BOX2D_JOINT_COUNT", 0, 0, box2dgbx::call::fnJointCount);
GBX_INLINE_FN("BOX2D_JOINT_REMOVE", 1, 1, box2dgbx::call::fnJointRemove);
GBX_INLINE_FN("BOX2D_JOINT_AX", 1, 1, box2dgbx::call::fnJointAX);
GBX_INLINE_FN("BOX2D_JOINT_AY", 1, 1, box2dgbx::call::fnJointAY);
GBX_INLINE_FN("BOX2D_JOINT_BX", 1, 1, box2dgbx::call::fnJointBX);
GBX_INLINE_FN("BOX2D_JOINT_BY", 1, 1, box2dgbx::call::fnJointBY);
GBX_INLINE_FN("BOX2D_JOINT_SOFTNESS", 2, 2, box2dgbx::call::fnJointSoftness);
GBX_INLINE_FN("BOX2D_JOINT_BIAS", 2, 2, box2dgbx::call::fnJointBias);
GBX_INLINE_FN("BOX2D_JOINT_IMPULSE", 2, 2, box2dgbx::call::fnJointImpulse);

GBX_INLINE_FN("BOX2D_CONTACT_COUNT", 0, 0, box2dgbx::call::fnContactCount);
GBX_INLINE_FN("BOX2D_CONTACT_BODY", 2, 2, box2dgbx::call::fnContactBody);
GBX_INLINE_FN("BOX2D_CONTACT_VALUE", 2, 2, box2dgbx::call::fnContactValue);
GBX_INLINE_FN("BOX2D_TOUCHING", 2, 2, box2dgbx::call::fnTouching);

GBX_INLINE_FN("BOX2D_AT", 2, 3, box2dgbx::call::fnAt);
GBX_INLINE_FN("BOX2D_RAY", 4, 5, box2dgbx::call::fnRay);
GBX_INLINE_FN("BOX2D_HIT", 1, 1, box2dgbx::call::fnHit);

GBX_INLINE_FN("BOX2D_DEBUG_FLAGS", 1, 1, box2dgbx::call::fnDebugFlags);
GBX_INLINE_FN("BOX2D_DEBUG_COLORS", 0, 4, box2dgbx::call::fnDebugColors);

#endif /* GBX_MODULE_BOX2D_COMMANDS_H */
