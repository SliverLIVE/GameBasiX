import {
  registerGbx,
  analyzeSource,
  suffixType,
  attachBlockAutoClose,
  attachKeywordCasing,
  findUserTypeRanges,
  EDITOR_THEME_FIELDS,
  EDITOR_THEME_PRESETS,
  EDITOR_THEME_PRESET_LABELS
} from './gbx-lang.js';
import { applyAppearance, normalizeAppearance, buildAppPalette, DEFAULT_APPEARANCE } from './theme.js';
import { t, getLanguage, setLanguage, initI18n, onLanguageChange, availableLanguages } from './i18n.js';
import { buildFileTree, countFiles } from './tree-model.js';
import { openGoogleFonts } from './google-fonts-tool.js';
import { createMediaView, mediaKind } from './media-preview.js';
import { checkLine, shiftLineNumbers } from './line-check.js';
import { createDock, DOCK_PANEL_IDS } from './dock.js';

const $ = (id) => document.getElementById(id);
const desktop = window.gbxStudioDesktop || null;

const state = {
  health: null,
  gbx: null,
  dataset: null,
  commands: [],
  appearance: null,
  templates: [],
  project: null,
  projectDir: '',
  tree: [],
  tabs: [],
  activePath: '',
  diagnostics: [],
  checked: false,
  buildJobId: '',
  runJobId: '',
  // Gesetzt, wenn der Nutzer das Schließen bestätigt hat (kein beforeunload mehr).
  allowUnload: false,
  building: false,
  // Zielplattform des Bauen-Knopfs: 'exe' oder 'html5'.
  buildTarget: localStorage.getItem('gbxstudio.buildTarget') === 'html5' ? 'html5' : 'exe',
  running: false,
  debug: emptyDebugState(),
  dirtySinceBuild: false,
  analysisCache: new Map(),
  projectSymbols: { functions: [], types: [], variables: [], constants: [] },
  ref: { q: '', category: '', kind: '', lang: 'en', selected: null },
  panel: 'problems',
  modalOpen: false,
  dragTab: '',
  dropTarget: null
};

let monaco = null;
let editor = null;
let lang = null;
let dock = null;
let persistTimer = null;
let indexTimer = null;

/* ------------------------------------------------------------------ */
/* Start                                                               */
/* ------------------------------------------------------------------ */

initI18n();

boot().catch((error) => {
  console.error(error);
  toast(t('Startfehler: {error}', { error: error.message || error }), 'error');
});

async function boot() {
  // Erscheinungsbild zuerst: Oberfläche und Modus stehen, bevor Monaco lädt.
  const health = await api('/api/health');
  state.health = health;
  state.gbx = health.gbx;
  state.cxx = health.cxx;
  state.emsdk = health.emsdk;
  state.html5 = health.html5;
  state.appearance = normalizeAppearance(health.config?.appearance, health.config?.editorTheme);
  applyAppearance(null, state.appearance);

  monaco = await loadMonaco();
  const dataset = await fetch('/api/dataset').then((r) => {
    if (!r.ok) throw new Error(t('Der GBX-Befehlsdatensatz fehlt. Bitte „npm run build:dataset“ ausführen.'));
    return r.json();
  });

  state.dataset = dataset;
  state.commands = dataset.commands || [];

  if (health.config?.language) setLanguage(health.config.language, { persist: true, notify: false });
  state.ref.lang = getLanguage();

  lang = registerGbx(monaco, dataset, { getSymbols, getAnalysis: analysisFor });
  applyAppearance(monaco, state.appearance);
  createEditor();
  bindUi();
  initDock();
  readOutlinePrefs();
  initSplitters();
  // Schmale Fenster starten ohne rechte Zone – die Panels bleiben erreichbar
  // über das Andock-Menü und den Referenz-Knopf.
  if (window.innerWidth < 1100) dock.setZoneOpen('right', false, { persist: false });
  fillReferenceFilters();
  renderReferenceList();
  renderRecent();
  renderOutline();
  updateGbxStatus();
  updateStatus();
  $('status-dataset').textContent = t('{n} GBX-Befehle', { n: state.commands.length });
  dock.setMeta('reference', `${state.commands.length}`);

  if (!state.gbx?.ok) {
    toast(t('gbx.exe wurde nicht gefunden. Bitte in den Einstellungen den Pfad prüfen.'), 'warn');
  }

  const last = state.health.config?.lastProject;
  if (last) {
    try {
      await openPath(last, { silent: true });
    } catch {
      showWelcome(true);
    }
  } else {
    showWelcome(true);
  }

  updateButtons();
  renderDebugScope();
  renderBreakpoints();
  updateDebugUi();
  window.__gbxStudio = { state, getSymbols, analyzeSource, editor: () => editor, monaco };
}

function loadMonaco() {
  return new Promise((resolve, reject) => {
    if (window.monaco) return resolve(window.monaco);
    if (typeof window.require !== 'function') return reject(new Error(t('Monaco-Loader nicht verfügbar.')));
    window.require(['vs/editor/editor.main'], () => {
      if (window.monaco) resolve(window.monaco);
      else reject(new Error(t('Monaco konnte nicht geladen werden.')));
    }, reject);
  });
}

function createEditor() {
  editor = monaco.editor.create($('editor'), {
    theme: 'gbx-theme',
    value: '',
    language: 'gbx',
    automaticLayout: true,
    fontSize: 13.5,
    lineHeight: 21,
    fontFamily: "'Cascadia Code', 'JetBrains Mono', Consolas, monospace",
    fontLigatures: true,
    glyphMargin: true,
    minimap: { enabled: true, maxColumn: 90, renderCharacters: false },
    scrollBeyondLastLine: false,
    smoothScrolling: true,
    cursorBlinking: 'smooth',
    cursorSmoothCaretAnimation: 'on',
    renderWhitespace: 'selection',
    tabSize: 4,
    insertSpaces: true,
    wordBasedSuggestions: 'off',
    suggest: {
      showWords: false,
      snippetsPreventQuickSuggestions: false,
      preview: true,
      showStatusBar: true
    },
    quickSuggestions: { other: true, comments: false, strings: false },
    fixedOverflowWidgets: true,
    padding: { top: 8, bottom: 30 },
    scrollbar: { verticalScrollbarSize: 11, horizontalScrollbarSize: 11 }
  });
  editor.setModel(null);

  editor.onDidChangeCursorPosition((event) => {
    updateCursorLabel(event.position);
    updateOutlineActive();
    scheduleLineCheck(60);
  });
  editor.onDidChangeModelContent((event) => {
    const model = editor.getModel();
    if (!model) return;
    lineErrors = shiftLineNumbers(errorsForModel(model), event.changes);
    modelLineErrors.set(model, lineErrors);
    refreshLineCheck();
    scheduleTypeColors();
    scheduleLiveCheck(model);
  });
  editor.onDidChangeModel(() => {
    const model = editor.getModel();
    lineErrors = model ? errorsForModel(model) : new Map();
    revalidateLineErrors();
    refreshLineCheck();
    scheduleTypeColors();
    scheduleLiveCheck(model, 120);
  });
  // Klick auf den Rand setzt oder entfernt einen Haltepunkt.
  editor.onMouseDown((event) => {
    const type = event.target.type;
    if (type !== monaco.editor.MouseTargetType.GUTTER_GLYPH_MARGIN && type !== monaco.editor.MouseTargetType.GUTTER_LINE_DECORATIONS) return;
    const line = event.target.position?.lineNumber;
    if (!line) return;
    toggleBreakpointAt(editor.getModel(), line);
  });
  editor.onDidScrollChange(() => updateGhost(editor.getModel(), editor.getPosition(), lastLineCheck));
  attachBlockAutoClose(monaco, editor);
  // Getippte Schlüsselwörter werden groß geschrieben (nicht in INLINE-Blöcken).
  attachKeywordCasing(monaco, editor, state.dataset);
  // Zusätzlich zu Ctrl+. öffnet Alt+Enter die Quickfixes am Fehler.
  editor.addAction({
    id: 'gbx.quickFix',
    label: t('Quickfix anzeigen'),
    keybindings: [monaco.KeyMod.Alt | monaco.KeyCode.Enter],
    run: (target) => target.getAction('editor.action.quickFix')?.run()
  });
  updateCursorLabel();

}

/* ------------------------------------------------------------------ */
/* Signaturleiste und Live-Prüfung der aktuellen Zeile                 */
/* ------------------------------------------------------------------ */

let lineCheckTimer = null;
let lastLineCheck = null;
// Fehler bleiben an ihrer Zeile stehen, bis genau diese Zeile korrigiert wird.
// Pro Modell eine eigene Liste, damit ein Tabwechsel nichts vergisst.
const modelLineErrors = new WeakMap();
let lineErrors = new Map();

function errorsForModel(model) {
  let map = modelLineErrors.get(model);
  if (!map) {
    map = new Map();
    modelLineErrors.set(model, map);
  }
  return map;
}

function hideSignatureBar() {
  const bar = $('signature-bar');
  if (bar) bar.hidden = true;
  clearGhost();
}

function clearGhost() {
  const ghost = $('sig-ghost');
  if (ghost) ghost.hidden = true;
}

/** Blasser Vorschlag des nächsten Arguments direkt hinter dem Cursor. */
function updateGhost(model, position, result) {
  const ghost = $('sig-ghost');
  if (!ghost || !model || !position) return clearGhost();
  const line = model.getLineContent(position.lineNumber);
  const beforeCaret = line.slice(0, position.column - 1);
  const afterCaret = line.slice(position.column - 1);
  // Nur direkt nach einem Komma oder einer öffnenden Klammer vorschlagen.
  const pending = afterCaret.trim() === '' && /[(,]\s*$/.test(beforeCaret);
  const param = result?.params?.[result.activeIndex];
  const show = pending && param && result.status !== 'none' && result.found === result.activeIndex;
  const spot = show ? editor.getScrolledVisiblePosition(position) : null;
  if (!spot) return clearGhost();
  ghost.textContent = ' ' + param.label;
  ghost.style.left = `${spot.left}px`;
  ghost.style.top = `${spot.top}px`;
  ghost.style.height = `${spot.height}px`;
  ghost.style.lineHeight = `${spot.height}px`;
  ghost.hidden = false;
}

/** Zeigt Name und Parameter des aktuellen Befehls; das aktive Argument blau. */
function renderSignatureBar(result) {
  const bar = $('signature-bar');
  if (!bar) return;
  if (!result || result.status === 'none' || !result.show) {
    bar.hidden = true;
    return;
  }
  $('sig-name').textContent = result.name;
  const params = $('sig-params');
  params.replaceChildren();
  if (result.parens && result.params.length) {
    const open = document.createElement('span');
    open.className = 'sig-paren';
    open.textContent = '(';
    params.append(open);
  }
  result.params.forEach((param, index) => {
    if (index) {
      const separator = document.createElement('span');
      separator.className = 'sig-sep';
      separator.textContent = ',';
      params.append(separator);
    }
    const span = document.createElement('span');
    span.className =
      'sig-param' +
      (index === result.activeIndex ? ' active' : '') +
      (param.optional ? ' optional' : '');
    span.textContent = param.label;
    params.append(span);
  });
  if (result.parens && result.params.length) {
    const close = document.createElement('span');
    close.className = 'sig-paren';
    close.textContent = ')';
    params.append(close);
  }
  const error = $('sig-error');
  error.textContent = result.status === 'error' ? result.message : '';
  error.hidden = result.status !== 'error';
  bar.classList.toggle('error', result.status === 'error');
  bar.hidden = false;
}

/** C++-Zeilen zwischen INLINE und ENDINLINE werden nicht geprüft. */
function isInsideInline(model, lineNumber) {
  for (let line = lineNumber - 1; line >= 1; line--) {
    const text = model.getLineContent(line).replace(/\/\/.*$/, '').trim();
    if (/^ENDINLINE\b/i.test(text)) return false;
    if (/^INLINE\b/i.test(text)) return true;
  }
  return false;
}

/** Prüft eine einzelne Zeile mit dem aktuellen Spaltenstand. */
function inspectLine(model, lineNumber, caretColumn) {
  if (!lang?.byName) return null;
  try {
    return checkLine(model.getLineContent(lineNumber), { byName: lang.byName, caretColumn });
  } catch {
    return null;
  }
}

/** Zeichnet die gemerkten Fehler als Marker – auch weit weg vom Cursor. */
function renderLineErrorMarkers(model) {
  if (!monaco || !model) return;
  const markers = [];
  const lineCount = model.getLineCount();
  // Wo die Live-Prüfung schon eine Stelle meldet, bleibt deren Marker allein.
  const liveLines = new Set((liveResults.get(model) || []).map((d) => d.line));
  for (const [line, info] of lineErrors) {
    if (line < 1 || line > lineCount) continue;
    if (liveLines.has(line)) continue;
    const maxColumn = model.getLineMaxColumn(line);
    const startColumn = Math.max(1, Math.min(info.startColumn, maxColumn));
    const endColumn = Math.max(startColumn, Math.min(info.endColumn, maxColumn));
    markers.push({
      severity: monaco.MarkerSeverity.Error,
      message: info.message,
      source: 'GBX',
      startLineNumber: line,
      startColumn,
      endLineNumber: line,
      endColumn
    });
  }
  monaco.editor.setModelMarkers(model, 'gbx-line', markers);
}

function refreshLineCheck() {
  if (!editor) return;
  const model = editor.getModel();
  const position = editor.getPosition();
  if (!model || !position) {
    hideSignatureBar();
    return;
  }
  const result = isInsideInline(model, position.lineNumber)
    ? null
    : inspectLine(model, position.lineNumber, position.column);
  lastLineCheck = result;
  // Merken statt vergessen: die Zeile bleibt markiert, bis sie stimmt.
  if (result && result.status === 'error') {
    lineErrors.set(position.lineNumber, {
      message: result.message,
      column: position.column,
      startColumn: result.range.startColumn,
      endColumn: result.range.endColumn
    });
  } else {
    lineErrors.delete(position.lineNumber);
  }
  renderSignatureBar(result);
  updateGhost(model, position, result);
  renderLineErrorMarkers(model);
}

/** Übersetzt bereits gemerkte Meldungen neu, wenn DE/EN umgeschaltet wird. */
function revalidateLineErrors() {
  const model = editor?.getModel();
  if (!model) return;
  const next = new Map();
  for (const line of lineErrors.keys()) {
    if (line < 1 || line > model.getLineCount()) continue;
    const result = inspectLine(model, line, lineErrors.get(line)?.column || 1);
    if (result && result.status === 'error') {
      next.set(line, { message: result.message, column: lineErrors.get(line)?.column, startColumn: result.range.startColumn, endColumn: result.range.endColumn });
    }
  }
  lineErrors = next;
  modelLineErrors.set(model, lineErrors);
  renderLineErrorMarkers(model);
}

function scheduleLineCheck(delay = 60) {
  clearTimeout(lineCheckTimer);
  lineCheckTimer = setTimeout(refreshLineCheck, delay);
}

/* ------------------------------------------------------------------ */
/* Live-Prüfung: Syntaxfehler schon beim Tippen                       */
/* ------------------------------------------------------------------ */

// Der ungespeicherte Editorinhalt geht an den Interpreter (--check --stdin).
// Geprüft wird erst nach einer kurzen Pause; ältere Antworten werden verworfen.
const LIVE_CHECK_DELAY = 500;
let liveCheckTimer = null;
let liveCheckToken = 0;
let liveResults = new WeakMap();

function setLiveStatus(text, { tone = '', title = '' } = {}) {
  const el = $('status-live');
  if (!el) return;
  el.textContent = text;
  el.title = title || text;
  el.classList.toggle('ok', tone === 'ok');
  el.classList.toggle('err', tone === 'err');
  el.hidden = !text;
}

/** Entfernt die Live-Marker; ohne Modell in allen geöffneten Dateien. */
function clearLiveMarkers(model = null) {
  if (!monaco) return;
  for (const target of model ? [model] : monaco.editor.getModels()) {
    monaco.editor.setModelMarkers(target, 'gbx-live', []);
  }
}

/** Verwirft ein laufendes Ergebnis und räumt die Anzeige auf. */
function cancelLiveCheck() {
  clearTimeout(liveCheckTimer);
  liveCheckTimer = null;
  liveCheckToken++;
  liveResults = new WeakMap();
  clearLiveMarkers();
  setLiveStatus('');
}

function scheduleLiveCheck(model = editor?.getModel(), delay = LIVE_CHECK_DELAY) {
  clearTimeout(liveCheckTimer);
  if (!model || !state.projectDir || model.isDisposed()) return;
  if (model.getLanguageId() !== 'gbx') return;
  const token = ++liveCheckToken;
  liveCheckTimer = setTimeout(() => runLiveCheck(model, token), delay);
}

async function runLiveCheck(model, token) {
  if (!model || model.isDisposed() || !state.projectDir || state.building) return;
  const rel = relativeToProject(model.uri.fsPath);
  if (!rel) return;
  setLiveStatus(t('Prüfe …'));
  // Nur die Einstiegsdatei kennt alle Namen des Projekts. In Teildateien
  // würde die Namensprüfung Variablen der anderen Dateien anmeckern.
  const entry = String(state.project?.entry || '').replace(/\\/g, '/').toLowerCase();
  const syntaxOnly = !!entry && rel.replace(/\\/g, '/').toLowerCase() !== entry;
  try {
    const result = await post('/api/check/live', {
      projectDir: state.projectDir,
      file: rel,
      source: model.getValue(),
      syntaxOnly
    });
    if (token !== liveCheckToken || model.isDisposed()) return;
    if (!result.available) {
      // Ohne Interpreter bleibt die Meldung der Syntaxprüfung (F7) maßgeblich.
      setLiveStatus('');
      return;
    }
    // Beim Tippen führt die Live-Prüfung die Marker; die alten Marker der
    // letzten Prüfung (F7) würden sonst doppelt erscheinen.
    monaco.editor.setModelMarkers(model, 'gbx', []);
    applyLiveDiagnostics(model, result.diagnostics || []);
  } catch (error) {
    if (token !== liveCheckToken) return;
    setLiveStatus(t('Live-Prüfung nicht möglich'), { tone: 'err', title: error.message });
  }
}

/** Zeigt die Live-Fehler als Marker und in der Statusleiste. */
function applyLiveDiagnostics(model, diagnostics) {
  // Dieselben Tipps wie in der Problemliste: Tippfehler bekommen einen Vorschlag.
  const list = diagnostics.map((d) => enrichDiagnostic(d));
  liveResults.set(model, list);
  const markers = list.map((d) => {
    const line = Math.min(Math.max(1, d.line || 1), model.getLineCount());
    const text = model.getLineContent(line);
    const maxColumn = Math.max(1, text.length + 1);
    const startColumn = Math.min(Math.max(1, d.column || 1), maxColumn);
    // Benennt die Meldung einen Namen, wird genau der angestrichen.
    const endColumn = d.tokenLength
      ? Math.min(startColumn + d.tokenLength, maxColumn)
      : Math.max(startColumn + 1, text.replace(/\s+$/, '').length + 1);
    return {
      severity: monaco.MarkerSeverity.Error,
      message: d.hint ? `${d.message}\n\n${d.hint}` : d.message,
      source: 'GBX',
      startLineNumber: line,
      startColumn,
      endLineNumber: line,
      endColumn
    };
  });
  monaco.editor.setModelMarkers(model, 'gbx-live', markers);
  if (editor?.getModel() === model) renderLineErrorMarkers(model);

  const errors = list.filter((d) => d.severity === 'error').length;
  if (!errors) {
    setLiveStatus(t('Keine Fehler'), { tone: 'ok', title: t('Keine Syntaxfehler gefunden.') });
    return;
  }
  const details = list.map((d) => `${d.line}: ${d.message}`).join('\n');
  setLiveStatus(errors === 1 ? t('1 Fehler') : t('{count} Fehler', { count: errors }), {
    tone: 'err',
    title: `${t('Live-Prüfung')}\n${details}`
  });
}

const inlineDecorations = new WeakMap();

/** Hebt alle Zeilen zwischen INLINE und ENDINLINE als eigenen Block hervor. */
function updateInlineHighlights(model = editor?.getModel()) {
  if (!model || !editor || editor.getModel() !== model) return;
  const blocks = [];
  let inside = false;
  let blockStart = 0;
  for (let line = 1; line <= model.getLineCount(); line++) {
    const text = model.getLineContent(line).trim();
    if (!inside && /^INLINE\b/i.test(text)) {
      inside = true;
      blockStart = line + 1;
    } else if (inside && /^ENDINLINE\b/i.test(text)) {
      if (blockStart <= line - 1) blocks.push({ start: blockStart, end: line - 1 });
      inside = false;
    }
  }
  if (inside && blockStart <= model.getLineCount()) blocks.push({ start: blockStart, end: model.getLineCount() });

  const decorations = blocks.map((block) => ({
    range: new monaco.Range(block.start, 1, block.end, 1),
    options: {
      isWholeLine: true,
      className: 'gbx-inline-block'
    }
  }));
  const previous = inlineDecorations.get(model) || [];
  inlineDecorations.set(model, editor.deltaDecorations(previous, decorations));
}

/* ------------------------------------------------------------------ */
/* Eigene TYPEs einfärben                                              */
/* ------------------------------------------------------------------ */

// Die Farbe steht in den Editor-Einstellungen (CSS-Variable --gbx-user-type).
const typeDecorations = new WeakMap();
let typeColorTimer = null;

function refreshTypeColors() {
  if (!monaco || !editor) return;
  let names = [];
  try {
    names = getSymbols().types.map((type) => type.name).filter(Boolean);
  } catch {
    return;
  }
  for (const model of monaco.editor.getModels()) {
    if (model.getLanguageId() !== 'gbx') continue;
    const ranges = findUserTypeRanges(model.getValue(), names).slice(0, 4000);
    const decorations = ranges.map((range) => ({
      range: new monaco.Range(range.line, range.startColumn, range.line, range.endColumn),
      options: { inlineClassName: 'gbx-user-type' }
    }));
    const previous = typeDecorations.get(model) || [];
    typeDecorations.set(model, model.deltaDecorations(previous, decorations));
  }
}

function scheduleTypeColors() {
  clearTimeout(typeColorTimer);
  typeColorTimer = setTimeout(refreshTypeColors, 200);
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

async function api(path, options = {}) {
  const opts = { ...options, headers: { ...(options.headers || {}) } };
  if (opts.body !== undefined && typeof opts.body !== 'string') {
    opts.headers['Content-Type'] = 'application/json';
    opts.body = JSON.stringify(opts.body);
  }
  let response;
  try {
    response = await fetch(path, opts);
  } catch (error) {
    throw new Error(t('Server nicht erreichbar: {error}', { error: error.message }));
  }
  let data = null;
  try {
    data = await response.json();
  } catch {
    data = null;
  }
  if (!response.ok || (data && data.ok === false)) {
    throw new Error(data?.error || t('Anfrage fehlgeschlagen ({status})', { status: response.status }));
  }
  return data;
}

const get = (path) => api(path);
const post = (path, body) => api(path, { method: 'POST', body });

/* ------------------------------------------------------------------ */
/* UI binding                                                          */
/* ------------------------------------------------------------------ */

function bindUi() {
  $('btn-new-project').addEventListener('click', newProjectDialog);
  $('btn-open').addEventListener('click', chooseProject);
  $('btn-save').addEventListener('click', () => saveCurrent());
  $('btn-build').addEventListener('click', () => buildProject());
  $('btn-run').addEventListener('click', runProgram);
  $('btn-stop').addEventListener('click', stopProgram);
  $('btn-debug').addEventListener('click', startDebug);
  $('btn-export').addEventListener('click', buildForTarget);
  const platform = $('target-platform');
  platform.value = state.buildTarget;
  platform.addEventListener('change', () => {
    state.buildTarget = platform.value === 'html5' ? 'html5' : 'exe';
    try {
      localStorage.setItem('gbxstudio.buildTarget', state.buildTarget);
    } catch {
      /* Speicher nicht verfügbar */
    }
    updateButtons();
  });
  $('btn-debug-start-panel').addEventListener('click', startDebug);
  $('btn-debug-continue').addEventListener('click', () => debugCommand('run'));
  $('btn-debug-pause').addEventListener('click', () => debugCommand('pause'));
  $('btn-debug-over').addEventListener('click', () => debugCommand('next'));
  $('btn-debug-into').addEventListener('click', () => debugCommand('step'));
  $('btn-debug-out').addEventListener('click', () => debugCommand('out'));
  $('btn-debug-stop').addEventListener('click', stopDebug);
  $('btn-settings').addEventListener('click', settingsDialog);
  $('btn-lang').addEventListener('click', () => switchLanguage(getLanguage() === 'de' ? 'en' : 'de'));
  $('btn-tools').addEventListener('click', (event) => {
    const rect = event.currentTarget.getBoundingClientRect();
    showContextMenu(rect.left, rect.bottom + 4, [
      {
        label: t('Google Fonts…'),
        action: () =>
          openGoogleFonts({
            t,
            openModal,
            toast,
            get,
            post,
            projectDir: () => state.projectDir,
            project: () => state.project
          })
      }
    ]);
  });
  $('btn-toggle-ref').addEventListener('click', () => toggleReference());
  $('btn-refresh').addEventListener('click', refreshTree);
  $('btn-open-folder').addEventListener('click', () => {
    if (!state.projectDir) return toast(t('Bitte zuerst ein Projekt öffnen.'), 'warn');
    openInExplorer(state.projectDir);
  });
  // Ohne Wrapper landete das Klick-Event als Ordnername im Dialog.
  $('btn-new-file').addEventListener('click', () => newFileDialog());
  $('btn-new-folder').addEventListener('click', () => newFolderDialog());
  const fileTree = $('file-tree');
  // Rechtsklick auf die freie Fläche des Dateibaums legt neue Einträge an.
  fileTree.addEventListener('contextmenu', (event) => {
    if (!state.project || event.target.closest('.tree-row')) return;
    event.preventDefault();
    showTreeMenu(event, null);
  });
  // Ablegen auf der freien Fläche verschiebt in den Projektordner.
  fileTree.addEventListener('dragover', (event) => {
    if (event.target.closest('.tree-row') || !canDropIntoFolder('')) return;
    event.preventDefault();
    if (event.dataTransfer) event.dataTransfer.dropEffect = 'move';
    fileTree.classList.add('drop-root');
  });
  fileTree.addEventListener('dragleave', (event) => {
    if (event.relatedTarget && fileTree.contains(event.relatedTarget)) return;
    fileTree.classList.remove('drop-root');
  });
  fileTree.addEventListener('drop', (event) => {
    fileTree.classList.remove('drop-root');
    if (event.target.closest('.tree-row') || !canDropIntoFolder('')) return;
    event.preventDefault();
    const from = treeDragPath;
    clearTreeDrag();
    moveTreeEntry(from, '').catch((error) => toast(error.message, 'error'));
  });
  $('welcome-new').addEventListener('click', newProjectDialog);
  $('welcome-open').addEventListener('click', chooseProject);
  $('btn-toggle-panel').addEventListener('click', togglePanel);
  // Ablegen rechts neben dem letzten Tab (leerer Bereich der Leiste).
  const tabbar = $('tabbar');
  tabbar.addEventListener('dragover', (event) => {
    if (!state.dragTab) return;
    autoScrollTabbar(event.clientX);
    if (event.target.closest?.('.tab')) return;
    event.preventDefault();
    clearTabDropMarkers();
    tabbar.classList.add('drop-end');
  });
  tabbar.addEventListener('dragleave', (event) => {
    if (event.target === tabbar) tabbar.classList.remove('drop-end');
  });
  tabbar.addEventListener('drop', (event) => {
    if (!state.dragTab || event.target.closest?.('.tab')) return;
    event.preventDefault();
    const path = state.dragTab;
    tabbar.classList.remove('drop-end');
    moveTab(path, '', false);
  });
  readTreePrefs();
  // Klick außerhalb schließt das Kontextmenü, Escape ebenso.
  document.addEventListener('mousedown', (event) => {
    if (contextMenuEl && !contextMenuEl.contains(event.target)) closeContextMenu();
  });
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') closeContextMenu();
  });
  // Pfade aus dem Dateibaum im Editor ablegen; zudem verhindern, dass hier
  // versehentlich abgelegte Dateien die Oberfläche ersetzen.
  const PATH_DRAG_TYPE = 'application/x-gbxstudio-path';
  document.addEventListener('dragover', (event) => {
    const types = event.dataTransfer?.types;
    if (!types) return;
    if (types.includes('Files')) {
      event.preventDefault();
      return;
    }
    if (!types.includes(PATH_DRAG_TYPE) || !event.target.closest?.('.monaco-editor')) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = 'copy';
  }, true);
  document.addEventListener('drop', (event) => {
    const types = event.dataTransfer?.types;
    if (!types) return;
    if (types.includes('Files')) {
      event.preventDefault();
      return;
    }
    if (!types.includes(PATH_DRAG_TYPE) || !event.target.closest?.('.monaco-editor')) return;
    event.preventDefault();
    event.stopPropagation();
    const rel = event.dataTransfer.getData(PATH_DRAG_TYPE);
    if (rel) insertPathAtCursor(rel);
  }, true);
  onLanguageChange(() => {
    state.ref.lang = getLanguage();
    dock?.render();
    renderTree();
    renderTabs();
    renderProblems();
    fillReferenceFilters();
    renderReferenceList();
    renderReferenceDetail();
    renderRecent();
    updateGbxStatus();
    updateStatus();
    updateCursorLabel();
    revalidateLineErrors();
    refreshLineCheck();
    renderOutline();
    renderDebugScope();
    renderBreakpoints();
    updateDebugUi();
    $('status-dataset').textContent = t('{n} GBX-Befehle', { n: state.commands.length });
  });
  $('ref-search').addEventListener('input', (event) => {
    state.ref.q = event.target.value;
    renderReferenceList();
  });
  $('ref-category').addEventListener('change', (event) => {
    state.ref.category = event.target.value;
    renderReferenceList();
  });
  $('ref-kind').addEventListener('change', (event) => {
    state.ref.kind = event.target.value;
    renderReferenceList();
  });

  // Capture-Phase: greift auch, wenn der Fokus außerhalb des Editors liegt
  // (Dateibaum, Referenzsuche, Eingabefelder), und verhindert das Standard-
  // Verhalten von Browser/Electron („Seite speichern“).
  window.addEventListener('keydown', (event) => {
    const mod = event.ctrlKey || event.metaKey;
    const withMod = mod && !event.altKey;
    const code = event.code;
    const key = event.key.toLowerCase();
    if (event.key === 'Escape' && state.modalOpen) return;
    let handled = true;
    if (event.key === 'F7') {
      buildProject();
    } else if (event.key === 'F6') {
      if (event.shiftKey) stopDebug();
      else startDebug();
    } else if (event.key === 'F5') {
      if (state.debug.active) debugCommand('run');
      else if (event.shiftKey) stopProgram();
      else runProgram();
    } else if (event.key === 'F8') {
      debugCommand('run');
    } else if (event.key === 'F9') {
      toggleBreakpointAt(editor?.getModel(), editor?.getPosition()?.lineNumber);
    } else if (event.key === 'F10') {
      debugCommand('next');
    } else if (event.key === 'F11') {
      debugCommand(event.shiftKey ? 'out' : 'step');
    } else if (withMod && (code === 'KeyE' || key === 'e')) {
      if (!event.repeat) buildForTarget();
    } else if (withMod && (code === 'KeyS' || key === 's')) {
      if (!event.repeat) {
        if (event.shiftKey) saveAll({ silent: false });
        else saveCurrent();
      }
    } else if (withMod && (code === 'KeyO' || key === 'o')) {
      chooseProject();
    } else if (withMod && (code === 'KeyN' || key === 'n')) {
      newFileDialog();
    } else if (withMod && (code === 'KeyW' || key === 'w')) {
      if (state.activePath) closeTab(state.activePath);
    } else if (withMod && (code === 'KeyB' || key === 'b')) {
      toggleReference();
    } else if (withMod && event.key === '`') {
      togglePanel();
    } else {
      handled = false;
    }
    if (handled) {
      event.preventDefault();
      event.stopPropagation();
    }
  }, true);

  window.addEventListener('beforeunload', (event) => {
    if (state.allowUnload) return;
    if (state.tabs.some((t) => t.dirty)) {
      event.preventDefault();
      event.returnValue = '';
    }
  });

  // Das X im Fensterrahmen: gleiche Rückfrage wie beim Schließen eines Tabs.
  if (desktop?.onCloseRequest) desktop.onCloseRequest(requestWindowClose);
}

