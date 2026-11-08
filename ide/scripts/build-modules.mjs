// Erzeugt HELP.md aus den Markdown-Kommentaren der zentralen Module.
// Die GBX-Dateien selbst bleiben unveraendert im Modules-Ordner.
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { modulesRoot, writeModuleHelp } from '../src/server/modules.mjs';

const root = modulesRoot(path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..'));
const written = await writeModuleHelp(root);
for (const item of written) {
  console.log(`HELP.md: ${path.relative(root, item.file)} (${item.id})`);
}
if (!written.length) {
  console.error(`Keine Module in ${root} gefunden.`);
  process.exitCode = 1;
}
