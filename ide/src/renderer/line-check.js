import { t } from './i18n.js';

/* Prüfung der aktuellen Zeile gegen den GBX-Befehlssatz.

   Die Funktionen hier sind ohne Monaco und ohne DOM nutzbar: Sie bekommen den
   Zeilentext und die Befehlsliste und liefern Signatur, aktives Argument und
   – wenn die Zeile nicht stimmt – eine Meldung für Marker und Signaturleiste. */

/** Schneidet einen `'`- oder `//`-Kommentar ab, ohne Strings anzufassen. */
export function stripLineComment(text) {
  const line = String(text ?? '');
  let inString = false;
  for (let i = 0; i < line.length; i++) {
    const ch = line[i];
    if (ch === '"') {
      inString = !inString;
    } else if (!inString && ch === "'") {
      return line.slice(0, i);
    } else if (!inString && ch === '/' && line[i + 1] === '/') {
      return line.slice(0, i);
    }
  }
  return line;
}

/** Zerlegt eine Zeile in Anweisungen – `:` trennt mehrere Befehle. */
export function splitStatements(code) {
  const text = String(code ?? '');
  const parts = [];
  let depth = 0;
  let inString = false;
  let start = 0;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (ch === '"') {
      inString = !inString;
      continue;
    }
    if (inString) continue;
    if (ch === '(' || ch === '[') depth++;
    else if (ch === ')' || ch === ']') depth = Math.max(0, depth - 1);
    else if (ch === ':' && depth === 0) {
      parts.push({ start, end: i });
      start = i + 1;
    }
  }
  parts.push({ start, end: text.length });
  return parts;
}

/** Nur die erste Anweisung einer Zeile. */
export function truncateStatement(code) {
  return String(code ?? '').slice(0, splitStatements(code)[0].end);
}

