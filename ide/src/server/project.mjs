import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';

import { setLanguage } from './locale.mjs';

// Erst beim Zugriff auflösen: portable Builds setzen GBXIDE_HOME ggf. erst
// nach dem Import dieses Moduls.
export function configDir() {
  return process.env.GBXIDE_HOME || path.join(os.homedir(), '.gbx-studio');
}

function configFile() {
  return path.join(configDir(), 'config.json');
}
const META_DIR = '.gbxide';
const META_FILE = 'project.json';
export const PROJECT_EXT = 'gbxproj';
export const SOURCE_EXT = 'gbx';

export function projectPaths(projectDir) {
  const dir = path.resolve(projectDir);
  return {
    dir,
    metaFile: path.join(dir, META_DIR, META_FILE),
    buildDir: path.join(dir, 'build')
  };
}

export function isInside(root, candidate) {
  const rel = path.relative(path.resolve(root), path.resolve(candidate));
  return rel === '' || (!rel.startsWith('..') && !path.isAbsolute(rel));
}

function defaultProject(name, sources) {
  return {
    name,
    appName: name,
    title: name,
    entry: sources[0] || `main.${SOURCE_EXT}`,
    width: 640,
    height: 480,
    fullscreen: false,
    resizable: true,
    frameRate: 60,
    vsync: true,
    audio: true,
    volume: 1,
    assetDir: 'assets',
    sources,
    openFiles: [...sources],
    createdAt: new Date().toISOString()
  };
}

