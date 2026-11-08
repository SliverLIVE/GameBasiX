/* box2d/world.h - stepping and contacts
 *
 * BOX2D_STEP advances the simulation and refreshes the contact snapshot, so
 * gameplay code can ask which bodies touch and where.
 */
#ifndef GBX_MODULE_BOX2D_WORLD_H
#define GBX_MODULE_BOX2D_WORLD_H

#include "common.h"

namespace box2dgbx {

/* Advances the simulation. dt is clamped so a long frame cannot tear the
 * simulation apart. Returns the number of bodies in the world. */
inline int step(double dt) {
    ensureWorld();
    State& s = state();
    const float clamped = static_cast<float>(std::clamp(dt, 1.0 / 240.0, 1.0 / 30.0));
    s.world->Step(clamped);

    s.contacts.clear();
    for (const auto& entry : s.world->arbiters) {
        const Arbiter& arbiter = entry.second;
        const int a = idOf(arbiter.body1);
        const int b = idOf(arbiter.body2);
        for (int i = 0; i < arbiter.numContacts; ++i) {
            const box2dlite::Contact& contact = arbiter.contacts[i];
            ContactInfo info;
            info.a = a;
            info.b = b;
            info.x = contact.position.x;
            info.y = contact.position.y;
            info.nx = contact.normal.x;
            info.ny = contact.normal.y;
            info.separation = contact.separation;
            info.force = contact.Pn;
            s.contacts.push_back(info);
        }
    }
    return static_cast<int>(s.world->bodies.size());
}

/* ---------------------------------------------------------- contacts */

inline int contactCount() {
    return static_cast<int>(state().contacts.size());
}

inline const ContactInfo* contactAt(int index) {
    if (index < 0 || index >= static_cast<int>(state().contacts.size())) return nullptr;
    return &state().contacts[static_cast<std::size_t>(index)];
}

inline int contactBody(int index, int which) {
    const ContactInfo* info = contactAt(index);
    if (!info) return 0;
    return which == 1 ? info->b : info->a;
}

/* which: 0/1 position, 2/3 normal, 4 depth, 5 accumulated impulse. */
inline double contactValue(int index, int which) {
    const ContactInfo* info = contactAt(index);
    if (!info) return 0.0;
    switch (which) {
        case 0: return info->x;
        case 1: return info->y;
        case 2: return info->nx;
        case 3: return info->ny;
        case 4: return -info->separation;  /* penetration depth, positive */
        case 5: return info->force;
        default: return 0.0;
    }
}

/* Number of contact points between two bodies, 0 when they do not touch. */
inline int touching(int idA, int idB) {
    Actor* actorA = live(idA);
    Actor* actorB = live(idB);
    if (!actorA || !actorB) return 0;
    State& s = state();
    if (!s.world) return 0;
    const ArbiterKey key(actorA->body.get(), actorB->body.get());
    const auto it = s.world->arbiters.find(key);
    return it == s.world->arbiters.end() ? 0 : it->second.numContacts;
}

}  // namespace box2dgbx

#endif /* GBX_MODULE_BOX2D_WORLD_H */
