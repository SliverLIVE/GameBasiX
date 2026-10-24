cmake_minimum_required(VERSION 3.24)
project(gbx LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(GBX_USE_SYSTEM_SDL "Use an installed SDL3 instead of fetching it" OFF)
option(GBX_WITH_TTF "Enable TrueType fonts via FreeType" ON)
# MinGW: Laufzeit statisch linken, damit gbx.exe und GBXStudio.exe ohne
# libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll laufen.
option(GBX_STATIC_RUNTIME "Link the MinGW runtime statically" OFF)

include(FetchContent)
set(GBX_SDL_TARGET SDL3::SDL3)
if(GBX_USE_SYSTEM_SDL)
  find_package(SDL3 3.2 CONFIG REQUIRED)
else()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.18.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(SDL3)
endif()

# Optional TrueType support ueber FreeType. Fehlt FreeType, wird nur die
# eingebaute Bitmap-Schrift benutzt.
set(GBX_HAVE_TTF OFF)
if(GBX_WITH_TTF)
  find_package(Freetype QUIET)
  if(GBX_STATIC_RUNTIME AND FREETYPE_FOUND AND NOT MSVC)
    # MSYS2 liefert libfreetype.dll.a und libfreetype.a. Fuer eine EXE ohne
    # DLLs muss das statische Archiv verwendet werden.
    get_filename_component(_gbx_ftdir "${FREETYPE_LIBRARY_RELEASE}" DIRECTORY)
    if(EXISTS "${_gbx_ftdir}/libfreetype.a")
      set_target_properties(Freetype::Freetype PROPERTIES
        IMPORTED_LOCATION "${_gbx_ftdir}/libfreetype.a"
        IMPORTED_LOCATION_RELEASE "${_gbx_ftdir}/libfreetype.a")
      # Das statische FreeType bringt Dekoder fuer gzip/bzip2 mit und nutzt
      # HarfBuzz (mit Graphite) fuer das Layout; die Systemteile stehen
      # dahinter, damit der Linker sie in dieser Reihenfolge aufloest.
      set(GBX_FREETYPE_EXTRA_LIBS
        harfbuzz graphite2 png16 brotlidec brotlicommon z bz2
        dwrite ole32 uuid usp10 gdi32 rpcrt4 user32)
    endif()
  endif()
  if(FREETYPE_FOUND)
    set(GBX_HAVE_TTF ON)
  else()
    message(STATUS "FreeType not found - building without TTF support")
  endif()
endif()

add_executable(gbx
  src/main.cpp
  src/i18n.cpp
  src/lexer.cpp
  src/parser.cpp
  src/value.cpp
  src/interpreter.cpp
  src/inline.cpp
  src/lint.cpp
  src/package.cpp
  src/debugger.cpp
  src/builtins.cpp
  src/graphics.cpp
  src/audio.cpp
  src/project.cpp
  src/runtime.cpp
)

target_include_directories(gbx PRIVATE src third_party inline)

# INLINE-Code bindet <gbx_inline.h> ein; die Datei muss neben gbx.exe liegen.
# Der Kopierschritt haengt am Build, damit Aenderungen am Header ankommen.
add_custom_command(TARGET gbx POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/inline
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/inline/gbx_inline.h ${CMAKE_BINARY_DIR}/inline/gbx_inline.h
  COMMENT "Copying gbx_inline.h next to gbx.exe")
target_link_libraries(gbx PRIVATE ${GBX_SDL_TARGET})
if(GBX_STATIC_RUNTIME AND NOT MSVC)
  target_link_options(gbx PRIVATE -static -static-libgcc -static-libstdc++)
  # Der Export warnt, wenn die EXE noch C++-Laufzeit-DLLs braucht.
  target_compile_definitions(gbx PRIVATE GBX_STATIC_RUNTIME=1)
endif()
if(GBX_HAVE_TTF)
  target_compile_definitions(gbx PRIVATE GBX_HAVE_TTF=1)
  target_link_libraries(gbx PRIVATE Freetype::Freetype ${GBX_FREETYPE_EXTRA_LIBS})
endif()

if(MSVC)
  target_compile_options(gbx PRIVATE /W3)
else()
  target_compile_options(gbx PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Kleines Startprogramm fuer die IDE: GBXStudio.exe landet im Projektordner
# und oeffnet den Editor aus ide/ (Electron, sonst Browser).
if(WIN32)
  enable_language(RC)
  configure_file(src/studio_launcher.rc.in ${CMAKE_BINARY_DIR}/studio_launcher.rc @ONLY)
  add_executable(GBXStudio WIN32
    src/studio_launcher.cpp
    ${CMAKE_BINARY_DIR}/studio_launcher.rc
  )
  set_target_properties(GBXStudio PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
  target_link_libraries(GBXStudio PRIVATE user32)
  # wWinMain als Einstiegspunkt (MinGW)
  if(NOT MSVC)
    target_link_options(GBXStudio PRIVATE -municode)
  endif()
  if(GBX_STATIC_RUNTIME AND NOT MSVC)
    target_link_options(GBXStudio PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()

enable_testing()
add_test(NAME language
         COMMAND gbx --headless --tests tests/language_tests.gbx)
set_tests_properties(language PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  PASS_REGULAR_EXPRESSION "0 fehlgeschlagen")

# INLINE-Bloecke: uebersetzt beim Start mit dem Compiler, der auch gbx baut.
add_test(NAME inline
         COMMAND gbx --headless --tests tests/inline_tests.gbx)
set_tests_properties(inline PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  ENVIRONMENT "GBX_CXX=${CMAKE_CXX_COMPILER}"
  PASS_REGULAR_EXPRESSION "0 fehlgeschlagen")

# UI.gbx: Widgets, Fenster, Maus und Tastatur mit virtueller Eingabe
add_test(NAME ui
         COMMAND gbx --headless --tests tests/ui_tests.gbx)
set_tests_properties(ui PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  PASS_REGULAR_EXPRESSION "0 fehlgeschlagen")

# Pixelpruefung der Zeichenbefehle (offscreen ueber den SDL-dummy-Treiber)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME render
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_render.py
                   $<TARGET_FILE:gbx>)
  set_tests_properties(render PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Befehlstests: jeder eingebaute Befehl mindestens einmal aufgerufen,
  # einmal ohne Fenster und einmal mit Dummy-Renderer
  add_test(NAME commands
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_commands.py
                   $<TARGET_FILE:gbx>)
  set_tests_properties(commands PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Spielpaket: Quellen in der EXE, Assets daneben, Start ohne Projektordner
  add_test(NAME export
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_export.py
                   $<TARGET_FILE:gbx>)
  set_tests_properties(export PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Namenspruefung: unbekannte Befehle, Funktionen, Variablen und Marken
  add_test(NAME lint
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_lint.py
                   $<TARGET_FILE:gbx>)
  set_tests_properties(lint PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Laufzeit-Debugger: Haltepunkte, Einzelschritte, Variablen, Syntaxfehler
  add_test(NAME debug
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_debug.py
                   $<TARGET_FILE:gbx>)
  set_tests_properties(debug PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
