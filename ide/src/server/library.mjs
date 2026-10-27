// C++-Bibliothek (.a) aus Projektquellen bauen.
//
// Bisher bekam die IDE nur fertige .a-Dateien zu sehen ([native] in der
// Projektdatei). Dieser Baustein macht daraus einen Weg: die gewählten Quellen
// mit g++ übersetzen, die Objektdateien mit ar zu einem Archiv bündeln und das
// Ergebnis dort ablegen, wo die Projektdatei es nur noch nennen muss.
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';

import { isInside } from './project.mjs';
import { tr } from './locale.mjs';

const SOURCE_RE = /\.(cpp|cc|cxx|c\+\+|c)$/i;
// Ordner, die nie Quellen für eine Bibliothek enthalten.
const IGNORED = new Set(['build', 'node_modules', 'html5']);

/** Alle C++-Quellen im Projekt (ohne Build-, Paket- und versteckte Ordner). */
export async function listLibrarySources(projectDir) {
  const root = path.resolve(projectDir);
  const out = [];
  async function walk(dir, rel) {
    let entries;
    try {
      entries = await fsp.readdir(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      if (entry.name.startsWith('.') || IGNORED.has(entry.name)) continue;
      const relative = rel ? `${rel}/${entry.name}` : entry.name;
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        await walk(full, relative);
        continue;
      }
      if (!SOURCE_RE.test(entry.name)) continue;
      let size = 0;
      try {
        size = (await fsp.stat(full)).size;
      } catch {
        /* Groesse ist nur eine Anzeige. */
      }
      // .c wird als C++ uebersetzt: das mitgelieferte g++ hat kein C-Frontend.
      out.push({ path: relative, size, language: /\.c$/i.test(entry.name) ? 'c' : 'cpp' });
    }
  }
  await walk(root, '');
  return out.sort((a, b) => a.path.localeCompare(b.path, 'en'));
}

/**
 * Ordner mit gbx_inline.h (neben der IDE bzw. eine Ebene hher). Ohne diesen
 * Include-Pfad liesse sich nicht einmal der C++-Teil eines GBX-Projekts als
 * Bibliothek bauen: der erzeugte Code beginnt mit #include <gbx_inline.h>.
 */
export function findInlineInclude(appRoot) {
  const candidates = [path.resolve(appRoot, '..', 'inline'), path.resolve(appRoot, 'inline')];
  for (const dir of candidates) {
    if (fs.existsSync(path.join(dir, 'gbx_inline.h'))) return dir;
  }
  return '';
}

/** Dateiname der Bibliothek auf harmlose Zeichen bringen (libmylib.a). */
export function libraryFileName(name) {
  const clean = String(name || '')
    .trim()
    .replace(/^lib/i, '')
    .replace(/\.a$/i, '')
    .replace(/[^\w.+-]/g, '_')
    .replace(/^_+|_+$/g, '');
  return `lib${clean || 'mylib'}.a`;
}

/** ar gehoert zum Compiler und kennt seinen eigenen Pfad. */
function findAr(compiler) {
  try {
    const result = spawnSync(compiler, ['-print-prog-name=ar'], {
      encoding: 'utf8',
      timeout: 20000,
      windowsHide: true
    });
    const name = String(result.stdout || '').trim();
    if (name && fs.existsSync(name)) return name;
  } catch {
    /* aeltere Treiber kennen die Option nicht: dann der Name aus dem PATH */
  }
  return process.platform === 'win32' ? 'ar.exe' : 'ar';
}

/** Ein Werkzeug starten und jede Ausgabezeile melden. */
function run(command, args, { cwd, onLog }) {
  return new Promise((resolve) => {
    let child;
    try {
      child = spawn(command, args, { cwd, windowsHide: true });
    } catch (error) {
      resolve({ code: -1, output: String(error?.message || error) });
      return;
    }
    let output = '';
    const handle = (chunk) => {
      const text = Buffer.isBuffer(chunk) ? chunk.toString('utf8') : String(chunk);
      output += text;
      for (const line of text.split(/\r?\n/)) {
        const trimmed = line.replace(/\s+$/, '');
        if (trimmed) onLog?.(trimmed);
      }
    };
    child.stdout?.on('data', handle);
    child.stderr?.on('data', handle);
    child.on('error', (error) => {
      output += String(error?.message || error);
      onLog?.(String(error?.message || error));
      resolve({ code: -1, output });
    });
    child.on('close', (code) => resolve({ code: code ?? -1, output }));
  });
}

