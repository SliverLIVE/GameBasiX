// Anbindung an das Emscripten SDK (emsdk).
//
// Der HTML5-Build braucht einen wasm-Compiler. Statt ihn vorauszusetzen, lädt
// die IDE das SDK auf Knopfdruck in einen Ordner neben der Editor-EXE. Alles
// Weitere - Erkennen, Version, Aufruf von emcc - steckt in dieser Datei.
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';

import { resolveTool, pathExists } from './gbx.mjs';
import { tr } from './locale.mjs';

/** Standardordner des SDK, relativ zur Editor-EXE. */
export function defaultEmsdkPath() {
  return 'emsdk';
}

/** Konfigurierter Wert (relativ oder absolut) als Ordnerpfad. */
export function emsdkRoot(candidate, appRoot) {
  const value = String(candidate || '').trim().replace(/^"|"$/g, '') || defaultEmsdkPath();
  return resolveTool(value, appRoot);
}

/** Erstes Verzeichnis, das auf das Muster passt (z.B. emsdk/node/*\/bin). */
function firstMatch(dir, pattern) {
  try {
    const entries = fs.readdirSync(dir, { withFileTypes: true }).filter((entry) => entry.isDirectory());
    for (const entry of entries.sort((a, b) => b.name.localeCompare(a.name, 'en', { numeric: true }))) {
      if (pattern.test(entry.name)) return path.join(dir, entry.name);
    }
  } catch {
    /* Ordner fehlt */
  }
  return '';
}

/** PATH-Erweiterung, damit emcc Python und Node aus dem SDK findet. */
export function emsdkEnv(root) {
  const bins = [path.join(root, 'upstream', 'emscripten')];
  const python = firstMatch(path.join(root, 'python'), /^3\./);
  if (python) bins.push(python);
  const nodeBase = firstMatch(path.join(root, 'node'), /^\d/);
  if (nodeBase) bins.push(path.join(nodeBase, 'bin'), nodeBase);
  const env = {
    ...process.env,
    EMSDK: root,
    EM_CONFIG: path.join(root, '.emscripten'),
    EMSDK_PYTHON: python ? path.join(python, process.platform === 'win32' ? 'python.exe' : 'bin/python3') : ''
  };
  if (!env.EMSDK_PYTHON) delete env.EMSDK_PYTHON;
  env.PATH = [...bins, process.env.PATH || ''].filter(Boolean).join(path.delimiter);
  return env;
}

/** Aufruf eines emcc-Treibers (Windows: *.exe, sonst emcc/em++). */
function driver(root, base) {
  const dir = path.join(root, 'upstream', 'emscripten');
  const candidates =
    process.platform === 'win32'
      ? [`${base}.exe`, `${base}.bat`, `${base}.cmd`, `${base}.py`, base]
      : [base, `${base}.py`];
  for (const name of candidates) {
    const file = path.join(dir, name);
    if (!pathExists(file)) continue;
    if (file.endsWith('.py')) return { cmd: emsdkEnv(root).EMSDK_PYTHON || 'python', args: [file], root };
    return { cmd: file, args: [], root };
  }
  return null;
}

/** C-Treiber: emcc */
export function emccCommand(root) {
  return driver(root, 'emcc');
}

/** C++-Treiber: em++ (linkt die C++-Laufzeit) */
export function emxxCommand(root) {
  return driver(root, 'em++');
}

/** Emscripten-Version aus `emcc --version`. */
function readVersion(root) {
  const emcc = emccCommand(root);
  if (!emcc) return '';
  try {
    const result = spawnSync(emcc.cmd, [...emcc.args, '--version'], {
      encoding: 'utf8',
      timeout: 30000,
      env: emsdkEnv(root),
      windowsHide: true
    });
    const text = `${result.stdout || ''}${result.stderr || ''}`;
    const match = /(\d+\.\d+\.\d+)/.exec(text);
    return match ? match[1] : '';
  } catch {
    return '';
  }
}

