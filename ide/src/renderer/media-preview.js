import { t } from './i18n.js';

/* Vorschau für Bilder und Klänge in der IDE.

   Beide Ansichten leben in einem eigenen DOM-Knoten, der beim Tab-Wechsel nur
   aus- und wieder eingehängt wird. So behält ein laufender Song seinen Zustand,
   solange der Tab offen bleibt. */

const IMAGE_EXTENSIONS = new Set(['png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp', 'svg']);
const AUDIO_EXTENSIONS = new Set(['wav', 'ogg', 'oga', 'mp3', 'm4a', 'flac', 'opus']);

const ZOOM_MIN = 0.02;
const ZOOM_MAX = 32;
const WAVE_BUCKETS = 512;

export function mediaKind(extension) {
  const ext = String(extension || '').replace(/^\./, '').toLowerCase();
  if (IMAGE_EXTENSIONS.has(ext)) return 'image';
  if (AUDIO_EXTENSIONS.has(ext)) return 'audio';
  return '';
}

export function mediaUrl(dir, rel) {
  return `/api/file/raw?dir=${encodeURIComponent(dir)}&file=${encodeURIComponent(rel)}`;
}

function formatBytes(bytes) {
  const value = Math.max(0, Number(bytes) || 0);
  if (value < 1024) return `${value} B`;
  if (value < 1024 * 1024) return `${(value / 1024).toFixed(value < 10240 ? 1 : 0)} KB`;
  return `${(value / 1024 / 1024).toFixed(1)} MB`;
}

export function formatTime(seconds) {
  const value = Number(seconds);
  if (!Number.isFinite(value) || value < 0) return '0:00';
  const total = Math.floor(value);
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const secs = total % 60;
  const pad = (n) => String(n).padStart(2, '0');
  return hours ? `${hours}:${pad(minutes)}:${pad(secs)}` : `${minutes}:${pad(secs)}`;
}

async function remoteSize(url) {
  try {
    const response = await fetch(url, { method: 'HEAD' });
    if (!response.ok) return 0;
    return Number(response.headers.get('content-length')) || 0;
  } catch {
    return 0;
  }
}

function fileExtension(rel) {
  return String(rel.split('.').pop() || '').toUpperCase();
}

function formatRate(rate) {
  if (!rate) return '';
  const khz = rate / 1000;
  return `${Number.isInteger(khz) ? khz : khz.toFixed(2).replace(/0$/, '')} kHz`;
}

/** Echte Abtastrate aus dem WAV-Kopf – der Decoder resampelt auf die Gerätefrequenz. */
function readWavInfo(buffer) {
  try {
    const view = new DataView(buffer);
    if (view.byteLength < 44) return null;
    const text = (offset) =>
      String.fromCharCode(view.getUint8(offset), view.getUint8(offset + 1), view.getUint8(offset + 2), view.getUint8(offset + 3));
    if (text(0) !== 'RIFF' || text(8) !== 'WAVE') return null;
    return { channels: view.getUint16(22, true), sampleRate: view.getUint32(24, true) };
  } catch {
    return null;
  }
}

/* ------------------------------------------------------------------ */
/* Bilder                                                              */
/* ------------------------------------------------------------------ */

