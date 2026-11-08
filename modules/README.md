# Writing GameBasiX modules

A module is a reusable piece of a GameBasiX project: BASIC functions and types,
C++ code behind `INLINE`, prebuilt archives, or all of them together. Modules
live centrally in this folder. A project only links one of them with a single
`INCLUDE` line - nothing is copied into the project, except native archives
when a module ships one.

The IDE discovers every folder here that contains a valid `module.json` and
lists it in the **Modules** dock: help text, hover tooltips, command
completion, Ctrl+click definitions and one-click add/remove.

A printable overview of all modules and their core commands is in
[CHEATSHEET.html](CHEATSHEET.html).

```
modules/
  README.md            this guide
  math/                pure GBX over eight .gbx files, no C++
  json/                pure GBX, DICT-based value model over four files
  ui/                  pure GBX module, several .gbx files
  easing/              native archive (.a) plus HTML5 fallback
  opengl3d/            large INLINE renderer, one header
  box2d/               wrapper split over several headers + umbrella include
  steam/               INLINE Steamworks wrapper, library loaded at runtime
```

---

## 1. What a module is

- A folder under `modules/`, for example `modules/box2d`.
- A `module.json` with `"format": "gbx-module"` - without it the folder is
  ignored completely.
- At least one `.gbx` file; the entry file is named in the manifest.
- Optional: any number of `.h`/`.hpp`/`.cpp` files, and prebuilt `.a`/`.o`
  archives in a `lib/` subfolder.

A project uses it with:

```gbx
INCLUDE "modules/box2d/BOX2D.gbx"
```

A module **does not have to be a single file**. It may be many `.gbx`, `.h` and
`.cpp` files, and it may link a prepared library such as `libsomething.a`. It
can also provide one umbrella file at the end that includes everything and
exposes the whole module through one include - `modules/box2d/box2d.h` is that
pattern.

---

## 2. Where modules are found

**IDE** (`modulesRoot` in `ide/src/server/modules.mjs`), first existing folder
wins:

1. `GBX_MODULES` environment variable
2. `<ide>/../modules` (repository layout)
3. `<ide>/modules`
4. `resources/modules` inside a packaged app
5. `<exe dir>/modules`, `<exe dir>/../modules`
6. `modules` below the current working directory

**Interpreter** (`findModuleDir` in `src/main.cpp`), same idea:

1. `GBX_MODULES`
2. `<exe dir>/modules`, `<exe dir>/../modules`, `<exe dir>/../../modules`
3. `<cwd>/modules`
4. `<project dir>/modules`

**INCLUDE resolution** (`Parser::parseInclude`):

- `INCLUDE "file.gbx"` is resolved relative to the file that contains it.
- If that file does not exist, the path is looked up in the central modules
  folder, with a leading `modules/` removed - `modules/box2d/BOX2D.gbx`
  becomes `<modules>/box2d/BOX2D.gbx`.
- Includes are read once (duplicates are skipped) and cycles are an error.

So `INCLUDE "modules/box2d/BOX2D.gbx"` works from anywhere: repository layout,
packaged app, or with `GBX_MODULES` pointing at a shared module folder.

---

## 3. Smallest working module

```
modules/hello/
  module.json
  HELLO.gbx
```

`module.json`:

```json
{
  "format": "gbx-module",
  "id": "HELLO",
  "name": "Hello",
  "version": "1.0.0",
  "summary": "One function that greets.",
  "folder": "hello",
  "entry": "HELLO.gbx",
  "include": { "entry": "modules/hello/HELLO.gbx" },
  "files": ["HELLO.gbx"]
}
```

`HELLO.gbx`:

```gbx
'@help
'# Hello
'
'A minimal module. Include it and call `HELLO_NAME$`.
'@endhelp

'@tooltip HELLO_NAME
'Greets the given name and returns the text.
'@endtooltip

FUNCTION HELLO_NAME$: who$
  RETURN "Hello, " + who$ + "!"
ENDFUNCTION
```

Then:

```gbx
INCLUDE "modules/hello/HELLO.gbx"
PRINT HELLO_NAME$("world"), 10, 10
```

---

## 4. `module.json` reference