/** Vor dem Schließen des Fensters nach ungespeicherten Änderungen fragen. */
async function requestWindowClose() {
  // Zweiter Klick aufs X, während die Rückfrage offen ist, öffnet keine zweite.
  if (state.modalOpen) return;
  const dirty = state.tabs.filter((tab) => tab.dirty);
  if (!dirty.length) {
    state.allowUnload = true;
    desktop?.confirmClose?.();
    return;
  }
  const message =
    dirty.length === 1
      ? t('{path} wurde geändert. Speichern?', { path: dirty[0].path })
      : t('{n} Dateien wurden geändert. Speichern?', { n: dirty.length });
  const choice = await saveChangesDialog(
    t('Ungespeicherte Änderungen'),
    message,
    t('Speichern & schließen')
  );
  if (choice === null) return;
  if (choice === 'save' && !(await saveAll())) return;
  state.allowUnload = true;
  desktop?.confirmClose?.();
}

function updateButtons() {
  const hasProject = !!state.project;
  const current = activeTab();
  $('btn-save').disabled = !current || !current.model;
  $('btn-build').disabled = !hasProject || state.building;
  $('btn-run').disabled = !hasProject || state.building || state.debug.active;
  $('btn-debug').disabled = !hasProject || state.building || state.debug.active;
  $('btn-debug').hidden = state.debug.active;
  $('btn-export').disabled = !hasProject || state.building || state.running || state.debug.active;
  $('target-platform').disabled = !hasProject || state.building;
  const html5Ready = Boolean(state.emsdk?.ok || state.html5?.playerReady);
  $('btn-export').title =
    state.buildTarget === 'html5'
      ? html5Ready
        ? t('Spiel für den Browser bauen')
        : t('Für HTML5 zuerst emsdk in den Einstellungen einrichten.')
      : t('Spiel als EXE erstellen (Strg+E)');
  $('btn-refresh').disabled = !hasProject;
  $('btn-new-file').disabled = !hasProject;
  $('btn-new-folder').disabled = !hasProject;
  $('btn-run').hidden = state.running;
  $('btn-stop').hidden = !state.running;
}

function updateGbxStatus() {
  const gbx = state.gbx;
  const dot = $('sdk-dot');
  if (!gbx) return;
  dot.className = 'dot ' + (gbx.ok ? 'ok' : 'err');
  $('sdk-label').textContent = gbx.ok
    ? t('GameBasiX {version} · bereit', { version: gbx.version || '' })
    : t('gbx.exe nicht gefunden');
  $('sdk-label').title = gbx.path;
}

function updateStatus() {
  const p = state.project;
  $('status-platform').textContent = p ? `${p.width || 640}×${p.height || 480} · ${p.frameRate ?? 60} FPS` : '—';
  $('status-path').textContent = state.projectDir || '';
  $('status-path').title = state.projectDir || '';
  $('project-name').textContent = p ? p.name : t('Kein Projekt geöffnet');
  $('project-name').title = p ? state.projectDir : '';
  dock?.refreshTitles();
  if (!editor?.getModel()) updateCursorLabel({ lineNumber: 1, column: 1 });
}

function updateCursorLabel(position = editor?.getPosition()) {
  const pos = position || { lineNumber: 1, column: 1 };
  $('status-cursor').textContent = t('Ln {line}, Sp {col}', { line: pos.lineNumber, col: pos.column });
}

function setBuildStatus(text, kind = '') {
  const el = $('status-build');
  el.textContent = text;
  el.style.color = kind === 'error' ? 'var(--red)' : kind === 'ok' ? 'var(--accent)' : 'var(--muted2)';
}

async function switchLanguage(lang) {
  setLanguage(lang);
  state.ref.lang = getLanguage();
  try {
    const settings = await post('/api/settings', { language: getLanguage() });
    if (settings.config) state.health.config = settings.config;
  } catch {
    /* Sprache ist lokal gespeichert und gilt trotzdem. */
  }
}

/* ------------------------------------------------------------------ */
/* Fensteraufteilung                                                   */
/* ------------------------------------------------------------------ */

const LAYOUT_KEY = 'gbxstudio.layout.v1';
const DEFAULT_LAYOUT = { sidebar: 248, reference: 352, bottom: 208 };

/**
 * Baut die Andockzonen und verteilt die Panel-Inhalte. Die Zonen sind die
 * bisherigen Behälter (linke Spalte, rechte Spalte, unterer Bereich); die
 * Panels darin wandern je nach Ablage.
 */
function initDock() {
  const sources = {};
  for (const id of DOCK_PANEL_IDS) {
    const el = document.querySelector(`#dock-sources .dock-src[data-panel="${id}"]`);
    if (el) sources[id] = el;
  }
  dock = createDock({
    t,
    hosts: {
      left: $('sidebar'),
      right: $('reference'),
      bottom: $('bottom-panel'),
      tabs: $('panel-tabs'),
      body: $('panel-body'),
      bottomSplitter: $('split-bottom')
    },
    sources,
    title: dockTitle,
    onChange: () => editor?.layout()
  });
  dock.render();
}

function dockTitle(id) {
  switch (id) {
    case 'files':
      return t('Dateien');
    case 'outline':
      return t('Outline');
    case 'reference':
      return t('Referenz');
    case 'problems':
      return t('Probleme');
    case 'debug':
      return t('Debugger');
    case 'output':
      return t('Prüf-Ausgabe');
    case 'console':
      return t('Konsole');
    default:
      return id;
  }
}

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function readLayout() {
  try {
    const parsed = JSON.parse(localStorage.getItem(LAYOUT_KEY) || '{}');
    return {
      sidebar: Number.isFinite(parsed.sidebar) ? parsed.sidebar : null,
      reference: Number.isFinite(parsed.reference) ? parsed.reference : null,
      bottom: Number.isFinite(parsed.bottom) ? parsed.bottom : null
    };
  } catch {
    return { sidebar: null, reference: null, bottom: null };
  }
}

function persistLayout() {
  const layout = {
    sidebar: Math.round($('sidebar').getBoundingClientRect().width),
    reference: Math.round($('reference').getBoundingClientRect().width),
    bottom: Math.round($('bottom-panel').getBoundingClientRect().height)
  };
  try {
    localStorage.setItem(LAYOUT_KEY, JSON.stringify(layout));
  } catch {
    /* Speicher nicht verfügbar */
  }
}

function setLayoutVar(name, value) {
  document.documentElement.style.setProperty(name, `${Math.round(value)}px`);
}

function initSplitters() {
  const saved = readLayout();
  if (saved.sidebar) setLayoutVar('--sidebar', clamp(saved.sidebar, 160, Math.min(560, window.innerWidth * 0.5)));
  if (saved.reference) setLayoutVar('--reference', clamp(saved.reference, 230, Math.min(720, window.innerWidth * 0.6)));
  if (saved.bottom) setLayoutVar('--bottom-panel', saved.bottom);

  bindSplitter($('split-left'), {
    axis: 'x',
    start: () => $('sidebar').getBoundingClientRect().width,
    apply: (size) => setLayoutVar('--sidebar', clamp(size, 160, Math.min(560, window.innerWidth * 0.5)))
  });
  bindSplitter($('split-right'), {
    axis: 'x',
    invert: true,
    start: () => $('reference').getBoundingClientRect().width,
    apply: (size) => setLayoutVar('--reference', clamp(size, 230, Math.min(720, window.innerWidth * 0.6)))
  });
  bindSplitter($('split-bottom'), {
    axis: 'y',
    invert: true,
    start: () => {
      // Eine eingeklappte Zone aufziehen: der Merker in der Ablage muss mit.
      if (dock && !dock.layout.open.bottom) dock.setZoneOpen('bottom', true);
      return parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--bottom-panel')) ||
        $('bottom-panel').getBoundingClientRect().height ||
        DEFAULT_LAYOUT.bottom;
    },
    apply: (size) => {
      const max = Math.max(160, document.querySelector('.main-area').clientHeight - 140);
      setLayoutVar('--bottom-panel', clamp(size, 90, max));
    }
  });
  // Panels in den Seitenzonen bekommen ihre eigene Trennlinie (dock.js).
}

function bindSplitter(el, { axis, invert = false, start, apply }) {
  if (!el) return;
  el.addEventListener('pointerdown', (event) => {
    if (event.button !== 0) return;
    event.preventDefault();
    try {
      el.setPointerCapture(event.pointerId);
    } catch {
      /* synthetische Events ohne aktiven Pointer */
    }
    el.classList.add('dragging');
    document.body.classList.add('resizing', axis === 'x' ? 'resizing-col' : 'resizing-row');
    const origin = axis === 'x' ? event.clientX : event.clientY;
    const startSize = start();
    let delta = 0;
    let frame = 0;
    const paint = () => {
      frame = 0;
      apply(startSize + (invert ? -delta : delta));
      editor?.layout();
    };
    const move = (moveEvent) => {
      delta = (axis === 'x' ? moveEvent.clientX : moveEvent.clientY) - origin;
      if (!frame) frame = requestAnimationFrame(paint);
    };
    const finish = () => {
      el.removeEventListener('pointermove', move);
      el.removeEventListener('pointerup', finish);
      el.removeEventListener('pointercancel', finish);
      if (frame) cancelAnimationFrame(frame);
      paint();
      el.classList.remove('dragging');
      document.body.classList.remove('resizing', 'resizing-col', 'resizing-row');
      persistLayout();
    };
    el.addEventListener('pointermove', move);
    el.addEventListener('pointerup', finish, { once: true });
    el.addEventListener('pointercancel', finish, { once: true });
  });
  el.addEventListener('dblclick', () => {
    if (axis === 'x' && el.id === 'split-left') setLayoutVar('--sidebar', DEFAULT_LAYOUT.sidebar);
    else if (axis === 'x') setLayoutVar('--reference', DEFAULT_LAYOUT.reference);
    else setLayoutVar('--bottom-panel', DEFAULT_LAYOUT.bottom);
    editor?.layout();
    persistLayout();
  });
}

/* ------------------------------------------------------------------ */
/* Projekt                                                             */
/* ------------------------------------------------------------------ */

async function chooseProject() {
  if (desktop) {
    const picked = await desktop.openProject();
    if (picked) {
      try {
        await openPath(picked);
      } catch (error) {
        toast(error.message, 'error');
      }
    }
    return;
  }
  const fallback = state.health?.config?.lastProject || state.health?.defaultProjectParent || '';
  openPathDialog(fallback);
}

