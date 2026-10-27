/* Bibliothek bauen – C++-Quellen des Projekts zu einer statischen Bibliothek
   (.a) bündeln und den Eintrag in [native] der Projektdatei setzen.

   Der Server macht die Arbeit (g++ -c, dann ar rcs) und meldet jede Zeile des
   Compilers; hier stehen nur Auswahl, Name, Ziel und das Protokoll. */

function esc(value) {
  return String(value ?? '').replace(/[&<>"']/g, (c) => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;'
  })[c]);
}

/** Dateiname wie im Server (library.mjs): lib<name>.a ohne doppeltes lib. */
function archiveName(name) {
  const clean = String(name || '')
    .trim()
    .replace(/^lib/i, '')
    .replace(/\.a$/i, '')
    .replace(/[^\w.+-]/g, '_')
    .replace(/^_+|_+$/g, '');
  return `lib${clean || 'mylib'}.a`;
}

function formatSize(bytes) {
  const size = Number(bytes) || 0;
  if (size <= 0) return '';
  if (size < 1024) return `${size} B`;
  if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KB`;
  return `${(size / (1024 * 1024)).toFixed(1)} MB`;
}

export function openLibraryTool({
  t,
  openModal,
  toast,
  get,
  post,
  subscribeJob,
  projectDir,
  onAdded
}) {
  const dir = projectDir();
  if (!dir) {
    toast(t('Bitte zuerst ein Projekt öffnen.'), 'warn');
    return null;
  }

  const body = document.createElement('div');
  body.className = 'lib-tool';
  body.innerHTML = `
    <p class="lib-intro">${t('Ausgewählte C++-Dateien des Projekts werden übersetzt und zu einer statischen Bibliothek gebündelt. Der Eintrag landet in [native] der Projektdatei; die INLINE-Blöcke linken die Bibliothek dann mit.')}</p>
    <div class="lib-status" id="lib-status"></div>
    <div class="lib-sources">
      <div class="lib-head">
        <span>${t('Quellen')}</span>
        <button type="button" class="btn ghost" id="lib-all">${t('Alle')}</button>
        <button type="button" class="btn ghost" id="lib-none">${t('Keine')}</button>
      </div>
      <div class="lib-list" id="lib-list"><div class="lib-empty">${t('Lade …')}</div></div>
      <div class="hint" id="lib-note"></div>
    </div>
    <div class="row">
      <div class="field">
        <label>${t('Name')}</label>
        <input type="text" id="lib-name" value="mylib" spellcheck="false">
      </div>
      <div class="field">
        <label>${t('Ziel')}</label>
        <input type="text" id="lib-target" value="lib/libmylib.a" spellcheck="false">
        <div class="hint">${t('Relativ zum Projektordner.')}</div>
      </div>
    </div>
    <label class="check lib-check"><input type="checkbox" id="lib-native" checked> ${t('In [native] eintragen')}</label>
    <pre class="lib-log" id="lib-log"></pre>
  `;

  const list = body.querySelector('#lib-list');
  const note = body.querySelector('#lib-note');
  const status = body.querySelector('#lib-status');
  const logView = body.querySelector('#lib-log');
  const nameField = body.querySelector('#lib-name');
  const targetField = body.querySelector('#lib-target');
  const nativeBox = body.querySelector('#lib-native');
  let sources = [];
  let targetEdited = false;
  let building = false;

  const appendLog = (text) => {
    logView.textContent += `${text}\n`;
    logView.scrollTop = logView.scrollHeight;
  };

  const render = () => {
    if (!sources.length) {
      list.innerHTML = `<div class="lib-empty">${t('Keine C++-Dateien im Projekt gefunden.')}</div>`;
      note.textContent = '';
      return;
    }
    list.innerHTML = sources
      .map(
        (item) => `<label class="lib-row">
          <input type="checkbox" data-path="${esc(item.path)}" ${item.language === 'c' ? '' : 'checked'}>
          <span class="p">${esc(item.path)}</span>
          <span class="kind">${item.language === 'c' ? 'C' : 'C++'}</span>
          <span class="size">${formatSize(item.size)}</span>
        </label>`
      )
      .join('');
    // Das mitgelieferte g++ hat kein C-Frontend, deshalb geht .c als C++ durch.
    note.textContent = sources.some((item) => item.language === 'c')
      ? t('C-Dateien werden als C++ übersetzt; der Header darf dann kein extern "C" verlangen.')
      : '';
  };

  const setTarget = () => {
    if (targetEdited) return;
    targetField.value = `lib/${archiveName(nameField.value)}`;
  };

  nameField.addEventListener('input', setTarget);
  targetField.addEventListener('input', () => {
    targetEdited = true;
  });
  body.querySelector('#lib-all').addEventListener('click', () => {
    for (const box of list.querySelectorAll('input[type=checkbox]')) box.checked = true;
  });
  body.querySelector('#lib-none').addEventListener('click', () => {
    for (const box of list.querySelectorAll('input[type=checkbox]')) box.checked = false;
  });

  const load = async () => {
    try {
      const data = await get(`/api/library/sources?dir=${encodeURIComponent(dir)}`);
      sources = data.sources || [];
      render();
      const cxx = data.cxx || {};
      if (!cxx.ok) {
        status.textContent = cxx.error || t('Kein C++-Compiler gefunden. Er lässt sich in den Einstellungen nachladen.');
        status.classList.add('bad');
      } else {
        status.textContent = t('Compiler: g++ {version}', { version: cxx.version || '' }).trim();
      }
      return cxx.ok;
    } catch (error) {
      list.innerHTML = `<div class="lib-empty">${esc(error.message)}</div>`;
      return false;
    }
  };
  load();

  const build = async () => {
    if (building) return false;
    const picked = [...list.querySelectorAll('input[type=checkbox]:checked')].map((box) => box.dataset.path);
    if (!picked.length) {
      toast(t('Bitte mindestens eine Quelldatei wählen.'), 'warn');
      return false;
    }
    if (!targetField.value.trim()) {
      toast(t('Bitte ein Ziel angeben.'), 'warn');
      return false;
    }
    building = true;
    logView.textContent = '';
    try {
      const result = await post('/api/library/build', {
        projectDir: dir,
        sources: picked,
        name: nameField.value.trim(),
        target: targetField.value.trim(),
        addToNative: nativeBox.checked
      });
      const done = await new Promise((resolve) => {
        subscribeJob(
          result.jobId,
          (event) => {
            if (event.type === 'log') appendLog(event.text);
          },
          resolve
        );
      });
      building = false;
      if (done.ok) {
        appendLog(`${t('Fertig')}: ${done.target} (${formatSize(done.size) || `${done.objects} ${t('Objekte')}`})`);
        toast(t('Bibliothek gebaut: {name}', { name: done.target }), 'ok');
        onAdded?.(done.nativeLibs || null, done.target);
      } else {
        toast(done.error || t('Die Bibliothek ließ sich nicht bauen.'), 'error');
      }
    } catch (error) {
      building = false;
      toast(error.message, 'error');
    }
    // Dialog offen lassen: das Protokoll bleibt so sichtbar.
    return false;
  };

  return openModal({
    title: t('Bibliothek bauen'),
    subtitle: dir,
    body,
    footer: [
      { label: t('Schließen'), action: () => true },
      // Der Dialog bleibt nach dem Bauen offen: das Protokoll bleibt so lesbar.
      { label: t('Bauen'), variant: 'primary', action: build }
    ]
  });
}
