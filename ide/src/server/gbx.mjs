// Anbindung an den GBX-Interpreter (gbx.exe).
//
// Es gibt keinen Zwischencode: gbx liest die
// Quelldateien bei jedem Start neu. „Bauen“ bedeutet hier deshalb
// Syntaxprüfung (gbx --check), „Starten“ führt das Projekt aus.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn, spawnSync } from 'node:child_process';

import { language, tr } from './locale.mjs';

/** Ordner der IDE (<paket>/ide); eine Ebene höher liegt GBXStudio.exe. */
export const APP_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');

/** Der Interpreter bekommt die Oberflächensprache über die Umgebung. */
let compilerPath = '';

/** Vom Server aus der Konfiguration gesetzt; leer = automatische Suche. */
export function setCompilerPath(value) {
  compilerPath = String(value || '').trim().replace(/^"|"$/g, '');
}

function gbxEnv() {
  const env = { ...process.env, GBX_LANG: language() };
  if (compilerPath) env.GBX_CXX = compilerPath;
  return env;
}

/** Ordner mit GBXStudio.exe – Bezugspunkt für relative Pfade. */
export function editorRoot(appRoot = APP_ROOT) {
  return path.resolve(appRoot, '..');
}

/**
 * Pfad zu Interpreter oder Compiler auflösen. Relative Angaben gelten ab dem
 * Ordner der Editor-EXE, damit ein verschobenes Paket weiter funktioniert.
 */
export function resolveTool(value, appRoot = APP_ROOT) {
  const text = String(value || '').trim().replace(/^"|"$/g, '');
  if (!text) return '';
  return path.isAbsolute(text) ? path.normalize(text) : path.resolve(editorRoot(appRoot), text);
}

/** Standardpfad, relativ zum Ordner der Editor-EXE: build\\gbx.exe */
export function defaultGbxPath() {
  return path.join('build', 'gbx.exe');
}

export function pathExists(p) {
  try {
    fs.accessSync(p);
    return true;
  } catch {
    return false;
  }
}

export function resolveGbx(candidate, appRoot = APP_ROOT) {
  const value = String(candidate || '').trim().replace(/^"|"$/g, '');
  return resolveTool(value || defaultGbxPath(), appRoot);
}

/** Der mitgelieferte Compiler liegt neben der IDE: compiler\\bin\\g++.exe */
export function defaultCxxPath() {
  return path.join('compiler', 'bin', 'g++.exe');
}

const compilerVersions = new Map();

function readCompilerVersion(file) {
  if (compilerVersions.has(file)) return compilerVersions.get(file);
  let version = '';
  try {
    const result = spawnSync(file, ['--version'], { encoding: 'utf8', timeout: 8000, windowsHide: true });
    const match = /(\d+\.\d+\.\d+)/.exec(`${result.stdout || ''}${result.stderr || ''}`);
    version = match ? match[1] : '';
  } catch {
    version = '';
  }
  compilerVersions.set(file, version);
  return version;
}

/** Zustand des C++-Compilers für INLINE. */
export function inspectCompiler(candidate, appRoot = APP_ROOT) {
  const bundled = resolveTool(defaultCxxPath(), appRoot);
  const configured = String(candidate || '').trim().replace(/^"|"$/g, '');
  const value = configured || defaultCxxPath();
  const resolved = resolveTool(value, appRoot);
  const ok = Boolean(resolved) && pathExists(resolved);
  return {
    path: value,
    resolved,
    ok,
    version: ok ? readCompilerVersion(resolved) : '',
    bundled: pathExists(bundled),
    // Nur unter Windows laesst sich der WinLibs-Build einfach ablegen.
    canDownload: process.platform === 'win32',
    error: ok ? '' : tr(`g++ wurde nicht gefunden: ${resolved}`)
  };
}

let cachedVersion = '';

export function inspectGbx(candidate, appRoot = APP_ROOT) {
  const configured = String(candidate || '').trim().replace(/^"|"$/g, '');
  const value = configured || defaultGbxPath();
  const resolved = resolveTool(value, appRoot);
  const ok = pathExists(resolved);
  return {
    path: value,
    resolved,
    ok,
    version: ok ? readVersion(resolved) : '',
    error: ok ? '' : tr(`gbx.exe wurde nicht gefunden: ${resolved}`)
  };
}

