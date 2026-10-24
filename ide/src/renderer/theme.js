/**
 * Erscheinungsbild der IDE.
 *
 * Aus drei Farben je Modus (Akzent, Hintergrund, Text) entstehen alle Flächen,
 * Rahmen und Zustandsfarben der Oberfläche. Die Werte landen als CSS-Variablen
 * auf <html>; das Editor-Theme kommt aus gbx-lang.js.
 */

import {
  applyEditorTheme,
  normalizeEditorTheme,
  DEFAULT_EDITOR_THEMES
} from './gbx-lang.js';

export const THEME_MODES = ['dark', 'light'];

export const MODE_LABELS = {
  dark: 'Dunkel',
  light: 'Hell'
};

export const DEFAULT_APPEARANCE = {
  mode: 'dark',
  themes: {
    dark: {
      accent: '#a6e22e',
      background: '#0e1116',
      text: '#dbe2ec',
      editor: DEFAULT_EDITOR_THEMES.dark
    },
    light: {
      accent: '#3f7d00',
      background: '#f6f8fa',
      text: '#1f2733',
      editor: DEFAULT_EDITOR_THEMES.light
    }
  }
};

const HEX_COLOR = /^#[0-9a-f]{6}$/i;

function isHexColor(value) {
  return typeof value === 'string' && HEX_COLOR.test(value.trim());
}

function channels(hex) {
  return [1, 3, 5].map((index) => parseInt(hex.slice(index, index + 2), 16));
}

function toHex(values) {
  return '#' + values.map((value) => Math.max(0, Math.min(255, Math.round(value))).toString(16).padStart(2, '0')).join('');
}

/** Mischt zwei Farben; amount 0 = from, 1 = to. */
function mix(from, to, amount) {
  const a = channels(from);
  const b = channels(to);
  return toHex(a.map((value, index) => value + (b[index] - value) * amount));
}

