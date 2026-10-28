/**
 * GameBasiX language support for Monaco.
 * Der Befehlsdatensatz wird von /api/dataset geladen und hier registriert.
 */

import { t, getLanguage } from './i18n.js';
import { checkLine, splitStatements, commandParamLabels } from './line-check.js';

// Steuerwörter von GameBasiX – dieselbe Liste wie in parseStatement()
// (src/parser.cpp) und im CheatSheet. Befehle wie MOD oder SHL sind hier
// absichtlich nicht dabei: Sie werden aufgerufen, nicht als Block benutzt.
const CONTROL = new Set([
  'IF', 'THEN', 'ELSE', 'ELSEIF', 'ENDIF', 'FOR', 'TO', 'STEP', 'NEXT',
  'WHILE', 'WEND', 'REPEAT', 'UNTIL', 'FOREACH', 'IN', 'SELECT', 'CASE',
  'DEFAULT', 'ENDSELECT', 'BREAK', 'CONTINUE', 'GOTO', 'GOSUB', 'RETURN',
  'EXIT', 'FUNCTION', 'ENDFUNCTION', 'SUB', 'ENDSUB', 'TYPE', 'ENDTYPE',
  'METHOD', 'ENDMETHOD',
  'GLOBAL', 'LOCAL', 'STATIC', 'CONSTANT', 'CONST', 'ENUM', 'ENDENUM',
  'DIM', 'REDIM',
  'DIMDEL', 'DIMPUSH', 'BYREF', 'AS', 'LET', 'END', 'INCLUDE', 'SWAP',
  'INC', 'DEC', 'TRY', 'CATCH', 'FINALLY', 'ENDTRY', 'THROW',
  'AND', 'OR', 'NOT', 'TRUE', 'FALSE'
]);

const TYPE_NAMES = new Set(['INTEGER', 'INTEGER64', 'FLOAT', 'NUMBER', 'STRING', 'ARRAY', 'ANY']);

/* ------------------------------------------------------------------ */
/* Editorfarben                                                        */
/* ------------------------------------------------------------------ */

const THEME_NAME = 'gbx-theme';

// Farbfelder für RGB() nur einmal anmelden, auch wenn die Sprachunterstützung
// neu aufgebaut wird.
let colorProviderRegistered = false;

// Liefert im Haltepunkt den aktuellen Wert einer Variablen (siehe app.js).
// Der Haken haengt am Editor, damit der Debugger nicht in dieses Modul muss.
let debugValueFor = null;

// Befehle, die das Projekt selbst per GBX_INLINE_FN(...) anmeldet
// ({ name, min, max, file, line }). Sie werden wie eingebaute Befehle gefaerbt,
// vorgeschlagen und im Hover erklaert.
let inlineCommands = [];

export function setDebugHover(hook) {
  debugValueFor = typeof hook === 'function' ? hook : null;
}