function readVersion(gbx) {
  if (cachedVersion) return cachedVersion;
  try {
    const result = spawnSync(gbx, ['--version'], { encoding: 'utf8', timeout: 8000, windowsHide: true });
    const text = `${result.stdout || ''}${result.stderr || ''}`;
    const match = /GameBasiX\s+([\d.]+)/i.exec(text);
    cachedVersion = match ? match[1] : '';
  } catch {
    cachedVersion = '';
  }
  return cachedVersion;
}

function decodeSource(buffer) {
  try {
    return new TextDecoder('utf-8', { fatal: true }).decode(buffer);
  } catch {
    return buffer.toString('latin1');
  }
}

function sourceLineAt(file, line) {
  try {
    const text = decodeSource(fs.readFileSync(file));
    return String(text.replace(/\r\n?/g, '\n').split('\n')[line - 1] ?? '').replace(/\s+$/, '');
  } catch {
    return '';
  }
}

/**
 * Sucht den in einer Meldung genannten Namen in der Zeile, damit die IDE
 * genau den Namen anstreichen kann statt der ganzen Zeile.
 */
export function locateNamedToken(message, context) {
  if (!context) return null;
  const match =
    /\b(?:Befehl|Funktion|Name|Marke|Unterprogramm|Feld|Methode|command|function|name|label|subroutine|field|method):\s*([A-Za-z_][\w$%#]*)/i.exec(
      String(message || '')
    );
  if (!match) return null;
  const index = String(context).toUpperCase().indexOf(match[1].toUpperCase());
  return index < 0 ? null : { column: index + 1, tokenLength: match[1].length };
}

/**
 * gbx meldet Fehler als `datei:zeile: Syntaxfehler: text` (stderr).
 * Diese Funktion macht daraus Diagnosen für die Problemliste.
 */
export function parseDiagnostics(text, projectDir) {
  const diagnostics = [];
  for (const raw of String(text || '').replace(/\r\n?/g, '\n').split('\n')) {
    const line = raw.trim();
    if (!line) continue;
    let match =
      /^(.*?):(\d+):\s*(Syntaxfehler|Laufzeitfehler|Fehler|Syntax error|Runtime error|Error):\s*(.+)$/i.exec(
        line
      );
    if (!match) {
      match = /^(.*?):(\d+):\s*(.+)$/.exec(line);
      if (!match || !/error|fehler/i.test(line)) continue;
    }
    const file = path.isAbsolute(match[1]) ? match[1] : path.resolve(projectDir, match[1]);
    const lineNumber = Number(match[2]);
    const kind = (match[3] || '').toLowerCase();
    const message = tr((match[4] || '').trim());
    const context = sourceLineAt(file, lineNumber);
    const spot = locateNamedToken(message, context);
    diagnostics.push({
      file,
      line: lineNumber,
      column: spot ? spot.column : 1,
      tokenLength: spot ? spot.tokenLength : 0,
      severity: 'error',
      message: kind.includes('laufzeit') || kind.includes('runtime') ? tr(`Laufzeitfehler: ${message}`) : message,
      code: kind.includes('syntax') ? 'GBX0001' : '',
      context,
      source: 'gbx'
    });
  }
  const seen = new Set();
  return diagnostics.filter((d) => {
    const key = `${d.file}|${d.line}|${d.message}`;
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}

function runProcess(command, args, options, onOutput) {
  return new Promise((resolve) => {
    let child;
    try {
      child = spawn(command, args, {
        cwd: options.cwd,
        env: { ...process.env, ...(options.env || {}) },
        windowsHide: true,
        shell: false
      });
    } catch (error) {
      resolve({ code: -1, output: String(error?.message || error) });
      return;
    }
    let output = '';
    const emit = (chunk, stream) => {
      const text = Buffer.isBuffer(chunk) ? chunk.toString('utf8') : String(chunk);
      output += text;
      onOutput?.(text, stream);
    };
    child.stdout?.on('data', (c) => emit(c, 'stdout'));
    child.stderr?.on('data', (c) => emit(c, 'stderr'));
    child.on('error', (error) => {
      output += `\n${error.message}`;
      resolve({ code: -1, output });
    });
    child.on('close', (code) => resolve({ code: code ?? -1, output }));
    options.onChild?.(child);
  });
}

function splitLines(onLine) {
  let buffer = '';
  return (text) => {
    buffer += text.replace(/\r\n?/g, '\n');
    let index;
    while ((index = buffer.indexOf('\n')) >= 0) {
      const line = buffer.slice(0, index);
      buffer = buffer.slice(index + 1);
      if (line.trim()) onLine(line);
    }
  };
}

function entryPathOf(projectDir, project) {
  const entry = String(project?.entry || `main.gbx`).replace(/\\/g, '/');
  return path.resolve(projectDir, entry);
}

function classify(line) {
  if (/fehler|error/i.test(line)) return 'error';
  if (/warnung|warning/i.test(line)) return 'warning';
  return 'muted';
}

export async function checkProject({ gbx, projectDir, project, log, onChild }) {
  const info = inspectGbx(gbx);
  if (!info.ok) throw new Error(info.error);
  const entry = entryPathOf(projectDir, project);
  if (!pathExists(entry)) throw new Error(`Einstiegsdatei nicht gefunden: ${entry}`);
  const emit = (text, level = 'info') => log?.({ type: 'log', level, text: String(text) });

  emit(tr(`GameBasiX Syntaxprüfung · ${new Date().toLocaleString(language() === 'en' ? 'en-GB' : 'de-DE')}`), 'title');
  emit(`Interpreter: ${info.path}`);
  emit(tr('Einstieg: ') + (path.relative(projectDir, entry) || entry));
  emit('');

  const lines = splitLines((line) => emit(line, classify(line)));
  const result = await runProcess(
    info.resolved,
    ['--check', entry],
    { cwd: projectDir, env: gbxEnv(), onChild },
    lines
  );
  const diagnostics = parseDiagnostics(result.output, projectDir);

  if (result.code !== 0) {
    emit('');
    emit(tr('Die Syntaxprüfung hat Fehler gemeldet.'), 'error');
    for (const [index, d] of diagnostics.entries()) {
      const relative = path.relative(projectDir, d.file) || path.basename(d.file);
      emit(`  ${index + 1}. ${relative}:${d.line} ${d.message}`, 'error');
      if (d.context) emit(`     ${d.context.trim()}`, 'muted');
    }
    return { ok: false, phase: 'check', diagnostics, output: result.output, entryPath: entry };
  }

  if (!result.output.trim()) emit('Syntax OK.', 'success');
  emit('');
  emit(tr('Prüfung erfolgreich. F5 startet das Projekt.'), 'success');
  return { ok: true, phase: 'done', diagnostics, output: result.output, entryPath: entry };
}

/** Die .gbxproj im Projektordner: Sie traegt Fenstergroesse, Titel und FPS. */
function projectFileOf(projectDir) {
  try {
    const file = fs.readdirSync(projectDir).find((name) => /\.gbxproj$/i.test(name));
    if (file) return path.join(projectDir, file);
  } catch {
    /* Ordner nicht lesbar: dann bleibt die Quelldatei der Einstieg. */
  }
  return '';
}

/** Dateiname ohne Zeichen, die Windows nicht erlaubt. */
function safeFileStem(name) {
  const cleaned = String(name || '')
    .replace(/[<>:"/\\|?*\u0000-\u001f]/g, '_')
    .replace(/^[.\s]+|[.\s]+$/g, '');
  return cleaned || 'spiel';
}

/**
 * Packt das Projekt in eine einzelne EXE: Quellen und die uebersetzte
 * INLINE-Bibliothek stecken darin, die Assets kopiert gbx neben die EXE.
 */
export async function exportGame({ gbx, projectDir, project, target = '', onEvent, onChild }) {
  const info = inspectGbx(gbx);
  if (!info.ok) throw new Error(info.error);
  const entry = entryPathOf(projectDir, project);
  if (!pathExists(entry)) throw new Error(`Einstiegsdatei nicht gefunden: ${entry}`);

  const extension = process.platform === 'win32' ? '.exe' : '';
  const file = path.resolve(
    target || path.join(projectDir, `${safeFileStem(project?.name || path.basename(projectDir))}${extension}`)
  );
  // Ueber die Projektdatei starten, damit die Fenstereinstellungen mitkommen.
  const start = projectFileOf(projectDir) || entry;
  const emit = (text, level = 'info') => onEvent?.({ type: 'log', level, text: String(text) });

  emit(tr('GameBasiX Export · ') + new Date().toLocaleString(language() === 'en' ? 'en-GB' : 'de-DE'), 'title');
  emit(`Interpreter: ${info.path}`);
  emit(tr('Ziel: ') + file);
  emit('');

  const lines = splitLines((line) => emit(line, classify(line)));
  const result = await runProcess(
    info.resolved,
    ['--export', file, start],
    { cwd: projectDir, env: gbxEnv(), onChild },
    lines
  );

  if (result.code !== 0) {
    emit('');
    emit(tr('Der Export ist fehlgeschlagen.'), 'error');
    return { ok: false, file, code: result.code, output: result.output };
  }

  emit('');
  emit(tr('Fertig. Die EXE liegt bei den Assets und startet ohne Projektordner.'), 'success');
  return { ok: true, file, code: 0, output: result.output };
}

/**
 * Prueft ungespeicherten Quelltext aus dem Editor. Die Quelle geht ueber die
 * Standardeingabe an gbx (--stdin), Bezugsordner und Dateiname kommen mit,
 * damit INCLUDE und Meldungen zur echten Datei passen. So gibt es
 * Syntaxfehler schon beim Tippen, ohne dass die Datei gespeichert wird.
 */
export function checkSource({ gbx, projectDir, file, source, syntaxOnly = false, timeout = 5000 }) {
  const info = inspectGbx(gbx);
  if (!info.ok) return Promise.resolve({ available: false, ok: false, diagnostics: [], error: info.error });
  const target = path.resolve(projectDir, file);
  const dir = path.dirname(target);
  const name = path.basename(target);

  return new Promise((resolve) => {
    let child;
    try {
      const args = ['--check', '--stdin', '--base', dir, '--name', name];
      if (syntaxOnly) args.push('--syntax-only');
      child = spawn(info.resolved, args, {
        cwd: dir,
        windowsHide: true,
        env: gbxEnv(),
        stdio: ['pipe', 'pipe', 'pipe'],
        shell: false
      });
    } catch (error) {
      resolve({ available: false, ok: false, diagnostics: [], error: String(error?.message || error) });
      return;
    }

    let output = '';
    let finished = false;
    const finish = (code, error = '') => {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      const lines = String(source ?? '').replace(/\r\n?/g, '\n').split('\n');
      const diagnostics = parseDiagnostics(output, dir).map((d) => {
        // Der Editorinhalt ist maßgeblich, nicht die gespeicherte Fassung.
        const context = String(lines[d.line - 1] ?? '').replace(/\s+$/, '');
        const spot = locateNamedToken(d.message, context);
        return {
          ...d,
          file: target,
          context,
          column: spot ? spot.column : d.column,
          tokenLength: spot ? spot.tokenLength : d.tokenLength
        };
      });
      resolve({ available: true, ok: code === 0, diagnostics, error });
    };
    const timer = setTimeout(() => {
      try {
        child.kill();
      } catch {
        /* Kind ist schon weg. */
      }
      finish(-1, tr('Zeitüberschreitung bei der Live-Prüfung'));
    }, timeout);

    child.stdout?.on('data', (chunk) => (output += Buffer.isBuffer(chunk) ? chunk.toString('utf8') : String(chunk)));
    child.stderr?.on('data', (chunk) => (output += Buffer.isBuffer(chunk) ? chunk.toString('utf8') : String(chunk)));
    child.on('error', (error) => {
      output += `\n${error.message}`;
      finish(-1, error.message);
    });
    child.on('close', (code) => finish(code ?? -1));
    try {
      child.stdin.end(String(source ?? ''), 'utf8');
    } catch {
      /* Prozess hat die Eingabe schon geschlossen. */
    }
  });
}

/**
 * Startet den Interpreter mit Laufzeit-Debugger. Der Debugger meldet sich auf
 * stdout mit Zeilen der Form `@@gbxdbg {"t": ...}`; alles andere ist Ausgabe
 * des Programms. Befehle gehen zeilenweise ueber stdin.
 */
export function startDebugSession({ gbx, projectDir, project, breakpoints = [], headless = false, onEvent, onChild }) {
  const info = inspectGbx(gbx);
  if (!info.ok) throw new Error(info.error);
  const entry = entryPathOf(projectDir, project);
  if (!pathExists(entry)) throw new Error(`Einstiegsdatei nicht gefunden: ${entry}`);

  let child;
  try {
    child = spawn(info.resolved, headless ? ['--headless', '--debug', entry] : ['--debug', entry], {
      cwd: projectDir,
      windowsHide: false,
      env: gbxEnv(),
      stdio: ['pipe', 'pipe', 'pipe'],
      shell: false
    });
  } catch (error) {
    throw new Error(String(error?.message || error));
  }

  const send = (command) => {
    try {
      child.stdin.write(`${command}\n`);
    } catch {
      /* Sitzung ist bereits beendet. */
    }
  };

  // Haltepunkte zuerst, dann freigeben: der Interpreter wartet mit der ersten
  // Anweisung, bis "run" kommt. So haengt kein Haltepunkt am Timing.
  for (const bp of breakpoints) {
    const line = Number(bp?.line);
    if (!Number.isInteger(line) || line <= 0 || !bp?.file) continue;
    const target = path.resolve(projectDir, String(bp.file));
    if (!target.startsWith(path.resolve(projectDir))) continue;
    // Tabs und Zeilenumbrueche wuerden das Zeilenprotokoll zerlegen.
    const condition = String(bp?.condition || '').replace(/[\t\r\n]+/g, ' ').trim();
    send(condition ? `bp\t${target}\t${line}\t${condition}` : `bp\t${target}\t${line}`);
  }
  send('run');

  const prefix = '@@gbxdbg ';
  let buffer = '';
  child.stdout?.on('data', (chunk) => {
    buffer += Buffer.isBuffer(chunk) ? chunk.toString('utf8') : String(chunk);
    buffer = buffer.replace(/\r\n?/g, '\n');
    let index;
    while ((index = buffer.indexOf('\n')) >= 0) {
      const line = buffer.slice(0, index);
      buffer = buffer.slice(index + 1);
      if (!line.trim()) continue;
      if (line.startsWith(prefix)) {
        let event;
        try {
          event = JSON.parse(line.slice(prefix.length));
        } catch {
          continue;
        }
        onEvent({ type: 'debug', event });
      } else {
        onEvent({ type: 'log', level: 'stdout', text: line + '\n' });
      }
    }
  });
  child.stderr?.on('data', (chunk) => {
    const text = Buffer.isBuffer(chunk) ? chunk.toString('utf8') : String(chunk);
    onEvent({ type: 'log', level: 'stderr', text });
  });
  child.on('error', (error) => {
    onEvent({ type: 'log', level: 'error', text: error.message });
    onEvent({ type: 'debug-exit', code: -1 });
  });
  child.on('close', (code) => onEvent({ type: 'debug-exit', code: code ?? -1 }));
  onChild?.(child);
  onEvent({ type: 'debug', event: { t: 'attach', pid: child.pid } });
  return { child, send };
}

export function runProject({ gbx, projectDir, project, args = [], onEvent, onChild }) {
  const info = inspectGbx(gbx);
  if (!info.ok) throw new Error(info.error);
  const entry = entryPathOf(projectDir, project);
  if (!pathExists(entry)) throw new Error(`Einstiegsdatei nicht gefunden: ${entry}`);

  let child;
  try {
    child = spawn(info.resolved, [...args, entry], {
      cwd: projectDir,
      windowsHide: false,
      env: gbxEnv(),
      shell: false
    });
  } catch (error) {
    onEvent({ type: 'log', level: 'error', text: String(error?.message || error) });
    onEvent({ type: 'exit', code: -1 });
    return null;
  }
  onChild?.(child);
  const emit = (chunk, level) => {
    const text = Buffer.isBuffer(chunk) ? chunk.toString('utf8') : String(chunk);
    onEvent({ type: 'log', level, text });
  };
  child.stdout?.on('data', (c) => emit(c, 'stdout'));
  child.stderr?.on('data', (c) => emit(c, 'stderr'));
  child.on('error', (error) => {
    onEvent({ type: 'log', level: 'error', text: error.message });
    onEvent({ type: 'exit', code: -1 });
  });
  child.on('close', (code) => onEvent({ type: 'exit', code: code ?? -1 }));
  onEvent({ type: 'started', pid: child.pid });
  return child;
}
