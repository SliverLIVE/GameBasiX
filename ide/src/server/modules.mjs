// Zentrale Module: Die Quelldateien bleiben im Modules-Ordner. Ein Projekt
// bekommt nur eine INCLUDE-Zeile; der Compiler loest sie ueber GBX_MODULES
// beziehungsweise seinen Modules-Ordner auf.
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';

import { addNativeLibrary, removeNativeLibrary } from './project.mjs';

const MODULE_FORMAT = 'gbx-module';
const SAFE_ID = /^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/;
// Quelldateien, die zu einem Modul gehoeren koennen. Header melden INLINE-
// Befehle an und sollen sich aus der IDE heraus oeffnen lassen.
const MODULE_SOURCE_EXT = /\.(gbx|h|hh|hpp|hxx|inl|ipp|c|cc|cpp|cxx)$/i;
// Fertige Archive und Objektdateien, die ein Modul mitbringen kann.
const MODULE_LIB_EXT = /\.(a|o)$/i;
const IGNORED = new Set(['.git', '.gbxide', 'build', 'node_modules', 'dist', 'distribute', '.vscode', '.idea', '__pycache__']);

function isInside(root, candidate) {
  const rel = path.relative(path.resolve(root), path.resolve(candidate));
  return rel === '' || (!rel.startsWith('..') && !path.isAbsolute(rel));
}

function slash(value) {
  return String(value || '').replace(/\\/g, '/');
}

