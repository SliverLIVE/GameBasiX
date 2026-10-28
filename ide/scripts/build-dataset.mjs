// Baut data/gbx-dataset.json für Referenz, Hover und Autovervollständigung.
//
// Quellen:
//   ../README.md              – Befehlsliste von GameBasiX (Name, Parameter, Gruppen)
//   data/gbx-descriptions.json – eigene Beschreibungen auf Deutsch und Englisch
//   data/signatures.json      – Prototypen der Befehle (Argumentsignaturen)
//
// Zusätzliche Hinweise zu einzelnen Befehlen stehen in NOTES.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..');

// Alle Beschreibungen sind eigene Texte und liegen zweisprachig vor. Fehlt
// einer, bricht der Lauf ab – so bleibt die Referenz vollständig.
const DESCRIPTIONS = JSON.parse(
  fs.readFileSync(path.resolve(ROOT, 'data', 'gbx-descriptions.json'), 'utf8')
);

function descriptionFor(name) {
  const own = DESCRIPTIONS[name];
  if (!own) throw new Error(`Beschreibung fehlt in data/gbx-descriptions.json: ${name}`);
  return { de: own.de, en: own.en };
}

// Die Ueberschriften der README sind englisch; intern bleiben die Kategorien
// deutsch, damit die Oberflaeche sie uebersetzen kann.
const CATEGORY_KEYS = {
  'Window and screen': 'Fenster und Bildschirm',
  Drawing: 'Zeichnen',
  'Image buffer and text': 'Bildpuffer und Text',
  Sprites: 'Sprites',
  Input: 'Eingabe',
  Sound: 'Sound',
  Math: 'Mathe',
  Strings: 'Strings',
  Files: 'Dateien',
  Network: 'Netzwerk',
  Steam: 'Steam',
  'Runtime and debug': 'Laufzeit und Debug'
};

function sectionIndex(markdown, english, german) {
  const found = markdown.indexOf(english);
  return found >= 0 ? found : markdown.indexOf(german);
}

// Steuerwörter der Sprache (aus parseStatement in src/parser.cpp).
// Die Beschreibungen stehen in data/gbx-descriptions.json.
const KEYWORDS = new Set([
  'IF',
  'THEN',
  'ELSE',
  'ELSEIF',
  'ENDIF',
  'FOR',
  'TO',
  'STEP',
  'NEXT',
  'WHILE',
  'WEND',
  'REPEAT',
  'UNTIL',
  'FOREACH',
  'IN',
  'SELECT',
  'CASE',
  'DEFAULT',
  'ENDSELECT',
  'BREAK',
  'CONTINUE',
  'GOTO',
  'GOSUB',
  'RETURN',
  'EXIT',
  'FUNCTION',
  'ENDFUNCTION',
  'SUB',
  'ENDSUB',
  'TYPE',
  'ENDTYPE',
  'METHOD',
  'ENDMETHOD',
  'SELF',
  'GLOBAL',
  'LOCAL',
  'STATIC',
  'CONSTANT',
  'CONST',
  'ENUM',
  'ENDENUM',
  'BYREF',
  'AS',
  'LET',
  'DIM',
  'REDIM',
  'DIMPUSH',
  'DIMDEL',
  'INCLUDE',
  'INLINE',
  'ENDINLINE',
  'SWAP',
  'INC',
  'DEC',
  'TRY',
  'CATCH',
  'FINALLY',
  'ENDTRY',
  'THROW',
  'END',
  'AND',
  'OR',
  'NOT'
]);

// Namen, die der Interpreter als Anweisung kennt (kein Befehl mit Argumenten).
// Sie stammen aus parseStatement() in src/parser.cpp.
const STATEMENTS = KEYWORDS;

