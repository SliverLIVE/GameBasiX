// Ende-zu-Ende-Test: startet den IDE-Server, legt ein Projekt an, prüft die
// Syntax, provoziert einen Fehler und führt ein kurzes Programm aus.
//
// Aufruf: npm test
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const work = await fsp.mkdtemp(path.join(os.tmpdir(), 'gbx-studio-test-'));
process.env.GBXIDE_HOME = path.join(work, 'home');

const { startServer } = await import('../src/server/server.mjs');
const {
  googleFontCssUrl,
  googleFontFileName,
  parseGoogleFontTtfUrl,
  parseMetadata,
  parseWebfonts,
  publicFamilies
} = await import('../src/server/googlefonts.mjs');
const server = await startServer({ port: 0 });

async function request(pathname, body, method = 'GET') {
  const response = await fetch(server.url.replace(/\/$/, '') + pathname, {
    method,
    headers: body ? { 'Content-Type': 'application/json' } : undefined,
    body: body ? JSON.stringify(body) : undefined
  });
  const data = await response.json();
  if (!response.ok || data.ok === false) throw new Error(data.error || `HTTP ${response.status}`);
  return data;
}

async function job(pathname, body) {
  const { jobId } = await request(pathname, body, 'POST');
  const response = await fetch(`${server.url.replace(/\/$/, '')}/api/jobs/${jobId}/events`);
  const reader = response.body.getReader();
  const decoder = new TextDecoder();
  let buffer = '';
  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    buffer += decoder.decode(value, { stream: true });
    if (buffer.includes('"type":"done"')) break;
  }
  reader.cancel().catch(() => {});
  const events = buffer
    .split('\n')
    .filter((line) => line.startsWith('data: '))
    .map((line) => JSON.parse(line.slice(6)));
  return { events, done: events.find((event) => event.type === 'done') || {} };
}

/** Liest die Ereignisse eines laufenden Auftrags bis zum Abschluss. */
async function eventsUntilDone(jobId) {
  const response = await fetch(`${server.url.replace(/\/$/, '')}/api/jobs/${jobId}/events`);
  const reader = response.body.getReader();
  const decoder = new TextDecoder();
  const events = [];
  let buffer = '';
  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    buffer += decoder.decode(value, { stream: true });
    let index;
    while ((index = buffer.indexOf('\n\n')) >= 0) {
      const raw = buffer.slice(0, index);
      buffer = buffer.slice(index + 2);
      const line = raw.split('\n').find((item) => item.startsWith('data: '));
      if (!line) continue;
      const event = JSON.parse(line.slice(6));
      events.push(event);
      if (event.type === 'done') {
        reader.cancel().catch(() => {});
        return events;
      }
    }
  }
  reader.cancel().catch(() => {});
  return events;
}

let failures = 0;
async function check(name, run) {
  try {
    await run();
    console.log(`  ok   ${name}`);
  } catch (error) {
    failures++;
    console.error(`  FAIL ${name}\n       ${error.message}`);
  }
}