function imageView({ rel, url, onReveal }) {
  const el = document.createElement('div');
  el.className = 'pv pv-image';
  el.innerHTML = `
    <div class="pv-bar">
      <button type="button" class="pv-btn" data-act="zoom-out" title="${t('Verkleinern')}">−</button>
      <button type="button" class="pv-btn pv-zoom" data-act="actual" title="${t('Originalgröße')}">100 %</button>
      <button type="button" class="pv-btn" data-act="zoom-in" title="${t('Vergrößern')}">+</button>
      <span class="pv-divider"></span>
      <button type="button" class="pv-btn" data-act="fit">${t('Einpassen')}</button>
      <span class="pv-meta"></span>
      <span class="pv-spacer"></span>
      <button type="button" class="pv-btn" data-act="reveal">${t('Im Explorer zeigen')}</button>
    </div>
    <div class="pv-stage">
      <div class="pv-canvas">
        <img class="pv-img" alt="" draggable="false">
      </div>
      <div class="pv-note" hidden></div>
    </div>`;

  const stage = el.querySelector('.pv-stage');
  const img = el.querySelector('.pv-img');
  const zoomLabel = el.querySelector('.pv-zoom');
  const meta = el.querySelector('.pv-meta');
  const note = el.querySelector('.pv-note');
  const revealButton = el.querySelector('[data-act="reveal"]');
  if (!onReveal) revealButton.hidden = true;

  let natural = { width: 0, height: 0 };
  let zoom = 1;
  let fitted = true;
  let dragging = null;

  function applyZoom() {
    if (!natural.width) return;
    img.style.width = `${Math.max(1, natural.width * zoom)}px`;
    img.style.height = `${Math.max(1, natural.height * zoom)}px`;
    // Pixelgrafik soll beim Vergrößern scharf bleiben.
    img.style.imageRendering = zoom >= 1 ? 'pixelated' : 'auto';
    zoomLabel.textContent = `${Math.round(zoom * 100)} %`;
  }

  function fitToStage() {
    if (!natural.width) return;
    const rect = stage.getBoundingClientRect();
    if (!rect.width || !rect.height) return;
    const scale = Math.min((rect.width - 40) / natural.width, (rect.height - 40) / natural.height);
    // Kleine Sprites dürfen wachsen, aber nicht ins Endlose.
    zoom = Math.min(16, Math.max(ZOOM_MIN, Number.isFinite(scale) && scale > 0 ? scale : 1));
    fitted = true;
    applyZoom();
  }

  function setZoom(next, anchor) {
    const previous = zoom;
    zoom = Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, next));
    if (Math.abs(zoom - previous) < 1e-6) return;
    fitted = false;
    const before = anchor
      ? {
          x: anchor.clientX - stage.getBoundingClientRect().left + stage.scrollLeft,
          y: anchor.clientY - stage.getBoundingClientRect().top + stage.scrollTop
        }
      : null;
    applyZoom();
    if (before && anchor) {
      const ratio = zoom / previous;
      const rect = stage.getBoundingClientRect();
      stage.scrollLeft = before.x * ratio - (anchor.clientX - rect.left);
      stage.scrollTop = before.y * ratio - (anchor.clientY - rect.top);
    }
  }

  function describe() {
    if (!natural.width) return;
    const parts = [`${natural.width} × ${natural.height} px`, fileExtension(rel)];
    meta.textContent = parts.join(' · ');
    remoteSize(url).then((size) => {
      if (size) meta.textContent = `${natural.width} × ${natural.height} px · ${formatBytes(size)} · ${fileExtension(rel)}`;
    });
  }

  img.addEventListener('load', () => {
    natural = { width: img.naturalWidth, height: img.naturalHeight };
    note.hidden = true;
    fitToStage();
    describe();
  });
  img.addEventListener('error', () => {
    note.textContent = t('Das Bild konnte nicht geladen werden.');
    note.hidden = false;
  });
  img.src = url;

  el.querySelector('[data-act="zoom-in"]').addEventListener('click', () => setZoom(zoom * 1.25));
  el.querySelector('[data-act="zoom-out"]').addEventListener('click', () => setZoom(zoom / 1.25));
  el.querySelector('[data-act="actual"]').addEventListener('click', () => {
    setZoom(1);
    stage.scrollTo({ left: 0, top: 0, behavior: 'smooth' });
  });
  el.querySelector('[data-act="fit"]').addEventListener('click', fitToStage);
  revealButton.addEventListener('click', () => onReveal?.());

  stage.addEventListener(
    'wheel',
    (event) => {
      if (!natural.width) return;
      event.preventDefault();
      setZoom(zoom * (event.deltaY < 0 ? 1.15 : 1 / 1.15), event);
    },
    { passive: false }
  );

  // Ziehen verschiebt den Ausschnitt – wie in einem Bildbetrachter.
  stage.addEventListener('pointerdown', (event) => {
    if (event.button !== 0) return;
    if (event.target.closest('.pv-note')) return;
    dragging = {
      x: event.clientX,
      y: event.clientY,
      left: stage.scrollLeft,
      top: stage.scrollTop
    };
    stage.classList.add('panning');
    stage.setPointerCapture(event.pointerId);
  });
  stage.addEventListener('pointermove', (event) => {
    if (!dragging) return;
    stage.scrollLeft = dragging.left - (event.clientX - dragging.x);
    stage.scrollTop = dragging.top - (event.clientY - dragging.y);
  });
  const endDrag = (event) => {
    if (!dragging) return;
    dragging = null;
    stage.classList.remove('panning');
    try {
      stage.releasePointerCapture(event.pointerId);
    } catch {
      /* Zeiger war bereits frei. */
    }
  };
  stage.addEventListener('pointerup', endDrag);
  stage.addEventListener('pointercancel', endDrag);

  const observer = new ResizeObserver(() => {
    if (fitted) fitToStage();
  });
  observer.observe(stage);

  return {
    el,
    dispose() {
      observer.disconnect();
      img.removeAttribute('src');
    },
    suspend() {}
  };
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