function openPathDialog(defaultPath) {
  const body = document.createElement('div');
  body.innerHTML = `
    <div class="field">
      <label>${t('Projektordner, .gbxproj oder .gbx')}</label>
      <div class="path-row">
        <input type="text" id="open-path" spellcheck="false" value="${escapeAttr(defaultPath)}">
        <button class="btn ghost" id="open-up" title="${t('Eine Ebene nach oben')}">↑</button>
      </div>
      <div class="hint">${t('Klick auf einen Ordner zeigt seinen Inhalt, Enter öffnet den eingetragenen Pfad.')}</div>
    </div>
    <div class="folder-roots" id="open-roots"></div>
    <div class="folder-list" id="open-list"><div class="folder-empty">${t('Lade …')}</div></div>
  `;
  const pathInput = body.querySelector('#open-path');
  const list = body.querySelector('#open-list');
  const roots = body.querySelector('#open-roots');
  let current = defaultPath;
  let parentPath = '';

  const choose = async (value) => {
    const target = String(value || '').trim();
    if (!target) return false;
    try {
      await openPath(target);
      modal.close();
    } catch (error) {
      toast(error.message, 'error');
    }
  };

  const navigate = async (target) => {
    list.classList.add('loading');
    try {
      stripEntries(await get('/api/fs/folders?path=' + encodeURIComponent(target || '')));
    } catch (error) {
      toast(error.message, 'error');
    } finally {
      list.classList.remove('loading');
    }
  };

  function stripEntries(data) {
    current = data.path;
    parentPath = data.parent || '';
    pathInput.value = data.path;
    roots.innerHTML = (data.roots || [])
      .map((root) => `<button type="button" class="folder-root" data-path="${escapeAttr(root)}">${escapeAttr(root)}</button>`)
      .join('');
    const rows = [];
    if (data.parent) {
      rows.push(`<button type="button" class="folder-item" data-path="${escapeAttr(data.parent)}">
        <span class="icon">↰</span><span class="name">${t('Übergeordneter Ordner')}</span></button>`);
    }
    for (const folder of data.folders) {
      rows.push(`<button type="button" class="folder-item" data-path="${escapeAttr(folder.path)}">
        <span class="icon">📁</span><span class="name">${escapeAttr(folder.name)}</span></button>`);
    }
    for (const file of data.files) {
      rows.push(`<button type="button" class="folder-item file" data-path="${escapeAttr(file.path)}">
        <span class="icon">📄</span><span class="name">${escapeAttr(file.name)}</span><span class="meta">${t('Projekt')}</span></button>`);
    }
    list.innerHTML = rows.join('') || `<div class="folder-empty">${t('Keine Unterordner')}</div>`;

    for (const button of list.querySelectorAll('.folder-item')) {
      const target = button.dataset.path;
      if (button.classList.contains('file')) {
        button.addEventListener('click', () => {
          pathInput.value = target;
          for (const other of list.querySelectorAll('.folder-item.file')) other.classList.toggle('active', other === button);
        });
        button.addEventListener('dblclick', () => choose(target));
      } else {
        button.addEventListener('click', () => navigate(target));
      }
    }
    for (const button of roots.querySelectorAll('.folder-root')) {
      button.addEventListener('click', () => navigate(button.dataset.path));
    }
  }

  const modal = openModal({
    title: t('Projekt öffnen'),
    body,
    footer: [
      { label: t('Abbrechen'), action: () => modal.close() },
      { label: t('Öffnen'), variant: 'primary', action: () => { choose(pathInput.value); return false; } }
    ]
  });

  body.querySelector('#open-up').addEventListener('click', () => {
    if (parentPath) navigate(parentPath);
  });
  pathInput.addEventListener('keydown', (event) => {
    if (event.key !== 'Enter') return;
    event.preventDefault();
    choose(pathInput.value);
  });

  navigate(defaultPath);
  setTimeout(() => pathInput.select(), 50);
}

/* Ordner wählen; ohne Electron übernimmt das der Server. */
async function browseFolder(start) {
  return new Promise((resolve) => {
    let picked = '';
    const body = document.createElement('div');
    body.innerHTML = `
      <div class="field">
        <label>${t('Ordner')}</label>
        <div class="path-row">
          <input type="text" id="browse-path" spellcheck="false" value="${escapeAttr(start)}">
          <button class="btn ghost" id="browse-up" title="${t('Eine Ebene nach oben')}">↑</button>
        </div>
      </div>
      <div class="folder-list" id="browse-list"><div class="folder-empty">${t('Lade …')}</div></div>
    `;
    const pathInput = body.querySelector('#browse-path');
    const list = body.querySelector('#browse-list');
    let current = start;
    let parentPath = '';

    const navigate = async (target) => {
      list.classList.add('loading');
      try {
        const data = await get('/api/fs/folders?path=' + encodeURIComponent(target || ''));
        current = data.path;
        parentPath = data.parent || '';
        pathInput.value = data.path;
        const rows = data.folders.map((folder) => `<button type="button" class="folder-item" data-path="${escapeAttr(folder.path)}">
          <span class="icon">📁</span><span class="name">${escapeAttr(folder.name)}</span></button>`);
        list.innerHTML = rows.join('') || `<div class="folder-empty">${t('Keine Unterordner')}</div>`;
        for (const button of list.querySelectorAll('.folder-item')) {
          button.addEventListener('click', () => navigate(button.dataset.path));
        }
      } catch (error) {
        toast(error.message, 'error');
      } finally {
        list.classList.remove('loading');
      }
    };

    openModal({
      title: t('Ordner wählen'),
      body,
      onClose: () => resolve(picked),
      footer: [
        { label: t('Abbrechen'), action: () => { picked = ''; return true; } },
        { label: t('Diesen Ordner wählen'), variant: 'primary', action: () => { picked = current; return true; } }
      ]
    });
    body.querySelector('#browse-up').addEventListener('click', () => {
      if (parentPath) navigate(parentPath);
    });
    navigate(start);
  });
}

async function openPath(target, { silent = false } = {}) {
  if (!target) return;
  if (state.tabs.some((t) => t.dirty)) {
    const proceed = await confirmDialog(t('Ungespeicherte Änderungen'), t('Es gibt ungespeicherte Änderungen. Vor dem Öffnen speichern?'), t('Speichern & öffnen'));
    if (!proceed) return;
    await saveAll();
  }
  const result = await post('/api/project/open', { path: target });
  await adoptProject(result);
  if (!silent) toast(t('Projekt geöffnet: {name}', { name: result.project.name }), 'ok');
}

async function adoptProject(result) {
  if (state.debug.active) await stopDebug();
  closeAllTabs();
  state.project = result.project;
  state.projectDir = result.dir;
  state.tree = result.tree || [];
  state.diagnostics = [];
  cancelLiveCheck();
  state.checked = false;
  state.dirtySinceBuild = true;
  if (result.config) state.health.config = result.config;
  if (result.gbx) state.gbx = result.gbx;
  state.projectSymbols = { functions: [], types: [], variables: [], constants: [] };
  state.analysisCache.clear();
  showWelcome(false);
  updateStatus();
  renderTree();
  renderProblems();
  clearAllMarkers();
  setBuildStatus(t('Bereit'));

  const openFiles = result.project.openFiles?.length ? result.project.openFiles : [result.project.sources?.[0]].filter(Boolean);
  for (const file of openFiles.slice(0, 8)) {
    try {
      await openFile(file, { focus: false });
    } catch {
      /* Datei ggf. nicht lesbar */
    }
  }
  if (state.tabs.length) activateTab(state.tabs[0].path);
  else showWelcome(true);

  loadBreakpoints();
  paintBreakpoints();
  renderBreakpoints();
  renderDebugScope();
  updateDebugUi();
  updateButtons();
  indexProjectSymbols();
  renderRecent();
}

function closeAllTabs() {
  for (const tab of state.tabs) {
    if (!tab.model) tab.view?.dispose?.();
    else tab.model.dispose();
  }
  state.tabs = [];
  state.activePath = '';
  if (editor) editor.setModel(null);
  setPreviewView(null);
  renderTabs();
}

async function refreshTree() {
  if (!state.projectDir) return;
  try {
    const result = await get(`/api/tree?dir=${encodeURIComponent(state.projectDir)}`);
    state.tree = result.tree || [];
    renderTree();
  } catch (error) {
    toast(error.message, 'error');
  }
}

/* ------------------------------------------------------------------ */
/* Dateibaum                                                           */
/* ------------------------------------------------------------------ */

const TREE_PREF_KEY = 'gbxstudio.tree.v1';
const treeCollapsed = new Set();

// Gruppen-Zustand merken, damit zugeklappte Bereiche beim nächsten Start
// wieder zu sind.
function readTreePrefs() {
  try {
    const saved = JSON.parse(localStorage.getItem(TREE_PREF_KEY) || '{}');
    treeCollapsed.clear();
    if (Array.isArray(saved.collapsed)) saved.collapsed.forEach((id) => treeCollapsed.add(id));
  } catch {
    /* Speicher nicht verfügbar */
  }
}

function persistTreePrefs() {
  try {
    localStorage.setItem(TREE_PREF_KEY, JSON.stringify({ collapsed: [...treeCollapsed] }));
  } catch {
    /* Speicher nicht verfügbar */
  }
}

/* ------------------------------------------------------------------ */
/* Dateien und Ordner per Drag & Drop verschieben                     */
/* ------------------------------------------------------------------ */

const TREE_MOVE_TYPE = 'application/x-gbxstudio-move';
// Der Pfad der gerade gezogenen Zeile; in dragover liefert der DataTransfer
// die Nutzlast nicht, deshalb merken wir sie uns hier.
let treeDragPath = '';

function treeDragStart(event, relPath) {
  const rel = String(relPath || '').replace(/\\/g, '/');
  if (!rel || !event.dataTransfer) return;
  treeDragPath = rel;
  event.dataTransfer.effectAllowed = 'copyMove';
  event.dataTransfer.setData('application/x-gbxstudio-path', rel);
  event.dataTransfer.setData(TREE_MOVE_TYPE, rel);
  event.dataTransfer.setData('text/plain', rel);
  event.currentTarget?.classList.add('dragging');
}

function clearTreeDrag() {
  treeDragPath = '';
  for (const row of document.querySelectorAll('.tree-row.drop-target')) row.classList.remove('drop-target');
  $('file-tree')?.classList.remove('drop-root');
}

/** Darf die gezogene Zeile in diesen Ordner ('' = Projektordner)? */
function canDropIntoFolder(folder) {
  if (!treeDragPath) return false;
  if (treeDragPath === folder) return false;
  // Nicht in sich selbst oder in einen eigenen Unterordner.
  if (folder && folder.startsWith(`${treeDragPath}/`)) return false;
  const from = treeDragPath.includes('/') ? treeDragPath.slice(0, treeDragPath.lastIndexOf('/')) : '';
  return from !== (folder || '');
}

function attachFolderDrop(row, folder) {
  row.addEventListener('dragover', (event) => {
    if (!canDropIntoFolder(folder)) return;
    event.preventDefault();
    event.stopPropagation();
    if (event.dataTransfer) event.dataTransfer.dropEffect = 'move';
    row.classList.add('drop-target');
  });
  row.addEventListener('dragleave', (event) => {
    if (event.relatedTarget && row.contains(event.relatedTarget)) return;
    row.classList.remove('drop-target');
  });
  row.addEventListener('drop', (event) => {
    row.classList.remove('drop-target');
    if (!canDropIntoFolder(folder)) return;
    event.preventDefault();
    event.stopPropagation();
    const from = treeDragPath;
    clearTreeDrag();
    moveTreeEntry(from, folder).catch((error) => toast(error.message, 'error'));
  });
}

function renderTree() {
  const host = $('file-tree');
  host.innerHTML = '';
  if (!state.project) {
    host.innerHTML = `<div class="empty-hint">${t('Noch kein Projekt geöffnet.')}</div>`;
    return;
  }
  const tree = buildFileTree(state.tree);
  if (!tree.dirs.length && !tree.files.length) {
    host.innerHTML = `<div class="empty-hint">${t('Keine Dateien im Projekt.')}</div>`;
    return;
  }
  const fragment = document.createDocumentFragment();
  appendTreeLevel(fragment, tree, 0);
  host.append(fragment);
}

/** Ordner und Dateien in der Reihenfolge des Baums – Ordner zuerst. */
function appendTreeLevel(host, node, depth) {
  for (const dir of node.dirs) {
    host.append(treeFolderRow(dir, depth));
    if (!treeCollapsed.has(dir.path)) appendTreeLevel(host, dir, depth + 1);
  }
  for (const file of node.files) host.append(treeFileRow(file, file.name, depth));
}

function treeFolderRow(dir, depth) {
  const row = document.createElement('div');
  row.className = 'tree-row folder';
  row.style.paddingLeft = `${8 + depth * 14}px`;
  row.dataset.path = dir.path;
  row.title = dir.path;
  const collapsed = treeCollapsed.has(dir.path);
  const chev = document.createElement('span');
  chev.className = 'chev';
  chev.textContent = collapsed ? '▸' : '▾';
  const icon = document.createElement('span');
  icon.className = 'fi folder';
  icon.textContent = '📁';
  const label = document.createElement('span');
  label.className = 'label';
  label.textContent = dir.name;
  row.append(chev, icon, label);
  const total = countFiles(dir);
  if (total) {
    const count = document.createElement('span');
    count.className = 'count';
    count.textContent = String(total);
    row.append(count);
  }
  row.addEventListener('click', () => {
    if (treeCollapsed.has(dir.path)) treeCollapsed.delete(dir.path);
    else treeCollapsed.add(dir.path);
    persistTreePrefs();
    renderTree();
  });
  row.addEventListener('contextmenu', (event) => {
    event.preventDefault();
    showTreeMenu(event, { ...dir, type: 'dir' });
  });
  // Ordner lassen sich wie Dateien ziehen – als Ziel für andere Einträge.
  row.draggable = true;
  row.addEventListener('dragstart', (event) => treeDragStart(event, dir.path));
  row.addEventListener('dragend', clearTreeDrag);
  attachFolderDrop(row, dir.path);
  return row;
}

function treeFileRow(item, label, depth = 0) {
  const row = document.createElement('div');
  row.className = 'tree-row' + (item.path === state.activePath ? ' active' : '');
  row.dataset.path = item.path;
  row.title = item.path;
  row.style.paddingLeft = `${25 + depth * 14}px`;
  row.draggable = true;
  const ext = (item.ext || item.name.split('.').pop() || '').replace('.', '').toLowerCase();
  const icon = document.createElement('span');
  icon.className = 'fi ' + ext;
  icon.textContent = ext === 'gbas'
    ? '◆'
    : ['png', 'jpg', 'jpeg', 'bmp', 'gif'].includes(ext)
      ? '▩'
      : ['wav', 'ogg', 'mp3'].includes(ext)
        ? '♪'
        : '▪';
  const text = document.createElement('span');
  text.className = 'label';
  text.textContent = label;
  row.append(icon, text);
  if (state.tabs.find((tab) => tab.path === item.path)?.dirty) {
    const dot = document.createElement('span');
    dot.className = 'dirty-dot';
    row.append(dot);
  }
  row.addEventListener('click', () => {
    openFile(item.path).catch((error) => toast(error.message, 'error'));
  });
  row.addEventListener('contextmenu', (event) => {
    event.preventDefault();
    showTreeMenu(event, item);
  });
  // Dateien lassen sich in einen Ordner verschieben oder in den Editor ziehen:
  // dort landet der relative Pfad an der Cursorposition, z. B. „Media/image.png“.
  row.addEventListener('dragstart', (event) => treeDragStart(event, item.path));
  row.addEventListener('dragend', clearTreeDrag);
  return row;
}

function isSourceFile(file) {
  return /\.gbx$/i.test(String(file || ''));
}

function showTreeMenu(event, item) {
  const entries = [];
  // Rechtsklick auf die freie Fläche: alles im Projektordner anlegen.
  if (!item) {
    entries.push(
      { label: t('Code erstellen (.gbx)'), action: () => newSourceDialog('') },
      { label: t('Datei erstellen …'), action: () => newFileDialog('', 'neu.txt') },
      { label: t('Ordner erstellen'), action: () => newFolderDialog('') }
    );
    showContextMenu(event.clientX, event.clientY, entries);
    return;
  }
  if (item.type === 'dir') {
    entries.push(
      { label: t('Code erstellen (.gbx)'), action: () => newSourceDialog(item.path) },
      { label: t('Datei erstellen …'), action: () => newFileDialog(item.path, 'neu.txt') },
      { label: t('Ordner erstellen'), action: () => newFolderDialog(item.path) },
      { label: t('Ordner löschen'), action: () => deleteFolderEntry(item.path) }
    );
  } else if (isSourceFile(item.path)) {
    entries.push({ label: t('Code löschen (.gbx)'), action: () => deleteSourceFile(item.path) });
  } else {
    entries.push({ label: t('Datei löschen'), action: () => deleteFileEntry(item.path) });
  }
  entries.push(
    { label: t('Im Explorer öffnen'), action: () => openInExplorer(absPath(item.path)) },
    { label: t('Pfad kopieren'), action: () => copyProjectPath(item.path) }
  );
  showContextMenu(event.clientX, event.clientY, entries);
}

/** Neuen Pfad einer verschobenen Datei – oder null, wenn sie nicht betroffen ist. */
function remapPath(file, from, to) {
  const rel = String(file || '').replace(/\\/g, '/');
  if (rel === from) return to;
  if (rel.startsWith(`${from}/`)) return `${to}${rel.slice(from.length)}`;
  return null;
}

/**
 * Verschiebt eine Datei oder einen Ordner in einen anderen Ordner und zieht
 * Projektdatei (.gbxproj), Reiter und Analyse mit.
 */
async function moveTreeEntry(from, folder) {
  if (!state.projectDir || !from) return;
  const name = from.split('/').pop();
  const to = folder ? `${folder}/${name}` : name;
  if (to === from) return;
  try {
    const result = await post('/api/move', { dir: state.projectDir, from, to });
    state.tree = result.tree || state.tree;
    remapProjectPaths(from, to);
    // Erst nach dem Verschieben speichern: die .gbxproj nennt den neuen Pfad.
    await persistProjectNow();
    expandTreePath(to);
    renderTree();
    renderTabs();
    renderOutline();
    scheduleIndex();
    toast(t('Verschoben: {from} → {to}', { from, to }), 'ok');
  } catch (error) {
    toast(error.message, 'error');
    await refreshTree();
  }
}

/** Alle Pfade im Projekt auf einen verschobenen Eintrag umstellen. */
function remapProjectPaths(from, to) {
  if (!state.project) return;
  state.project.sources = (state.project.sources || []).map((source) => remapPath(source, from, to) || source);
  for (const collapsed of [...treeCollapsed]) {
    const next = remapPath(collapsed, from, to);
    if (!next) continue;
    treeCollapsed.delete(collapsed);
    treeCollapsed.add(next);
  }
  persistTreePrefs();
  remapOpenTabs(from, to);
}

/**
 * Offene Reiter folgen der Datei. Textreiter bekommen ein Modell unter dem
 * neuen Pfad (Inhalt und Änderungsstand bleiben, die Undo-Historie nicht),
 * Bild- und Klangvorschauen werden am neuen Pfad neu geöffnet.
 */
function remapOpenTabs(from, to) {
  const reopen = [];
  for (const tab of [...state.tabs]) {
    const next = remapPath(tab.path, from, to);
    if (!next) continue;
    const active = state.activePath === tab.path;
    if (!tab.model) {
      if (activePreviewView === tab.view) setPreviewView(null);
      tab.view?.dispose?.();
      state.tabs.splice(state.tabs.indexOf(tab), 1);
      if (active) {
        state.activePath = '';
        editor.setModel(null);
      }
      reopen.push(next);
      continue;
    }
    const oldModel = tab.model;
    state.analysisCache.delete(oldModel.uri.toString());
    const uri = monaco.Uri.file(absPath(next));
    const model = monaco.editor.getModel(uri)
      || monaco.editor.createModel(oldModel.getValue(), tab.language || languageFor(next), uri);
    tab.path = next;
    bindTabModel(tab, model);
    if (active) {
      state.activePath = next;
      editor.setModel(model);
      updateInlineHighlights(model);
    }
    // Das alte Modell erst danach freigeben – der Editor zeigt jetzt das neue.
    oldModel.dispose();
  }
  state.project.openFiles = (state.project.openFiles || []).map((file) => remapPath(file, from, to) || file);
  for (const file of reopen) openFile(file).catch((error) => toast(error.message, 'error'));
}

/** Neue Quelle in das Projekt aufnehmen. */
async function addSourceToProject(relPath) {
  if (!state.project) return;
  state.project.sources = [...new Set([...(state.project.sources || []), relPath])];
  await persistProjectNow();
  scheduleIndex();
}

async function deleteSourceFile(relPath) {
  if (!state.project) return;
  const proceed = await confirmDialog(
    t('Code löschen (.gbx)'),
    t('„{path}“ wird aus dem Projekt und von der Festplatte entfernt.', { path: relPath }),
    t('Löschen'),
    true
  );
  if (!proceed) return;
  try {
    const result = await post('/api/file/delete', { dir: state.projectDir, file: relPath });
    // Erst löschen, dann das Projekt bereinigen – schlägt das Löschen fehl, bleibt
    // das Projekt unverändert (und der Tab offen).
    state.project.sources = (state.project.sources || []).filter((source) => source !== relPath);
    await persistProjectNow();
    state.tree = result.tree || state.tree;
    removeTab(relPath);
    renderTree();
    scheduleIndex();
    toast(t('Gelöscht: {path}', { path: relPath }), 'ok');
  } catch (error) {
    toast(error.message, 'error');
  }
}

async function deleteFileEntry(relPath) {
  const proceed = await confirmDialog(
    t('Datei löschen'),
    t('„{path}“ wird von der Festplatte gelöscht.', { path: relPath }),
    t('Löschen'),
    true
  );
  if (!proceed) return;
  try {
    const result = await post('/api/file/delete', { dir: state.projectDir, file: relPath });
    state.tree = result.tree || state.tree;
    removeTab(relPath);
    renderTree();
    toast(t('Gelöscht: {path}', { path: relPath }), 'ok');
  } catch (error) {
    toast(error.message, 'error');
  }
}

async function deleteFolderEntry(relPath) {
  if (!state.project) return;
  const proceed = await confirmDialog(
    t('Ordner löschen'),
    t('„{path}“ wird mit allen enthaltenen Dateien gelöscht.', { path: relPath }),
    t('Löschen'),
    true
  );
  if (!proceed) return;
  const inside = (file) => file === relPath || String(file || '').startsWith(`${relPath}/`);
  try {
    const result = await post('/api/folder/delete', { dir: state.projectDir, folder: relPath });
    state.project.sources = (state.project.sources || []).filter((source) => !inside(source));
    await persistProjectNow();
    state.tree = result.tree || state.tree;
    for (const collapsed of [...treeCollapsed]) if (inside(collapsed)) treeCollapsed.delete(collapsed);
    persistTreePrefs();
    for (const tab of [...state.tabs]) if (inside(tab.path)) removeTab(tab.path);
    renderTree();
    scheduleIndex();
    toast(t('Gelöscht: {path}', { path: relPath }), 'ok');
  } catch (error) {
    toast(error.message, 'error');
  }
}

/** Ordner samt allen Elternordnern aufklappen, damit ein neuer Eintrag sichtbar ist. */
function expandTreePath(relativePath) {
  const parts = String(relativePath || '').replace(/\\/g, '/').split('/').filter(Boolean);
  parts.pop();
  let current = '';
  for (const part of parts) {
    current = current ? `${current}/${part}` : part;
    treeCollapsed.delete(current);
  }
  persistTreePrefs();
}