| Field | Type | Required | Rules |
| --- | --- | --- | --- |
| `format` | string | **yes** | Must be exactly `"gbx-module"`, otherwise the folder is skipped |
| `id` | string | no | `^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$`; defaults to the folder name. Used as prefix for definitions (`BOX2D_BOX`) |
| `name` | string | no | Display name, defaults to `id` |
| `version` | string | no | Free text, e.g. `"1.0.0"` (default `"1.0.0"`) |
| `summary` | string | no | One line for the list |
| `description` | string | no | Longer text for the details pane |
| `folder` | string | no | Module folder, relative and inside `modules/`; defaults to the directory name |
| `entry` | string | no | Entry `.gbx`, relative to the module folder; defaults to the first `files` entry |
| `include` | object or string | no | `{"entry": "modules/<folder>/<entry>"}`; must start with `modules/` and end in `.gbx`. A plain string works too |
| `files` | string[] | **yes** | Files that belong to the module; at least one `.gbx` is required |
| `native.libs` | string[] | no | Prebuilt `.a`/`.o` archives, relative to the module folder |
| `tags` | string[] | no | Search words for the Modules dock |
| `author` | string | no | Free text |

Allowed source extensions in `files` (anything else is ignored):
`.gbx .h .hh .hpp .hxx .inl .ipp .c .cc .cpp .cxx`
Allowed archive extensions: `.a .o`

Notes:

- Paths in `files` and `native.libs` must be relative, inside the module
  folder and use `/` (backslashes are normalised).
- Paths outside the module folder are dropped. A listed file that does not
  exist stays in the list and fails later; a module without any `.gbx` in
  `files` is rejected completely.
- `include.entry` must be the path a project writes into its `INCLUDE` line.
  The IDE rejects anything that does not start with `modules/`. Instead of
  `entry` the object may also use `modular` or `single`.
- Docs and the definitions index scan the whole module folder recursively
  (`lib/`, `.git`, `build`, `node_modules`, `dist` are skipped), so a source
  file that is not listed in `files` is still indexed. List every source file
  anyway - the Modules dock uses `files` to open them.
- Unknown fields are ignored, so extra metadata is harmless.
- `LICENSE`, `README.md`, `.txt` and images are not listed in `files`; keep
  them in the folder anyway. Only source files are indexed.

---

## 5. One file or many?

Everything below is supported and used by the modules in this folder.

### 5.1 Several `.gbx` files

The entry file includes the others; `files` lists all of them.

```json
"files": ["UI.gbx", "widgets.gbx", "helpers.gbx"]
```

```gbx
' UI.gbx
INCLUDE "widgets.gbx"
INCLUDE "helpers.gbx"
```

`modules/ui` works this way: `UI.gbx` is the API, `widgets.gbx` and
`helpers.gbx` hold the implementation.

### 5.2 Several C++ headers

The entry file has one `INLINE` block that includes the umbrella header; the
umbrella includes the parts. Each part is a normal header with its own include
guard and `inline` functions.

```
modules/box2d/
  BOX2D.gbx            entry: help, INLINE include, GBX wrappers
  box2d.h              umbrella: includes everything below
  box2d/common.h       state, shared helpers
  box2d/world.h        stepping, contacts
  box2d/body.h         bodies
  box2d/joint.h        joints
  box2d/query.h        point and ray queries
  box2d/debug.h        DebugDraw
  box2d/commands.h     GBX_INLINE_FN registrations
  box2d_lite.h         third-party engine, unchanged
  box2d-lite-LICENSE.txt
  lib/                 optional prebuilt archives
```

`box2d.h`:

```cpp
#ifndef GBX_MODULE_BOX2D_H
#define GBX_MODULE_BOX2D_H
#include "box2d/common.h"
#include "box2d/body.h"
/* ... */
#include "box2d/commands.h"
#endif
```

`BOX2D.gbx`:

```gbx
INLINE
#include "box2d.h"
ENDINLINE
```

This is the "single file at the end" pattern: users include one file (or one
GBX) and get the whole module; the code stays split for readability.

Include resolution inside C++ follows the C++ rule: `"..."` is searched next to
the including file first, so `box2d.h` finds `box2d/common.h` without any `-I`.

### 5.3 Prebuilt archive

```
modules/easing/
  EASING.gbx
  easing.h
  easing.cpp
  lib/libeasing.a
```

```json
"native": { "libs": ["lib/libeasing.a"] }
```

The IDE copies it to `lib/<module folder>/libeasing.a` in the project and adds
it to `[native] libs` when the module is added. See section 8.

### 5.4 Mixed

All of it can be combined: GBX API + several C++ headers + a prepared archive
+ extra `.gbx` files with examples or presets. `files` simply lists everything.

---

## 6. The entry file

Typical structure:

