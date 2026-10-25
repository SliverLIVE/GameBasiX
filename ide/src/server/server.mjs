import http from 'node:http';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { randomUUID } from 'node:crypto';
import { spawn } from 'node:child_process';
import { checkProject, checkSource, exportGame, inspectGbx, inspectCompiler, runProject, startDebugSession, defaultGbxPath, defaultCxxPath, resolveTool, setCompilerPath, pathExists } from './gbx.mjs';
import { compilerRoot, installCompiler } from './cxx.mjs';
import { downloadGoogleFont, fetchGoogleFontFile, listGoogleFonts } from './googlefonts.mjs';
import { inspectEmsdk, defaultEmsdkPath, emsdkRoot, installEmsdk } from './emsdk.mjs';
import { buildPlayer, exportHtml5, inspectHtml5 } from './html5.mjs';
import { setLanguage, tr } from './locale.mjs';
import {
  createFile,
  createProject,
  deleteFile,
  deleteFolder,
  deleteTemplate,
  listTemplates,
  listTree,
  moveEntry,
  loadConfig,
  openProject,
  projectPaths,
  readProjectFile,
  createFolder,
  sanitizeAppearance,
  sanitizeEditorTheme,
  saveConfig,
  saveProject,
  saveTemplate,
  scanInlineCommands,
  templatesDir,
  writeProjectFileContent,
  isInside
} from './project.mjs';

// Öffnet einen Ordner (oder markiert eine Datei) im Dateimanager des Systems.
// In der Desktop-Variante erledigt das Electron über shell.openPath; hierfür
// gibt es diesen Weg für die WebStart-Variante.
async function revealInFileManager(target) {
  const stat = await fsp.stat(target);
  const isDir = stat.isDirectory();
  let command;
  let args;
  if (process.platform === 'win32') {
    command = 'explorer.exe';
    args = isDir ? [target] : ['/select,', target];
  } else if (process.platform === 'darwin') {
    command = 'open';
    args = isDir ? [target] : ['-R', target];
  } else {
    command = 'xdg-open';
    args = [isDir ? target : path.dirname(target)];
  }
  await new Promise((resolve, reject) => {
    const child = spawn(command, args, { detached: true, stdio: 'ignore' });
    child.once('error', reject);
    child.once('spawn', () => {
      child.unref();
      resolve();
    });
  });
}

const MODULE_DIR = (() => {
  try {
    return path.dirname(fileURLToPath(import.meta.url));
  } catch {
    // In einem gebündelten CJS-Build fehlt import.meta.url.
    return process.cwd();
  }
})();
const ROOT = process.env.GBXIDE_ROOT ? path.resolve(process.env.GBXIDE_ROOT) : path.resolve(MODULE_DIR, '..', '..');
const RENDERER_DIR = path.join(ROOT, 'src', 'renderer');
const MONACO_DIR = path.join(ROOT, 'node_modules', 'monaco-editor', 'min');
const DATA_DIR = path.join(ROOT, 'data');
const ASSETS_DIR = path.join(ROOT, 'assets');

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.svg': 'image/svg+xml',
  '.webp': 'image/webp',
  '.bmp': 'image/bmp',
  '.ico': 'image/x-icon',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
  '.ttf': 'font/ttf',
  '.map': 'application/json',
  '.wav': 'audio/wav',
  '.ogg': 'audio/ogg',
  '.oga': 'audio/ogg',
  '.mp3': 'audio/mpeg',
  '.m4a': 'audio/mp4',
  '.flac': 'audio/flac',
  '.opus': 'audio/ogg'
};

// Bilder und Klänge, die die Vorschau in der IDE direkt anzeigen kann.
const MEDIA_EXTENSIONS = new Set(['.png', '.jpg', '.jpeg', '.gif', '.bmp', '.webp', '.svg', '.wav', '.ogg', '.oga', '.mp3', '.m4a', '.flac', '.opus']);

const jobs = new Map();

function publicConfig(config) {
  // Der API-Key bleibt im Server; die Oberfläche erfährt nur, ob einer gesetzt ist.
  const { licenseContent, licensePath, licenseEnabled, sdkPath, googleFontsApiKey, ...rest } = config || {};
  return { ...rest, googleFontsKeySet: Boolean(googleFontsApiKey) };
}

function json(res, status, value) {
  const body = JSON.stringify(value);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    'Content-Length': Buffer.byteLength(body)
  });
  res.end(body);
}

function fail(res, error, status = 400) {
  json(res, status, { ok: false, error: errorText(error) });
}

async function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > 30 * 1024 * 1024) {
        reject(new Error('Anfrage zu groß.'));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => {
      if (!chunks.length) return resolve({});
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString('utf8')));
      } catch {
        reject(new Error('Ungültiges JSON.'));
      }
    });
    req.on('error', reject);
  });
}

