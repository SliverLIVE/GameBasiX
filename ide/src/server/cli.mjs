#!/usr/bin/env node
import { spawn } from 'node:child_process';
import { startServer } from './server.mjs';
import { tr } from './locale.mjs';

const portArg = Number(process.argv.find((a) => /^--port=/.test(a))?.split('=')[1] || process.env.PORT || 3417);
const start = await startServer({ port: Number.isFinite(portArg) ? portArg : 3417 });
console.log('');
console.log('  GBX Studio');
console.log(tr('  Läuft unter ') + start.url);
console.log(tr('  Beenden mit Strg+C'));
console.log('');

if (!process.argv.includes('--no-open')) {
  try {
    if (process.platform === 'win32') {
      spawn('cmd', ['/c', 'start', '', start.url], { detached: true, stdio: 'ignore' }).unref();
    } else if (process.platform === 'darwin') {
      spawn('open', [start.url], { detached: true, stdio: 'ignore' }).unref();
    } else {
      spawn('xdg-open', [start.url], { detached: true, stdio: 'ignore' }).unref();
    }
  } catch {
    /* Browser lässt sich nicht automatisch öffnen. */
  }
}

const shutdown = async () => {
  await start.close();
  process.exit(0);
};
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