```gbx
' ============================================================
' MYMOD - short description
' ============================================================

'@help
'# MyMod
'
'Markdown help. Shown in the Modules dock and written to HELP.md.
'
'## Commands
'
'| Command | Meaning |
'| --- | --- |
'| `MYMOD_DO x` | Does something |
'@endhelp

'@tooltip MYMOD_DO
'Hover text for MYMOD_DO.
'@endtooltip

INLINE
#include "mymod.h"
ENDINLINE

' Optional GBX wrappers, see below.
FUNCTION MYMOD_DRAW:
  INLINE
    mymod::draw();
  ENDINLINE
ENDFUNCTION
```

### Documentation blocks

- `'@help` ... `'@endhelp`, `'@tooltip NAME` ... `'@endtooltip` in `'`
  comments (GBX) are read by the IDE.
- In `.h`/`.cpp` files the same markers work in `//` and `/* ... */`
  comments; block comments are converted to line comments before parsing.
- `@tooltip` takes the command or function name (`MYMOD_DO`); it is matched
  case-insensitively and shown in the editor hover.
- Several `@help` blocks are concatenated.
- `HELP.md` is generated from them:

```bash
node ide/scripts/build-modules.mjs
```

If a module has no `@help` block at all, an existing `HELP.md` is used as
fallback.

### Drawing from C++

`DRAW_LINE`, `POLY_BEGIN`, `PRINT` and the other GBX commands are only
reachable from an **INLINE step** (an `INLINE` block in the middle of code).
A registered function (`GBX_INLINE_FN`) is called from ordinary GBX code and
has no step context, so drawing must be wrapped:

```gbx
FUNCTION MYMOD_DEBUG_DRAW:
  INLINE
    mymod::debugDraw();   // calls gbxinline::DRAW_LINE(...) internally
  ENDINLINE
ENDFUNCTION
```

`BOX2D_DEBUG_DRAW` and the `METHOD` drawing in `examples/08_box2d` show both
variants (GBX function with step, and step inside a `METHOD`).

---

## 7. What a module can expose

### 7.1 GBX API

Anything the language offers can live in a module `.gbx` file:

| Construct | Discovered as |
| --- | --- |
| `FUNCTION name`, `SUB name` | `name` and `<ID>.name` |
| `TYPE Name` | `Name` and `<ID>.Name` |
| `METHOD name` inside a type | `<ID>.name`, `<ID>.<Type>.name`, `<Type>.name` |
| `CONSTANT name`, `CONST name` | `name` and `<ID>.name` |
| `ENUM name` | `name` and `<ID>.name` |

Names with a type suffix (`$`, `%`, `#`) are also indexed without it, so
`BOX2D_HIT$` is reachable as `BOX2D_HIT` too.

These definitions power completion, hover and Ctrl+click in the IDE.

### 7.2 Registered C++ commands

```cpp
INLINE
#include <gbx_inline.h>

static void twice(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, gbxinline::argNum(args, argc, 0) * 2.0);
}
GBX_INLINE_FN("MYMOD_TWICE", 1, 1, twice);

static void label(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retText(ret, gbxinline::argText(args, argc, 0) + "!");
}
GBX_INLINE_FN("MYMOD_LABEL$", 1, 1, label);
ENDINLINE
```

- `GBX_INLINE_FN(name, minArgs, maxArgs, fn)` registers a command.
  `maxArgs == -1` means "any number"; `GBX_INLINE_FN_ANY(name, fn)` is the
  short form.
- Arguments arrive as `GbxValue`: `type 0` number (`num`), `type 1` text
  (`text`, UTF-8, valid during the call). Helpers: `argNum(args, argc, i,
  fallback)`, `argText(...)`, `argIsText(...)`.
- Results: `retNum(ret, value)` or `retText(ret, std::string)`.
- Arrays and `TYPE` records cannot be passed as command arguments.
- A name with `$`/`%`/`#` is also callable without the suffix; the registry
  strips it.

### 7.3 INLINE steps inside module functions

A step (`INLINE` in the middle of GBX code) sees the BASIC variables of that
place. This is what makes a full C++ API possible without copying data:

BASIC variables are visible as C++ identifiers: `counter` stays `counter`, a
text `name` becomes `name_str`, an array `list` becomes `list[i]`, a `TYPE`
`p` exposes `p.x`. Names are lower case in C++ (BASIC names are
case-insensitive).

