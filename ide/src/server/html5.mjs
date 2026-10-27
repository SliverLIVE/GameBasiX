// HTML5-Build: Interpreter als WebAssembly und Spiel-Export für den Browser.
//
// Der Interpreter wird einmal mit emcc übersetzt und liegt danach als
// gbx.js (wasm eingebettet) neben der Editor-EXE (Ordner "html5"). Ein Export
// kopiert diese Datei, legt die Quellen als game.js daneben und schreibt eine
// index.html, die alles in die virtuelle Dateiablage des Browsers legt.
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { spawn } from 'node:child_process';

import { emxxCommand, emsdkEnv } from './emsdk.mjs';
import { pathExists } from './gbx.mjs';

/** Ordner mit dem fertigen Player (gbx.js), relativ zur Editor-EXE. */
export function playerDir(appRoot) {
  return path.resolve(appRoot, '..', 'html5');
}

/** Quellen des Interpreters: im Paket unter <paket>/src, im Quellbaum <repo>/src. */
export function sourceRoot(appRoot) {
  const candidates = [path.resolve(appRoot, '..'), path.resolve(appRoot, '..', '..')];
  for (const dir of candidates) {
    if (pathExists(path.join(dir, 'src', 'main.cpp')) && pathExists(path.join(dir, 'inline', 'gbx_inline.h'))) {
      return dir;
    }
  }
  return '';
}

/** Rezept des fertigen Players: hochziehen, wenn sich die em++-Optionen ändern. */
const PLAYER_BUILD = 'player-2';

/** Datei mit dem Stempel des Players (Quell-Hash und Bauzeit). */
function playerStampFile(appRoot) {
  return path.join(playerDir(appRoot), 'gbx.js.build.json');
}

/**
 * Kennung der Interpreter-Quellen. Der Player traegt sie als Stempel: nur so
 * faellt auf, wenn gbx.js aus einer aelteren Version stammt als src/ - genau
 * das laesst Exports sonst mit "Unbekannter Befehl" scheitern.
 */
function interpreterSourceHash(sourceDir) {
  const hash = createHash('sha1');
  hash.update(PLAYER_BUILD);
  if (!sourceDir) return hash.digest('hex');
  for (const file of interpreterFiles(sourceDir)) {
    // Nur der relative Pfad: derselbe Quellstand in einem anderen Ordner
    // (Repo und Paket) muss denselben Stempel ergeben.
    hash.update(path.relative(sourceDir, file).replace(/\\/g, '/'));
    try {
      hash.update(fs.readFileSync(file));
    } catch {
      hash.update('?');
    }
  }
  return hash.digest('hex');
}

/** Gespeicherter Stempel des Players, oder null. */
function readPlayerStamp(appRoot) {
  try {
    return JSON.parse(fs.readFileSync(playerStampFile(appRoot), 'utf8'));
  } catch {
    return null;
  }
}

/** Zustand von Player und Quellen. */
export function inspectHtml5(appRoot) {
  const player = playerDir(appRoot);
  const sources = sourceRoot(appRoot);
  const ready = pathExists(path.join(player, 'gbx.js'));
  const stamp = readPlayerStamp(appRoot);
  const sourceHash = sources ? interpreterSourceHash(sources) : '';
  return {
    playerDir: player,
    playerReady: ready,
    // Ohne Quellen laesst sich nichts vergleichen; dann bleibt der Player gueltig.
    playerStale: Boolean(ready && sources && (!stamp || stamp.recipe !== PLAYER_BUILD || stamp.sourceHash !== sourceHash)),
    sourceDir: sources,
    sourcesReady: Boolean(sources)
  };
}

function run(cmd, args, { cwd, env, onLog }) {
  return new Promise((resolve) => {
    let child;
    try {
      child = spawn(cmd, args, { cwd, env });
    } catch (error) {
      resolve({ code: -1, error: String(error?.message || error) });
      return;
    }
    const handle = (chunk) => {
      for (const line of chunk.toString().split(/\r?\n/)) {
        const text = line.trimEnd();
        if (text) onLog?.(text);
      }
    };
    child.stdout?.on('data', handle);
    child.stderr?.on('data', handle);
    child.on('error', (error) => resolve({ code: -1, error: String(error?.message || error) }));
    child.on('close', (code) => resolve({ code: code ?? -1 }));
  });
}

