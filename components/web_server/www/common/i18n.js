'use strict';
/* Shared i18n loader/applier for the portal and management pages.
 * Contract: specs/004-fix-web-i18n/contracts/i18n-http.md §3–§4.
 *
 * The server picks the language from Accept-Language and patches the
 * <html lang="…"> attribute; this script fetches the matching language
 * pack and applies it to [data-i18n] / [data-i18n-placeholder] elements.
 * English needs no pack: the authored markup and the fallback argument
 * at each call site cover every failure mode (unreachable pack, bad
 * JSON, missing key) so the page always stays complete and functional.
 *
 * window.i18nReady resolves to { t, tn }:
 *   t(key, fallback)     → translated string, or fallback
 *   tn(key, n, fallback) → same, with the '{n}' placeholder replaced by n
 */
window.i18nReady = (async () => {
  let pack = {};
  const lang = document.documentElement.lang || 'en';
  if (lang !== 'en') {
    try {
      const r = await fetch(`/i18n/${lang}.json`);
      if (r.ok) {
        const data = await r.json();
        if (data && typeof data === 'object' && !Array.isArray(data)) pack = data;
      }
    } catch (_) { /* stay English */ }
  }

  const t = (key, fallback = '') => {
    const v = pack[key];
    return typeof v === 'string' && v ? v : fallback;
  };
  const tn = (key, n, fallback = '') => t(key, fallback).replace('{n}', n);

  document.querySelectorAll('[data-i18n]').forEach(el => {
    const v = pack[el.dataset.i18n];
    if (typeof v === 'string' && v) el.textContent = v;
  });
  document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
    const v = pack[el.dataset.i18nPlaceholder];
    if (typeof v === 'string' && v) el.placeholder = v;
  });

  return { t, tn };
})();
