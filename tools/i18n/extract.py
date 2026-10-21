# SPDX-FileCopyrightText: 2026 Emilien Etadam
# SPDX-License-Identifier: BSL-1.0
"""Extrait les textes japonais des pages de reglages (HTML + JS), hors commentaires."""
import re, sys, json
from html.parser import HTMLParser
JP = re.compile(r'[\u3040-\u30ff\u3400-\u9fff\uff00-\uffef\u3000-\u303f]')
def norm(s): return re.sub(r'\s+', ' ', s).strip()

def js_strings(src):
    """Litteraux JS hors commentaires. Renvoie des morceaux statiques (templates coupes sur ${})."""
    out = []; i = 0; n = len(src)
    while i < n:
        c = src[i]
        if c == '/' and i+1 < n and src[i+1] == '/':
            j = src.find('\n', i); i = n if j < 0 else j; continue
        if c == '/' and i+1 < n and src[i+1] == '*':
            j = src.find('*/', i+2); i = n if j < 0 else j+2; continue
        if c in "'\"":
            j = i+1; buf = []
            while j < n and src[j] != c:
                if src[j] == '\\' and j+1 < n: buf.append(src[j:j+2]); j += 2; continue
                if src[j] == '\n': break
                buf.append(src[j]); j += 1
            out.append(''.join(buf)); i = j+1; continue
        if c == '`':
            j = i+1; parts = []; cur = []
            while j < n and src[j] != '`':
                if src[j] == '\\' and j+1 < n: cur.append(src[j:j+2]); j += 2; continue
                if src[j] == '$' and j+1 < n and src[j+1] == '{':
                    parts.append(''.join(cur)); cur = []; depth = 1; j += 2
                    while j < n and depth:
                        if src[j] == '{': depth += 1
                        elif src[j] == '}': depth -= 1
                        elif src[j] in "'\"`":   # nested strings in ${}
                            q = src[j]; j += 1
                            while j < n and src[j] != q:
                                if src[j] == '\\': j += 1
                                j += 1
                        j += 1
                    continue
                cur.append(src[j]); j += 1
            parts.append(''.join(cur)); out.extend(parts); i = j+1; continue
        i += 1
    return out

def unescape_js(s):
    s = s.replace("\\'", "'").replace('\\"', '"').replace('\\n', '\n').replace('\\`', '`')
    s = re.sub(r'\\u\{?([0-9a-fA-F]{4,5})\}?', lambda m: chr(int(m.group(1), 16)), s)
    return s.replace('\\\\', '\\')

def pieces(s):
    """Coupe un texte (eventuellement HTML) en morceaux de texte entre balises."""
    s = re.sub(r'&nbsp;', ' ', s)
    for part in re.split(r'<[^>]*>', s):
        part = re.sub(r'&amp;', '&', part); part = re.sub(r'&lt;', '<', part); part = re.sub(r'&gt;', '>', part)
        p = norm(part)
        if JP.search(p): yield p

class P(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True); self.keys = []; self.in_script = False; self.scripts = []; self.cur = []
    def handle_starttag(self, tag, attrs):
        if tag in ('script', 'style'): self.in_script = tag; self.cur = []
        for k, v in attrs:
            if v and k in ('title', 'placeholder', 'aria-label', 'alt', 'label', 'data-tip') and JP.search(v):
                self.keys.append(norm(v))
    def handle_endtag(self, tag):
        if tag == self.in_script:
            if tag == 'script': self.scripts.append(''.join(self.cur))
            self.in_script = False
    def handle_data(self, d):
        if self.in_script: self.cur.append(d); return
        p = norm(d)
        if JP.search(p): self.keys.append(p)
    def handle_comment(self, d): pass

def extract(path):
    src = open(path, encoding='utf-8').read()
    keys = []
    if path.endswith('.js'):
        scripts = [src]
    else:
        p = P(); p.feed(src); keys += p.keys; scripts = p.scripts
    for sc in scripts:
        for lit in js_strings(sc):
            keys += list(pieces(unescape_js(lit)))
    return keys

if __name__ == '__main__':
    allk = {}
    for path in sys.argv[2:]:
        for k in extract(path):
            allk.setdefault(k, set()).add(path.split('/')[-1])
    json.dump(sorted(allk), open(sys.argv[1], 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    print(len(allk), "cles,", sum(len(k) for k in allk), "caracteres")