/** Index direkt hinter dem THEN einer IF-Zeile, sonst -1. */
export function indexAfterThen(statement) {
  const text = String(statement ?? '');
  let depth = 0;
  let inString = false;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (ch === '"') {
      inString = !inString;
      continue;
    }
    if (inString) continue;
    if (ch === '(' || ch === '[') depth++;
    else if (ch === ')' || ch === ']') depth = Math.max(0, depth - 1);
    else if (depth === 0 && /^THEN\b/i.test(text.slice(i))) {
      if (i === 0 || !/[A-Za-z0-9_$%#]/.test(text[i - 1])) return i + 4;
    }
  }
  return -1;
}

/**
 * Ob ein Befehl mit Klammern aufgerufen wird, steht im Datensatz:
 * Funktionen liefern einen Wert (`returnCode` außer `v`/`-`) und bekommen
 * Klammern, Befehle nicht. ASSERT ist die Ausnahme mit Klammern.
 */
export function needsParentheses(command) {
  if (!command) return false;
  const name = String(command.displayName || command.name || '');
  const signature = String(command.signature || '');
  if (name) {
    const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    if (new RegExp(`\\b${escaped}\\s*\\(`, 'i').test(signature)) return true;
  }
  const code = String(command.returnCode || '').trim().toLowerCase();
  return code !== '' && code !== 'v' && code !== '-';
}

function findClosing(code, openIndex) {
  let depth = 0;
  let inString = false;
  for (let i = openIndex; i < code.length; i++) {
    const ch = code[i];
    if (ch === '"') {
      inString = !inString;
      continue;
    }
    if (inString) continue;
    if (ch === '(') depth++;
    else if (ch === ')') {
      depth--;
      if (depth === 0) return i;
    }
  }
  return -1;
}

/** Zerlegt eine Argumentliste an Kommas der obersten Ebene. */
export function splitArguments(text, baseOffset = 0) {
  const source = String(text ?? '');
  const args = [];
  let depth = 0;
  let inString = false;
  let start = 0;
  const push = (end) => {
    const slice = source.slice(start, end);
    args.push({
      text: slice,
      start: baseOffset + start,
      end: baseOffset + end,
      empty: slice.trim() === ''
    });
  };
  for (let i = 0; i < source.length; i++) {
    const ch = source[i];
    if (ch === '"') {
      inString = !inString;
      continue;
    }
    if (inString) continue;
    if (ch === '(' || ch === '[') depth++;
    else if (ch === ')' || ch === ']') depth = Math.max(0, depth - 1);
    else if (ch === ',' && depth === 0) {
      push(i);
      start = i + 1;
    }
  }
  push(source.length);
  if (args.length === 1 && args[0].empty && source.trim() === '') return [];
  return args;
}

function commasBefore(code, fromIndex, toIndex) {
  let depth = 0;
  let inString = false;
  let count = 0;
  const end = Math.min(Math.max(0, toIndex), code.length);
  for (let i = fromIndex; i < end; i++) {
    const ch = code[i];
    if (ch === '"') {
      inString = !inString;
      continue;
    }
    if (inString) continue;
    if (ch === '(' || ch === '[') depth++;
    else if (ch === ')' || ch === ']') depth = Math.max(0, depth - 1);
    else if (ch === ',' && depth === 0) count++;
  }
  return count;
}

function tooFewMessage(name, required, found) {
  return required === 1
    ? t('{name} erwartet 1 Argument, {found} angegeben.', { name, found })
    : t('{name} erwartet {count} Argumente, {found} angegeben.', { name, count: required, found });
}

function tooManyMessage(name, max, found) {
  return max === 1
    ? t('{name} erlaubt höchstens 1 Argument, {found} angegeben.', { name, found })
    : t('{name} erlaubt höchstens {count} Argumente, {found} angegeben.', { name, count: max, found });
}

const NONE = { status: 'none' };

/* Manche Befehle nennen in prototypen.txt nur den Typ ihrer Argumente
   (`MOUSE_STATE` → vier Mal `Integer (BYREF)`); die Namen stehen allein in der
   Signatur (`MOUSE_STATE mx##, my##, mba##, mbb##`). */
const GENERIC_LABEL = /\((?:BYREF|REF)\)$|^(?:beliebig|optional|number|string|any|array)(?:\s*\(.*\))?$/i;

function escapeRegExp(text) {
  return String(text).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/** Argumentnamen aus der Signatur eines Befehls – leer, wenn dort keine stehen. */
export function labelsFromSignature(command) {
  const signature = String(command?.signature || '').replace(/\r/g, '').split('\n')[0];
  const name = String(command?.name || '');
  if (!signature || !name) return [];
  // Der Name darf auch hinter einer Zuweisung stehen (`len%= LEN(word$)`),
  // und sein Typsuffix darf dort fehlen oder zusätzlich stehen
  // (`URLDECODE$` → `URLDECODE(...)`, `NETWEBGET` → `NETWEBGET$(...)`).
  const base = escapeRegExp(name.replace(/[$%#]$/, ''));
  const match = new RegExp(`(^|[^A-Za-z0-9_$%#])${base}[$%#]?(?![A-Za-z0-9_$%#])`, 'i').exec(signature);
  if (!match) return [];
  let rest = signature.slice(match.index + match[0].length).trim();
  if (rest.startsWith('(')) {
    let depth = 0;
    let end = -1;
    for (let i = 0; i < rest.length; i++) {
      if (rest[i] === '(') depth++;
      else if (rest[i] === ')') {
        depth--;
        if (!depth) {
          end = i;
          break;
        }
      }
    }
    rest = end > 0 ? rest.slice(1, end) : rest.slice(1);
  }
  if (!rest || !/[A-Za-z_$%#]/.test(rest)) return [];
  const labels = [];
  let depth = 0;
  let current = '';
  for (const ch of rest) {
    // Nur runde Klammern halten Kommas zusammen; `[, scale#=1]` beschreibt
    // trotz der eckigen Klammern ein eigenes Argument.
    if (ch === '(') depth++;
    else if (ch === ')') depth = Math.max(0, depth - 1);
    if (ch === ',' && !depth) {
      labels.push(current);
      current = '';
      continue;
    }
    current += ch;
  }
  labels.push(current);
  // Eckige Klammern markieren optionale Argumente (`[, scale#=1]`, `[d1#]`).
  return labels
    .map((label) =>
      label
        .replace(/[[\]]/g, ' ')
        .replace(/\bBYREF\b/gi, ' ')
        .replace(/=.*$/, ' ')
        .replace(/[\s,;]+/g, ' ')
        .trim()
    )
    .filter(Boolean);
}

/**
 * Beschriftung der Argumente für Signaturleiste, Ghost und Signaturhilfe.
 * Typlabels aus den Prototypen werden durch die Namen aus der Signatur
 * ersetzt, sobald beide dieselbe Anzahl Argumente nennen.
 */
export function commandParamLabels(command) {
  const params = Array.isArray(command?.params) ? command.params : [];
  const labels = params.map((param) => param?.label || param?.type || '?');
  if (!params.length) return labels;
  const names = labelsFromSignature(command);
  if (names.length !== params.length) return labels;
  return labels.map((label, index) =>
    GENERIC_LABEL.test(String(label)) && names[index] ? names[index] : label
  );
}

/**
 * Verschiebt gemerkte Zeilenfehler, wenn oberhalb Zeilen eingefügt oder
 * gelöscht werden. Fehler in geänderten Zeilen fallen weg und werden dort
 * neu geprüft.
 */
export function shiftLineNumbers(errors, changes) {
  let current = errors;
  for (const change of changes || []) {
    const range = change.range || change;
    const start = Number(range.startLineNumber) || 1;
    const end = Number(range.endLineNumber) || start;
    const added = String(change.text ?? '').split(/\r?\n/).length - 1;
    const delta = added - (end - start);
    if (!delta && start === end) {
      // Änderung innerhalb einer Zeile: nur diese Zeile neu prüfen.
      current = new Map([...current].filter(([line]) => line !== start));
      continue;
    }
    const next = new Map();
    for (const [line, info] of current) {
      if (line < start) next.set(line, info);
      else if (line > end) next.set(line + delta, info);
    }
    current = next;
  }
  return current;
}

/**
 * Prüft eine Zeile. `caretColumn` ist 1-basiert (Monaco-Spalte) und bestimmt,
 * welches Argument gerade geschrieben wird.
 */
export function checkLine(text, { byName, caretColumn = 0 } = {}) {
  const raw = String(text ?? '');
  const code = stripLineComment(raw).replace(/\s+$/, '');
  if (!code.trim()) return NONE;

  // Die Anweisung, in der der Cursor steht – nicht die ganze Zeile.
  const caret = Number(caretColumn) || 0;
  const caretIndex = Math.max(0, Math.min(caret - 1, code.length));
  const segments = splitStatements(code);
  const segment = segments.find((part) => caretIndex >= part.start && caretIndex <= part.end) || segments[segments.length - 1];
  const statementEnd = segment.end;

  let offset = segment.start;
  while (offset < statementEnd && /\s/.test(code[offset])) offset++;
  let statement = code.slice(offset, statementEnd).replace(/\s+$/, '');
  if (!statement) return NONE;

  // Präprozessor und Labels sind keine Befehlsaufrufe.
  if (statement.startsWith('?') || /^[A-Za-z_]\w*[$%#]?\s*:\s*$/.test(statement)) return NONE;

  // `IF … THEN befehl`: geprüft wird der Befehl hinter THEN; im Bedingungsteil
  // steht keine Anweisung, die sich prüfen lässt.
  if (/^(IF|ELSEIF)\b/i.test(statement)) {
    const thenAt = indexAfterThen(statement);
    if (thenAt < 0) return NONE;
    if (caretIndex < offset + thenAt) return NONE;
    const rest = statement.slice(thenAt);
    const lead = rest.length - rest.trimStart().length;
    offset += thenAt + lead;
    statement = rest.trimStart();
    if (!statement) return NONE;
  } else if (/^ELSE\b/i.test(statement)) {
    const elseMatch = /^ELSE\b/i.exec(statement);
    if (caretIndex < offset + elseMatch[0].length) return NONE;
    const rest = statement.slice(elseMatch[0].length);
    const lead = rest.length - rest.trimStart().length;
    offset += elseMatch[0].length + lead;
    statement = rest.trimStart();
    if (!statement) return NONE;
  }

  // Zuweisung: die rechte Seite kann einen Befehl enthalten (n# = DELTA_TIME()).
  const assignment = /^[A-Za-z_]\w*[$%#]?\s*=\s*/.exec(statement);
  if (assignment) {
    offset += assignment[0].length;
    statement = code.slice(offset, statementEnd);
    if (!statement.trim()) return NONE;
  }

  const nameMatch = /^([A-Za-z_]\w*[$%#]?)/.exec(statement);
  if (!nameMatch) return NONE;
  const name = nameMatch[1];
  const nameStart = offset;
  const command = byName?.get(name.toUpperCase());
  if (!command) return NONE;
  if (command.kind !== 'command' && command.kind !== 'optional') return NONE;

  const params = Array.isArray(command.params) ? command.params : [];
  const afterName = nameStart + name.length;
  let cursorIndex = afterName;
  while (cursorIndex < statementEnd && /\s/.test(code[cursorIndex])) cursorIndex++;

  const parensRequired = needsParentheses(command);
  // Befehle dürfen Klammern nur als Gruppierung eines Ausdrucks tragen:
  // `PRINT ("x"), 0, 0` ist gültig, `PRINT("x")` nicht. Funktionen bekommen
  // ihre Klammern mit oder ohne Leerzeichen davor.
  const spaced = cursorIndex > afterName;
  const parenCall = code[cursorIndex] === '(' && (parensRequired || !spaced);
  let openParen = -1;
  let closeParen = -1;
  let argsStart = cursorIndex;
  let argsEnd = statementEnd;
  if (parenCall) {
    openParen = cursorIndex;
    closeParen = findClosing(code, openParen);
    argsStart = openParen + 1;
    argsEnd = closeParen >= 0 ? closeParen : statementEnd;
  }

  const args = splitArguments(code.slice(argsStart, argsEnd), argsStart);
  const filled = args.filter((arg) => !arg.empty);
  const found = filled.length;
  const required = params.filter((param) => !param.optional).length;
  const max = params.length;
  const emptyArg = args.some((arg) => arg.empty);

  const activeIndex = Math.max(
    0,
    Math.min(commasBefore(code, argsStart, caretIndex), Math.max(0, max - 1))
  );

  const labels = commandParamLabels(command);
  const callSignature = `${command.name || name}(${labels.join(', ')})`;
  const signature = params.length ? labels.join(', ') : command.name;
  const base = {
    status: 'ok',
    name: command.name || name,
    // Stabile Kennung für Quickfixes; die Meldung bleibt übersetzbar.
    code: '',
    // 0-basierte Positionen in der Zeile für Text-Edits.
    offsets: { nameStart, nameEnd: afterName, statementEnd, argsStart, argsEnd, openParen, closeParen },
    params: labels.map((label, index) => ({ label, optional: !!params[index].optional })),
    signature,
    parens: parensRequired,
    args: args.map((arg) => arg.text.trim()),
    activeIndex,
    required,
    max,
    found,
    message: '',
    range: {
      startColumn: nameStart + 1,
      endColumn: Math.max(nameStart + name.length, (closeParen >= 0 ? closeParen + 1 : statementEnd)) + 1
    }
  };

  let message = '';
  let errorCode = '';
  if (parensRequired && !parenCall) {
    errorCode = 'parens-required';
    message = t('{name} wird mit Klammern aufgerufen: {call}', { name: base.name, call: callSignature });
  } else if (!parensRequired && parenCall) {
    errorCode = 'parens-forbidden';
    message = t('{name} wird ohne Klammern aufgerufen.', { name: base.name });
  } else if (emptyArg) {
    errorCode = 'empty-argument';
    message = t('Nach dem Komma fehlt ein Argument.');
  } else if (max === 0 && found > 0) {
    errorCode = 'no-arguments';
    message = t('{name} erwartet keine Argumente.', { name: base.name });
  } else if (found > max) {
    errorCode = 'too-many-arguments';
    message = tooManyMessage(base.name, max, found);
  } else if (found < required) {
    errorCode = 'too-few-arguments';
    message = tooFewMessage(base.name, required, found);
  }

  return {
    ...base,
    status: message ? 'error' : 'ok',
    code: errorCode,
    message,
    // Befehle ohne Argumente brauchen keine Leiste, außer es ist ein Fehler.
    show: message ? true : params.length > 0
  };
}
