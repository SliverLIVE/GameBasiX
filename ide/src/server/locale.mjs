// Meldungen des IDE-Servers auf Deutsch und Englisch.
//
// Die Texte im Code stehen auf Deutsch; tr() ersetzt beim Ausgeben bekannte
// Bausteine. Gewählt wird die Sprache aus den Einstellungen (config.language),
// die die Oberfläche beim Umschalten setzt.
const TABLE = [
  /* Server / API */
  ['Anfrage zu groß.', 'Request too large.'],
  ['Ungültiges JSON.', 'Invalid JSON.'],
  ['projectDir und file sind erforderlich.', 'projectDir and file are required.'],
  ['dir und file sind erforderlich.', 'dir and file are required.'],
  ['Der Pfad liegt außerhalb des Projekts.', 'The path is outside the project.'],
  ['Kein Pfad angegeben.', 'No path given.'],
  ['Projektdaten fehlen.', 'Project data is missing.'],
  ['Kein Projektordner angegeben.', 'No project folder given.'],
  ['Dieser Dateityp lässt sich nicht als Vorschau anzeigen.', 'This file type cannot be previewed.'],
  ['Datei nicht gefunden.', 'File not found.'],
  ['Keine Debug-Sitzung aktiv.', 'No debug session is active.'],
  ['Haltepunkt ohne Datei oder Zeile.', 'Breakpoint without a file or line.'],
  ['Unbekannter Debug-Befehl: ', 'Unknown debug command: '],
  ['Auftrag nicht gefunden.', 'Job not found.'],
  ['Kein Speicherort angegeben.', 'No save location given.'],
  ['Bitte einen absoluten Pfad angeben.', 'Please give an absolute path.'],
  ['Zeichensätze werden als .png oder .bmp gespeichert.', 'Character sets are saved as .png or .bmp.'],
  ['Die Bilddaten konnten nicht gelesen werden.', 'The image data could not be read.'],
  ['Die Bilddaten sind leer.', 'The image data is empty.'],
  ['Kein Pfad.', 'No path.'],
  ['Datei existiert nicht.', 'The file does not exist.'],
  ['Unbekannte API-Route: ', 'Unknown API route: '],

  /* Google Fonts */
  ['Keine Schrift angegeben.', 'No font given.'],
  ['Google Fonts ist nicht erreichbar. Bitte die Internetverbindung prüfen.',
   'Google Fonts cannot be reached. Please check the internet connection.'],
  ['Google Fonts hat keine Schriften geliefert.', 'Google Fonts returned no fonts.'],
  ['Die Antwort von Google Fonts war unlesbar.', 'The response from Google Fonts could not be read.'],
  ['Die Liste war leer.', 'The list was empty.'],
  ['Der Google-Fonts-API-Key wurde abgelehnt – die öffentliche Fontliste wird verwendet.',
   'The Google Fonts API key was rejected – the public font list is used instead.'],
  ['Die Google-Fonts-API antwortet nicht – die öffentliche Fontliste wird verwendet.',
   'The Google Fonts API is not responding – the public font list is used instead.'],
  ['Die Schrift konnte nicht von Google Fonts geladen werden.',
   'The font could not be loaded from Google Fonts.'],
  ['Diese Schriftvariante gibt es bei Google Fonts nicht.',
   'This font variant does not exist at Google Fonts.'],
  ['Die Schriftdatei konnte nicht heruntergeladen werden.',
   'The font file could not be downloaded.'],
  ['Google Fonts hat keine TrueType-Datei geliefert.', 'Google Fonts did not deliver a TrueType file.'],
  ['Die Schriftdatei ist zu groß.', 'The font file is too large.'],
  ['Die Schrift konnte nicht gespeichert werden: ', 'The font could not be saved: '],
  ['Der Asset-Ordner liegt außerhalb des Projekts.', 'The asset folder is outside the project.'],
  ['Unbekannter Fehler', 'Unknown error'],
  ['Datensatz fehlt. Bitte npm run build:dataset ausführen.',
   'Dataset missing. Run npm run build:dataset.'],

  /* Projekt und Dateien */
  ['Pfad nicht gefunden: ', 'Path not found: '],
  ['Bitte einen Projektordner, eine .gbxproj- oder eine .gbx-Datei öffnen.',
   'Please open a project folder, a .gbxproj or a .gbx file.'],
  ['Die Projektdatei enthält keine .gbx-Quelldateien.', 'The project file contains no .gbx sources.'],
  ['In diesem Ordner wurde kein GBX-Projekt gefunden.', 'No GBX project was found in this folder.'],
  ['Bitte einen gültigen Projektnamen angeben.', 'Please give a valid project name.'],
  ['Der Ordner existiert bereits und ist nicht leer: ', 'The folder already exists and is not empty: '],
  ['Ungültiger Pfad in der Vorlage: ', 'Invalid path in the template: '],
  ['Die Vorlage enthält keine GBX-Quelldateien.', 'The template contains no GBX sources.'],
  ['Unbekannte Vorlage.', 'Unknown template.'],
  ['Vorlage nicht gefunden.', 'Template not found.'],
  ['Die Vorlage ist beschädigt.', 'The template is damaged.'],
  ['Das ist keine GBX-Studio-Vorlage.', 'This is not a GBX Studio template.'],
  ['Bitte zuerst ein Projekt öffnen.', 'Please open a project first.'],
  ['Bitte einen Namen für die Vorlage angeben.', 'Please give a name for the template.'],
  ['Das Projekt enthält keine Quelldateien.', 'The project contains no source files.'],
  ['Ungültiger Quelldateipfad.', 'Invalid source path.'],
  ['Datei existiert bereits: ', 'File already exists: '],
  ['Kein Dateiname angegeben.', 'No file name given.'],
  ['Datei nicht gefunden: ', 'File not found: '],
  ['Das ist keine Datei: ', 'That is not a file: '],
  ['Kein Ordnername angegeben.', 'No folder name given.'],
  ['Ordner nicht gefunden: ', 'Folder not found: '],
  ['Das ist kein Ordner: ', 'That is not a folder: '],
  ['Quelle und Ziel sind erforderlich.', 'Source and destination are required.'],
  ['Der Projektordner selbst lässt sich nicht verschieben.',
   'The project folder itself cannot be moved.'],
  ['Ein Ordner lässt sich nicht in sich selbst verschieben.',
   'A folder cannot be moved into itself.'],
  ['Nicht gefunden: ', 'Not found: '],
  ['Das Ziel existiert bereits: ', 'The destination already exists: '],

  /* Interpreter-Anbindung */
  ['gbx.exe wurde nicht gefunden: ', 'gbx.exe was not found: '],
  ['Einstiegsdatei nicht gefunden: ', 'Entry file not found: '],
  ['GameBasiX Syntaxprüfung · ', 'GameBasiX syntax check · '],
  ['Einstieg: ', 'Entry: '],
  ['Die Syntaxprüfung hat Fehler gemeldet.', 'The syntax check reported errors.'],
  ['Prüfung erfolgreich. F5 startet das Projekt.', 'Check passed. F5 runs the project.'],
  ['Zeitüberschreitung bei der Live-Prüfung', 'Live check timed out'],
  ['Laufzeitfehler: ', 'Runtime error: '],
  ['GameBasiX Export · ', 'GameBasiX export · '],
  ['Ziel: ', 'Target: '],
  ['Der Export ist fehlgeschlagen.', 'The export failed.'],
  ['Fertig. Die EXE liegt bei den Assets und startet ohne Projektordner.',
   'Done. The EXE sits next to the assets and starts without the project folder.'],
  ['Prozess wurde beendet.', 'Process stopped.'],
  ['g++ wurde nicht gefunden: ', 'g++ was not found: '],
  ['Ordner nicht lesbar: ', 'Folder not readable: '],
  ['Nicht gefunden', 'Not found'],

  /* Startmeldungen auf der Konsole */
  ['Läuft unter ', 'Running at '],
  ['Beenden mit Strg+C', 'Exit with Ctrl+C'],
  ['Einstellungen: ', 'Settings: '],
  [' ist belegt, wähle einen freien Port …', ' is in use, choosing a free port …'],
  ['WebStart konnte nicht gestartet werden: ', 'WebStart could not be started: '],

  /* C++-Compiler (g++) */
  ['Der Compiler-Download ist nur für Windows gedacht – bitte g++ über die Paketverwaltung installieren.',
   'The compiler download is meant for Windows – please install g++ through your package manager.'],
  ['g++ ist bereits vorhanden.', 'g++ is already installed.'],
  ['Suche den aktuellen GCC-Build (WinLibs) …', 'Looking up the current GCC build (WinLibs) …'],
  ['Kein passender Download gefunden – ist GitHub erreichbar?',
   'No matching download found – is GitHub reachable?'],
  ['Lade ', 'Downloading '],
  ['Der Download ist fehlgeschlagen: ', 'The download failed: '],
  ['Entpacke …', 'Extracting …'],
  ['Das Archiv konnte nicht entpackt werden.', 'The archive could not be extracted.'],
  ['Im Archiv wurde kein g++.exe gefunden.', 'No g++.exe was found in the archive.'],
  ['g++ ist eingerichtet: ', 'g++ is ready: '],

  /* Emscripten SDK */
  ['emsdk ist vorhanden, aber emcc läuft noch nicht (install/activate abgeschlossen?)',
   'emsdk is present, but emcc does not run yet (install/activate finished?)'],
  ['emsdk wurde nicht gefunden: ', 'emsdk was not found: '],
  ['emsdk wird heruntergeladen (git clone) …', 'Downloading emsdk (git clone) …'],
  ['git clone ist fehlgeschlagen', 'git clone failed'],
  ['emsdk ist bereits vorhanden, Installation wird fortgesetzt.',
   'emsdk is already present, continuing the installation.'],
  ['emsdk ist eingerichtet.', 'emsdk is ready.'],
  ['emsdk ist bereit.', 'emsdk is ready.'],
  ['Die Installation ist nicht abgeschlossen.', 'The installation did not finish.'],
  ['emsdk ist nicht eingerichtet.', 'emsdk is not set up.'],
  ['emsdk fehlt', 'emsdk missing'],

  /* HTML5 */
  ['Die C++-Quellen fehlen neben der Editor-EXE (Ordner src/, inline/, third_party/).',
   'The C++ sources are missing next to the editor EXE (src/, inline/, third_party/).'],
  ['Baue den Interpreter als WebAssembly (', 'Building the interpreter as WebAssembly ('],
  [' Dateien) …', ' files) …'],
  ['Der wasm-Build ist fehlgeschlagen', 'The wasm build failed'],
  ['emcc ist fehlgeschlagen', 'emcc failed'],
  ['Fertig in ', 'Done in '],
  [' MB, wasm eingebettet)', ' MB, wasm embedded)'],
  ['Der wasm-Player fehlt noch (Einstellungen → HTML5: Player bauen).',
   'The wasm player is still missing (Settings → HTML5: Build player).'],
  ['Player fehlt', 'Player missing'],
  ['Quellen fehlen', 'Sources missing'],
  ['Keine .gbx-Dateien im Projekt gefunden.', 'No .gbx files found in the project.'],
  ['Keine Quellen', 'No sources'],
  ['Für INLINE fehlt gbx.exe neben der Editor-EXE.',
   'gbx.exe is missing next to the editor EXE for INLINE.'],
  ['INLINE im Browser braucht emsdk (Einstellungen → HTML5).',
   'INLINE in the browser needs emsdk (Settings → HTML5).'],
  ['INLINE-Blöcke werden mit Emscripten in den Player übersetzt (dauert etwas) …',
   'INLINE blocks are compiled into the player with Emscripten (this takes a moment) …'],
  ['Das INLINE-C++ ließ sich nicht erzeugen: ', 'The INLINE C++ could not be generated: '],
  ['INLINE-Quelltext fehlt', 'INLINE source missing'],
  ['Player aus dem Zwischenspeicher übernommen – am Projekt hat sich nichts geändert.',
   'Player taken from the cache – nothing in the project has changed.'],
  ['gbx.exe fehlt', 'gbx.exe missing'],
  ['Schreibe ', 'Writing '],
  [' Quelldateien und ', ' source files and '],
  [' Assets nach ', ' assets to '],
  ['HTML5-Build fertig: ', 'HTML5 build finished: '],
  ['Zum Starten genügt ein Doppelklick auf index.html; ein Webserver geht auch.',
   'A double-click on index.html is enough to start; a web server works too.'],

];

let current = 'en';

export function setLanguage(lang) {
  current = String(lang || '').toLowerCase().startsWith('de') ? 'de' : 'en';
}

export function language() {
  return current;
}

/** Übersetzt deutsche Bausteine; unbekannter Text bleibt unverändert. */
export function tr(text) {
  if (current !== 'en') return text;
  let out = String(text ?? '');
  for (const [de, en] of TABLE) {
    if (!out.includes(de)) continue;
    out = out.split(de).join(en);
  }
  return out;
}
