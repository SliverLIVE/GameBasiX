// Google Fonts für den Editor: Schriften auflisten, als TrueType laden und
// in den Asset-Ordner eines Projekts legen.
//
// Mit API-Key (Einstellungen → Allgemein) kommt die Liste von der offiziellen
// Webfonts-API; sie enthält bereits die TTF-Adressen. Ohne Key liest der Server
// die öffentliche Fontliste von fonts.google.com und holt die TTF-Datei über
// die CSS-Schnittstelle – deren Antwort hängt vom User-Agent ab, eine alte
// Kennung liefert TrueType statt woff2.
import fsp from 'node:fs/promises';
import path from 'node:path';

const API_URL = 'https://www.googleapis.com/webfonts/v1/webfonts';
const METADATA_URL = 'https://fonts.google.com/metadata/fonts';
const CSS_URL = 'https://fonts.googleapis.com/css2';
const TTF_UA = 'Mozilla/4.0';

const LIST_TTL = 12 * 60 * 60 * 1000;
const FILE_CACHE_MAX = 8;

const CATEGORY_LABELS = {
  'sans-serif': 'Sans Serif',
  serif: 'Serif',
  display: 'Display',
  handwriting: 'Handwriting',
  monospace: 'Monospace'
};

const WEIGHT_NAMES = {
  100: 'Thin',
  200: 'ExtraLight',
  300: 'Light',
  400: 'Regular',
  500: 'Medium',
  600: 'SemiBold',
  700: 'Bold',
  800: 'ExtraBold',
  900: 'Black',
  1000: 'ExtraBlack'
};

// key merkt sich, mit welchem API-Key die Liste geholt wurde – nach dem
// Eintragen oder Löschen eines Keys wird neu geladen.
let listCache = { at: 0, families: [], source: '', warning: '', key: '' };
const fileCache = new Map();

function categoryLabel(value) {
  const raw = String(value || '').trim();
  return CATEGORY_LABELS[raw.toLowerCase()] || raw || 'Sans Serif';
}

/** Varianten-Schlüssel der API („regular“, „700italic“) → „400“, „700i“. */
function variantKey(variant) {
  const raw = String(variant || '').trim();
  if (raw === 'regular') return '400';
  if (raw === 'italic') return '400i';
  const match = /^(\d+)(italic|i)?$/.exec(raw);
  if (!match) return '';
  return `${match[1]}${match[2] ? 'i' : ''}`;
}

/** Liest Gewichte und Kursivschrift aus den Varianten einer Familie. */
export function variantsFromKeys(keys) {
  const weights = new Set();
  let italic = false;
  for (const raw of keys || []) {
    const key = String(raw).trim();
    if (!key) continue;
    if (/(italic|i)$/.test(key)) italic = true;
    const core = key.replace(/(italic|i)$/, '');
    if (core.includes('..')) {
      const [from, to] = core.split('..').map(Number);
      if (Number.isFinite(from) && Number.isFinite(to)) {
        for (let weight = Math.ceil(from / 100) * 100; weight <= to; weight += 100) weights.add(weight);
      }
      continue;
    }
    const weight = Number(core);
    if (Number.isFinite(weight)) weights.add(weight);
  }
  if (!weights.size) weights.add(400);
  return { weights: [...weights].sort((a, b) => a - b), italic };
}

/** Offizielle Webfonts-API (mit Key): eine Familie je Eintrag. */
export function parseWebfonts(json) {
  const items = Array.isArray(json?.items) ? json.items : [];
  return items
    .map((item, index) => {
      const files = {};
      for (const [variant, url] of Object.entries(item.files || {})) {
        const key = variantKey(variant);
        if (key && typeof url === 'string' && url.startsWith('https://')) files[key] = url;
      }
      const { weights, italic } = variantsFromKeys(Object.keys(files).length ? Object.keys(files) : item.variants);
      return {
        name: String(item.family || '').trim(),
        category: categoryLabel(item.category),
        weights,
        italic,
        popularity: index + 1,
        files
      };
    })
    .filter((family) => family.name);
}

/** Öffentliche Fontliste (ohne Key). */
export function parseMetadata(json) {
  const items = Array.isArray(json?.familyMetadataList) ? json.familyMetadataList : [];
  return items
    .map((item, index) => {
      const { weights, italic } = variantsFromKeys(Object.keys(item.fonts || {}));
      return {
        name: String(item.family || '').trim(),
        category: categoryLabel(item.category),
        weights,
        italic,
        popularity: Number(item.popularity) || index + 1,
        files: null
      };
    })
    .filter((family) => family.name)
    .sort((a, b) => a.popularity - b.popularity || a.name.localeCompare(b.name, 'en'));
}

