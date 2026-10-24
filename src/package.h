// GameBasiX - Spielpaket
//
// Quellen und die fertig uebersetzte INLINE-Bibliothek werden an das Ende der
// eigenen EXE gehaengt. Ein so gepacktes Spiel startet ohne Projektordner: Es
// haengt sein Paket beim Start in den Temp-Ordner aus und laeuft von dort.
// Assets bleiben bewusst draussen - sie liegen neben der EXE und lassen sich
// dort austauschen. Windows ignoriert die angehaengten Bytes einer EXE.
#pragma once

#include <string>

#include "ast.h"
#include "project.h"

namespace gbxpackage {

/** Pfad der laufenden EXE. */
std::string executablePath();

/** Haengt ein Paket aus der laufenden EXE aus (nur, wenn eines vorhanden ist).
 *  `found` bleibt false, wenn die EXE ein normaler Interpreter ist. */
bool mountSelf(std::string& entryPath, bool& found, std::string& error);

/** Schreibt eine einzelne EXE mit Quellen und INLINE-Bibliothek; Assets werden
 *  daneben kopiert. `target` darf leer bleiben: dann entsteht
 *  <projektname>.exe im Projektordner. */
bool exportProject(const Program& prog, const ProjectConfig& cfg, const std::string& target,
                   std::string& report, std::string& error);

/** Wurde dieser Interpreter ohne DLL-Abhaengigkeiten gelinkt? Nur dann ist die
 *  exportierte EXE auf fremden Rechnern wirklich lauffaehig. */
bool selfContained();

}  // namespace gbxpackage
