# Library example – C++ archives as GBX commands

A live signal on screen: hash noise as input, a one-pole low pass as smoothing,
and a Catmull-Rom curve from a second archive. Everything below the BASIC surface
is C++ that was compiled into a `.a` once.

```bash
cd examples/15_library
../../build/gbx.exe library.gbxproj
```

SPACE resets the filter, `+`/`-` change the amplitude, ESC quits.

All paths in the project file start at this folder, so run the example from
here (or open this folder in the IDE). The `[native]` section is short and does
all the work:

```ini
[native]
libs = lib/libgame.a              ; archive by path
libdirs = lib                     ; search folder, passed as -L
flags = "-lcurve -Inative -DGAIN=1.6"   ; -l from libdirs, an -I and a define
```

`-Inative` lets the `INLINE` block `#include "native/commands.h"`, and
`-DGAIN=1.6` is what the header sees as `GAIN` – the archive was already built
when the block is compiled, so the flag can only change the header. The
`.gbxproj` loses hand written comments when the program runs, so this README and
the header of `main.gbx` keep the explanation.

| Path | What it is |
| --- | --- |
| `main.gbx` | the program: `INLINE` include, self check, main loop |
| `scene.gbx` | drawing, uses the same commands through `INCLUDE` |
| `library.gbxproj` | `[native]` with `libs`, `libdirs` and `flags` |
| `native/commands.h` | registers the eight GBX commands |
| `native/game.h`, `native/game.cpp` | plain C++ → `lib/libgame.a` |
| `native/spline.h`, `native/spline.cpp` | plain C++ → `lib/libcurve.a` |

## The archives are checked in

`lib/libgame.a` and `lib/libcurve.a` are build output, but they belong to the
example: without them it cannot show what it is about. They were built with the
bundled toolchain from this folder:

```bash
../../compiler/bin/g++.exe -O2 -c native/game.cpp   -o game.o
../../compiler/bin/g++.exe -O2 -c native/spline.cpp -o curve.o
../../compiler/x86_64-w64-mingw32/bin/ar.exe rcs lib/libgame.a game.o
../../compiler/x86_64-w64-mingw32/bin/ar.exe rcs lib/libcurve.a curve.o
```

In the IDE **Tools → Build library** does the same and writes the archive next to
the sources. Rebuild after changing a `.cpp` – the cache key of the `INLINE` build
contains size and time stamp of the archive, so the blocks are compiled again.

`tests/library/lib_checks.gbxproj` links exactly these two files and checks their
results value by value (`ctest -R library`), so an archive that no longer matches
its header is noticed.