async function copyProjectPath(rel) {
  const text = rel.replace(/\\/g, '/');
  const copied = await copyToClipboard(text);
  if (copied) toast(t('Pfad kopiert: {path}', { path: text }), 'ok');
  else toast(t('Der Pfad konnte nicht kopiert werden.'), 'error');
}

async function copyToClipboard(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    /* Fallback für Umgebungen ohne Clipboard-API */
  }
  try {
    const area = document.createElement('textarea');
    area.value = text;
    area.setAttribute('readonly', '');
    area.style.cssText = 'position:fixed;top:-1000px;opacity:0';
    document.body.append(area);
    area.select();
    const ok = document.execCommand('copy');
    area.remove();
    return ok;
  } catch {
    return false;
  }
}

let contextMenuEl = null;

function closeContextMenu() {
  if (!contextMenuEl) return;
  contextMenuEl.remove();
  contextMenuEl = null;
}

function showContextMenu(x, y, items) {
  closeContextMenu();
  const menu = document.createElement('div');
  menu.className = 'context-menu';
  menu.setAttribute('role', 'menu');
  for (const entry of items) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'context-item';
    button.setAttribute('role', 'menuitem');
    button.textContent = entry.label;
    button.addEventListener('click', () => {
      closeContextMenu();
      Promise.resolve(entry.action()).catch((error) => toast(error.message, 'error'));
    });
    menu.append(button);
  }
  document.body.append(menu);
  const rect = menu.getBoundingClientRect();
  menu.style.left = `${Math.min(x, Math.max(4, window.innerWidth - rect.width - 6))}px`;
  menu.style.top = `${Math.min(y, Math.max(4, window.innerHeight - rect.height - 6))}px`;
  contextMenuEl = menu;
}

// Ordner im Explorer öffnen – in der Desktop-Variante über Electron, in der
// WebStart-Variante über den Server.
async function openInExplorer(target) {
  if (!target) return;
  try {
    if (desktop?.openInExplorer) {
      const ok = await desktop.openInExplorer(target);
      if (!ok) throw new Error(t('Der Explorer konnte nicht geöffnet werden.'));
    } else {
      await post('/api/reveal', { path: target });
    }
    toast(t('Im Explorer geöffnet.'), 'ok');
  } catch (error) {
    toast(error.message, 'error');
  }
}

// Dateipfad an der Cursorposition einfügen (Drag & Drop aus dem Dateibaum).
function insertPathAtCursor(rel) {
  if (!editor) return;
  const model = editor.getModel();
  const position = editor.getPosition();
  if (!model || !position) return;
  const text = rel.replace(/\\/g, '/');
  editor.pushUndoStop();
  editor.executeEdits('tree-path-drop', [
    {
      range: new monaco.Range(position.lineNumber, position.column, position.lineNumber, position.column),
      text,
      forceMoveMarkers: true
    }
  ]);
  editor.pushUndoStop();
  editor.setPosition({ lineNumber: position.lineNumber, column: position.column + text.length });
  editor.revealPositionInCenterIfOutsideViewport();
  editor.focus();
}

/* ------------------------------------------------------------------ */
/* Tabs / Editor                                                       */
/* ------------------------------------------------------------------ */

function languageFor(file) {
  return /\.gbx$/i.test(file) ? 'gbx' : 'plaintext';
}

function absPath(rel) {
  return `${state.projectDir.replace(/\\/g, '/').replace(/\/$/, '')}/${rel}`;
}

function activeTab() {
  return state.tabs.find((t) => t.path === state.activePath) || null;
}

async function openFile(rel, { focus = true, line = 0 } = {}) {
  if (!state.projectDir) return;
  const existing = state.tabs.find((t) => t.path === rel);
  if (existing) {
    activateTab(rel);
    paintBreakpoints();
    paintDebugLine();
    if (line) revealLine(line);
    return;
  }
  const ext = rel.split('.').pop().toLowerCase();
  // Bilder und Klänge öffnen nicht im Editor, sondern in der Vorschau.
  const kind = mediaKind(ext);
  if (kind) {
    const view = createMediaView({
      kind,
      rel,
      dir: state.projectDir,
      onReveal: () => openInExplorer(absPath(rel))
    });
    state.tabs.push({ path: rel, kind, model: null, dirty: false, view });
    activateTab(rel);
    schedulePersist();
    return;
  }
  const binary = ['exe', 'dll', 'pdf', 'zip', 'ddd', 'dda', 'ddw'].includes(ext);
  if (binary) {
    toast(t('Diese Datei kann nicht als Text geöffnet werden.'), 'warn');
    return;
  }
  const result = await get(`/api/file?dir=${encodeURIComponent(state.projectDir)}&file=${encodeURIComponent(rel)}`);
  const uri = monaco.Uri.file(absPath(rel));
  let model = monaco.editor.getModel(uri);
  if (!model) {
    model = monaco.editor.createModel(result.content, languageFor(rel), uri);
  }
  const tab = {
    path: rel,
    model,
    dirty: false,
    kind: 'text',
    language: languageFor(rel)
  };
  bindTabModel(tab, model);
  state.tabs.push(tab);
  activateTab(rel);
  paintBreakpoints();
  paintDebugLine();
  if (focus) editor.focus();
  if (line) revealLine(line);
  schedulePersist();
}

/**
 * Verbindet einen Tab mit seinem Monaco-Modell. Wird auch gebraucht, wenn
 * eine verschobene Datei unter neuem Pfad ein neues Modell bekommt.
 */
function bindTabModel(tab, model) {
  tab.model = model;
  const changeSub = model.onDidChangeContent(() => {
    tab.dirty = true;
    state.dirtySinceBuild = true;
    state.analysisCache.delete(model.uri.toString());
    renderTabs();
    renderTree();
    updateInlineHighlights(model);
    scheduleOutline();
    schedulePersist();
    scheduleIndex();
  });
  const disposeSub = model.onWillDispose(() => {
    changeSub.dispose();
    disposeSub.dispose();
  });
}

let activePreviewView = null;

/** Zeigt eine Bild-/Audio-Vorschau im Editorbereich oder blendet sie aus. */
function setPreviewView(view) {
  const pane = $('preview-pane');
  const wrap = $('editor-wrap');
  if (!pane || !wrap) return;
  if (activePreviewView && activePreviewView !== view) activePreviewView.suspend?.();
  activePreviewView = view || null;
  if (view) {
    pane.hidden = false;
    if (pane.firstElementChild !== view.el) pane.replaceChildren(view.el);
    wrap.classList.add('previewing');
    view.activate?.();
  } else {
    pane.hidden = true;
    pane.replaceChildren();
    wrap.classList.remove('previewing');
  }
}

function activateTab(path) {
  const tab = state.tabs.find((t) => t.path === path);
  if (!tab) return;
  state.activePath = path;
  // Beim Öffnen einer Datei muss die Startseite weichen – auch wenn vorher
  // alle Tabs geschlossen waren.
  showWelcome(false);
  if (!tab.model) {
    editor.setModel(null);
    hideSignatureBar();
    setPreviewView(tab.view);
    $('status-cursor').textContent = t('Vorschau');
    renderTabs();
    renderTree();
    renderOutline();
    updateButtons();
    return;
  }
  setPreviewView(null);
  editor.setModel(tab.model);
  editor.focus();
  // Nach dem Modellwechsel steht die Cursorposition erst jetzt fest.
  refreshLineCheck();
  updateCursorLabel();
  updateInlineHighlights(tab.model);
  renderTabs();
  renderTree();
  applyMarkers();
  renderOutline();
  $('btn-save').disabled = false;
  updateButtons();
}

function renderTabs() {
  const host = $('tabbar');
  host.innerHTML = '';
  for (const tab of state.tabs) {
    const el = document.createElement('div');
    el.className = 'tab' + (tab.path === state.activePath ? ' active' : '');
    el.title = tab.path;
    const dot = document.createElement('span');
    if (tab.dirty) dot.className = 'dot';
    const name = document.createElement('span');
    name.className = 'name';
    const prefix = tab.kind === 'audio' ? '♪ ' : tab.kind === 'image' ? '▩ ' : '';
    name.textContent = prefix + tab.path.split('/').pop();
    const close = document.createElement('button');
    close.className = 'close';
    close.textContent = '×';
    close.title = t('Schließen');
    close.addEventListener('click', (event) => {
      event.stopPropagation();
      closeTab(tab.path);
    });
    el.append(dot, name, close);
    el.addEventListener('click', () => activateTab(tab.path));
    makeTabDraggable(el, tab);
    host.append(el);
  }
}

// Tabs lassen sich per Drag & Drop umsortieren; die Reihenfolge wird im
// Projekt gespeichert und beim nächsten Öffnen wiederhergestellt.
function makeTabDraggable(el, tab) {
  el.draggable = true;
  el.addEventListener('dragstart', (event) => {
    state.dragTab = tab.path;
    el.classList.add('dragging');
    if (event.dataTransfer) {
      event.dataTransfer.effectAllowed = 'move';
      // Eigener Typ, damit ein versehentliches Ablegen im Editor nicht den Pfad einfügt.
      event.dataTransfer.setData('application/x-gbxstudio-tab', tab.path);
    }
  });
  el.addEventListener('dragend', () => {
    state.dragTab = '';
    state.dropTarget = null;
    clearTabDropMarkers();
    renderTabs();
  });
  el.addEventListener('dragover', (event) => {
    if (!state.dragTab || state.dragTab === tab.path) return;
    event.preventDefault();
    autoScrollTabbar(event.clientX);
    if (event.dataTransfer) event.dataTransfer.dropEffect = 'move';
    const rect = el.getBoundingClientRect();
    const after = event.clientX > rect.left + rect.width / 2;
    clearTabDropMarkers();
    el.classList.add(after ? 'drop-after' : 'drop-before');
    state.dropTarget = { path: tab.path, after };
  });
  el.addEventListener('dragleave', () => el.classList.remove('drop-before', 'drop-after'));
  el.addEventListener('drop', (event) => {
    event.preventDefault();
    const target = state.dropTarget;
    state.dropTarget = null;
    if (!state.dragTab || !target) return;
    moveTab(state.dragTab, target.path, target.after);
  });
}

// Bei vielen Tabs schiebt Ziehen am Rand die Leiste weiter, damit auch
// verdeckte Tabs erreichbar sind.
function autoScrollTabbar(clientX) {
  const bar = $('tabbar');
  if (bar.scrollWidth <= bar.clientWidth) return;
  const rect = bar.getBoundingClientRect();
  const edge = 48;
  if (clientX < rect.left + edge) bar.scrollLeft -= 14;
  else if (clientX > rect.right - edge) bar.scrollLeft += 14;
}

function clearTabDropMarkers() {
  document.querySelectorAll('#tabbar .tab.drop-before, #tabbar .tab.drop-after').forEach((el) => {
    el.classList.remove('drop-before', 'drop-after');
  });
  $('tabbar').classList.remove('drop-end');
}

function moveTab(path, targetPath, after) {
  const from = state.tabs.findIndex((t) => t.path === path);
  if (from < 0) return;
  const [tab] = state.tabs.splice(from, 1);
  let index = targetPath ? state.tabs.findIndex((t) => t.path === targetPath) : state.tabs.length;
  if (index < 0) index = state.tabs.length;
  else if (targetPath && after) index += 1;
  state.tabs.splice(index, 0, tab);
  state.dragTab = '';
  clearTabDropMarkers();
  renderTabs();
  schedulePersist();
}

// Schließt den Tab ohne Rückfrage – für gelöschte Dateien, deren Inhalt
// ohnehin verschwindet.
function removeTab(path) {
  const tab = state.tabs.find((t) => t.path === path);
  if (!tab) return;
  const index = state.tabs.indexOf(tab);
  state.tabs.splice(index, 1);
  if (!tab.model) {
    if (activePreviewView === tab.view) setPreviewView(null);
    tab.view?.dispose?.();
  } else {
    tab.model.dispose();
  }
  if (state.activePath === path) {
    const next = state.tabs[index] || state.tabs[index - 1];
    if (next) {
      activateTab(next.path);
    } else {
      state.activePath = '';
      editor.setModel(null);
      setPreviewView(null);
      showWelcome(true);
    }
  }
  renderTabs();
  renderTree();
  renderOutline();
  schedulePersist();
  updateButtons();
}

async function closeTab(path) {
  const tab = state.tabs.find((t) => t.path === path);
  if (!tab) return;
  if (tab.dirty) {
    const choice = await saveChangesDialog(
      t('Ungespeicherte Änderungen'),
      t('{path} wurde geändert. Speichern?', { path }),
      t('Speichern & schließen')
    );
    if (choice === null) return;
    if (choice === 'save' && !(await saveTab(tab))) return;
  }
  removeTab(path);
}

function revealLine(line) {
  if (!line) return;
  const model = editor.getModel();
  if (!model) return;
  const target = Math.min(Math.max(1, line), model.getLineCount());
  editor.revealLineInCenter(target);
  editor.setPosition({ lineNumber: target, column: 1 });
  editor.focus();
}

async function saveTab(tab) {
  if (!tab || !tab.model) return false;
  try {
    await post('/api/file', { dir: state.projectDir, file: tab.path, content: tab.model.getValue() });
    tab.dirty = false;
    renderTabs();
    renderTree();
    scheduleIndex();
    return true;
  } catch (error) {
    toast(error.message, 'error');
    return false;
  }
}

async function saveCurrent() {
  const tab = activeTab();
  if (!tab || !tab.model) return false;
  const ok = await saveTab(tab);
  if (ok) toast(t('Gespeichert.'), 'ok');
  return ok;
}

async function saveAll({ silent = true } = {}) {
  const dirty = state.tabs.filter((t) => t.dirty);
  let ok = true;
  for (const tab of dirty) {
    const saved = await saveTab(tab);
    if (!saved) ok = false;
  }
  if (ok && dirty.length && !silent) toast(t('{n} Datei(en) gespeichert.', { n: dirty.length }), 'ok');
  await persistProjectNow();
  return ok;
}

function schedulePersist() {
  clearTimeout(persistTimer);
  persistTimer = setTimeout(() => persistProjectNow(), 800);
}

function scheduleIndex() {
  clearTimeout(indexTimer);
  indexTimer = setTimeout(() => {
    indexProjectSymbols().catch(() => {});
  }, 1200);
}

async function persistProjectNow() {
  if (!state.project || !state.projectDir) return;
  state.project.openFiles = state.tabs.map((t) => t.path);
  try {
    await post('/api/project/save', { projectDir: state.projectDir, project: state.project });
  } catch {
    /* stilles Speichern der Metadaten */
  }
}

/* ------------------------------------------------------------------ */
/* Neue Dateien / Ordner                                               */
/* ------------------------------------------------------------------ */

/** „Code erstellen“ – neue .gbx, die automatisch im Projekt landet. */
function newSourceDialog(folder = '') {
  newFileDialog(folder, 'neu.gbx', { source: true });
}

/**
 * Legt eine Datei an. Als Quelle (.gbx) wird sie dem Projekt hinzugefügt,
 * sonst darf die Endung frei gewählt werden.
 */
function newFileDialog(folder = '', defaultName = 'neu.gbx', options = {}) {
  if (!state.project) return toast(t('Bitte zuerst ein Projekt öffnen.'), 'warn');
  const asSource = !!options.source;
  const base = String(folder || '').replace(/\\/g, '/').replace(/\/+$/, '');
  const initial = base ? `${base}/${defaultName}` : defaultName;
  const body = document.createElement('div');
  body.innerHTML = `
    <div class="field">
      <label>${asSource ? t('Dateiname (.gbx)') : t('Dateiname mit Endung')}</label>
      <input type="text" id="new-file-name" value="${escapeAttr(initial)}" spellcheck="false">
      <div class="hint">${asSource
        ? t('Die Datei wird angelegt und dem Projekt hinzugefügt.')
        : t('Endung frei wählbar, z. B. „notes.txt“ oder „lib/math.gbx“. Eine .gbx-Datei wird dem Projekt hinzugefügt.')}</div>
    </div>
  `;
  const modal = openModal({
    title: asSource ? t('Code erstellen (.gbx)') : t('Datei erstellen'),
    body,
    footer: [
      { label: t('Abbrechen'), action: () => modal.close() },
      {
        label: t('Anlegen'),
        variant: 'primary',
        action: async () => {
          let name = body.querySelector('#new-file-name').value.trim().replace(/\\/g, '/');
          if (!name) return false;
          if (asSource) {
            // Feste Endung: eine andere Endung wird ersetzt.
            name = `${name.replace(/\.[^./]+$/, '')}.gbx`;
          } else if (/\.gbxproj$/i.test(name)) {
            toast(t('Die Projektdatei .gbxproj wird von der IDE verwaltet.'), 'warn');
            return false;
          }
          try {
            const source = isSourceFile(name);
            const content = source ? `' ${name.split('/').pop()}\n\n` : '';
            const result = await post('/api/file/create', { dir: state.projectDir, file: name, content });
            if (source) await addSourceToProject(name);
            state.tree = result.tree;
            expandTreePath(name);
            renderTree();
            modal.close();
            await openFile(name);
          } catch (error) {
            toast(error.message, 'error');
          }
        }
      }
    ]
  });
  setTimeout(() => body.querySelector('#new-file-name')?.select(), 50);
}

function newFolderDialog(folder = '') {
  if (!state.project) return toast(t('Bitte zuerst ein Projekt öffnen.'), 'warn');
  const base = String(folder || '').replace(/\\/g, '/').replace(/\/+$/, '');
  const initial = base ? `${base}/Neu` : 'Neu';
  const body = document.createElement('div');
  body.innerHTML = `
    <div class="field">
      <label>${t('Ordnername')}</label>
      <input type="text" id="new-folder-name" value="${escapeAttr(initial)}" spellcheck="false">
    </div>
  `;
  const modal = openModal({
    title: t('Neuer Ordner'),
    body,
    footer: [
      { label: t('Abbrechen'), action: () => modal.close() },
      {
        label: t('Anlegen'),
        variant: 'primary',
        action: async () => {
          const name = body.querySelector('#new-folder-name').value.trim().replace(/\\/g, '/');
          if (!name) return false;
          try {
            const result = await post('/api/folder/create', { dir: state.projectDir, folder: name });
            state.tree = result.tree;
            expandTreePath(`${name}/`);
            renderTree();
            modal.close();
          } catch (error) {
            toast(error.message, 'error');
          }
        }
      }
    ]
  });
}

/* ------------------------------------------------------------------ */
/* Neues Projekt                                                       */
/* ------------------------------------------------------------------ */

async function newProjectDialog() {
  const parentDefault = state.health?.defaultProjectParent || '';
  let userTemplates = [];
  try {
    const stored = await get('/api/templates');
    userTemplates = stored.templates || [];
    state.templates = userTemplates;
  } catch {}
  const body = document.createElement('div');
  body.innerHTML = `
    <div class="field">
      <label>${t('Projektname')}</label>
      <input type="text" id="np-name" value="${t('MeinSpiel')}" spellcheck="false">
    </div>
    <div class="field">
      <label>${t('Speicherort')}</label>
      <div class="path-row">
        <input type="text" id="np-path" value="${escapeAttr(parentDefault)}" spellcheck="false">
        <button class="btn ghost" id="np-browse">${t('Wählen …')}</button>
      </div>
      <div class="hint">${t('Es wird ein Unterordner mit dem Projektnamen angelegt.')}</div>
    </div>
    <div class="field">
      <label>${t('Vorlage')}</label>
      <div class="template-grid" id="np-templates">
        <button class="template active" data-template="hello">
          <span class="t">Hello World</span>
          <span class="s">${t('Fenster, Text, Formen und Maussteuerung')}</span>
        </button>
        <button class="template" data-template="empty">
          <span class="t">${t('Leeres Projekt')}</span>
          <span class="s">${t('Minimale Hauptschleife')}</span>
        </button>
        <button class="template" data-template="shapes">
          <span class="t">${t('Formen')}</span>
          <span class="s">${t('Kreise, Linien und Farben animieren')}</span>
        </button>
        <button class="template" data-template="sprites">
          <span class="t">${t('Sprites')}</span>
          <span class="s">${t('Bild laden, bewegen und drehen')}</span>
        </button>
        ${userTemplates
          .map(
            (tpl) => `
        <button class="template" data-template="user:${escapeAttr(tpl.id)}" title="${escapeAttr(tpl.description || '')}">
          <span class="t">${escapeAttr(tpl.name)}</span>
          <span class="s">${escapeAttr(tpl.description || t('{n} Datei(en)', { n: tpl.files?.length || 0 }))}</span>
        </button>`
          )
          .join('')}
      </div>
    </div>
    <div class="error-box" id="np-error"></div>
  `;
  let template = 'hello';
  body.querySelectorAll('.template').forEach((button) => {
    button.addEventListener('click', () => {
      template = button.dataset.template;
      body.querySelectorAll('.template').forEach((b) => b.classList.toggle('active', b === button));
    });
  });
  body.querySelector('#np-browse').addEventListener('click', async () => {
    if (desktop) {
      const picked = await desktop.chooseLocation();
      if (picked) body.querySelector('#np-path').value = picked;
    } else {
      // Ohne Electron gibt es keinen Systemdialog – der Server listet die Ordner.
      const picked = await browseFolder(body.querySelector('#np-path').value);
      if (picked) body.querySelector('#np-path').value = picked;
    }
  });
  const modal = openModal({
    title: t('Neues GBX-Projekt'),
    subtitle: t('Editor, Referenz und Interpreter in einem'),
    body,
    footer: [
      { label: t('Abbrechen'), action: () => modal.close() },
      {
        label: t('Projekt anlegen'),
        variant: 'primary',
        action: async () => {
          const name = body.querySelector('#np-name').value.trim();
          const parent = body.querySelector('#np-path').value.trim();
          const error = body.querySelector('#np-error');
          error.classList.remove('show');
          if (!name || !parent) {
            error.textContent = t('Bitte Name und Speicherort angeben.');
            error.classList.add('show');
            return false;
          }
          try {
            const result = await post('/api/project/create', { path: parent, name, template });
            modal.close();
            await adoptProject(result);
            toast(t('Projekt „{name}“ erstellt.', { name }), 'ok');
          } catch (err) {
            error.textContent = err.message;
            error.classList.add('show');
          }
        }
      }
    ]
  });
  setTimeout(() => body.querySelector('#np-name')?.select(), 50);
}