const NOTES = {
  'FORMAT$': {
    de: 'Hinweis: Mit einer Vorlage als erstem Argument füllt der Befehl Platzhalter: FORMAT$("{0} von {1}", 3, 10). {{ und }} stehen für eine geschweifte Klammer.',
    en: 'Note: with a template as first argument the command fills placeholders: FORMAT$("{0} of {1}", 3, 10). {{ and }} stand for a literal brace.'
  },
  'JOIN$': {
    de: 'Hinweis: Das Gegenstück zu SPLIT_STRING; Start und Anzahl sind optional, ein negativer Start zählt von hinten.',
    en: 'Note: the counterpart of SPLIT_STRING; start and count are optional and a negative start counts from the end.'
  },
  ENUM: {
    de: 'Hinweis: Der erste Wert ist 0, jeder weitere zählt eins weiter; = setzt einen eigenen Wert. Die Werte gelten wie CONSTANT im ganzen Programm.',
    en: 'Note: the first value is 0 and every further value counts one up; = sets a value yourself. Like CONSTANT the values are visible in the whole program.'
  },
  RND: {
    de: 'Hinweis: RND(max) liefert eine ganze Zahl von 0 bis einschließlich max.',
    en: 'Note: RND(max) returns an integer from 0 to max inclusive.'
  },
  'MID$': {
    de: 'Hinweis: Der Startindex ist 0-basiert.',
    en: 'Note: the start index is 0-based.'
  },
  INSTR: {
    de: 'Hinweis: Der Startindex ist 0-basiert, -1 bedeutet „nicht gefunden“.',
    en: 'Note: the start index is 0-based, -1 means “not found”.'
  },
  ASC: {
    de: 'Hinweis: Der Index ist 0-basiert; ohne Index wird das erste Zeichen gelesen.',
    en: 'Note: the index is 0-based; without an index the first character is read.'
  },
  KEY_DOWN: {
    de: 'Hinweis: Der Parameter ist ein SDL-Scancode; die KEY_*-Konstanten ersparen das Nachschlagen.',
    en: 'Note: the parameter is an SDL scancode; the KEY_* constants avoid looking them up.'
  },
  FONT_LOAD: {
    de: 'Hinweis: .bmp-Dateien werden als Raster mit 16 Zeichen pro Zeile gelesen, .ttf/.otf über FreeType.',
    en: 'Note: .bmp files are read as a raster with 16 characters per row; .ttf/.otf are loaded through FreeType.'
  },
  PRINT: {
    de: 'Hinweis: Der Kerning-Parameter wird ignoriert; Bitmap-Schriften sind festbreit.',
    en: 'Note: the kerning parameter is ignored; bitmap fonts are monospaced.'
  },
  SET_ALPHA_MODE: {
    de: 'Hinweis: > 0 additiv, < 0 Alpha mit 1+Wert, 0 normal.',
    en: 'Note: > 0 additive, < 0 alpha with 1+value, 0 normal.'
  },
  SOUND_PLAY: {
    de: 'Hinweis: Liefert den Kanal zurück, den SOUND_STOP und SOUND_PLAYING erwarten.',
    en: 'Note: returns the channel expected by SOUND_STOP and SOUND_PLAYING.'
  },
  MUSIC_PLAY: {
    de: 'Hinweis: WAV und MP3 laufen über denselben Mixer; andere Formate werden abgelehnt.',
    en: 'Note: WAV and MP3 use the same mixer; other formats are rejected.'
  },
  SCREEN_SCALE: {
    de: 'Hinweis: Nutzt SDL-Logical-Presentation; SET_VIEWPORT rechnet dann in Fensterkoordinaten.',
    en: 'Note: uses SDL logical presentation; SET_VIEWPORT then works in window coordinates.'
  },
  SPRITE_GRAB: {
    de: 'Hinweis: Braucht einen echten Renderer und funktioniert nicht mit --headless.',
    en: 'Note: requires a real renderer and does not work with --headless.'
  },
  SCREEN_SAVE_BMP: {
    de: 'Hinweis: Braucht einen echten Renderer und funktioniert nicht mit --headless.',
    en: 'Note: requires a real renderer and does not work with --headless.'
  }
};