function relativeSafe(value) {
  const rel = slash(value).replace(/^\.\//, '').trim();
  if (!rel || path.isAbsolute(rel) || rel.split('/').some((part) => part === '..')) return '';
  return rel;
}

/**
 * Findet den zentralen Modules-Ordner. In der Entwicklung liegt er neben
 * `ide/` (also im Repository-Root); im Paket neben der EXE oder in
 * `resources/modules`. `GBX_MODULES` hat Vorrang und wird an gbx weitergegeben.
 */
export function modulesRoot(appRoot) {
  const candidates = [];
  if (process.env.GBX_MODULES) candidates.push(process.env.GBX_MODULES);
  if (appRoot) {
    candidates.push(path.join(appRoot, '..', 'modules'));
    candidates.push(path.join(appRoot, 'modules'));
  }
  if (process.resourcesPath) candidates.push(path.join(process.resourcesPath, 'modules'));
  if (process.execPath) {
    const dir = path.dirname(process.execPath);
    candidates.push(path.join(dir, 'modules'));
    candidates.push(path.join(dir, '..', 'modules'));
  }
  candidates.push(path.join(process.cwd(), 'modules'));
  for (const candidate of candidates) {
    try {
      if (fs.statSync(candidate).isDirectory()) return path.resolve(candidate);
    } catch {
      /* naechster Kandidat */
    }
  }
  return appRoot ? path.resolve(appRoot, '..', 'modules') : path.resolve('modules');
}

function commentText(line) {
  const match = /^\s*(?:'|\/\/)\s?(.*)$/.exec(String(line || ''));
  return match ? match[1] : null;
}

/**
 * Klappt Blockkommentare aus C++-Quellen in `//`-Zeilen um, damit `@help` und
 * `@tooltip` auch in `.h`- und `.cpp`-Dateien funktionieren. Zeilen, die mit
 * einem GBX-Kommentar beginnen, bleiben unveraendert.
 */
function blockCommentsAsLineComments(text) {
  const source = String(text || '').replace(/\r\n?/g, '\n');
  let out = '';
  let index = 0;
  let quote = '';
  while (index < source.length) {
    const char = source[index];
    const next = source[index + 1];
    if (quote) {
      out += char;
      if (char === '\\' && next) {
        out += next;
        index += 2;
        continue;
      }
      if (char === quote) quote = '';
      index += 1;
      continue;
    }
    if (char === '/' && next === '/') {
      const end = source.indexOf('\n', index);
      const stop = end < 0 ? source.length : end;
      out += source.slice(index, stop);
      index = stop;
      continue;
    }
    if (char === "'" && /(^|\n)[ \t]*$/.test(out)) {
      // GBX-Kommentar: bis zum Zeilenende unveraendert uebernehmen, damit ein
      // einzelnes Anfuehrungszeichen im Text keinen Block vortaeuscht.
      const end = source.indexOf('\n', index);
      const stop = end < 0 ? source.length : end;
      out += source.slice(index, stop);
      index = stop;
      continue;
    }
    if (char === '"') {
      quote = char;
      out += char;
      index += 1;
      continue;
    }
    if (char === '/' && next === '*') {
      const end = source.indexOf('*/', index + 2);
      const body = source.slice(index + 2, end < 0 ? source.length : end);
      // Fuehrende `*`-Dekoration der ueblichen C++-Kommentarbloecke entfernen.
      out += body
        .split('\n')
        .map((line) => `// ${line.replace(/^\s*\*?\s?/, '')}`)
        .join('\n');
      index = end < 0 ? source.length : end + 2;
      continue;
    }
    out += char;
    index += 1;
  }
  return out;
}

/**
 * Liest `'@help`- und `'@tooltip`-Bloecke aus GBX- und C++-Kommentaren. Die
 * Bloecke sind bewusst Markdown: derselbe Text wird als HELP.md und als Hover
 * benutzt.
 */
export function parseModuleDocs(text, out = { help: '', tooltips: {} }) {
  const lines = blockCommentsAsLineComments(text).split('\n');
  let mode = '';
  let target = '';
  let buffer = [];
  const flush = () => {
    const body = buffer.join('\n').trim();
    if (mode === 'help' && body) {
      out.help = out.help ? `${out.help.trim()}\n\n${body}` : body;
    } else if (mode === 'tooltip' && target && body) {
      out.tooltips[target.toUpperCase()] = body;
    }
    mode = '';
    target = '';
    buffer = [];
  };

  for (const line of lines) {
    const textLine = commentText(line);
    if (textLine !== null) {
      const help = /^\s*@help\b/i.exec(textLine);
      if (help) {
        flush();
        mode = 'help';
        continue;
      }
      const tooltip = /^\s*@tooltip\s+(.+?)\s*$/i.exec(textLine);
      if (tooltip) {
        flush();
        mode = 'tooltip';
        target = tooltip[1].trim();
        continue;
      }
      if (/^\s*@(endhelp|endtooltip|end)\b/i.test(textLine)) {
        flush();
        continue;
      }
      if (mode) buffer.push(textLine);
      continue;
    }
    // Eine echte Codezeile beendet einen offenen Kommentarblock.
    if (mode) flush();
  }
  flush();
  return out;
}

/**
 * Alle Quellen eines Moduls: die im Manifest genannten Dateien zuerst, danach
 * jede weitere `.gbx`/`.h`/`.cpp` aus dem Modulordner. So findet der Hover
 * auch `@tooltip`-Bloecke, die nicht in `module.json` eingetragen sind.
 */
function moduleSourceFiles(moduleDir, files) {
  const out = [...files];
  const walk = (dir, rel) => {
    let entries;
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      if (IGNORED.has(entry.name) || entry.name.startsWith('.')) continue;
      const nextRel = rel ? `${rel}/${entry.name}` : entry.name;
      if (entry.isDirectory()) {
        // `lib` enthaelt Archive und keine Doku.
        if (!rel && entry.name === 'lib') continue;
        walk(path.join(dir, entry.name), nextRel);
        continue;
      }
      if (!MODULE_SOURCE_EXT.test(entry.name) || out.includes(nextRel)) continue;
      out.push(nextRel);
    }
  };
  walk(moduleDir, '');
  return out;
}

function moduleDocsFromFiles(moduleDir, files) {
  const docs = { help: '', tooltips: {} };
  for (const rel of files) {
    try {
      const text = fs.readFileSync(path.join(moduleDir, rel), 'utf8');
      parseModuleDocs(text, docs);
    } catch {
      /* fehlende Datei wird beim Auflisten gemeldet */
    }
  }
  if (!docs.help) {
    try {
      docs.help = fs.readFileSync(path.join(moduleDir, 'HELP.md'), 'utf8').trim();
    } catch {
      /* kein statisches Help noetig */
    }
  }
  return docs;
}

/**
 * Oeffentliche Namen eines Moduls mit Datei und Zeile. Der Index ist bewusst
 * unabhaengig von der IDE: Definition und Peek werden daraus direkt auf die
 * Modulquelle abgebildet.
 */
function moduleDefinitions(id, folder, moduleDir, files) {
  const definitions = {};
  const add = (key, rel, line, kind, name, extra = {}) => {
    const target = String(key || '').trim().toUpperCase();
    if (!target || definitions[target]) return;
    definitions[target] = {
      file: slash(path.join(folder, rel)),
      line: Math.max(1, Number(line) || 1),
      kind,
      name: String(name || key),
      ...extra
    };
    // GL3D_INFO$ ist auch als GL3D_INFO erreichbar.
    const bare = target.replace(/[$%#]$/, '');
    if (bare && bare !== target && !definitions[bare]) definitions[bare] = definitions[target];
  };

  for (const rel of files) {
    let text;
    try {
      text = fs.readFileSync(path.join(moduleDir, rel), 'utf8');
    } catch {
      continue;
    }
    let currentType = '';
    let inline = false;
    const lines = text.replace(/\r\n?/g, '\n').split('\n');
    lines.forEach((raw, index) => {
      const line = raw.trim();
      if (!line) return;
      if (/^(?:'|\/\/)/.test(line)) return;
      // INLINE-Befehle stehen als C++-Anmeldung im Block oder im Header.
      if (line.includes('GBX_INLINE_FN')) {
        const code = line.replace(/\/\/.*$/, '');
        const any = /GBX_INLINE_FN_ANY\s*\(\s*"([^"]+)"/.exec(code);
        if (any) {
          add(any[1], rel, index + 1, 'inline', any[1], { min: 0, max: -1 });
          return;
        }
        const registered = /GBX_INLINE_FN\s*\(\s*"([^"]+)"\s*,\s*(-?\d+)\s*,\s*(-?\d+)/.exec(code);
        if (registered) {
          add(registered[1], rel, index + 1, 'inline', registered[1], {
            min: Number(registered[2]),
            max: Number(registered[3])
          });
          return;
        }
      }
      if (/^INLINE\b/i.test(line)) {
        inline = true;
        return;
      }
      if (/^ENDINLINE\b/i.test(line)) {
        inline = false;
        return;
      }
      if (inline) return;
      // Nur GBX-Quellen enthalten GBX-Deklarationen. C++-Quellen wuerden
      // sonst etwa `const double` als Konstante DOUBLE indexieren.
      if (!/\.gbx$/i.test(rel)) return;

      const type = /^TYPE\s+([A-Za-z_]\w*)/i.exec(line);
      if (type) {
        currentType = type[1];
        add(currentType, rel, index + 1, 'type', currentType);
        add(`${id}.${currentType}`, rel, index + 1, 'type', currentType);
        return;
      }
      if (/^ENDTYPE\b/i.test(line)) {
        currentType = '';
        return;
      }

      const method = /^METHOD\s+([A-Za-z_]\w*)/i.exec(line);
      if (method) {
        add(`${id}.${method[1]}`, rel, index + 1, 'method', method[1]);
        if (currentType) {
          add(`${id}.${currentType}.${method[1]}`, rel, index + 1, 'method', method[1]);
          add(`${currentType}.${method[1]}`, rel, index + 1, 'method', method[1]);
        }
        return;
      }

      const routine = /^(?:FUNCTION|SUB)\s+([A-Za-z_]\w*)/i.exec(line);
      if (routine) {
        add(`${id}.${routine[1]}`, rel, index + 1, 'function', routine[1]);
        add(routine[1], rel, index + 1, 'function', routine[1]);
        return;
      }

      const constant = /^(?:CONSTANT|CONST)\s+([A-Za-z_]\w*)/i.exec(line);
      if (constant) {
        add(`${id}.${constant[1]}`, rel, index + 1, 'constant', constant[1]);
        add(constant[1], rel, index + 1, 'constant', constant[1]);
        return;
      }

      const enumDecl = /^ENUM\s+([A-Za-z_]\w*)/i.exec(line);
      if (enumDecl) {
        add(`${id}.${enumDecl[1]}`, rel, index + 1, 'enum', enumDecl[1]);
        add(enumDecl[1], rel, index + 1, 'enum', enumDecl[1]);
      }
    });
  }
  return definitions;
}

function validateInclude(value, fallback) {
  const rel = relativeSafe(value || fallback);
  if (!rel || !/^modules\//i.test(rel) || !/\.gbx$/i.test(rel)) {
    throw new Error(`Ungueltiger Module-Include: ${value || fallback}`);
  }
  return rel;
}

async function readModule(root, dirName) {
  const dir = path.join(root, dirName);
  const manifestPath = path.join(dir, 'module.json');
  let raw;
  try {
    raw = JSON.parse(await fsp.readFile(manifestPath, 'utf8'));
  } catch {
    return null;
  }
  if (raw?.format !== MODULE_FORMAT) return null;
  const id = String(raw.id || dirName).trim();
  if (!SAFE_ID.test(id)) return null;
  const folder = relativeSafe(raw.folder || dirName);
  if (!folder) return null;
  const moduleDir = path.join(root, folder);
  const files = [];
  for (const item of Array.isArray(raw.files) ? raw.files : []) {
    const rel = relativeSafe(item);
    if (!rel || !MODULE_SOURCE_EXT.test(rel)) continue;
    const absolute = path.join(moduleDir, rel);
    if (!isInside(moduleDir, absolute)) continue;
    if (!files.includes(rel)) files.push(rel);
  }
  if (!files.length || !files.some((rel) => /\.gbx$/i.test(rel))) return null;
  // native.libs nennt Archive relativ zum Modulordner. Sie bleiben dort; beim
  // Einbinden kopiert die IDE sie ins Projekt und traegt sie in [native] ein.
  const nativeRaw = raw.native && typeof raw.native === 'object' ? raw.native : {};
  const nativeLibs = [];
  for (const item of Array.isArray(nativeRaw.libs) ? nativeRaw.libs : []) {
    const rel = relativeSafe(item);
    if (!rel || !MODULE_LIB_EXT.test(rel)) continue;
    const absolute = path.join(moduleDir, rel);
    if (!isInside(moduleDir, absolute)) continue;
    if (!nativeLibs.includes(rel)) nativeLibs.push(rel);
  }
  const entry = relativeSafe(raw.entry || files[0]) || files[0];
  const include = raw.include && typeof raw.include === 'object' ? raw.include : {};
  const declaredInclude =
    typeof raw.include === 'string' ? raw.include : include.entry || include.modular || include.single;
  const entryInclude = validateInclude(declaredInclude, `modules/${folder}/${entry}`);
  const sources = moduleSourceFiles(moduleDir, files);
  const docs = moduleDocsFromFiles(moduleDir, sources);
  const definitions = moduleDefinitions(id, folder, moduleDir, sources);
  return {
    format: MODULE_FORMAT,
    id,
    name: String(raw.name || id),
    version: String(raw.version || '1.0.0'),
    summary: String(raw.summary || raw.description || '').trim(),
    description: String(raw.description || '').trim(),
    tags: (Array.isArray(raw.tags) ? raw.tags : []).map(String).filter(Boolean),
    author: String(raw.author || ''),
    folder,
    dir: moduleDir,
    entry,
    files,
    sources,
    native: { libs: nativeLibs },
    include: {
      entry: entryInclude
    },
    help: docs.help,
    tooltips: docs.tooltips,
    definitions
  };
}

/** Alle Module im zentralen Ordner; ohne Projekt sind sie nur "verfuegbar". */
export async function listModules(root, projectDir = '') {
  let entries;
  try {
    entries = await fsp.readdir(root, { withFileTypes: true });
  } catch {
    return [];
  }
  const installed = projectDir ? await findProjectIncludes(projectDir) : new Map();
  const modules = [];
  for (const entry of entries.sort((a, b) => a.name.localeCompare(b.name, 'de'))) {
    if (!entry.isDirectory() || entry.name.startsWith('.')) continue;
    const module = await readModule(root, entry.name);
    if (!module) continue;
    const hit = installed.get(module.include.entry) || '';
    modules.push({
      id: module.id,
      name: module.name,
      version: module.version,
      summary: module.summary,
      description: module.description,
      tags: module.tags,
      author: module.author,
      files: module.files,
      entry: module.entry,
      folder: module.folder,
      native: module.native || { libs: [] },
      definitions: module.definitions || {},
      include: module.include,
      installed: Boolean(hit),
      installedIn: hit ? [hit] : [],
      help: module.help,
      tooltips: module.tooltips
    });
  }
  return modules;
}

async function projectEntries(projectDir) {
  const out = [];
  async function walk(dir, rel) {
    let entries;
    try {
      entries = await fsp.readdir(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      if (IGNORED.has(entry.name) || entry.name.startsWith('.')) continue;
      const relative = rel ? `${rel}/${entry.name}` : entry.name;
      if (entry.isDirectory()) await walk(path.join(dir, entry.name), relative);
      else if (/\.gbx$/i.test(entry.name)) out.push(relative);
    }
  }
  await walk(path.resolve(projectDir), '');
  return out.sort();
}

async function findProjectIncludes(projectDir) {
  const found = new Map();
  let files = [];
  try {
    files = await projectEntries(projectDir);
  } catch {
    return found;
  }
  for (const file of files) {
    let text;
    try {
      text = await fsp.readFile(path.join(projectDir, file), 'utf8');
    } catch {
      continue;
    }
    // Kommentierte Beispiele duerfen nicht als installiert gelten.
    const code = String(text)
      .replace(/\r\n?/g, '\n')
      .split('\n')
      .filter((line) => !/^\s*(?:'|\/\/)/.test(line))
      .join('\n');
    for (const match of code.matchAll(/\bINCLUDE\s+"([^"]+\.gbx)"/gi)) {
      found.set(slash(match[1]), file);
    }
  }
  return found;
}

async function projectEntry(projectDir, targetFile = '') {
  const requested = relativeSafe(targetFile);
  if (requested && /\.gbx$/i.test(requested)) return requested;
  try {
    const meta = JSON.parse(await fsp.readFile(path.join(projectDir, '.gbxide', 'project.json'), 'utf8'));
    const entry = relativeSafe(meta.entry);
    if (entry && fs.existsSync(path.join(projectDir, entry))) return entry;
  } catch {
    /* Metadaten sind optional */
  }
  for (const candidate of ['main.gbx', 'Main.gbx']) {
    if (fs.existsSync(path.join(projectDir, candidate))) return candidate;
  }
  const files = await projectEntries(projectDir);
  return files[0] || '';
}

function insertInclude(text, include) {
  const newline = /\r\n/.test(text) ? '\r\n' : '\n';
  const lines = String(text).replace(/\r\n?/g, '\n').split('\n');
  let at = 0;
  while (at < lines.length && (!lines[at].trim() || /^\s*(?:'|\/\/)/.test(lines[at]))) at += 1;
  lines.splice(at, 0, `INCLUDE "${include}"`, '');
  return lines.join(newline);
}

/**
 * Fuegt einem Projekt nur die Include-Zeile hinzu. Die Moduldateien bleiben
 * unangetastet im zentralen Modules-Ordner.
 */
export async function addModuleToProject({ root, projectDir, id, targetFile = '' }) {
  const dir = path.resolve(projectDir || '');
  let stat;
  try {
    stat = await fsp.stat(dir);
  } catch {
    throw new Error('Projektordner nicht gefunden.');
  }
  if (!stat.isDirectory()) throw new Error('Projektordner nicht gefunden.');
  const module = await readModule(root, String(id || '').toLowerCase());
  const module2 = module || (await listModules(root).then((items) => items.find((item) => item.id.toUpperCase() === String(id).toUpperCase())));
  if (!module2) throw new Error(`Modul nicht gefunden: ${id}`);
  const include = module2.include.entry;

  // Erst pruefen, ob der Eintrag ueberhaupt moeglich ist: sonst bliebe eine
  // kopierte Bibliothek ohne [native]-Zeile im Projekt liegen.
  if (module2.native?.libs?.length) {
    const entries = await fsp.readdir(dir);
    if (!entries.some((entry) => /\.gbxproj$/i.test(entry))) {
      throw new Error('Keine .gbxproj im Projekt gefunden.');
    }
  }
  const copiedLibs = await installModuleLibraries(module2, dir);
  let nativeLibs = null;
  for (const lib of copiedLibs) {
    nativeLibs = (await addNativeLibrary(dir, lib)).libs;
  }

  const installed = await findProjectIncludes(dir);
  if (installed.has(include)) {
    return {
      ok: true,
      added: false,
      module: publicModule(module2),
      include,
      file: installed.get(include),
      libs: copiedLibs,
      nativeLibs
    };
  }
  const file = await projectEntry(dir, targetFile);
  if (!file) throw new Error('Keine Einstiegsdatei im Projekt gefunden.');
  const target = path.join(dir, file);
  if (!isInside(dir, target)) throw new Error('Der Pfad liegt ausserhalb des Projekts.');
  const text = await fsp.readFile(target, 'utf8');
  if (!new RegExp(`\\bINCLUDE\\s+"${include.replace(/[.*+?^${}()|[\\]\\\\]/g, '\\\\$&')}"`, 'i').test(text)) {
    await fsp.writeFile(target, insertInclude(text, include), 'utf8');
  }
  return {
    ok: true,
    added: true,
    module: publicModule(module2),
    include,
    file,
    libs: copiedLibs,
    nativeLibs
  };
}

/** Kopiert die im Manifest genannten Archive nach lib/<Modulordner>/ im Projekt. */
async function installModuleLibraries(module, projectDir) {
  const root = path.resolve(projectDir);
  const copied = [];
  for (const rel of module.native?.libs || []) {
    const source = path.resolve(module.dir, rel);
    if (!isInside(module.dir, source)) {
      throw new Error(`Bibliothek liegt ausserhalb des Moduls: ${rel}`);
    }
    let stat;
    try {
      stat = await fsp.stat(source);
    } catch {
      throw new Error(`Bibliothek fehlt im Modul: ${rel}`);
    }
    if (!stat.isFile()) throw new Error(`Bibliothek fehlt im Modul: ${rel}`);
    const targetRel = moduleLibraryTarget(module, rel);
    const target = path.join(root, targetRel);
    if (!isInside(root, target) || target === root) {
      throw new Error(`Ungueltiges Bibliotheksziel: ${targetRel}`);
    }
    await fsp.mkdir(path.dirname(target), { recursive: true });
    await fsp.copyFile(source, target);
    copied.push(targetRel);
  }
  return copied;
}

function moduleLibraryTarget(module, relativePath) {
  return slash(path.join('lib', module.folder, path.basename(String(relativePath || ''))));
}

function escapeRegExp(value) {
  return String(value).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/** Entfernt die INCLUDE-Zeile eines Moduls, ohne den Rest der Datei anzufassen. */
function removeIncludeLine(text, include) {
  const newline = /\r\n/.test(text) ? '\r\n' : '\n';
  const lines = String(text).replace(/\r\n?/g, '\n').split('\n');
  const pattern = new RegExp(`^\\s*INCLUDE\\s+"${escapeRegExp(include)}"\\s*$`, 'i');
  let changed = false;
  for (let index = lines.length - 1; index >= 0; index -= 1) {
    if (!pattern.test(lines[index])) continue;
    lines.splice(index, 1);
    changed = true;
    // insertInclude legt eine Leerzeile hinter die Zeile; mit entfernen.
    if (lines[index] === '') lines.splice(index, 1);
  }
  return changed ? lines.join(newline) : text;
}

/**
 * Loest ein Modul wieder: alle INCLUDE-Zeilen, die zugehoerigen
 * [native]-Eintraege und die ins Projekt kopierten Archive verschwinden.
 */
export async function removeModuleFromProject({ root, projectDir, id }) {
  const dir = path.resolve(projectDir || '');
  let stat;
  try {
    stat = await fsp.stat(dir);
  } catch {
    throw new Error('Projektordner nicht gefunden.');
  }
  if (!stat.isDirectory()) throw new Error('Projektordner nicht gefunden.');
  const lookup = String(id || '').trim();
  let module = await readModule(root, lookup.toLowerCase());
  if (!module) {
    module = (await listModules(root)).find((item) => item.id.toUpperCase() === lookup.toUpperCase());
  }
  if (!module) throw new Error(`Modul nicht gefunden: ${id}`);

  const include = module.include.entry;
  const files = await projectEntries(dir);
  const changed = [];
  for (const file of files) {
    const target = path.join(dir, file);
    const text = await fsp.readFile(target, 'utf8');
    const next = removeIncludeLine(text, include);
    if (next !== text) {
      await fsp.writeFile(target, next, 'utf8');
      changed.push(file);
    }
  }

  let nativeLibs = null;
  const deletedLibs = [];
  let hasProjectFile = false;
  try {
    hasProjectFile = (await fsp.readdir(dir)).some((entry) => /\.gbxproj$/i.test(entry));
  } catch {
    /* Projektordner wurde gerade entfernt. */
  }
  for (const rel of module.native?.libs || []) {
    const targetRel = moduleLibraryTarget(module, rel);
    if (hasProjectFile) {
      nativeLibs = (await removeNativeLibrary(dir, targetRel)).libs;
    }
    const target = path.join(dir, targetRel);
    if (!isInside(dir, target) || !fs.existsSync(target)) continue;
    await fsp.rm(target, { force: true });
    deletedLibs.push(targetRel);
    try {
      await fsp.rmdir(path.dirname(target));
    } catch {
      /* Ordner ist nicht leer oder schon weg. */
    }
  }
  if (deletedLibs.length) {
    try {
      await fsp.rmdir(path.join(dir, 'lib'));
    } catch {
      /* lib enthaelt noch andere Dateien. */
    }
  }

  return {
    ok: true,
    removed: changed.length > 0 || deletedLibs.length > 0,
    files: changed,
    libs: deletedLibs,
    nativeLibs
  };
}

/**
 * Befehle, die aktive zentrale Module per `GBX_INLINE_FN(...)` anmelden.
 * Sie wandern in dieselbe Liste wie die INLINE-Befehle des Projekts, damit
 * Farbe, Vorschlaege, Hover und Ctrl+Klick auch fuer Modulbefehle gelten.
 */
export async function listModuleInlineCommands(root, projectDir = '') {
  const modules = await listModules(root, projectDir);
  const found = new Map();
  for (const module of modules) {
    if (!module.installed) continue;
    for (const definition of Object.values(module.definitions || {})) {
      if (definition.kind !== 'inline' || !definition.name || found.has(definition.name)) continue;
      found.set(definition.name, {
        name: definition.name,
        min: Number(definition.min) || 0,
        max: definition.max === undefined ? -1 : Number(definition.max),
        file: definition.file,
        line: definition.line,
        module: true
      });
    }
  }
  return [...found.values()].sort((a, b) => a.name.localeCompare(b.name, 'de'));
}

/** Nur die Daten, die die Oberflaeche braucht (ohne absolute Serverpfade). */
export function publicModule(module) {
  return {
    id: module.id,
    name: module.name,
    version: module.version,
    summary: module.summary,
    description: module.description,
    tags: module.tags || [],
    author: module.author || '',
    files: module.files || [],
    entry: module.entry,
    native: module.native || { libs: [] },
    definitions: module.definitions || {},
    include: module.include,
    help: module.help || '',
    tooltips: module.tooltips || {}
  };
}

/** HELP.md aus den Markdown-Kommentaren der Module schreiben. */
export async function writeModuleHelp(root) {
  const modules = await listModules(root);
  const written = [];
  for (const module of modules) {
    // listModules liefert den Ordner nicht mit; ueber module.json ermitteln.
    let dir = '';
    try {
      const entries = await fsp.readdir(root, { withFileTypes: true });
      for (const entry of entries) {
        if (!entry.isDirectory()) continue;
        const candidate = path.join(root, entry.name, 'module.json');
        if (!fs.existsSync(candidate)) continue;
        const raw = JSON.parse(await fsp.readFile(candidate, 'utf8'));
        if (String(raw.id).toUpperCase() === module.id.toUpperCase()) {
          dir = path.join(root, String(raw.folder || entry.name));
          break;
        }
      }
    } catch {
      /* naechstes Modul */
    }
    if (!dir) continue;
    const file = path.join(dir, 'HELP.md');
    const body = `${module.help.trim()}\n\n---\n\nGenerated from \`@help\` blocks in the module's GBX sources.\n`;
    await fsp.writeFile(file, body, 'utf8');
    written.push({ id: module.id, file });
  }
  return written;
}