/** Nur die Felder, die die Oberfläche braucht (ohne TTF-Adressen). */
export function publicFamilies(families) {
  return (families || []).map(({ name, category, weights, italic, popularity }) => ({
    name,
    category,
    weights,
    italic,
    popularity
  }));
}

function fetchWithTimeout(url, options = {}, timeoutMs = 20000) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  return fetch(url, { ...options, signal: controller.signal }).finally(() => clearTimeout(timer));
}

async function readJson(url) {
  const response = await fetchWithTimeout(url, { headers: { 'User-Agent': TTF_UA } });
  const text = await response.text();
  let data = null;
  try {
    data = JSON.parse(text);
  } catch {
    data = null;
  }
  if (!response.ok) {
    const message = data?.error?.message || `HTTP ${response.status}`;
    const error = new Error(message);
    error.status = response.status;
    throw error;
  }
  if (!data) throw new Error('Die Antwort von Google Fonts war unlesbar.');
  return data;
}

/**
 * Fontliste (12 Stunden gecacht). Mit API-Key zuerst die offizielle API;
 * scheitert sie, bleibt die öffentliche Liste und die Antwort nennt den Grund.
 */
export async function listGoogleFonts({ apiKey = '', refresh = false } = {}) {
  const cacheKey = apiKey ? `api:${apiKey}` : 'public';
  if (!refresh && listCache.families.length && listCache.key === cacheKey && Date.now() - listCache.at < LIST_TTL) {
    return {
      families: publicFamilies(listCache.families),
      source: listCache.source,
      warning: listCache.warning,
      fetchedAt: listCache.at
    };
  }

  let warning = '';
  if (apiKey) {
    try {
      const json = await readJson(`${API_URL}?key=${encodeURIComponent(apiKey)}&sort=popularity`);
      const families = parseWebfonts(json);
      if (!families.length) throw new Error('Die Liste war leer.');
      listCache = { at: Date.now(), families, source: 'api', warning: '', key: cacheKey };
      return { families: publicFamilies(families), source: 'api', warning: '', fetchedAt: listCache.at };
    } catch (error) {
      warning =
        error?.status === 400 || error?.status === 403
          ? 'Der Google-Fonts-API-Key wurde abgelehnt – die öffentliche Fontliste wird verwendet.'
          : 'Die Google-Fonts-API antwortet nicht – die öffentliche Fontliste wird verwendet.';
    }
  }

  let json;
  try {
    json = await readJson(METADATA_URL);
  } catch {
    throw new Error('Google Fonts ist nicht erreichbar. Bitte die Internetverbindung prüfen.');
  }
  const families = parseMetadata(json);
  if (!families.length) throw new Error('Google Fonts hat keine Schriften geliefert.');
  listCache = { at: Date.now(), families, source: 'metadata', warning, key: cacheKey };
  return { families: publicFamilies(families), source: 'metadata', warning, fetchedAt: listCache.at };
}

/** Adresse der TTF-Datei für eine Variante. */
export function googleFontCssUrl(family, { weight = 400, style = 'normal' } = {}) {
  const name = String(family || '').trim();
  if (!name) throw new Error('Keine Schrift angegeben.');
  const familyPart = encodeURIComponent(name).replace(/%20/g, '+').replace(/%2B/g, '+');
  const italic = style === 'italic' ? 1 : 0;
  const numeric = Number(weight);
  const axis = Number.isFinite(numeric) && numeric > 0 ? numeric : 400;
  return `${CSS_URL}?family=${familyPart}:ital,wght@${italic},${axis}&display=swap`;
}

/** Liest die TrueType-Adresse aus der CSS-Antwort von Google. */
export function parseGoogleFontTtfUrl(css) {
  const text = String(css || '');
  const match = /src:\s*url\((https:\/\/[^)]+)\)/.exec(text);
  return match ? match[1] : '';
}

function cachedFamily(name) {
  const wanted = String(name || '').trim().toLowerCase();
  return listCache.families.find((family) => family.name.toLowerCase() === wanted) || null;
}