const SAMPLES = {
  DIM: 'DIM punkte[10]\nDIM karte[20][15]\nDIM gegner[5] AS FIGUR',
  REDIM: 'REDIM punkte[20]',
  DIMPUSH: 'DIMPUSH namen$[], "Ada"',
  DIMDEL: 'DIMDEL namen$[], 0',
  SCREEN_SET: 'SCREEN_SET 800, 600',
  SCREEN_SHOW: 'WHILE TRUE\n    SCREEN_CLEAR\n    PRINT "Hallo", 20, 20\n    SCREEN_SHOW\n    IF KEY_PRESSED(KEY_ESC) THEN BREAK\nWEND',
  PRINT: 'PRINT "Punkte: " + punkte, 20, 20',
  DRAW_RECT: 'DRAW_RECT 40, 40, 200, 100, RGB(200, 60, 60)',
  DRAW_LINE: 'DRAW_LINE 0, 0, 800, 600, RGB(255, 255, 255)',
  DRAW_CIRCLE: 'DRAW_CIRCLE 320, 240, 60, RGB(240, 200, 80)',
  PIXEL_SET: 'PIXEL_SET 10, 10, RGB(255, 0, 0)',
  SPRITE_LOAD: 'SPRITE_LOAD "assets/player.png", 0',
  SPRITE_DRAW: 'SPRITE_DRAW 0, 100, 80',
  SPRITE_DRAW_ROTATED_SCALED: 'SPRITE_DRAW_ROTATED_SCALED 0, 200, 150, 45, 120',
  ANIM_LOAD: 'ANIM_LOAD "assets/sheet.png", 1, 32, 32\nDRAWANIM 1, 2, 100, 100',
  MOUSE_STATE: 'MOUSE_STATE mx, my, mb1, mb2\nIF mb1 THEN PRINT "Klick bei " + mx + ", " + my, 10, 10',
  KEY_DOWN: 'IF KEY_DOWN(KEY_LEFT) THEN x = x - 4\nIF KEY_DOWN(KEY_RIGHT) THEN x = x + 4',
  KEY_PRESSED: 'IF KEY_PRESSED(KEY_SPACE) THEN schuss()',
  SOUND_LOAD: 'SOUND_LOAD "assets/blip.wav", 0\nkanal = SOUND_PLAY(0, 0, 1)',
  SOUND_PLAY: 'kanal = SOUND_PLAY(0, 0, 1)',
  MUSIC_PLAY: 'MUSIC_PLAY "assets/musik.mp3", TRUE',
  FONT_LOAD: 'FONT_LOAD "assets/font.png", 0\nSETFONT 0',
  RND: 'wurf = RND(6) + 1',
  RGB: 'farbe = RGB(255, 128, 0)',
  DELTA_TIME: 'start = DELTA_TIME()\n...\nsekunden = (DELTA_TIME() - start) / 1000.0',
  SLEEP: 'SLEEP 250',
  SPLIT_STRING: 'SPLIT_STRING "a;b;c", teile$[], ";"',
  ASSERT: 'ASSERT 2 + 2 = 4, "Mathe kaputt"'
};

// Befehle, die in der README nur außerhalb der Befehlsliste erklärt werden.
const SUPPLEMENT = [
  {
    name: 'DIM', args: 'a[n][, ...]', sample: 'DIM punkte[10]\nDIM karte[20, 15]\nDIM gegner[5] AS FIGUR'
  },
  { name: 'REDIM', args: 'a[n]', sample: 'REDIM punkte[20]' },
  { name: 'DIMDATA', args: 'a[], wert, ...', sample: 'DIM zahlen[5]\nDIMDATA zahlen[], 3, 8, 15, 1, 9' },
  { name: 'DIMPUSH', args: 'a[], wert', sample: 'DIMPUSH namen$[], "Ada"' },
  { name: 'DIMDEL', args: 'a[], index', sample: 'DIMDEL namen$[], 0' },
  { name: 'ARRAY_BOUNDS', args: 'a[], dimension', kind: 'function', sample: 'n = ARRAY_BOUNDS(punkte[], 0)' },
  { name: 'FONT_NEW', args: '', kind: 'function' },
  { name: 'SCREEN_NEW', args: '', kind: 'function' }
];

const CONSTANTS = {
  TRUE: { de: 'Wahr, also 1.', en: 'True, that is 1.' },
  FALSE: { de: 'Falsch, also 0.', en: 'False, that is 0.' },
  KEY_ESC: { de: 'Escape', en: 'Escape' },
  KEY_ENTER: { de: 'Eingabetaste', en: 'Enter' },
  KEY_SPACE: { de: 'Leertaste', en: 'Space' },
  KEY_TAB: { de: 'Tabulator', en: 'Tab' },
  KEY_BACKSPACE: { de: 'Rücktaste', en: 'Backspace' },
  KEY_DELETE: { de: 'Entf', en: 'Delete' },
  KEY_INSERT: { de: 'Einfg', en: 'Insert' },
  KEY_HOME: { de: 'Pos1', en: 'Home' },
  KEY_END: { de: 'Ende', en: 'End' },
  KEY_PGUP: { de: 'Bild auf', en: 'Page up' },
  KEY_PGDN: { de: 'Bild ab', en: 'Page down' },
  KEY_UP: { de: 'Pfeil oben', en: 'Arrow up' },
  KEY_DOWN: { de: 'Pfeil unten', en: 'Arrow down' },
  KEY_LEFT: { de: 'Pfeil links', en: 'Arrow left' },
  KEY_RIGHT: { de: 'Pfeil rechts', en: 'Arrow right' },
  KEY_LSHIFT: { de: 'Linke Umschalttaste', en: 'Left shift' },
  KEY_RSHIFT: { de: 'Rechte Umschalttaste', en: 'Right shift' },
  KEY_LCTRL: { de: 'Linke Strg-Taste', en: 'Left Ctrl' },
  KEY_RCTRL: { de: 'Rechte Strg-Taste', en: 'Right Ctrl' },
  KEY_LALT: { de: 'Linke Alt-Taste', en: 'Left Alt' },
  KEY_RALT: { de: 'Rechte Alt-Taste', en: 'Right Alt' },
  KEY_MINUS: { de: 'Minus', en: 'Minus' },
  KEY_EQUALS: { de: 'Gleichheitszeichen', en: 'Equals sign' },
  KEY_COMMA: { de: 'Komma', en: 'Comma' },
  KEY_PERIOD: { de: 'Punkt', en: 'Period' }
};