/**
 * Interpreter als WebAssembly bauen; schreibt gbx.js in den Player-Ordner.
 * extraSources/extraIncludes/defines dienen dem HTML5-Export mit INLINE:
 * dann wandert das erzeugte C++ der Blöcke mit in den Player.
 * Gibt { ok, files } zurück.
 */
export async function buildPlayer({
  appRoot,
  emsdk,
  onLog,
  onChild,
  extraSources = [],
  extraIncludes = [],
  extraFlags = [],
  defines = [],
  out: target
}) {
  const info = inspectHtml5(appRoot);
  if (!info.sourcesReady) {
    onLog?.('Die C++-Quellen fehlen neben der Editor-EXE (Ordner src/, inline/, third_party/).');
    return { ok: false, error: 'Quellen fehlen' };
  }
  const emcc = emxxCommand(emsdk);
  if (!emcc) {
    onLog?.('emsdk ist nicht eingerichtet.');
    return { ok: false, error: 'emsdk fehlt' };
  }

  const srcDir = path.join(info.sourceDir, 'src');
  const files = fs
    .readdirSync(srcDir)
    .filter((name) => name.endsWith('.cpp') && name !== 'studio_launcher.cpp')
    .map((name) => path.join(srcDir, name));

  const out = target || path.join(playerDir(appRoot), 'gbx.js');
  await fsp.mkdir(path.dirname(out), { recursive: true });

  const args = [
    '-std=c++17',
    '-O2',
    `-I${path.join(info.sourceDir, 'src')}`,
    `-I${path.join(info.sourceDir, 'inline')}`,
    `-I${path.join(info.sourceDir, 'third_party')}`,
    '-sUSE_SDL=3',
    // TrueType über den FreeType-Port von Emscripten.
    '-sUSE_FREETYPE=1',
    '-DGBX_HAVE_TTF=1',
    // Ohne -fexceptions bricht der Interpreter bei jedem Laufzeitfehler ab,
    // statt die BASIC-Meldung anzuzeigen.
    '-fexceptions',
    '-sASYNCIFY',
    '-sALLOW_MEMORY_GROWTH=1',
    '-sFORCE_FILESYSTEM=1',
    '-sMODULARIZE=1',
    // wasm in die JS-Datei einbetten: dann läuft der Export auch per
    // Doppelklick über file:// (dort darf nichts nachgeladen werden).
    '-sSINGLE_FILE=1',
    '-sEXPORT_NAME=createGbx',
    '-sINVOKE_RUN=0',
    '-sEXPORTED_RUNTIME_METHODS=FS,callMain',
    '-sENVIRONMENT=web',
    ...defines.map((name) => `-D${name}`),
    ...extraIncludes.map((dir) => `-I${dir}`),
    ...extraFlags,
    ...files,
    ...extraSources,
    '-o',
    out
  ];

  const count = files.length + extraSources.length;
  onLog?.(`Baue den Interpreter als WebAssembly (${count} Dateien) …`);
  const started = Date.now();
  const result = await run(emcc.cmd, [...emcc.args, ...args], {
    cwd: info.sourceDir,
    env: emsdkEnv(emsdk),
    onLog
  });
  if (result.code !== 0) {
    onLog?.(`Der wasm-Build ist fehlgeschlagen${result.error ? `: ${result.error}` : ''}.`);
    return { ok: false, error: 'emcc ist fehlgeschlagen' };
  }
  if (!pathExists(out)) {
    onLog?.('em++ hat keine Player-Datei geschrieben.');
    return { ok: false, error: 'kein Player geschrieben' };
  }
  // Eine alte gbx.wasm aus früheren Builds würde nur verwirren.
  const staleWasm = out.replace(/\.js$/, '.wasm');
  if (pathExists(staleWasm)) await fsp.rm(staleWasm, { force: true });
  // Nur der Player neben der Editor-EXE bekommt einen Stempel; die Player im
  // Zwischenspeicher (INLINE) sind ueber ihren Hash-Namen schon eindeutig.
  const playerOut = path.join(playerDir(appRoot), 'gbx.js');
  if (path.resolve(out) === path.resolve(playerOut)) {
    await fsp.writeFile(
      playerStampFile(appRoot),
      `${JSON.stringify(
        {
          recipe: PLAYER_BUILD,
          sourceHash: interpreterSourceHash(info.sourceDir),
          builtAt: new Date().toISOString()
        },
        null,
        2
      )}\n`,
      'utf8'
    );
  }
  const seconds = Math.round((Date.now() - started) / 1000);
  onLog?.(`Fertig in ${seconds} s: ${out} (${(fs.statSync(out).size / 1e6).toFixed(1)} MB, wasm eingebettet)`);
  return { ok: true, files: [out], path: out };
}

