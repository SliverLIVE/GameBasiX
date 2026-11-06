// GameBasiX - Hintergrund-Jobs
//
// Auf dem Desktop laeuft ein Job auf einem eigenen Thread; im Browser wird er
// synchron erledigt. So bleibt die BASIC-Schnittstelle in beiden Builds gleich.
// SPAWN bekommt eine Kopie der Globals und reine Argumente; das Ergebnis wird
// erst nach JOB_DONE uebernommen.
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime.h"
#include "value.h"

namespace jobs {

// Startet eine beliebige Arbeit. Liefert -1, wenn zu viele Jobs laufen.
bool available();

int submit(std::function<Value()> work);

bool done(int id);
// Kopie des Ergebnisses. `error` ist leer, wenn der Job erfolgreich war.
Value value(int id, std::string& error);
std::string error(int id);
bool release(int id);
void shutdown();

// Fuehrt eine FUNCTION auf einer Kopie der Globals aus. Nur Zahlen, Texte,
// Arrays und TYPEs als Argumente; BYREF gibt es in einem Job nicht.
int spawnFunction(Program& prog, const std::string& name, std::vector<Value> args,
                  const std::unordered_map<std::string, Slot>& globals, const SrcPos& pos);

}  // namespace jobs