/* ------------------------------------------------------------------ */
/* Einstellungen                                                       */
/* ------------------------------------------------------------------ */

/** Relative Pfade zusätzlich als vollen Pfad zeigen. */
function resolvedHint(info) {
  const resolved = info?.resolved;
  if (!resolved || resolved === info.path) return '';
  return `<div class="hint">${t('Liegt unter: {path}', { path: escapeAttr(resolved) })}</div>`;
}

async function settingsDialog() {
  const gbx = state.gbx || {};
  const cxx = state.cxx || state.health?.cxx || {};
  const emsdk = state.emsdk || state.health?.emsdk || {};
  const html5 = state.html5 || state.health?.html5 || {};
  const keySet = Boolean(state.health?.config?.googleFontsKeySet);
  const p = state.project || {};
  let templates = [];
  let templatesDir = '';
  try {
    const stored = await get('/api/templates');
    templates = stored.templates || [];
    templatesDir = stored.dir || '';
  } catch {}
  let draft = normalizeAppearance(state.appearance);
  const originalAppearance = normalizeAppearance(state.appearance);
  const presetIds = [...Object.keys(EDITOR_THEME_PRESETS.dark), 'custom'];
  const presetOptions = presetIds
    .map((id) => `<option value="${id}">${t(EDITOR_THEME_PRESETS.dark[id]?.label || EDITOR_THEME_PRESET_LABELS[id])}</option>`)
    .join('');
  const editorFields = EDITOR_THEME_FIELDS.map((field) => `
    <label class="theme-field" title="${t(field.label)}">
      <input type="color" data-theme-group="${field.group}" data-theme-key="${field.key}" value="${draft.themes[draft.mode].editor[field.group][field.key]}">
      <span>${t(field.label)}</span>
    </label>`).join('');

  const body = document.createElement('div');
  body.innerHTML = `
    <div class="settings-tabs" role="tablist">
      <button type="button" role="tab" class="active" data-tab="general">${t('Allgemein')}</button>
      <button type="button" role="tab" data-tab="project">${t('Projekt')}</button>
      <button type="button" role="tab" data-tab="appearance">${t('Darstellung')}</button>
      <button type="button" role="tab" data-tab="templates">${t('Vorlagen')}</button>
    </div>

    <section class="settings-pane active" data-pane="general">
      <p class="pane-intro">${t('Sprache und Interpreter. GameBasiX läuft direkt aus den Quelldateien – es gibt keinen Compiler-Zwischenschritt und keine Lizenz.')}</p>
      <div class="field">
        <label>${t('Sprache')}</label>
        <select id="set-language">
          ${availableLanguages().map((language) => `<option value="${language.id}" ${language.id === getLanguage() ? 'selected' : ''}>${language.label}</option>`).join('')}
        </select>
        <div class="hint">${t('Gilt sofort für die Oberfläche und für die Meldungen des Interpreters.')}</div>
      </div>
      <div class="field">
        <label>${t('gbx.exe (Interpreter)')}</label>
        <div class="path-row">
          <input type="text" id="set-gbx" value="${escapeAttr(gbx.path || '')}" spellcheck="false">
          <button class="btn ghost" id="set-check">${t('Prüfen')}</button>
        </div>
        <div class="hint" id="set-gbx-info">${gbx.ok ? t('Gefunden: GameBasiX {version}', { version: gbx.version || '' }) : t('gbx.exe wurde nicht gefunden.')}</div>
        ${resolvedHint(gbx)}
        <div class="hint">${t('Ohne Angabe gilt build\\gbx.exe relativ zur Editor-EXE.')}</div>
      </div>
      <div class="field">
        <label>${t('C++-Compiler (g++)')}</label>
        <div class="path-row">
          <input type="text" id="set-cxx" value="${escapeAttr(cxx.path || '')}" spellcheck="false" placeholder="${t('automatisch suchen')}">
          <button class="btn ghost" id="set-cxx-check">${t('Prüfen')}</button>
        </div>
        <div class="hint" id="set-cxx-info">${cxx.ok ? t('Gefunden: g++ {version}', { version: cxx.version || '' }) : t('g++ wurde nicht gefunden.')}</div>
        ${resolvedHint(cxx)}
        <div class="hint">${t('Nur für INLINE-Blöcke. Ohne Angabe gilt compiler\\bin\\g++.exe relativ zur Editor-EXE.')}</div>
      </div>
      <div class="field">
        <label>${t('HTML5 (Emscripten SDK)')}</label>
        <div class="path-row">
          <input type="text" id="set-emsdk" value="${escapeAttr(emsdk.path || '')}" spellcheck="false" placeholder="emsdk">
          <button class="btn ghost" id="set-emsdk-check">${t('Prüfen')}</button>
        </div>
        <div class="hint" id="set-emsdk-info">${emsdk.ok ? t('Gefunden: Emscripten {version}', { version: emsdk.version || '' }) : t('emsdk wurde nicht gefunden.')}</div>
        ${resolvedHint(emsdk)}
        <div class="hint">${t('Wird nur für den HTML5-Build gebraucht. Ohne Angabe gilt emsdk relativ zur Editor-EXE; „Herunterladen“ holt das SDK (einige hundert MB) dorthin.')}</div>
        <div class="path-row" style="margin-top:8px">
          <button class="btn ghost" id="set-emsdk-install">${t('Herunterladen …')}</button>
          <button class="btn ghost" id="set-emsdk-build">${t('Player bauen')}</button>
        </div>
        <div class="hint" id="set-emsdk-progress">${html5.playerReady ? t('Der wasm-Player ist gebaut.') : t('Der wasm-Player fehlt noch.')}</div>
      </div>
      <div class="field">
        <label>${t('Google Fonts (API-Key)')}</label>
        <div class="path-row">
          <input type="password" id="set-fonts-key" value="" placeholder="${keySet ? t('API-Key ist gesetzt') : t('API-Key (optional)')}" autocomplete="off" spellcheck="false">
          <button type="button" class="btn ghost" id="set-fonts-clear" ${keySet ? '' : 'disabled'}>${t('Entfernen')}</button>
        </div>
        <div class="hint" id="set-fonts-hint">${keySet ? t('Ein API-Key ist gespeichert. Leer lassen, um ihn zu behalten.') : t('Ohne Key nutzt das Werkzeug die öffentliche Fontliste von fonts.google.com.')}</div>
        <div class="hint">${t('Der Key wird lokal in den Einstellungen gespeichert und nur für die Webfonts-API verwendet.')}</div>
      </div>
      <div class="license-box">
        <div class="license-head">
          <span class="t">${t('Projektdatei')}</span>
          <span class="state">${state.project ? escapeAttr(p.name || '') : t('kein Projekt geöffnet')}</span>
        </div>
        <div class="hint">${t('Fenster, Bildrate und Einstiegspunkt stehen in der .gbxproj. Die IDE schreibt sie beim Speichern neu.')}</div>
      </div>
    </section>

    <section class="settings-pane" data-pane="project">
      <p class="pane-intro">${t('Einstellungen des geöffneten Projekts. Sie landen in der Projektdatei und gelten beim nächsten Start.')}</p>
      <div class="field">
        <label>${t('Fenstertitel')}</label>
        <input type="text" id="set-title" value="${escapeAttr(p.title || p.name || '')}" spellcheck="false" ${state.project ? '' : 'disabled'}>
      </div>
      <div class="field">
        <label>${t('Einstiegspunkt')}</label>
        <input type="text" id="set-entry" value="${escapeAttr(p.entry || '')}" spellcheck="false" disabled>
        <div class="hint">${t('Die Datei, die gbx.exe startet. Weitere Dateien binden sich mit INCLUDE ein.')}</div>
      </div>
      <div class="row">
        <div class="field">
          <label>${t('Fensterbreite')}</label>
          <input type="number" id="set-width" value="${p.width || 640}" min="64" max="7680" ${state.project ? '' : 'disabled'}>
        </div>
        <div class="field">
          <label>${t('Fensterhöhe')}</label>
          <input type="number" id="set-height" value="${p.height || 480}" min="64" max="4320" ${state.project ? '' : 'disabled'}>
        </div>
      </div>
      <div class="row">
        <div class="field">
          <label>${t('Framerate')}</label>
          <input type="number" id="set-fps" value="${p.frameRate ?? 60}" min="0" max="240" ${state.project ? '' : 'disabled'}>
          <div class="hint">${t('0 = unbegrenzt; VSYNC bremst dann auf die Bildwiederholrate des Monitors.')}</div>
        </div>
        <div class="field">
          <label>${t('Musiklautstärke')}</label>
          <input type="number" id="set-volume" value="${p.volume ?? 1}" min="0" max="1" step="0.05" ${state.project ? '' : 'disabled'}>
        </div>
      </div>
      <div class="field">
        <label class="check"><input type="checkbox" id="set-fullscreen" ${p.fullscreen ? 'checked' : ''} ${state.project ? '' : 'disabled'}> ${t('Vollbild starten')}</label>
        <label class="check"><input type="checkbox" id="set-resizable" ${p.resizable !== false ? 'checked' : ''} ${state.project ? '' : 'disabled'}> ${t('Fenster skalierbar')}</label>
        <label class="check"><input type="checkbox" id="set-vsync" ${p.vsync !== false ? 'checked' : ''} ${state.project ? '' : 'disabled'}> ${t('VSYNC')}</label>
        <label class="check"><input type="checkbox" id="set-audio" ${p.audio !== false ? 'checked' : ''} ${state.project ? '' : 'disabled'}> ${t('Sound und Musik')}</label>
      </div>
      <div class="field">
        <label>${t('Asset-Ordner')}</label>
        <input type="text" id="set-assets" value="${escapeAttr(p.assetDir || 'assets')}" spellcheck="false" ${state.project ? '' : 'disabled'}>
        <div class="hint">${t('Zusätzlicher Suchpfad für Bilder, Schriften und Sounds. Relative Pfade gelten ab dem Projektordner.')}</div>
      </div>
    </section>

    <section class="settings-pane" data-pane="appearance">
      <p class="pane-intro">${t('Erscheinungsbild der gesamten IDE: Hell oder Dunkel, Akzentfarbe und alle Editorfarben. Jede Änderung ist sofort sichtbar und gilt nach dem Speichern dauerhaft.')}</p>
      <div class="field">
        <label>${t('Modus')}</label>
        <div class="mode-switch">
          <button type="button" class="mode-btn" data-mode="dark">
            <span class="t">🌙 ${t('Dunkel')}</span>
            <span class="s">${t('Dunkle Flächen, helle Schrift')}</span>
          </button>
          <button type="button" class="mode-btn" data-mode="light">
            <span class="t">☀ ${t('Hell')}</span>
            <span class="s">${t('Helle Flächen, dunkle Schrift')}</span>
          </button>
        </div>
        <div class="hint">${t('Hell und Dunkel merken sich getrennte Farben; beim Umschalten wird der Satz des gewählten Modus geladen.')}</div>
      </div>
      <div class="theme-preview" id="set-preview">
        <div class="tp-line"><span data-tp="keyword">TYPE</span> <span data-tp="userType">PUNKT</span></div>
        <div class="tp-line"><span data-tp="command">PRINT</span> <span data-tp="string">"Hallo"</span>, <span data-tp="number">42</span></div>
        <div class="tp-line"><span data-tp="comment">' ${t('Kommentar')}</span></div>
        <div class="tp-ui">
          <span class="swatch"><i data-sw="background"></i>${t('Hintergrund')}</span>
          <span class="swatch"><i data-sw="surface"></i>${t('Fläche')}</span>
          <span class="swatch"><i data-sw="accent"></i>${t('Akzent')}</span>
          <span class="swatch"><i data-sw="text"></i>${t('Text')}</span>
        </div>
      </div>
      <div class="license-box">
        <div class="license-head">
          <span class="t">${t('Oberfläche')}</span>
          <button class="btn ghost" id="set-app-reset">${t('Standard für diesen Modus')}</button>
        </div>
        <div class="theme-grid">
          <label class="theme-field"><input type="color" id="set-app-accent" value="${draft.themes[draft.mode].accent}"><span>${t('Akzentfarbe')}</span></label>
          <label class="theme-field"><input type="color" id="set-app-bg" value="${draft.themes[draft.mode].background}"><span>${t('Fensterhintergrund')}</span></label>
          <label class="theme-field"><input type="color" id="set-app-text" value="${draft.themes[draft.mode].text}"><span>${t('Schriftfarbe')}</span></label>
        </div>
        <div class="hint">${t('Der Akzent färbt Buttons, aktive Zeilen und Fokuspunkte. Aus Hintergrund und Schrift entstehen alle Flächen, Rahmen und Abstufungen.')}</div>
      </div>
      <div class="license-box">
        <div class="license-head">
          <span class="t">${t('Editorfarben')}</span>
          <select id="set-theme-preset" class="theme-preset">${presetOptions}</select>
          <button class="btn ghost" id="set-theme-reset">${t('Zurücksetzen')}</button>
        </div>
        <div class="theme-grid" id="set-theme-grid">${editorFields}</div>
        <div class="hint">${t('Gilt für den aktiven Modus. Eigene TYPEs werden in der Farbe „Eigener Typ“ hervorgehoben.')}</div>
      </div>
    </section>

    <section class="settings-pane" data-pane="templates">
      <p class="pane-intro">${t('Eigene Projektvorlagen als Dateien. Sie stehen beim Anlegen eines Projekts zur Auswahl.')}</p>
      <div class="license-box">
        <div class="license-head">
          <span class="t">${t('Eigene Vorlagen')}</span>
          <span class="state" id="set-tpl-state"></span>
        </div>
        <div class="hint">${t('Vorlagen liegen als Dateien in {dir} und stehen beim Anlegen eines Projekts zur Auswahl.', { dir: templatesDir || t('Vorlagenordner') })}</div>
        <div class="tpl-list" id="set-tpl-list"></div>
        <div class="row" style="margin-top:10px">
          <div class="field">
            <label>${t('Name der Vorlage')}</label>
            <input type="text" id="set-tpl-name" spellcheck="false" placeholder="${t('Mein Starter')}" ${state.project ? '' : 'disabled'}>
          </div>
          <div class="field">
            <label>${t('Beschreibung')}</label>
            <input type="text" id="set-tpl-desc" spellcheck="false" placeholder="${t('Kurzer Hinweis für die Auswahl')}" ${state.project ? '' : 'disabled'}>
          </div>
        </div>
        <div class="path-row">
          <button class="btn ghost" id="set-tpl-save" ${state.project ? '' : 'disabled'}>${t('Aktuelles Projekt als Vorlage speichern')}</button>
          <button class="btn ghost" id="set-tpl-open">${t('Vorlagenordner öffnen')}</button>
        </div>
        <div class="hint">${t('Gespeichert werden alle .gbx-Quelldateien und die Projekteinstellungen. Bilder und Klänge gehören nicht zur Vorlage.')}</div>
        ${state.project ? '' : `<div class="hint">${t('Zum Speichern zuerst ein Projekt öffnen.')}</div>`}
      </div>
    </section>

    <div class="error-box" id="set-error"></div>
  `;

  const tabs = [...body.querySelectorAll('[data-tab]')];
  const panes = [...body.querySelectorAll('[data-pane]')];
  tabs.forEach((tab) =>
    tab.addEventListener('click', () => {
      for (const item of tabs) item.classList.toggle('active', item === tab);
      for (const pane of panes) pane.classList.toggle('active', pane.dataset.pane === tab.dataset.tab);
    })
  );

  body.querySelector('#set-check').addEventListener('click', async () => {
    const info = body.querySelector('#set-gbx-info');
    info.textContent = t('Prüfe …');
    try {
      const result = await post('/api/gbx/check', { path: body.querySelector('#set-gbx').value.trim() });
      state.gbx = result;
      info.textContent = result.ok ? t('Gefunden: GameBasiX {version}', { version: result.version }) : result.error;
      info.style.color = result.ok ? 'var(--accent)' : 'var(--red)';
      updateGbxStatus();
    } catch (error) {
      info.textContent = error.message;
      info.style.color = 'var(--red)';
    }
  });

  body.querySelector('#set-cxx-check').addEventListener('click', async () => {
    const info = body.querySelector('#set-cxx-info');
    info.textContent = t('Prüfe …');
    try {
      const result = await post('/api/compiler/check', { path: body.querySelector('#set-cxx').value.trim() });
      info.textContent = result.ok ? t('Gefunden: g++ {version}', { version: result.version }) : result.error;
      info.style.color = result.ok ? 'var(--accent)' : 'var(--red)';
    } catch (error) {
      info.textContent = error.message;
      info.style.color = 'var(--red)';
    }
  });

  // API-Key: gespeicherten Key nicht anzeigen, nur das Entfernen anbieten.
  body.querySelector('#set-fonts-clear').addEventListener('click', () => {
    const input = body.querySelector('#set-fonts-key');
    input.value = '';
    input.dataset.clear = '1';
    input.placeholder = t('API-Key wird entfernt');
    body.querySelector('#set-fonts-hint').textContent = t('Der gespeicherte API-Key wird beim Speichern gelöscht.');
    body.querySelector('#set-fonts-clear').disabled = true;
  });

  /* HTML5: SDK holen, prüfen, den wasm-Player bauen. */
  const emsdkField = () => body.querySelector('#set-emsdk').value.trim();
  const emsdkInfo = () => body.querySelector('#set-emsdk-info');
  const html5Progress = () => body.querySelector('#set-emsdk-progress');
  const showEmsdk = (result) => {
    state.emsdk = result;
    if (state.health) state.health.emsdk = result;
    const info = emsdkInfo();
    info.textContent = result.ok
      ? t('Gefunden: Emscripten {version}', { version: result.version })
      : result.error;
    info.style.color = result.ok ? 'var(--accent)' : 'var(--red)';
    updateButtons();
  };
  const progressLine = (event) => {
    if (event.type === 'log') html5Progress().textContent = event.text.slice(0, 160);
  };

  body.querySelector('#set-emsdk-check').addEventListener('click', async () => {
    emsdkInfo().textContent = t('Prüfe …');
    try {
      showEmsdk(await post('/api/emsdk/check', { path: emsdkField() }));
    } catch (error) {
      emsdkInfo().textContent = error.message;
      emsdkInfo().style.color = 'var(--red)';
    }
  });

  body.querySelector('#set-emsdk-install').addEventListener('click', async () => {
    const button = body.querySelector('#set-emsdk-install');
    button.disabled = true;
    html5Progress().textContent = t('emsdk wird heruntergeladen …');
    try {
      const result = await post('/api/emsdk/install', { path: emsdkField() });
      subscribeJob(result.jobId, progressLine, async (done) => {
        button.disabled = false;
        html5Progress().textContent = done.ok
          ? t('emsdk ist eingerichtet.')
          : t('Die Installation ist nicht abgeschlossen.');
        html5Progress().style.color = done.ok ? 'var(--accent)' : 'var(--red)';
        showEmsdk(done.emsdk || (await post('/api/emsdk/check', { path: emsdkField() })));
      });
    } catch (error) {
      button.disabled = false;
      html5Progress().textContent = error.message;
      html5Progress().style.color = 'var(--red)';
    }
  });

  body.querySelector('#set-emsdk-build').addEventListener('click', async () => {
    const button = body.querySelector('#set-emsdk-build');
    button.disabled = true;
    html5Progress().textContent = t('Baue den wasm-Player …');
    try {
      const result = await post('/api/html5/build');
      subscribeJob(result.jobId, progressLine, (done) => {
        button.disabled = false;
        state.html5 = done.html5 || state.html5;
        html5Progress().textContent = done.ok
          ? t('Der wasm-Player ist gebaut.')
          : t('Der wasm-Build ist fehlgeschlagen.');
        html5Progress().style.color = done.ok ? 'var(--accent)' : 'var(--red)';
        updateButtons();
      });
    } catch (error) {
      button.disabled = false;
      html5Progress().textContent = error.message;
      html5Progress().style.color = 'var(--red)';
    }
  });

  /* Darstellung: Änderungen wirken sofort, gespeichert wird beim Speichern. */
  const preview = body.querySelector('#set-preview');
  const modeButtons = [...body.querySelectorAll('.mode-btn')];
  const accentInput = body.querySelector('#set-app-accent');
  const bgInput = body.querySelector('#set-app-bg');
  const textInput = body.querySelector('#set-app-text');
  const presetSelect = body.querySelector('#set-theme-preset');
  const themeInputs = [...body.querySelectorAll('input[data-theme-group]')];

  const readEditorInputs = () => {
    const spec = { preset: presetSelect.value, tokens: {}, colors: {} };
    for (const input of themeInputs) {
      const group = input.dataset.themeGroup;
      if (!spec[group]) continue;
      spec[group][input.dataset.themeKey] = input.value;
    }
    return spec;
  };
  const updatePreview = () => {
    const theme = draft.themes[draft.mode];
    const palette = buildAppPalette(theme, draft.mode);
    preview.style.background = theme.editor.colors.background;
    preview.style.color = theme.editor.colors.foreground;
    preview.style.borderColor = palette.border;
    for (const span of preview.querySelectorAll('[data-tp]')) {
      span.style.color = theme.editor.tokens[span.dataset.tp] || theme.editor.colors.foreground;
    }
    const swatches = { background: palette.bg, surface: palette.bgElev, accent: palette.accent, text: palette.text };
    for (const [key, color] of Object.entries(swatches)) {
      const el = preview.querySelector(`[data-sw="${key}"]`);
      if (el) el.style.background = color;
    }
  };
  const fillEditorInputs = (spec) => {
    for (const input of themeInputs) {
      input.value = spec[input.dataset.themeGroup][input.dataset.themeKey];
    }
    presetSelect.value = presetIds.includes(spec.preset) ? spec.preset : 'custom';
  };
  const fillAppearanceInputs = () => {
    const theme = draft.themes[draft.mode];
    accentInput.value = theme.accent;
    bgInput.value = theme.background;
    textInput.value = theme.text;
    fillEditorInputs(theme.editor);
    for (const button of modeButtons) button.classList.toggle('active', button.dataset.mode === draft.mode);
    updatePreview();
  };
  const applyDraft = () => {
    draft = normalizeAppearance({
      mode: draft.mode,
      themes: {
        ...draft.themes,
        [draft.mode]: {
          accent: accentInput.value,
          background: bgInput.value,
          text: textInput.value,
          editor: readEditorInputs()
        }
      }
    });
    state.appearance = applyAppearance(monaco, draft);
    updatePreview();
  };

  modeButtons.forEach((button) =>
    button.addEventListener('click', () => {
      const next = button.dataset.mode === 'light' ? 'light' : 'dark';
      if (next === draft.mode) return;
      // Eingaben des bisherigen Modus sichern, dann den Satz des neuen laden.
      draft = normalizeAppearance({
        mode: draft.mode,
        themes: {
          ...draft.themes,
          [draft.mode]: {
            accent: accentInput.value,
            background: bgInput.value,
            text: textInput.value,
            editor: readEditorInputs()
          }
        }
      });
      draft.mode = next;
      fillAppearanceInputs();
      state.appearance = applyAppearance(monaco, draft);
    })
  );
  accentInput.addEventListener('input', applyDraft);
  bgInput.addEventListener('input', applyDraft);
  textInput.addEventListener('input', applyDraft);
  themeInputs.forEach((input) =>
    input.addEventListener('input', () => {
      presetSelect.value = 'custom';
      applyDraft();
    })
  );
  presetSelect.addEventListener('change', () => {
    const preset = EDITOR_THEME_PRESETS[draft.mode][presetSelect.value];
    if (!preset) return;
    fillEditorInputs(preset.theme);
    applyDraft();
  });
  body.querySelector('#set-theme-reset').addEventListener('click', () => {
    fillEditorInputs(EDITOR_THEME_PRESETS[draft.mode].standard.theme);
    applyDraft();
  });
  body.querySelector('#set-app-reset').addEventListener('click', () => {
    const fallback = DEFAULT_APPEARANCE.themes[draft.mode];
    accentInput.value = fallback.accent;
    bgInput.value = fallback.background;
    textInput.value = fallback.text;
    applyDraft();
  });
  fillAppearanceInputs();

  /* Eigene Vorlagen: liegen als Dateien im Vorlagenordner. */
  const tplList = body.querySelector('#set-tpl-list');
  const tplState = body.querySelector('#set-tpl-state');
  const renderTemplates = () => {
    tplState.textContent = templates.length ? t('{n} Vorlage(n)', { n: templates.length }) : t('keine');
    tplList.innerHTML = templates.length
      ? templates
          .map(
            (tpl) => `
            <div class="tpl-row">
              <span class="tpl-name">${escapeAttr(tpl.name)}</span>
              <span class="tpl-meta">${t('{n} Datei(en)', { n: tpl.files?.length || 0 })}</span>
              <button class="btn ghost tpl-delete" data-delete="${escapeAttr(tpl.id)}" title="${t('Vorlage löschen')}">✕</button>
            </div>`
          )
          .join('')
      : `<div class="hint">${t('Noch keine eigene Vorlage gespeichert.')}</div>`;
  };
  const takeTemplates = (result) => {
    if (Array.isArray(result?.templates)) {
      templates = result.templates;
      state.templates = templates;
    }
    renderTemplates();
  };
  renderTemplates();

  tplList.addEventListener('click', async (event) => {
    const button = event.target.closest('[data-delete]');
    if (!button) return;
    try {
      takeTemplates(await post('/api/templates/delete', { id: button.dataset.delete }));
      toast(t('Vorlage gelöscht.'), 'ok');
    } catch (error) {
      toast(error.message, 'error');
    }
  });

  body.querySelector('#set-tpl-save').addEventListener('click', async () => {
    const name = body.querySelector('#set-tpl-name').value.trim();
    if (!name) {
      toast(t('Bitte einen Namen für die Vorlage angeben.'), 'warn');
      return;
    }
    try {
      const result = await post('/api/templates', {
        dir: state.projectDir,
        name,
        description: body.querySelector('#set-tpl-desc').value.trim()
      });
      takeTemplates(result);
      toast(t('Vorlage „{name}“ gespeichert.', { name: result.template?.name || name }), 'ok');
    } catch (error) {
      toast(error.message, 'error');
    }
  });

  body.querySelector('#set-tpl-open').addEventListener('click', () => openInExplorer(templatesDir));

  // Ohne Speichern wird das vorherige Erscheinungsbild wiederhergestellt.
  let appearanceSaved = false;
  const modal = openModal({
    title: t('Einstellungen'),
    wide: true,
    body,
    onClose: () => {
      if (!appearanceSaved) state.appearance = applyAppearance(monaco, originalAppearance);
    },
    footer: [
      { label: t('Abbrechen'), action: () => modal.close() },
      {
        label: t('Speichern'),
        variant: 'primary',
        action: async () => {
          const errorBox = body.querySelector('#set-error');
          errorBox.classList.remove('show');
          try {
            const chosenLanguage = body.querySelector('#set-language').value;
            const fontsKey = body.querySelector('#set-fonts-key');
            const settingsBody = {
              gbxPath: body.querySelector('#set-gbx').value.trim(),
              cxxPath: body.querySelector('#set-cxx').value.trim(),
              emsdkPath: emsdkField(),
              language: chosenLanguage,
              appearance: draft
            };
            if (fontsKey.value.trim()) settingsBody.googleFontsApiKey = fontsKey.value.trim();
            if (fontsKey.dataset.clear === '1') settingsBody.clearGoogleFontsApiKey = true;
            const settings = await post('/api/settings', settingsBody);
            state.gbx = settings.gbx;
            state.cxx = settings.cxx;
            state.emsdk = settings.emsdk;
            state.html5 = settings.html5;
            state.health.gbx = settings.gbx;
            state.health.cxx = settings.cxx;
            state.health.emsdk = settings.emsdk;
            state.health.html5 = settings.html5;
            state.health.config = settings.config;
            appearanceSaved = true;
            state.appearance = applyAppearance(monaco, settings.config?.appearance || draft);
            setLanguage(chosenLanguage);
            state.ref.lang = chosenLanguage;
            updateGbxStatus();
            if (state.project) {
              state.project.title = body.querySelector('#set-title').value.trim();
              state.project.width = Number(body.querySelector('#set-width').value) || 640;
              state.project.height = Number(body.querySelector('#set-height').value) || 480;
              state.project.frameRate = Number(body.querySelector('#set-fps').value) || 0;
              state.project.volume = Number(body.querySelector('#set-volume').value);
              state.project.fullscreen = body.querySelector('#set-fullscreen').checked;
              state.project.resizable = body.querySelector('#set-resizable').checked;
              state.project.vsync = body.querySelector('#set-vsync').checked;
              state.project.audio = body.querySelector('#set-audio').checked;
              state.project.assetDir = body.querySelector('#set-assets').value.trim();
              await post('/api/project/save', { projectDir: state.projectDir, project: state.project });
              updateStatus();
            }
            modal.close();
            toast(t('Einstellungen gespeichert.'), 'ok');
          } catch (error) {
            errorBox.textContent = error.message;
            errorBox.classList.add('show');
            errorBox.scrollIntoView({ block: 'nearest' });
          }
        }
      }
    ]
  });
}