// ------------------------------------------------------------------ Export

function listSources(dir, base = dir, out = []) {
  let entries = [];
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return out;
  }
  for (const entry of entries) {
    if (entry.name.startsWith('.') || entry.name === 'node_modules') continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      if (entry.name === 'build' || entry.name.endsWith('-web')) continue;
      listSources(full, base, out);
    } else if (/\.(gbx|gbxproj)$/i.test(entry.name)) {
      out.push(path.relative(base, full).replace(/\\/g, '/'));
    }
  }
  return out;
}

/** Ordner im Projekt, in denen INLINE-Header liegen koennen (-I fuer em++). */
function headerDirs(projectDir, base = projectDir, out = []) {
  out.push(path.relative(projectDir, base) || '.');
  let entries = [];
  try {
    entries = fs.readdirSync(base, { withFileTypes: true });
  } catch {
    return out;
  }
  for (const entry of entries) {
    if (!entry.isDirectory()) continue;
    if (entry.name.startsWith('.') || entry.name === 'node_modules') continue;
    if (entry.name === 'build' || entry.name.endsWith('-web')) continue;
    headerDirs(projectDir, path.join(base, entry.name), out);
  }
  return out;
}

/** Erzeugt das C++ der INLINE-Bloecke mit gbx.exe (--inline-cpp). */
async function writeInlineSource({ gbx, projectDir, entry, target, onLog }) {
  await fsp.mkdir(path.dirname(target), { recursive: true });
  const result = await run(gbx, ['--inline-cpp', target, path.join(projectDir, entry)], {
    cwd: projectDir,
    env: process.env,
    onLog
  });
  if (result.code !== 0 || !pathExists(target)) {
    return { ok: false, error: result.error || 'gbx.exe konnte das INLINE-C++ nicht schreiben' };
  }
  return { ok: true };
}

/** Fassung des Player-Rezepts: hochziehen, wenn sich die em++-Optionen ändern. */
const PLAYER_RECIPE = 'inline-player-1';

/** Header eines Projekts: sie fließen in den INLINE-Player ein. */
function projectHeaders(projectDir) {
  const files = [];
  for (const dir of headerDirs(projectDir)) {
    const abs = path.join(projectDir, dir);
    let entries = [];
    try {
      entries = fs.readdirSync(abs, { withFileTypes: true });
    } catch {
      continue;
    }
    for (const entry of entries) {
      if (!entry.isFile() || !/\.(h|hh|hpp|hxx)$/i.test(entry.name)) continue;
      files.push(path.join(abs, entry.name));
    }
  }
  return files.sort();
}

/** Alle Dateien des Interpreters, die in den Player wandern. */
function interpreterFiles(sourceDir) {
  if (!sourceDir) return [];
  const files = [];
  for (const dir of ['src', 'inline', 'third_party']) {
    const base = path.join(sourceDir, dir);
    for (const name of listAssets(base, base)) files.push(path.join(base, name));
  }
  return files.sort();
}

/**
 * Kennung des fertigen Players: alles, was das Übersetzen beeinflusst,
 * steckt im Hash - Blöcke, Projekt-Header, Interpreter-Quellen und emsdk.
 */
function inlinePlayerKey({ appRoot, projectDir, generated, emsdk, emsdkVersion, defines, extras = '' }) {
  const info = inspectHtml5(appRoot);
  const emcc = emsdk ? emxxCommand(emsdk) : null;
  const hash = createHash('sha1');
  hash.update(PLAYER_RECIPE);
  hash.update(JSON.stringify(defines));
  hash.update(generated);
  hash.update(extras);
  hash.update(String(emsdkVersion || ''));
  hash.update(String(emcc ? emcc.cmd : ''));
  for (const file of [...interpreterFiles(info.sourceDir), ...projectHeaders(projectDir)]) {
    hash.update(file.replace(/\\/g, '/'));
    try {
      hash.update(fs.readFileSync(file));
    } catch {
      hash.update('?');
    }
  }
  return hash.digest('hex');
}