function looksLikeTtf(buffer) {
  if (!buffer || buffer.length < 12) return false;
  const tag = buffer.readUInt32BE(0);
  // 0x00010000 = TrueType, OTTO = CFF/OpenType, true = Apple TrueType.
  return tag === 0x00010000 || tag === 0x4f54544f || tag === 0x74727565;
}

/** Lädt die TTF-Datei einer Variante (im Speicher gecacht) und liefert Bytes. */
export async function fetchGoogleFontFile(family, { weight = 400, style = 'normal' } = {}) {
  const name = String(family || '').trim();
  if (!name) throw new Error('Keine Schrift angegeben.');
  const numeric = Number(weight) || 400;
  const key = `${name.toLowerCase()}|${numeric}|${style === 'italic' ? 'i' : 'n'}`;
  const hit = fileCache.get(key);
  if (hit) return hit;

  const familyEntry = cachedFamily(name);
  let url = familyEntry?.files?.[`${numeric}${style === 'italic' ? 'i' : ''}`] || '';

  if (!url) {
    let response;
    try {
      response = await fetchWithTimeout(googleFontCssUrl(name, { weight: numeric, style }), {
        headers: { 'User-Agent': TTF_UA }
      });
    } catch {
      throw new Error('Die Schrift konnte nicht von Google Fonts geladen werden.');
    }
    const css = await response.text();
    if (response.status === 400 || response.status === 404) {
      throw new Error('Diese Schriftvariante gibt es bei Google Fonts nicht.');
    }
    if (!response.ok) throw new Error('Die Schrift konnte nicht von Google Fonts geladen werden.');
    url = parseGoogleFontTtfUrl(css);
    if (!url) throw new Error('Diese Schriftvariante gibt es bei Google Fonts nicht.');
  }

  let buffer;
  try {
    const response = await fetchWithTimeout(url, { headers: { 'User-Agent': TTF_UA } }, 30000);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    buffer = Buffer.from(await response.arrayBuffer());
  } catch {
    throw new Error('Die Schriftdatei konnte nicht heruntergeladen werden.');
  }
  if (!looksLikeTtf(buffer)) throw new Error('Google Fonts hat keine TrueType-Datei geliefert.');
  if (buffer.length > 32 * 1024 * 1024) throw new Error('Die Schriftdatei ist zu groß.');

  if (fileCache.size >= FILE_CACHE_MAX) fileCache.delete(fileCache.keys().next().value);
  const entry = { buffer, url };
  fileCache.set(key, entry);
  return entry;
}

/** Dateiname im Projekt, z. B. „Roboto-Flex-BoldItalic.ttf“. */
export function googleFontFileName(family, { weight = 400, style = 'normal' } = {}) {
  const base = String(family || '')
    .normalize('NFKD')
    .replace(/[\u0300-\u036f]/g, '')
    .replace(/[^A-Za-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 64) || 'Font';
  const numeric = Number(weight) || 400;
  const weightName = WEIGHT_NAMES[numeric] || String(numeric);
  return `${base}-${weightName}${style === 'italic' ? 'Italic' : ''}.ttf`;
}

/**
 * Lädt eine Schrift herunter und legt sie im Asset-Ordner des Projekts ab.
 * Liefert Pfad, relative Angabe für LOADFONT und die fertige Befehlszeile.
 */
export async function downloadGoogleFont({
  projectDir = '',
  dir = 'assets',
  family = '',
  weight = 400,
  style = 'normal'
} = {}) {
  if (!projectDir) throw new Error('Bitte zuerst ein Projekt öffnen.');
  const root = path.resolve(projectDir);
  const folder = path.resolve(root, String(dir || 'assets'));
  const relativeFolder = path.relative(root, folder);
  if (relativeFolder.startsWith('..') || path.isAbsolute(relativeFolder)) {
    throw new Error('Der Asset-Ordner liegt außerhalb des Projekts.');
  }

  const { buffer } = await fetchGoogleFontFile(family, { weight, style });
  const fileName = googleFontFileName(family, { weight, style });
  const target = path.join(folder, fileName);

  try {
    await fsp.mkdir(folder, { recursive: true });
    await fsp.writeFile(target, buffer);
  } catch (error) {
    throw new Error(`Die Schrift konnte nicht gespeichert werden: ${error.message}`);
  }

  // Pfad für GBX: immer mit /, unabhängig vom Betriebssystem.
  const relative = path.relative(root, target).split(path.sep).join('/');
  return {
    path: target,
    relative,
    bytes: buffer.length,
    command: `LOADFONT "${relative}", 1, 20`
  };
}