const REVIEW = {
  de: 'GameBasiX ist ein BASIC-Dialekt für 2D-Spiele auf SDL3. Der Interpreter liest die Quelldateien bei jedem Start; es gibt keinen Compiler-Zwischenschritt.',
  en: 'GameBasiX is a BASIC dialect for 2D games on SDL3. The interpreter reads the sources on every start; there is no compiler step.'
};

function normalizeName(name) {
  return String(name || '')
    .toUpperCase()
    .replace(/\(.*$/, '')
    .replace(/[$%#]+$/, '')
    .trim();
}

function parseParams(argsRaw) {
  if (!argsRaw) return [];
  let text = argsRaw.trim();
  if (text.startsWith('(') && text.endsWith(')')) text = text.slice(1, -1);
  if (!text.trim()) return [];
  const params = [];
  let optional = false;
  for (const token of text.split(',')) {
    let raw = token.trim();
    // `[farbe]` und verschachtelte Varianten wie `id[,scale[,farbe]]`
    if (raw.includes('[')) optional = true;
    const quoted = /^"[^"]*"$/.test(raw);
    raw = raw.replace(/[[\]]/g, '').replace(/^"|"$/g, '').trim();
    if (!raw) continue;
    const name = quoted ? 'datei' : raw.replace(/[^A-Za-z0-9_]/g, '') || 'wert';
    params.push({ name, label: name, type: 'Wert', optional });
  }
  return params;
}

function parseReference(markdown, existing) {
  const start = sectionIndex(markdown, '## Command reference', '## Befehlsreferenz');
  const end = sectionIndex(markdown, '## Language details', '## Sprachdetails');
  if (start < 0) throw new Error('Befehlsreferenz fehlt in der README.');
  const section = markdown.slice(start, end > 0 ? end : undefined);
  const chunks = section.split(/\n\s*\n/);
  const byName = new Map(existing.map((entry) => [entry.name.toUpperCase(), entry]));
  const commands = new Map();
  const categoriesExtra = new Set();
  let category = 'Allgemein';

  for (const chunk of chunks) {
    const lines = chunk.split('\n').map((line) => line.trim()).filter(Boolean);
    if (!lines.length) continue;
    const head = /^\*\*(.+)\*\*$/.exec(lines[0]);
    if (head) {
      const title = head[1].trim();
      category = CATEGORY_KEYS[title] || title;
      lines.shift();
      if (!lines.length) continue;
    }
    if (/^Mit `\*`|^Fertige Tastencodes/.test(lines[0])) continue;
    const text = lines.join(' ');
    for (const part of text.split('·')) {
      const match = /`([^`]+)`/.exec(part);
      if (!match) continue;
      const code = match[1].trim();
      // Fließtext wie `python tools/make_cheatsheet.py` ist kein Befehl.
      if (/[/\\]/.test(code)) continue;
      const nameMatch = /^([A-Za-z][A-Za-z0-9_$]*)\s*(.*)$/.exec(code);
      if (!nameMatch) continue;
      const name = nameMatch[1].toUpperCase();
      // Die README listet die Tastencode-Konstanten (KEY_A … KEY_Z, KEY_0 …
      // KEY_9); das sind keine Befehle. KEY_DOWN, KEY_PRESSED und KEY_WAIT
      // stehen dagegen im Interpreter und gehören in den Datensatz.
      if (/^(KEY_|Fertige)/.test(code) && !BUILTINS.has(name)) continue;
      if (['TRUE', 'FALSE', 'END'].includes(name)) continue;
      const argsRaw = nameMatch[2].trim();
      const isFunction = argsRaw.startsWith('(');
      const params = parseParams(argsRaw);
      const signature = isFunction ? `${name}(${argsRaw.replace(/^\(|\)$/g, '')})` : argsRaw ? `${name} ${argsRaw}` : name;
      const base = byName.get(name);
      const statement = STATEMENTS.has(name);
      // Funktionen liefern einen Wert: Der Prototyp steht dann in der
      // Form `n# = NAME(...)`. Nötig, weil die README Mathe- und Stringbefehle
      // ohne Klammern auflistet („ABS · SGN · …“).
      const returnsValue = (base?.syntax || []).some((form) =>
        /^\s*[A-Za-z_]\w*[$%#]?\s*=\s*[A-Za-z_]\w*[$%#]?\s*\(/.test(form)
      );
      const isValueFunction = isFunction || returnsValue;
      const description = descriptionFor(name);
      const note = NOTES[name];
      if (note) {
        description.de += `\n\n${note.de}`;
        description.en += `\n\n${note.en}`;
      }
      const entry = {
        name,
        displayName: name,
        kind: statement ? 'keyword' : isValueFunction ? 'function' : 'command',
        category: statement ? 'Sprache' : category,
        signature,
        syntax: [signature],
        description,
        sample: SAMPLES[name] || '',
        returnType: statement ? 'Schlüsselwort' : isValueFunction ? 'Wert' : 'Befehl',
        returnCode: statement ? 'v' : isValueFunction ? 'f' : 'v',
        params,
        page: base?.page || '',
        optional: false
      };
      const current = commands.get(name);
      // Eine Variante mit Parametern ist aussagekräftiger als die nackte Liste.
      if (!current || entry.params.length > current.params.length) commands.set(name, entry);
    }
  }

  for (const item of SUPPLEMENT) {
    if (commands.has(item.name)) continue;
    const argsRaw = item.args || '';
    const statement = STATEMENTS.has(item.name);
    const isFunction = item.kind === 'function' || argsRaw.startsWith('(');
    const params = statement ? [] : parseParams(argsRaw);
    const signature = argsRaw ? `${item.name} ${argsRaw}` : item.name;
    commands.set(item.name, {
      name: item.name,
      displayName: item.name,
      kind: statement ? 'keyword' : isFunction ? 'function' : 'command',
      category: statement ? 'Sprache' : 'Arrays',
      signature: isFunction && argsRaw && !statement ? `${item.name}(${argsRaw})` : signature,
      syntax: [signature],
      description: descriptionFor(item.name),
      sample: item.sample || '',
      returnType: statement ? 'Schlüsselwort' : isFunction ? 'Wert' : 'Befehl',
      returnCode: statement ? 'v' : isFunction ? 'f' : 'v',
      params,
      page: '',
      optional: false
    });
  }
  categoriesExtra.add('Arrays');

  for (const name of KEYWORDS) {
    if (commands.has(name)) continue;
    commands.set(name, {
      name,
      displayName: name,
      kind: 'keyword',
      category: 'Sprache',
      signature: name,
      syntax: [name],
      description: descriptionFor(name),
      sample: '',
      returnType: 'Schlüsselwort',
      returnCode: 'v',
      params: [],
      page: '',
      optional: false
    });
  }

  return [...commands.values()];
}

function constantEntries() {
  const entries = [];
  const push = (name, de, en) => {
    entries.push({
      name,
      displayName: name,
      kind: 'constant',
      category: 'Konstanten',
      signature: name,
      syntax: [name],
      description: { de, en },
      sample: '',
      returnType: 'Konstante',
      returnCode: 'n',
      params: [],
      page: '',
      optional: false
    });
  };
  for (const [name, text] of Object.entries(CONSTANTS)) push(name, text.de, text.en);
  for (const letter of 'ABCDEFGHIJKLMNOPQRSTUVWXYZ') {
    push(`KEY_${letter}`, `Taste ${letter}`, `Key ${letter}`);
  }
  for (let digit = 0; digit <= 9; digit++) push(`KEY_${digit}`, `Ziffer ${digit}`, `Digit ${digit}`);
  for (let fn = 1; fn <= 12; fn++) {
    push(`KEY_F${fn}`, `Funktionstaste F${fn}`, `Function key F${fn}`);
  }
  return entries;
}

function overviewEntries() {
  const entry = (name, de, en) => ({
    name,
    displayName: name,
    kind: 'overview',
    category: 'Übersicht',
    signature: name,
    syntax: [],
    description: { de, en },
    sample: '',
    returnType: 'Übersicht',
    returnCode: '-',
    params: [],
    page: '',
    optional: false
  });
  return [
    entry(
      'Sprache und Typen',
      `${REVIEW.de}\n\nZahlen sind standardmäßig FLOAT, % erzwingt Ganzzahlen, $ kennzeichnet Strings und # FLOAT. TRUE und FALSE sind 1 und 0. Blöcke enden mit ENDIF, NEXT, WEND, UNTIL, ENDSELECT, ENDFUNCTION, ENDSUB, ENDMETHOD und ENDTYPE.`,
      `${REVIEW.en}\n\nNumbers are FLOAT by default, % forces integers, $ marks strings and # FLOAT. TRUE and FALSE are 1 and 0. Blocks end with ENDIF, NEXT, WEND, UNTIL, ENDSELECT, ENDFUNCTION, ENDSUB, ENDMETHOD and ENDTYPE.`
    ),
    entry(
      'Projektdatei (.gbxproj)',
      'Die Projektdatei ist eine INI-Datei mit den Abschnitten [project], [window], [graphics], [audio], [assets] und [native]. Wichtigste Schlüssel: name, main (Einstiegspunkt), title, width, height, fullscreen, resizable, fps, vsync, enabled, volume und root (Asset-Ordner). Der Abschnitt [native] bindet fremde C++-Bibliotheken in die INLINE-Blöcke ein: libs (Archive .a und Objektdateien .o), libdirs (Suchordner, werden zu -L) und flags (weitere Flags, z. B. -lmy). Relative Pfade gelten ab dem Projektordner, Werte werden durch Leerzeichen getrennt.\n\n[project]\nname = "Mein Spiel"\nmain = "main.gbx"\n\n[window]\ntitle = "Mein Spiel"\nwidth = 800\nheight = 600\nfps = 60\n\n[native]\nlibs = lib/mylib.a\nlibdirs = lib\nflags = -lmy -DFOO=1\n\nDie IDE schreibt die Datei beim Speichern; sie lässt sich aber auch von Hand bearbeiten.',
      'The project file is an INI file with the sections [project], [window], [graphics], [audio], [assets] and [native]. Key names: name, main (entry point), title, width, height, fullscreen, resizable, fps, vsync, enabled, volume and root (asset folder). The [native] section links foreign C++ libraries into the INLINE blocks: libs (archives .a and object files .o), libdirs (search folders, passed as -L) and flags (further flags such as -lmy). Relative paths start at the project folder, values are separated by spaces.\n\n[project]\nname = "My Game"\nmain = "main.gbx"\n\n[window]\ntitle = "My Game"\nwidth = 800\nheight = 600\nfps = 60\n\n[native]\nlibs = lib/mylib.a\nlibdirs = lib\nflags = -lmy -DFOO=1\n\nThe IDE writes this file when saving, but it can also be edited by hand.'
    ),
    entry(
      'Quelldateien und INCLUDE',
      'Quellen enden auf .gbx und sind UTF-8. Kommentare beginnen mit \' oder //. Mit INCLUDE "lib.gbx" bindet die Hauptdatei weitere Quellen ein (Pfad relativ zur einbindenden Datei, Mehrfach-Include wird ignoriert). Bilder, Schriften und Sounds sucht gbx im Projektordner, im Asset-Ordner und im aktuellen Verzeichnis.',
      'Sources use the .gbx extension and UTF-8. Comments start with \' or //. INCLUDE "lib.gbx" pulls in further sources (paths relative to the including file, duplicate includes are ignored). gbx looks for images, fonts and sounds in the project folder, the asset folder and the current directory.'
    ),
    entry(
      'Sprachdetails',
      'KEY_DOWN() und KEY_PRESSED() nehmen SDL-Scancodes (KEY_*-Konstanten). MID$, ASC und INSTR sind 0-basiert, RND(max) ist inklusiv. SIN/COS/TAN und ATAN rechnen in Grad. DIM a[n] ergibt die Indizes 0…n-1, negative Indizes zählen von hinten. GOTO springt nur innerhalb des aktuellen Abschnitts. FINALLY beendet den TRY-Block. Musik spielt WAV und MP3, SCREEN_SCALE nutzt SDL-Logical-Presentation. 3D, Netzwerk, INLINE und Datei-Streams fehlen bewusst.',
      'KEY_DOWN() and KEY_PRESSED() take SDL scancodes (KEY_* constants). MID$, ASC and INSTR are 0-based, RND(max) is inclusive. SIN/COS/TAN and ATAN work in degrees. DIM a[n] yields indices 0…n-1, negative indices count from the end. GOTO only jumps within the current section. FINALLY ends the TRY block. Music plays WAV and MP3, SCREEN_SCALE uses SDL logical presentation. 3D, networking, INLINE and file streams are intentionally absent.'
    )
  ];
}

const markdown = fs.readFileSync(path.resolve(ROOT, '..', 'README.md'), 'utf8');
const SIGNATURES = JSON.parse(fs.readFileSync(path.resolve(ROOT, 'data', 'signatures.json'), 'utf8'));
const base = {
  commands: Object.entries(SIGNATURES).map(([name, syntax]) => ({ name, syntax }))
};

// Was der Interpreter wirklich kennt: die add("NAME", min, max, …)-Aufrufe.
const BUILTINS = (() => {
  const source = fs.readFileSync(path.resolve(ROOT, '..', 'src', 'builtins.cpp'), 'utf8');
  const map = new Map();
  const re = /add\("([^"]+)",\s*(-?\d+),\s*(-?\d+)/g;
  let match;
  while ((match = re.exec(source))) {
    map.set(match[1].toUpperCase(), {
      min: Number(match[2]),
      max: match[3] === '-1' ? Number.POSITIVE_INFINITY : Number(match[3])
    });
  }
  // IIF ist keine Tabellenfunktion: Der Interpreter wertet nur den gewählten
  // Zweig aus (evalCall in src/interpreter.cpp), damit `IIF(ok, x/y, 0)` nicht
  // durch 0 teilt. Für Signatur und Prüfung zählt es trotzdem mit.
  map.set('IIF', { min: 3, max: 3 });
  return map;
})();

