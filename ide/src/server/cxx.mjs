// Anbindung an den C++-Compiler (g++) fuer die INLINE-Bloecke.
//
// Ein Quelltext-Checkout bringt keinen Compiler mit. Fehlt g++ im Ordner
// compiler/ neben der Editor-EXE, laedt die IDE auf Knopfdruck einen fertigen
// MinGW-w64-Build (WinLibs) dorthin - derselbe Weg wie beim Emscripten-SDK.
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { Readable } from 'node:stream';
import { pipeline } from 'node:stream/promises';

import { resolveTool, pathExists } from './gbx.mjs';
import { tr } from './locale.mjs';

// GitHub nennt immer den neuesten Build; der Dateiname traegt die Version.
const RELEASE_API = 'https://api.github.com/repos/brechtsanders/winlibs_mingw/releases/latest';
const ASSET_NAME = /^winlibs-x86_64-posix-seh-gcc-[\d.]+-mingw-w64ucrt-[\d.]+-r\d+\.zip$/i;
// Schutz gegen Ueberraschungen: der Build liegt bei rund 250 MB.
const MAX_BYTES = 800 * 1024 * 1024;

/** Zielordner des mitgelieferten Compilers: `compiler` neben der Editor-EXE. */
export function compilerRoot(appRoot) {
  return resolveTool('compiler', appRoot);
}

/** Liegt dort ein benutzbarer g++? */
export function compilerReady(root) {
  return pathExists(path.join(root, 'bin', process.platform === 'win32' ? 'g++.exe' : 'g++'));
}

function run(cmd, args, timeout = 0) {
  try {
    const result = spawnSync(cmd, args, { encoding: 'utf8', windowsHide: true, timeout });
    return { code: result.status ?? -1, out: `${result.stdout || ''}${result.stderr || ''}` };
  } catch (error) {
    return { code: -1, out: String(error?.message || error) };
  }
}

/** Passenden Build aus den GitHub-Releases waehlen (kleinster ohne LLVM). */
async function releaseAsset() {
  let data;
  try {
    const response = await fetch(RELEASE_API, {
      headers: { 'User-Agent': 'GBX-Studio', Accept: 'application/vnd.github+json' },
      signal: AbortSignal.timeout(20000)
    });
    if (!response.ok) return null;
    data = await response.json();
  } catch {
    return null;
  }
  const assets = (data?.assets || []).filter(
    (asset) => ASSET_NAME.test(asset.name) && asset.size > 0 && asset.size < MAX_BYTES
  );
  if (!assets.length) return null;
  assets.sort((a, b) => a.size - b.size);
  const [best] = assets;
  return { name: best.name, url: best.browser_download_url, size: best.size };
}

/** Laedt die Datei und meldet den Fortschritt in 10-MB-Schritten. */
async function download(url, file, onLog) {
  const response = await fetch(url, {
    redirect: 'follow',
    headers: { 'User-Agent': 'GBX-Studio' },
    signal: AbortSignal.timeout(30 * 60 * 1000)
  });
  if (!response.ok || !response.body) throw new Error(`HTTP ${response.status}`);
  const total = Number(response.headers.get('content-length') || 0);
  const totalMb = total ? Math.round(total / 1048576) : 0;
  let seen = 0;
  let nextReport = 1;
  const source = Readable.fromWeb(response.body);
  source.on('data', (chunk) => {
    seen += chunk.length;
    const mb = Math.floor(seen / (10 * 1024 * 1024));
    if (mb < nextReport) return;
    nextReport = mb + 1;
    onLog?.(`${mb} MB${totalMb ? ` / ${totalMb} MB` : ''}`);
  });
  await pipeline(source, fs.createWriteStream(file));
  return file;
}

/** Ordner im Archiv, in dem bin/g++.exe liegt. */
function findCompilerDir(dir, depth = 0) {
  if (depth > 3) return '';
  if (pathExists(path.join(dir, 'bin', 'g++.exe'))) return dir;
  let entries;
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return '';
  }
  for (const entry of entries) {
    if (!entry.isDirectory()) continue;
    const hit = findCompilerDir(path.join(dir, entry.name), depth + 1);
    if (hit) return hit;
  }
  return '';
}

/** ZIP entpacken: bsdtar (Windows 10+) kann es, sonst tar/unzip bzw. PowerShell. */
async function extractArchive(archive, work) {
  const quick = run('tar', ['-xf', archive, '-C', work], 10 * 60 * 1000);
  if (quick.code === 0 && findCompilerDir(work)) return true;

  if (process.platform === 'win32') {
    const quote = (text) => `'${text.replace(/'/g, "''")}'`;
    const extract = run(
      'powershell',
      [
        '-NoProfile',
        '-NonInteractive',
        '-Command',
        `Expand-Archive -LiteralPath ${quote(archive)} -DestinationPath ${quote(work)} -Force`
      ],
      20 * 60 * 1000
    );
    if (extract.code === 0 && findCompilerDir(work)) return true;
  } else {
    const unzip = run('unzip', ['-o', archive, '-d', work], 10 * 60 * 1000);
    if (unzip.code === 0 && findCompilerDir(work)) return true;
  }
  return false;
}

/**
 * Compiler nach `root` holen: Release suchen, laden, entpacken und den
 * mingw64-Ordner zu `compiler/` machen. Meldet jede Zeile ueber onLog.
 */
export async function installCompiler({ root, onLog }) {
  if (process.platform !== 'win32') {
    onLog?.(tr('Der Compiler-Download ist nur für Windows gedacht – bitte g++ über die Paketverwaltung installieren.'));
    return false;
  }
  if (compilerReady(root)) {
    onLog?.(tr('g++ ist bereits vorhanden.'));
    return true;
  }

  onLog?.(tr('Suche den aktuellen GCC-Build (WinLibs) …'));
  const asset = await releaseAsset();
  if (!asset) {
    onLog?.(tr('Kein passender Download gefunden – ist GitHub erreichbar?'));
    return false;
  }
  onLog?.(`${tr('Lade')} ${asset.name} (${Math.round(asset.size / 1048576)} MB) …`);

  const work = path.join(root, '.install');
  await fsp.rm(work, { recursive: true, force: true });
  await fsp.mkdir(work, { recursive: true });
  const archive = path.join(work, asset.name);
  try {
    await download(asset.url, archive, onLog);
  } catch (error) {
    onLog?.(`${tr('Der Download ist fehlgeschlagen: ')}${error.message}`);
    return false;
  }

  onLog?.(tr('Entpacke …'));
  if (!(await extractArchive(archive, work))) {
    onLog?.(tr('Das Archiv konnte nicht entpackt werden.'));
    return false;
  }
  const source = findCompilerDir(work);
  if (!source) {
    onLog?.(tr('Im Archiv wurde kein g++.exe gefunden.'));
    return false;
  }

  // mingw64/* wird zu compiler/* – so sucht die IDE den Compiler.
  for (const entry of await fsp.readdir(source)) {
    const target = path.join(root, entry);
    await fsp.rm(target, { recursive: true, force: true });
    await fsp.rename(path.join(source, entry), target);
  }
  await fsp.rm(work, { recursive: true, force: true });

  const gcc = path.join(root, 'bin', 'g++.exe');
  const version = run(gcc, ['--version'], 20000).out.split(/\r?\n/)[0].trim();
  onLog?.(`${tr('g++ ist eingerichtet: ')}${version || gcc}`);
  return compilerReady(root);
}