try {
  console.log(`GBX Studio – Test (${work})`);

  const health = await request('/api/health');
  await check('Interpreter gefunden', () => {
    assert.equal(health.gbx.ok, true, health.gbx.error);
    assert.match(health.gbx.version, /\d/);
  });
  if (!health.gbx.ok) throw new Error('Ohne gbx.exe sind Prüfen und Starten nicht testbar.');

  await check('C++-Compiler wird geprüft und ist nachladbar', async () => {
    const cxx = await request('/api/compiler/check', {}, 'POST');
    assert.equal(typeof cxx.canDownload, 'boolean', 'canDownload fehlt');
    assert.ok(cxx.path, 'kein Pfad');
    assert.ok(cxx.resolved, 'kein aufgelöster Pfad');
    if (cxx.ok) assert.match(cxx.version, /^\d+\.\d+/, 'Version fehlt');
    else assert.ok(cxx.error, 'keine Fehlermeldung');
  });

  await check('Datensatz deckt den Befehlssatz ab', () => {
    const ideRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
    const dataset = JSON.parse(fs.readFileSync(path.join(ideRoot, 'data', 'gbx-dataset.json'), 'utf8'));
    const source = fs.readFileSync(path.join(ideRoot, '..', 'src', 'builtins.cpp'), 'utf8');
    const builtins = new Set([...source.matchAll(/add\("([^"]+)"/g)].map((match) => match[1].toUpperCase()));
    const entries = new Map(dataset.commands.map((entry) => [entry.name.toUpperCase(), entry]));

    assert.equal(builtins.size, 219, 'Interpreter-Befehle');
    for (const name of builtins) assert.ok(entries.has(name), `${name} fehlt im Datensatz`);
    // Steuerwörter, die früher in der Vervollständigung fehlten.
    for (const keyword of ['INCLUDE', 'IF', 'ENDIF', 'SWAP', 'INC', 'DEC', 'TRY', 'CATCH', 'FINALLY', 'ENDTRY', 'THROW', 'STATIC']) {
      assert.equal(entries.get(keyword)?.kind, 'keyword', `${keyword} ist kein Schlüsselwort`);
    }
    // IIF steht nicht in der Befehlsliste (nur ein Zweig wird ausgewertet),
    // gehört aber zur Sprache und muss mit Signatur erscheinen.
    const iif = entries.get('IIF');
    assert.equal(iif?.kind, 'function', 'IIF fehlt');
    assert.equal(iif.params.length, 3);
    // INLINE ist ein Schlüsselwort der Sprache (C++-Blöcke), kein Befehl.
    assert.equal(entries.get('INLINE')?.kind, 'keyword', 'INLINE fehlt');
    assert.equal(entries.get('ENDINLINE')?.kind, 'keyword', 'ENDINLINE fehlt');
    // Befehle ohne Funktion dürfen im Datensatz nicht auftauchen.
    assert.ok(!entries.has('IMPORT'), 'IMPORT gibt es in GBX nicht');
    // Argumentzahl folgt dem Interpreter (INPUT var[,x[,y]]).
    const input = entries.get('INPUT');
    assert.equal(input.params.length, 3);
    assert.deepEqual(input.params.map((param) => param.optional), [false, true, true]);
    // Funktionen werden mit Klammern aufgerufen, Befehle ohne.
    assert.equal(entries.get('ABS').kind, 'function');
    assert.equal(entries.get('PRINT').kind, 'command');
  });

  await check('Google-Fonts-Liste der Webfonts-API', () => {
    const families = parseWebfonts({
      items: [
        {
          family: 'Roboto',
          category: 'sans-serif',
          variants: ['regular', 'italic', '700', '700italic'],
          files: {
            regular: 'https://fonts.gstatic.com/regular.ttf',
            italic: 'https://fonts.gstatic.com/italic.ttf',
            700: 'https://fonts.gstatic.com/bold.ttf',
            '700italic': 'https://fonts.gstatic.com/bolditalic.ttf'
          }
        }
      ]
    });
    assert.equal(families.length, 1);
    assert.equal(families[0].category, 'Sans Serif');
    assert.deepEqual(families[0].weights, [400, 700]);
    assert.equal(families[0].italic, true);
    assert.equal(families[0].files['700i'], 'https://fonts.gstatic.com/bolditalic.ttf');
    // Die Oberfläche bekommt keine direkten TTF-Adressen.
    assert.deepEqual(Object.keys(publicFamilies(families)[0]).sort(), [
      'category', 'italic', 'name', 'popularity', 'weights'
    ]);
  });

  await check('Öffentliche Fontliste wird gelesen und sortiert', () => {
    const families = parseMetadata({
      familyMetadataList: [
        { family: 'B Font', category: 'Serif', popularity: 20, fonts: { '400': {}, '400i': {} } },
        { family: 'A Font', category: 'Display', popularity: 10, fonts: { '100': {}, '900': {} } }
      ]
    });
    assert.deepEqual(families.map((family) => family.name), ['A Font', 'B Font']);
    assert.deepEqual(families[0].weights, [100, 900]);
    assert.equal(families[1].italic, true);
  });

  await check('TrueType-Adresse aus der CSS-Antwort', () => {
    const css = "@font-face {\n  src: url(https://fonts.gstatic.com/s/roboto/v51/abc.ttf) format('truetype');\n}";
    assert.equal(parseGoogleFontTtfUrl(css), 'https://fonts.gstatic.com/s/roboto/v51/abc.ttf');
    assert.equal(parseGoogleFontTtfUrl('kein @font-face'), '');
    assert.equal(
      googleFontCssUrl('Roboto Flex', { weight: 700, style: 'italic' }),
      'https://fonts.googleapis.com/css2?family=Roboto+Flex:ital,wght@1,700&display=swap'
    );
  });

  await check('Schriftdatei bekommt einen sprechenden Namen', () => {
    assert.equal(googleFontFileName('Roboto', { weight: 400 }), 'Roboto-Regular.ttf');
    assert.equal(
      googleFontFileName('Roboto Flex', { weight: 700, style: 'italic' }),
      'Roboto-Flex-BoldItalic.ttf'
    );
    assert.equal(googleFontFileName('Noto Sans JP', { weight: 900 }), 'Noto-Sans-JP-Black.ttf');
  });

  await check('Icon wird ausgeliefert', async () => {
    const base = server.url.replace(/\/$/, '');
    for (const [pathname, type] of [['/favicon.ico', 'image/x-icon'],
                                    ['/assets/icon-256.png', 'image/png']]) {
      const response = await fetch(base + pathname);
      assert.equal(response.status, 200, pathname);
      assert.equal(response.headers.get('content-type'), type, pathname);
      assert.ok((await response.arrayBuffer()).byteLength > 1000, `${pathname} ist leer`);
    }
  });

  const created = await request('/api/project/create', {
    path: work,
    name: 'Smoke',
    template: 'hello'
  }, 'POST');
  const dir = created.dir;

  await check('Projektdatei angelegt', () => {
    assert.ok(fs.existsSync(path.join(dir, 'Smoke.gbxproj')));
    assert.equal(created.project.entry, 'main.gbx');
  });

  const opened = await request('/api/project/open', { path: dir }, 'POST');
  await check('Projekt geöffnet', () => {
    assert.equal(opened.project.name, 'Smoke');
    assert.deepEqual(opened.project.sources, ['main.gbx']);
  });

  await check('Befehle aus GBX_INLINE_FN werden gesammelt', async () => {
    await request(
      '/api/file',
      {
        dir,
        file: 'curve.h',
        content:
          'inline void roseX(const GbxValue* a, int n, GbxValue* r) {}\n' +
          'GBX_INLINE_FN("ROSE_X", 3, 3, roseX);\n' +
          'GBX_INLINE_FN_ANY("ROSE_ANY", roseX);\n' +
          '// GBX_INLINE_FN("AUSKOMMENTIERT", 0, 0, roseX);\n'
      },
      'POST'
    );
    const found = await request(`/api/inline-commands?dir=${encodeURIComponent(dir)}`);
    const byName = Object.fromEntries(found.commands.map((entry) => [entry.name, entry]));
    assert.ok(byName.ROSE_X, 'ROSE_X fehlt');
    assert.equal(byName.ROSE_X.min, 3);
    assert.equal(byName.ROSE_X.max, 3);
    assert.equal(byName.ROSE_X.file, 'curve.h');
    assert.equal(byName.ROSE_X.line, 2);
    assert.equal(byName.ROSE_ANY?.max, -1, 'GBX_INLINE_FN_ANY fehlt oder hat falsche Grenzen');
    assert.ok(!byName.AUSKOMMENTIERT, 'Kommentar wurde mitgezaehlt');
  });

  await request('/api/file', { dir, file: 'main.gbx', content: `' Smoke\nPRINT "Hallo", 10, 10\nDEBUG "MARKE" + CHR$(10)\nEND\n` }, 'POST');
  const good = await job('/api/build', { projectDir: dir, project: opened.project });
  await check('Syntaxprüfung erfolgreich', () => {
    assert.equal(good.done.ok, true, good.done.error);
  });

  await request('/api/file', { dir, file: 'main.gbx', content: `' Smoke\nIF THEN\n` }, 'POST');
  const bad = await job('/api/build', { projectDir: dir, project: opened.project });
  await check('Syntaxfehler wird gemeldet', () => {
    assert.equal(bad.done.ok, false);
    const diagnostic = bad.events.find((event) => event.type === 'diagnostic')?.diagnostic;
    assert.ok(diagnostic, 'keine Diagnose');
    assert.equal(diagnostic.severity, 'error');
    assert.ok(diagnostic.line > 0);
    assert.ok(diagnostic.file.endsWith('main.gbx'));
  });

  // ------------------------------------------------------------ Live-Prüfung
  await request('/api/file', { dir, file: 'main.gbx', content: `PRINT "Hallo", 0, 0\nEND\n` }, 'POST');
  const liveOk = await request('/api/check/live', {
    projectDir: dir,
    file: 'main.gbx',
    source: `PRINT "Hallo", 0, 0\nEND\n`
  }, 'POST');
  await check('Live-Prüfung ohne Fehler', () => {
    assert.equal(liveOk.available, true, liveOk.error);
    assert.equal(liveOk.passed, true);
    assert.equal(liveOk.diagnostics.length, 0);
  });

  const liveBad = await request('/api/check/live', {
    projectDir: dir,
    file: 'main.gbx',
    source: `PRINT "Hallo", 0, 0\na = = 2\nEND\n`
  }, 'POST');
  await check('Live-Prüfung meldet Fehler der ungespeicherten Fassung', () => {
    assert.equal(liveBad.available, true);
    assert.equal(liveBad.passed, false);
    assert.equal(liveBad.diagnostics.length, 1);
    assert.equal(liveBad.diagnostics[0].line, 2);
    assert.ok(liveBad.diagnostics[0].file.endsWith('main.gbx'));
    assert.equal(liveBad.diagnostics[0].context, 'a = = 2');
    assert.equal(
      fs.readFileSync(path.join(dir, 'main.gbx'), 'utf8').includes('a = = 2'),
      false,
      'Die Live-Prüfung darf die Datei nicht verändern'
    );
  });

  const liveUnknown = await request('/api/check/live', {
    projectDir: dir,
    file: 'main.gbx',
    source: 'bla\nEND\n'
  }, 'POST');
  await check('Live-Prüfung meldet unbekannte Namen', () => {
    assert.equal(liveUnknown.passed, false);
    assert.equal(liveUnknown.diagnostics.length, 1);
    assert.equal(liveUnknown.diagnostics[0].line, 1);
    // Die Meldung kommt von gbx und folgt dessen Sprache (Standard: Englisch).
    assert.match(liveUnknown.diagnostics[0].message, /^(Unbekannter Befehl|Unknown command): BLA$/);
    assert.equal(liveUnknown.diagnostics[0].column, 1);
    assert.equal(liveUnknown.diagnostics[0].tokenLength, 3);
  });

  const livePart = await request('/api/check/live', {
    projectDir: dir,
    file: 'lib.gbx',
    source: 'bla\nEND\n',
    syntaxOnly: true
  }, 'POST');
  await check('Teildateien werden nur auf Syntax geprüft', () => {
    assert.equal(livePart.passed, true, JSON.stringify(livePart.diagnostics));
    assert.equal(livePart.diagnostics.length, 0);
  });

  const liveOutside = await fetch(`${server.url.replace(/\/$/, '')}/api/check/live`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ projectDir: dir, file: '../../entwischt.gbx', source: 'PRINT 1\n' })
  });
  await check('Live-Prüfung bleibt im Projektordner', () => {
    assert.equal(liveOutside.status, 403);
  });

  await check('Sprachwechsel gilt sofort für Servermeldungen', async () => {
    const outside = async (language) => {
      await request('/api/settings', { language }, 'POST');
      const response = await fetch(`${server.url.replace(/\/$/, '')}/api/check/live`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ projectDir: dir, file: '../../entwischt.gbx', source: 'PRINT 1\n' })
      });
      return response.json();
    };
    assert.equal((await outside('de')).error, 'Der Pfad liegt außerhalb des Projekts.');
    assert.equal((await outside('en')).error, 'The path is outside the project.');
  });

  await check('Job-Logs folgen der gewählten Sprache', async () => {
    await request('/api/settings', { language: 'de' }, 'POST');
    const german = await job('/api/build', { projectDir: dir, project: opened.project });
    assert.ok(
      german.events.some((event) => event.type === 'title' && event.text.startsWith('GameBasiX Syntaxprüfung')),
      'deutscher Titel fehlt'
    );
    await request('/api/settings', { language: 'en' }, 'POST');
    const english = await job('/api/build', { projectDir: dir, project: opened.project });
    assert.ok(
      english.events.some((event) => event.type === 'title' && event.text.startsWith('GameBasiX syntax check')),
      'englischer Titel fehlt'
    );
  });

  await request('/api/file', { dir, file: 'main.gbx', content: `' Smoke\nDEBUG "FERTIG" + CHR$(10)\nEND\n` }, 'POST');
  const run = await job('/api/run', { projectDir: dir, project: opened.project, args: '--headless' });
  await check('Programm startet und endet mit 0', () => {
    assert.equal(run.done.code, 0);
    const text = run.events.filter((event) => event.type === 'log').map((event) => event.text).join('');
    assert.match(text, /FERTIG/);
  });

  // ------------------------------------------------------------ Debugger
  await request('/api/file', {
    dir,
    file: 'main.gbx',
    content: `x = 1\ny = 2\nFUNCTION add\n  total = x + y\n  RETURN total\nENDFUNCTION\nz = add()\nPRINT "z=" + z, 0, 0\nEND\n`
  }, 'POST');

  const debug = await request('/api/debug/start', {
    projectDir: dir,
    project: opened.project,
    breakpoints: [{ file: 'main.gbx', line: 5 }],
    headless: true
  }, 'POST');

  const debugEvents = [];
  let debugDone = false;
  const debugStream = await fetch(`${server.url.replace(/\/$/, '')}/api/jobs/${debug.jobId}/events`);
  const debugReader = debugStream.body.getReader();
  const debugDecoder = new TextDecoder();
  let debugBuffer = '';
  (async () => {
    while (!debugDone) {
      const { value, done } = await debugReader.read();
      if (done) break;
      debugBuffer += debugDecoder.decode(value, { stream: true });
      let index;
      while ((index = debugBuffer.indexOf('\n\n')) >= 0) {
        const raw = debugBuffer.slice(0, index);
        debugBuffer = debugBuffer.slice(index + 2);
        const line = raw.split('\n').find((item) => item.startsWith('data: '));
        if (!line) continue;
        const event = JSON.parse(line.slice(6));
        debugEvents.push(event);
        if (event.type === 'done') debugDone = true;
      }
    }
  })().catch(() => {});

  const waitFor = async (predicate, timeout = 15000) => {
    const started = Date.now();
    while (Date.now() - started < timeout) {
      const hit = debugEvents.find(predicate);
      if (hit) return hit;
      if (debugDone) break;
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    return null;
  };
  const debugCommand = (command, extra = {}) =>
    request('/api/debug/command', { jobId: debug.jobId, command, ...extra }, 'POST');

  await check('Debugger hält am Haltepunkt', async () => {
    const stop = await waitFor((event) => event.type === 'debug' && event.event.t === 'stop' && event.event.reason === 'breakpoint');
    assert.ok(stop, 'kein Haltepunkt erreicht');
    assert.equal(stop.event.line, 5);
    assert.equal(stop.event.scope.func, 'ADD');
    const locals = Object.fromEntries(stop.event.scope.locals.map((variable) => [variable.name, variable.value]));
    assert.equal(locals.TOTAL, '3');
  });

  await check('Wert-Tooltip liest Variablen im Haltepunkt', async () => {
    await debugCommand('eval', { id: 11, expression: 'TOTAL' });
    const reply = await waitFor(
      (event) => event.type === 'debug' && event.event.t === 'eval' && String(event.event.id) === '11'
    );
    assert.ok(reply, 'keine Antwort auf eval');
    assert.equal(reply.event.ok, true, reply.event.error);
    assert.equal(reply.event.detail, '3');
    assert.equal(reply.event.type, 'INTEGER');
    assert.equal(reply.event.expr, 'TOTAL');

    await debugCommand('eval', { id: 12, expression: 'GIBTESNICHT' });
    const unknown = await waitFor(
      (event) => event.type === 'debug' && event.event.t === 'eval' && String(event.event.id) === '12'
    );
    assert.ok(unknown, 'keine Antwort auf den unbekannten Namen');
    assert.equal(unknown.event.ok, false, 'unbekannter Name liefert einen Wert');
  });

  await check('Debugger geht schrittweise weiter', async () => {
    await debugCommand('next');
    const step = await waitFor((event) => event.type === 'debug' && event.event.t === 'stop' && event.event.reason === 'step');
    assert.ok(step, 'kein Einzelschritt');
    assert.equal(step.event.line, 8);
    await debugCommand('run');
    const done = await waitFor((event) => event.type === 'done');
    assert.ok(done, 'Sitzung endet nicht');
    assert.equal(done.code, 0);
  });
  debugReader.cancel().catch(() => {});

  await check('Bedingter Haltepunkt greift nur bei wahrer Bedingung', async () => {
    const start = await request('/api/debug/start', {
      projectDir: dir,
      project: opened.project,
      breakpoints: [{ file: 'main.gbx', line: 5, condition: 'X = 2' }],
      headless: true
    }, 'POST');
    const events = await eventsUntilDone(start.jobId);
    const stops = events.filter(
      (event) => event.type === 'debug' && event.event.t === 'stop' && event.event.reason !== 'start'
    );
    assert.equal(stops.length, 0, 'Bedingung wurde nicht beachtet');
    assert.equal(events.find((event) => event.type === 'done')?.code, 0);
  });

  // ------------------------------------------------------------ Export
  await request('/api/file', {
    dir,
    file: 'main.gbx',
    content: `INLINE
#include <gbx_inline.h>
static void doppelt(const GbxValue* args, int argc, GbxValue* ret) {
    gbxinline::retNum(ret, gbxinline::argNum(args, argc, 0) * 2);
}
GBX_INLINE_FN("DOPPELT", 1, 1, doppelt);
ENDINLINE
ASSERT DOPPELT(21) = 42, "INLINE aus dem Paket"
END
`
  }, 'POST');

  const exported = await job('/api/export', { projectDir: dir, project: opened.project });
  await check('EXE wird erstellt', () => {
    assert.equal(exported.done.ok, true, exported.done.error);
    assert.ok(exported.done.file, 'kein Zielpfad');
    assert.ok(fs.existsSync(exported.done.file), `EXE fehlt: ${exported.done.file}`);
  });

  const looseDir = path.join(work, 'spiel');
  await fsp.mkdir(looseDir, { recursive: true });
  const looseExe = path.join(looseDir, 'Smoke.exe');
  await fsp.copyFile(exported.done.file, looseExe);
  await check('EXE läuft ohne Projektordner', () => {
    const run = spawnSync(looseExe, ['--tests'], { cwd: looseDir, encoding: 'utf8', timeout: 30000 });
    assert.equal(run.status, 0, run.stderr || run.stdout);
    assert.match(`${run.stdout}${run.stderr}`, /0 fehlgeschlagen/);
  });
} finally {
  await server.close();
  await fsp.rm(work, { recursive: true, force: true });
}

if (failures) {
  console.error(`\n${failures} Test(s) fehlgeschlagen.`);
  process.exit(1);
}
console.log('\nAlle Tests bestanden.');