/**
 * Quellen übersetzen und als Archiv ablegen.
 * `target` ist relativ zum Projektordner (z. B. lib/libmylib.a).
 */
export async function buildLibrary({ compiler, projectDir, sources, target, includes = [], onLog }) {
  const root = path.resolve(projectDir);
  const log = (text) => onLog?.(String(text));
  const list = (Array.isArray(sources) ? sources : []).map((item) => String(item || '').trim()).filter(Boolean);
  if (!list.length) return { ok: false, error: tr('Keine Quelldatei ausgewählt.') };
  if (!compiler) return { ok: false, error: tr('Kein C++-Compiler eingestellt.') };

  const files = [];
  for (const source of list) {
    const full = path.resolve(root, source);
    if (!isInside(root, full)) return { ok: false, error: tr('Der Pfad liegt außerhalb des Projekts: ') + source };
    if (!fs.existsSync(full)) return { ok: false, error: tr('Quelldatei fehlt: ') + source };
    files.push({ source, full });
  }

  const archive = path.resolve(root, String(target || ''));
  if (!isInside(root, archive) || archive === root) {
    return { ok: false, error: tr('Das Ziel liegt außerhalb des Projekts: ') + target };
  }
  await fsp.mkdir(path.dirname(archive), { recursive: true });

  const ar = findAr(compiler);
  log(tr('Compiler: ') + compiler);
  log(tr('ar: ') + ar);
  log(tr('Archiv: ') + path.relative(root, archive));

  const work = await fsp.mkdtemp(path.join(os.tmpdir(), 'gbx-lib-'));
  const objects = [];
  try {
    for (const [index, file] of files.entries()) {
      const name = path.basename(file.source).replace(/[^\w.-]/g, '_');
      const object = path.join(work, `${String(index).padStart(2, '0')}-${name}.o`);
      const args = ['-O2', '-c'];
      // Eine .c-Quelle geht als C++ durch: das mitgelieferte g++ bringt nur das
      // C++-Frontend mit. Der Header darf dann kein extern "C" verlangen.
      if (/\.c$/i.test(file.source)) {
        args.push('-x', 'c++');
        log(tr('C-Quelle wird als C++ übersetzt (kein C-Frontend im mitgelieferten Compiler): ') + file.source);
      }
      args.push(file.full, '-o', object, '-I', root);
      const dir = path.dirname(file.full);
      if (dir !== root) args.push('-I', dir);
      // gbx_inline.h u. a. aus dem Editor-Ordner.
      for (const include of includes) {
        if (include && include !== root && include !== dir) args.push('-I', include);
      }
      if (process.platform !== 'win32') args.push('-fPIC');
      log(`g++ ${args.map((arg) => (/\s/.test(arg) ? `"${arg}"` : arg)).join(' ')}`);
      const done = await run(compiler, args, { cwd: root, onLog: log });
      if (done.code !== 0) {
        return { ok: false, error: tr('Übersetzen fehlgeschlagen: ') + file.source };
      }
      objects.push(object);
    }

    const packed = await run(ar, ['rcs', archive, ...objects], { cwd: root, onLog: log });
    if (packed.code !== 0) {
      return { ok: false, error: tr('ar konnte das Archiv nicht schreiben: ') + ar };
    }
    let size = 0;
    try {
      size = (await fsp.stat(archive)).size;
    } catch {
      /* Groesse ist nur eine Anzeige. */
    }
    log(tr('Fertig: ') + path.relative(root, archive));
    return {
      ok: true,
      target: path.relative(root, archive).replace(/\\/g, '/'),
      size,
      objects: objects.length
    };
  } finally {
    await fsp.rm(work, { recursive: true, force: true });
  }
}
