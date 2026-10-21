// SPDX-FileCopyrightText: 2026 Emilien Etadam
// SPDX-License-Identifier: BSL-1.0
//
// [EN-FR fork] French / English for the settings pages, without touching the
// Japanese pages themselves (so upstream updates merge cleanly).
//
// Every Japanese text that reaches the page (static HTML, text set later by
// the scripts, title / placeholder attributes, alert / confirm) is looked up
// in the dictionary below: exact match on the whitespace-collapsed text
// first, then replacement of the known fragments inside it (for messages the
// scripts build by concatenation). Unknown text stays Japanese.
//
// tools/settings_i18n.js is GENERATED from this file + tools/i18n/dict.json by
// tools/i18n/build.py. Edit those, then run the script.
(function () {
  'use strict';
  const DICT = /*{{DICT}}*/{};
  const STORE = 'stackchan-ui-lang';
  const JP = /[　-〿぀-ヿ㐀-鿿＀-￯]/;
  const ATTRS = ['title', 'placeholder', 'aria-label', 'alt', 'label'];
  const SKIP = { SCRIPT: 1, STYLE: 1, TEXTAREA: 1 };

  function initialLang() {
    try {
      const v = localStorage.getItem(STORE);
      if (v === 'fr' || v === 'en' || v === 'ja') return v;
    } catch (e) { /* storage blocked */ }
    const n = (navigator.language || '').toLowerCase();
    if (n.startsWith('ja')) return 'ja';
    if (n.startsWith('fr')) return 'fr';
    return 'en';
  }
  const lang = initialLang();

  const keys = Object.keys(DICT).sort((a, b) => b.length - a.length);
  const esc = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const FRAG = keys.length ? new RegExp(keys.map(esc).join('|'), 'g') : null;

  function tr(s) {
    if (lang === 'ja' || typeof s !== 'string' || !JP.test(s)) return s;
    const norm = s.replace(/\s+/g, ' ').trim();
    const hit = DICT[norm];
    if (hit && hit[lang]) {
      const lead = s.match(/^\s*/)[0];
      const trail = s.match(/\s*$/)[0];
      return lead + hit[lang] + trail;
    }
    if (!FRAG) return s;
    return s.replace(FRAG, (m) => (DICT[m] && DICT[m][lang]) || m);
  }

  function skipped(el) {
    for (let e = el; e; e = e.parentElement) {
      if (SKIP[e.tagName] || (e.dataset && e.dataset.noI18n !== undefined)) return true;
    }
    return false;
  }

  function fixText(node) {
    const v = node.nodeValue;
    if (!v || !JP.test(v) || skipped(node.parentElement)) return;
    const t = tr(v);
    if (t !== v) node.nodeValue = t;
  }

  function fixAttrs(el) {
    for (const a of ATTRS) {
      const v = el.getAttribute && el.getAttribute(a);
      if (v && JP.test(v)) {
        const t = tr(v);
        if (t !== v) el.setAttribute(a, t);
      }
    }
  }

  function walk(root) {
    if (root.nodeType === Node.TEXT_NODE) { fixText(root); return; }
    if (root.nodeType !== Node.ELEMENT_NODE && root.nodeType !== Node.DOCUMENT_NODE) return;
    if (root.nodeType === Node.ELEMENT_NODE) {
      if (SKIP[root.tagName]) return;
      fixAttrs(root);
    }
    const it = document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT);
    for (let n = it.nextNode(); n; n = it.nextNode()) {
      if (n.nodeType === Node.TEXT_NODE) fixText(n);
      else fixAttrs(n);
    }
  }

  function picker() {
    const box = document.createElement('div');
    box.dataset.noI18n = '';
    box.style.cssText = 'position:fixed;top:6px;right:6px;z-index:99999;font:13px system-ui,sans-serif';
    const sel = document.createElement('select');
    sel.title = 'Langue / Language / 言語';
    for (const [v, label] of [['fr', 'Français'], ['en', 'English'], ['ja', '日本語']]) {
      const o = document.createElement('option');
      o.value = v; o.textContent = label;
      if (v === lang) o.selected = true;
      sel.appendChild(o);
    }
    sel.addEventListener('change', () => {
      try { localStorage.setItem(STORE, sel.value); } catch (e) { /* ignore */ }
      location.reload();
    });
    box.appendChild(sel);
    document.body.appendChild(box);
  }

  window.StackchanI18n = { lang, tr };

  function start() {
    picker();
    if (lang === 'ja') return;
    document.documentElement.lang = lang;
    if (JP.test(document.title)) document.title = tr(document.title);
    walk(document.body);
    new MutationObserver((muts) => {
      for (const m of muts) {
        if (m.type === 'characterData') fixText(m.target);
        else if (m.type === 'attributes') fixAttrs(m.target);
        else for (const n of m.addedNodes) walk(n);
      }
    }).observe(document.body, {
      subtree: true, childList: true, characterData: true,
      attributes: true, attributeFilter: ATTRS,
    });
  }

  if (lang !== 'ja') {
    for (const f of ['alert', 'confirm', 'prompt']) {
      const orig = window[f].bind(window);
      window[f] = (msg, ...rest) => orig(tr(String(msg ?? '')), ...rest);
    }
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
})();
