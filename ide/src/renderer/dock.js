/**
 * Andockbare Panels: Dateien, Outline, Referenz, Probleme, Ausgabe, Konsole
 * und Debugger lassen sich zwischen linker, rechter und unterer Zone
 * verschieben. Die Seitenzonen stapeln ihre Panels untereinander, die untere
 * Zone zeigt sie als Tabs.
 *
 * Die Layout-Helfer sind ohne DOM nutzbar (Tests). `createDock` zeichnet die
 * Zonen, übernimmt Ziehen am Panel-Kopf, das Andock-Menü und das Speichern.
 */

export const DOCK_PANEL_IDS = ['files', 'outline', 'reference', 'problems', 'output', 'console', 'debug'];
export const DOCK_ZONES = ['left', 'right', 'bottom'];

export const DEFAULT_DOCK = {
  zones: {
    left: ['files', 'outline'],
    right: ['reference'],
    bottom: ['problems', 'output', 'console', 'debug']
  },
  heights: {
    files: 280,
    outline: 240,
    reference: 340,
    problems: 220,
    output: 220,
    console: 220,
    debug: 260
  },
  active: 'problems'
};

export function isDockPanel(id) {
  return DOCK_PANEL_IDS.includes(id);
}

function clampHeight(value, fallback) {
  const size = Number(value);
  if (!Number.isFinite(size)) return fallback;
  return Math.min(Math.max(Math.round(size), 64), 1400);
}

/**
 * Prüft eine gespeicherte Ablage: unbekannte und doppelte Panels fallen weg,
 * fehlende landen in ihrer Standardzone. So verschwindet ein Panel nicht,
 * wenn eine ältere Ablage es noch nicht kennt.
 */
export function normalizeDock(input) {
  const saved = input && typeof input === 'object' ? input : {};
  const stored = saved.zones && typeof saved.zones === 'object' ? saved.zones : {};
  const zones = { left: [], right: [], bottom: [] };
  const seen = new Set();
  for (const zone of DOCK_ZONES) {
    const list = Array.isArray(stored[zone]) ? stored[zone] : [];
    for (const id of list) {
      if (!isDockPanel(id) || seen.has(id)) continue;
      seen.add(id);
      zones[zone].push(id);
    }
  }
  for (const zone of DOCK_ZONES) {
    for (const id of DEFAULT_DOCK.zones[zone]) {
      if (seen.has(id)) continue;
      seen.add(id);
      zones[zone].push(id);
    }
  }
  const collapsed = {};
  const heights = {};
  for (const id of DOCK_PANEL_IDS) {
    collapsed[id] = !!saved.collapsed?.[id];
    heights[id] = clampHeight(saved.heights?.[id], DEFAULT_DOCK.heights[id]);
  }
  const open = {
    left: saved.open?.left !== false,
    right: saved.open?.right !== false,
    bottom: saved.open?.bottom !== false
  };
  const active = zones.bottom.includes(saved.active) ? saved.active : zones.bottom[0] || '';
  return { zones, collapsed, heights, open, active };
}

export function dockZoneOf(layout, id) {
  for (const zone of DOCK_ZONES) {
    if (layout.zones[zone].includes(id)) return zone;
  }
  return '';
}

export function dockPanels(layout, zone) {
  return layout.zones[zone] || [];
}

/** Verschiebt ein Panel in eine Zone; `index` ist die Einfügeposition. */
export function dockMove(layout, id, zone, index = -1) {
  const next = normalizeDock(layout);
  if (!isDockPanel(id) || !DOCK_ZONES.includes(zone)) return next;
  const from = dockZoneOf(next, id);
  if (from) next.zones[from] = next.zones[from].filter((item) => item !== id);
  const target = next.zones[zone];
  const at = Number.isInteger(index) && index >= 0 && index <= target.length ? index : target.length;
  target.splice(at, 0, id);
  // Ein verschobenes Panel ist sichtbar – man hat es ja gerade angefasst.
  next.collapsed[id] = false;
  next.open[zone] = true;
  if (zone === 'bottom') next.active = id;
  if (!next.zones.bottom.includes(next.active)) next.active = next.zones.bottom[0] || '';
  return next;
}

