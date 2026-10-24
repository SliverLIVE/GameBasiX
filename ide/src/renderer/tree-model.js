/* Aufbau des Dateibaums. Reine Funktionen ohne DOM, damit die Einteilung
   unabhängig von der Oberfläche geprüft werden kann. */

export const MEDIA_EXT = new Set(['png', 'jpg', 'jpeg', 'gif', 'bmp', 'dds', 'tga', 'wav', 'ogg', 'mp3']);

export function extensionOf(item) {
  const name = item.name || item.path.split('/').pop() || '';
  return (item.ext || name.split('.').pop() || '').replace('.', '').toLowerCase();
}

export function isMediaFile(item) {
  return MEDIA_EXT.has(extensionOf(item)) || /^media\//i.test(item.path);
}

function emptyDir(path, name) {
  return { type: 'dir', path, name, dirs: [], files: [] };
}

function byName(a, b) {
  return a.name.localeCompare(b.name, 'de', { numeric: true });
}

function sortTree(node) {
  node.dirs.sort(byName);
  node.files.sort(byName);
  for (const dir of node.dirs) sortTree(dir);
}

/**
 * Baut aus der flachen Liste des Servers einen Ordnerbaum. Ordner erscheinen
 * auch dann, wenn sie leer sind – genau deshalb sieht man einen neu
 * angelegten Ordner sofort in der Übersicht.
 */
export function buildFileTree(items = []) {
  const root = emptyDir('', '');
  const dirs = new Map([['', root]]);

  const ensureDir = (parts) => {
    let parent = root;
    let current = '';
    for (const part of parts) {
      current = current ? `${current}/${part}` : part;
      let node = dirs.get(current);
      if (!node) {
        node = emptyDir(current, part);
        dirs.set(current, node);
        parent.dirs.push(node);
      }
      parent = node;
    }
    return parent;
  };

  for (const item of items || []) {
    if (!item || typeof item.path !== 'string') continue;
    const parts = item.path.replace(/\\/g, '/').split('/').filter(Boolean);
    if (!parts.length) continue;
    const name = parts.pop();
    const parent = ensureDir(parts);
    if (item.type === 'dir') {
      ensureDir([...parts, name]);
      continue;
    }
    if (parent.files.some((file) => file.path === item.path)) continue;
    parent.files.push({ ...item, name: item.name || name });
  }

  sortTree(root);
  return root;
}

/** Anzahl der Dateien unterhalb eines Ordners (für die Zahl am Ordner). */
export function countFiles(node) {
  return (node.files?.length || 0) + (node.dirs || []).reduce((sum, dir) => sum + countFiles(dir), 0);
}
