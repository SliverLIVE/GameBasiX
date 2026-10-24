/* Google Fonts – Schriften suchen, in der echten Schrift ansehen und als
   TrueType-Datei in den Asset-Ordner des Projekts laden.

   Die Vorschau kommt nicht von fonts.gstatic.com, sondern über den lokalen
   Server: der holt die TTF einmal, die Oberfläche baut daraus eine FontFace.
   So funktioniert die Vorschau auch, wenn der Browser keine fremden Schriften
   laden darf, und der Download nimmt denselben Weg. */

const SAMPLE_TEXT = 'Franz jagt im komplett verwahrlosten Taxi quer durch Bayern.';
const LIST_CHUNK = 80;

const WEIGHT_LABELS = {
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

function weightLabel(weight) {
  return `${WEIGHT_LABELS[weight] || weight} ${weight}`;
}

/** Dateiname wie im Server (googlefonts.mjs) – nur für die Anzeige vorab. */
function previewFileName(family, weight, style) {
  const base = String(family || '')
    .normalize('NFKD')
    .replace(/[\u0300-\u036f]/g, '')
    .replace(/[^A-Za-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 64) || 'Font';
  return `${base}-${WEIGHT_LABELS[weight] || weight}${style === 'italic' ? 'Italic' : ''}.ttf`;
}

function normalize(value) {
  return String(value || '')
    .normalize('NFKD')
    .replace(/[\u0300-\u036f]/g, '')
    .toLowerCase()
    .trim();
}

export function openGoogleFonts({ t, openModal, toast, get, post, projectDir, project }) {
  const body = document.createElement('div');
  body.className = 'gfont-tool';
  body.innerHTML = `
    <div class="gfont-side">
      <div class="field">
        <label for="gf-search">${t('Schrift suchen')}</label>
        <input type="search" id="gf-search" placeholder="${t('z. B. Roboto')}" autocomplete="off" spellcheck="false">
      </div>
      <div class="field">
        <label for="gf-category">${t('Kategorie')}</label>
        <select id="gf-category">
          <option value="">${t('Alle Kategorien')}</option>
        </select>
      </div>
      <div class="gfont-list" id="gf-list" role="listbox" aria-label="Google Fonts"></div>
      <div class="hint" id="gf-count">${t('Google Fonts werden geladen …')}</div>
    </div>

    <div class="gfont-main">
      <div class="gfont-head">
        <div class="gfont-name" id="gf-name">–</div>
        <div class="gfont-cat" id="gf-cat"></div>
      </div>

      <div class="gfont-preview" id="gf-preview">
        <div class="gfont-sample" id="gf-sample">${SAMPLE_TEXT}</div>
        <div class="gfont-alphabet" id="gf-alphabet">ABCDEFGHIJKLMNOPQRSTUVWXYZ<br>abcdefghijklmnopqrstuvwxyz 0123456789</div>
        <div class="gfont-note" id="gf-note"></div>
      </div>

      <div class="row">
        <div class="field">
          <label for="gf-style">${t('Stil')}</label>
          <select id="gf-style">
            <option value="normal">${t('Normal')}</option>
            <option value="italic">${t('Kursiv')}</option>
          </select>
        </div>
        <div class="field">
          <label for="gf-weight">${t('Gewicht')}</label>
          <select id="gf-weight"></select>
        </div>
        <div class="field">
          <label for="gf-size">${t('Größe')}</label>
          <div class="slider-row">
            <input type="range" id="gf-size" min="10" max="96" value="40">
            <span class="val" id="gf-size-val">40</span>
          </div>
        </div>
      </div>

      <div class="field">
        <label for="gf-text">${t('Vorschautext')}</label>
        <input type="text" id="gf-text" value="${SAMPLE_TEXT}" spellcheck="false">
      </div>

      <div class="gfont-target">
        <span class="gfont-target-label">${t('Ziel')}</span>
        <span class="gfont-file" id="gf-target"></span>
        <button type="button" class="btn ghost" id="gf-copy" hidden>${t('Befehl kopieren')}</button>
      </div>
      <div class="hint" id="gf-command"></div>
    </div>
  `;

  const $ = (id) => body.querySelector(`#${id}`);
  const search = $('gf-search');
  const category = $('gf-category');
  const list = $('gf-list');
  const count = $('gf-count');
  const sample = $('gf-sample');
  const alphabet = $('gf-alphabet');
  const note = $('gf-note');
  const nameEl = $('gf-name');
  const catEl = $('gf-cat');
  const styleSel = $('gf-style');
  const weightSel = $('gf-weight');
  const sizeInput = $('gf-size');
  const sizeValue = $('gf-size-val');
  const textInput = $('gf-text');
  const targetEl = $('gf-target');
  const commandEl = $('gf-command');
  const copyButton = $('gf-copy');

  let families = [];
  let filtered = [];
  let rendered = 0;
  let selected = null;
  let warning = '';
  let source = '';
  let token = 0;
  let lastCommand = '';
  let searchTimer = null;

  // Geladene FontFace je Familie/Stärke/Stil – einmal reicht für die Sitzung.
  const faces = new Map();

  const assetDir = () => (project()?.assetDir || 'assets').trim() || 'assets';

  function setCount(text) {
    count.textContent = text;
  }

  function weightOptions(family) {
    return (family.weights || [400]).map((weight) => `<option value="${weight}">${weightLabel(weight)}</option>`).join('');
  }

  function keepWeight(family) {
    const current = Number(weightSel.value);
    if ((family.weights || []).includes(current)) return current;
    return (family.weights || []).includes(400) ? 400 : family.weights?.[0] || 400;
  }

  function renderRows() {
    const slice = filtered.slice(rendered, rendered + LIST_CHUNK);
    const nodes = slice.map((family) => {
      const row = document.createElement('button');
      row.type = 'button';
      row.className = 'gfont-row' + (selected && family.name === selected.name ? ' active' : '');
      row.dataset.name = family.name;
      row.setAttribute('role', 'option');
      row.setAttribute('aria-selected', selected && family.name === selected.name ? 'true' : 'false');

      const name = document.createElement('span');
      name.className = 'gfont-row-name';
      name.textContent = family.name;

      const meta = document.createElement('span');
      meta.className = 'gfont-row-meta';
      meta.textContent = family.italic ? `${family.category} · I` : family.category;

      row.append(name, meta);
      row.addEventListener('click', () => choose(family));
      return row;
    });
    list.append(...nodes);
    rendered += nodes.length;
  }

  function appendMore() {
    if (rendered >= filtered.length) return;
    renderRows();
  }

  function markActive() {
    for (const row of list.querySelectorAll('.gfont-row')) {
      const active = Boolean(selected) && row.dataset.name === selected.name;
      row.classList.toggle('active', active);
      row.setAttribute('aria-selected', active ? 'true' : 'false');
    }
  }

  function updateTarget() {
    if (!selected) {
      targetEl.textContent = '';
      commandEl.textContent = '';
      return;
    }
    const weight = Number(weightSel.value) || 400;
    const style = styleSel.value === 'italic' ? 'italic' : 'normal';
    if (!projectDir()) {
      targetEl.textContent = t('Kein Projekt geöffnet');
      commandEl.textContent = t('Bitte zuerst ein Projekt öffnen – die Datei landet im Ordner {dir}.', {
        dir: assetDir()
      });
      return;
    }
    const relative = `${assetDir().replace(/\\/g, '/').replace(/\/$/, '')}/${previewFileName(selected.name, weight, style)}`;
    targetEl.textContent = relative;
    commandEl.textContent = lastCommand && lastCommand.includes(relative)
      ? lastCommand
      : `LOADFONT "${relative}", 1, 20`;
  }

  function resetDownload() {
    lastCommand = '';
    copyButton.hidden = true;
    commandEl.textContent = '';
  }

  async function ensureFace(family, weight, style) {
    const key = `${family}|${weight}|${style}`;
    if (faces.has(key)) return faces.get(key);
    const pending = (async () => {
      const url = `/api/googlefonts/preview?family=${encodeURIComponent(family)}&weight=${encodeURIComponent(weight)}&style=${encodeURIComponent(style)}`;
      let response;
      try {
        response = await fetch(url);
      } catch {
        throw new Error(t('Die Vorschau konnte nicht geladen werden.'));
      }
      if (!response.ok) {
        let message = '';
        try {
          message = (await response.json())?.error || '';
        } catch {
          message = '';
        }
        throw new Error(message || t('Die Vorschau konnte nicht geladen werden.'));
      }
      const buffer = await response.arrayBuffer();
      const face = new FontFace(`GBX ${family} ${weight} ${style}`, buffer);
      await face.load();
      document.fonts.add(face);
      return face;
    })();
    faces.set(key, pending);
    try {
      return await pending;
    } catch (error) {
      faces.delete(key);
      throw error;
    }
  }

  async function renderPreview() {
    if (!selected) return;
    const weight = Number(weightSel.value) || 400;
    const style = styleSel.value === 'italic' ? 'italic' : 'normal';
    const current = ++token;
    const text = textInput.value.trim() || SAMPLE_TEXT;
    sample.textContent = text;
    sample.style.fontSize = `${Number(sizeInput.value) || 40}px`;
    sample.style.fontFamily = '';
    alphabet.style.fontFamily = '';
    note.textContent = t('Schrift wird geladen …');
    try {
      const face = await ensureFace(selected.name, weight, style);
      if (current !== token) return;
      sample.style.fontFamily = `"${face.family}"`;
      alphabet.style.fontFamily = `"${face.family}"`;
      const bits = [selected.category, weightLabel(weight)];
      if (style === 'italic') bits.push(t('Kursiv'));
      note.textContent = bits.join(' · ');
    } catch (error) {
      if (current !== token) return;
      note.textContent = error.message;
      sample.style.fontFamily = 'var(--font-ui, sans-serif)';
      alphabet.style.fontFamily = 'var(--font-ui, sans-serif)';
    }
  }

  function choose(family) {
    if (!family) return;
    selected = family;
    nameEl.textContent = family.name;
    catEl.textContent = family.category;
    markActive();
    resetDownload();

    const weight = keepWeight(family);
    weightSel.innerHTML = weightOptions(family);
    weightSel.value = String(weight);
    styleSel.disabled = !family.italic;
    if (!family.italic) styleSel.value = 'normal';

    updateTarget();
    renderPreview();
  }

  function applyFilter() {
    const query = normalize(search.value);
    const chosen = category.value;
    filtered = families.filter((family) => {
      if (chosen && family.category !== chosen) return false;
      return !query || normalize(family.name).includes(query);
    });

    rendered = 0;
    list.replaceChildren();
    list.scrollTop = 0;
    appendMore();
    markActive();

    if (filtered.length && (!selected || !filtered.some((family) => family.name === selected.name))) {
      choose(filtered[0]);
    } else if (!filtered.length) {
      selected = null;
      nameEl.textContent = '–';
      catEl.textContent = '';
      sample.textContent = '';
      alphabet.style.fontFamily = '';
      note.textContent = '';
      targetEl.textContent = '';
      commandEl.textContent = '';
      copyButton.hidden = true;
      setCount(t('Keine Treffer.'));
      return;
    }

    const total = families.length;
    const origin = source === 'api' ? t('Google-Fonts-API') : t('öffentliche Fontliste');
    const hits = filtered.length === total ? `${total}` : `${filtered.length} / ${total}`;
    setCount(t('{n} Schriften · {origin}', { n: hits, origin }));
  }

  function fillCategories() {
    const wanted = ['Sans Serif', 'Serif', 'Display', 'Handwriting', 'Monospace'];
    const present = [...new Set(families.map((family) => family.category))];
    const ordered = [...wanted.filter((name) => present.includes(name)), ...present.filter((name) => !wanted.includes(name))];
    category.append(...ordered.map((name) => Object.assign(document.createElement('option'), { value: name, textContent: name })));
  }

  async function loadFamilies() {
    try {
      const data = await get('/api/googlefonts');
      families = Array.isArray(data.families) ? data.families : [];
      source = data.source || '';
      warning = data.warning || '';
      fillCategories();
      applyFilter();
      if (warning) {
        toast(warning, 'warn');
      }
    } catch (error) {
      setCount(error.message);
      note.textContent = '';
    }
  }

  search.addEventListener('input', () => {
    clearTimeout(searchTimer);
    searchTimer = setTimeout(applyFilter, 120);
  });
  category.addEventListener('change', applyFilter);

  list.addEventListener('scroll', () => {
    if (list.scrollTop + list.clientHeight >= list.scrollHeight - 160) appendMore();
  });

  sizeInput.addEventListener('input', () => {
    sizeValue.textContent = sizeInput.value;
    sample.style.fontSize = `${Number(sizeInput.value) || 40}px`;
  });
  textInput.addEventListener('input', () => {
    sample.textContent = textInput.value.trim() || SAMPLE_TEXT;
  });
  styleSel.addEventListener('change', () => {
    resetDownload();
    updateTarget();
    renderPreview();
  });
  weightSel.addEventListener('change', () => {
    resetDownload();
    updateTarget();
    renderPreview();
  });

  copyButton.addEventListener('click', async () => {
    const text = lastCommand || commandEl.textContent;
    if (!text) return;
    try {
      await navigator.clipboard.writeText(text);
      toast(t('Befehl kopiert.'), 'ok');
    } catch {
      toast(t('Der Befehl konnte nicht kopiert werden.'), 'error');
    }
  });

  const modal = openModal({
    title: t('Google Fonts'),
    subtitle: t('Schriften für LOADFONT'),
    className: 'gfont-modal',
    body,
    footer: [
      { label: t('Schließen'), action: () => true },
      {
        label: t('Herunterladen'),
        variant: 'primary',
        action: async () => {
          if (!selected) {
            toast(t('Bitte zuerst eine Schrift wählen.'), 'warn');
            return false;
          }
          if (!projectDir()) {
            toast(t('Bitte zuerst ein Projekt öffnen.'), 'warn');
            return false;
          }
          try {
            const result = await post('/api/googlefonts/download', {
              projectDir: projectDir(),
              dir: assetDir(),
              family: selected.name,
              weight: Number(weightSel.value) || 400,
              style: styleSel.value === 'italic' ? 'italic' : 'normal'
            });
            lastCommand = result.command || '';
            commandEl.textContent = lastCommand;
            copyButton.hidden = !lastCommand;
            toast(t('Schrift gespeichert: {path}', { path: result.relative || result.path }), 'ok');
          } catch (error) {
            toast(error.message, 'error');
          }
          // Der Dialog bleibt offen: oft möchte man gleich eine zweite Stärke.
          return false;
        }
      }
    ]
  });

  setTimeout(() => {
    textInput.value = SAMPLE_TEXT;
    sample.style.fontSize = '40px';
    loadFamilies();
  }, 0);
  return modal;
}