/**
 * Hält den Datensatz am Interpreter: Die Argumentzahl und welches Argument
 * optional ist, kommt aus builtins.cpp. Befehle, die dort nicht stehen (etwa
 * IIF), fallen raus.
 */
// Zwei Zeichenbefehle nehmen ein fünftes Flag entgegen, das der Interpreter
// ignoriert. Ohne die Korrektur würde die Signatur ein Argument anzeigen,
// das nichts tut.
const ARITY_FIX = {
  DRAW_CIRCLE: 4,
  DRAW_CIRCLE_FILLED: 4
};

// Argumentnamen für Befehle, deren Prototyp keine brauchbaren Namen hergibt.
const PARAM_LABELS = {
  DEBUG: ['text'],
  STDOUT: ['text'],
  STDERR: ['text'],
  SET_ALPHA_TEST: ['wert'],
  CEIL: ['wert'],
  FLOOR: ['wert'],
  INTEGER64: ['wert'],
  'STR$': ['wert'],
  VAL: ['text'],
  SET_ENV: ['name', 'wert'],
  'TRIM$': ['text', 'zeichen'],
  'LTRIM$': ['text', 'zeichen'],
  'RTRIM$': ['text', 'zeichen'],
  DIMDATA: ['a', 'w1', 'w2'],
  IIF: ['bedingung', 'a', 'b']
};