// Baut den Tooltip: Ueberschrift mit Name und Typ, darunter der Wert als
// Codeblock (Arrays und TYPEs sind dort schon aufgeklappt).
function debugHoverContents(hit) {
  const head = `**${hit.expression}**${hit.type ? ' · ' + hit.type : ''}`;
  const detail = String(hit.detail || '');
  if (!detail) return head;
  // Ein Wert kann selbst Backticks enthalten; der Zaun muss laenger sein.
  const longest = (detail.match(/`+/g) || []).reduce((max, run) => Math.max(max, run.length), 0);
  const fence = '`'.repeat(Math.max(3, longest + 1));
  return `${head}\n\n${fence}\n${detail}\n${fence}`;
}

/** Felder des Styling-Menüs – Reihenfolge und Beschriftung der Farbwähler. */
export const EDITOR_THEME_FIELDS = [
  { group: 'tokens', key: 'comment', label: 'Kommentar' },
  { group: 'tokens', key: 'keyword', label: 'Schlüsselwort' },
  { group: 'tokens', key: 'command', label: 'Befehl' },
  { group: 'tokens', key: 'directive', label: 'Präprozessor' },
  { group: 'tokens', key: 'type', label: 'Eingebauter Typ' },
  { group: 'tokens', key: 'userType', label: 'Eigener Typ' },
  { group: 'tokens', key: 'func', label: 'Funktionsname' },
  { group: 'tokens', key: 'constant', label: 'Konstante' },
  { group: 'tokens', key: 'enum', label: 'ENUM' },
  { group: 'tokens', key: 'number', label: 'Zahl' },
  { group: 'tokens', key: 'string', label: 'Zeichenkette' },
  { group: 'tokens', key: 'operator', label: 'Operator' },
  { group: 'tokens', key: 'delimiter', label: 'Trennzeichen' },
  { group: 'tokens', key: 'identifier', label: 'Bezeichner' },
  { group: 'tokens', key: 'label', label: 'Sprungmarke' },
  { group: 'colors', key: 'background', label: 'Hintergrund' },
  { group: 'colors', key: 'foreground', label: 'Text' },
  { group: 'colors', key: 'cursor', label: 'Cursor' },
  { group: 'colors', key: 'selection', label: 'Auswahl' },
  { group: 'colors', key: 'lineHighlight', label: 'Aktuelle Zeile' },
  { group: 'colors', key: 'lineNumber', label: 'Zeilennummern' }
];

/** Dunkle Standardfarben des Editors (auch Ausgangspunkt der Presets). */
export const DEFAULT_EDITOR_THEMES = {
  dark: {
    preset: 'standard',
    tokens: {
      comment: '#63735c',
      keyword: '#c792ea',
      command: '#a6e22e',
      directive: '#6cb6ff',
      type: '#ffcb6b',
      userType: '#4fd6be',
      number: '#f78c6c',
      string: '#c3e88d',
      operator: '#89ddff',
      delimiter: '#8b98a8',
      identifier: '#d7e0ea',
      label: '#ffb454',
      func: '#82aaff',
      constant: '#f2799f',
      enum: '#9d7bff'
    },
    colors: {
      background: '#0e1116',
      foreground: '#d7e0ea',
      cursor: '#a6e22e',
      selection: '#2c4020',
      lineHighlight: '#141a21',
      lineNumber: '#3c4a5b'
    }
  },
  light: {
    preset: 'standard',
    tokens: {
      comment: '#6b7a66',
      keyword: '#8b3fb8',
      command: '#2f6d00',
      directive: '#0550ae',
      type: '#8a5300',
      userType: '#0f766e',
      number: '#b45309',
      string: '#3a7d1d',
      operator: '#0b6f8a',
      delimiter: '#6b7280',
      identifier: '#1f2733',
      label: '#a15c00',
      func: '#0b57a4',
      constant: '#b0245f',
      enum: '#6b3fd6'
    },
    colors: {
      background: '#ffffff',
      foreground: '#1f2733',
      cursor: '#3f7d00',
      selection: '#cfe6c0',
      lineHighlight: '#f3f6f2',
      lineNumber: '#9aa5b1'
    }
  }
};

export const DEFAULT_EDITOR_THEME = DEFAULT_EDITOR_THEMES.dark;

/** Beschriftungen der Farbsätze. */
export const EDITOR_THEME_PRESET_LABELS = {
  standard: 'Standard',
  contrast: 'Hoher Kontrast',
  muted: 'Gedeckt',
  custom: 'Eigene Farben'
};

/** Fertige Farbsätze je Modus für das Styling-Menü. */
export const EDITOR_THEME_PRESETS = {
  dark: {
    standard: { label: 'Standard', theme: DEFAULT_EDITOR_THEMES.dark },
    contrast: {
      label: 'Hoher Kontrast',
      theme: {
        preset: 'contrast',
        tokens: {
          comment: '#8fa39a',
          keyword: '#ff9cf0',
          command: '#b6ff3d',
          directive: '#7cd1ff',
          type: '#ffd166',
          userType: '#5ff3dc',
          number: '#ff9d6b',
          string: '#d3ff8f',
          operator: '#9be7ff',
          delimiter: '#b6c2cf',
          identifier: '#ffffff',
          label: '#ffc16b',
          func: '#8fd7ff',
          constant: '#ff8fbf',
          enum: '#c4a6ff'
        },
        colors: {
          background: '#07090c',
          foreground: '#f2f6fa',
          cursor: '#b6ff3d',
          selection: '#3c5c2a',
          lineHighlight: '#10151b',
          lineNumber: '#556473'
        }
      }
    },
    muted: {
      label: 'Gedeckt',
      theme: {
        preset: 'muted',
        tokens: {
          comment: '#6b7a70',
          keyword: '#b39ddb',
          command: '#9ccc65',
          directive: '#82b1ff',
          type: '#e0b070',
          userType: '#66c2b4',
          number: '#e59a7a',
          string: '#b5cc8e',
          operator: '#8fb8c7',
          delimiter: '#7f8a96',
          identifier: '#c8d2dc',
          label: '#d9a864',
          func: '#8fb0cf',
          constant: '#cf8fa8',
          enum: '#a996d2'
        },
        colors: {
          background: '#12161b',
          foreground: '#c8d2dc',
          cursor: '#9ccc65',
          selection: '#2b3a2f',
          lineHighlight: '#171c22',
          lineNumber: '#46525e'
        }
      }
    }
  },
  light: {
    standard: { label: 'Standard', theme: DEFAULT_EDITOR_THEMES.light },
    contrast: {
      label: 'Hoher Kontrast',
      theme: {
        preset: 'contrast',
        tokens: {
          comment: '#4a5a46',
          keyword: '#6b1fa0',
          command: '#1f5a00',
          directive: '#003f8f',
          type: '#6a3d00',
          userType: '#00575a',
          number: '#8a3a00',
          string: '#2a6a10',
          operator: '#005f7a',
          delimiter: '#4a5560',
          identifier: '#000000',
          label: '#8a4a00',
          func: '#004a99',
          constant: '#8f0f4e',
          enum: '#4a21b0'
        },
        colors: {
          background: '#ffffff',
          foreground: '#000000',
          cursor: '#1f5a00',
          selection: '#b8e3a8',
          lineHighlight: '#eef4ec',
          lineNumber: '#5f6b73'
        }
      }
    },
    muted: {
      label: 'Gedeckt',
      theme: {
        preset: 'muted',
        tokens: {
          comment: '#7d8a78',
          keyword: '#9a73b8',
          command: '#4f7a2a',
          directive: '#4a6f96',
          type: '#8a6a3a',
          userType: '#3f8078',
          number: '#a06a4a',
          string: '#5f7d3a',
          operator: '#5f7f8f',
          delimiter: '#8a949c',
          identifier: '#3b4650',
          label: '#8a6a3a',
          func: '#3f6b8a',
          constant: '#9c5470',
          enum: '#6b5a92'
        },
        colors: {
          background: '#fbfbf8',
          foreground: '#3b4650',
          cursor: '#4f7a2a',
          selection: '#e2ead6',
          lineHighlight: '#f2f3ec',
          lineNumber: '#a8b0a6'
        }
      }
    }
  }
};

const isHexColor = (value) => typeof value === 'string' && /^#[0-9a-f]{6}$/i.test(value.trim());

/** Ergänzt fehlende Felder und verwirft ungültige Farbwerte. */
export function normalizeEditorTheme(spec, mode = 'dark') {
  const defaults = DEFAULT_EDITOR_THEMES[mode === 'light' ? 'light' : 'dark'];
  const out = {
    preset: 'custom',
    tokens: { ...defaults.tokens },
    colors: { ...defaults.colors }
  };
  if (!spec || typeof spec !== 'object') {
    return { preset: 'standard', tokens: { ...defaults.tokens }, colors: { ...defaults.colors } };
  }
  if (typeof spec.preset === 'string' && EDITOR_THEME_PRESET_LABELS[spec.preset]) out.preset = spec.preset;
  for (const group of ['tokens', 'colors']) {
    const source = spec[group];
    if (!source || typeof source !== 'object') continue;
    for (const key of Object.keys(out[group])) {
      if (isHexColor(source[key])) out[group][key] = source[key].trim().toLowerCase();
    }
  }
  return out;
}

// Mischt zwei Hexfarben; dient für Widgets, die nicht eigens gewählt werden.
function mixColor(from, to, amount) {
  const read = (hex) => [1, 3, 5].map((index) => parseInt(hex.slice(index, index + 2), 16));
  const [r1, g1, b1] = read(from);
  const [r2, g2, b2] = read(to);
  const channel = (a, b) => Math.round(a + (b - a) * amount).toString(16).padStart(2, '0');
  return `#${channel(r1, r2)}${channel(g1, g2)}${channel(b1, b2)}`;
}

/** Baut die Monaco-Theme-Daten aus einem gespeicherten Farbsatz. */
export function editorThemeData(spec, mode = 'dark') {
  const theme = normalizeEditorTheme(spec, mode);
  const tokens = theme.tokens;
  const c = theme.colors;
  const bare = (hex) => hex.slice(1);
  return {
    base: mode === 'light' ? 'vs' : 'vs-dark',
    inherit: true,
    rules: [
      { token: 'comment', foreground: bare(tokens.comment), fontStyle: 'italic' },
      { token: 'keyword.control', foreground: bare(tokens.keyword), fontStyle: 'bold' },
      { token: 'keyword.command', foreground: bare(tokens.command) },
      { token: 'keyword.directive', foreground: bare(tokens.directive), fontStyle: 'bold' },
      { token: 'preprocessor', foreground: bare(tokens.directive) },
      { token: 'type.identifier', foreground: bare(tokens.type) },
      { token: 'number', foreground: bare(tokens.number) },
      { token: 'number.hex', foreground: bare(tokens.number) },
      { token: 'string', foreground: bare(tokens.string) },
      { token: 'string.quote', foreground: bare(tokens.string) },
      { token: 'operator', foreground: bare(tokens.operator) },
      { token: 'operator.word', foreground: bare(tokens.operator) },
      { token: 'delimiter', foreground: bare(tokens.delimiter) },
      { token: 'identifier', foreground: bare(tokens.identifier) },
      { token: 'label', foreground: bare(tokens.label), fontStyle: 'italic' },
      { token: 'function.name', foreground: bare(tokens.func), fontStyle: 'bold' }
    ],
    colors: {
      'editor.background': c.background,
      'editor.foreground': c.foreground,
      'editorLineNumber.foreground': c.lineNumber,
      'editorLineNumber.activeForeground': c.foreground,
      'editorCursor.foreground': c.cursor,
      'editor.selectionBackground': c.selection,
      'editor.lineHighlightBackground': c.lineHighlight,
      'editorIndentGuide.background1': mixColor(c.background, c.foreground, 0.12),
      'editorIndentGuide.activeBackground1': mixColor(c.background, c.foreground, 0.26),
      'editorWidget.background': mixColor(c.background, c.foreground, 0.06),
      'editorSuggestWidget.background': mixColor(c.background, c.foreground, 0.06),
      'editorSuggestWidget.selectedBackground': mixColor(c.background, c.selection, 0.75),
      'editorHoverWidget.background': mixColor(c.background, c.foreground, 0.06),
      'editorGutter.background': c.background,
      'editorBracketMatch.background': mixColor(c.background, c.cursor, 0.18),
      'editorBracketMatch.border': c.cursor
    }
  };
}

/** Setzt Theme und die CSS-Farben für eigene TYPEs und FUNCTIONs. */
export function applyEditorTheme(monaco, spec, mode = 'dark') {
  const theme = normalizeEditorTheme(spec, mode);
  monaco.editor.defineTheme(THEME_NAME, editorThemeData(theme, mode));
  monaco.editor.setTheme(THEME_NAME);
  if (typeof document !== 'undefined') {
    document.documentElement.style.setProperty('--gbx-user-type', theme.tokens.userType);
    document.documentElement.style.setProperty('--gbx-func', theme.tokens.func);
    document.documentElement.style.setProperty('--gbx-const', theme.tokens.constant);
    document.documentElement.style.setProperty('--gbx-enum', theme.tokens.enum);
  }
  return theme;
}

// Welcher Befehl welchen Block öffnet – und womit er geschlossen wird.
const OPEN_BLOCKS = {
  FUNCTION: 'ENDFUNCTION',
  SUB: 'ENDSUB',
  METHOD: 'ENDMETHOD',
  IF: 'ENDIF',
  FOR: 'NEXT',
  FOREACH: 'NEXT',
  WHILE: 'WEND',
  REPEAT: 'UNTIL',
  SELECT: 'ENDSELECT',
  TYPE: 'ENDTYPE',
  ENUM: 'ENDENUM'
};
const CLOSERS = new Set(Object.values(OPEN_BLOCKS));
// Blöcke, bei denen ein Abschluss automatisch ergänzt werden darf.
const AUTO_CLOSE_BLOCKS = new Set(['FUNCTION', 'SUB', 'METHOD', 'IF', 'FOR', 'FOREACH', 'WHILE', 'REPEAT', 'SELECT', 'TYPE', 'ENUM']);

const norm = (s) => String(s || '').toUpperCase();
const escapeRe = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

// Scopes, die eine Deklaration beschreiben (und damit eine Zuweisung gewinnen).
const DECL_SCOPES = new Set(['GLOBAL', 'LOCAL', 'STATIC', 'PARAM', 'DIM', 'FOREACH', 'READ', 'ALIAS']);

export function analyzeSource(text) {
  const src = String(text || '');
  const codeLines = stripCommentsAndStrings(src).split(/\r?\n/);
  // Dieselben Zeilen mit Zeichenketten: der Wert einer CONSTANT steht nur dort
  // (stripCommentsAndStrings() ersetzt ihn durch Leerzeichen).
  const rawLines = src.split(/\r?\n/);
  const functions = new Map();
  const types = new Map();
  const variables = new Map();
  const constants = new Map();
  const enums = new Map();
  const labels = new Set();
  const labelDefs = [];
  let currentType = null;
  let currentMethod = null;
  let currentEnum = null;
  // Letzter Wert des offenen ENUM: ohne "=" zaehlt der naechste eins weiter.
  let enumLastValue = '';

  const addVar = (name, type = '', scope = '', line = 0, extra = {}) => {
    const clean = String(name || '').trim();
    if (!clean || /^[0-9]/.test(clean)) return;
    const key = norm(clean);
    if (CONTROL.has(key) || TYPE_NAMES.has(key)) return;
    const previous = variables.get(key);
    // Eine spätere Zuweisung darf eine erkannte Deklaration nicht überschreiben –
    // sonst verlieren GLOBALS ihren Scope und ihre Deklarationszeile.
    if (previous && DECL_SCOPES.has(String(previous.scope).toUpperCase()) && !DECL_SCOPES.has(String(scope).toUpperCase())) {
      return;
    }
    variables.set(key, {
      name: clean,
      type: type || suffixType(clean),
      scope,
      line,
      ...extra
    });
  };

  codeLines.forEach((rawLine, index) => {
    const line = rawLine.trim();
    const lineNo = index + 1;
    if (!line) return;

    // ENUM … ENDENUM: jeder Wert heisst ENUM.WERT - wie eine Konstante,
    // aber mit dem ENUM davor, damit zwei ENUMs denselben Namen haben dürfen.
    if (currentEnum) {
      if (/^ENDENUM\b/i.test(line)) {
        currentEnum = null;
        return;
      }
      const member = /^([A-Za-z_]\w*[$%#]?)\s*(?:=\s*(.*))?$/i.exec(line);
      if (member) {
        const explicit = member[2] === undefined ? '' : member[2].trim();
        // Wie im Parser: ohne "=" zaehlt der Wert eins weiter, der erste ist 0.
        const value = explicit || (enumLastValue ? `${enumLastValue}+1` : '0');
        enumLastValue = value;
        const owner = enums.get(norm(currentEnum));
        if (owner) {
          owner.values.push({ name: member[1], type: 'INTEGER', line: lineNo, value });
        }
        constants.set(norm(`${currentEnum}.${member[1]}`), {
          name: `${currentEnum}.${member[1]}`,
          value,
          type: 'INTEGER',
          line: lineNo,
          enum: currentEnum
        });
      }
      return;
    }

    if (currentType) {
      if (/^ENDTYPE\b/i.test(line)) {
        // Eine nicht geschlossene Methode endet spätestens mit dem TYPE.
        if (currentMethod) currentMethod.endLine = lineNo;
        currentType = null;
        currentMethod = null;
        return;
      }
      // TYPE-Methoden: METHOD name: params … ENDMETHOD. Der Rumpf gehört zur
      // Methode und darf nicht als TYPE-Feld gelesen werden. FUNCTION/SUB
      // innerhalb eines TYPEs lehnt der Interpreter ab; sie werden hier nur
      // noch erkannt, damit alte Dateien lesbar bleiben.
      if (currentMethod) {
        if (/^END(FUNCTION|SUB|METHOD)\b/i.test(line)) {
          currentMethod.endLine = lineNo;
          currentMethod = null;
        }
        return;
      }
      const method = /^(METHOD|FUNCTION|SUB)\s+([A-Za-z_]\w*[$%#]?)\s*(?::\s*(.*))?$/i.exec(line);
      if (method) {
        const type = types.get(currentType);
        const kind = norm(method[1]);
        const params = parseParams(method[3] || '');
        currentMethod = {
          name: method[2],
          kind,
          params,
          returnType: suffixType(method[2]) || 'NUMERIC',
          line: lineNo,
          endLine: lineNo,
          type: type.name
        };
        type.methods.push(currentMethod);
        return;
      }
      // Felder: eins pro Zeile (`x#`) oder mehrere mit Semikolon (`x#;y#;z#`).
      if (!/^(DIM|LOCAL|GLOBAL|TYPE|FUNCTION|SUB|ENDFUNCTION|ENDSUB|METHOD|ENDMETHOD)\b/i.test(line)) {
        const fields = /\bAS\b/i.test(line) ? [line] : line.split(';').map((part) => part.trim()).filter(Boolean);
        let found = false;
        for (const field of fields) {
          // Feld mit optionalem Standardwert: `x%`, `x% = 0`, `pos AS Vec`.
          const member = /^([A-Za-z_]\w*[$%#]?)(\s*\[\s*\d*\s*\](\s*\[\s*\d*\s*\])?)?(\s+AS\s+([A-Za-z_]\w*))?(\s*=\s*.*)?$/i.exec(field);
          if (!member) continue;
          found = true;
          types.get(currentType).members.push({
            name: member[1] + (member[2] ? '[]' : ''),
            type: member[5] || suffixType(member[1]),
            array: !!member[2],
            line: lineNo
          });
        }
        if (found) return;
      }
    }

    const label = /^([A-Za-z_]\w*)\s*:\s*$/.exec(line);
    if (label) {
      labels.add(label[1]);
      labelDefs.push({ name: label[1], line: lineNo });
      return;
    }

    let m = /^ENUM\s+([A-Za-z_]\w*)/i.exec(line);
    if (m) {
      currentEnum = m[1];
      enumLastValue = '';
      enums.set(norm(m[1]), { name: m[1], values: [], line: lineNo });
      return;
    }
    if (/^ENUM\b/i.test(line)) {
      currentEnum = 'ENUM';
      enumLastValue = '';
      enums.set(norm('ENUM'), { name: 'ENUM', values: [], line: lineNo });
      return;
    }

    m = /^TYPE\s+([A-Za-z_]\w*)/i.exec(line);
    if (m) {
      currentType = norm(m[1]);
      types.set(currentType, { name: m[1], members: [], methods: [], line: lineNo });
      return;
    }

    m = /^(FUNCTION|SUB)\s+([A-Za-z_]\w*[$%#]?)\s*(?::\s*(.*))?$/i.exec(line);
    if (m) {
      const kind = norm(m[1]);
      const name = m[2];
      const params = parseParams(m[3] || '');
      functions.set(norm(name), {
        name,
        kind,
        params,
        returnType: suffixType(name) || 'NUMERIC',
        line: lineNo
      });
      for (const param of params) {
        if (param.name) addVar(param.name, param.type, 'param', lineNo);
      }
      return;
    }

    m = /^(GLOBAL|LOCAL|STATIC)\b(.*)$/i.exec(line);
    if (m) {
      for (const decl of splitTopLevel(m[2])) {
        const d = /^\s*([A-Za-z_]\w*[$%#]?)(\s*\[[^\]]*\])?\s*(?:=\s*[^,]+)?\s*(?:AS\s+([A-Za-z_]\w*))?/i.exec(decl);
        if (d) addVar(d[1], d[3] || suffixType(d[1]), norm(m[1]), lineNo, { array: !!d[2] });
      }
      return;
    }

    m = /^DIM\s+([A-Za-z_]\w*[$%#]?)(\s*\[[^\]]*\])+/i.exec(line);
    if (m) {
      addVar(m[1], suffixType(m[1]), 'DIM', lineNo, { array: true });
      return;
    }

    m = /^(?:CONSTANT|CONST)\s+([A-Za-z_]\w*[$%#]?)\s*(?:AS\s+([A-Za-z_]\w*)\s*)?=\s*(.*)$/i.exec(line);
    if (m) {
      // Den Wert aus der Rohzeile holen: `"Hallo"` ist in `line` ausgeleert.
      const raw = rawLines[index] || '';
      const eq = raw.indexOf('=');
      const value = eq >= 0 ? stripLineComment(raw.slice(eq + 1)).trim() : m[3].trim();
      constants.set(norm(m[1]), {
        name: m[1],
        type: m[2] ? m[2].toUpperCase() : suffixType(m[1]),
        value,
        line: lineNo
      });
      return;
    }

    m = /^ALIAS\s+([A-Za-z_]\w*)\s+AS\s+/i.exec(line);
    if (m) {
      addVar(m[1], '', 'ALIAS', lineNo);
      return;
    }

    m = /^FOREACH\s+([A-Za-z_]\w*[$%#]?)\s+IN\b/i.exec(line);
    if (m) {
      addVar(m[1], suffixType(m[1]), 'FOREACH', lineNo);
      return;
    }

    m = /^FOR\s+([A-Za-z_]\w*[$%#]?)\s*=/i.exec(line);
    if (m) {
      addVar(m[1], suffixType(m[1]), 'FOR', lineNo);
      return;
    }

    m = /^(?:READ|INPUT)\s+(.+)$/i.exec(line);
    if (m) {
      for (const item of m[1].split(',')) {
        const name = /^\s*([A-Za-z_]\w*[$%#]?)/.exec(item)?.[1];
        if (name) addVar(name, suffixType(name), 'READ', lineNo);
      }
      return;
    }

    // Zuweisung, auch in der Kurzform `x += 1`.
    m = /^([A-Za-z_]\w*[$%#]?)\s*(?:\[[^\]]*\]\s*)*(?:\+=|-=|\*=|\/=|=)\s*[^=]/i.exec(line);
    if (m) {
      addVar(m[1], suffixType(m[1]), 'variable', lineNo);
    }
  });

  // Methoden ohne ENDMETHOD reichen bis zum Dateiende.
  for (const type of types.values()) {
    for (const method of type.methods) {
      if (!method.endLine || method.endLine <= method.line) method.endLine = codeLines.length;
    }
  }

  // Der Block endet mit seinem letzten Wert - für Outline und Sprungmarken.
  for (const entry of enums.values()) {
    entry.endLine = entry.values.length ? entry.values[entry.values.length - 1].line : entry.line;
  }

  return {
    functions: [...functions.values()],
    types: [...types.values()],
    variables: [...variables.values()],
    constants: [...constants.values()],
    enums: [...enums.values()],
    labels: [...labels],
    labelDefs
  };
}

function stripCommentsAndStrings(text) {
  let out = '';
  let inString = false;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    const next = text[i + 1];
    if (inString) {
      if (ch === '"') inString = false;
      out += ch === '\n' ? '\n' : ' ';
      continue;
    }
    if (ch === '"') {
      inString = true;
      out += ' ';
      continue;
    }
    if ((ch === '/' && next === '/') || ch === "'") {
      while (i < text.length && text[i] !== '\n') {
        out += ' ';
        i++;
      }
      if (i < text.length) out += '\n';
      continue;
    }
    out += ch;
  }
  return out;
}

/**
 * Sucht Vorkommen eigener TYPE-Namen (außerhalb von Kommentaren und
 * Zeichenketten) und liefert 1-basierte Bereiche für den Editor.
 */
export function findUserTypeRanges(text, names) {
  const wanted = new Set();
  for (const name of names || []) {
    const clean = String(name || '').trim().toUpperCase();
    if (/^[A-Z_][A-Z0-9_]*$/.test(clean)) wanted.add(clean);
  }
  if (!wanted.size) return [];
  const codeLines = stripCommentsAndStrings(String(text || '')).split(/\r?\n/);
  const ranges = [];
  codeLines.forEach((line, index) => {
    if (!line) return;
    const word = /[A-Za-z_]\w*/g;
    let match;
    while ((match = word.exec(line))) {
      if (!wanted.has(match[0].toUpperCase())) continue;
      ranges.push({ line: index + 1, startColumn: match.index + 1, endColumn: match.index + match[0].length + 1 });
    }
  });
  return ranges;
}

/**
 * Bereiche eigener FUNCTION-Namen im Code. Eigene Funktionen duerfen wie
 * Befehle ohne Klammern geschrieben werden (`DRAW_HUD`) - deshalb wird jeder
 * Treffer gefaerbt, ausser einem Feldzugriff (obj.name).
 */
export function findUserFunctionRanges(text, names) {
  const wanted = new Set();
  for (const name of names || []) {
    const clean = String(name || '').trim();
    if (/^[A-Za-z_]\w*[%$#]?$/.test(clean)) wanted.add(clean.toUpperCase());
  }
  if (!wanted.size) return [];
  const ranges = [];
  stripCommentsAndStrings(String(text || '')).split(/\r?\n/).forEach((line, index) => {
    if (!line) return;
    const word = /[A-Za-z_]\w*[%$#]?/g;
    let match;
    while ((match = word.exec(line))) {
      if (!wanted.has(match[0].toUpperCase())) continue;
      if (match.index > 0 && line[match.index - 1] === '.') continue;
      ranges.push({ line: index + 1, startColumn: match.index + 1, endColumn: match.index + match[0].length + 1 });
    }
  });
  return ranges;
}

/**
 * Bereiche von METHOD-Aufrufen (`obj.name(...)`, `self.name(...)`). Gefaerbt
 * wird nur der Methodenname und nur, wenn wirklich eine Klammer folgt und der
 * Name zu einer METHOD eines TYPEs gehoert - Variablen bleiben unberuehrt.
 */
export function findUserMethodRanges(text, names) {
  const wanted = new Set();
  for (const name of names || []) {
    const clean = String(name || '').trim();
    if (/^[A-Za-z_]\w*[%$#]?$/.test(clean)) wanted.add(clean.toUpperCase());
  }
  if (!wanted.size) return [];
  const ranges = [];
  // Empfaenger (auch Feld/Index wie liste[2]) . Methode (Leerraum) (
  const call = /[A-Za-z_]\w*(?:\s*\[[^\]]*\])*\s*\.\s*([A-Za-z_]\w*[%$#]?)(\s*)\(/g;
  stripCommentsAndStrings(String(text || '')).split(/\r?\n/).forEach((line, index) => {
    if (!line) return;
    call.lastIndex = 0;
    let match;
    while ((match = call.exec(line))) {
      const method = match[1];
      if (!wanted.has(method.toUpperCase())) continue;
      // Spalte des Methodennamens: hinter dem Namen stehen nur Leerzeichen
      // und die Klammer.
      const start = match.index + match[0].length - 1 - match[2].length - method.length;
      ranges.push({ line: index + 1, startColumn: start + 1, endColumn: start + method.length + 1 });
    }
  });
  return ranges;
}

/**
 * Bereiche von CONSTANT-Namen. Ein Feldzugriff (`obj.NAME`) bleibt aussen vor,
 * und ENUM-Werte kommen hier nicht an - die stehen in findEnumRanges().
 */
export function findConstantRanges(text, names) {
  const wanted = new Set();
  for (const name of names || []) {
    const clean = String(name || '').trim();
    if (/^[A-Za-z_]\w*[%$#]?$/.test(clean)) wanted.add(clean.toUpperCase());
  }
  if (!wanted.size) return [];
  const ranges = [];
  stripCommentsAndStrings(String(text || '')).split(/\r?\n/).forEach((line, index) => {
    if (!line) return;
    const word = /[A-Za-z_]\w*[%$#]?/g;
    let match;
    while ((match = word.exec(line))) {
      if (!wanted.has(match[0].toUpperCase())) continue;
      if (match.index > 0 && line[match.index - 1] === '.') continue;
      ranges.push({ line: index + 1, startColumn: match.index + 1, endColumn: match.index + match[0].length + 1 });
    }
  });
  return ranges;
}

/**
 * Bereiche von ENUMs: der ENUM-Name selbst (`kind: 'enum'`) und der Wert hinter
 * dem Punkt (`kind: 'constant'`, z. B. STOP in PHASE.STOP). Nur die qualifizierte
 * Schreibweise wird gefaerbt - ein nackter Wert ist ein Fehler der Pruefung.
 */
export function findEnumRanges(text, enums) {
  const wanted = new Map();
  for (const entry of enums || []) {
    const name = String(entry?.name || '').trim().toUpperCase();
    if (!/^[A-Z_]\w*$/.test(name)) continue;
    const values = new Set(
      (entry.values || [])
        .map((item) => String(item?.name || '').trim().toUpperCase())
        .filter((item) => /^[A-Z_]\w*[%$#]?$/.test(item))
    );
    wanted.set(name, values);
  }
  if (!wanted.size) return [];
  const ranges = [];
  // Die Wertedeklarationen im ENUM-Block: dort steht der Name nackt, weil er
  // erst hier definiert wird. Er wird wie eine Konstante gefaerbt.
  const declared = new Map();
  for (const entry of enums || []) {
    for (const value of entry.values || []) {
      const name = String(value?.name || '').trim();
      const line = Number(value?.line) || 0;
      if (name && line > 0) declared.set(line, name);
    }
  }
  stripCommentsAndStrings(String(text || '')).split(/\r?\n/).forEach((line, index) => {
    if (!line) return;
    // Der ENUM-Name ist nur an der Deklaration und vor einem Punkt ein ENUM.
    // Ein blosses PHASE gibt es in der Sprache nicht - und die Schreibweise
    // allein reicht nicht, weil `phase%` genauso gross geschrieben wird.
    const enumHead = /^\s*ENUM\s+([A-Za-z_]\w*)/i.exec(line);
    const declare = declared.get(index + 1);
    if (declare) {
      const head = new RegExp(`^\\s*(${escapeRe(declare)})\\b`, 'i').exec(line);
      if (head) {
        const start = head.index + head[0].length - head[1].length;
        ranges.push({
          kind: 'constant',
          line: index + 1,
          startColumn: start + 1,
          endColumn: start + head[1].length + 1
        });
      }
    }
    const word = /[A-Za-z_]\w*/g;
    let match;
    while ((match = word.exec(line))) {
      const key = match[0].toUpperCase();
      const values = wanted.get(key);
      if (!values) continue;
      const isDeclaration = Boolean(enumHead) && enumHead[1].toUpperCase() === key;
      const rest = line.slice(match.index + match[0].length);
      // Nur `ENUM PHASE` oder `PHASE.WERT` - kein Bezeichner, der zufaellig so heisst.
      if (!isDeclaration && !/^\s*\./.test(rest)) continue;
      const member = /^\s*\.\s*([A-Za-z_]\w*[%$#]?)/.exec(rest);
      ranges.push({
        kind: 'enum',
        line: index + 1,
        startColumn: match.index + 1,
        endColumn: match.index + match[0].length + 1
      });
      if (!member || !values.has(member[1].toUpperCase())) continue;
      const start = match.index + match[0].length + member[0].length - member[1].length;
      ranges.push({
        kind: 'constant',
        line: index + 1,
        startColumn: start + 1,
        endColumn: start + member[1].length + 1
      });
    }
  });
  return ranges;
}

const RGB_LITERAL = /\bRGB\s*\(\s*(0x[0-9a-f]+|-?\d+)\s*,\s*(0x[0-9a-f]+|-?\d+)\s*,\s*(0x[0-9a-f]+|-?\d+)\s*\)/gi;

/**
 * Sucht `RGB(r, g, b)` mit Zahlenwerten (auch 0x…) außerhalb von Kommentaren
 * und Zeichenketten. Liefert 1-basierte Spalten und die geklemmten Kanäle;
 * nur so lässt sich am Aufruf ein Farbfeld anzeigen.
 */
export function findRgbColors(text) {
  const hits = [];
  stripCommentsAndStrings(String(text || '')).split(/\r?\n/).forEach((line, index) => {
    RGB_LITERAL.lastIndex = 0;
    let match;
    while ((match = RGB_LITERAL.exec(line))) {
      const channels = [match[1], match[2], match[3]].map((value) => {
        const number = Number(value);
        return Number.isFinite(number) ? Math.max(0, Math.min(255, Math.round(number))) : 0;
      });
      hits.push({
        line: index + 1,
        startColumn: match.index + 1,
        endColumn: match.index + match[0].length + 1,
        red: channels[0],
        green: channels[1],
        blue: channels[2]
      });
    }
  });
  return hits;
}

function splitTopLevel(text) {
  const parts = [];
  let depth = 0;
  let current = '';
  for (const ch of String(text || '')) {
    if (ch === '(' || ch === '[') depth++;
    if (ch === ')' || ch === ']') depth = Math.max(0, depth - 1);
    if (ch === ',' && depth === 0) {
      parts.push(current);
      current = '';
    } else {
      current += ch;
    }
  }
  if (current.trim()) parts.push(current);
  return parts;
}

function parseParams(text) {
  return splitTopLevel(text)
    .map((raw) => {
      const clean = raw.replace(/\bBYREF\b/gi, '').trim();
      if (!clean) return null;
      const m = /^([A-Za-z_]\w*)([$%#]?)(\s*\[[^\]]*\])?(\s+AS\s+([A-Za-z_]\w*))?/i.exec(clean);
      if (!m) return { name: clean, type: '', byref: false };
      return {
        name: m[1] + (m[2] || ''),
        type: m[5] || suffixType(m[1] + (m[2] || '')),
        array: !!m[3],
        byref: /\bBYREF\b/i.test(raw)
      };
    })
    .filter(Boolean);
}

export function suffixType(name) {
  const n = String(name || '');
  if (n.endsWith('$')) return 'STRING';
  if (n.endsWith('%')) return 'INTEGER';
  if (n.endsWith('#')) return 'FLOAT';
  return 'NUMERIC';
}

/** Schneidet einen //-Kommentar ab, ohne Zeichenketten anzufassen. */
function stripLineComment(text) {
  let inString = false;
  for (let i = 0; i < text.length; i++) {
    if (text[i] === '"') inString = !inString;
    else if (!inString && text[i] === '/' && text[i + 1] === '/') return text.slice(0, i);
  }
  return text;
}

/**
 * Wert einer CONSTANT oder eines ENUM-Werts, soweit er ohne Laufzeit feststeht.
 * Zahl, Zeichenkette und einfache Rechnungen daraus werden ausgerechnet; ein
 * Ausdruck mit anderen Namen (`BASE * 2`) bleibt als Ausdruck stehen - so
 * steht im Tooltip nie ein geratener Wert.
 */
export function constantValueText(raw) {
  const text = stripLineComment(String(raw ?? '')).trim();
  if (!text) return '';
  const quoted = /^"([\s\S]*)"$/.exec(text);
  if (quoted) return quoted[1];
  if (/^[-+()\d\s.*/%]+$/.test(text)) {
    const number = evalNumberExpression(text);
    if (number !== null) return String(Number.isInteger(number) ? number : Number(number.toPrecision(12)));
  }
  return text;
}

/** Kleiner Rechner fuer Zahlenausdruecke: + - * / % und Klammern. */
function evalNumberExpression(text) {
  let index = 0;
  const skip = () => {
    while (index < text.length && /\s/.test(text[index])) index++;
  };
  function primary() {
    skip();
    if (text[index] === '(') {
      index++;
      const value = addition();
      skip();
      if (value === null || text[index] !== ')') return null;
      index++;
      return value;
    }
    const start = index;
    while (index < text.length && /[\d.]/.test(text[index])) index++;
    if (start === index) return null;
    const value = Number(text.slice(start, index));
    return Number.isFinite(value) ? value : null;
  }
  function unary() {
    skip();
    if (text[index] === '+' || text[index] === '-') {
      const sign = text[index] === '-' ? -1 : 1;
      index++;
      const value = unary();
      return value === null ? null : sign * value;
    }
    return primary();
  }
  function product() {
    let value = unary();
    if (value === null) return null;
    for (;;) {
      skip();
      const op = text[index];
      if (op !== '*' && op !== '/' && op !== '%') return value;
      index++;
      const right = unary();
      if (right === null) return null;
      if ((op === '/' || op === '%') && right === 0) return null;
      value = op === '*' ? value * right : op === '/' ? value / right : value % right;
    }
  }
  function addition() {
    let value = product();
    if (value === null) return null;
    for (;;) {
      skip();
      const op = text[index];
      if (op !== '+' && op !== '-') return value;
      index++;
      const right = product();
      if (right === null) return null;
      value = op === '+' ? value + right : value - right;
    }
  }
  const result = addition();
  skip();
  if (result === null || index !== text.length) return null;
  return result;
}

/**
 * Löst eine Kette wie `b.pos` über Variablen und TYPE-Member auf.
 * TYPE-Member dürfen selbst ein TYPE sein (`pos AS Vec`), eine
 * TYPE-Deklaration innerhalb eines TYPE gibt es nicht.
 * Liefert den erreichten TYPE samt Pfad – oder null, wenn die Kette in
 * keinem eigenen TYPE endet (z. B. `b.name$` oder eine Zahl).
 */
export function resolveMemberChain(symbols, parts) {
  const variables = symbols?.variables || [];
  const types = symbols?.types || [];
  const findType = (name) => types.find((item) => norm(item.name) === norm(memberBaseName(name)));
  if (!parts?.length) return null;
  const variable = variables.find((item) => norm(item.name) === norm(memberBaseName(parts[0])));
  if (!variable?.type) return null;
  let type = findType(variable.type);
  if (!type) return null;
  const path = [variable.name];
  for (const part of parts.slice(1)) {
    // `items[3]` und `items[]` treffen beide das Array-Member `items[]`.
    const member = (type.members || []).find((item) => norm(memberBaseName(item.name)) === norm(memberBaseName(part)));
    if (!member) return null;
    path.push(memberBaseName(member.name));
    type = findType(member.type);
    if (!type) return null;
  }
  return { type, path, members: type.members || [] };
}

/**
 * Sucht die TYPE-Methode, in deren Rumpf die Zeile liegt. Der Rumpf beginnt
 * hinter der FUNCTION/SUB-Zeile und endet mit ENDFUNCTION/ENDSUB.
 */
export function enclosingTypeAt(symbols, lineNumber) {
  const types = Array.isArray(symbols) ? symbols : symbols?.types || [];
  const line = Number(lineNumber) || 0;
  for (const type of types) {
    for (const method of type.methods || []) {
      const start = Number(method.line) || 0;
      const end = Number(method.endLine) || start;
      if (line > start && line <= end) return { type, method };
    }
  }
  return null;
}

/**
 * Löst `self.…` innerhalb einer TYPE-Methode auf. `self` zeigt auf das
 * Objekt – dahinter steht kein Typsuffix (`self.x`, nicht `self.x#`).
 */
export function resolveSelfChain(symbols, type, parts) {
  const types = symbols?.types || [];
  const findType = (name) => types.find((item) => norm(item.name) === norm(memberBaseName(name)));
  if (!type) return null;
  let current = type;
  const path = ['self'];
  for (const part of parts) {
    const member = (current.members || []).find((item) => norm(memberBaseName(item.name)) === norm(memberBaseName(part)));
    if (!member) return null;
    path.push(memberBaseName(member.name));
    current = findType(member.type);
    if (!current) return null;
  }
  return { type: current, path, members: current.members || [], viaSelf: true };
}

/**
 * Member-Name ohne Array-Kennung und ohne Typsuffix: `items[]` → `items`,
 * `items[3]` → `items`, `y#` → `y`.
 */
export function memberBaseName(name) {
  return String(name || '')
    .replace(/\[\]$/, '')
    .replace(/\[[^\]]*\]$/, '')
    .replace(/[$%#]$/, '');
}

/** Member-Name für die Einfügung hinter `self.` – ohne `#`, `%`, `$` und `[]`. */
export function selfMemberName(name) {
  return String(name || '').replace(/\[\]$/, '').replace(/[$%#]$/, '');
}

/**
 * Datensatz-Einträge zu einem Präfix: erst die Treffer am Wortanfang, dann
 * die Treffer irgendwo im Namen. Der zweite Topf wird begrenzt, damit ein
 * einzelner Buchstabe nicht den ganzen Datensatz aufblendet. Die Trennung
 * ist wichtig, weil der Datensatz alphabetisch liegt: Würde schon beim
 * Sammeln abgebrochen, fielen späte Namen wie `TRUE` bei `T` heraus, obwohl
 * sie vorne im Wort stehen.
 */
export function matchCommands(commands, prefix, limit = 120) {
  const upper = String(prefix || '').toUpperCase();
  const atStart = [];
  const inside = [];
  for (const command of commands || []) {
    const name = String(command?.name || '').toUpperCase();
    if (!name) continue;
    if (!upper || name.startsWith(upper)) atStart.push(command);
    else if (name.includes(upper)) inside.push(command);
  }
  return { atStart, inside: inside.slice(0, Math.max(0, limit)) };
}

/**
 * Ermittelt die Blöcke, die bis zur angegebenen Zeile (1-basiert) noch offen
 * sind – innerster Block zuletzt.
 */
export function openBlocksAt(text, lineNumber) {
  const rows = stripCommentsAndStrings(String(text || '')).split(/\r?\n/);
  const limit = Math.min(Math.max(1, Number(lineNumber) || rows.length), rows.length);
  const stack = [];
  for (let index = 0; index < limit; index++) {
    const line = rows[index].replace(/\s+$/, '').trim();
    if (!line) continue;
    const first = (/^([A-Za-z_?]\w*)/.exec(line) || [])[1];
    if (!first) continue;
    const kind = first.toUpperCase();
    if (Object.hasOwn(OPEN_BLOCKS, kind)) {
      if (kind === 'IF') {
        // THEN ist optional; einzeilig wird es erst mit einer Anweisung dahinter:
        // „IF x THEN PRINT …“ schließt in derselben Zeile ab.
        const thenIndex = line.search(/\bTHEN\b/i);
        if (thenIndex >= 0 && line.slice(thenIndex + 4).trim()) continue;
      }
      stack.push({ kind, closer: OPEN_BLOCKS[kind], line: index + 1 });
      continue;
    }
    if (CLOSERS.has(kind)) {
      for (let i = stack.length - 1; i >= 0; i--) {
        if (stack[i].closer === kind) {
          stack.splice(i, 1);
          break;
        }
      }
    }
  }
  return stack;
}

/** Prüft, ob eine Zeile einen Block eröffnet, der geschlossen werden muss. */
export function openerInLine(text) {
  const line = stripCommentsAndStrings(String(text || '')).replace(/\s+$/, '').trim();
  if (!line) return null;
  const first = (/^([A-Za-z_]\w*)/.exec(line) || [])[1];
  if (!first) return null;
  const kind = first.toUpperCase();
  if (!AUTO_CLOSE_BLOCKS.has(kind)) return null;
  if (kind === 'IF') {
    // THEN ist optional; nur die Einzeilerform schließt sofort.
    const thenIndex = line.search(/\bTHEN\b/i);
    if (thenIndex >= 0 && line.slice(thenIndex + 4).trim()) return null;
  }
  return { kind, closer: OPEN_BLOCKS[kind] };
}

/**
 * Ergänzt nach dem Drücken von Enter hinter einem Blockanfang den passenden
 * Abschluss (z. B. WEND nach WHILE), solange die nächste Zeile frei ist.
 */
export function attachBlockAutoClose(monaco, editor) {
  editor.onKeyDown((event) => {
    if (event.keyCode !== monaco.KeyCode.Enter) return;
    if (event.ctrlKey || event.metaKey || event.altKey || event.shiftKey) return;
    const model = editor.getModel();
    const position = editor.getPosition();
    if (!model || !position) return;
    const lineText = model.getLineContent(position.lineNumber);
    if (position.column !== lineText.length + 1) return;
    const opener = openerInLine(lineText);
    if (!opener) return;
    const openerLine = position.lineNumber;
    const indent = /^\s*/.exec(lineText)[0];
    // Erst nach der normalen Zeilenschaltung eingreifen.
    setTimeout(() => {
      const model2 = editor.getModel();
      const current = editor.getPosition();
      if (!model2 || model2 !== model || !current) return;
      if (current.lineNumber !== openerLine + 1) return;
      if (model2.getLineContent(current.lineNumber).trim()) return;
      const nextLine = current.lineNumber < model2.getLineCount() ? model2.getLineContent(current.lineNumber + 1) : '';
      if (nextLine.trim().toUpperCase().startsWith(opener.closer)) return;
      // Rumpf eine Stufe einrücken, Abschluss auf Höhe des Blockanfangs.
      const bodyIndent = indent + '    ';
      const lineLength = model2.getLineContent(current.lineNumber).length;
      editor.pushUndoStop();
      editor.executeEdits('gbx-block-close', [
        {
          range: new monaco.Range(current.lineNumber, 1, current.lineNumber, lineLength + 1),
          text: `${bodyIndent}\n${indent}${opener.closer}`
        }
      ]);
      // Der Cursor bleibt im Rumpf, nicht hinter dem Abschluss.
      editor.setPosition({ lineNumber: current.lineNumber, column: bodyIndent.length + 1 });
      editor.pushUndoStop();
      editor.revealPositionInCenterIfOutsideViewport();
    }, 0);
  });
}

/* ------------------------------------------------------------------ */
/* Schlüsselwörter groß schreiben                                      */
/* ------------------------------------------------------------------ */

/**
 * Wörter, die die IDE groß schreibt: Schlüsselwörter, Datentypen und alle
 * Befehle des Datensatzes. Die Sprache unterscheidet keine Groß-/
 * Kleinschreibung, Referenz und IDE schreiben diese Wörter aber groß.
 */
export function keywordCaseWords(dataset) {
  const words = new Set();
  const add = (name) => {
    // Präprozessorzeilen (?IFDEF …) werden ohne das Fragezeichen gemerkt.
    const clean = String(name || '').trim().replace(/^\?/, '');
    if (!/^[A-Za-z_]\w*[$%#]?$/.test(clean)) return;
    words.add(clean.toUpperCase());
  };
  CONTROL.forEach(add);
  TYPE_NAMES.forEach(add);
  for (const command of dataset?.commands || []) {
    if (command.kind === 'overview') continue;
    add(command.name);
  }
  for (const entry of dataset?.preprocessor || []) add(entry.name);
  return words;
}

/**
 * Liegt die Zeile `lineIndex` (0-basiert) in einem INLINE … ENDINLINE-Block?
 * `lineAt(line)` liefert den Text der Zeile, damit auch ein Monaco-Modell
 * ohne Kopie der ganzen Datei durchsucht werden kann.
 */
export function insideInlineBlock(lineAt, lineIndex) {
  for (let line = lineIndex - 1; line >= 0; line--) {
    const text = stripCommentsAndStrings(String(lineAt(line) || '')).trim();
    if (/^ENDINLINE\b/i.test(text)) return false;
    if (/^INLINE\b/i.test(text)) return true;
  }
  return false;
}

/**
 * Prüft, ob das Wort direkt vor `index` groß geschrieben wird. `index` ist die
 * Stelle hinter dem Wort – dort steht gerade das Trennzeichen, das das Wort
 * beendet. Reine Funktion, damit die Regel ohne Editor geprüft werden kann.
 */
export function keywordCaseAt(line, index, words, insideInline = false) {
  if (!words || typeof words.has !== 'function') return null;
  const text = String(line == null ? '' : line);
  const end = Math.max(0, Math.min(index == null ? text.length : index, text.length));
  const before = text.slice(0, end);
  const match = /(?:^|[^A-Za-z0-9_$%#])([A-Za-z_]\w*[$%#]?)$/.exec(before);
  if (!match) return null;
  const start = end - match[1].length;
  // In Zeichenketten und Kommentaren darf nichts umgeschrieben werden.
  if (stripCommentsAndStrings(before).slice(start) !== match[1]) return null;
  const upper = match[1].toUpperCase();
  if (!words.has(upper)) return null;
  // Im C++-Block ist nur das abschließende ENDINLINE ein Sprachwort.
  if (insideInline && upper !== 'ENDINLINE') return null;
  if (match[1] === upper) return null;
  return { start, end, text: upper };
}

/**
 * Schreibt getippte Schlüsselwörter groß – nicht in INLINE … ENDINLINE,
 * Zeichenketten und Kommentaren. Ausgelöst wird das am Trennzeichen hinter
 * dem Wort (Leerzeichen, Klammer, Zeilenumbruch …), damit ein Name wie
 * `globalcounter` unangetastet bleibt.
 */
export function attachKeywordCasing(monaco, editor, dataset) {
  const words = keywordCaseWords(dataset);
  if (!words.size) return;
  let writing = false;

  editor.onDidChangeModelContent((event) => {
    if (writing || event.changes.length !== 1) return;
    const change = event.changes[0];
    const inserted = change.text;
    if (change.rangeLength !== 0 || !inserted) return;
    // Nur Trennzeichen beenden ein Wort; `.` gehört zu Feldzugriffen.
    if (/[A-Za-z0-9_$%#]/.test(inserted[0]) || inserted[0] === '.') return;
    const model = editor.getModel();
    if (!model || model.getLanguageId() !== 'gbx') return;
    const start = change.range.getStartPosition();
    const lineNumber = start.lineNumber;
    if (lineNumber < 1 || lineNumber > model.getLineCount()) return;
    const lineText = model.getLineContent(lineNumber);
    const edit = keywordCaseAt(
      lineText,
      start.column - 1,
      words,
      insideInlineBlock((line) => model.getLineContent(line + 1), lineNumber - 1)
    );
    if (!edit) return;
    const original = lineText.slice(edit.start, edit.end);
    // Nicht während des Änderungsereignisses ins Modell schreiben.
    queueMicrotask(() => {
      if (writing) return;
      const current = editor.getModel();
      if (!current || current !== model) return;
      if (current.getLineContent(lineNumber).slice(edit.start, edit.end) !== original) return;
      writing = true;
      try {
        editor.executeEdits('gbx-keyword-case', [
          {
            range: new monaco.Range(lineNumber, edit.start + 1, lineNumber, edit.end + 1),
            text: edit.text
          }
        ]);
      } finally {
        writing = false;
      }
    });
  });
}

export function registerGbx(monaco, dataset, options = {}) {
  const getSymbols = options.getSymbols || (() => ({ functions: [], types: [], variables: [], constants: [], enums: [] }));
  // Die Analyse der aktiven Datei kommt aus der IDE (dort zwischengespeichert),
  // damit `self` denselben Stand sieht wie Outline und Symbolsuche.
  const getAnalysis = options.getAnalysis || ((model) => analyzeSource(model.getValue()));
  const commands = dataset.commands || [];
  const byName = new Map(commands.map((c) => [norm(c.name), c]));
  // Alle eingebauten Namen bekommen die Befehl-Farbe – auch Funktionen wie
  // KEY_PRESSED oder ABS und die KEY_*-Konstanten. Vorher waren nur kind 'command'
  // gefärbt, dadurch blieben Funktionen grau.
  const commandNames = commands
    .filter((c) => ['command', 'optional', 'function', 'constant'].includes(c.kind))
    .map((c) => c.name)
    .sort((a, b) => b.length - a.length);
  const keywordNames = commands.filter((c) => c.kind === 'keyword').map((c) => c.name);
  const operatorNames = commands.filter((c) => c.kind === 'operator').map((c) => c.name);

  // Befehle aus GBX_INLINE_FN(...) sehen wie eingebaute aus – nur mit dem
  // Hinweis, wo sie angemeldet wurden.
  function inlineSignature(entry) {
    const count = entry.max < 0 ? Math.max(entry.min, 1) : Math.max(entry.max, entry.min, 0);
    const args = Array.from({ length: count }, (_, index) => `arg${index + 1}`);
    if (entry.max < 0) args.push('…');
    return `${entry.name}(${args.join(', ')})`;
  }

  function inlineWhere(entry) {
    return entry.file ? `${entry.file}${entry.line ? ':' + entry.line : ''}` : t('im INLINE-Block');
  }

  function inlineEntries() {
    return inlineCommands.map((entry) => ({
      name: entry.name,
      displayName: entry.name,
      kind: 'inline',
      category: 'INLINE',
      signature: inlineSignature(entry),
      syntax: [inlineSignature(entry)],
      description: {
        de: `Befehl aus dem INLINE-Block, angemeldet in ${inlineWhere(entry)}.`,
        en: `Command from an INLINE block, registered in ${inlineWhere(entry)}.`
      },
      sample: '',
      returnType: 'Wert',
      params: []
    }));
  }

  const languages = monaco.languages;
  if (!languages.getLanguages().some((l) => l.id === 'gbx')) {
    languages.register({ id: 'gbx', extensions: ['.gbx'], aliases: ['GameBasiX', 'GBX', 'gbx'] });
  }

  languages.setLanguageConfiguration('gbx', {
    comments: { lineComment: "'" },
    brackets: [
      ['(', ')'],
      ['[', ']']
    ],
    autoClosingPairs: [
      { open: '"', close: '"', notIn: ['string', 'comment'] },
      { open: '(', close: ')', notIn: ['string', 'comment'] },
      { open: '[', close: ']', notIn: ['string', 'comment'] }
    ],
    surroundingPairs: [
      { open: '"', close: '"' },
      { open: '(', close: ')' },
      { open: '[', close: ']' }
    ],
    indentationRules: {
      increaseIndentPattern: /^\s*(FUNCTION|SUB|IF|FOR|WHILE|REPEAT|SELECT|TYPE|ENUM|TRY|ELSE|ELSEIF|CASE|DEFAULT|CATCH|FINALLY)\b(?!.*\bTHEN\b.*\S).*/i,
      decreaseIndentPattern: /^\s*(ENDIF|ENDFUNCTION|ENDSUB|ENDMETHOD|ENDTYPE|ENDSELECT|ENDENUM|ENDTRY|NEXT|WEND|UNTIL|ELSE|ELSEIF|CASE|DEFAULT|CATCH|FINALLY)\b/i
    }
  });

  // Farbfelder an RGB(r, g, b): ein Klick auf das Kästchen öffnet Monacos
  // Farbwähler, der den Aufruf mit den gewählten Werten neu schreibt.
  if (!colorProviderRegistered && typeof languages.registerColorProvider === 'function') {
    colorProviderRegistered = true;
    const to255 = (value) => Math.max(0, Math.min(255, Math.round(value * 255)));
    languages.registerColorProvider('gbx', {
      provideDocumentColors(model) {
        return findRgbColors(model.getValue()).map((hit) => ({
          range: new monaco.Range(hit.line, hit.startColumn, hit.line, hit.endColumn),
          color: { red: hit.red / 255, green: hit.green / 255, blue: hit.blue / 255, alpha: 1 }
        }));
      },
      provideColorPresentations(model, colorInfo) {
        const { red, green, blue } = colorInfo.color;
        const text = `RGB(${to255(red)}, ${to255(green)}, ${to255(blue)})`;
        return [{ label: text, textEdit: { range: colorInfo.range, text } }];
      }
    });
  }

  // Der Tokenizer kennt die eingebauten Befehle und die Namen, die das Projekt
  // selbst per GBX_INLINE_FN(...) anmeldet. Aendert sich diese Liste, wird er
  // neu gesetzt – Monaco faerbt die offenen Dateien dann sofort neu.
  function monarchSpec() {
    return {
      ignoreCase: true,
      defaultToken: '',
      keywords: [...new Set([...CONTROL, ...keywordNames])],
      types: [...TYPE_NAMES],
      commands: [...new Set([...commandNames, ...inlineCommands.map((entry) => entry.name)])].sort(
        (a, b) => b.length - a.length
      ),
      operators: operatorNames,
      tokenizer: {
        root: [
          // Vor der Leerraum-Regel: sonst ist der Zeilenanfang schon verbraucht
          // und ein eingerueckter INLINE-Block beginnt nie.
          [/^\s*INLINE\s*$/, { token: 'keyword.control', next: '@inline', nextEmbedded: 'cpp' }],
          // Kopfzeile von FUNCTION/SUB/METHOD: der Name hinter dem Schluesselwort
          // und der Doppelpunkt bekommen eine eigene Farbe (FUNCTION name: …).
          // Der Doppelpunkt ist Pflicht; Zeilen ohne ihn werden trotzdem als
          // Name gefaerbt, den Fehler meldet der Parser.
          [
            /^(\s*)(FUNCTION|SUB|METHOD)(\s+)([A-Za-z_]\w*[$%#]?)(\s*:)/,
            ['white', 'keyword.control', 'white', 'function.name', 'function.name']
          ],
          [
            /^(\s*)(FUNCTION|SUB|METHOD)(\s+)([A-Za-z_]\w*[$%#]?)(?!\s*:)/,
            ['white', 'keyword.control', 'white', 'function.name']
          ],
          [/\s+/, 'white'],
          [/'.*$/, 'comment'],
          [/\/\/.*$/, 'comment'],
          [/0x[0-9a-f]+/i, 'number.hex'],
          [/\b\d+(\.\d+)?([eE][+-]?\d+)?\b/, 'number'],
          [/"/, { token: 'string.quote', next: '@string' }],
          [/[A-Za-z_]\w*(?=\s*:)/, 'label'],
          [
            /[A-Za-z_]\w*[$%#]?/,
            {
              cases: {
                '@keywords': 'keyword.control',
                '@types': 'type.identifier',
                '@commands': 'keyword.command',
                '@operators': 'operator.word',
                '@default': 'identifier'
              }
            }
          ],
          [/<>|<=|>=|==|[=<>+\-*/^&|!~]/, 'operator'],
          [/[()\[\]{},.:;]/, 'delimiter']
        ],
        inline: [
          // Das Wort ENDINLINE beendet Block und eingebettete Sprache.
          [/^\s*ENDINLINE\b/, { token: 'keyword.control', next: '@pop', nextEmbedded: '@pop' }]
        ],
        string: [
          [/[^"]+/, 'string'],
          [/"/, { token: 'string.quote', next: '@pop' }]
        ]
      }
    };
  }

  function installTokenizer() {
    languages.setMonarchTokensProvider('gbx', monarchSpec());
  }
  installTokenizer();

  applyEditorTheme(monaco, options.theme);

  const markdown = {
    escape(value) {
      return String(value || '').replace(/[\\`*_{}[\]()#+\-.!]/g, '\\$&');
    },
    forCommand(command) {
      const english = getLanguage() === 'en';
      const signature =
        (english ? command.signatureEn : command.signature) || command.signature || command.name;
      let md = '```gbx\n' + signature + '\n```\n';
      // Die Beschreibungen liegen in beiden Sprachen vor; die Auswahl folgt
      // dem DE/EN-Schalter und greift beim nächsten Hover sofort.
      const de = command.description?.de;
      const en = command.description?.en;
      const description = english ? en || de : de || en;
      if (description) md += '\n' + description;
      if (command.category) md += `\n\n*${t(command.category)}*`;
      return md;
    }
  };

  // `self` braucht den TYPE, in dessen Methodenrumpf der Cursor steht.
  function ownTypeAt(model, position) {
    return enclosingTypeAt(getAnalysis(model), position.lineNumber);
  }

  function methodCompletion(method, typeName, range) {
    const snippetRule = languages.CompletionItemInsertTextRule?.InsertAsSnippet;
    const params = method.params || [];
    const snippet = params.length
      ? `${method.name}(${params.map((param, index) => '${' + (index + 1) + ':' + param.name + '}').join(', ')})`
      : `${method.name}()`;
    return {
      label: method.name,
      kind: languages.CompletionItemKind.Method,
      detail: `${method.kind}${params.length ? ' · ' + params.map((param) => param.name).join(', ') : ''}`,
      documentation: t('{kind} von {type}', { kind: method.kind, type: typeName }),
      insertText: params.length && !snippetRule ? `${method.name}()` : snippet,
      insertTextRules: snippetRule,
      sortText: '0_' + method.name,
      range
    };
  }

  function wordRange(model, position) {
    const line = model.getLineContent(position.lineNumber);
    const before = line.slice(0, position.column - 1);
    const m = /([A-Za-z_]\w*[$%#]?)$/.exec(before);
    if (!m) return new monaco.Range(position.lineNumber, position.column, position.lineNumber, position.column);
    const start = position.column - m[1].length;
    return new monaco.Range(position.lineNumber, start, position.lineNumber, position.column);
  }

  function wordBefore(model, position) {
    const line = model.getLineContent(position.lineNumber);
    const before = line.slice(0, position.column - 1);
    return /([A-Za-z_]\w*[$%#]?)$/.exec(before)?.[1] || '';
  }

  /** Das ganze Wort unter dem Cursor – nicht nur der Teil davor. Sonst
      erscheint der Hover erst, wenn der Zeiger hinter dem Wort steht. */
  function wordAt(model, position) {
    const line = model.getLineContent(position.lineNumber);
    const index = Math.min(Math.max(0, position.column - 1), line.length);
    const isName = (ch) => /[A-Za-z0-9_]/.test(ch);
    if (index < line.length && !isName(line[index]) && !/[$%#]/.test(line[index])) {
      // Zwischen zwei Zeichen: nur dann zählt das Wort davor.
      if (!(index > 0 && isName(line[index - 1]))) return null;
    }
    let start = index;
    while (start > 0 && isName(line[start - 1])) start--;
    let end = index;
    while (end < line.length && isName(line[end])) end++;
    // Typkennungen hängen direkt am Namen (MID$, score%, flag#).
    while (end < line.length && /[$%#]/.test(line[end])) end++;
    if (start === end) return null;
    const word = line.slice(start, end);
    if (!/^[A-Za-z_]/.test(word)) return null;
    return {
      word,
      range: new monaco.Range(position.lineNumber, start + 1, position.lineNumber, end + 1)
    };
  }

  function completionKind(command) {
    if (command.kind === 'keyword' || command.kind === 'preprocessor') return languages.CompletionItemKind.Keyword;
    if (command.kind === 'operator') return languages.CompletionItemKind.Operator;
    if (command.kind === 'constant') return languages.CompletionItemKind.Constant;
    if (command.kind === 'type') return languages.CompletionItemKind.TypeParameter;
    if (command.returnType && !/^(Befehl|void|System)/.test(command.returnType)) return languages.CompletionItemKind.Function;
    return languages.CompletionItemKind.Function;
  }

  languages.registerCompletionItemProvider('gbx', {
    // Nur bewusste Zeichen öffnen die Liste; Buchstaben lösen sie über die
    // normalen Vorschläge aus. Ein Leerzeichen soll nichts aufblenden.
    triggerCharacters: ['.', '?'],
    provideCompletionItems(model, position, context) {
      const prefix = wordBefore(model, position);
      const range = wordRange(model, position);
      const suggestions = [];
      const before = model.getLineContent(position.lineNumber).slice(0, position.column - 1);

      // Blöcke, die noch offen sind: der passende Abschluss (WEND, ENDIF,
      // ENDFUNCTION …) steht ganz oben – mit Verweis auf die geöffnete Zeile.
      // Abschlüsse gibt es nur am Anfang einer Anweisung – mitten in einem
      // Befehl lässt sich kein WHILE/FOR schließen. Erlaubt sind nur das leere
      // Zeilenende und das erste Wort, das gerade getippt wird.
      const atStatementStart = /^\s*[A-Za-z_]\w*[$%#]?$/.test(before);
      const openBlocks = atStatementStart ? openBlocksAt(model.getValue(), position.lineNumber) : [];
      if (openBlocks.length) {
        const seen = new Set();
        [...openBlocks].reverse().forEach((block, index) => {
          if (seen.has(block.closer)) return;
          seen.add(block.closer);
          suggestions.push({
            label: block.closer,
            kind: languages.CompletionItemKind.Keyword,
            detail: t('schließt {kind} aus Zeile {line}', { kind: block.kind, line: block.line }),
            documentation: {
              value: t('Der Block **{kind}** aus Zeile {line} ist noch offen. **{closer}** beendet ihn.', {
                kind: block.kind,
                line: block.line,
                closer: block.closer
              })
            },
            insertText: block.closer,
            filterText: block.closer,
            sortText: `0_${index}_${block.closer}`,
            range
          });
        });
      }

      // Member eines TYPE nach "variable." – auch verschachtelt (b.pos.x).
      // Die Kette wird über die Member-Typen aufgelöst; endet sie nicht in
      // einem eigenen TYPE, bleibt die Liste leer statt alle Befehle zu zeigen.
      const own = ownTypeAt(model, position);
      // Ketten dürfen über Array-Indizes laufen (`b.items[3].`); der Index
      // wird bei der Auflösung übersprungen.
      const chain = /((?:[A-Za-z_]\w*[$%#]?\s*(?:\[[^\]]*\]\s*)?\.\s*)+)([A-Za-z_]\w*[$%#]?)?$/.exec(before);
      if (chain) {
        const parts = chain[1].split('.').map((part) => part.trim()).filter(Boolean);
        const symbols = getSymbols();
        // "PHASE." - die Werte genau dieses ENUM. Vor dem Punkt steht der
        // ENUM-Name, nicht ein Objekt; ein TYPE-Feld gleichen Namens bleibt
        // bei der Auflösung darunter.
        const enumOwner = parts.length === 1 ? (symbols.enums || []).find((e) => norm(e.name) === norm(parts[0])) : null;
        if (enumOwner) {
          for (const value of enumOwner.values || []) {
            suggestions.push({
              label: value.name,
              kind: languages.CompletionItemKind.Constant,
              detail: `ENUM ${enumOwner.name}`,
              documentation: t('Wert von `{type}.{value}`', {
                type: enumOwner.name,
                value: value.name
              }),
              insertText: value.name,
              sortText: '0_' + value.name,
              range
            });
          }
          return { suggestions };
        }
        // `self` steht in einer TYPE-Methode für das eigene Objekt; sonst
        // bleibt die Kette bei Variablen und deren Membern.
        const resolved = parts.length
          ? norm(parts[0]) === 'SELF'
            ? own?.type
              ? resolveSelfChain(symbols, own.type, parts.slice(1))
              : null
            : resolveMemberChain(symbols, parts)
          : null;
        for (const member of resolved?.members || []) {
          const shown = resolved.viaSelf ? selfMemberName(member.name) : member.name;
          suggestions.push({
            label: shown,
            kind: languages.CompletionItemKind.Field,
            detail: member.type || t('Member'),
            documentation: t('Member von {path} ({type})', {
              path: resolved.path.concat(shown).join('.'),
              type: resolved.type.name
            }),
            insertText: shown,
            range
          });
        }
        // Methoden des TYPE: FUNCTION/SUB im TYPE-Rumpf.
        for (const method of resolved?.type?.methods || []) {
          suggestions.push(methodCompletion(method, resolved.type.name, range));
        }
        return { suggestions };
      }

      // Typen nach AS – auch im Rumpf eines TYPE (`pos AS Vec`). Dort sind
      // nur Typen sinnvoll, keine Befehle oder Variablen.
      if (/\bAS\s+(?:[A-Za-z_]\w*[$%#]?)?$/i.test(before)) {
        const symbols = getSymbols();
        for (const type of symbols.types) {
          suggestions.push({
            label: type.name,
            kind: languages.CompletionItemKind.Struct,
            detail: `TYPE · ${t('{n} Member', { n: type.members?.length || 0 })}`,
            documentation: `TYPE ${type.name}`,
            insertText: type.name,
            sortText: '0_' + type.name,
            range
          });
        }
        for (const builtin of TYPE_NAMES) {
          suggestions.push({
            label: builtin,
            kind: languages.CompletionItemKind.TypeParameter,
            detail: t('Typ'),
            insertText: builtin,
            sortText: '1_' + builtin,
            range
          });
        }
        return { suggestions };
      }

      const symbols = getSymbols();
      // Eigene Funktionen aus einer anderen Datei zeigen ihren Fundort.
      const currentFile = decodeURIComponent(model.uri.path || '').replace(/\\/g, '/');
      const fileHint = (file) =>
        file && !currentFile.endsWith('/' + String(file).replace(/\\/g, '/')) ? ` · ${file}` : '';
      for (const fn of symbols.functions) {
        suggestions.push({
          label: fn.name,
          kind: fn.kind === 'SUB' ? languages.CompletionItemKind.Method : languages.CompletionItemKind.Function,
          detail: `${fn.kind}${
            fn.params?.length ? ' · ' + fn.params.map((p) => p.name).join(', ') : ''
          }${fileHint(fn.file)}`,
          documentation: `**${fn.kind} ${fn.name}**${
            fn.params?.length
              ? `\n\n${t('Parameter: {params}', { params: fn.params.map((p) => `\`${p.name}\``).join(', ') })}`
              : ''
          }`,
          insertText: fn.name,
          sortText: '0_' + fn.name,
          range
        });
      }
      for (const v of symbols.variables) {
        suggestions.push({
          label: v.name,
          kind: languages.CompletionItemKind.Variable,
          detail: `${v.type || t('Variable')}${v.scope ? ' · ' + v.scope : ''}`,
          insertText: v.name,
          sortText: '1_' + v.name,
          range
        });
      }
      for (const c of symbols.constants) {
        // ENUM-Werte stehen hinter ihrem ENUM (PHASE.STOP), nicht einzeln.
        if (c.enum) continue;
        suggestions.push({
          label: c.name,
          kind: languages.CompletionItemKind.Constant,
          detail: 'CONSTANT',
          insertText: c.name,
          sortText: '1_' + c.name,
          range
        });
      }
      for (const entry of symbols.enums || []) {
        suggestions.push({
          label: entry.name,
          kind: languages.CompletionItemKind.Enum,
          detail: `ENUM · ${t('{n} Werte', { n: (entry.values || []).length })}`,
          documentation: `**ENUM ${entry.name}**\n\n${(entry.values || [])
            .map((value) => `- \`${entry.name}.${value.name}\``)
            .join('\n')}`,
          insertText: entry.name,
          sortText: '1_' + entry.name,
          range
        });
      }
      for (const declaration of symbols.types) {
        suggestions.push({
          label: declaration.name,
          kind: languages.CompletionItemKind.Struct,
          detail: `TYPE · ${t('{n} Member', { n: declaration.members?.length || 0 })}`,
          insertText: declaration.name,
          sortText: '2_' + declaration.name,
          range
        });
      }

      // Im Rumpf einer TYPE-Methode: `self` und die eigenen Felder/Methoden.
      // `self` ist immer klein; der Member dahinter trägt keinen
      // Typsuffix (`self.pos`, nicht `self.pos#`).
      if (own?.type) {
        suggestions.push({
          label: 'self',
          kind: languages.CompletionItemKind.Keyword,
          detail: `TYPE ${own.type.name}`,
          documentation: t('Das eigene Objekt in {type}.{method} (`*this`). Klein schreiben und ohne Typsuffix verwenden: `self.x`.', {
            type: own.type.name,
            method: own.method?.name || ''
          }),
          insertText: 'self',
          sortText: '0_self',
          range
        });
        for (const member of own.type.members || []) {
          const bare = selfMemberName(member.name);
          suggestions.push({
            label: member.name,
            kind: languages.CompletionItemKind.Field,
            detail: `self.${bare}${member.type ? ' · ' + member.type : ''}`,
            documentation: t('Feld von {type} – im Rumpf auch als `self.{bare}`', {
              type: own.type.name,
              bare
            }),
            insertText: member.name,
            sortText: '0_' + member.name,
            range
          });
        }
        for (const method of own.type.methods || []) {
          suggestions.push(methodCompletion(method, own.type.name, range));
        }
      }

      const upper = prefix.toUpperCase();
      if (upper || context.triggerKind === languages.CompletionTriggerKind.Invoke || context.triggerKind === 0) {
        // Übersichtsartikel („Sprache und Typen“ …) sind Referenztexte, keine
        // Befehle – sie gehören nicht in die Vorschlagsliste.
        const pool = [...commands.filter((command) => command.kind !== 'overview'), ...inlineEntries()];
        const { atStart, inside } = matchCommands(pool, upper);
        const snippetRule = languages.CompletionItemInsertTextRule?.InsertAsSnippet;
        const suggestionFor = (command, rank) => {
          // INCLUDE braucht einen Dateinamen; der Vorschlag setzt ihn gleich mit.
          const isInclude = command.name.toUpperCase() === 'INCLUDE';
          return {
            label: command.name,
            kind: completionKind(command),
            detail:
              (getLanguage() === 'en' ? command.signatureEn : command.signature) ||
              t(command.category || ''),
            documentation: { value: markdown.forCommand(command) },
            insertText: isInclude && snippetRule ? 'INCLUDE "${1:datei.gbx}"' : command.name,
            insertTextRules: isInclude ? snippetRule : undefined,
            filterText: command.name,
            sortText: rank + command.name,
            range
          };
        };
        // Treffer am Wortanfang zuerst – `TRUE` steht bei `WHILE T` damit
        // vor Befehlen, die das T nur irgendwo im Namen tragen.
        for (const command of atStart) suggestions.push(suggestionFor(command, '3_'));
        for (const command of inside) suggestions.push(suggestionFor(command, '5_'));
      }

      return { suggestions };
    }
  });

  languages.registerHoverProvider('gbx', {
    async provideHover(model, position) {
      const base = staticHover(model, position);
      // Im Haltepunkt steht der aktuelle Wert ueber der statischen Hilfe.
      const live = debugValueFor ? await debugValueFor(model, position) : null;
      if (!live) return base;
      const value = { value: debugHoverContents(live) };
      if (!base) return { range: live.range, contents: [value] };
      return { range: base.range, contents: [value, ...base.contents] };
    }
  });

  /** Herkunft eines eigenen Symbols: "Definiert in datei.gbx:12 · F12". */
  function originText(symbol) {
    if (!symbol?.file) return '';
    return `\n\n${t('Definiert in {file}:{line}', { file: symbol.file, line: symbol.line })}\n\nF12 · ${t(
      'Rechtsklick → Gehe zu Definition'
    )}`;
  }

  function staticHover(model, position) {
    const hit = wordAt(model, position);
    if (!hit) return null;
    const word = hit.word;
    const own = ownTypeAt(model, position);
    // `self` im Methodenrumpf: eigenes Objekt samt Feldern und Methoden.
    // Außerhalb einer Methode bleibt es beim Schlüsselwort aus der Referenz.
    if (norm(word) === 'SELF') {
      if (own?.type) {
        const type = own.type;
        const where = own.method ? `\n\n${own.method.kind} ${type.name}.${own.method.name}` : '';
        const parts = [
          `**self** · TYPE ${type.name}${where}\n\n${t('Klein schreiben und ohne Typsuffix: `self.x`.')}`
        ];
        const fields = (type.members || [])
          .map((member) => `- \`${member.name}\`${member.type ? ': ' + member.type : ''}`)
          .join('\n');
        const methods = (type.methods || [])
          .map((method) => `- ${method.kind} ${method.name}(${(method.params || []).map((param) => param.name).join(', ')})`)
          .join('\n');
        if (fields) parts.push(`**${t('Felder')}**\n\n${fields}`);
        if (methods) parts.push(`**${t('Methoden')}**\n\n${methods}`);
        return { range: hit.range, contents: [{ value: parts.join('\n\n') }] };
      }
    }
    // PHASE.STOP: der Wert eines ENUM, kein Feld eines TYPEs.
    {
      const line = model.getLineContent(position.lineNumber);
      const before = line.slice(0, hit.range.startColumn - 1);
      const owner = /([A-Za-z_]\w*)\s*\.\s*$/.exec(before)?.[1];
      const symbols = getSymbols();
      const enums = symbols.enums || [];
      const enumOwner = owner ? enums.find((entry) => norm(entry.name) === norm(owner)) : null;
      const enumValue = (enumOwner?.values || []).find((value) => norm(value.name) === norm(word));
      if (enumValue) {
        const where = originText({ ...enumValue, file: enumOwner.file });
        return {
          range: hit.range,
          contents: [
            {
              value: `**${enumOwner.name}.${enumValue.name}** · ENUM ${enumOwner.name}${
                enumValue.type ? ' · ' + enumValue.type : ''
              }\n\n${t('Wert {value} von ENUM {type}. Schreib ihn immer mit dem ENUM davor.', {
                value: constantValueText(enumValue.value),
                type: enumOwner.name
              })}${where}`
            }
          ]
        };
      }
      const enumHit = enums.find((entry) => norm(entry.name) === norm(word));
      if (enumHit) {
        const values = (enumHit.values || [])
          .map((value) => {
            const shown = constantValueText(value.value);
            return `- \`${enumHit.name}.${value.name}\`${shown ? ' = ' + shown : ''}`;
          })
          .join('\n');
        return {
          range: hit.range,
          contents: [
            {
              value: `**ENUM ${enumHit.name}**${
                values ? `\n\n**${t('Werte')}**\n\n${values}` : ''
              }${originText(enumHit)}`
            }
          ]
        };
      }
    }
    // Member-Ketten: `self.pos` im Methodenrumpf und `b.pos.y` über
    // Variablen. Gezeigt werden Pfad und Ziel-TYPE mit Feldern/Methoden.
    {
      const line = model.getLineContent(position.lineNumber);
      const before = line.slice(0, hit.range.startColumn - 1);
      const chain = /((?:[A-Za-z_]\w*[$%#]?\s*(?:\[[^\]]*\]\s*)?\.\s*)+)$/.exec(before);
      const parts = chain ? chain[1].split('.').map((part) => part.trim()).filter(Boolean) : [];
      if (parts.length) {
        const symbols = getSymbols();
        const parent =
          norm(parts[0]) === 'SELF'
            ? own?.type
              ? resolveSelfChain(symbols, own.type, parts.slice(1))
              : null
            : resolveMemberChain(symbols, parts);
        // Member werden ohne Typsuffix geschrieben (`b.pos.y`, nicht `y#`).
        const member = (parent?.members || []).find(
          (item) => norm(memberBaseName(item.name)) === norm(memberBaseName(word))
        );
        if (parent && member) {
          // Der Pfad folgt der Schreibweise im Quelltext (`b.pos.y`, nicht `y#`).
          const path = parent.path.concat(word).join('.');
          const typeName = member.type || suffixType(member.name);
          const target =
            (getAnalysis(model).types || []).find((item) => norm(item.name) === norm(memberBaseName(typeName))) ||
            symbols.types.find((item) => norm(item.name) === norm(memberBaseName(typeName)));
          const lines = [`**${path}** · ${target ? 'TYPE ' : ''}${typeName}`];
          const fields = (target?.members || [])
            .map((field) => `- \`${field.name}\`${field.type ? ': ' + field.type : ''}`)
            .join('\n');
          const methods = (target?.methods || [])
            .map((method) => `- ${method.kind} ${method.name}(${(method.params || []).map((param) => param.name).join(', ')})`)
            .join('\n');
          if (fields) lines.push(`**${t('Felder')}**\n\n${fields}`);
          if (methods) lines.push(`**${t('Methoden')}**\n\n${methods}`);
          return { range: hit.range, contents: [{ value: lines.join('\n\n') }] };
        }
      }
    }
    // CONSTANT: der Wert steht im Tooltip, auch wenn die Deklaration in einer
    // anderen Datei liegt.
    {
      const symbols = getSymbols();
      const constant = (symbols.constants || []).find(
        (entry) => !entry.enum && norm(entry.name) === norm(word)
      );
      if (constant) {
        const value = constantValueText(constant.value);
        const type = constant.type ? ` · ${constant.type}` : '';
        const shown = value ? `\n\n${t('Wert `{value}`', { value })}` : '';
        return {
          range: hit.range,
          contents: [
            { value: `**${constant.name}** · CONSTANT${type}${shown}${originText(constant)}` }
          ]
        };
      }
    }
    const command = byName.get(norm(word));
    if (command) {
      return {
        range: hit.range,
        contents: [{ value: markdown.forCommand(command) }]
      };
    }
    // Selbst angemeldete Befehle gibt es nur in diesem Projekt.
    const inline = inlineCommands.find((entry) => entry.name === norm(word));
    if (inline) {
      const english = getLanguage() === 'en';
      const where = inlineWhere(inline);
      const value = english
        ? `**${inlineSignature(inline)}** · INLINE\n\nRegistered in \`${where}\`.`
        : `**${inlineSignature(inline)}** · INLINE\n\nAngemeldet in \`${where}\`.`;
      return { range: hit.range, contents: [{ value }] };
    }
    const symbols = getSymbols();
    const fn = symbols.functions.find((f) => norm(f.name) === norm(word));
    if (fn) {
      const params = fn.params?.length ? fn.params.map((p) => `\`${p.name}\``).join(', ') : t('keine');
      return {
        range: hit.range,
        contents: [
          { value: `**${fn.kind} ${fn.name}**\n\n${t('Parameter: {params}', { params })}${originText(fn)}` }
        ]
      };
    }
    for (const type of symbols.types) {
      const method = (type.methods || []).find((entry) => norm(entry.name) === norm(word));
      if (!method) continue;
      const params = method.params?.length ? method.params.map((p) => `\`${p.name}\``).join(', ') : t('keine');
      return {
        range: hit.range,
        contents: [
          {
            value: `**${method.kind} ${type.name}.${method.name}**\n\n${t('Parameter: {params}', {
              params
            })}${originText({ file: type.file, line: method.line })}`
          }
        ]
      };
    }
    const variable = symbols.variables.find((v) => norm(v.name) === norm(word));
    if (variable) {
      return {
        range: hit.range,
        contents: [
          {
            value: `**${variable.name}**\n\n${t('Typ: `{type}`', { type: variable.type || 'NUMERIC' })}${
              variable.scope ? ' · ' + variable.scope : ''
            }`
          }
        ]
      };
    }
    const type = symbols.types.find((item) => norm(item.name) === norm(word));
    if (type) {
      return {
        range: hit.range,
        contents: [
          {
            value: `**TYPE ${type.name}**${originText(type)}\n\n${(type.members || [])
              .map((m) => `- \`${m.name}\`${m.type ? ': ' + m.type : ''}`)
              .join('\n') || t('Keine Member')}${
              (type.methods || []).length
                ? '\n\n' +
                  type.methods
                    .map((m) => `- ${m.kind} ${m.name}(${(m.params || []).map((p) => p.name).join(', ')})`)
                    .join('\n')
                : ''
            }`
          }
        ]
      };
    }
    return null;
  }

  languages.registerSignatureHelpProvider('gbx', {
    triggerCharacters: ['(', ','],
    provideSignatureHelp(model, position) {
      const before = model.getValueInRange({
        startLineNumber: Math.max(1, position.lineNumber - 3),
        startColumn: 1,
        endLineNumber: position.lineNumber,
        endColumn: position.column
      });
      const open = before.lastIndexOf('(');
      if (open < 0) return null;
      const head = before.slice(0, open);
      const nameMatch = /([A-Za-z_]\w*[$%#]?)\s*$/.exec(head);
      if (!nameMatch) return null;
      const command = byName.get(norm(nameMatch[1]));
      if (!command) return null;
      const afterOpen = before.slice(open + 1);
      let depth = 0;
      let activeParameter = 0;
      for (const ch of afterOpen) {
        if (ch === '(') depth++;
        if (ch === ')') depth = Math.max(0, depth - 1);
        if (ch === ',' && depth === 0) activeParameter++;
      }
      // Typlabels aus den Prototypen durch die Namen aus der Signatur ersetzen
      // (`MOUSE_STATE` → mx##, my##, mba##, mbb## statt vier Mal `Integer (BYREF)`).
      const english = getLanguage() === 'en';
      const labels = commandParamLabels(command).map((label, index) => {
        const param = command.params?.[index];
        return english ? param?.labelEn || label : label;
      });
      const description =
        getLanguage() === 'en'
          ? command.description?.en || command.description?.de || ''
          : command.description?.de || command.description?.en || '';
      const signatures = [];
      if (labels.length) {
        signatures.push({
          label: `${command.name}(${labels.join(', ')})`,
          documentation: { value: description },
          parameters: labels.map((label) => ({ label }))
        });
      } else {
        const sig = (english ? command.signatureEn : command.signature) || command.name;
        signatures.push({
          label: sig,
          documentation: { value: description },
          parameters: []
        });
      }
      return {
        value: { signatures, activeSignature: 0, activeParameter: Math.min(activeParameter, Math.max(0, labels.length - 1)) },
        dispose() {}
      };
    }
  });

  languages.registerDocumentSymbolProvider('gbx', {
    provideDocumentSymbols(model) {
      const analysis = analyzeSource(model.getValue());
      const result = [];
      for (const type of analysis.types) {
        const symbol = (item, kind) => ({
          name: item.name,
          detail: kind === 'field' ? item.type : item.kind,
          kind: kind === 'field' ? monaco.languages.SymbolKind.Field : monaco.languages.SymbolKind.Method,
          range: new monaco.Range(item.line, 1, item.line, 1),
          selectionRange: new monaco.Range(item.line, 1, item.line, 1)
        });
        result.push({
          name: type.name,
          detail: 'TYPE',
          kind: monaco.languages.SymbolKind.Struct,
          range: new monaco.Range(type.line, 1, type.line, 1),
          selectionRange: new monaco.Range(type.line, 1, type.line, 1),
          children: [
            ...(type.members || []).map((member) => symbol(member, 'field')),
            ...(type.methods || []).map((method) => symbol(method, 'method'))
          ]
        });
      }
      for (const fn of analysis.functions) {
        result.push({
          name: fn.name,
          detail: fn.kind,
          kind: fn.kind === 'SUB' ? monaco.languages.SymbolKind.Method : monaco.languages.SymbolKind.Function,
          range: new monaco.Range(fn.line, 1, fn.line, 1),
          selectionRange: new monaco.Range(fn.line, 1, fn.line, 1)
        });
      }
      for (const entry of analysis.enums || []) {
        result.push({
          name: entry.name,
          detail: 'ENUM',
          kind: monaco.languages.SymbolKind.Enum,
          range: new monaco.Range(entry.line, 1, entry.endLine || entry.line, 1),
          selectionRange: new monaco.Range(entry.line, 1, entry.line, 1),
          children: (entry.values || []).map((value) => ({
            name: `${entry.name}.${value.name}`,
            detail: 'ENUM',
            kind: monaco.languages.SymbolKind.Constant,
            range: new monaco.Range(value.line, 1, value.line, 1),
            selectionRange: new monaco.Range(value.line, 1, value.line, 1)
          }))
        });
      }
      for (const c of analysis.constants) {
        if (c.enum) continue;
        result.push({
          name: c.name,
          detail: 'CONSTANT',
          kind: monaco.languages.SymbolKind.Constant,
          range: new monaco.Range(c.line, 1, c.line, 1),
          selectionRange: new monaco.Range(c.line, 1, c.line, 1)
        });
      }
      return result;
    }
  });

  languages.registerFoldingRangeProvider('gbx', {
    provideFoldingRanges(model) {
      const ranges = [];
      const stack = [];
      const lines = model.getValue().split(/\r?\n/);
      lines.forEach((raw, index) => {
        const line = raw.replace(/\/\/.*$/, '').trim();
        if (!line) return;
        const first = /^([A-Za-z_]\w*)/.exec(line)?.[1]?.toUpperCase();
        if (!first) return;
        if (first === 'IF' && /\bTHEN\b/i.test(line)) return;
        if (Object.hasOwn(OPEN_BLOCKS, first)) {
          stack.push({ kind: first, line: index + 1 });
        } else if (CLOSERS.has(first)) {
          for (let i = stack.length - 1; i >= 0; i--) {
            if (OPEN_BLOCKS[stack[i].kind] === first) {
              const open = stack.splice(i, 1)[0];
              if (index + 1 > open.line) {
                ranges.push({ start: open.line, end: index + 1, kind: 'region' });
              }
              break;
            }
          }
        }
      });
      return ranges;
    }
  });

  /** Findet einen Fehler der Zeile – auch wenn der Cursor nicht genau darin
      steht, damit die Glühbirne am Marker erscheint. */
  function findLineError(model, lineNumber, column) {
    if (!Number.isFinite(lineNumber) || lineNumber < 1 || lineNumber > model.getLineCount()) return null;
    const text = model.getLineContent(lineNumber);
    const columns = [column, 1];
    for (const part of splitStatements(text)) {
      columns.push(part.start + 1);
      // Auch das Ende der Anweisung prüfen, damit der Fix hinter THEN und
      // nach einem Doppelpunkt auch bei Cursor am Zeilenanfang erscheint.
      columns.push(part.start + text.slice(part.start, part.end).length + 1);
    }
    const seen = new Set();
    for (const caret of columns) {
      if (seen.has(caret)) continue;
      seen.add(caret);
      const result = checkLine(text, { byName, caretColumn: caret });
      if (result && result.status === 'error') return result;
    }
    return null;
  }

  // Quickfixes für die Live-Prüfung: Klammern ergänzen/entfernen und
  // überzählige Argumente bei Befehlen ohne Parameter streichen.
  if (typeof languages.registerCodeActionProvider === 'function') {
    // Monaco 0.52 exportiert CodeActionKind nicht über die API; der Wert ist stabil.
    const quickFix = 'quickfix';
    languages.registerCodeActionProvider(
      'gbx',
      {
        provideCodeActions(model, range) {
          const error = findLineError(model, range.startLineNumber, range.startColumn);
          if (!error?.offsets) return { actions: [], dispose() {} };
          const line = range.startLineNumber;
          const offsets = error.offsets;
          const span = (start, end) => new monaco.Range(line, start + 1, line, end + 1);
          const workspaceEdit = (edits) => ({
            edits: edits.map((edit) => ({ resource: model.uri, textEdit: edit, versionId: model.getVersionId() }))
          });
          const actions = [];
          const add = (title, edits, preferred = false) =>
            actions.push({ title, kind: quickFix, isPreferred: preferred, edit: workspaceEdit(edits) });

          if (error.code === 'parens-required') {
            if (offsets.argsStart > offsets.nameEnd) {
              add(
                t('Klammern ergänzen'),
                [
                  { range: span(offsets.nameEnd, offsets.argsStart), text: '(' },
                  { range: span(offsets.statementEnd, offsets.statementEnd), text: ')' }
                ],
                true
              );
            } else {
              add(t('Klammern ergänzen'), [{ range: span(offsets.nameEnd, offsets.nameEnd), text: '()' }], true);
            }
          } else if (error.code === 'parens-forbidden' && offsets.openParen >= 0) {
            // `SPRITE_DRAW(logo…)` braucht nach dem Entfernen ein Leerzeichen.
            const next = model.getLineContent(line)[offsets.openParen + 1] || '';
            const gap = next && !/[\s)]/.test(next) ? ' ' : '';
            const edits = [{ range: span(offsets.openParen, offsets.openParen + 1), text: gap }];
            if (offsets.closeParen >= 0) edits.push({ range: span(offsets.closeParen, offsets.closeParen + 1), text: '' });
            add(t('Klammern entfernen'), edits, true);
          } else if (error.code === 'no-arguments') {
            add(t('Argumente entfernen'), [{ range: span(offsets.nameEnd, offsets.statementEnd), text: '' }]);
          }
          return { actions, dispose() {} };
        }
      },
      { providedCodeActionKinds: [quickFix] }
    );
  }

  // Neue Liste aus dem Projekt: Namen farben, Vorschlaege und Hover folgen
  // sofort, ohne dass die Sprache neu aufgebaut werden muss.
  function setInlineCommands(list) {
    const next = (Array.isArray(list) ? list : [])
      .map((entry) => ({
        name: String(entry?.name || '').trim().toUpperCase(),
        min: Number(entry?.min) || 0,
        max: entry?.max === undefined ? -1 : Number(entry.max),
        file: String(entry?.file || ''),
        line: Number(entry?.line) || 0
      }))
      .filter((entry) => /^[A-Z_][A-Z0-9_$%#]*$/.test(entry.name))
      .sort((a, b) => a.name.localeCompare(b.name));
    const changed =
      next.length !== inlineCommands.length || next.some((entry, index) => entry.name !== inlineCommands[index]?.name);
    inlineCommands = next;
    if (changed) installTokenizer();
    return inlineCommands.length;
  }

  return { analyzeSource, byName, commands, setInlineCommands };
}