/* ------------------------------------------------------------------ */
/* Bauen                                                               */
/* ------------------------------------------------------------------ */

async function buildProject() {
  if (!state.project || state.building) return false;
  state.building = true;
  cancelLiveCheck();
  updateButtons();
  $('build-icon').outerHTML = '<span class="build-spinner" id="build-icon"></span>';
  setBuildStatus(t('Prüfe …'));
  clearProblems();
  showPanel('output');
  $('output').textContent = '';
  $('console').textContent = '';
  const saved = await saveAll();
  if (!saved) {
    finishBuildUi();
    setBuildStatus(t('Speichern fehlgeschlagen'), 'error');
    return false;
  }

  try {
    const result = await post('/api/build', { projectDir: state.projectDir, project: state.project });
    state.buildJobId = result.jobId;
    subscribeJob(result.jobId, handleBuildEvent, (done) => {
      finishBuildUi();
      if (done.ok) {
        state.checked = true;
        state.dirtySinceBuild = false;
        setBuildStatus(t('Syntax OK'), 'ok');
        toast(t('Syntaxprüfung erfolgreich. F5 startet das Projekt.'), 'ok');
        $('status-build').title = done.entryPath || '';
      } else {
        const errors = state.diagnostics.filter((d) => d.severity === 'error').length;
        setBuildStatus(
          errors ? t(errors === 1 ? 'Prüfung fehlgeschlagen (1 Fehler)' : 'Prüfung fehlgeschlagen ({count} Fehler)', { count: errors }) : t('Prüfung fehlgeschlagen'),
          'error'
        );
        toast(
          errors
            ? t(errors === 1 ? 'Die Syntaxprüfung hat 1 Fehler gefunden. Details unter „Probleme“.' : 'Die Syntaxprüfung hat {count} Fehler gefunden. Details unter „Probleme“.', {
                count: errors
              })
            : t('Die Syntaxprüfung ist fehlgeschlagen. Details unter „Probleme“.'),
          'error'
        );
        showPanel('problems');
      }
    });
  } catch (error) {
    finishBuildUi();
    setBuildStatus(t('Prüfung fehlgeschlagen'), 'error');
    toast(error.message, 'error');
    return false;
  }
  return true;
}

function finishBuildUi() {
  state.building = false;
  const spinner = $('build-icon');
  if (spinner) spinner.outerHTML = '<span id="build-icon">⚙</span>';
  updateButtons();
}

function handleBuildEvent(event) {
  if (event.type === 'title') appendOutput(event.text + '\n', 'title');
  else if (event.type === 'log') appendOutput(event.text + '\n', event.level || 'info');
  else if (event.type === 'diagnostic') addDiagnostic(event.diagnostic);
  else if (event.type === 'done') {
    if (event.error) appendOutput(event.error + '\n', 'error');
  }
}

function appendOutput(text, level = 'info') {
  appendTo($('output'), text, level);
}

function appendTo(pre, text, level) {
  if (!pre) return;
  const span = document.createElement('span');
  const cls = level === 'error' ? 'l-error' : level === 'warning' ? 'l-warning' : level === 'success' ? 'l-success' : level === 'title' ? 'l-title' : level === 'muted' ? 'l-muted' : level === 'stdout' ? 'l-stdout' : level === 'stderr' ? 'l-stderr' : '';
  if (cls) span.className = cls;
  span.textContent = String(text);
  pre.append(span);
  while (pre.childNodes.length > 2500) pre.removeChild(pre.firstChild);
  pre.scrollTop = pre.scrollHeight;
}

/* ------------------------------------------------------------------ */
/* Diagnosen                                                           */
/* ------------------------------------------------------------------ */

function clearProblems() {
  state.diagnostics = [];
  clearAllMarkers();
  renderProblems();
}

function clearAllMarkers() {
  if (!monaco) return;
  for (const model of monaco.editor.getModels()) {
    monaco.editor.setModelMarkers(model, 'gbx', []);
  }
}

/* ------------------------------------------------------------------ */
/* Meldungen verständlich machen                                      */
/* ------------------------------------------------------------------ */

/** Alle Namen, die der Editor als Befehl oder Schlüsselwort kennt. */
function knownCommandNames() {
  const names = new Set();
  for (const command of state.commands || []) {
    const name = String(command.name || '').toUpperCase();
    if (name) names.add(name);
  }
  for (const fn of state.projectSymbols?.functions || []) {
    names.add(String(fn.name || '').toUpperCase());
  }
  return names;
}

/** Abstand zweier Wörter als Anzahl Einfügungen, Löschungen und Ersetzungen. */
function editDistance(a, b) {
  const previous = new Array(b.length + 1);
  const current = new Array(b.length + 1);
  for (let j = 0; j <= b.length; j++) previous[j] = j;
  for (let i = 1; i <= a.length; i++) {
    current[0] = i;
    for (let j = 1; j <= b.length; j++) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1;
      current[j] = Math.min(previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost);
    }
    for (let j = 0; j <= b.length; j++) previous[j] = current[j];
  }
  return previous[b.length];
}

/** Nächstgelegener bekannter Name – nur bei eindeutigem Tippfehler. */
function suggestCommand(word) {
  const target = String(word || '').toUpperCase();
  if (target.length < 3) return '';
  let best = '';
  let bestScore = Infinity;
  for (const command of state.commands || []) {
    if (command.kind === 'operator' || command.kind === 'overview') continue;
    const name = String(command.name || '').toUpperCase();
    if (!name || Math.abs(name.length - target.length) > 2) continue;
    const score = editDistance(target, name);
    if (score < bestScore) {
      best = command.name;
      bestScore = score;
    }
  }
  const limit = target.length > 4 ? 2 : 1;
  return bestScore <= limit ? best : '';
}

/**
 * Ergänzt eine Meldung um einen Tipp und – wenn möglich – um den
 * unbekannten Befehl, den der Interpreter nicht verstanden hat.
 */
function enrichDiagnostic(diagnostic) {
  const d = { ...diagnostic };
  const hints = [];
  if (d.code === 'GBX0001') {
    hints.push(t('Der Interpreter konnte diese Zeile nicht lesen. Meist ist es ein Tippfehler im Befehl, ein fehlendes Komma oder ein offener Block.'));
  }
  if (d.context) {
    const match = /^\s*([A-Za-z_][A-Za-z0-9_$#%]*)\b/.exec(d.context);
    if (match && !/^\s*[A-Za-z_][\w$#%]*\s*[=,(]/.test(d.context)) {
      const word = match[1];
      if (!knownCommandNames().has(word.toUpperCase())) {
        const suggestion = suggestCommand(word);
        if (suggestion) {
          hints.push(t('Unbekannter Befehl „{name}“ – meintest du „{suggestion}“?', { name: word, suggestion }));
          d.column = match.index + match[0].indexOf(word) + 1;
          d.tokenLength = word.length;
        }
      }
    }
  }
  if (hints.length) d.hint = hints.join(' ');
  return d;
}

function addDiagnostic(diagnostic) {
  if (!diagnostic) return;
  const enriched = enrichDiagnostic(diagnostic);
  const key = `${enriched.file}|${enriched.line}|${enriched.message}`;
  if (state.diagnostics.some((d) => `${d.file}|${d.line}|${d.message}` === key)) return;
  state.diagnostics.push(enriched);
  renderProblems();
  applyMarkers();
}

function renderProblems() {
  const list = $('problems-list');
  list.innerHTML = '';
  const errors = state.diagnostics.filter((d) => d.severity === 'error').length;
  const warnings = state.diagnostics.length - errors;
  dock?.setMeta('problems', String(state.diagnostics.length), errors ? 'errors' : '');
  $('problems-empty').hidden = state.diagnostics.length > 0;
  for (const d of state.diagnostics) {
    const row = document.createElement('div');
    row.className = 'problem ' + (d.severity === 'warning' ? 'warning' : 'error');
    const icon = document.createElement('span');
    icon.className = 'ico';
    icon.textContent = d.severity === 'warning' ? '▲' : '●';
    const loc = document.createElement('span');
    loc.className = 'loc';
    loc.textContent = diagnosticLocation(d);
    loc.title = d.file || '';
    const body = document.createElement('div');
    body.className = 'body';
    const message = document.createElement('div');
    message.className = 'msg';
    message.append(document.createTextNode(d.message));
    if (d.code) {
      const code = document.createElement('span');
      code.className = 'code';
      code.textContent = d.code;
      message.append(' ', code);
    }
    body.append(message);
    if (d.hint) {
      const hint = document.createElement('div');
      hint.className = 'hint';
      hint.textContent = d.hint;
      body.append(hint);
    }
    if (d.context?.trim()) {
      const context = document.createElement('code');
      context.className = 'ctx';
      context.textContent = `${d.line} │ ${d.context.trim()}`;
      body.append(context);
    }
    row.append(icon, loc, body);
    row.addEventListener('click', () => {
      const rel = relativeToProject(d.file);
      if (!rel) {
        toast(t('Diese Meldung gehört zum Linken und hat keine Quelldatei.'), 'warn');
        return;
      }
      openFile(rel, { line: d.line }).catch((error) => toast(error.message, 'error'));
    });
    list.append(row);
  }
}

function diagnosticLocation(d) {
  if (!d.file) return d.source === 'ld' ? t('Linker') : t('Build');
  const relative = relativeToProject(d.file);
  const label = relative || d.file;
  const prefix = d.generated ? t('generiert › ') : '';
  return `${prefix}${relative ? label.split('/').pop() : shortPath(d.file)}:${d.line}`;
}

function shortPath(file) {
  if (!file) return '';
  const rel = relativeToProject(file);
  return rel || file.split(/[\\/]/).slice(-2).join('/');
}

function relativeToProject(file) {
  if (!file || !state.projectDir) return '';
  const root = state.projectDir.replace(/\\/g, '/').replace(/\/$/, '').toLowerCase();
  const target = file.replace(/\\/g, '/');
  if (!target.toLowerCase().startsWith(root + '/')) return '';
  return target.slice(root.length + 1);
}

function applyMarkers() {
  if (!monaco) return;
  for (const model of monaco.editor.getModels()) {
    if (model.getLanguageId() !== 'gbx') continue;
    const modelPath = model.uri.fsPath.replace(/\\/g, '/').toLowerCase();
    const relevant = state.diagnostics.filter((d) => d.file && d.file.replace(/\\/g, '/').toLowerCase() === modelPath);
    const markers = relevant.map((d) => {
      const line = Math.min(Math.max(1, d.line || 1), model.getLineCount());
      const maxColumn = model.getLineMaxColumn(line);
      const column = Math.min(Math.max(1, d.column || 1), maxColumn);
      const length = d.tokenLength || 0;
      const endColumn = length ? Math.min(column + length, maxColumn) : maxColumn;
      return {
        severity: d.severity === 'warning' ? monaco.MarkerSeverity.Warning : monaco.MarkerSeverity.Error,
        message: d.hint ? `${d.message}\n\n${d.hint}` : d.message,
        code: d.code || undefined,
        source: 'gbx',
        startLineNumber: line,
        startColumn: column,
        endLineNumber: line,
        endColumn
      };
    });
    monaco.editor.setModelMarkers(model, 'gbx', markers);
  }
}

/* ------------------------------------------------------------------ */
/* Debugger                                                            */
/* ------------------------------------------------------------------ */

const BREAKPOINT_KEY = 'gbxstudio.breakpoints.v2';
// Projektpfad (relativ) → (Zeile → Bedingung). Leere Bedingung heisst: immer anhalten.
let breakpointMap = new Map();
let debugSource = null;
const breakpointDecorations = new WeakMap();
const debugLineDecorations = new WeakMap();

const normalizeRel = (rel) => String(rel || '').replace(/\\/g, '/');

function emptyDebugState() {
  return {
    jobId: '',
    active: false,
    paused: false,
    reason: '',
    file: '',
    line: 0,
    func: '',
    scope: null,
    error: '',
    condition: '',
    conditionError: ''
  };
}

function projectRelative(file) {
  return normalizeRel(relativeToProject(file));
}

function readBreakpointStore() {
  const all = {};
  // v1 enthielt nur Zeilennummern; wer die vorherige Fassung benutzt hat,
  // soll seine Haltepunkte behalten.
  for (const key of [BREAKPOINT_KEY, 'gbxstudio.breakpoints.v1']) {
    try {
      const data = JSON.parse(localStorage.getItem(key) || '{}') || {};
      for (const [dir, entry] of Object.entries(data)) {
        if (!all[dir]) all[dir] = entry;
      }
    } catch {
      /* defekter Eintrag wird übersprungen */
    }
  }
  return all;
}

function loadBreakpoints() {
  breakpointMap = new Map();
  if (!state.projectDir) return;
  const saved = readBreakpointStore()[state.projectDir] || {};
  for (const [rel, entry] of Object.entries(saved)) {
    const lines = new Map();
    if (Array.isArray(entry)) {
      // Früheres Format: nur Zeilennummern.
      for (const value of entry) {
        const line = Number(value);
        if (Number.isInteger(line) && line > 0) lines.set(line, '');
      }
    } else if (entry && typeof entry === 'object') {
      for (const [key, condition] of Object.entries(entry)) {
        const line = Number(key);
        if (Number.isInteger(line) && line > 0) lines.set(line, String(condition || ''));
      }
    }
    if (lines.size) breakpointMap.set(normalizeRel(rel), lines);
  }
}

function saveBreakpoints() {
  if (!state.projectDir) return;
  let all = {};
  try {
    all = JSON.parse(localStorage.getItem(BREAKPOINT_KEY) || '{}') || {};
  } catch {
    all = {};
  }
  const entry = {};
  for (const [rel, lines] of breakpointMap) {
    if (!lines.size) continue;
    const saved = {};
    for (const [line, condition] of [...lines].sort((a, b) => a[0] - b[0])) saved[line] = condition || '';
    entry[rel] = saved;
  }
  if (Object.keys(entry).length) all[state.projectDir] = entry;
  else delete all[state.projectDir];
  try {
    localStorage.setItem(BREAKPOINT_KEY, JSON.stringify(all));
  } catch {
    /* Speicher nicht verfügbar */
  }
}

function allBreakpoints() {
  const list = [];
  for (const [rel, lines] of breakpointMap) {
    for (const [line, condition] of lines) list.push({ file: rel, line, condition });
  }
  return list;
}

function breakpointLines(rel) {
  return breakpointMap.get(normalizeRel(rel)) || new Map();
}

function breakpointCondition(rel, line) {
  return breakpointLines(rel).get(line) || '';
}

/** Zeichnet die Haltepunkte in allen offenen Modellen. */
function paintBreakpoints() {
  if (!monaco) return;
  for (const model of monaco.editor.getModels()) {
    if (model.getLanguageId() !== 'gbx') continue;
    const rel = projectRelative(model.uri.fsPath);
    const lines = rel ? breakpointLines(rel) : new Map();
    const decorations = [...lines]
      .filter(([line]) => line <= model.getLineCount())
      .map(([line, condition]) => ({
        range: new monaco.Range(line, 1, line, 1),
        options: {
          glyphMarginClassName: condition ? 'gbx-bp gbx-bp-cond' : 'gbx-bp',
          linesDecorationsClassName: 'gbx-bp-line',
          hoverMessage: {
            value: condition ? t('Haltepunkt – nur wenn {condition}', { condition }) : t('Haltepunkt')
          },
          stickiness: monaco.editor.TrackedRangeStickiness.NeverGrowsWhenTypingAtEdges
        }
      }));
    const previous = breakpointDecorations.get(model) || [];
    breakpointDecorations.set(model, model.deltaDecorations(previous, decorations));
  }
}

/** Markiert die Zeile, in der der Debugger gerade steht. */
function paintDebugLine() {
  if (!monaco) return;
  const d = state.debug;
  for (const model of monaco.editor.getModels()) {
    if (model.getLanguageId() !== 'gbx') continue;
    const rel = projectRelative(model.uri.fsPath);
    const show = d.active && d.paused && d.file && rel === normalizeRel(d.file) && d.line >= 1 && d.line <= model.getLineCount();
    const decorations = show
      ? [
          {
            range: new monaco.Range(d.line, 1, d.line, 1),
            options: {
              isWholeLine: true,
              className: 'gbx-debug-line',
              glyphMarginClassName: 'gbx-debug-arrow'
            }
          }
        ]
      : [];
    const previous = debugLineDecorations.get(model) || [];
    debugLineDecorations.set(model, model.deltaDecorations(previous, decorations));
  }
}

function toggleBreakpointAt(model, line) {
  if (!model || !line) return;
  const rel = projectRelative(model.uri.fsPath);
  if (!rel) return toast(t('Diese Datei liegt außerhalb des Projekts.'), 'warn');
  const lines = breakpointLines(rel);
  const on = !lines.has(line);
  if (on) lines.set(line, '');
  else lines.delete(line);
  if (lines.size) breakpointMap.set(rel, lines);
  else breakpointMap.delete(rel);
  saveBreakpoints();
  paintBreakpoints();
  renderBreakpoints();
  if (state.debug.active) debugCommand(on ? 'bp' : 'bpc', { file: rel, line, condition: '' });
}

/**
 * Fragt die Bedingung eines Haltepunkts ab. Der Text ist ein normaler
 * GBX-Ausdruck, der im aktuellen Bereich ausgewertet wird; leer heisst
 * „immer anhalten“.
 */
function breakpointConditionDialog(rel, line) {
  if (!state.projectDir) return;
  const existing = breakpointCondition(rel, line);
  const body = document.createElement('div');
  body.innerHTML = `
    <div class="field">
      <label>${t('Bedingung (leer = immer anhalten)')}</label>
      <input type="text" id="bp-condition" value="${escapeAttr(existing)}" placeholder="z. B. I >= 3 AND LEBEN = 0" spellcheck="false">
    </div>
    <p class="hint">${t('Der Haltepunkt greift nur, wenn dieser Ausdruck wahr ist. Er wird im aktuellen Bereich ausgewertet.')}</p>
  `;
  const apply = () => {
    const value = body.querySelector('#bp-condition').value.trim();
    const lines = breakpointLines(rel);
    if (!lines.has(line)) return true;
    lines.set(line, value);
    breakpointMap.set(rel, lines);
    saveBreakpoints();
    paintBreakpoints();
    renderBreakpoints();
    if (state.debug.active) debugCommand('bp', { file: rel, line, condition: value });
    return true;
  };
  const modal = openModal({
    title: t('Haltepunkt {file}:{line}', { file: rel.split('/').pop(), line }),
    body,
    footer: [
      { label: t('Abbrechen'), action: () => true },
      { label: t('Übernehmen'), variant: 'primary', action: apply }
    ]
  });
  const input = body.querySelector('#bp-condition');
  input.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      event.preventDefault();
      if (apply() !== false) modal.close();
    }
  });
  setTimeout(() => {
    input.focus();
    input.select();
  }, 0);
}