// Die Argumentnamen in der Signatur gibt es auf Deutsch und Englisch:
// DRAW_RECT x, y, w, h, farbe / colour.
const LABELS = {
  an: 'on',
  wert: 'value',
  bedingung: 'condition',
  farbe: 'colour',
  farbe2: 'colour2',
  bildpng: 'image',
  datei: 'file',
  stellen: 'width',
  nachkomma: 'decimals',
  zahl: 'number',
  suche: 'needle',
  kachelw: 'tileW',
  kachelh: 'tileH',
  groesse: 'size',
  zeichen: 'chars',
  laenge: 'length',
  achse: 'axis',
  taste: 'button',
  links: 'left',
  rechts: 'right',
  winkel: 'angle',
  kanal: 'channel',
  server: 'listener',
  feld: 'array',
  trenner: 'separator',
  anzahl: 'count',
  leer: 'skipEmpty'
};

/** Übersetzt die Argumentnamen in einer Signatur, Klammern bleiben stehen. */
function signatureInEnglish(signature) {
  return String(signature).replace(/\b[A-Za-z_][A-Za-z0-9_]*\b/g, (word) => LABELS[word] || word);
}

/** Argumentnamen aus den Prototypen, z. B. `ABS(m#)` → ['m']. */
function syntaxLabels(entry) {
  for (const form of entry?.syntax || []) {
    const open = form.indexOf('(');
    const close = form.lastIndexOf(')');
    if (open < 0 || close <= open) continue;
    const labels = form
      .slice(open + 1, close)
      .split(',')
      .map((part) => part.trim().replace(/[^A-Za-z0-9_]/g, ''))
      .filter(Boolean);
    if (labels.length) return labels;
  }
  return [];
}