/** Ordner mit bereits übersetzten Projekt-Playern. */
function inlinePlayerCache() {
  return path.join(os.tmpdir(), 'gbx-inline-player');
}

/** Alte Player aussortieren, damit der Zwischenspeicher nicht wächst. */
async function prunePlayerCache(keep = 8) {
  const dir = inlinePlayerCache();
  let entries = [];
  try {
    entries = await fsp.readdir(dir, { withFileTypes: true });
  } catch {
    return;
  }
  const files = [];
  for (const entry of entries) {
    if (!entry.isFile() || !entry.name.endsWith('.js')) continue;
    const full = path.join(dir, entry.name);
    try {
      files.push({ full, time: (await fsp.stat(full)).mtimeMs });
    } catch {
      /* gerade gelöscht */
    }
  }
  files.sort((a, b) => b.time - a.time);
  for (const old of files.slice(keep)) await fsp.rm(old.full, { force: true });
}

function listAssets(dir, base = dir, out = []) {
  let entries = [];
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return out;
  }
  for (const entry of entries) {
    if (entry.name.startsWith('.')) continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) listAssets(full, base, out);
    else out.push(path.relative(base, full).replace(/\\/g, '/'));
  }
  return out;
}

/** Dateien als { pfad: base64 } einlesen. */
function packFiles(root, names) {
  const files = {};
  for (const name of names) {
    try {
      files[name] = fs.readFileSync(path.join(root, name)).toString('base64');
    } catch {
      /* Datei nicht lesbar */
    }
  }
  return files;
}

function htmlTemplate({ title, width, height }) {
  return `<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${title.replace(/[<>&"]/g, (c) => ({ '<': '&lt;', '>': '&gt;', '&': '&amp;', '"': '&quot;' }[c]))}</title>
<style>
  html, body { margin: 0; height: 100%; background: #0b0e14; overflow: hidden; }
  body { display: flex; align-items: center; justify-content: center; }
  #canvas { max-width: 100vw; max-height: 100vh; image-rendering: pixelated; outline: none; }
  #note { position: fixed; left: 0; right: 0; bottom: 10px; text-align: center; color: #6b7688;
          font: 12px/1.5 system-ui, sans-serif; pointer-events: none; }
</style>
</head>
<body>
<canvas id="canvas" width="${width}" height="${height}" tabindex="0"></canvas>
<div id="note">Klick ins Bild, damit Tastatur und Maus ankommen · ESC beendet</div>
<script src="game.js"></script>
<script src="gbx.js"></script>
<script>
(function () {
  const game = window.GBX_GAME;
  const canvas = document.getElementById('canvas');
  canvas.focus();
  canvas.addEventListener('click', () => canvas.focus());

  function decode(base64) {
    const binary = atob(base64);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i += 1) bytes[i] = binary.charCodeAt(i);
    return bytes;
  }

  createGbx({ canvas }).then((mod) => {
    const FS = mod.FS;
    try { FS.mkdir('/game'); } catch (e) { /* schon da */ }
    for (const [name, base64] of Object.entries(game.files)) {
      const target = '/game/' + name;
      const dir = target.slice(0, target.lastIndexOf('/'));
      if (dir) { try { FS.mkdirTree(dir); } catch (e) { /* schon da */ } }
      FS.writeFile(target, decode(base64));
    }
    for (const [name, base64] of Object.entries(game.assets || {})) {
      const target = '/game/' + name;
      const dir = target.slice(0, target.lastIndexOf('/'));
      if (dir) { try { FS.mkdirTree(dir); } catch (e) { /* schon da */ } }
      FS.writeFile(target, decode(base64));
    }
    mod.callMain(['/game/' + game.entry]);
  }).catch((error) => {
    document.body.innerHTML = '<pre style="color:#f66;padding:20px">' + error + '</pre>';
    console.error(error);
  });
})();
</script>
</body>
</html>
`;
}

/**
 * Sektion [native] fuer den Browser: Archive lassen sich nicht linken (sie
 * sind fuer den Desktop uebersetzt), ihre C++-Quellen dagegen schon. -D, -U
 * und -I gelten auch hier; -l und -L zeigen auf genau diese Archive.
 */