/** Zustand des SDK für die IDE. */
export function inspectEmsdk(candidate, appRoot) {
  const value = String(candidate || '').trim().replace(/^"|"$/g, '') || defaultEmsdkPath();
  const root = resolveTool(value, appRoot);
  const installed = pathExists(path.join(root, 'emsdk.py'));
  const emcc = emccCommand(root);
  const version = emcc ? readVersion(root) : '';
  const ok = Boolean(version);
  return {
    path: value,
    resolved: root,
    ok,
    installed,
    version,
    emcc: emcc ? emcc.cmd : '',
    error: ok
      ? ''
      : installed
        ? tr('emsdk ist vorhanden, aber emcc läuft noch nicht (install/activate abgeschlossen?)')
        : tr(`emsdk wurde nicht gefunden: ${root}`)
  };
}

// ---------------------------------------------------------------- Installation

function pythonCommand() {
  return process.platform === 'win32' ? 'python' : 'python3';
}

/** Ein Kommando starten und seine Ausgabe Zeile für Zeile melden. */
function runStream(cmd, args, { cwd, env, onLog, timeoutMs = 0 }) {
  return new Promise((resolve) => {
    let child;
    try {
      child = spawn(cmd, args, { cwd, env });
    } catch (error) {
      resolve({ code: -1, error: String(error?.message || error) });
      return;
    }
    let buffer = '';
    let timer = null;
    const emit = (line) => {
      const text = line.replace(/\s+$/, '');
      if (text) onLog?.(text);
    };
    const handle = (chunk) => {
      buffer += chunk.toString();
      let index;
      while ((index = buffer.search(/\r\n|\n|\r/)) >= 0) {
        const line = buffer.slice(0, index);
        buffer = buffer.slice(index + (buffer[index] === '\r' && buffer[index + 1] === '\n' ? 2 : 1));
        emit(line);
      }
    };
    child.stdout?.on('data', handle);
    child.stderr?.on('data', handle);
    if (timeoutMs > 0) timer = setTimeout(() => child.kill(), timeoutMs);
    child.on('error', (error) => {
      if (timer) clearTimeout(timer);
      resolve({ code: -1, error: String(error?.message || error) });
    });
    child.on('close', (code) => {
      if (timer) clearTimeout(timer);
      if (buffer.trim()) emit(buffer);
      resolve({ code: code ?? -1 });
    });
  });
}

/**
 * SDK in `root` einrichten: Repository holen, installieren, aktivieren.
 * Meldet jede Zeile über onLog; gibt false zurück, wenn ein Schritt scheitert.
 */
export async function installEmsdk({ root, onLog, onChild }) {
  await fsp.mkdir(root, { recursive: true });

  if (!pathExists(path.join(root, 'emsdk.py'))) {
    onLog?.('emsdk wird heruntergeladen (git clone) …');
    const clone = await runStream(
      'git',
      ['clone', '--depth', '1', 'https://github.com/emscripten-core/emsdk.git', root],
      { onLog, timeoutMs: 10 * 60 * 1000 }
    );
    if (clone.code !== 0) {
      onLog?.(`git clone ist fehlgeschlagen${clone.error ? `: ${clone.error}` : ''}.`);
      return false;
    }
  } else {
    onLog?.('emsdk ist bereits vorhanden, Installation wird fortgesetzt.');
  }

  const script = path.join(root, 'emsdk.py');
  for (const step of ['install', 'activate']) {
    onLog?.(`emsdk ${step} latest …`);
    const result = await runStream(pythonCommand(), [script, step, 'latest'], {
      cwd: root,
      env: { ...process.env, EMSDK: root },
      onLog
    });
    if (result.code !== 0) {
      onLog?.(`emsdk ${step} ist fehlgeschlagen${result.error ? `: ${result.error}` : ''}.`);
      return false;
    }
  }

  onLog?.('emsdk ist eingerichtet.');
  return true;
}