let sharedContext = null;

function audioContext() {
  const Ctor = window.AudioContext || window.webkitAudioContext;
  if (!Ctor) return null;
  if (!sharedContext || sharedContext.state === 'closed') sharedContext = new Ctor();
  return sharedContext;
}

function computePeaks(buffer, buckets) {
  const channels = [];
  for (let c = 0; c < buffer.numberOfChannels; c++) channels.push(buffer.getChannelData(c));
  const peaks = new Float32Array(buckets);
  const step = buffer.length / buckets;
  for (let i = 0; i < buckets; i++) {
    const start = Math.floor(i * step);
    const end = Math.min(buffer.length, Math.floor((i + 1) * step));
    const stride = Math.max(1, Math.floor((end - start) / 96));
    let peak = 0;
    for (const data of channels) {
      for (let s = start; s < end; s += stride) {
        const value = Math.abs(data[s]);
        if (value > peak) peak = value;
      }
    }
    peaks[i] = peak;
  }
  let max = 0;
  for (const value of peaks) if (value > max) max = value;
  if (max > 0) for (let i = 0; i < peaks.length; i++) peaks[i] /= max;
  return peaks;
}

function drawWaveform(canvas, peaks, progress) {
  const context = canvas.getContext('2d');
  if (!context) return;
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.round(rect.width * dpr));
  const height = Math.max(1, Math.round(rect.height * dpr));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  context.clearRect(0, 0, width, height);
  const center = height / 2;
  if (!peaks) {
    context.strokeStyle = 'rgba(138, 151, 168, .35)';
    context.lineWidth = Math.max(1, dpr);
    context.beginPath();
    context.moveTo(0, center);
    context.lineTo(width, center);
    context.stroke();
    return;
  }
  const barWidth = width / peaks.length;
  const active = progress * peaks.length;
  for (let i = 0; i < peaks.length; i++) {
    const barHeight = Math.max(1.5 * dpr, peaks[i] * height * 0.94);
    context.fillStyle = i < active ? '#a6e22e' : 'rgba(108, 182, 255, .38)';
    context.fillRect(i * barWidth, center - barHeight / 2, Math.max(1, barWidth - dpr * 0.7), barHeight);
  }
}