function debugHint(text) {
  const el = document.createElement('div');
  el.className = 'empty-hint';
  el.textContent = text;
  return el;
}

function debugVariableRow(variable) {
  const row = document.createElement('div');
  row.className = 'debug-var';
  row.title = `${variable.name}${variable.type ? `: ${variable.type}` : ''}`;
  const name = document.createElement('span');
  name.className = 'name';
  name.textContent = variable.name;
  const value = document.createElement('span');
  value.className = 'value';
  value.textContent = variable.value === '' ? '—' : variable.value;
  const type = document.createElement('span');
  type.className = 'type';
  type.textContent = variable.type || '';
  row.append(name, value, type);
  return row;
}

function renderDebugScope() {
  const localsHost = $('debug-locals');
  const globalsHost = $('debug-globals');
  if (!localsHost || !globalsHost) return;
  const scope = state.debug.scope;
  const locals = scope?.locals || [];
  const globals = scope?.globals || [];
  $('debug-func').textContent = scope ? scope.func || t('Hauptprogramm') : '';
  localsHost.replaceChildren();
  if (!locals.length) localsHost.append(debugHint(t('Keine lokalen Variablen.')));
  for (const variable of locals) localsHost.append(debugVariableRow(variable));
  globalsHost.replaceChildren();
  if (!globals.length) globalsHost.append(debugHint(t('Noch keine Werte – starte den Debugger mit F6.')));
  for (const variable of globals) globalsHost.append(debugVariableRow(variable));
  $('debug-global-count').textContent = scope ? String(globals.length) : '';

  const stackHost = $('debug-stack');
  if (!stackHost) return;
  stackHost.replaceChildren();
  if (!state.debug.active) {
    stackHost.append(debugHint(t('Keine Sitzung.')));
    return;
  }
  // Der Interpreter liefert die Aufrufe von außen nach innen; das Hauptprogramm
  // steht nicht in der Liste und kommt an den Anfang.
  const frames = ['', ...(scope?.stack || [])].reverse();
  frames.forEach((name, index) => {
    const row = document.createElement('div');
    row.className = 'debug-frame' + (index === 0 ? ' active' : '');
    row.textContent = name || t('Hauptprogramm');
    stackHost.append(row);
  });
}

function renderBreakpoints() {
  const host = $('debug-breakpoints');
  if (!host) return;
  host.replaceChildren();
  const items = [];
  for (const [rel, lines] of [...breakpointMap].sort((a, b) => a[0].localeCompare(b[0]))) {
    for (const [line, condition] of [...lines].sort((a, b) => a[0] - b[0])) items.push({ rel, line, condition });
  }
  dock?.setMeta('debug', items.length ? String(items.length) : '');
  if (!items.length) {
    host.append(debugHint(t('Klicke links neben eine Zeile, um einen Haltepunkt zu setzen.')));
    return;
  }
  for (const item of items) {
    const row = document.createElement('div');
    row.className = 'debug-bp';
    const dot = document.createElement('span');
    dot.className = item.condition ? 'dot cond' : 'dot';
    const label = document.createElement('button');
    label.className = 'link';
    label.textContent = `${item.rel.split('/').pop()}:${item.line}`;
    label.title = `${item.rel}:${item.line}${item.condition ? `\n${t('Bedingung: {condition}', { condition: item.condition })}` : ''}`;
    label.addEventListener('click', () => {
      openFile(item.rel, { line: item.line }).catch((error) => toast(error.message, 'error'));
    });
    row.append(dot, label);
    if (item.condition) {
      const condition = document.createElement('span');
      condition.className = 'cond';
      condition.textContent = item.condition;
      condition.title = t('Bedingung: {condition}', { condition: item.condition });
      row.append(condition);
    }
    const edit = document.createElement('button');
    edit.className = 'icon-btn';
    edit.textContent = '✎';
    edit.title = t('Bedingung bearbeiten');
    edit.addEventListener('click', () => breakpointConditionDialog(item.rel, item.line));
    const remove = document.createElement('button');
    remove.className = 'icon-btn';
    remove.textContent = '✕';
    remove.title = t('Haltepunkt entfernen');
    remove.addEventListener('click', () => {
      const lines = breakpointLines(item.rel);
      lines.delete(item.line);
      if (lines.size) breakpointMap.set(item.rel, lines);
      else breakpointMap.delete(item.rel);
      saveBreakpoints();
      paintBreakpoints();
      renderBreakpoints();
      if (state.debug.active) debugCommand('bpc', { file: item.rel, line: item.line });
    });
    row.append(edit, remove);
    host.append(row);
  }
}

function updateDebugUi() {
  const d = state.debug;
  const bar = $('debug-bar');
  if (!bar) return;
  bar.hidden = !d.active;
  const paused = d.active && d.paused;
  for (const id of ['btn-debug-continue', 'btn-debug-over', 'btn-debug-into', 'btn-debug-out']) {
    const button = $(id);
    if (button) button.disabled = !paused;
  }
  const pauseButton = $('btn-debug-pause');
  if (pauseButton) pauseButton.disabled = !d.active || paused;
  $('debug-state').textContent = !d.active
    ? ''
    : d.reason === 'error'
      ? t('Fehler')
      : paused
        ? t('Angehalten')
        : t('Läuft …');
  $('debug-where').textContent = d.active && d.line
    ? `${d.file || '?'}:${d.line}${d.func ? ` · ${d.func}` : ''}${d.condition ? ` · ${t('Bedingung: {condition}', { condition: d.condition })}` : ''}`
    : '';
  const panelState = $('debug-panel-state');
  if (panelState) {
    panelState.textContent = !d.active
      ? t('Keine Debug-Sitzung.')
      : d.reason === 'error'
        ? t('Angehalten nach Fehler in Zeile {line}.', { line: d.line })
        : d.conditionError
          ? t('Bedingung fehlerhaft: {error}', { error: d.conditionError })
          : paused
            ? t('Angehalten in {file}:{line}{func}', { file: d.file || '?', line: d.line, func: d.func ? ` · ${d.func}` : '' })
            : t('Programm läuft – Haltepunkte werden beachtet.');
  }
  const start = $('btn-debug-start-panel');
  if (start) start.hidden = d.active;
  updateButtons();
}

async function startDebug() {
  if (!state.project || state.building || state.debug.active) return;
  const saved = await saveAll();
  if (!saved) return toast(t('Speichern fehlgeschlagen'), 'error');
  showPanel('debug');
  $('console').textContent = '';
  clearProblems();
  setBuildStatus(t('Debugger startet …'));
  try {
    const result = await post('/api/debug/start', {
      projectDir: state.projectDir,
      project: state.project,
      breakpoints: allBreakpoints()
    });
    state.debug = { ...emptyDebugState(), jobId: result.jobId, active: true };
    debugSource = subscribeJob(result.jobId, handleDebugEvent, finishDebug);
    renderDebugScope();
    updateDebugUi();
    appendTo($('console'), t('Debugger gestartet. F8 läuft weiter, F10 geht zeilenweise.') + '\n', 'muted');
  } catch (error) {
    setBuildStatus(t('Debugger konnte nicht starten'), 'error');
    toast(error.message, 'error');
  }
}

function handleDebugEvent(event) {
  if (event.type === 'log') {
    appendTo($('console'), event.text, event.level);
    return;
  }
  if (event.type !== 'debug') return;
  const data = event.event || {};
  if (data.t === 'stop') onDebugStop(data);
  else if (data.t === 'scope') {
    state.debug.scope = data.scope;
    renderDebugScope();
  } else if (data.t === 'error') onDebugError(data);
  else if (data.t === 'note') {
    appendTo($('console'), `${data.text || ''}\n`, data.level === 'error' ? 'error' : 'warning');
  }
}

function onDebugStop(data) {
  const rel = projectRelative(data.file);
  state.debug.paused = true;
  state.debug.reason = data.reason || 'breakpoint';
  state.debug.file = rel || normalizeRel(data.file);
  state.debug.line = data.line;
  state.debug.func = data.scope?.func || '';
  state.debug.scope = data.scope || null;
  state.debug.error = '';
  state.debug.condition = data.condition || '';
  state.debug.conditionError = data.conditionError || '';
  if (data.conditionError) {
    appendTo($('console'), `${t('Bedingung des Haltepunkts ist fehlerhaft: {error}', { error: data.conditionError })}\n`, 'warning');
  }
  setBuildStatus(
    data.reason === 'breakpoint'
      ? t('Haltepunkt in Zeile {line}', { line: data.line })
      : t('Angehalten in Zeile {line}', { line: data.line })
  );
  const reveal = rel ? openFile(rel, { line: data.line, focus: false }).catch(() => null) : Promise.resolve(null);
  reveal.then(() => {
    paintDebugLine();
    paintBreakpoints();
  });
  renderDebugScope();
  updateDebugUi();
}

function onDebugError(data) {
  state.debug.paused = true;
  state.debug.reason = 'error';
  state.debug.error = data.message || '';
  state.debug.line = data.line || 0;
  state.debug.file = projectRelative(data.file) || normalizeRel(data.file);
  state.debug.func = data.scope?.func || '';
  if (data.scope) state.debug.scope = data.scope;
  addDiagnostic({
    file: data.file,
    line: data.line || 1,
    column: 1,
    severity: 'error',
    message: data.compile ? data.message : t('Laufzeitfehler: {message}', { message: data.message }),
    code: data.compile ? 'GBX0001' : '',
    source: 'gbx'
  });
  paintDebugLine();
  renderDebugScope();
  updateDebugUi();
  showPanel('problems');
  setBuildStatus(t('Fehler in Zeile {line}', { line: data.line || '?' }), 'error');
}

function finishDebug(done) {
  if (debugSource) debugSource.close();
  debugSource = null;
  const failed = state.debug.reason === 'error';
  const code = done?.code ?? 0;
  state.debug = emptyDebugState();
  paintDebugLine();
  renderDebugScope();
  updateDebugUi();
  if (!failed) {
    if (done?.status === 'stopped') setBuildStatus(t('Debug-Sitzung beendet'), 'ok');
    else setBuildStatus(t('Debug-Sitzung beendet (Code {code})', { code }), code ? 'error' : 'ok');
  }
}

const DEBUG_RESUME_COMMANDS = new Set(['run', 'continue', 'step', 'next', 'out']);

function debugCommand(command, extra = {}) {
  if (!state.debug.active || !state.debug.jobId) return;
  if (DEBUG_RESUME_COMMANDS.has(command)) {
    if (!state.debug.paused) return;
    // Sofort als laufend zeigen; der naechste Halt kommt als Ereignis.
    state.debug.paused = false;
    state.debug.reason = '';
    paintDebugLine();
    updateDebugUi();
  }
  post('/api/debug/command', { jobId: state.debug.jobId, command, ...extra }).catch((error) => {
    toast(error.message, 'error');
  });
}

async function stopDebug() {
  if (!state.debug.active) return;
  const jobId = state.debug.jobId;
  try {
    await post('/api/stop', { jobId });
  } catch (error) {
    toast(error.message, 'error');
  }
}

/* ------------------------------------------------------------------ */
/* Starten                                                             */
/* ------------------------------------------------------------------ */

async function runProgram() {
  if (!state.project || state.running || state.building) return;
  const saved = await saveAll();
  if (!saved) return toast(t('Speichern fehlgeschlagen'), 'error');
  showPanel('console');
  $('console').textContent = '';
  const entry = state.project.entry || 'main.gbx';
  appendTo($('console'), t('Starte {path}', { path: entry }) + '\n\n', 'muted');
  try {
    const result = await post('/api/run', { projectDir: state.projectDir, project: state.project });
    state.runJobId = result.jobId;
    state.running = true;
    updateButtons();
    setBuildStatus(t('Programm läuft …'));
    subscribeJob(result.jobId, (event) => {
      if (event.type === 'log') appendTo($('console'), event.text, event.level);
      else if (event.type === 'started') setBuildStatus(t('Programm läuft (PID {pid})', { pid: event.pid ?? '?' }));
    }, (done) => {
      state.running = false;
      state.runJobId = '';
      updateButtons();
      setBuildStatus(t('Programm beendet (Code {code})', { code: done.code ?? 0 }), done.code ? 'error' : 'ok');
      if (done.error) appendTo($('console'), done.error + '\n', 'error');
    });
  } catch (error) {
    state.running = false;
    updateButtons();
    toast(error.message, 'error');
  }
}

async function stopProgram() {
  if (!state.runJobId) return;
  try {
    await post('/api/stop', { jobId: state.runJobId });
  } catch (error) {
    toast(error.message, 'error');
  }
}

/* ------------------------------------------------------------------ */
/* Bauen: Zielplattform aus dem Dropdown neben dem Knopf               */
/* ------------------------------------------------------------------ */

/** Baut das Projekt für die gewählte Plattform (Strg+E). */
function buildForTarget() {
  if (state.buildTarget === 'html5') return exportHtml5Game();
  return exportGame();
}

// Quellen und INLINE-Bibliothek landen in der EXE, Assets kommen daneben.
async function exportGame() {
  if (!state.project || state.building || state.running) return;
  const saved = await saveAll();
  if (!saved) return toast(t('Speichern fehlgeschlagen'), 'error');

  const name = String(state.project.name || 'spiel');
  let target = '';
  if (desktop?.saveFile) {
    target = await desktop.saveFile({
      title: t('Spiel als EXE speichern'),
      defaultPath: `${state.projectDir}/${name}.exe`,
      filters: [
        { name: t('Windows-EXE'), extensions: ['exe'] },
        { name: t('Alle Dateien'), extensions: ['*'] }
      ]
    });
    if (!target) return;
  }

  state.building = true;
  updateButtons();
  showPanel('output');
  $('output').textContent = '';
  setBuildStatus(t('Erstelle EXE …'));
  appendOutput(t('Erstelle {name} …', { name }) + '\n', 'title');

  try {
    const result = await post('/api/export', {
      projectDir: state.projectDir,
      project: state.project,
      target
    });
    state.buildJobId = result.jobId;
    subscribeJob(
      result.jobId,
      (event) => {
        if (event.type === 'log') appendOutput(event.text + '\n', event.level || 'info');
      },
      (done) => {
        state.building = false;
        state.buildJobId = '';
        updateButtons();
        const file = String(done.file || '');
        const label = file.split(/[\\/]/).pop() || name;
        if (done.ok) {
          setBuildStatus(t('EXE erstellt: {name}', { name: label }), 'ok');
          appendOutput(`\n${t('EXE erstellt: {name}', { name: label })}\n${file}\n`, 'success');
          toast(t('EXE erstellt: {name}', { name: label }), 'ok');
        } else {
          setBuildStatus(t('Export fehlgeschlagen'), 'error');
          appendOutput(`\n${t('Der Export ist fehlgeschlagen.')}\n`, 'error');
          toast(t('Der Export ist fehlgeschlagen. Details unter „Ausgabe“.'), 'error');
        }
      }
    );
  } catch (error) {
    state.building = false;
    updateButtons();
    setBuildStatus(t('Export fehlgeschlagen'), 'error');
    toast(error.message, 'error');
  }
}

/* Spiel für den Browser (HTML5)                                       */
/* ------------------------------------------------------------------ */

/** Den wasm-Player einmalig bauen; Ausgabe landet im Panel „Ausgabe“. */
async function ensurePlayer() {
  if (state.html5?.playerReady) return true;
  if (!state.emsdk?.ok) {
    toast(t('Für HTML5 zuerst emsdk in den Einstellungen einrichten.'), 'warn');
    return false;
  }
  setBuildStatus(t('Baue den wasm-Player …'));
  appendOutput(t('Übersetze den Interpreter nach WebAssembly …') + '\n', 'title');
  const result = await post('/api/html5/build');
  return new Promise((resolve) => {
    subscribeJob(
      result.jobId,
      (event) => {
        if (event.type === 'log') appendOutput(event.text + '\n', event.level || 'info');
      },
      (done) => {
        state.html5 = done.html5 || state.html5;
        resolve(Boolean(done.ok));
      }
    );
  });
}

async function exportHtml5Game() {
  if (!state.project || state.building || state.running) return;
  const saved = await saveAll();
  if (!saved) return toast(t('Speichern fehlgeschlagen'), 'error');
  if (!state.emsdk?.ok) return toast(t('Für HTML5 zuerst emsdk in den Einstellungen einrichten.'), 'warn');

  state.building = true;
  updateButtons();
  showPanel('output');
  $('output').textContent = '';
  try {
    if (!(await ensurePlayer())) {
      state.building = false;
      updateButtons();
      setBuildStatus(t('Der wasm-Build ist fehlgeschlagen.'), 'error');
      return;
    }
    const name = String(state.project.name || 'spiel');
    setBuildStatus(t('Baue HTML5 …'));
    appendOutput(t('Baue {name} für den Browser …', { name }) + '\n', 'title');
    const result = await post('/api/html5/export', {
      projectDir: state.projectDir,
      project: state.project
    });
    subscribeJob(
      result.jobId,
      (event) => {
        if (event.type === 'log') appendOutput(event.text + '\n', event.level || 'info');
      },
      (done) => {
        state.building = false;
        updateButtons();
        if (done.ok) {
          const dir = String(done.dir || '');
          const label = dir.split(/[\\/]/).pop() || name;
          setBuildStatus(t('HTML5-Build fertig: {name}', { name: label }), 'ok');
          appendOutput(`\n${t('HTML5-Build fertig: {name}', { name: label })}\n${done.file || dir}\n`, 'success');
          toast(t('HTML5-Build fertig: {name}', { name: label }), 'ok');
          openInExplorer(dir);
        } else {
          setBuildStatus(t('HTML5-Build fehlgeschlagen'), 'error');
          appendOutput(`\n${t('Der HTML5-Build ist fehlgeschlagen.')}\n`, 'error');
          toast(t('Der HTML5-Build ist fehlgeschlagen. Details unter „Ausgabe“.'), 'error');
        }
      }
    );
  } catch (error) {
    state.building = false;
    updateButtons();
    setBuildStatus(t('HTML5-Build fehlgeschlagen'), 'error');
    toast(error.message, 'error');
  }
}

/* ------------------------------------------------------------------ */
/* Referenz                                                            */
/* ------------------------------------------------------------------ */

function fillReferenceFilters() {
  const select = $('ref-category');
  const previous = select.value;
  while (select.options.length > 1) select.remove(1);
  for (const category of state.dataset.categories || []) {
    const option = document.createElement('option');
    option.value = category;
    option.textContent = t(category);
    select.append(option);
  }
  select.value = previous;
}