| Step helper | Meaning |
| --- | --- |
| `gbxinline::variable("name")` | handle to a BASIC variable, parameter or METHOD field |
| `gbxinline::field(value, "name")` | field of a `TYPE` |
| `gbxinline::item(value, i)` | array element |
| `gbxinline::count(value)` | array length |
| `gbxinline::valueType(value)` | kind of a handle: number, text, array, record or empty |
| `gbxinline::readNum/writeNum`, `readText/writeText` | value access on a handle |
| `gbxinline::arrayRank/arraySize` | array dimensions (like `ARRAY_BOUNDS`) |
| `gbxinline::arrayResize/arrayPush/arrayInsert/arrayRemove/arrayClear` | change an array like `REDIM`, `DIMPUSH`, `DIMINSERT`, ... |
| `gbxinline::call("NAME", args...)` | call a built-in GBX command; variables are passed BYREF, numbers and texts by value |
| `gbxinline::DRAW_LINE(...)`, `POLY_POINT(...)`, `PRINT(...)`, `TEXT_COLOR(...)`, `LEN(...)`, `SCREEN_WIDTH()` | generated typed wrappers for the built-in commands (`GBX_INLINE_COMMAND`) |
| `gbxinline::log(text)`, `gbxinline::fail(text)` | console output, runtime error |

The handle classes `Num`, `Str`, `Element`, `Array` and `Record` wrap these
handles and behave like variables, which keeps the C++ readable:

```cpp
Num counter(gbxinline::variable("counter"));
counter += 1;
Array rows(gbxinline::variable("rows"));
rows.push(3.5);
```

`gbxinline::Result` converts to both number and text, so
`gbxinline::LEN(text)` or `gbxinline::SCREEN_WIDTH()` work like in BASIC.

### 7.4 Host access (`gbx_inline.h`, version 5)

Available in any INLINE block, no extra include beyond `<gbx_inline.h>`:

| Function | Meaning |
| --- | --- |
| `gbxinline::blitScreen(rgba, w, h, stride)` | copy an RGBA8 image into the GBX screen (custom renderers) |
| `gbxinline::spritePixels(id, &rgba, &w, &h)` | pixels of a loaded sprite, for textures |
| `gbxinline::log(text)` | write to the IDE console / stdout |
| `gbxinline::windowHandle()` | native window: Windows `HWND`, macOS `NSWindow*`, X11 `Window`, Wayland `wl_surface*`; `nullptr` headless or in the browser |
| `gbxinline::windowSize(w, h)` | window size in pixels |
| `gbxinline::glContext()` | OpenGL context created by GBX; GBX creates none, so normally `nullptr` - a module can create its own on the window handle |

The same fields exist on `GbxHost` (`window_handle`, `window_size`,
`gl_context`, added in version 5). `GBX_INLINE_VERSION` tells a module which
host fields it may use; older builds simply do not have them.

### 7.5 What modules cannot do

- Commands cannot receive arrays or records - pass handles (IDs) instead, as
  the BOX2D module does with body IDs.
- No drawing outside a step (see 6).
- No event callbacks from C++ into BASIC.
- Native archives are desktop-only; the HTML5 player compiles sources only.
- One `INLINE` compilation unit per program: all blocks share the same
  generated C++ file, so header include guards and unique helper names matter.
  Prefix private helpers with the module name or put them in a namespace.

---

## 8. Native libraries (`.a`, `.o`)

```json
"native": { "libs": ["lib/libmymod.a"] }
```

What the IDE does when the module is added:

1. Copies the archive to `lib/<module folder>/<file name>` inside the project.
2. Adds that path to the project file:

```ini
[native]
libs = lib/mymod/libmymod.a
```

3. Writes `INCLUDE "modules/mymod/MYMOD.gbx"` at the top of the project entry
   file (after leading comments and blank lines).

Removing the module reverses all three steps. The archive in `modules/` is
never touched, and the project keeps working without the module folder only if
the copy stays.

The compiler command line gets the archive and the project's `[native]`
`libDirs` and `flags`, so a module can rely on its archive being linked.

Building an archive for the module folder:

```bash
g++ -O2 -c easing.cpp -o easing.o
ar rcs lib/libeasing.a easing.o
```

**HTML5:** a Windows `.a` cannot be linked into WebAssembly. A module that
ships an archive must compile the same algorithm for the browser:

```cpp
#ifdef __EMSCRIPTEN__
namespace mymod_web { /* same functions, plain C++ */ }
#define mymod_value mymod_web::value
#endif
```