async function serveStatic(res, file, req) {
  let target = file;
  try {
    const stat = await fsp.stat(target);
    if (stat.isDirectory()) target = path.join(target, 'index.html');
  } catch {
    res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('Nicht gefunden');
    return;
  }
  const ext = path.extname(target).toLowerCase();
  const headers = {
    'Content-Type': MIME[ext] || 'application/octet-stream',
    'X-Content-Type-Options': 'nosniff'
  };
  // Die App-Hülle (HTML/JS/CSS) muss nach einem Update sofort greifen;
  // nur unveränderliche Assets (Bilder, Fonts, Monaco-Chunks) dürfen cachen.
  if (['.html', '.htm', '.js', '.mjs', '.css', '.json'].includes(ext)) {
    headers['Cache-Control'] = 'no-cache';
  } else {
    headers['Cache-Control'] = 'public, max-age=3600';
  }
  if (req.method === 'HEAD') {
    res.writeHead(200, headers);
    res.end();
    return;
  }
  try {
    const data = await fsp.readFile(target);
    headers['Content-Length'] = data.length;
    res.writeHead(200, headers);
    res.end(data);
  } catch {
    res.writeHead(500, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('Lesefehler');
  }
}

function createJob(type, meta = {}) {
  const id = randomUUID();
  const job = {
    id,
    type,
    status: 'running',
    createdAt: Date.now(),
    events: [],
    subscribers: new Set(),
    child: null,
    meta,
    emit(event) {
      // Alle Job-Meldungen laufen hier durch: Log-Texte und Fehlermeldungen
      // erscheinen so in der gerade eingestellten Sprache.
      const shaped =
        event?.type === 'log' && typeof event.text === 'string'
          ? { ...event, text: tr(event.text) }
          : event?.type === 'done' && typeof event.error === 'string'
            ? { ...event, error: tr(event.error) }
            : event;
      if (this.events.length < 6000) this.events.push(shaped);
      const payload = `data: ${JSON.stringify(shaped)}\n\n`;
      for (const res of this.subscribers) {
        try {
          res.write(payload);
        } catch {
          this.subscribers.delete(res);
        }
      }
    },
    finish(status, extra = {}) {
      this.status = status;
      this.emit({ type: 'done', status, ...extra });
    }
  };
  jobs.set(id, job);
  if (jobs.size > 30) {
    const oldest = [...jobs.values()].sort((a, b) => a.createdAt - b.createdAt)[0];
    if (oldest && oldest.status !== 'running') jobs.delete(oldest.id);
  }
  return job;
}

function streamJob(req, res, job) {
  res.writeHead(200, {
    'Content-Type': 'text/event-stream; charset=utf-8',
    'Cache-Control': 'no-cache, no-transform',
    Connection: 'keep-alive',
    'X-Accel-Buffering': 'no'
  });
  res.write(`retry: 2000\n\n`);
  for (const event of job.events) res.write(`data: ${JSON.stringify(event)}\n\n`);
  job.subscribers.add(res);
  const heartbeat = setInterval(() => {
    try {
      res.write(': ping\n\n');
    } catch {
      clearInterval(heartbeat);
    }
  }, 15000);
  req.on('close', () => {
    clearInterval(heartbeat);
    job.subscribers.delete(res);
  });
}

function jobChildHandler(job) {
  return (child) => {
    job.child = child;
  };
}

function errorText(error) {
  return tr(String(error?.message || error || 'Unbekannter Fehler'));
}

/**
 * Compiler für INLINE setzen. Nur wenn der Pfad wirklich existiert, sonst
 * sucht der Interpreter selbst (mitgelieferter Compiler, PATH, MSYS2).
 */
function applyCompiler(config) {
  const resolved = resolveTool(config.cxxPath || defaultCxxPath(), ROOT);
  setCompilerPath(pathExists(resolved) ? resolved : '');
}

/** Zustand von Interpreter und Konfiguration für /api/health. */
async function runtimeInfo() {
  const config = await loadConfig();
  applyCompiler(config);
  return {
    config: publicConfig(config),
    gbx: inspectGbx(config.gbxPath, ROOT),
    cxx: inspectCompiler(config.cxxPath, ROOT),
    emsdk: inspectEmsdk(config.emsdkPath, ROOT),
    html5: inspectHtml5(ROOT)
  };
}

/** Laufwerke für den Ordner-Dialog (Windows) bzw. die Wurzel. */
function folderRoots() {
  if (process.platform !== 'win32') return ['/'];
  const roots = [];
  for (let code = 65; code <= 90; code += 1) {
    const root = `${String.fromCharCode(code)}:\\`;
    if (fs.existsSync(root)) roots.push(root);
  }
  return roots;
}

/**
 * Ordner und Projektdateien für den Auswahl-Dialog der Web-Variante.
 * Ohne Angabe beginnt die Suche beim letzten Projekt, sonst im Dokumente-Ordner.
 */
async function listFolders(requested) {
  const config = await loadConfig();
  const home = os.homedir();
  const candidates = [
    requested,
    config.lastProject,
    config.lastProject ? path.dirname(config.lastProject) : '',
    path.join(home, 'Documents', 'GBX'),
    home
  ];
  let dir = '';
  for (const candidate of candidates) {
    if (!candidate) continue;
    try {
      const resolved = path.resolve(String(candidate));
      const stat = await fsp.stat(resolved);
      dir = stat.isDirectory() ? resolved : path.dirname(resolved);
      break;
    } catch {
      // nächster Kandidat
    }
  }
  if (!dir) dir = home;

  let entries;
  try {
    entries = await fsp.readdir(dir, { withFileTypes: true });
  } catch {
    throw new Error(`Ordner nicht lesbar: ${dir}`);
  }

  const folders = [];
  const files = [];
  for (const entry of entries) {
    if (entry.name.startsWith('.') || entry.name.startsWith('$') || entry.name === 'node_modules') continue;
    const full = path.join(dir, entry.name);
    let isDir = entry.isDirectory();
    if (entry.isSymbolicLink()) {
      try {
        isDir = (await fsp.stat(full)).isDirectory();
      } catch {
        continue;
      }
    }
    if (isDir) folders.push({ name: entry.name, path: full });
    else if (/\.(gbxproj|gbx)$/i.test(entry.name)) files.push({ name: entry.name, path: full });
  }
  const byName = (a, b) => a.name.localeCompare(b.name, 'de', { sensitivity: 'base' });
  folders.sort(byName);
  files.sort(byName);

  const parent = path.dirname(dir);
  return {
    path: dir,
    parent: parent === dir ? '' : parent,
    roots: folderRoots(),
    folders,
    files
  };
}

async function routeApi(req, res, url) {
  const { pathname } = url;
  const method = req.method || 'GET';

  if (pathname === '/api/health') {
    const info = await runtimeInfo();
    return json(res, 200, {
      ok: true,
      app: 'GBX Studio',
      config: info.config,
      gbx: info.gbx,
      cxx: info.cxx,
      emsdk: info.emsdk,
      html5: info.html5,
      defaultProjectParent: path.join(os.homedir(), 'Documents', 'GBX')
    });
  }

  if (pathname === '/api/settings' && method === 'GET') {
    const info = await runtimeInfo();
    return json(res, 200, { ...info.config, gbx: info.gbx, cxx: info.cxx, emsdk: info.emsdk, html5: info.html5 });
  }

  if (pathname === '/api/settings' && method === 'POST') {
    const body = await readBody(req);
    const current = await loadConfig();
    const next = { ...current };
    if (body.gbxPath !== undefined) {
      // Relative Pfade bleiben relativ – sie gelten ab der Editor-EXE.
      next.gbxPath = String(body.gbxPath || '').trim().replace(/^"|"$/g, '');
    }
    if (body.lastProject !== undefined) next.lastProject = body.lastProject;
    if (body.language === 'de' || body.language === 'en') next.language = body.language;
    if (body.editorTheme !== undefined) next.editorTheme = sanitizeEditorTheme(body.editorTheme);
    if (body.appearance !== undefined) next.appearance = sanitizeAppearance(body.appearance);
    if (body.cxxPath !== undefined) {
      next.cxxPath = String(body.cxxPath || '').trim().replace(/^"|"$/g, '');
    }
    if (body.emsdkPath !== undefined) {
      next.emsdkPath = String(body.emsdkPath || '').trim().replace(/^"|"$/g, '');
    }
    if (body.googleFontsApiKey !== undefined) {
      next.googleFontsApiKey = String(body.googleFontsApiKey || '').trim();
    }
    if (body.clearGoogleFontsApiKey) delete next.googleFontsApiKey;
    await saveConfig(next);
    // Die Oberfläche erwartet, dass ein Sprachwechsel sofort für alle
    // folgenden Meldungen und Job-Logs gilt.
    setLanguage(next.language);
    applyCompiler(next);
    return json(res, 200, {
      ok: true,
      gbx: inspectGbx(next.gbxPath, ROOT),
      cxx: inspectCompiler(next.cxxPath, ROOT),
      emsdk: inspectEmsdk(next.emsdkPath, ROOT),
      html5: inspectHtml5(ROOT),
      config: publicConfig(next)
    });
  }

  if (pathname === '/api/gbx/check') {
    const body = method === 'POST' ? await readBody(req) : {};
    const config = await loadConfig();
    return json(res, 200, inspectGbx(body.path || config.gbxPath, ROOT));
  }

  // Ordnerbaum für „Projekt öffnen“ in der Browser-Variante; im
  // Desktop-Fenster übernimmt Electron den Auswahl-Dialog.
  if (pathname === '/api/fs/folders' && method === 'GET') {
    const body = await listFolders(url.searchParams.get('path') || '');
    return json(res, 200, body);
  }

  if (pathname === '/api/compiler/check') {
    const body = method === 'POST' ? await readBody(req) : {};
    const config = await loadConfig();
    return json(res, 200, inspectCompiler(body.path !== undefined ? body.path : config.cxxPath, ROOT));
  }

  // C++-Compiler nach compiler/ laden: derselbe Weg wie beim Emscripten-SDK,
  // damit INLINE ohne eine MSYS2-Installation laeuft.
  if (pathname === '/api/compiler/install' && method === 'POST') {
    const target = compilerRoot(ROOT);
    const job = createJob('compiler', {});
    json(res, 200, { ok: true, jobId: job.id, dir: target });
    (async () => {
      try {
        job.emit({ type: 'started', kind: 'compiler' });
        const ok = await installCompiler({
          root: target,
          onLog: (text) => job.emit({ type: 'log', level: 'info', text })
        });
        const config = await loadConfig();
        job.finish(ok ? 'success' : 'failed', { ok, cxx: inspectCompiler(config.cxxPath, ROOT) });
      } catch (error) {
        job.emit({ type: 'log', level: 'error', text: `${errorText(error)}\n` });
        job.finish('failed', { ok: false, error: errorText(error) });
      }
    })();
    return;
  }

  if (pathname === '/api/emsdk/check') {
    const body = method === 'POST' ? await readBody(req) : {};
    const config = await loadConfig();
    return json(res, 200, inspectEmsdk(body.path !== undefined ? body.path : config.emsdkPath, ROOT));
  }

  // Emscripten SDK herunterladen und einrichten (dauert einige Minuten).
  if (pathname === '/api/emsdk/install' && method === 'POST') {
    const body = await readBody(req);
    const config = await loadConfig();
    const target = emsdkRoot(body.path !== undefined ? body.path : config.emsdkPath, ROOT);
    const job = createJob('emsdk', {});
    json(res, 200, { ok: true, jobId: job.id, dir: target });
    (async () => {
      try {
        job.emit({ type: 'started', kind: 'emsdk' });
        const ok = await installEmsdk({
          root: target,
          onLog: (text) => job.emit({ type: 'log', level: 'info', text }),
          onChild: jobChildHandler(job)
        });
        job.emit({ type: 'log', level: ok ? 'info' : 'error', text: ok ? 'emsdk ist bereit.' : 'Die Installation ist nicht abgeschlossen.' });
        job.finish(ok ? 'success' : 'failed', { ok, emsdk: inspectEmsdk(target, ROOT) });
      } catch (error) {
        job.emit({ type: 'log', level: 'error', text: `${errorText(error)}\n` });
        job.finish('failed', { ok: false, error: errorText(error) });
      }
    })();
    return;
  }

  // Interpreter als WebAssembly bauen (einmalig, danach nur noch kopieren).
  if (pathname === '/api/html5/build' && method === 'POST') {
    const body = await readBody(req);
    const config = await loadConfig();
    const info = inspectEmsdk(body.emsdkPath !== undefined ? body.emsdkPath : config.emsdkPath, ROOT);
    if (!info.ok) return fail(res, new Error(info.error));
    const job = createJob('html5', {});
    json(res, 200, { ok: true, jobId: job.id });
    (async () => {
      try {
        job.emit({ type: 'started', kind: 'html5' });
        const result = await buildPlayer({
          appRoot: ROOT,
          emsdk: info.resolved,
          onLog: (text) => job.emit({ type: 'log', level: 'info', text }),
          onChild: jobChildHandler(job)
        });
        job.finish(result.ok ? 'success' : 'failed', { ...result, html5: inspectHtml5(ROOT) });
      } catch (error) {
        job.emit({ type: 'log', level: 'error', text: `${errorText(error)}\n` });
        job.finish('failed', { ok: false, error: errorText(error) });
      }
    })();
    return;
  }

  // Projekt als HTML5-Ordner schreiben (index.html, gbx.js, game.js).
  if (pathname === '/api/html5/export' && method === 'POST') {
    const body = await readBody(req);
    if (!body.projectDir || !body.project) return fail(res, new Error('Projektdaten fehlen.'));
    // INLINE wird beim Export mit Emscripten in den Player übersetzt.
    const config = await loadConfig();
    const gbxInfo = inspectGbx(config.gbxPath, ROOT);
    const emsdkInfo = inspectEmsdk(config.emsdkPath, ROOT);
    const job = createJob('html5-export', { projectDir: body.projectDir });
    json(res, 200, { ok: true, jobId: job.id });
    (async () => {
      try {
        const project = await saveProject(body.project, body.projectDir);
        job.emit({ type: 'started', kind: 'html5-export' });
        const result = await exportHtml5({
          appRoot: ROOT,
          projectDir: body.projectDir,
          project,
          target: body.target,
          gbx: gbxInfo.ok ? gbxInfo.resolved : '',
          emsdk: emsdkInfo.ok ? emsdkInfo.resolved : '',
          emsdkVersion: emsdkInfo.version || '',
          onLog: (text) => job.emit({ type: 'log', level: 'info', text })
        });
        job.finish(result.ok ? 'success' : 'failed', result);
      } catch (error) {
        job.emit({ type: 'log', level: 'error', text: `${errorText(error)}\n` });
        job.finish('failed', { ok: false, error: errorText(error) });
      }
    })();
    return;
  }

  // Live-Prüfung: prüft den ungespeicherten Editorinhalt einer Datei, ohne
  // sie zu schreiben.
  if (pathname === '/api/check/live' && method === 'POST') {
    const body = await readBody(req);
    const projectDir = String(body.projectDir || '').trim();
    const file = String(body.file || '').trim();
    if (!projectDir || !file) return fail(res, new Error('projectDir und file sind erforderlich.'));
    const root = path.resolve(projectDir);
    if (!isInside(root, path.resolve(root, file))) {
      return fail(res, new Error('Der Pfad liegt außerhalb des Projekts.'), 403);
    }
    const config = await loadConfig();
    const result = await checkSource({
      gbx: config.gbxPath,
      projectDir: root,
      file,
      source: String(body.source ?? ''),
      // Teildateien kennen die Namen der übrigen Dateien nicht.
      syntaxOnly: body.syntaxOnly === true
    });
    // ok bleibt der Anfrage-Status; passed sagt, ob der Code in Ordnung ist.
    return json(res, 200, {
      ok: true,
      available: result.available,
      passed: result.ok,
      diagnostics: result.diagnostics,
      error: result.error
    });
  }

  if (pathname === '/api/templates' && method === 'GET') {
    return json(res, 200, { ok: true, dir: templatesDir(), templates: await listTemplates() });
  }

  if (pathname === '/api/templates' && method === 'POST') {
    const body = await readBody(req);
    try {
      const template = await saveTemplate(body.dir, body.name, body.description);
      return json(res, 200, { ok: true, template, templates: await listTemplates() });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/templates/delete' && method === 'POST') {
    const body = await readBody(req);
    try {
      await deleteTemplate(body.id);
      return json(res, 200, { ok: true, templates: await listTemplates() });
    } catch (error) {
      return fail(res, error);
    }
  }

  // Google Fonts: Liste (mit oder ohne API-Key), TTF für die Vorschau und
  // Download in den Asset-Ordner des geöffneten Projekts.
  if (pathname === '/api/googlefonts' && method === 'GET') {
    try {
      const config = await loadConfig();
      const data = await listGoogleFonts({
        apiKey: config.googleFontsApiKey || '',
        refresh: url.searchParams.get('refresh') === '1'
      });
      return json(res, 200, { ...data, warning: tr(data.warning), ok: true });
    } catch (error) {
      return fail(res, error, 502);
    }
  }

  if (pathname === '/api/googlefonts/preview' && method === 'GET') {
    try {
      const { buffer } = await fetchGoogleFontFile(url.searchParams.get('family') || '', {
        weight: Number(url.searchParams.get('weight')) || 400,
        style: url.searchParams.get('style') === 'italic' ? 'italic' : 'normal'
      });
      res.writeHead(200, {
        'Content-Type': 'font/ttf',
        'Content-Length': buffer.length,
        'Cache-Control': 'public, max-age=86400'
      });
      return res.end(buffer);
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/googlefonts/download' && method === 'POST') {
    const body = await readBody(req);
    try {
      const result = await downloadGoogleFont({
        projectDir: body.projectDir,
        dir: body.dir,
        family: body.family,
        weight: body.weight,
        style: body.style
      });
      return json(res, 200, { ok: true, ...result });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/dataset' && method === 'GET') {
    try {
      const data = await fsp.readFile(path.join(DATA_DIR, 'gbx-dataset.json'), 'utf8');
      res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'public, max-age=300' });
      return res.end(data);
    } catch {
      return json(res, 500, { ok: false, error: tr('Datensatz fehlt. Bitte npm run build:dataset ausführen.') });
    }
  }

  // Namen aus GBX_INLINE_FN(...): Der Editor faerbt sie wie eingebaute
  // Befehle und schlaegt sie vor. Ein Projekt ist klein, deshalb darf der
  // Scan nach jedem Speichern laufen.
  if (pathname === '/api/inline-commands' && method === 'GET') {
    const dir = url.searchParams.get('dir') || '';
    if (!dir) return fail(res, new Error('Projektordner fehlt.'));
    try {
      return json(res, 200, { ok: true, commands: await scanInlineCommands(dir) });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/project/open' && method === 'POST') {
    const body = await readBody(req);
    if (!body.path) return fail(res, new Error('Kein Pfad angegeben.'));
    try {
      const result = await openProject(body.path);
      const config = await loadConfig();
      await saveConfig({ ...config, lastProject: result.dir });
      const info = await runtimeInfo();
      return json(res, 200, {
        ok: true,
        ...result,
        tree: await listTree(result.dir),
        config: info.config,
        gbx: info.gbx
      });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/project/create' && method === 'POST') {
    const body = await readBody(req);
    try {
      const result = await createProject(body.path || '', body.name, body.template || 'hello');
      const config = await loadConfig();
      await saveConfig({ ...config, lastProject: result.dir });
      const info = await runtimeInfo();
      return json(res, 200, {
        ok: true,
        ...result,
        tree: await listTree(result.dir),
        config: info.config,
        gbx: info.gbx
      });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/project/save' && method === 'POST') {
    const body = await readBody(req);
    if (!body.projectDir || !body.project) return fail(res, new Error('Projektdaten fehlen.'));
    try {
      const project = await saveProject(body.project, body.projectDir);
      return json(res, 200, { ok: true, project });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/tree' && method === 'GET') {
    const dir = url.searchParams.get('dir');
    if (!dir) return fail(res, new Error('Kein Projektordner angegeben.'));
    try {
      return json(res, 200, { ok: true, tree: await listTree(dir) });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/file' && method === 'GET') {
    const dir = url.searchParams.get('dir');
    const file = url.searchParams.get('file');
    if (!dir || !file) return fail(res, new Error('dir und file sind erforderlich.'));
    try {
      const content = await readProjectFile(dir, file);
      return json(res, 200, { ok: true, content, file });
    } catch (error) {
      return fail(res, error);
    }
  }

  // Rohdaten für die Vorschau: Bilder und Klänge werden unverändert
  // ausgeliefert. Range-Anfragen sind nötig, damit man in längeren Songs
  // springen kann, ohne die ganze Datei zu laden.
  if (pathname === '/api/file/raw' && (method === 'GET' || method === 'HEAD')) {
    const dir = url.searchParams.get('dir');
    const file = url.searchParams.get('file');
    if (!dir || !file) return fail(res, new Error('dir und file sind erforderlich.'));
    const root = path.resolve(dir);
    const target = path.resolve(root, file);
    if (!isInside(root, target)) return fail(res, new Error('Der Pfad liegt außerhalb des Projekts.'), 403);
    const extension = path.extname(target).toLowerCase();
    if (!MEDIA_EXTENSIONS.has(extension)) {
      return fail(res, new Error('Dieser Dateityp lässt sich nicht als Vorschau anzeigen.'), 415);
    }
    let stat;
    try {
      stat = await fsp.stat(target);
    } catch {
      return fail(res, new Error('Datei nicht gefunden.'), 404);
    }
    if (!stat.isFile()) return fail(res, new Error('Datei nicht gefunden.'), 404);
    const headers = {
      'Content-Type': MIME[extension] || 'application/octet-stream',
      'Cache-Control': 'no-store',
      'Accept-Ranges': 'bytes',
      'X-Content-Type-Options': 'nosniff'
    };
    const range = String(req.headers.range || '').trim();
    const match = /^bytes=(\d*)-(\d*)$/.exec(range);
    let start = 0;
    let end = stat.size - 1;
    if (match) {
      const hasStart = match[1] !== '';
      const hasEnd = match[2] !== '';
      if (hasStart) start = Number(match[1]);
      if (hasEnd) end = Number(match[2]);
      if (!hasStart && hasEnd) {
        start = Math.max(0, stat.size - Number(match[2]));
        end = stat.size - 1;
      }
      if (!Number.isFinite(start) || !Number.isFinite(end) || start > end || start >= stat.size) {
        res.writeHead(416, { 'Content-Range': `bytes */${stat.size}` });
        return res.end();
      }
      end = Math.min(end, stat.size - 1);
      headers['Content-Range'] = `bytes ${start}-${end}/${stat.size}`;
      headers['Content-Length'] = end - start + 1;
      res.writeHead(206, headers);
      if (method === 'HEAD') return res.end();
      return fs.createReadStream(target, { start, end }).pipe(res);
    }
    headers['Content-Length'] = stat.size;
    res.writeHead(200, headers);
    if (method === 'HEAD') return res.end();
    return fs.createReadStream(target).pipe(res);
  }

  if (pathname === '/api/file' && method === 'POST') {
    const body = await readBody(req);
    if (!body.dir || !body.file) return fail(res, new Error('dir und file sind erforderlich.'));
    try {
      const result = await writeProjectFileContent(body.dir, body.file, body.content ?? '');
      return json(res, 200, { ok: true, ...result });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/file/create' && method === 'POST') {
    const body = await readBody(req);
    try {
      const file = await createFile(body.dir, body.file, body.content || '');
      return json(res, 200, { ok: true, file, tree: await listTree(body.dir) });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/folder/create' && method === 'POST') {
    const body = await readBody(req);
    try {
      const folder = await createFolder(body.dir, body.folder);
      return json(res, 200, { ok: true, folder, tree: await listTree(body.dir) });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/file/delete' && method === 'POST') {
    const body = await readBody(req);
    try {
      const file = await deleteFile(body.dir, body.file);
      return json(res, 200, { ok: true, file, tree: await listTree(body.dir) });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/folder/delete' && method === 'POST') {
    const body = await readBody(req);
    try {
      const folder = await deleteFolder(body.dir, body.folder);
      return json(res, 200, { ok: true, folder, tree: await listTree(body.dir) });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/move' && method === 'POST') {
    const body = await readBody(req);
    try {
      const moved = await moveEntry(body.dir, body.from, body.to);
      return json(res, 200, { ok: true, ...moved, tree: await listTree(body.dir) });
    } catch (error) {
      return fail(res, error);
    }
  }

  if (pathname === '/api/build' && method === 'POST') {
    const body = await readBody(req);
    if (!body.projectDir || !body.project) return fail(res, new Error('Projektdaten fehlen.'));
    const config = await loadConfig();
    const job = createJob('build', { projectDir: body.projectDir });
    json(res, 200, { ok: true, jobId: job.id });
    (async () => {
      try {
        const project = await saveProject(body.project, body.projectDir);
        job.emit({ type: 'started', kind: 'build' });
        const result = await checkProject({
          gbx: config.gbxPath,
          projectDir: body.projectDir,
          project,
          log: (event) => {
            if (event.level === 'title') job.emit({ type: 'title', text: event.text });
            else job.emit({ type: 'log', level: event.level, text: event.text });
          },
          onChild: jobChildHandler(job)
        });
        for (const diagnostic of result.diagnostics || []) job.emit({ type: 'diagnostic', diagnostic });
        job.finish(result.ok ? 'success' : 'failed', {
          ok: result.ok,
          entryPath: result.entryPath,
          phase: result.phase,
          diagnostics: result.diagnostics || []
        });
      } catch (error) {
        job.emit({ type: 'log', level: 'error', text: `${errorText(error)}\n` });
        job.finish('failed', { ok: false, error: errorText(error) });
      }
    })();
    return;
  }

  // Ein einzelnes Spiel: Quellen in der EXE, Assets daneben.
  if (pathname === '/api/export' && method === 'POST') {
    const body = await readBody(req);
    if (!body.projectDir || !body.project) return fail(res, new Error('Projektdaten fehlen.'));
    const config = await loadConfig();
    const info = inspectGbx(config.gbxPath, ROOT);
    if (!info.ok) return fail(res, new Error(info.error));
    const job = createJob('export', { projectDir: body.projectDir });
    json(res, 200, { ok: true, jobId: job.id });
    (async () => {
      try {
        const project = await saveProject(body.project, body.projectDir);
        job.emit({ type: 'started', kind: 'export' });
        const result = await exportGame({
          gbx: info.resolved,
          projectDir: body.projectDir,
          project,
          target: body.target,
          onEvent: (event) => {
            if (event.type === 'log') job.emit({ type: 'log', level: event.level, text: event.text });
            else job.emit(event);
          },
          onChild: jobChildHandler(job)
        });
        job.finish(result.ok ? 'success' : 'failed', result);
      } catch (error) {
        job.emit({ type: 'log', level: 'error', text: `${errorText(error)}\n` });
        job.finish('failed', { ok: false, error: errorText(error) });
      }
    })();
    return;
  }

  if (pathname === '/api/run' && method === 'POST') {
    const body = await readBody(req);
    if (!body.projectDir || !body.project) return fail(res, new Error('Projektdaten fehlen.'));
    const config = await loadConfig();
    const info = inspectGbx(config.gbxPath, ROOT);
    if (!info.ok) return fail(res, new Error(info.error));
    const job = createJob('run', { projectDir: body.projectDir });
    json(res, 200, { ok: true, jobId: job.id });
    (async () => {
      try {
        const project = await saveProject(body.project, body.projectDir);
        const args = String(body.args || '')
          .split(/\s+/)
          .filter(Boolean);
        const child = runProject({
          gbx: info.resolved,
          projectDir: body.projectDir,
          project,
          args,
          onEvent: (event) => {
            if (event.type === 'log') job.emit({ type: 'log', level: event.level, text: event.text });
            else job.emit(event);
            if (event.type === 'exit') job.finish('success', { code: event.code });
          },
          onChild: jobChildHandler(job)
        });
        if (!child) return;
      } catch (error) {
        job.emit({ type: 'log', level: 'error', text: `${errorText(error)}\n` });
        job.finish('failed', { ok: false, error: errorText(error) });
      }
    })();
    return;
  }

  // Laufzeit-Debugger: Sitzung starten, Befehle senden, Haltepunkte setzen.
  if (pathname === '/api/debug/start' && method === 'POST') {
    const body = await readBody(req);
    if (!body.projectDir || !body.project) return fail(res, new Error('Projektdaten fehlen.'));
    const config = await loadConfig();
    const info = inspectGbx(config.gbxPath, ROOT);
    if (!info.ok) return fail(res, new Error(info.error));
    const job = createJob('debug', { projectDir: body.projectDir });
    json(res, 200, { ok: true, jobId: job.id });
    (async () => {
      try {
        const project = await saveProject(body.project, body.projectDir);
        const session = startDebugSession({
          gbx: info.resolved,
          projectDir: body.projectDir,
          project,
          breakpoints: Array.isArray(body.breakpoints) ? body.breakpoints : [],
          headless: !!body.headless,
          onEvent: (event) => {
            if (event.type === 'debug') job.emit({ type: 'debug', event: event.event });
            else if (event.type === 'debug-exit') job.finish('success', { code: event.code });
            else job.emit(event);
          },
          onChild: jobChildHandler(job)
        });
        job.send = session.send;
      } catch (error) {
        job.emit({ type: 'log', level: 'error', text: `${errorText(error)}\n` });
        job.finish('failed', { ok: false, error: errorText(error) });
      }
    })();
    return;
  }

  if (pathname === '/api/debug/command' && method === 'POST') {
    const body = await readBody(req);
    const job = jobs.get(body.jobId);
    if (!job || !job.send) return fail(res, new Error('Keine Debug-Sitzung aktiv.'), 404);
    const projectDir = job.meta?.projectDir;
    const command = String(body.command || '').trim();
    const simple = new Set(['run', 'continue', 'step', 'next', 'out', 'pause', 'vars', 'quit']);
    if (simple.has(command)) {
      job.send(command);
      return json(res, 200, { ok: true });
    }
    // Wert-Tooltip im Haltepunkt: Die Kennung kommt mit der Antwort zurueck,
    // damit der Editor die Anfrage zuordnen kann.
    if (command === 'eval') {
      const id = String(body.id ?? '').replace(/[^0-9]/g, '').slice(0, 9);
      const expression = String(body.expression ?? '')
        .replace(/[\t\r\n]+/g, ' ')
        .trim()
        .slice(0, 200);
      if (!id || !expression) return fail(res, new Error('Ausdruck fehlt.'));
      job.send(`eval\t${id}\t${expression}`);
      return json(res, 200, { ok: true });
    }
    if (command === 'bp' || command === 'bpc') {
      const line = Number(body.line);
      if (!projectDir || !body.file || !Number.isInteger(line) || line <= 0) {
        return fail(res, new Error('Haltepunkt ohne Datei oder Zeile.'));
      }
      const target = path.resolve(projectDir, String(body.file));
      if (!target.startsWith(path.resolve(projectDir))) {
        return fail(res, new Error('Der Pfad liegt außerhalb des Projekts.'));
      }
      const condition = String(body.condition || '').replace(/[\t\r\n]+/g, ' ').trim();
      job.send(condition ? `${command}\t${target}\t${line}\t${condition}` : `${command}\t${target}\t${line}`);
      return json(res, 200, { ok: true });
    }
    return fail(res, new Error(`Unbekannter Debug-Befehl: ${command}`));
  }

  if (pathname.startsWith('/api/jobs/') && pathname.endsWith('/events')) {
    const id = pathname.split('/')[3];
    const job = jobs.get(id);
    if (!job) return fail(res, new Error('Auftrag nicht gefunden.'), 404);
    return streamJob(req, res, job);
  }

  if (pathname === '/api/stop' && method === 'POST') {
    const body = await readBody(req);
    const job = jobs.get(body.jobId);
    if (!job) return fail(res, new Error('Auftrag nicht gefunden.'), 404);
    try {
      job.child?.kill();
      job.emit({ type: 'log', level: 'warning', text: 'Prozess wurde beendet.' });
      job.finish('stopped', { ok: false });
    } catch (error) {
      return fail(res, error);
    }
    return json(res, 200, { ok: true });
  }

  if (pathname === '/api/reveal' && method === 'POST') {
    const body = await readBody(req);
    if (!body.path) return fail(res, new Error('Kein Pfad.'));
    if (!pathExists(body.path)) return fail(res, new Error('Datei existiert nicht.'));
    try {
      await revealInFileManager(body.path);
      return json(res, 200, { ok: true, path: body.path });
    } catch (error) {
      return fail(res, error);
    }
  }

  return fail(res, new Error(`Unbekannte API-Route: ${method} ${pathname}`), 404);
}

export async function startServer({ port = 3417, host = '127.0.0.1' } = {}) {
  const config = await loadConfig();
  // Ohne Eintrag gelten die Standardpfade relativ zur Editor-EXE
  // (..\build\gbx.exe und ..\compiler\bin\g++.exe); gespeichert wird nichts,
  // damit ein verschobenes Paket weiterläuft.
  // Ältere Einträge mit genau diesen Pfaden werden auf die relative Form
  // umgestellt, damit auch sie ein Verschieben überstehen.
  let migrated = false;
  for (const [key, relative] of Object.entries({ gbxPath: defaultGbxPath(), cxxPath: defaultCxxPath() })) {
    const stored = config[key];
    if (!stored || !path.isAbsolute(stored)) continue;
    if (path.normalize(stored) !== resolveTool(relative, ROOT)) continue;
    config[key] = relative;
    migrated = true;
  }
  if (migrated) await saveConfig(config);
  applyCompiler(config);

  const server = http.createServer(async (req, res) => {
    let url;
    try {
      url = new URL(req.url || '/', `http://${req.headers.host || '127.0.0.1'}`);
    } catch {
      res.writeHead(400);
      return res.end('Bad request');
    }
    try {
      if (url.pathname.startsWith('/api/')) {
        await routeApi(req, res, url);
        return;
      }
      if (url.pathname.startsWith('/monaco/')) {
        const rel = decodeURIComponent(url.pathname.slice('/monaco/'.length));
        const target = path.join(MONACO_DIR, rel);
        if (!isInside(MONACO_DIR, target)) {
          res.writeHead(403);
          return res.end('Forbidden');
        }
        return serveStatic(res, target, req);
      }
      if (url.pathname.startsWith('/data/')) {
        const rel = decodeURIComponent(url.pathname.slice('/data/'.length));
        const target = path.join(DATA_DIR, rel);
        if (!isInside(DATA_DIR, target)) {
          res.writeHead(403);
          return res.end('Forbidden');
        }
        return serveStatic(res, target, req);
      }
      // Programm-Icon: als /favicon.ico und als Bilddatei fuer die Seite.
      if (url.pathname === '/favicon.ico') {
        return serveStatic(res, path.join(ASSETS_DIR, 'icon.ico'), req);
      }
      if (url.pathname.startsWith('/assets/')) {
        const rel = decodeURIComponent(url.pathname.slice('/assets/'.length));
        const target = path.join(ASSETS_DIR, rel);
        if (!isInside(ASSETS_DIR, target)) {
          res.writeHead(403);
          return res.end('Forbidden');
        }
        return serveStatic(res, target, req);
      }
      const rel = url.pathname === '/' ? 'index.html' : decodeURIComponent(url.pathname.slice(1));
      const target = path.join(RENDERER_DIR, rel);
      if (!isInside(RENDERER_DIR, target)) {
        res.writeHead(403);
        return res.end('Forbidden');
      }
      return serveStatic(res, target, req);
    } catch (error) {
      if (!res.headersSent) fail(res, error, 500);
      else res.end();
    }
  });

  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, () => resolve());
  });
  const address = server.address();
  const actualPort = typeof address === 'object' && address ? address.port : port;
  return {
    server,
    port: actualPort,
    url: `http://${host}:${actualPort}/`,
    async close() {
      for (const job of jobs.values()) {
        try {
          job.child?.kill();
        } catch {
          /* ignore */
        }
      }
      await new Promise((resolve) => server.close(resolve));
    }
  };
}
