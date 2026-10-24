import { en } from './locale.en.js';

const STORAGE_KEY = 'gbxstudio.language';
const listeners = new Set();
let language = 'en';

export function getLanguage() {
  return language;
}

export function availableLanguages() {
  return [
    { id: 'de', label: 'Deutsch' },
    { id: 'en', label: 'English' }
  ];
}

/** Übersetzt einen deutschen UI-Text. Platzhalter im Format {name}. */
export function t(text, vars) {
  const template = language === 'en' ? en[text] ?? text : text;
  if (!vars) return template;
  return template.replace(/\{(\w+)\}/g, (match, key) => (vars[key] === undefined ? match : String(vars[key])));
}

/** Übersetzt statische Elemente mit data-i18n, data-i18n-title und data-i18n-placeholder. */
export function applyTranslations(root = document) {
  root.querySelectorAll('[data-i18n]').forEach((el) => {
    el.textContent = t(el.getAttribute('data-i18n'));
  });
  root.querySelectorAll('[data-i18n-title]').forEach((el) => {
    el.title = t(el.getAttribute('data-i18n-title'));
  });
  root.querySelectorAll('[data-i18n-placeholder]').forEach((el) => {
    el.setAttribute('placeholder', t(el.getAttribute('data-i18n-placeholder')));
  });
}

export function onLanguageChange(handler) {
  listeners.add(handler);
  return () => listeners.delete(handler);
}

export function setLanguage(next, { persist = true, notify = true } = {}) {
  language = next === 'de' ? 'de' : 'en';
  document.documentElement.lang = language;
  const toggle = document.getElementById('btn-lang');
  if (toggle) toggle.textContent = language.toUpperCase();
  if (persist) {
    try {
      localStorage.setItem(STORAGE_KEY, language);
    } catch {
      /* Speicher nicht verfügbar */
    }
  }
  applyTranslations();
  if (notify) {
    for (const handler of listeners) handler(language);
  }
}

export function initI18n(fallback = 'en') {
  let stored = '';
  try {
    stored = localStorage.getItem(STORAGE_KEY) || '';
  } catch {
    stored = '';
  }
  setLanguage(stored || fallback, { notify: false });
}