export function dockToggleCollapsed(layout, id, force) {
  const next = normalizeDock(layout);
  if (!isDockPanel(id)) return next;
  next.collapsed[id] = force === undefined ? !next.collapsed[id] : !!force;
  return next;
}

export function dockSetOpen(layout, zone, open) {
  const next = normalizeDock(layout);
  if (!DOCK_ZONES.includes(zone)) return next;
  next.open[zone] = !!open;
  return next;
}

export function dockSetHeight(layout, id, height) {
  const next = normalizeDock(layout);
  if (!isDockPanel(id)) return next;
  next.heights[id] = clampHeight(height, next.heights[id]);
  return next;
}

export function dockActivate(layout, id) {
  const next = normalizeDock(layout);
  const zone = dockZoneOf(next, id);
  if (!zone) return next;
  next.collapsed[id] = false;
  next.open[zone] = true;
  if (zone === 'bottom') next.active = id;
  return next;
}

/** Eine Seitenzone ohne sichtbares Panel braucht keinen Platz. */
export function dockZoneVisible(layout, zone) {
  if (zone === 'bottom') return !!layout.zones.bottom.length;
  if (!layout.open[zone]) return false;
  return layout.zones[zone].some((id) => !layout.collapsed[id]);
}

/* ------------------------------------------------------------------ */
/* Darstellung                                                         */
/* ------------------------------------------------------------------ */

const DOCK_STORAGE_KEY = 'gbxstudio.dock.v1';

/**
 * Baut die Zonen, verteilt die Panel-Inhalte und merkt sich die Ablage.
 *
 * @param {object} options
 * @param {(text: string) => string} options.t            Übersetzer
 * @param {Record<string, HTMLElement>} options.hosts     Zonen (left/right/bottom…)
 * @param {Record<string, HTMLElement>} options.sources   Panel-Inhalte je id
 * @param {(id: string) => string} options.title          Titel eines Panels
 * @param {(id: string) => HTMLElement | null} [options.action]  Zusatzaktion
 * @param {() => void} [options.onChange]                 nach jeder Änderung
 */