function alignWithInterpreter(list, reference) {
  const byName = new Map(reference.map((entry) => [entry.name.toUpperCase(), entry]));
  const kept = [];
  const dropped = [];
  for (const entry of list) {
    if (entry.kind !== 'command' && entry.kind !== 'function') {
      kept.push(entry);
      continue;
    }
    const key = entry.name.toUpperCase();
    const info = BUILTINS.get(key);
    if (!info) {
      dropped.push(entry.name);
      continue;
    }
    const fixed = ARITY_FIX[key];
    const max = fixed || (Number.isFinite(info.max) ? info.max : Math.max(info.min, entry.params.length));
    const own = (PARAM_LABELS[key] || []).slice(0, max);
    const readme = entry.params.map((param) => param.label || param.name).filter(Boolean);
    const names = syntaxLabels(byName.get(key));
    // Reihenfolge: eigene Namen, vollständige README-Liste, Prototyp.
    const labels =
      own.length === max
        ? own
        : readme.length === max && !readme.includes('wert')
          ? readme
          : names.length === max
            ? names
            : Array.from({ length: max }, (_, index) => readme[index] || names[index] || `arg${index + 1}`);
    const params = labels.map((label, index) => ({
      name: label,
      label,
      labelEn: LABELS[label] || label,
      type: entry.params[index]?.type || 'Wert',
      optional: index >= info.min
    }));
    // Signatur ohne Argumente ("ABS") um die Namen ergänzen – so steht in
    // Hover und Referenz eine vollständige Aufrufzeile.
    let signature = entry.signature;
    const bare = !/[\s(]/.test(signature.slice(entry.name.length));
    if (entry.kind === 'function' && (bare || !signature.includes('('))) {
      signature = `${entry.name}(${labels.join(', ')})`;
    } else if (params.length && bare) {
      signature = `${entry.name} ${labels.join(', ')}`;
    }
    kept.push({
      ...entry,
      params,
      signature,
      signatureEn: signatureInEnglish(signature),
      syntax: [signature],
      syntaxEn: [signatureInEnglish(signature)]
    });
  }
  if (dropped.length) {
    console.warn(`  nicht im Interpreter, übersprungen: ${dropped.sort().join(', ')}`);
  }
  return kept;
}

const commands = alignWithInterpreter(parseReference(markdown, base.commands || []), base.commands || []);

// Jeder eingebaute Befehl muss im Datensatz stehen – sonst fehlt er in
// Vervollständigung, Hover und Referenz.
const known = new Set(commands.map((entry) => entry.name.toUpperCase()));
const missing = [...BUILTINS.keys()].filter((name) => !known.has(name));
if (missing.length) throw new Error(`Befehle fehlen im Datensatz: ${missing.join(', ')}`);
const constants = constantEntries();
const overview = overviewEntries();
const categories = [...new Set(commands.map((command) => command.category))];
categories.push('Konstanten', 'Übersicht');

const dataset = {
  meta: {
    app: 'GameBasiX',
    version: 1,
    generatedAt: new Date().toISOString(),
    commands: commands.length,
    note: 'Aus der Befehlsliste der README erzeugt; die Beschreibungen sind eigene Texte auf Deutsch und Englisch. Spracheigenschaften stehen im Eintrag „Sprachdetails“. '
  },
  categories,
  commands: [...commands, ...constants, ...overview]
    .map((entry) => ({
      ...entry,
      signatureEn: entry.signatureEn || signatureInEnglish(entry.signature),
      syntaxEn: entry.syntaxEn || entry.syntax.map(signatureInEnglish)
    }))
    .sort((a, b) => a.name.localeCompare(b.name, 'de', { numeric: true })),
  preprocessor: [],
  suffixes: [
    { suffix: '%', description: 'Ganzzahl' },
    { suffix: '$', description: 'Zeichenkette' },
    { suffix: '#', description: 'Kommazahl' }
  ],
  builtinTypes: ['INTEGER', 'INTEGER64', 'FLOAT', 'NUMBER', 'STRING']
};

fs.writeFileSync(path.resolve(ROOT, 'data', 'gbx-dataset.json'), JSON.stringify(dataset, null, 1), 'utf8');
console.log(`${dataset.commands.length} Einträge geschrieben.`);
