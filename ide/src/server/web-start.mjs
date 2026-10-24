import path from 'node:path';
import os from 'node:os';
import { spawn } from 'node:child_process';

/* Eigenständiger WebStart: startet den lokalen Server und öffnet den Browser.
   Wird für die WebStart-EXE mit esbuild gebündelt (daher __dirname statt import.meta). */

const bundledDir = typeof __dirname === 'string' ? __dirname : path.join(process.cwd(), 'src', 'server');
process.env.GBXIDE_ROOT ||= path.resolve(bundledDir, '..', '..');

if (!process.env.GBXIDE_HOME) {
  const appData = process.env.APPDATA || path.join(os.homedir(), 'AppData', 'Roaming');
  process.env.GBXIDE_HOME = path.join(appData, 'GBX Studio');
}

const portArg = Number(
  (process.argv.find((arg) => /^--port=/.test(arg)) || '').split('=')[1] ||
    process.env.PORT ||
    3417
);
const preferredPort = Number.isFinite(portArg) ? portArg : 3417;

function openBrowser(url) {
  try {
    if (process.platform === 'win32') {
      spawn('cmd', ['/c', 'start', '', url], { detached: true, stdio: 'ignore' }).unref();
    } else if (process.platform === 'darwin') {
      spawn('open', [url], { detached: true, stdio: 'ignore' }).unref();
    } else {
      spawn('xdg-open', [url], { detached: true, stdio: 'ignore' }).unref();
    }
  } catch {
    /* Browser lässt sich nicht automatisch öffnen. */
  }
}

async function main() {
  const { startServer } = await import('./server.mjs');
  const { tr } = await import('./locale.mjs');

  let server;
  try {
    server = await startServer({ port: preferredPort });
  } catch (error) {
    if (error?.code !== 'EADDRINUSE') throw error;
    console.log(`Port ${preferredPort}` + tr(' ist belegt, wähle einen freien Port …'));
    server = await startServer({ port: 0 });
  }

  console.log('');
  console.log('  GBX Studio – WebStart');
  console.log(tr('  Läuft unter ') + server.url);
  console.log(tr('  Einstellungen: ') + process.env.GBXIDE_HOME);
  console.log(tr('  Beenden mit Strg+C'));
  console.log('');

  if (!process.argv.includes('--no-open')) openBrowser(server.url);

  let shuttingDown = false;
  const shutdown = async () => {
    if (shuttingDown) return;
    shuttingDown = true;
    try {
      await server.close();
    } finally {
      process.exit(0);
    }
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

main().catch((error) => {
  console.error(tr('WebStart konnte nicht gestartet werden: ') + (error?.stack || error));
  process.exit(1);
});