function renderReferenceList() {
  const host = $('ref-list');
  if (!host) return;
  const q = state.ref.q.trim().toLowerCase();
  const category = state.ref.category;
  const kind = state.ref.kind;
  let items = state.commands.filter((command) => {
    if (category && command.category !== category) return false;
    if (kind && command.kind !== kind) return false;
    if (!q) return true;
    const haystack = `${command.name} ${command.displayName || ''} ${command.category || ''} ${command.signature || ''} ${command.description?.de || ''} ${command.description?.en || ''}`.toLowerCase();
    return haystack.includes(q);
  });
  items.sort((a, b) => {
    const an = a.name.toUpperCase();
    const bn = b.name.toUpperCase();
    if (q) {
      const as = an.startsWith(q.toUpperCase()) ? 0 : 1;
      const bs = bn.startsWith(q.toUpperCase()) ? 0 : 1;
      if (as !== bs) return as - bs;
    }
    return an.localeCompare(bn);
  });
  dock?.setMeta('reference', `${items.length} / ${state.commands.length}`);
  host.innerHTML = '';
  const fragment = document.createDocumentFragment();
  for (const command of items.slice(0, 600)) {
    const row = document.createElement('div');
    row.className = 'ref-item' + (state.ref.selected?.name === command.name ? ' active' : '');
    row.title = command.signature || command.name;
    const dot = document.createElement('span');
    dot.className = 'kind ' + command.kind;
    const name = document.createElement('span');
    name.className = 'nm';
    name.textContent = command.name;
    const cat = document.createElement('span');
    cat.className = 'cat';
    cat.textContent = command.category ? t(command.category) : '';
    row.append(dot, name, cat);
    row.addEventListener('click', () => {
      state.ref.selected = command;
      renderReferenceDetail();
      host.querySelectorAll('.ref-item.active').forEach((el) => el.classList.remove('active'));
      row.classList.add('active');
    });
    row.addEventListener('dblclick', () => insertReference(command));
    fragment.append(row);
  }
  if (!items.length) {
    const hint = document.createElement('div');
    hint.className = 'empty-hint';
    hint.textContent = t('Keine Treffer.');
    fragment.append(hint);
  }
  host.append(fragment);
  if (state.ref.selected) renderReferenceDetail();
}

function renderReferenceDetail() {
  const host = $('ref-detail');
  const command = state.ref.selected;
  if (!command) {
    host.innerHTML = `<div class="empty-hint">${t('Wähle links einen Eintrag. Der Datensatz enthält alle Befehle, Schlüsselwörter und Konstanten von GameBasiX.')}</div>`;
    return;
  }
  const lang = state.ref.lang;
  const description =
    command.description?.[lang] || command.description?.en || command.description?.de || t('Keine Beschreibung im Datensatz.');
  host.innerHTML = '';
  const title = document.createElement('h2');
  title.textContent = command.name;
  const meta = document.createElement('div');
  meta.className = 'meta';
  const kindBadge = document.createElement('span');
  kindBadge.className = 'badge ' + command.kind;
  kindBadge.textContent = kindLabel(command.kind);
  const catBadge = document.createElement('span');
  catBadge.className = 'badge';
  catBadge.textContent = command.category ? t(command.category) : t('Allgemein');
  meta.append(kindBadge, catBadge);
  if (command.optional) {
    const opt = document.createElement('span');
    opt.className = 'badge';
    opt.textContent = t('Optional');
    meta.append(opt);
  }
  const toggle = document.createElement('div');
  toggle.className = 'lang-toggle';
  for (const code of ['de', 'en']) {
    const button = document.createElement('button');
    button.textContent = code.toUpperCase();
    button.className = lang === code ? 'active' : '';
    button.addEventListener('click', () => {
      state.ref.lang = code;
      renderReferenceDetail();
    });
    toggle.append(button);
  }
  meta.append(toggle);

  const sig = document.createElement('div');
  sig.className = 'ref-sig';
  const syntax = (lang === 'en' ? command.syntaxEn : command.syntax) || command.syntax || [];
  sig.textContent = syntax.length
    ? syntax.join('\n\n')
    : (lang === 'en' ? command.signatureEn : command.signature) || command.name;

  const desc = document.createElement('div');
  desc.className = 'ref-desc';
  desc.textContent = description;

  host.append(title, meta, sig, desc);
  if (command.sample) {
    const sampleTitle = document.createElement('div');
    sampleTitle.className = 'hint';
    sampleTitle.style.marginTop = '12px';
    sampleTitle.textContent = t('Beispiel');
    const sample = document.createElement('pre');
    sample.className = 'ref-sample';
    sample.textContent = command.sample;
    host.append(sampleTitle, sample);
  }
  const actions = document.createElement('div');
  actions.className = 'ref-actions';
  const insert = document.createElement('button');
  insert.className = 'primary';
  insert.textContent = t('Einfügen');
  insert.addEventListener('click', () => insertReference(command));
  const copy = document.createElement('button');
  copy.textContent = t('Kopieren');
  copy.addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(command.syntax?.[0] || command.name);
      toast(t('In die Zwischenablage kopiert.'), 'ok');
    } catch {
      toast(t('Kopieren fehlgeschlagen.'), 'error');
    }
  });
  actions.append(insert, copy);
  host.append(actions);
}

function kindLabel(kind) {
  return (
    {
      command: t('Befehl'),
      keyword: t('Schlüsselwort'),
      operator: t('Operator'),
      constant: t('Konstante'),
      preprocessor: t('Präprozessor'),
      optional: t('Optional'),
      type: t('Datentyp'),
      overview: t('Übersicht')
    }[kind] || kind
  );
}

function insertReference(command) {
  if (!editor.getModel()) {
    toast(t('Bitte zuerst eine Datei öffnen.'), 'warn');
    return;
  }
  const selection = editor.getSelection();
  const text = command.name;
  editor.executeEdits('gbx-reference', [{ range: selection, text, forceMoveMarkers: true }]);
  editor.focus();
}

function referenceVisible() {
  if (!dock) return false;
  const zone = dock.zoneOf('reference');
  const layout = dock.layout;
  return !!zone && layout.open[zone] && !layout.collapsed.reference;
}

function toggleReference(force) {
  if (!dock) return;
  const show = force === undefined ? !referenceVisible() : force;
  // Ein- und Ausblenden gilt dem Panel, wo es auch immer angedockt ist.
  if (show) dock.activate('reference');
  else dock.toggle('reference', true);
  if (show) {
    $('ref-search')?.focus();
    renderReferenceList();
  }
}

/* ------------------------------------------------------------------ */
/* Panel                                                               */
/* ------------------------------------------------------------------ */

function showPanel(name) {
  state.panel = name;
  dock?.activate(name);
}

function togglePanel() {
  if (!dock) return;
  dock.setZoneOpen('bottom', !dock.layout.open.bottom);
}

/* ------------------------------------------------------------------ */
/* Symbole / Analyse                                                   */
/* ------------------------------------------------------------------ */

function analysisFor(model) {
  const key = model.uri.toString();
  const cached = state.analysisCache.get(key);
  if (cached && cached.version === model.getVersionId()) return cached.data;
  const data = analyzeSource(model.getValue());
  state.analysisCache.set(key, { version: model.getVersionId(), data });
  return data;
}

function getSymbols() {
  const out = { functions: [], types: [], variables: [], constants: [] };
  const seen = { functions: new Set(), types: new Set(), variables: new Set(), constants: new Set() };
  const push = (bucket, item) => {
    const key = item.name.toUpperCase();
    if (seen[bucket].has(key)) return;
    seen[bucket].add(key);
    out[bucket].push(item);
  };
  // Die geöffnete Datei ist maßgeblich: Ihr Stand ist neuer als der zuletzt
  // gesammelte Projektindex (sonst fehlen gerade getippte METHODen).
  const tab = activeTab();
  if (tab) {
    const current = analysisFor(tab.model);
    for (const bucket of ['functions', 'types', 'variables', 'constants']) {
      for (const item of current[bucket] || []) push(bucket, item);
    }
  }
  for (const bucket of ['functions', 'types', 'constants']) {
    for (const item of state.projectSymbols[bucket] || []) push(bucket, item);
  }
  for (const variable of state.projectSymbols.variables || []) {
    if (variable.scope === 'GLOBAL') push('variables', variable);
  }
  return out;
}

async function indexProjectSymbols() {
  if (!state.tree.length) {
    refreshTypeColors();
    return;
  }
  const files = state.tree.filter((item) => item.type === 'file' && /\.gbx$/i.test(item.name)).slice(0, 80);
  const aggregate = { functions: [], types: [], variables: [], constants: [] };
  const seen = { functions: new Set(), types: new Set(), variables: new Set(), constants: new Set() };
  for (const file of files) {
    const open = state.tabs.find((t) => t.path === file.path);
    let data;
    if (open) {
      data = analysisFor(open.model);
    } else {
      try {
        const content = await get(`/api/file?dir=${encodeURIComponent(state.projectDir)}&file=${encodeURIComponent(file.path)}`);
        data = analyzeSource(content.content);
      } catch {
        continue;
      }
    }
    for (const bucket of ['functions', 'types', 'variables', 'constants']) {
      for (const item of data[bucket] || []) {
        const key = item.name.toUpperCase();
        if (seen[bucket].has(key)) continue;
        seen[bucket].add(key);
        aggregate[bucket].push(item);
      }
    }
  }
  state.projectSymbols = aggregate;
  refreshTypeColors();
}

/* ------------------------------------------------------------------ */
/* Outline                                                             */
/* ------------------------------------------------------------------ */

const OUTLINE_KEY = 'gbxstudio.outline.v1';
const outlineState = { collapsed: new Set(), rows: [], timer: null, activeEl: null };
const OUTLINE_KINDS = { globals: 'VAR', functions: 'FN', subs: 'SUB', types: 'TYP', constants: 'CON', labels: 'LBL' };

function readOutlinePrefs() {
  try {
    const saved = JSON.parse(localStorage.getItem(OUTLINE_KEY) || '{}');
    if (Array.isArray(saved.collapsed)) outlineState.collapsed = new Set(saved.collapsed);
  } catch {
    /* Speicher nicht verfügbar */
  }
}

function persistOutlinePrefs() {
  try {
    localStorage.setItem(OUTLINE_KEY, JSON.stringify({ collapsed: [...outlineState.collapsed] }));
  } catch {
    /* Speicher nicht verfügbar */
  }
}

/**
 * Sammelt die Symbole der aktiven Datei, gruppiert nach Art. Die Reihenfolge
 * der Gruppen entspricht dem Aufbau eines GBX-Programms.
 */
function outlineGroups(model) {
  const analysis = analysisFor(model);
  const totalLines = model.getLineCount();
  const blocks = [...analysis.functions, ...analysis.types].map((entry) => entry.line).sort((a, b) => a - b);
  const endOf = (line) => {
    const next = blocks.find((block) => block > line);
    return next ? next - 1 : totalLines;
  };
  const groups = [];
  const add = (id, label, rows) => {
    if (rows.length) groups.push({ id, label, rows: rows.sort((a, b) => a.line - b.line) });
  };
  const signature = (fn) => (fn.params.length ? `(${fn.params.map((param) => param.name).join(', ')})` : '()');
  const functionRow = (fn) => ({ name: fn.name, detail: signature(fn), line: fn.line, endLine: endOf(fn.line) });

  add(
    'globals',
    t('Globals'),
    analysis.variables
      .filter((variable) => ['GLOBAL', 'DIM', 'STATIC'].includes(String(variable.scope).toUpperCase()))
      .map((variable) => ({
        name: variable.name,
        detail: `${variable.type || 'NUMERIC'}${variable.array ? '[]' : ''}`,
        line: variable.line
      }))
  );
  add('functions', t('Functions'), analysis.functions.filter((fn) => fn.kind === 'FUNCTION').map(functionRow));
  add('subs', t('Subs'), analysis.functions.filter((fn) => fn.kind === 'SUB').map(functionRow));
  add(
    'types',
    t('Types'),
    analysis.types.map((type) => ({
      name: type.name,
      detail: type.methods?.length
        ? t('{n} Felder · {m} Methoden', { n: type.members.length, m: type.methods.length })
        : t('{n} Felder', { n: type.members.length }),
      line: type.line,
      endLine: endOf(type.line),
      members: [
        ...type.members.map((member) => ({
          name: member.name,
          detail: member.type,
          line: member.line || type.line
        })),
        ...(type.methods || []).map((method) => ({
          name: method.name,
          detail: `${method.kind}(${(method.params || []).map((param) => param.name).join(', ')})`,
          line: method.line || type.line
        }))
      ]
    }))
  );
  add(
    'constants',
    t('Constants'),
    analysis.constants.map((constant) => ({ name: constant.name, detail: constant.type, line: constant.line }))
  );
  add(
    'labels',
    t('Labels'),
    (analysis.labelDefs || []).map((label) => ({ name: `${label.name}:`, detail: '', line: label.line }))
  );
  return groups;
}

function outlineRow(row, groupId, { member = false } = {}) {
  const el = document.createElement('div');
  el.className = 'outline-row' + (member ? ' member' : '');
  el.title = t('Zeile {line}', { line: row.line });
  if (!member) {
    const kind = document.createElement('span');
    kind.className = 'kind';
    kind.textContent = OUTLINE_KINDS[groupId] || '';
    el.append(kind);
  }
  const name = document.createElement('span');
  name.className = 'name';
  name.textContent = row.name;
  el.append(name);
  if (row.detail) {
    const detail = document.createElement('span');
    detail.className = 'detail';
    detail.textContent = row.detail;
    el.append(detail);
  }
  const line = document.createElement('span');
  line.className = 'ln';
  line.textContent = String(row.line);
  el.append(line);
  el.addEventListener('click', () => revealLine(row.line));
  outlineState.rows.push({ ...row, el, groupId });
  return el;
}

function renderOutline() {
  const body = $('outline-body');
  if (!body) return;
  outlineState.rows = [];
  outlineState.activeEl = null;
  body.innerHTML = '';
  const tab = activeTab();
  const empty = (text) => {
    const hint = document.createElement('div');
    hint.className = 'outline-empty';
    hint.textContent = text;
    return hint;
  };
  if (!tab) {
    dock?.setMeta('outline', '0');
    body.append(empty(t('Keine Datei geöffnet.')));
    return;
  }
  const groups = tab.language === 'gbx' ? outlineGroups(tab.model) : [];
  const total = groups.reduce((sum, group) => sum + group.rows.length, 0);
  dock?.setMeta('outline', String(total));
  if (!total) {
    body.append(empty(t('Keine Symbole in dieser Datei.')));
    return;
  }
  for (const group of groups) {
    const collapsed = outlineState.collapsed.has(group.id);
    const head = document.createElement('div');
    head.className = 'outline-group-head';
    const chevron = document.createElement('span');
    chevron.className = 'chev';
    chevron.textContent = collapsed ? '▸' : '▾';
    const label = document.createElement('span');
    label.textContent = group.label;
    const count = document.createElement('span');
    count.className = 'count';
    count.textContent = String(group.rows.length);
    head.append(chevron, label, count);
    head.addEventListener('click', () => {
      if (collapsed) outlineState.collapsed.delete(group.id);
      else outlineState.collapsed.add(group.id);
      persistOutlinePrefs();
      renderOutline();
    });
    body.append(head);
    if (collapsed) continue;
    for (const row of group.rows) {
      body.append(outlineRow(row, group.id));
      for (const member of row.members || []) {
        body.append(outlineRow(member, group.id, { member: true }));
      }
    }
  }
  updateOutlineActive();
}

function scheduleOutline() {
  clearTimeout(outlineState.timer);
  outlineState.timer = setTimeout(renderOutline, 160);
}

/** Hebt den Block hervor, in dem der Cursor steht. */
function updateOutlineActive() {
  if (!outlineState.rows.length) return;
  const position = editor?.getPosition();
  const line = position?.lineNumber || 0;
  let match = null;
  for (const row of outlineState.rows) {
    if (!row.endLine || row.line > line || row.endLine < line) continue;
    if (!match || row.endLine - row.line < match.endLine - match.line) match = row;
  }
  if (!match) {
    for (const row of outlineState.rows) {
      if (row.line > line) continue;
      if (!match || row.line > match.line) match = row;
    }
  }
  // Nur beim Wechsel des Symbols scrollen, damit die Liste beim Tippen ruhig bleibt.
  if (match && outlineState.activeEl !== match.el) match.el.scrollIntoView({ block: 'nearest' });
  outlineState.activeEl = match ? match.el : null;
  for (const row of outlineState.rows) row.el.classList.toggle('active', row === match);
}

/* ------------------------------------------------------------------ */
/* Welcome / Recent                                                    */
/* ------------------------------------------------------------------ */

function showWelcome(show) {
  $('welcome').classList.toggle('hidden', !show);
}

function renderRecent() {
  const recent = state.health?.config?.recent || [];
  const wrap = $('recent-wrap');
  const list = $('recent-list');
  list.innerHTML = '';
  if (!recent.length) {
    wrap.hidden = true;
    return;
  }
  wrap.hidden = false;
  for (const item of recent.slice(0, 6)) {
    const row = document.createElement('div');
    row.className = 'recent-item';
    const left = document.createElement('div');
    const name = document.createElement('div');
    name.textContent = item.name;
    const pathEl = document.createElement('div');
    pathEl.className = 'p';
    pathEl.textContent = item.path;
    left.append(name, pathEl);
    row.append(left);
    row.addEventListener('click', () => {
      showWelcome(true);
      openPath(item.path).catch((error) => toast(error.message, 'error'));
    });
    list.append(row);
  }
}

/* ------------------------------------------------------------------ */
/* Jobs (SSE)                                                          */
/* ------------------------------------------------------------------ */

function subscribeJob(jobId, onEvent, onDone) {
  const source = new EventSource(`/api/jobs/${jobId}/events`);
  source.onmessage = (message) => {
    let data;
    try {
      data = JSON.parse(message.data);
    } catch {
      return;
    }
    if (data.type === 'done') {
      source.close();
      onDone?.(data);
      return;
    }
    onEvent?.(data);
  };
  source.onerror = () => {
    if (source.readyState === EventSource.CLOSED) return;
    // Der Server hält die Verbindung offen; ein Fehler bedeutet meist, dass
    // der Auftrag nicht mehr existiert.
  };
  return source;
}

/* ------------------------------------------------------------------ */
/* Modals / Toasts                                                     */
/* ------------------------------------------------------------------ */

function openModal({ title, subtitle = '', body, footer = [], wide = false, className = '', onClose = null }) {
  const root = $('modal-root');
  const backdrop = document.createElement('div');
  backdrop.className = 'modal-backdrop';
  const modal = document.createElement('div');
  modal.className = 'modal' + (wide ? ' wide' : '') + (className ? ` ${className}` : '');
  const head = document.createElement('div');
  head.className = 'modal-head';
  head.textContent = title;
  if (subtitle) {
    const sub = document.createElement('span');
    sub.className = 'sub';
    sub.textContent = subtitle;
    head.append(sub);
  }
  const bodyWrap = document.createElement('div');
  bodyWrap.className = 'modal-body';
  bodyWrap.append(body);
  const foot = document.createElement('div');
  foot.className = 'modal-foot';
  let closed = false;
  const api = {
    close() {
      if (closed) return;
      closed = true;
      document.removeEventListener('keydown', onKey);
      backdrop.remove();
      state.modalOpen = false;
      if (onClose) onClose();
    }
  };
  for (const item of footer) {
    const button = document.createElement('button');
    button.className = 'btn ' + (item.variant || 'ghost');
    button.textContent = item.label;
    button.addEventListener('click', async () => {
      button.disabled = true;
      try {
        const result = await item.action();
        if (result !== false) api.close();
      } finally {
        button.disabled = false;
      }
    });
    foot.append(button);
  }
  const onKey = (event) => {
    if (event.key === 'Escape') api.close();
  };
  document.addEventListener('keydown', onKey);
  backdrop.addEventListener('mousedown', (event) => {
    if (event.target === backdrop) api.close();
  });
  modal.append(head, bodyWrap, foot);
  backdrop.append(modal);
  root.append(backdrop);
  state.modalOpen = true;
  return api;
}

function confirmDialog(title, message, confirmLabel = 'OK', allowCancel = false) {
  return new Promise((resolve) => {
    const body = document.createElement('div');
    body.style.color = 'var(--muted)';
    body.style.lineHeight = '1.55';
    body.textContent = message;
    const modal = openModal({
      title,
      body,
      footer: [
        { label: t('Abbrechen'), action: () => { resolve(allowCancel ? null : false); return true; } },
        { label: confirmLabel, variant: 'primary', action: () => { resolve(true); return true; } }
      ]
    });
    modal.close = ((original) => () => {
      original();
      resolve(allowCancel ? null : false);
    })(modal.close);
  });
}

/** Rückfrage bei ungespeicherten Änderungen: 'save', 'discard' oder null. */
function saveChangesDialog(title, message, saveLabel) {
  return new Promise((resolve) => {
    const body = document.createElement('div');
    const text = document.createElement('div');
    text.style.color = 'var(--muted)';
    text.style.lineHeight = '1.55';
    text.textContent = message;
    const hint = document.createElement('div');
    hint.style.marginTop = '8px';
    hint.style.fontSize = '11.5px';
    hint.style.color = 'var(--muted2)';
    hint.textContent = t('Schließen verwirft die Änderungen.');
    body.append(text, hint);

    const modal = openModal({
      title,
      body,
      footer: [
        { label: t('Abbrechen'), action: () => { resolve(null); return true; } },
        { label: t('Schließen'), variant: 'danger', action: () => { resolve('discard'); return true; } },
        { label: saveLabel, variant: 'primary', action: () => { resolve('save'); return true; } }
      ]
    });
    modal.close = ((original) => () => {
      original();
      resolve(null);
    })(modal.close);
  });
}

function toast(message, kind = 'ok') {
  const host = $('toasts');
  const el = document.createElement('div');
  el.className = 'toast ' + (kind === 'error' ? 'error' : kind === 'warn' ? 'warn' : '');
  el.textContent = message;
  host.append(el);
  setTimeout(() => {
    el.style.opacity = '0';
    el.style.transform = 'translateY(6px)';
    el.style.transition = 'all .2s';
    setTimeout(() => el.remove(), 220);
  }, kind === 'error' ? 5500 : 3200);
}

function escapeAttr(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/"/g, '&quot;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}