`modules/easing/EASING.gbx` is the reference for this pattern.

---

## 9. Providing a module

### Through the IDE (recommended)

1. Put the folder into `modules/`.
2. Open a project, open **Modules**, pick the module, press **Add**.
3. The IDE writes the `INCLUDE` line, copies native archives and registers
   them in `[native]`.

Installed state is derived from the project sources: a module counts as
installed when any `.gbx` in the project contains
`INCLUDE "modules/<folder>/<entry>"` (commented-out lines do not count).
**Remove** deletes that include, the `[native]` entries and the copied
archives again.

### Manually

```gbx
INCLUDE "modules/box2d/BOX2D.gbx"
```

Plus, when the module has native libs: copy `lib/<folder>/*.a` into the project
and add the path to `[native] libs`.

### Shipping a module

- Keep the whole folder together (sources, headers, `lib/`, license, `HELP.md`).
- `module.json` must stay at the top of the folder; the folder name does not
  have to match `folder` or `id`, but keeping them equal avoids confusion.
- Third-party code needs its license file in the folder (not in `files`).
- Run `node ide/scripts/build-modules.mjs` before shipping so `HELP.md`
  matches the `@help` blocks.
- Test with a small project that only has the `INCLUDE` line, in the IDE, in
  the EXE and - if the module has no native parts - in the HTML5 player.

### How the existing modules do it

| Module | Pattern |
| --- | --- |
| `modules/math` | pure GBX over eight topic files (`vectors.gbx`, `collision.gbx`, ...), GBX types and `METHOD`s, no C++ at all |
| `modules/ui` | pure GBX, several `.gbx` files, entry includes them |
| `modules/easing` | prebuilt `.a` plus `__EMSCRIPTEN__` fallback |
| `modules/opengl3d` | one large C++ header, registered commands, sprite textures, host blit |
| `modules/box2d` | wrapper split over seven headers, umbrella `box2d.h`, DebugDraw wrapper |
| `modules/json` | pure GBX, value model on `DICT`/arrays, split into value/path/parse/write |
| `modules/steam` | INLINE C++ over the Steamworks flat API, runtime-loaded library, tests against a mock |

---

## 10. Checklist for a new module

1. Pick a folder name, an `id` and a command prefix (`MYMOD_`, `BOX2D_`, ...).
   The prefix avoids collisions with BASIC and other modules.
2. Create `modules/<folder>/module.json` with `format`, `id`, `name`,
   `version`, `summary`, `folder`, `entry`, `include.entry`, `files`, tags.
3. Create the entry `.gbx`: `@help`, `@tooltip` blocks, `INLINE` include,
   optional GBX wrappers for step-only features.
4. Split C++ by topic into headers; add an umbrella header that includes them.
5. Register C++ commands with `GBX_INLINE_FN`; give them a stable prefix.
6. If you need an archive, build it into `lib/` and add `native.libs`; add the
   `__EMSCRIPTEN__` path for HTML5.
7. Add an example project under `examples/` that only uses the module.
8. Run `node ide/scripts/build-modules.mjs` and check the Modules dock.
9. Verify: `gbx --check examples/<your example>/main.gbx`, then run it. With
   `--headless` for logic checks, windowed for input and drawing.

---

## 11. Pitfalls

| Symptom | Cause |
| --- | --- |
| Module does not appear | `module.json` missing, invalid JSON, or `format` not `"gbx-module"` |
| Module appears but Add fails | `include.entry` does not start with `modules/` or does not end in `.gbx`; or no `.gbx` in `files` |
| `INCLUDE` not found | File is not in `files`/not present; path inside the include does not match `include.entry`; neither project nor central folder resolves it |
| Archive not linked | `native.libs` missing, path outside the module, or the project has no `.gbxproj` |
| Archive missing in the project | Module was added but the `[native]` entry was removed by hand |
| Help missing | No `@help` block and no `HELP.md` |
| Tooltip missing | `@tooltip` name does not match the command; or the block is outside `'`/`//`/`/* */` comments |
| Draw calls fail at runtime | Called from a registered command instead of an INLINE step |
| Works desktop, fails in HTML5 | Module ships an archive without an `__EMSCRIPTEN__` fallback |
| Old code keeps running after an edit | INLINE blocks are cached by source hash; the hash includes the included headers, so a header edit recompiles |

---

Everything in this file reflects the behaviour of
`ide/src/server/modules.mjs`, `src/parser.cpp` and `src/main.cpp`.
If one of them changes, update this guide with it.