function audioView({ rel, url }) {
  const el = document.createElement('div');
  el.className = 'pv pv-audio';
  el.innerHTML = `
    <div class="pv-audio-inner">
      <div class="pv-audio-head">
        <div class="pv-audio-badge">♪</div>
        <div class="pv-audio-titles">
          <div class="pv-audio-name"></div>
          <div class="pv-audio-sub"></div>
        </div>
      </div>
      <div class="pv-wave-wrap">
        <canvas class="pv-wave"></canvas>
        <div class="pv-playhead"></div>
        <div class="pv-note" hidden></div>
      </div>
      <div class="pv-controls">
        <button type="button" class="pv-play" data-act="play" title="${t('Abspielen')}">▶</button>
        <span class="pv-time pv-current">0:00</span>
        <input class="pv-seek" type="range" min="0" max="1000" value="0" step="1" aria-label="${t('Position')}">
        <span class="pv-time pv-duration">0:00</span>
        <button type="button" class="pv-btn pv-loop" data-act="loop" title="${t('Wiederholen')}">↻</button>
        <span class="pv-volume-icon" title="${t('Lautstärke')}">🔈</span>
        <input class="pv-volume" type="range" min="0" max="100" value="80" step="1" aria-label="${t('Lautstärke')}">
      </div>
    </div>`;

  const nameEl = el.querySelector('.pv-audio-name');
  const subEl = el.querySelector('.pv-audio-sub');
  const wave = el.querySelector('.pv-wave');
  const playhead = el.querySelector('.pv-playhead');
  const playButton = el.querySelector('[data-act="play"]');
  const loopButton = el.querySelector('[data-act="loop"]');
  const seek = el.querySelector('.pv-seek');
  const volume = el.querySelector('.pv-volume');
  const currentEl = el.querySelector('.pv-current');
  const durationEl = el.querySelector('.pv-duration');
  const note = el.querySelector('.pv-note');

  nameEl.textContent = rel.split('/').pop();
  nameEl.title = rel;

  const audio = new Audio();
  // Erst beim ersten Anzeigen laden – beim Wiederherstellen vieler Tabs soll
  // nicht sofort jede Datei angefasst werden.
  audio.preload = 'none';
  audio.src = url;

  const VOLUME_KEY = 'gbxstudio.preview.volume';
  let storedVolume = 0.8;
  try {
    const value = Number(localStorage.getItem(VOLUME_KEY));
    if (Number.isFinite(value) && value >= 0 && value <= 1) storedVolume = value;
  } catch {
    /* Speicher nicht verfügbar */
  }
  audio.volume = storedVolume;
  volume.value = String(Math.round(storedVolume * 100));

  let peaks = null;
  let detail = { sampleRate: 0, channels: 0, duration: 0 };
  let progress = 0;
  let lastBucket = -1;
  let frame = 0;
  let seeking = false;

  function draw() {
    drawWaveform(wave, peaks, progress);
  }

  function updateSubtitle() {
    const duration = Number.isFinite(audio.duration) && audio.duration > 0 ? audio.duration : detail.duration;
    const parts = [fileExtension(rel)];
    const rate = formatRate(detail.sampleRate);
    if (rate) parts.push(rate);
    if (detail.channels) parts.push(detail.channels === 1 ? t('Mono') : t('Stereo'));
    if (duration) parts.push(formatTime(duration));
    remoteSize(url).then((size) => {
      if (size) subEl.textContent = [...parts, formatBytes(size)].join(' · ');
      else subEl.textContent = parts.join(' · ');
    });
    subEl.textContent = parts.join(' · ');
  }

  function updateProgress() {
    const duration = Number.isFinite(audio.duration) && audio.duration > 0 ? audio.duration : detail.duration;
    progress = duration ? Math.min(1, audio.currentTime / duration) : 0;
    playhead.style.left = `${progress * 100}%`;
    currentEl.textContent = formatTime(audio.currentTime);
    durationEl.textContent = formatTime(duration);
    if (!seeking) seek.value = String(Math.round(progress * 1000));
    const bucket = Math.round(progress * WAVE_BUCKETS);
    if (bucket !== lastBucket) {
      lastBucket = bucket;
      draw();
    }
  }

  function tick() {
    updateProgress();
    if (!audio.paused && !audio.ended) frame = requestAnimationFrame(tick);
    else frame = 0;
  }

  function startLoop() {
    if (!frame) frame = requestAnimationFrame(tick);
  }

  function setPlaying(playing) {
    playButton.textContent = playing ? '❚❚' : '▶';
    playButton.title = playing ? t('Pause') : t('Abspielen');
    el.classList.toggle('playing', playing);
  }

  audio.addEventListener('loadedmetadata', () => {
    detail.duration = Number.isFinite(audio.duration) ? audio.duration : 0;
    updateSubtitle();
    updateProgress();
  });
  audio.addEventListener('play', () => {
    setPlaying(true);
    startLoop();
  });
  audio.addEventListener('pause', () => {
    setPlaying(false);
    updateProgress();
  });
  audio.addEventListener('ended', () => {
    setPlaying(false);
    updateProgress();
  });
  audio.addEventListener('error', () => {
    note.textContent = t('Dieses Format kann die Vorschau nicht abspielen.');
    note.hidden = false;
  });

  playButton.addEventListener('click', () => {
    if (audio.paused) audio.play().catch(() => {});
    else audio.pause();
  });

  loopButton.addEventListener('click', () => {
    audio.loop = !audio.loop;
    loopButton.classList.toggle('active', audio.loop);
  });

  volume.addEventListener('input', () => {
    const value = Number(volume.value) / 100;
    audio.volume = value;
    try {
      localStorage.setItem(VOLUME_KEY, String(value));
    } catch {
      /* Speicher nicht verfügbar */
    }
  });

  const seekToPointer = (event) => {
    const rect = wave.getBoundingClientRect();
    if (!rect.width) return;
    const ratio = Math.min(1, Math.max(0, (event.clientX - rect.left) / rect.width));
    const duration = Number.isFinite(audio.duration) && audio.duration > 0 ? audio.duration : detail.duration;
    if (duration) {
      audio.currentTime = ratio * duration;
      progress = ratio;
      lastBucket = -1;
      updateProgress();
    }
  };

  wave.addEventListener('pointerdown', (event) => {
    if (event.button !== 0) return;
    seeking = true;
    wave.setPointerCapture(event.pointerId);
    wave.classList.add('scrubbing');
    seekToPointer(event);
  });
  wave.addEventListener('pointermove', (event) => {
    if (!seeking) return;
    seekToPointer(event);
  });
  const endSeek = (event) => {
    if (!seeking) return;
    seeking = false;
    wave.classList.remove('scrubbing');
    try {
      wave.releasePointerCapture(event.pointerId);
    } catch {
      /* Zeiger war bereits frei. */
    }
    updateProgress();
  };
  wave.addEventListener('pointerup', endSeek);
  wave.addEventListener('pointercancel', endSeek);

  seek.addEventListener('pointerdown', () => {
    seeking = true;
  });
  seek.addEventListener('input', () => {
    const duration = Number.isFinite(audio.duration) && audio.duration > 0 ? audio.duration : detail.duration;
    if (duration) audio.currentTime = (Number(seek.value) / 1000) * duration;
  });
  seek.addEventListener('change', () => {
    seeking = false;
    updateProgress();
  });

  const observer = new ResizeObserver(() => draw());
  observer.observe(wave);

  // Wellenform im Hintergrund dekodieren; schlägt das fehl (z. B. bei
  // exotischen Formaten), bleibt eine ruhige Linie stehen.
  let waveStarted = false;
  function startWaveform() {
    if (waveStarted) return;
    waveStarted = true;
    audio.preload = 'metadata';
    audio.load();
    (async () => {
      try {
        const response = await fetch(url);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const data = await response.arrayBuffer();
        const context = audioContext();
        if (!context) throw new Error('Kein AudioContext');
        const decoded = await context.decodeAudioData(data.slice(0));
        const header = readWavInfo(data);
        detail = {
          sampleRate: header?.sampleRate || 0,
          channels: header?.channels || decoded.numberOfChannels,
          duration: decoded.duration
        };
        peaks = computePeaks(decoded, WAVE_BUCKETS);
        lastBucket = -1;
        draw();
        updateSubtitle();
      } catch {
        /* Ohne Wellenform bleibt die Wiedergabe möglich. */
      }
    })();
  }

  updateSubtitle();
  draw();

  return {
    el,
    activate: startWaveform,
    dispose() {
      observer.disconnect();
      cancelAnimationFrame(frame);
      frame = 0;
      audio.pause();
      audio.removeAttribute('src');
      audio.load();
    },
    suspend() {
      audio.pause();
    }
  };
}

/** Erzeugt die passende Vorschau für eine Datei. */
export function createMediaView({ kind, rel, dir, onReveal }) {
  const url = mediaUrl(dir, rel);
  return kind === 'image' ? imageView({ rel, url, onReveal }) : audioView({ rel, url });
}