export function createDock({ t, hosts, sources, title, action, onChange }) {
  let layout = readStored();
  const meta = {};
  let dragId = '';
  let menuEl = null;
  // Inhalte, die gerade nicht sichtbar sind, warten im versteckten Vorrat.
  const stash = document.getElementById('dock-sources');

  function readStored() {
    try {
      return normalizeDock(JSON.parse(localStorage.getItem(DOCK_STORAGE_KEY) || 'null'));
    } catch {
      return normalizeDock(null);
    }
  }

  function save() {
    try {
      localStorage.setItem(DOCK_STORAGE_KEY, JSON.stringify(layout));
    } catch {
      /* Speicher nicht verfügbar */
    }
  }

  function changed(persist = true) {
    if (persist) save();
    render();
    onChange?.();
  }

  function setMeta(id, text, className = '') {
    meta[id] = { text: String(text ?? ''), className };
    for (const el of document.querySelectorAll(`[data-meta="${id}"]`)) {
      el.textContent = meta[id].text;
      el.className = `count${className ? ' ' + className : ''}`;
    }
  }

  function panelHead(id, { tab = false } = {}) {
    const collapsed = !!layout.collapsed[id];
    const el = document.createElement(tab ? 'button' : 'div');
    el.className = tab ? 'panel-tab' : 'dock-head';
    el.dataset.panel = id;
    if (tab) {
      el.type = 'button';
      el.classList.toggle('active', layout.active === id);
      el.setAttribute('role', 'tab');
      el.setAttribute('aria-selected', String(layout.active === id));
    } else {
      el.title = t('{panel} – ziehen zum Andocken', { panel: title(id) });
    }
    const name = document.createElement('span');
    name.className = 'dock-title';
    name.textContent = title(id);
    el.append(name);
    // Zähler für Outline, Probleme und Referenz stehen im Kopf bzw. Tab.
    if (['outline', 'problems', 'reference', 'debug'].includes(id)) {
      const count = document.createElement('span');
      count.className = 'count';
      count.dataset.meta = id;
      count.textContent = meta[id]?.text ?? '';
      if (meta[id]?.className) count.classList.add(meta[id].className);
      el.append(count);
    }
    if (tab) {
      bindDrag(el, id);
      return el;
    }

    const spacer = document.createElement('span');
    spacer.className = 'spacer';
    el.append(spacer);
    const extra = action?.(id);
    if (extra) el.append(extra);
    const collapse = document.createElement('button');
    collapse.type = 'button';
    collapse.className = 'icon-btn dock-collapse';
    collapse.title = collapsed ? t('Ausklappen') : t('Einklappen');
    collapse.setAttribute('aria-label', collapse.title);
    collapse.textContent = collapsed ? '▸' : '▾';
    collapse.addEventListener('click', (event) => {
      event.stopPropagation();
      toggle(id);
    });
    const menu = document.createElement('button');
    menu.type = 'button';
    menu.className = 'icon-btn dock-menu';
    menu.title = t('Panel-Menü');
    menu.setAttribute('aria-label', menu.title);
    menu.textContent = '⋯';
    menu.addEventListener('click', (event) => {
      event.stopPropagation();
      openMenu(menu, id);
    });
    el.append(collapse, menu);
    el.addEventListener('dblclick', (event) => {
      if (event.target.closest('button')) return;
      toggle(id);
    });
    bindDrag(el, id);
    return el;
  }

  /** Ziehen am Panel-Kopf oder Tab: die Zielzone wird hervorgehoben. */
  function bindDrag(el, id) {
    el.draggable = true;
    el.addEventListener('dragstart', (event) => {
      dragId = id;
      event.dataTransfer.setData('text/plain', id);
      event.dataTransfer.effectAllowed = 'move';
      document.body.classList.add('dock-dragging');
    });
    el.addEventListener('dragend', () => {
      dragId = '';
      document.body.classList.remove('dock-dragging');
      clearDropMarks();
    });
  }

  /** Titel nach einem Projekt- oder Sprachwechsel ohne Neuaufbau tauschen. */
  function refreshTitles() {
    for (const el of document.querySelectorAll('.dock-head[data-panel], .panel-tab[data-panel]')) {
      const name = el.querySelector('.dock-title');
      if (name) name.textContent = title(el.dataset.panel);
    }
  }

  function openMenu(anchor, id) {
    closeMenu();
    const zone = dockZoneOf(layout, id);
    const menu = document.createElement('div');
    menu.className = 'context-menu dock-menu-list';
    const item = (label, run, active = false) => {
      const button = document.createElement('button');
      button.className = 'context-item' + (active ? ' active' : '');
      button.type = 'button';
      button.textContent = label;
      button.addEventListener('click', () => {
        closeMenu();
        run();
      });
      menu.append(button);
    };
    menu.append(heading(t('Andocken an')));
    for (const target of DOCK_ZONES) {
      const label = { left: t('Links'), right: t('Rechts'), bottom: t('Unten') }[target];
      item(label, () => move(id, target), zone === target);
    }
    menu.append(heading(t('Panel')));
    item(layout.collapsed[id] ? t('Ausklappen') : t('Einklappen'), () => toggle(id));
    item(t('Zonen zurücksetzen'), () => reset());
    document.body.append(menu);
    const box = anchor.getBoundingClientRect();
    const width = menu.getBoundingClientRect().width;
    menu.style.left = `${Math.max(8, Math.min(box.right - width, window.innerWidth - width - 8))}px`;
    menu.style.top = `${Math.min(box.bottom + 4, window.innerHeight - menu.offsetHeight - 8)}px`;
    menuEl = menu;
    setTimeout(() => {
      document.addEventListener('pointerdown', onOutside, { once: true });
      document.addEventListener('keydown', onEscape, { once: true });
    }, 0);
  }

  function heading(text) {
    const el = document.createElement('div');
    el.className = 'context-heading';
    el.textContent = text;
    return el;
  }

  function onOutside(event) {
    if (menuEl && menuEl.contains(event.target)) {
      document.addEventListener('pointerdown', onOutside, { once: true });
      return;
    }
    closeMenu();
  }

  function onEscape(event) {
    if (event.key === 'Escape') closeMenu();
  }

  function closeMenu() {
    menuEl?.remove();
    menuEl = null;
    document.removeEventListener('pointerdown', onOutside);
    document.removeEventListener('keydown', onEscape);
  }

  function clearDropMarks() {
    for (const el of document.querySelectorAll('.dock-drop')) el.classList.remove('dock-drop');
  }

  function bindZone(host) {
    if (!host || host.dataset.dockBound) return;
    host.dataset.dockBound = '1';
    const zone = host.dataset.zone;
    host.addEventListener('dragover', (event) => {
      if (!dragId) return;
      event.preventDefault();
      event.dataTransfer.dropEffect = 'move';
      clearDropMarks();
      host.classList.add('dock-drop');
    });
    host.addEventListener('dragleave', (event) => {
      if (event.target === host) host.classList.remove('dock-drop');
    });
    host.addEventListener('drop', (event) => {
      event.preventDefault();
      const id = dragId || event.dataTransfer.getData('text/plain');
      clearDropMarks();
      document.body.classList.remove('dock-dragging');
      if (!isDockPanel(id)) return;
      move(id, zone, dropIndex(host, event.clientY));
    });
  }

  /** Einfügeposition anhand der Panel-Mitten – Ziehen nach oben/unten sortiert. */
  function dropIndex(host, clientY) {
    if (host.dataset.zone === 'bottom') return layout.zones.bottom.length;
    const panels = [...host.querySelectorAll(':scope > .dock-panel')];
    for (let index = 0; index < panels.length; index++) {
      const box = panels[index].getBoundingClientRect();
      if (clientY < box.top + box.height / 2) return index;
    }
    return panels.length;
  }

  function bindSplitter(el, id) {
    el.addEventListener('pointerdown', (event) => {
      if (event.button !== 0) return;
      event.preventDefault();
      try {
        el.setPointerCapture?.(event.pointerId);
      } catch {
        /* synthetische Events ohne aktiven Pointer */
      }
      el.classList.add('dragging');
      document.body.classList.add('resizing', 'resizing-row');
      const panel = el.nextElementSibling;
      const startY = event.clientY;
      const startHeight = panel?.getBoundingClientRect().height || layout.heights[id];
      const zoneHeight = el.parentElement?.clientHeight || 0;
      const move = (moveEvent) => {
        // Die Trennlinie ist die Oberkante des unteren Panels: nach oben ziehen
        // macht es größer, nach unten kleiner.
        const next = Math.min(
          Math.max(startHeight - (moveEvent.clientY - startY), 64),
          Math.max(64, zoneHeight - 96)
        );
        if (panel) {
          panel.style.flex = `0 0 ${Math.round(next)}px`;
          panel.style.height = `${Math.round(next)}px`;
        }
      };
      const finish = () => {
        el.removeEventListener('pointermove', move);
        el.classList.remove('dragging');
        document.body.classList.remove('resizing', 'resizing-row');
        if (panel) layout = dockSetHeight(layout, id, panel.getBoundingClientRect().height);
        changed();
      };
      el.addEventListener('pointermove', move);
      el.addEventListener('pointerup', finish, { once: true });
      el.addEventListener('pointercancel', finish, { once: true });
    });
    el.addEventListener('dblclick', () => {
      layout = dockSetHeight(layout, id, DEFAULT_DOCK.heights[id]);
      changed();
    });
  }

  function sidePanel(id, zoneList, fillIndex) {
    const index = zoneList.indexOf(id);
    const collapsed = !!layout.collapsed[id];
    const wrapper = document.createElement('section');
    wrapper.className = 'dock-panel' + (collapsed ? ' collapsed' : '');
    wrapper.dataset.panel = id;
    wrapper.append(panelHead(id));
    if (collapsed) return wrapper;
    const body = document.createElement('div');
    body.className = 'dock-body';
    const source = sources[id];
    if (source) body.append(source);
    wrapper.append(body);
    // Das oberste offene Panel füllt den Rest, die darunter behalten ihre Höhe.
    const fills = index === fillIndex;
    wrapper.style.flex = fills ? '1 1 auto' : `0 0 ${layout.heights[id]}px`;
    if (!fills) wrapper.style.height = `${layout.heights[id]}px`;
    return wrapper;
  }

  function renderSide(host, zone) {
    if (!host) return;
    host.replaceChildren();
    bindZone(host);
    const list = layout.zones[zone];
    if (!list.length) return;
    const fillIndex = Math.max(0, list.findIndex((id) => !layout.collapsed[id]));
    list.forEach((id, index) => {
      if (index > 0) {
        const split = document.createElement('div');
        split.className = 'splitter dock-split';
        split.setAttribute('role', 'separator');
        split.setAttribute('aria-orientation', 'horizontal');
        split.title = t('Höhe ziehen');
        bindSplitter(split, id);
        host.append(split);
      }
      host.append(sidePanel(id, list, fillIndex));
    });
  }

  function renderBottom() {
    const { tabs, bottom, body } = hosts;
    if (!tabs || !bottom) return;
    bindZone(bottom);
    tabs.replaceChildren();
    const list = layout.zones.bottom;
    if (!list.length) {
      body?.replaceChildren();
      bottom.hidden = true;
      bottom.classList.add('dock-empty');
      if (hosts.bottomSplitter) hosts.bottomSplitter.hidden = true;
      return;
    }
    bottom.hidden = false;
    bottom.classList.remove('dock-empty');
    if (hosts.bottomSplitter) hosts.bottomSplitter.hidden = false;
    for (const id of list) {
      const tab = panelHead(id, { tab: true });
      tab.addEventListener('click', () => activate(id));
      tabs.append(tab);
    }
    if (!list.includes(layout.active)) layout.active = list[0];
    body?.replaceChildren();
    const source = sources[layout.active];
    if (source && body) body.append(source);
  }

  function applyZones() {
    const workspace = document.getElementById('workspace');
    if (!workspace) return;
    workspace.classList.toggle('sidebar-collapsed', !dockZoneVisible(layout, 'left'));
    workspace.classList.toggle('ref-collapsed', !dockZoneVisible(layout, 'right'));
    const bottomVisible = dockZoneVisible(layout, 'bottom') && layout.open.bottom;
    hosts.bottom?.classList.toggle('collapsed', !layout.open.bottom);
    if (hosts.bottomSplitter) hosts.bottomSplitter.hidden = !bottomVisible;
    hosts.left?.classList.toggle('zone-empty', !layout.zones.left.length);
    hosts.right?.classList.toggle('zone-empty', !layout.zones.right.length);
  }

  /** Alle Inhalte zurück in den Vorrat – sonst hängt ein ersetztes Panel
      ohne Inhalt in der Luft und seine Ausgabe wäre verloren. */
  function collectSources() {
    if (!stash) return;
    for (const id of DOCK_PANEL_IDS) {
      const el = sources[id];
      if (el) stash.append(el);
    }
  }

  function render() {
    collectSources();
    renderSide(hosts.left, 'left');
    renderSide(hosts.right, 'right');
    renderBottom();
    applyZones();
  }

  function move(id, zone, index = -1) {
    layout = dockMove(layout, id, zone, index);
    changed();
  }

  function activate(id) {
    layout = dockActivate(layout, id);
    changed();
    if (dockZoneOf(layout, id) === 'bottom') editorLayout();
  }

  function toggle(id, force) {
    layout = dockToggleCollapsed(layout, id, force);
    changed();
  }

  function setZoneOpen(zone, open, { persist = true } = {}) {
    layout = dockSetOpen(layout, zone, open);
    changed(persist);
  }

  function toggleZone(zone) {
    setZoneOpen(zone, !layout.open[zone]);
  }

  function reset() {
    layout = normalizeDock(null);
    changed();
  }

  /**
   * Übernimmt eine gespeicherte Ablage, z. B. die eines Projekts. Unbekannte
   * Panels ergänzt `normalizeDock`, damit die Oberfläche vollständig bleibt.
   */
  function apply(saved, { persist = true } = {}) {
    layout = normalizeDock(saved);
    changed(persist);
  }

  function editorLayout() {
    // Monaco braucht nach jeder Größenänderung der Zonen ein layout().
    onChange?.();
  }

  function zoneOf(id) {
    return dockZoneOf(layout, id);
  }

  function isCollapsed(id) {
    return !!layout.collapsed[id];
  }

  return {
    render,
    refreshTitles,
    move,
    activate,
    toggle,
    toggleZone,
    setZoneOpen,
    setMeta,
    reset,
    apply,
    zoneOf,
    isCollapsed,
    get layout() {
      return layout;
    }
  };
}
