/* box2d.h - GameBasiX wrapper around Box2D-Lite (umbrella header)
 *
 * The wrapper is split into small files under `box2d/`, one topic each:
 *
 *   common.h    state, body and joint slots, shared helpers
 *   world.h     stepping and the contact list
 *   body.h      creating, reading and changing bodies
 *   joint.h     point-to-point joints
 *   query.h     point and ray queries
 *   debug.h     DebugDraw
 *   commands.h  the BOX2D_* GBX commands
 *
 * This file is the single entry point: include it (or BOX2D.gbx) and the
 * whole module is available. Nothing else has to be included by hand, so a
 * project can also copy these files into one place and keep one include.
 *
 * Box2D-Lite (Erin Catto, MIT license) lives in ../box2d_lite.h and is not
 * modified; see ../box2d-lite-LICENSE.txt.
 */
#ifndef GBX_MODULE_BOX2D_H
#define GBX_MODULE_BOX2D_H

#include "box2d/common.h"
#include "box2d/body.h"
#include "box2d/joint.h"
#include "box2d/world.h"
#include "box2d/query.h"
#include "box2d/debug.h"
#include "box2d/commands.h"

#endif /* GBX_MODULE_BOX2D_H */