export function sanitizeProjectName(name) {
  return String(name || '')
    .trim()
    .replace(/[<>:"/\\|?*\u0000-\u001f]/g, '')
    .replace(/\s+/g, ' ')
    .slice(0, 80);
}

export async function loadConfig() {
  try {
    const raw = await fsp.readFile(configFile(), 'utf8');
    const config = JSON.parse(raw);
    // Die gewählte Sprache gilt für alle Meldungen dieses Prozesses.
    setLanguage(config.language);
    return config;
  } catch {
    setLanguage('en');
    return { gbxPath: '', recent: [] };
  }
}

export async function saveConfig(config) {
  await fsp.mkdir(configDir(), { recursive: true });
  await fsp.writeFile(configFile(), JSON.stringify(config, null, 2), 'utf8');
  return config;
}

const EDITOR_THEME_TOKEN_KEYS = [
  'comment', 'keyword', 'command', 'directive', 'type', 'userType',
  'number', 'string', 'operator', 'delimiter', 'identifier', 'label'
];
const EDITOR_THEME_COLOR_KEYS = ['background', 'foreground', 'cursor', 'selection', 'lineHighlight', 'lineNumber'];
const HEX_COLOR = /^#[0-9a-fA-F]{6}$/;

/** Nimmt nur bekannte Farbfelder mit gültigem Hexwert an. */
export function sanitizeEditorTheme(spec) {
  const out = {};
  if (!spec || typeof spec !== 'object') return out;
  if (typeof spec.preset === 'string' && /^[a-z]{1,20}$/.test(spec.preset)) out.preset = spec.preset;
  for (const [group, keys] of [
    ['tokens', EDITOR_THEME_TOKEN_KEYS],
    ['colors', EDITOR_THEME_COLOR_KEYS]
  ]) {
    const source = spec[group];
    if (!source || typeof source !== 'object') continue;
    const target = {};
    for (const key of keys) {
      const value = source[key];
      if (typeof value === 'string' && HEX_COLOR.test(value.trim())) target[key] = value.trim().toLowerCase();
    }
    if (Object.keys(target).length) out[group] = target;
  }
  return out;
}

/** Prüft gespeicherte Erscheinungsbild-Einstellungen (Hell/Dunkel). */
export function sanitizeAppearance(spec) {
  const out = {};
  if (!spec || typeof spec !== 'object') return out;
  if (spec.mode === 'light' || spec.mode === 'dark') out.mode = spec.mode;
  const themes = {};
  for (const mode of ['dark', 'light']) {
    const source = spec.themes?.[mode];
    if (!source || typeof source !== 'object') continue;
    const theme = {};
    for (const key of ['accent', 'background', 'text']) {
      const value = source[key];
      if (typeof value === 'string' && HEX_COLOR.test(value.trim())) theme[key] = value.trim().toLowerCase();
    }
    if (source.editor !== undefined) theme.editor = sanitizeEditorTheme(source.editor);
    if (Object.keys(theme).length) themes[mode] = theme;
  }
  if (Object.keys(themes).length) out.themes = themes;
  return out;
}

export async function addRecent(projectDir, name) {
  const config = await loadConfig();
  const recent = (config.recent || []).filter((r) => path.resolve(r.path) !== path.resolve(projectDir));
  recent.unshift({ path: projectDir, name, openedAt: new Date().toISOString() });
  config.recent = recent.slice(0, 15);
  await saveConfig(config);
}

async function readMeta(projectDir) {
  const { metaFile } = projectPaths(projectDir);
  try {
    return JSON.parse(await fsp.readFile(metaFile, 'utf8'));
  } catch {
    return null;
  }
}

async function writeMeta(projectDir, project) {
  const { metaFile } = projectPaths(projectDir);
  await fsp.mkdir(path.dirname(metaFile), { recursive: true });
  await fsp.writeFile(metaFile, JSON.stringify(project, null, 2), 'utf8');
  return metaFile;
}

/* ------------------------------------------------------------------ */
/* .gbxproj lesen und schreiben                                        */
/* ------------------------------------------------------------------ */

function parseIni(text) {
  const sections = {};
  let current = sections;
  for (const raw of String(text).replace(/\r\n?/g, '\n').split('\n')) {
    const line = raw.trim();
    if (!line || line.startsWith(';') || line.startsWith('#') || line.startsWith('//')) continue;
    const section = /^\[(.+)]$/.exec(line);
    if (section) {
      const name = section[1].trim().toUpperCase();
      sections[name] = sections[name] || {};
      current = sections[name];
      continue;
    }
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    const key = line.slice(0, eq).trim().toUpperCase();
    let value = line.slice(eq + 1).trim();
    // Kommentar hinter dem Wert entfernen, aber nicht in Anführungszeichen.
    if (!/^["']/.test(value)) value = value.replace(/\s+[;#].*$/, '').trim();
    if (/^".*"$/.test(value) || /^'.*'$/.test(value)) value = value.slice(1, -1);
    current[key] = value;
  }
  return sections;
}

function iniBool(value, fallback) {
  const text = String(value ?? '').trim().toUpperCase();
  if (['1', 'TRUE', 'YES', 'ON', 'JA'].includes(text)) return true;
  if (['0', 'FALSE', 'NO', 'OFF', 'NEIN'].includes(text)) return false;
  return fallback;
}

function iniNumber(value, fallback) {
  const number = Number(String(value ?? '').replace(',', '.'));
  return Number.isFinite(number) ? number : fallback;
}

function parseProjectFile(text) {
  const ini = parseIni(text);
  const project = ini.PROJECT || {};
  const window = ini.WINDOW || {};
  const graphics = ini.GRAPHICS || {};
  const audio = ini.AUDIO || {};
  const assets = ini.ASSETS || ini.PATHS || {};
  const out = {};
  if (project.NAME) out.appName = project.NAME;
  if (project.MAIN || project.ENTRY) out.entry = project.MAIN || project.ENTRY;
  if (window.TITLE) out.title = window.TITLE;
  if (window.WIDTH) out.width = Math.max(64, Math.round(iniNumber(window.WIDTH, 640)));
  if (window.HEIGHT) out.height = Math.max(64, Math.round(iniNumber(window.HEIGHT, 480)));
  out.fullscreen = iniBool(window.FULLSCREEN, false);
  out.resizable = iniBool(window.RESIZABLE, true);
  if (graphics.FPS !== undefined) out.frameRate = Math.max(0, iniNumber(graphics.FPS, 60));
  out.vsync = iniBool(graphics.VSYNC, true);
  out.audio = iniBool(audio.ENABLED, true);
  if (audio.VOLUME !== undefined) out.volume = Math.min(1, Math.max(0, iniNumber(audio.VOLUME, 1)));
  if (assets.ROOT || assets.DIR) out.assetDir = assets.ROOT || assets.DIR;
  return out;
}

function iniValue(value) {
  const text = String(value ?? '');
  if (!text) return '';
  return /^[A-Za-z0-9_.\- /\\:]+$/.test(text) && !/^\s|\s$/.test(text) ? text : `"${text.replace(/"/g, '\\"')}"`;
}

export function projectFileText(project) {
  const p = project || {};
  const bool = (value) => (value ? 1 : 0);
  const lines = [
    '; GameBasiX project file',
    '; Written by the IDE, but you can also edit it by hand.',
    '',
    '[project]',
    `name = ${iniValue(p.appName || p.name || 'GBX-Projekt')}`,
    `main = ${iniValue(String(p.entry || `main.${SOURCE_EXT}`).replace(/\\/g, '/'))}`,
    '',
    '[window]',
    `title = ${iniValue(p.title || p.appName || p.name || 'GBX-Projekt')}`,
    `width = ${Math.max(64, Math.round(Number(p.width) || 640))}`,
    `height = ${Math.max(64, Math.round(Number(p.height) || 480))}`,
    `fullscreen = ${bool(p.fullscreen)}`,
    `resizable = ${bool(p.resizable !== false)}`,
    '',
    '[graphics]',
    `fps = ${Math.max(0, Math.round(Number(p.frameRate) || 0))}`,
    `vsync = ${bool(p.vsync !== false)}`,
    '',
    '[audio]',
    `enabled = ${bool(p.audio !== false)}`,
    `volume = ${Math.min(1, Math.max(0, Number(p.volume ?? 1)))}`,
    '',
    '[assets]',
    `root = ${iniValue(p.assetDir || 'assets')}`,
    ''
  ];
  return lines.join('\r\n');
}

export async function writeProjectFile(project, projectDir) {
  const name = sanitizeProjectName(project?.name || project?.appName || path.basename(projectDir));
  const file = path.join(projectDir, `${name}.${PROJECT_EXT}`);
  await fsp.writeFile(file, projectFileText(project), 'utf8');
  return file;
}

function readEntry(project, projectDir) {
  const entry = String(project.entry || '').replace(/\\/g, '/');
  if (entry) {
    const target = path.join(projectDir, entry);
    if (fs.existsSync(target)) return entry;
  }
  return '';
}

/** Alle .gbx-Quellen im Projektordner (ohne Build- und Metadatenordner). */
async function findSources(projectDir) {
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
      else if (new RegExp(`\\.${SOURCE_EXT}$`, 'i').test(entry.name)) out.push(relative);
    }
  }
  await walk(path.resolve(projectDir), '');
  return out.sort((a, b) => a.localeCompare(b, 'de'));
}

export async function openProject(target) {
  const resolved = path.resolve(target);
  let stat;
  try {
    stat = await fsp.stat(resolved);
  } catch {
    throw new Error(`Pfad nicht gefunden: ${resolved}`);
  }

  if (stat.isFile()) {
    const extension = path.extname(resolved).toLowerCase();
    if (extension === `.${SOURCE_EXT}`) {
      // Einzelne Quelldatei: der Ordner wird zum Projekt, main ist die Datei.
      const dir = path.dirname(resolved);
      const sources = await findSources(dir);
      const existing = await readMeta(dir);
      const name = existing?.name || path.basename(dir);
      const project = {
        ...defaultProject(name, sources),
        ...(existing || {}),
        name,
        entry: path.basename(resolved),
        sources: sources.length ? sources : [path.basename(resolved)]
      };
      await writeMeta(dir, project);
      await addRecent(dir, project.name);
      return { project, dir };
    }
    if (extension !== `.${PROJECT_EXT}`) throw new Error('Bitte einen Projektordner, eine .gbxproj- oder eine .gbx-Datei öffnen.');
    const dir = path.dirname(resolved);
    const parsed = parseProjectFile(await fsp.readFile(resolved, 'utf8'));
    const existing = await readMeta(dir);
    const sources = await findSources(dir);
    if (!sources.length) throw new Error('Die Projektdatei enthält keine .gbx-Quelldateien.');
    const name = existing?.name || path.basename(resolved, `.${PROJECT_EXT}`);
    const project = {
      ...defaultProject(name, sources),
      ...(existing || {}),
      ...parsed,
      name,
      sources,
      openFiles: existing?.openFiles?.filter((file) => sources.includes(file)) || [parsed.entry].filter((file) => sources.includes(file))
    };
    if (!project.openFiles.length) project.openFiles = [project.entry].filter((file) => sources.includes(file));
    if (!project.openFiles.length) project.openFiles = [sources[0]];
    await writeMeta(dir, project);
    await addRecent(dir, project.name);
    return { project, dir };
  }

  const entries = await fsp.readdir(resolved);
  const projectFile = entries.find((f) => new RegExp(`\\.${PROJECT_EXT}$`, 'i').test(f));
  let parsed = {};
  if (projectFile) {
    parsed = parseProjectFile(await fsp.readFile(path.join(resolved, projectFile), 'utf8'));
  }
  const meta = await readMeta(resolved);
  const sources = await findSources(resolved);
  if (!sources.length) throw new Error('In diesem Ordner wurde kein GBX-Projekt gefunden.');
  const name = meta?.name || (projectFile ? path.basename(projectFile, `.${PROJECT_EXT}`) : path.basename(resolved));
  // Projekt-Einstellungen kommen aus der .gbxproj (auch wenn sie von Hand
  // geändert wurde); der Editorzustand (offene Reiter) stammt aus der Metadatei.
  const project = { ...defaultProject(name, sources), ...(meta || {}), ...parsed, name, sources };
  if (!readEntry(project, resolved)) project.entry = sources.includes(project.entry) ? project.entry : sources[0];
  if (!project.openFiles) project.openFiles = [];
  project.sources = sources;
  project.openFiles = (project.openFiles || []).filter((file) => sources.includes(file));
  if (!project.openFiles.length) project.openFiles = [project.entry];
  await writeMeta(resolved, project);
  await addRecent(resolved, project.name);
  return { project, dir: resolved };
}

const TEMPLATES = {
  hello: (name) => `' -------------------------------------------------------------
' ${name}
' Erstellt mit GBX Studio
' -------------------------------------------------------------

SETSCREEN 800, 600
SETWINDOWTITLE "${name}"

GLOBAL x, y, mx, my, mb1, mb2

x = 400
y = 300

WHILE TRUE
    CLEARSCREEN RGB(20, 24, 32)
    DRAWRECT 60, 60, 680, 480, RGB(35, 42, 56)
    DRAWBORDER 60, 60, 680, 480, RGB(90, 120, 200)
    DRAWCIRCLE x, y, 40, RGB(240, 180, 60)
    PRINT "Hallo GameBasiX!", x - 70, y + 70
    PRINT "F5 startet, F7 prueft die Syntax.", 80, 500
    SHOWSCREEN
    MOUSESTATE mx, my, mb1, mb2
    IF mb1 THEN x = mx
    IF mb1 THEN y = my
    IF KEYHIT(KEY_ESC) THEN BREAK
WEND

END
`,
  empty: (name) => `' ${name}
' Leeres GBX-Projekt

SETSCREEN 640, 480

WHILE TRUE
    CLEARSCREEN RGB(0, 0, 0)
    PRINT "Bereit.", 20, 20
    SHOWSCREEN
    IF KEYHIT(KEY_ESC) THEN BREAK
WEND

END
`,
  shapes: (name) => `' ${name}
' Formen, Farben und Text

SETSCREEN 800, 600
CONSTANT HINTERGRUND = RGB(18, 20, 26)

GLOBAL t#

WHILE TRUE
    CLEARSCREEN HINTERGRUND
    t = t + 0.02
    FOR i = 0 TO 11
        winkel# = i * 30 + t * 40
        x# = 400 + COS(winkel#) * 180
        y# = 300 + SIN(winkel#) * 180
        DRAWFILLEDCIRCLE x#, y#, 26, RGB(60 + i * 15, 120, 220 - i * 10)
    NEXT
    DRAWLINE 0, 300, 800, 300, RGB(70, 80, 100)
    PRINT "GameBasiX - Formen", 24, 24
    SHOWSCREEN
    IF KEYHIT(KEY_ESC) THEN BREAK
WEND

END
`,
  sprites: (name) => `' ${name}
' Sprites laden und bewegen
' Bilder in den Ordner assets/ legen (PNG, JPG oder BMP).

SETSCREEN 800, 600

GLOBAL x#, y#, vx#, vy#, mx, my, mb1, mb2

LOADSPRITE "assets/sprite.png", 0
x# = 400
y# = 300
vx# = 3.5
vy# = 2.5

WHILE TRUE
    CLEARSCREEN RGB(16, 18, 24)
    x# = x# + vx#
    y# = y# + vy#
    IF x# < 32 OR x# > 768 THEN vx# = -vx#
    IF y# < 32 OR y# > 568 THEN vy# = -vy#
    ROTOZOOMSPRITE 0, x#, y#, x# * 0.25, 100
    PRINT "Maus bewegt den Sprite, ESC beendet.", 20, 20
    SHOWSCREEN
    MOUSESTATE mx, my, mb1, mb2
    IF mb1 THEN x# = mx
    IF mb1 THEN y# = my
    IF KEYHIT(KEY_ESC) THEN BREAK
WEND

END
`
};

export async function createProject(parentDir, name, template = 'hello') {
  const cleanName = sanitizeProjectName(name);
  if (!cleanName) throw new Error('Bitte einen gültigen Projektnamen angeben.');
  const dir = path.join(path.resolve(parentDir || '.'), cleanName);
  if (fs.existsSync(dir)) {
    const entries = await fsp.readdir(dir);
    if (entries.length) throw new Error(`Der Ordner existiert bereits und ist nicht leer: ${dir}`);
  }
  const userTemplate = String(template || '').startsWith('user:')
    ? await readTemplate(String(template).slice('user:'.length))
    : null;
  const sourceName = `main.${SOURCE_EXT}`;
  const project = defaultProject(cleanName, [sourceName]);
  const files = [];
  if (userTemplate) {
    for (const entry of userTemplate.files || []) {
      const rel = String(entry?.path || '').replace(/\\/g, '/').replace(/^\.\//, '');
      if (!rel || typeof entry.content !== 'string') continue;
      if (!isInside(dir, path.join(dir, rel))) throw new Error(`Ungültiger Pfad in der Vorlage: ${rel}`);
      files.push({ rel, content: entry.content });
    }
    const sources = files.map((file) => file.rel).filter((rel) => new RegExp(`\\.${SOURCE_EXT}$`, 'i').test(rel));
    if (!sources.length) throw new Error('Die Vorlage enthält keine GBX-Quelldateien.');
    project.sources = sources;
    project.openFiles = [sources[0]];
    project.entry = sources[0];
    const settings = userTemplate.project || {};
    if (Number(settings.width) >= 64) project.width = Math.round(Number(settings.width));
    if (Number(settings.height) >= 64) project.height = Math.round(Number(settings.height));
    project.fullscreen = !!settings.fullscreen;
    project.resizable = settings.resizable !== false;
    if (Number(settings.frameRate) >= 0) project.frameRate = Math.round(Number(settings.frameRate));
    if (typeof settings.title === 'string' && settings.title.trim()) project.title = settings.title.trim().slice(0, 120);
  } else {
    const fn = TEMPLATES[template] || TEMPLATES.hello;
    files.push({ rel: sourceName, content: fn(cleanName) });
  }
  await fsp.mkdir(path.join(dir, 'assets'), { recursive: true });
  for (const file of files) {
    const target = path.join(dir, file.rel);
    await fsp.mkdir(path.dirname(target), { recursive: true });
    await fsp.writeFile(target, file.content.replace(/\r\n?/g, '\n'), 'utf8');
  }
  await writeMeta(dir, project);
  await writeProjectFile(project, dir);
  await addRecent(dir, cleanName);
  return { project, dir };
}

/* ------------------------------------------------------------------ */
/* Eigene Vorlagen (Dateien im Konfigurationsordner)                   */
/* ------------------------------------------------------------------ */

export const TEMPLATE_FORMAT = 'gbx-studio-template';
const TEMPLATE_EXT = '.gbxtpl.json';

export function templatesDir() {
  return path.join(configDir(), 'templates');
}

function templateFileName(id) {
  const safe = String(id || '').trim();
  if (!/^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$/.test(safe)) throw new Error('Unbekannte Vorlage.');
  return path.join(templatesDir(), `${safe}${TEMPLATE_EXT}`);
}

function templateSlug(name) {
  const slug = String(name || '')
    .replace(/ä/gi, 'ae')
    .replace(/ö/gi, 'oe')
    .replace(/ü/gi, 'ue')
    .replace(/ß/g, 'ss')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 60);
  return slug || 'vorlage';
}

async function readTemplate(id) {
  const file = templateFileName(id);
  let raw;
  try {
    raw = await fsp.readFile(file, 'utf8');
  } catch {
    throw new Error('Vorlage nicht gefunden.');
  }
  let data;
  try {
    data = JSON.parse(raw);
  } catch {
    throw new Error('Die Vorlage ist beschädigt.');
  }
  if (data?.format !== TEMPLATE_FORMAT) throw new Error('Das ist keine GBX-Studio-Vorlage.');
  return { ...data, id: path.basename(file, TEMPLATE_EXT) };
}

export async function listTemplates() {
  const dir = templatesDir();
  let entries;
  try {
    entries = await fsp.readdir(dir);
  } catch {
    return [];
  }
  const templates = [];
  for (const file of entries) {
    if (!file.endsWith(TEMPLATE_EXT)) continue;
    const id = file.slice(0, -TEMPLATE_EXT.length);
    try {
      const data = JSON.parse(await fsp.readFile(path.join(dir, file), 'utf8'));
      if (data?.format !== TEMPLATE_FORMAT) continue;
      templates.push({
        id,
        name: String(data.name || id),
        description: String(data.description || ''),
        files: (Array.isArray(data.files) ? data.files : []).map((entry) => String(entry?.path || '')).filter(Boolean),
        updatedAt: String(data.updatedAt || data.createdAt || '')
      });
    } catch {
      /* Beschädigte Dateien überspringen. */
    }
  }
  return templates.sort((a, b) => a.name.localeCompare(b.name, 'de'));
}

/** Speichert die Quelldateien und Einstellungen eines Projekts als Vorlage. */
export async function saveTemplate(projectDir, name, description = '') {
  const dir = path.resolve(projectDir || '');
  const meta = await readMeta(dir);
  if (!meta) throw new Error('Bitte zuerst ein Projekt öffnen.');
  const label = sanitizeProjectName(name || meta.name);
  if (!label) throw new Error('Bitte einen Namen für die Vorlage angeben.');
  const files = [];
  for (const entry of meta.sources || []) {
    const rel = String(entry || '').replace(/\\/g, '/');
    if (!rel) continue;
    const target = path.join(dir, rel);
    if (!isInside(dir, target)) continue;
    try {
      files.push({ path: rel, content: await fsp.readFile(target, 'utf8') });
    } catch {
      /* Fehlende Quelle überspringen. */
    }
  }
  if (!files.length) throw new Error('Das Projekt enthält keine Quelldateien.');
  const id = templateSlug(label);
  const file = templateFileName(id);
  let createdAt = new Date().toISOString();
  try {
    const existing = JSON.parse(await fsp.readFile(file, 'utf8'));
    if (existing?.createdAt) createdAt = existing.createdAt;
  } catch {
    /* Neue Vorlage. */
  }
  const template = {
    format: TEMPLATE_FORMAT,
    version: 1,
    name: label,
    description: String(description || '').trim().slice(0, 200),
    createdAt,
    updatedAt: new Date().toISOString(),
    project: {
      width: Number(meta.width) || 640,
      height: Number(meta.height) || 480,
      fullscreen: !!meta.fullscreen,
      resizable: meta.resizable !== false,
      frameRate: Number(meta.frameRate) || 60,
      title: meta.title || label
    },
    files
  };
  await fsp.mkdir(templatesDir(), { recursive: true });
  await fsp.writeFile(file, JSON.stringify(template, null, 2), 'utf8');
  return { id, name: label, path: file, files: files.map((entry) => entry.path) };
}

export async function deleteTemplate(id) {
  const file = templateFileName(id);
  try {
    await fsp.rm(file);
  } catch {
    throw new Error('Vorlage nicht gefunden.');
  }
  return { id };
}

export async function saveProject(project, projectDir) {
  const dir = path.resolve(projectDir);
  const meta = { ...project };
  meta.sources = [...new Set((meta.sources || []).filter(Boolean))];
  for (const source of meta.sources) {
    if (!isInside(dir, path.join(dir, source))) throw new Error('Ungültiger Quelldateipfad.');
  }
  await writeMeta(dir, meta);
  await writeProjectFile(meta, dir);
  return meta;
}

export async function createFile(projectDir, relativePath, initial = '') {
  const dir = path.resolve(projectDir);
  const target = path.join(dir, relativePath);
  if (!isInside(dir, target)) throw new Error('Der Pfad liegt außerhalb des Projekts.');
  await fsp.mkdir(path.dirname(target), { recursive: true });
  if (fs.existsSync(target)) throw new Error(`Datei existiert bereits: ${relativePath}`);
  await fsp.writeFile(target, toSourceLineEndings(initial), 'utf8');
  return relativePath;
}

export async function createFolder(projectDir, relativePath) {
  const dir = path.resolve(projectDir);
  const target = path.join(dir, relativePath);
  if (!isInside(dir, target)) throw new Error('Der Pfad liegt außerhalb des Projekts.');
  await fsp.mkdir(target, { recursive: true });
  return relativePath;
}

/** Löscht eine Datei im Projekt. Ordner und Pfade außerhalb werden abgelehnt. */
export async function deleteFile(projectDir, relativePath) {
  const dir = path.resolve(projectDir);
  const target = path.resolve(dir, relativePath);
  if (!relativePath || target === dir) throw new Error('Kein Dateiname angegeben.');
  if (!isInside(dir, target)) throw new Error('Der Pfad liegt außerhalb des Projekts.');
  const stat = await fsp.stat(target).catch(() => null);
  if (!stat) throw new Error(`Datei nicht gefunden: ${relativePath}`);
  if (!stat.isFile()) throw new Error(`Das ist keine Datei: ${relativePath}`);
  await fsp.rm(target, { force: true });
  return relativePath;
}

/** Löscht einen Ordner samt Inhalt. Der Projektordner selbst bleibt bestehen. */
export async function deleteFolder(projectDir, relativePath) {
  const dir = path.resolve(projectDir);
  const target = path.resolve(dir, relativePath);
  if (!relativePath || target === dir) throw new Error('Kein Ordnername angegeben.');
  if (!isInside(dir, target)) throw new Error('Der Pfad liegt außerhalb des Projekts.');
  const stat = await fsp.stat(target).catch(() => null);
  if (!stat) throw new Error(`Ordner nicht gefunden: ${relativePath}`);
  if (!stat.isDirectory()) throw new Error(`Das ist kein Ordner: ${relativePath}`);
  await fsp.rm(target, { recursive: true, force: true });
  return relativePath;
}

/**
 * Verschiebt eine Datei oder einen Ordner innerhalb des Projekts. Der neue
 * Pfad wird nicht überschrieben – ein vorhandenes Ziel ist ein Fehler.
 */
export async function moveEntry(projectDir, from, to) {
  const dir = path.resolve(projectDir);
  if (!from || !to) throw new Error('Quelle und Ziel sind erforderlich.');
  const source = path.resolve(dir, String(from));
  const target = path.resolve(dir, String(to));
  if (source === dir || target === dir) throw new Error('Der Projektordner selbst lässt sich nicht verschieben.');
  if (!isInside(dir, source) || !isInside(dir, target)) throw new Error('Der Pfad liegt außerhalb des Projekts.');
  if (source === target) return { from, to };
  if (target.startsWith(`${source}${path.sep}`)) throw new Error('Ein Ordner lässt sich nicht in sich selbst verschieben.');
  const stat = await fsp.stat(source).catch(() => null);
  if (!stat) throw new Error(`Nicht gefunden: ${from}`);
  if (fs.existsSync(target)) throw new Error(`Das Ziel existiert bereits: ${to}`);
  await fsp.mkdir(path.dirname(target), { recursive: true });
  await fsp.rename(source, target);
  return { from, to };
}

export async function readProjectFile(projectDir, relativePath) {
  const dir = path.resolve(projectDir);
  const target = path.join(dir, relativePath);
  if (!isInside(dir, target)) throw new Error('Der Pfad liegt außerhalb des Projekts.');
  const buffer = await fsp.readFile(target);
  let text;
  try {
    text = new TextDecoder('utf-8', { fatal: true }).decode(buffer);
  } catch {
    text = buffer.toString('latin1');
  }
  return text.replace(/^\uFEFF/, '');
}

// Quelldateien immer mit LF speichern; gbx wertet Zeilenenden nicht aus.
export function toSourceLineEndings(content) {
  return String(content ?? '').replace(/\r\n?/g, '\n');
}

export async function writeProjectFileContent(projectDir, relativePath, content) {
  const dir = path.resolve(projectDir);
  const target = path.join(dir, relativePath);
  if (!isInside(dir, target)) throw new Error('Der Pfad liegt außerhalb des Projekts.');
  await fsp.mkdir(path.dirname(target), { recursive: true });
  const data = toSourceLineEndings(content);
  await fsp.writeFile(target, data, 'utf8');
  return { path: target, size: Buffer.byteLength(data, 'utf8') };
}

const IGNORED = new Set(['.git', '.gbxide', 'build', 'node_modules', 'dist', 'distribute', '.vscode', '.idea', '__pycache__']);

export async function listTree(projectDir) {
  const root = path.resolve(projectDir);
  const items = [];
  async function walk(dir, rel) {
    let entries;
    try {
      entries = await fsp.readdir(dir, { withFileTypes: true });
    } catch {
      return;
    }
    entries.sort((a, b) => {
      if (a.isDirectory() !== b.isDirectory()) return a.isDirectory() ? -1 : 1;
      return a.name.localeCompare(b.name, 'de');
    });
    for (const entry of entries) {
      if (IGNORED.has(entry.name)) continue;
      // Die Projektdatei beschreibt nur das Projekt und wird über „Projekt öffnen“ geladen.
      if (new RegExp(`\\.${PROJECT_EXT}$`, 'i').test(entry.name)) continue;
      const abs = path.join(dir, entry.name);
      const relative = rel ? `${rel}/${entry.name}` : entry.name;
      if (entry.isDirectory()) {
        items.push({ type: 'dir', name: entry.name, path: relative });
        await walk(abs, relative);
      } else {
        const stat = await fsp.stat(abs).catch(() => null);
        items.push({
          type: 'file',
          name: entry.name,
          path: relative,
          size: stat?.size || 0,
          ext: path.extname(entry.name).toLowerCase()
        });
      }
    }
  }
  await walk(root, '');
  return items;
}

export async function ensureDir(dir) {
  await fsp.mkdir(dir, { recursive: true });
  return dir;
}