function nativeSettings(projectDir, project) {
  const words = (value) =>
    (Array.isArray(value) ? value : String(value ?? '').split(/\s+/))
      .map((item) => String(item).trim())
      .filter(Boolean);
  const sources = words(project?.nativeSources).map((file) => path.resolve(projectDir, file));
  const flags = [];
  const dropped = [];
  for (const flag of words(project?.nativeFlags)) {
    if (/^-(D|U|I)/.test(flag)) flags.push(flag);
    else dropped.push(flag);
  }
  return {
    libs: words(project?.nativeLibs),
    sources,
    missing: sources.filter((file) => !pathExists(file)),
    flags,
    dropped,
    // Alles, was den Player beeinflusst: Quelle plus Flags und Quellstand.
    key: [
      flags.join(' '),
      ...sources.map((file) => {
        try {
          return `${path.basename(file)}:${createHash('sha1').update(fs.readFileSync(file)).digest('hex')}`;
        } catch {
          return `${path.basename(file)}:?`;
        }
      })
    ].join('|')
  };
}

/**
 * Spiel als HTML5-Ordner schreiben: index.html, gbx.js, game.js mit den
 * Quellen und den Assets daneben.
 */
export async function exportHtml5({ appRoot, projectDir, project, target, gbx, emsdk, emsdkVersion, onLog }) {
  const info = inspectHtml5(appRoot);
  const name = String(project?.name || 'spiel').replace(/[^\w.-]+/g, '-').toLowerCase() || 'spiel';
  const outDir = target ? path.resolve(target) : path.join(projectDir, `${name}-web`);
  await fsp.mkdir(outDir, { recursive: true });

  const sources = listSources(projectDir);
  if (!sources.length) {
    onLog?.('Keine .gbx-Dateien im Projekt gefunden.');
    return { ok: false, error: 'Keine Quellen' };
  }
  // Im Browser gibt es weder Compiler noch dlopen: die INLINE-Bloecke werden
  // hier mit Emscripten übersetzt und fest zum Player dazugelinkt.
  const withInline = sources.find((file) =>
    /^\s*INLINE\b/im.test(fs.readFileSync(path.join(projectDir, file), 'utf8'))
  );
  const entry = sources.includes('main.gbxproj')
    ? 'main.gbxproj'
    : sources.find((file) => file.endsWith('.gbxproj')) || sources.find((file) => file.endsWith('.gbx'));

  // Assets: der Projektordner "assets" und der eingestellte Asset-Ordner.
  const assetRoot = path.join(projectDir, project?.assetDir || 'assets');
  const assets = pathExists(assetRoot) ? listAssets(assetRoot, path.dirname(assetRoot)) : [];

  onLog?.(`Schreibe ${sources.length} Quelldateien und ${assets.length} Assets nach ${outDir} …`);
  const playerOut = path.join(outDir, 'gbx.js');
  if (withInline) {
    if (!gbx) {
      onLog?.('Für INLINE fehlt gbx.exe neben der Editor-EXE.');
      return { ok: false, error: 'gbx.exe fehlt' };
    }
    if (!emsdk) {
      onLog?.('INLINE im Browser braucht emsdk (Einstellungen → HTML5).');
      return { ok: false, error: 'emsdk fehlt' };
    }
    const native = nativeSettings(projectDir, project);
    if (native.missing.length) {
      onLog?.(`HTML5: diese [native]-Quellen fehlen: ${native.missing.join(', ')}`);
      return { ok: false, error: '[native] sources fehlen' };
    }
    if (native.libs.length && !native.sources.length) {
      onLog?.('HTML5 kann keine .a-Archive einbinden: sie enthalten Code fuer den Desktop.');
      onLog?.('Trage die C++-Dateien der Bibliothek in [native] sources ein - sie werden fuer den Browser uebersetzt.');
      return { ok: false, error: 'C++-Quellen der Bibliothek fehlen' };
    }
    if (native.dropped.length) {
      onLog?.(`HTML5: ohne Wirkung im Browser (nur fuer Archive): ${native.dropped.join(' ')}`);
    }
    onLog?.(`${withInline}: INLINE-Blöcke werden mit Emscripten in den Player übersetzt (dauert etwas) …`);
    const tmp = await fsp.mkdtemp(path.join(os.tmpdir(), 'gbx-inline-export-'));
    const cpp = path.join(tmp, 'inline_static.cpp');
    try {
      const written = await writeInlineSource({ gbx, projectDir, entry, target: cpp, onLog });
      if (!written.ok) {
        onLog?.(`Das INLINE-C++ ließ sich nicht erzeugen: ${written.error}`);
        return { ok: false, error: 'INLINE-Quelltext fehlt' };
      }
      const defines = ['GBX_INLINE_STATIC'];
      const extraIncludes = headerDirs(projectDir).map((dir) => path.join(projectDir, dir));
      const generated = await fsp.readFile(cpp, 'utf8');
      const key = inlinePlayerKey({
        appRoot,
        projectDir,
        generated,
        emsdk,
        emsdkVersion,
        defines,
        extras: native.key
      });
      const cached = path.join(inlinePlayerCache(), `${key}.js`);
      if (pathExists(cached)) {
        onLog?.('Player aus dem Zwischenspeicher übernommen – am Projekt hat sich nichts geändert.');
        await fsp.copyFile(cached, playerOut);
        // Der jüngste Treffer bleibt oben, damit er beim Aufräumen überlebt.
        await fsp.utimes(cached, new Date(), new Date());
      } else {
        // Erst daneben bauen und dann umbenennen: ein abgebrochener Lauf
        // hinterlässt so keinen halben Player im Zwischenspeicher.
        const part = `${cached.replace(/\.js$/, '')}.part.js`;
        const built = await buildPlayer({
          appRoot,
          emsdk,
          onLog,
          extraSources: [cpp, ...native.sources],
          extraIncludes,
          extraFlags: native.flags,
          defines,
          out: part
        });
        if (!built.ok) {
          await fsp.rm(part, { force: true });
          return { ok: false, error: built.error || 'Player-Build fehlgeschlagen' };
        }
        await fsp.rename(part, cached);
        await fsp.copyFile(cached, playerOut);
        await prunePlayerCache();
      }
    } finally {
      await fsp.rm(tmp, { recursive: true, force: true });
    }
  } else {
    if (!info.playerReady || info.playerStale) {
      if (info.sourcesReady && emsdk) {
        onLog?.(
          info.playerReady
            ? 'Der wasm-Player ist aelter als die Quellen - er wird jetzt neu gebaut …'
            : 'Der wasm-Player fehlt - er wird jetzt gebaut …'
        );
        const built = await buildPlayer({ appRoot, emsdk, onLog });
        if (!built.ok) return { ok: false, error: built.error || 'Player-Build fehlgeschlagen' };
      } else if (!info.playerReady) {
        onLog?.('Der wasm-Player fehlt noch (Einstellungen → HTML5: Player bauen).');
        return { ok: false, error: 'Player fehlt' };
      } else {
        onLog?.(
          'Achtung: der wasm-Player stammt aus einer aelteren Version und kennt ' +
            'womoeglich nicht alle Befehle. Einstellungen → HTML5: "Player bauen" erneuert ihn.'
        );
      }
    }
    await fsp.copyFile(path.join(info.playerDir, 'gbx.js'), playerOut);
    // Ältere Player liegen als zwei Dateien vor; die wasm-Datei wird dann
    // mitkopiert (der eingebettete Player braucht sie nicht).
    const wasm = path.join(info.playerDir, 'gbx.wasm');
    if (pathExists(wasm)) await fsp.copyFile(wasm, path.join(outDir, 'gbx.wasm'));
  }

  const payload = {
    title: project?.title || project?.name || name,
    width: project?.width || 640,
    height: project?.height || 480,
    entry,
    files: packFiles(projectDir, sources),
    assets: packFiles(projectDir, assets)
  };
  // Daten als eigene Datei: die index.html bleibt klein und lesbar.
  await fsp.writeFile(
    path.join(outDir, 'game.js'),
    `window.GBX_GAME = ${JSON.stringify(payload)};\n`,
    'utf8'
  );

  const html = htmlTemplate({
    title: payload.title,
    width: payload.width,
    height: payload.height
  });
  await fsp.writeFile(path.join(outDir, 'index.html'), html, 'utf8');

  onLog?.(`HTML5-Build fertig: ${path.join(outDir, 'index.html')}`);
  onLog?.('Zum Starten genügt ein Doppelklick auf index.html; ein Webserver geht auch.');
  return { ok: true, dir: outDir, file: path.join(outDir, 'index.html') };
}
