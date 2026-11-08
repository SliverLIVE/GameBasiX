# Easing

A small C++ library (`lib/libeasing.a`) with interpolation curves for
animations. The module is the folder `modules/easing`; `EASING.gbx` is its
entry file. A complete animated example is `examples/31_easing`.

Add the module with the Modules dock or write:

```gbx
INCLUDE "modules/easing/EASING.gbx"
```

## What the IDE does

When the module is added, GBX Studio copies the archive into the project and
registers it in the project file, so INLINE blocks can link against it:

```
lib/easing/libeasing.a        (copy inside the project)

[native]
libs = lib/easing/libeasing.a
```

Removing the module reverses both steps: the `INCLUDE` line and the
`[native]` entry are removed, the copied archive is deleted and an empty
`lib/easing` folder disappears. The archive in `modules/easing/lib` is never
touched.

In `module.json` the archive is declared like this:

```json
"native": { "libs": ["lib/libeasing.a"] }
```

Paths are relative to the module folder. Only `.a` and `.o` files are
accepted; they are copied to `lib/<module folder>/` in the project.

## Commands

| Command | Meaning |
| --- | --- |
| `EASE_VALUE start, end, t` | Linear interpolation from `start` to `end` at `t` (0…1) |
| `EASE_VALUE start, end, t, mode` | Same with a curve: 0 linear, 1 smoothstep, 2 cubic in/out, 3 back out |

`t` is clamped to 0…1. The result is `start` at 0 and `end` at 1.

```gbx
INCLUDE "modules/easing/EASING.gbx"

FOR i = 0 TO 20
 t = i / 20.0
 PRINT "t=" + STR$(t) + " x=" + STR$(EASE_VALUE(0, 320, t, 1))
NEXT
```

## Desktop and HTML5

- Desktop: the INLINE block calls `easing_value` from `libeasing.a`.
- HTML5: a Windows archive cannot be linked into WebAssembly, so the module
 compiles the same curve code directly into the player. Projects need no
 extra setting for the browser export.

---

Generated from `@help` blocks in the module's GBX sources.