/** Relative Helligkeit (0 = schwarz, 1 = weiß) für die Textfarbe auf dem Akzent. */
function luminance(hex) {
  const [r, g, b] = channels(hex).map((value) => {
    const channel = value / 255;
    return channel <= 0.03928 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4;
  });
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

/**
 * Leitet aus Hintergrund, Text und Akzent alle Oberflächenfarben ab.
 * `mode` entscheidet nur über die Richtung der Abstufungen und die
 * Statusfarben; die drei Eingabefarben bestimmen den Rest.
 */
export function buildAppPalette(theme, mode = 'dark') {
  const dark = mode !== 'light';
  const bg = isHexColor(theme?.background) ? theme.background.trim().toLowerCase() : DEFAULT_APPEARANCE.themes[mode].background;
  const fg = isHexColor(theme?.text) ? theme.text.trim().toLowerCase() : DEFAULT_APPEARANCE.themes[mode].text;
  const accent = isHexColor(theme?.accent) ? theme.accent.trim().toLowerCase() : DEFAULT_APPEARANCE.themes[mode].accent;

  const step = (amount) => mix(bg, fg, amount);
  const accentContrast = luminance(accent) > 0.42 ? '#0b0f06' : '#ffffff';

  return {
    bg,
    bgElev: step(dark ? 0.055 : 0.045),
    bgElev2: step(dark ? 0.095 : 0.08),
    bgHover: step(dark ? 0.135 : 0.115),
    bgActive: step(dark ? 0.19 : 0.16),
    bgInput: dark ? mix(bg, '#000000', 0.22) : mix(bg, '#ffffff', 0.85),
    bgDeep: dark ? mix(bg, '#000000', 0.45) : mix(bg, fg, 0.045),
    border: step(dark ? 0.17 : 0.16),
    borderSoft: step(dark ? 0.11 : 0.1),
    borderStrong: step(dark ? 0.28 : 0.26),
    text: fg,
    textStrong: mix(fg, dark ? '#ffffff' : '#000000', 0.28),
    // Im hellen Modus muss gedämpfte Schrift dunkler werden, nicht heller.
    muted: mix(fg, bg, dark ? 0.42 : 0.26),
    muted2: mix(fg, bg, dark ? 0.62 : 0.36),
    accent,
    accentDim: mix(accent, fg, 0.25),
    accentContrast,
    accentSoft: mix(bg, accent, dark ? 0.17 : 0.14),
    accentSoftStrong: mix(bg, accent, dark ? 0.27 : 0.24),
    accentText: mix(fg, accent, 0.42),
    red: dark ? '#ff6b6b' : '#b3382c',
    amber: dark ? '#ffb454' : '#9a5b00',
    blue: dark ? '#6cb6ff' : '#1f6fd0',
    purple: dark ? '#c792ea' : '#7b3fa8',
    teal: dark ? '#5d8f8a' : '#2f6f6a',
    tintFaint: dark ? 'rgba(255,255,255,.03)' : 'rgba(15,23,42,.03)',
    tint: dark ? 'rgba(255,255,255,.05)' : 'rgba(15,23,42,.05)',
    tintStrong: dark ? 'rgba(255,255,255,.09)' : 'rgba(15,23,42,.08)',
    shadow: dark ? 'rgba(0,0,0,.45)' : 'rgba(15,23,42,.18)',
    shadowStrong: dark ? 'rgba(0,0,0,.55)' : 'rgba(15,23,42,.28)',
    backdrop: dark ? 'rgba(4,7,10,.66)' : 'rgba(15,23,42,.38)',
    scrollThumb: step(dark ? 0.22 : 0.2),
    scrollThumbHover: step(dark ? 0.34 : 0.3)
  };
}

/** Zuordnung der Palettenfelder auf die CSS-Variablen. */
export const CSS_VARS = {
  bg: '--bg',
  bgElev: '--bg-elev',
  bgElev2: '--bg-elev2',
  bgHover: '--bg-hover',
  bgActive: '--bg-active',
  bgInput: '--bg-input',
  bgDeep: '--bg-deep',
  border: '--border',
  borderSoft: '--border-soft',
  borderStrong: '--border-strong',
  text: '--text',
  textStrong: '--text-strong',
  muted: '--muted',
  muted2: '--muted2',
  accent: '--accent',
  accentDim: '--accent-dim',
  accentContrast: '--accent-contrast',
  accentSoft: '--accent-soft',
  accentSoftStrong: '--accent-soft-strong',
  accentText: '--accent-text',
  red: '--red',
  amber: '--amber',
  blue: '--blue',
  purple: '--purple',
  teal: '--teal',
  tintFaint: '--tint-faint',
  tint: '--tint',
  tintStrong: '--tint-strong',
  shadow: '--shadow',
  shadowStrong: '--shadow-strong',
  backdrop: '--backdrop',
  scrollThumb: '--scroll-thumb',
  scrollThumbHover: '--scroll-thumb-hover'
};

/**
 * Prüft gespeicherte Einstellungen und ergänzt fehlende Werte.
 * `legacyEditor` übernimmt eine früher gespeicherte Editor-Palette als
 * Dunkel-Satz, damit bestehende Installationen ihre Farben behalten.
 */
export function normalizeAppearance(spec, legacyEditor = null) {
  const out = { mode: 'dark', themes: {} };
  for (const mode of THEME_MODES) {
    const source = spec?.themes?.[mode] || {};
    const fallback = DEFAULT_APPEARANCE.themes[mode];
    const editorSource =
      source.editor || (mode === 'dark' && legacyEditor ? legacyEditor : null);
    out.themes[mode] = {
      accent: isHexColor(source.accent) ? source.accent.trim().toLowerCase() : fallback.accent,
      background: isHexColor(source.background) ? source.background.trim().toLowerCase() : fallback.background,
      text: isHexColor(source.text) ? source.text.trim().toLowerCase() : fallback.text,
      editor: normalizeEditorTheme(editorSource, mode)
    };
  }
  out.mode = spec?.mode === 'light' ? 'light' : 'dark';
  return out;
}

/**
 * Wendet einen Farbsatz auf die Oberfläche an: CSS-Variablen, color-scheme
 * und das Monaco-Theme des gewählten Modus.
 */
export function applyAppearance(monaco, spec) {
  const appearance = normalizeAppearance(spec);
  const theme = appearance.themes[appearance.mode];
  const palette = buildAppPalette(theme, appearance.mode);

  if (typeof document !== 'undefined') {
    const root = document.documentElement;
    for (const [key, variable] of Object.entries(CSS_VARS)) {
      root.style.setProperty(variable, palette[key]);
    }
    root.style.setProperty('--editor-bg', theme.editor.colors.background);
    root.style.colorScheme = appearance.mode;
    root.dataset.theme = appearance.mode;
  }

  if (monaco) applyEditorTheme(monaco, theme.editor, appearance.mode);
  return appearance;
}
