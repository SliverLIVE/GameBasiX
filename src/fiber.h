// GameBasiX - kooperative Fibern fuer TASK-Bloecke
//
// Ein Task laeuft auf einem eigenen Stack, aber immer auf dem Thread des
// Interpreters. Damit duerfen Tasks ganz normal Zeichnen, PRINT und alle
// GBX-Befehle benutzen; AWAIT gibt nur die Kontrolle an den Scheduler zurueck.
#pragma once

#include <cstddef>

namespace gbxfiber {

using Handle = void*;

// Im Browser nutzt TASK die kooperativen Fibern von Emscripten; auf dem
// Desktop kommen Windows-Fibern bzw. ucontext zum Einsatz.
bool available();

// Handle des aktuellen (Haupt-)Kontexts. Auf Windows wird der Thread beim
// ersten Aufruf in eine Fiber umgewandelt.
Handle mainHandle();

// Neue Fiber anlegen. `entry` darf niemals zurueckkehren; der Task-Code
// wechselt am Ende selbst zum Scheduler.
Handle create(void (*entry)(void*), void* arg, std::size_t stackBytes = 1024 * 1024);

// Eine beendete oder wartende Fiber freigeben (nie die laufende).
void destroy(Handle fiber);

// Zur angegebenen Fiber wechseln. Beim naechsten Wechsel zurueck kehrt der
// Aufruf an dieser Stelle fort.
void switchTo(Handle fiber);

}  // namespace gbxfiber
