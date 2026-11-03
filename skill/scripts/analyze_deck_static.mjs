// Static (no browser) fallback of analyze_deck.mjs: reads the deck's HTML as text.
//
//   node analyze_deck_static.mjs deck.html out_dir
//
// Needs only Node (node: builtins). Writes to out_dir:
//   analysis.json   same shape as analyze_deck.mjs (deck, navigation, tabs_count, theme, tabs[] with title, kicker,
//                   kpis, charts, tables, cards, text; logos) plus screenshots:false, split_rule, limits[],
//                   script_data[] and per-tab confidence. No bboxes.
//   inventory.md    the same as a readable list
//   logo_N.*        logos found in the deck
// There are no screenshots: confirm numbers and anything marked low/medium confidence in the deck's HTML.
//
// Strategy: a tolerant HTML tokenizer builds a tree; a small CSS reader gives class/inline styles (colors,
// font sizes, display:none); the slides are found by a marker / repeated-sibling / heading rule; each slide is
// read for text, KPIs, tables, cards and SVG charts (rect/path geometry calibrated on axis labels); chart configs and
// data arrays in <script> are read with a relaxed parser. Deck code is never executed. Slides drawn by JS
// (innerHTML, template strings) are invisible here: they are reported in `limits` and the tab's `confidence`.
import fs from 'node:fs';
import path from 'node:path';

const [deckArg, outArg] = process.argv.slice(2);
if (!deckArg || !outArg) {
  console.error('usage: node analyze_deck_static.mjs deck.html out_dir');
  process.exit(2);
}
const deck = path.resolve(deckArg), out = path.resolve(outArg);
fs.mkdirSync(out, { recursive: true });
const pad = (i) => String(i).padStart(2, '0');
const SRC = fs.readFileSync(deck, 'utf8');

// ---------- HTML tokenizer -> tree ----------
const ENT = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: ' ', middot: '·', times: '×', minus: '−', ndash: '–', mdash: '—',
  rarr: '→', larr: '←', uarr: '↑', darr: '↓', hellip: '…', bull: '•', deg: '°', plusmn: '±', laquo: '«', raquo: '»', copy: '©',
  ge: '≥', le: '≤', ne: '≠', euro: '€', pound: '£', percnt: '%', lsquo: '‘', rsquo: '’', ldquo: '“', rdquo: '”', check: '✓' };
for (const [n, ch] of Object.entries({ aacute: 'á', agrave: 'à', acirc: 'â', atilde: 'ã', auml: 'ä', eacute: 'é', egrave: 'è', ecirc: 'ê', euml: 'ë', iacute: 'í',
  igrave: 'ì', icirc: 'î', iuml: 'ï', oacute: 'ó', ograve: 'ò', ocirc: 'ô', otilde: 'õ', ouml: 'ö', uacute: 'ú', ugrave: 'ù', ucirc: 'û',
  uuml: 'ü', ccedil: 'ç', ntilde: 'ñ' })) { ENT[n] = ch; ENT[n[0].toUpperCase() + n.slice(1)] = ch.toUpperCase(); }
const decode = (s) => s.indexOf('&') < 0 ? s : s.replace(/&(#x[0-9a-f]+|#\d+|[a-z][a-z0-9]*);/gi, (m, e) =>
  e[0] === '#' ? String.fromCodePoint(e[1] === 'x' || e[1] === 'X' ? parseInt(e.slice(2), 16) : +e.slice(1)) : ENT[e] ?? ENT[e.toLowerCase()] ?? m);
const VOID = new Set(['area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'param', 'source', 'track', 'wbr']);
const CLOSERS = { li: ['li'], td: ['td', 'th'], th: ['td', 'th'], tr: ['tr', 'td', 'th'], p: ['p'], option: ['option'] };

function parseHTML(s) {
  const root = { tag: '#root', attrs: {}, kids: [], parent: null, s: 0, e: s.length };
  let cur = root, pos = 0, m;
  const re = /<!--[\s\S]*?-->|<![^>]*>|<\?[^>]*>|<\/([a-zA-Z][\w:-]*)[^>]*>|<([a-zA-Z][\w:-]*)((?:"[^"]*"|'[^']*'|[^>"'])*)>/g;
  const text = (t) => { if (t) cur.kids.push({ tag: '#text', text: decode(t), parent: cur }); };
  while ((m = re.exec(s))) {
    text(s.slice(pos, m.index)); pos = re.lastIndex;
    if (m[1]) {
      const tag = m[1].toLowerCase();
      for (let n = cur; n && n.tag !== '#root'; n = n.parent)
        if (n.tag === tag) { for (let k = cur; k !== n; k = k.parent) k.e = m.index; n.e = pos; cur = n.parent; break; }
    } else if (m[2]) {
      const tag = m[2].toLowerCase(), attrs = {};
      for (const a of m[3].matchAll(/([^\s=/>"']+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'>]+)))?/g)) {
        const v = a[2] ?? a[3] ?? a[4] ?? '';
        attrs[a[1].toLowerCase()] = v.length > 3000 ? v : decode(v);
      }
      while (CLOSERS[tag] && CLOSERS[tag].includes(cur.tag)) { cur.e = m.index; cur = cur.parent; }
      const el = { tag, attrs, kids: [], parent: cur, s: m.index, e: pos };
      cur.kids.push(el);
      if (VOID.has(tag) || /\/\s*$/.test(m[3])) continue;
      if (tag === 'script' || tag === 'style') {
        const r = new RegExp('</' + tag + '\\s*>', 'ig');
        r.lastIndex = pos;
        const c = r.exec(s), end = c ? c.index : s.length;
        el.kids.push({ tag: '#text', text: s.slice(pos, end), parent: el });
        re.lastIndex = pos = el.e = c ? end + c[0].length : s.length;
        continue;
      }
      cur = el;
    }
  }
  text(s.slice(pos));
  return root;
}
const elems = (n, f, inSvg = true) => { // all descendant elements, document order; f(el) false skips the subtree
  const o = [];
  const rec = (x) => { for (const k of x.kids) if (k.tag !== '#text') { o.push(k); if ((inSvg || k.tag !== 'svg') && (!f || f(k) !== false)) rec(k); } };
  rec(n);
  return o;
};
const classes = (el) => el._cls ??= (el.attrs.class || '').split(/\s+/).filter(Boolean);
const contains = (a, b) => { for (let n = b; n; n = n.parent) if (n === a) return true; return false; };

// ---------- CSS ----------
const rules = [], cssVars = {}, scopedVars = {}; // scopedVars: custom properties defined on other selectors (fallback)
const byClass = new Map(), byId = new Map(), byTag = new Map();
const splitDecls = (str) => {
  const o = {};
  let depth = 0, q = '', last = 0;
  const put = (t) => {
    const i = t.indexOf(':');
    if (i < 1) return;
    const k = t.slice(0, i).trim().toLowerCase();
    o[k] = t.slice(i + 1).replace(/!important/i, '').trim();
    if (/!important/i.test(t)) (o.__imp ||= []).push(k);
  };
  for (let i = 0; i < str.length; i++) {
    const c = str[i];
    if (q) { if (c === q) q = ''; } else if (c === '"' || c === "'") q = c;
    else if (c === '(') depth++; else if (c === ')') depth--;
    else if (c === ';' && !depth) { put(str.slice(last, i)); last = i + 1; }
  }
  put(str.slice(last));
  return o;
};
function parseCSS(css) {
  css = css.replace(/\/\*[\s\S]*?\*\//g, '').replace(/@(import|charset)[^;]*;/g, '');
  let i = 0;
  while (i < css.length) {
    const b = css.indexOf('{', i);
    if (b < 0) break;
    const prelude = css.slice(i, b).trim();
    let d = 1, j = b + 1;
    while (j < css.length && d) { if (css[j] === '{') d++; else if (css[j] === '}') d--; j++; }
    const body = css.slice(b + 1, j - 1);
    i = j;
    if (prelude[0] === '@') { if (/^@(media|supports|layer)/i.test(prelude) && !/print|max-width|prefers|hover|orientation|max-height/i.test(prelude)) parseCSS(body); continue; }
    const decls = splitDecls(body);
    for (const sel of prelude.split(',').map((x) => x.trim())) {
      for (const [k, v] of Object.entries(decls)) if (k.startsWith('--')) { const t = /^(:root|html|body|\*)$/.test(sel) ? cssVars : scopedVars; if (!(k in t)) t[k] = v; }
      if (/[[:(]|::/.test(sel) || !sel) continue;
      if (/[+~]/.test(sel)) continue;
      const cs = sel.split(/\s*>\s*|\s+/).map((p) => {
        const mm = /^([a-z][\w-]*|\*)?((?:[.#][\w-]+)*)$/i.exec(p);
        return mm && { tag: (mm[1] || '').toLowerCase().replace('*', ''), cls: [...mm[2].matchAll(/\.([\w-]+)/g)].map((x) => x[1]), id: (/#([\w-]+)/.exec(mm[2]) || [])[1] };
      });
      if (cs.some((c) => !c)) continue;
      const r = { ...cs.pop(), anc: cs,
        spec: (sel.match(/#/g) || []).length * 100 + (sel.match(/\./g) || []).length * 10 + (sel.match(/(^|[\s>+~])[a-z]/gi) || []).length, order: rules.length, decls };
      rules.push(r);
      const key = r.cls.length ? [byClass, r.cls[r.cls.length - 1]] : r.id ? [byId, r.id] : [byTag, r.tag || '*'];
      if (!key[0].has(key[1])) key[0].set(key[1], []);
      key[0].get(key[1]).push(r);
    }
  }
}
const resolveVars = (v) => {
  for (let k = 0; k < 6 && v.includes('var('); k++)
    v = v.replace(/var\(\s*(--[\w-]+)\s*(?:,\s*([^()]*(?:\([^()]*\)[^()]*)*))?\)/g, (m, n, fb) => cssVars[n] ?? scopedVars[n] ?? fb ?? m);
  return v;
};
const cmp = (n, c) => (!c.tag || c.tag === n.tag) && (!c.id || c.id === n.attrs.id) && c.cls.every((k) => classes(n).includes(k));
function matches(el, r) {
  if (!cmp(el, r)) return false;
  let n = el.parent;
  for (let i = r.anc.length - 1; i >= 0; i--) { while (n && !cmp(n, r.anc[i])) n = n.parent; if (!n) return false; n = n.parent; }
  return true;
}
function styleOf(el) {
  if (el._st) return el._st;
  const cand = [];
  for (const c of classes(el)) if (byClass.has(c)) cand.push(...byClass.get(c));
  if (el.attrs.id && byId.has(el.attrs.id)) cand.push(...byId.get(el.attrs.id));
  if (byTag.has(el.tag)) cand.push(...byTag.get(el.tag));
  if (byTag.has('*')) cand.push(...byTag.get('*'));
  const st = {};
  for (const k of ['fill', 'stroke', 'stroke-width', 'stroke-dasharray', 'font-size', 'text-transform']) if (el.attrs[k] !== undefined) st[k] = el.attrs[k];
  const hit = cand.filter((r) => matches(el, r)).sort((a, b) => a.spec - b.spec || a.order - b.order);
  hit.forEach((r) => Object.assign(st, r.decls));
  hit.forEach((r) => (r.decls.__imp || []).forEach((k) => { st[k] = r.decls[k]; }));
  delete st.__imp;
  if (el.attrs.style) Object.assign(st, splitDecls(el.attrs.style));
  for (const k in st) if (st[k].includes('var(')) st[k] = resolveVars(st[k]);
  return el._st = st;
}
const HIDE = new Set(['script', 'style', 'template', 'noscript', 'head', 'title', 'meta', 'link']);
// hidden: display:none, visibility:hidden, collapsed (max-height:0), or a thumbnail copy (transform: scale(<0.5))
const ownHidden = (el) => el._h ??= HIDE.has(el.tag) || 'hidden' in el.attrs ||
  styleOf(el).display === 'none' || styleOf(el).visibility === 'hidden' ||
  /scale\(\s*0?\.[0-4]/.test(styleOf(el).transform || '') ||
  (/^0(px)?$/.test(styleOf(el)['max-height'] || '') && /hidden/.test(styleOf(el).overflow || ''));
const visibleIn = (el, root) => { for (let n = el; n && n !== root; n = n.parent) if (n.tag !== '#root' && ownHidden(n)) return false; return true; };

// font size in px: units, clamp(), calc(), inheritance, tag defaults
const KW = { 'xx-small': 9, 'x-small': 10, small: 13, medium: 16, large: 18, 'x-large': 24, 'xx-large': 32 };
const TAGFS = { h1: 32, h2: 24, h3: 18.7, h4: 16, small: 13, sub: 12, sup: 12 };
function px(v, parent) {
  v = String(v).trim();
  let m;
  if ((m = /^clamp\((.*)\)$/i.exec(v))) { const a = splitArgs(m[1]).map((x) => px(x, parent)); return Math.min(Math.max(a[1] ?? a[0], a[0]), a[2] ?? 1e9); }
  if ((m = /^(?:min|max)\((.*)\)$/i.exec(v))) { const a = splitArgs(m[1]).map((x) => px(x, parent)).filter(Number.isFinite); return /^min/i.test(v) ? Math.min(...a) : Math.max(...a); }
  if ((m = /^calc\((.*)\)$/i.exec(v))) return [...m[1].matchAll(/([+-]?)\s*([\d.]+)\s*(px|rem|em|vw|vh|pt|%)?/g)].reduce((s, t) => s + (t[1] === '-' ? -1 : 1) * px(t[2] + (t[3] || 'px'), parent), 0);
  if (KW[v]) return KW[v];
  if (!(m = /^(-?[\d.]+)(px|rem|em|vw|vh|pt|%|vmin|vmax)?$/i.exec(v))) return NaN;
  const n = +m[1], u = (m[2] || 'px').toLowerCase();
  return u === 'px' ? n : u === 'rem' ? n * 16 : u === 'em' ? n * parent : u === '%' ? n * parent / 100 : u === 'pt' ? n * 4 / 3 : u === 'vw' || u === 'vmax' ? n * 12.8 : n * 7.2;
}
const splitArgs = (s) => { // top-level commas, brackets and strings aware
  const o = [];
  let d = 0, l = 0;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c === '"' || c === "'" || c === '`') { for (i++; i < s.length && s[i] !== c; i++) if (s[i] === '\\') i++; }
    else if ('([{'.includes(c)) d++; else if (')]}'.includes(c)) d--;
    else if (c === ',' && !d) { o.push(s.slice(l, i)); l = i + 1; }
  }
  o.push(s.slice(l));
  return o;
};
function fsOf(el) {
  if (el._fs) return el._fs;
  const parent = el.parent && el.parent.tag !== '#root' ? fsOf(el.parent) : 16;
  let v = NaN;
  const raw = styleOf(el)['font-size'];
  if (raw && raw !== 'inherit') v = px(raw, parent);
  return el._fs = Number.isFinite(v) && v > 0 ? v : TAGFS[el.tag] && parent <= 16 ? TAGFS[el.tag] : parent;
}
const maxFs = (el) => Math.max(fsOf(el), ...elems(el).map(fsOf));

// ---------- text helpers ----------
const BLOCK = new Set('div p h1 h2 h3 h4 h5 h6 li ul ol tr table section article aside header footer main nav figure figcaption blockquote pre hr br dl dt dd form td th thead tbody tfoot canvas button label text'.split(' '));
function textLines(node, svg = false) {
  const lines = [];
  let line = '';
  const flush = () => { const t = line.replace(/\s+/g, ' ').trim(); if (t) lines.push(t); line = ''; };
  const rec = (n) => {
    if (n.tag === '#text') { line += n.text; return; }
    if (HIDE.has(n.tag) || (n !== node && ownHidden(n)) || (n.tag === 'svg' && !svg && n !== node)) return;
    const blk = BLOCK.has(n.tag) || /^(block|flex|grid|inline-block|inline-flex|list-item|table)/.test(styleOf(n).display || '') || /flex|grid/.test(n.parent ? styleOf(n.parent).display || '' : '');
    if (blk) flush();
    n.kids.forEach(rec);
    if (blk) flush();
  };
  rec(node); flush();
  return lines;
}
const textOf = (n, svg = false) => textLines(n, svg).join(' ');
const NUM = /^[+\-−–]?\s?(R\$|US\$|\$|€|£)?\s?[+\-−]?\d[\d.,]*\s?(k|K|mi|Mi|mil|bi|M|MM|bn|%|p\.p\.|pp|x|×|dias|days|d)?$/;
const NUMC = /^[+\-−–]?(R\$|US\$|\$|€|£)?[+\-−–]?\d[\d.,]*(k|K|mi|Mi|M|mil|bi|bn|MM|%|p\.p\.|pp|x|×|dias|days|d)?$/;
function num(t) { // "R$ 1.234,5k", "−12%", "(3,2)" -> number (suffix unit kept out)
  const s = String(t).replace(/[−–]/g, '-').replace(/\s/g, '');
  const m = /-?\d[\d.,]*/.exec(s);
  if (!m) return NaN;
  let x = m[0];
  const lc = x.lastIndexOf(','), ld = x.lastIndexOf('.');
  if (lc >= 0 && ld >= 0) x = lc > ld ? x.replace(/\./g, '').replace(',', '.') : x.replace(/,/g, '');
  else if (lc >= 0) x = x.split(',').length > 2 || x.length - lc - 1 === 3 ? x.replace(/,/g, '') : x.replace(',', '.');
  else if (ld >= 0 && (x.split('.').length > 2 || x.length - ld - 1 === 3)) x = x.replace(/\./g, '');
  const v = parseFloat(x);
  return (m[0][0] === '-' || /^\(.*\)$/.test(s)) && v > 0 ? -v : v;
}

// ---------- relaxed JS/JSON5 reader (never evaluates) ----------
function scanEnd(t, i) { // index of the bracket closing the one at t[i]; strings/comments aware
  const open = t[i], close = { '{': '}', '[': ']', '(': ')' }[open];
  let d = 0;
  for (; i < t.length; i++) {
    const c = t[i];
    if (c === '"' || c === "'" || c === '`') { for (i++; i < t.length && t[i] !== c; i++) if (t[i] === '\\') i++; }
    else if (c === '/' && t[i + 1] === '/') { i = t.indexOf('\n', i); if (i < 0) return -1; }
    else if (c === '/' && t[i + 1] === '*') { i = t.indexOf('*/', i); if (i < 0) return -1; i++; }
    else if (c === open) d++;
    else if (c === close && !--d) return i;
  }
  return -1;
}
function relaxed(t, vars, depth = 0) {
  let i = 0, bad = 0;
  const ws = () => {
    for (;;) {
      while (i < t.length && /\s/.test(t[i])) i++;
      if (t.startsWith('//', i)) { i = t.indexOf('\n', i); if (i < 0) i = t.length; }
      else if (t.startsWith('/*', i)) { i = t.indexOf('*/', i); i = i < 0 ? t.length : i + 2; }
      else return;
    }
  };
  const skip = () => { // an expression we do not understand: advance to the next top-level , } ]
    let d = 0;
    for (; i < t.length; i++) {
      const c = t[i];
      if (c === '"' || c === "'" || c === '`') { for (i++; i < t.length && t[i] !== c; i++) if (t[i] === '\\') i++; }
      else if ('{[('.includes(c)) d++;
      else if ('}])'.includes(c)) { if (!d) return; d--; }
      else if (c === ',' && !d) return;
    }
  };
  const str = () => {
    const q = t[i++]; let s = '';
    for (; i < t.length && t[i] !== q; i++) { if (t[i] === '\\') { i++; s += { n: '\n', t: '\t' }[t[i]] ?? t[i]; } else s += t[i]; }
    i++;
    return s;
  };
  const val = () => {
    ws();
    const c = t[i];
    if (c === '{') {
      i++;
      const o = {};
      for (;;) {
        ws();
        if (i >= t.length) throw new Error('eof');
        if (t[i] === '}') { i++; return o; }
        if (t[i] === ',') { i++; continue; }
        if (t.startsWith('...', i)) { i += 3; skip(); bad++; continue; }
        let k;
        if (t[i] === '"' || t[i] === "'" || t[i] === '`') k = str();
        else if (t[i] === '[') { const e = scanEnd(t, i); if (e < 0) throw new Error('key'); k = '?' + i; i = e + 1; }
        else { const m = /^[\w$-]+/.exec(t.slice(i, i + 80)); if (!m) throw new Error('key@' + i); k = m[0]; i += k.length; }
        ws();
        if (t[i] === ':') { i++; o[k] = val(); }
        else if (t[i] === '(') { skip(); bad++; } // method
        else o[k] = vars ? vars(k, depth + 1) ?? null : null; // shorthand
      }
    }
    if (c === '[') {
      i++;
      const a = [];
      for (;;) {
        ws();
        if (i >= t.length) throw new Error('eof');
        if (t[i] === ']') { i++; return a; }
        if (t[i] === ',') { i++; if (a.length && t[i - 2] === ',') a.push(null); continue; }
        if (t.startsWith('...', i)) { i += 3; skip(); bad++; continue; }
        a.push(val());
      }
    }
    if (c === '"' || c === "'" || c === '`') { const s = str(); ws(); if (t[i] === '+' || t[i] === '.') { skip(); bad++; } return s; }
    const m = /^[+-]?(?:0x[0-9a-f]+|(?:\d[\d_]*\.?\d*|\.\d+)(?:e[+-]?\d+)?)/i.exec(t.slice(i, i + 40));
    if (m) {
      const j = i; i += m[0].length; ws();
      if (i >= t.length || ',]})'.includes(t[i])) return Number(m[0].replace(/_/g, ''));
      i = j; skip(); bad++;
      return null;
    }
    const id = /^[A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*/.exec(t.slice(i, i + 120));
    if (id) {
      const w = id[0], j = i;
      i += w.length; ws();
      if (i >= t.length || ',]})'.includes(t[i])) {
        if (w === 'true') return true; if (w === 'false') return false; if (w === 'null' || w === 'undefined') return null;
        if (w === 'NaN') return NaN; if (w === 'Infinity') return Infinity;
        const r = vars && depth < 4 ? vars(w, depth + 1) : undefined;
        if (r !== undefined) return r;
        bad++;
        return null;
      }
      i = j;
    }
    skip(); bad++;
    return null;
  };
  const v = val();
  return { v, bad };
}

// ---------- parse the document ----------
const root = parseHTML(SRC);
const html = elems(root);
const body = html.find((e) => e.tag === 'body') || root;
for (const st of html.filter((e) => e.tag === 'style')) parseCSS(st.kids.map((k) => k.text).join(''));
const scripts = html.filter((e) => e.tag === 'script');
// inline libraries (minified Chart.js and the like) are not deck code
const isLib = (t) => t.length > 50000 && (t.split('\n').length < t.length / 3000 || /^\s*\/\*[\s\S]{0,400}?(Chart\.js|Plotly|ECharts|Highcharts|ApexCharts) v?\d/.test(t));
const jsText = scripts.filter((s) => !/json|importmap|template/i.test(s.attrs.type || '')).map((s) => s.kids.map((k) => k.text).join('\n')).filter((t) => !isLib(t));
const jsonBlobs = scripts.filter((s) => /json/i.test(s.attrs.type || '') && !/ld\+json/.test(s.attrs.type)).map((s) => ({ id: s.attrs.id, text: s.kids.map((k) => k.text).join('') }));

// subtree text length and element count (post-order) for the slide finder
(function size(n) { let tl = 0, c = 0; for (const k of n.kids) { if (k.tag === '#text') tl += k.text.trim().length; else if (!HIDE.has(k.tag)) { size(k); tl += k._tl; c += 1 + k._n; } } n._tl = tl; n._n = c; })(body);

// variables: `const NAME = [..]/{..}` in scripts, parsed lazily
const defs = new Map();
for (const t of jsText)
  for (const m of t.matchAll(/(?:^|[;\s{(])(?:const|let|var)\s+([A-Za-z_$][\w$]*)\s*=\s*(?=[[{])/g)) if (!defs.has(m[1])) defs.set(m[1], { t, at: m.index + m[0].length });
const varCache = new Map();
function getVar(name, depth = 0) {
  if (varCache.has(name)) return varCache.get(name);
  const d = defs.get(name);
  if (!d) return undefined;
  varCache.set(name, undefined);
  const e = scanEnd(d.t, d.at);
  let v;
  if (e > 0 && e - d.at < 600000) try { v = relaxed(d.t.slice(d.at, e + 1), getVar, depth).v; } catch {}
  varCache.set(name, v);
  return v;
}
const mounts = new Set(); // ids filled by JS (innerHTML etc.)
for (const t of jsText) for (const m of t.matchAll(/(?:getElementById\(\s*['"]([\w-]+)['"]\s*\)|querySelector\(\s*['"]#([\w-]+)['"]\s*\)|\$\(\s*['"]#?([\w-]+)['"]\s*\))\s*\.\s*(?:innerHTML|insertAdjacentHTML|append|appendChild|html)\b/g)) mounts.add(m[1] || m[2] || m[3]);

const ids = new Map(); // js variable -> element id
for (const t of jsText) for (const m of t.matchAll(/(?:const|let|var)\s+(\w+)\s*=\s*(?:document\.getElementById\(\s*['"]([\w-]+)|\$\(\s*['"]#([\w-]+)|echarts\.init\(\s*document\.getElementById\(\s*['"]([\w-]+))/g)) ids.set(m[1], m[2] || m[3] || m[4]);
// HTML built in script: `X.innerHTML = \`...\``. Its static text and aria-labels are kept per target (${...} dropped).
function readTemplate(t, i) { // t[i] is the opening backtick; returns [text with \u0001 for each ${...}, index after the closing backtick]
  let s = '';
  for (i++; i < t.length && t[i] !== '`'; i++) {
    if (t[i] === '\\') s += t[++i] ?? '';
    else if (t[i] === '$' && t[i + 1] === '{') {
      let d = 1;
      for (i += 2; i < t.length && d; i++) {
        const c = t[i];
        if (c === '`') i = readTemplate(t, i)[1] - 1;
        else if (c === '"' || c === "'") { for (i++; i < t.length && t[i] !== c; i++) if (t[i] === '\\') i++; }
        else if (c === '{') d++; else if (c === '}') d--;
      }
      i--; s += '\u0001';
    } else s += t[i];
  }
  return [s, i + 1];
}
const tplHtml = [];
for (const t of jsText)
  for (const m of t.matchAll(/([\w$.]+(?:\(\s*['"][^'"]*['"]\s*\))?)\s*\.\s*innerHTML\s*\+?=\s*(?=`)/g)) {
    const dom = parseHTML(readTemplate(t, m.index + m[0].length)[0].replace(/\u0001/g, ' '));
    const texts = textLines(dom, true).filter((l) => l.length > 1).map((l) => l.slice(0, 160)).slice(0, 30);
    const aria = elems(dom).map((e) => e.attrs['aria-label']).filter(Boolean).map((l) => l.slice(0, 300));
    const id = (/#([\w-]+)|getElementById\(\s*['"]([\w-]+)|^['"]?([\w-]+)['"]?$/.exec(m[1]) || []).slice(1).find(Boolean);
    if (texts.length || aria.length) tplHtml.push({ id: id && (ids.get(id) || id), target: m[1], texts, aria });
  }

// ---------- chart configs in scripts ----------
const color = (c) => typeof c === 'string' ? c : Array.isArray(c) ? c.filter((x) => typeof x === 'string')[0] : undefined;
const arr = (x) => Array.isArray(x) ? x : x == null ? [] : [x];
function normalize(lib, cfg, extra) {
  if (!cfg || typeof cfg !== 'object') return null;
  const ds = [];
  let kind, labels = null;
  if (lib === 'chartjs') {
    kind = cfg.type; labels = cfg.data?.labels ?? null;
    for (const d of arr(cfg.data?.datasets)) ds.push({ label: d?.label, type: d?.type, data: d?.data, color: color(d?.borderColor) || color(d?.backgroundColor) });
  } else if (lib === 'echarts') {
    const ser = arr(cfg.series);
    kind = ser[0]?.type;
    const ax = [...arr(cfg.xAxis), ...arr(cfg.yAxis)].find((a) => Array.isArray(a?.data));
    labels = ax ? ax.data : null;
    for (const s of ser) ds.push({ label: s?.name, type: s?.type, data: s?.data, color: color(s?.itemStyle?.color) || color(s?.lineStyle?.color) });
  } else if (lib === 'plotly') {
    const tr = arr(cfg);
    kind = (tr[0]?.type || 'scatter') + (tr[0]?.mode ? ':' + tr[0].mode : '');
    labels = tr[0]?.x ?? null;
    for (const s of tr) ds.push({ label: s?.name, type: s?.type, data: s?.y ?? s?.values ?? s?.z, x: s?.x === labels ? undefined : s?.x });
  } else if (lib === 'highcharts' || lib === 'apexcharts') {
    kind = cfg.chart?.type; labels = cfg.xAxis?.categories ?? cfg.xaxis?.categories ?? cfg.labels ?? null;
    if (arr(cfg.series).length && arr(cfg.series).every((x) => typeof x === 'number')) ds.push({ label: 'series', data: cfg.series });
    else for (const s of arr(cfg.series)) ds.push({ label: s?.name, type: s?.type, data: s?.data });
  }
  if (!ds.length && !labels) return null;
  const unresolved = ds.filter((d) => !Array.isArray(d.data)).length;
  return { source: lib, kind: kind || 'unknown', title: '', labels, datasets: ds, ...(unresolved || extra?.bad ? { partial: true } : {}), ...(extra?.id ? { element_id: extra.id } : {}) };
}
const scriptCharts = [];
for (const t of jsText) {
  const find = (re, lib, pick) => {
    for (const m of t.matchAll(re)) {
      const op = m.index + m[0].length - 1, e = scanEnd(t, op);
      if (e < 0) continue;
      const args = splitArgs(t.slice(op + 1, e));
      try {
        const first = args[0];
        const a = pick(args, m);
        const r = relaxed(a, getVar);
        const c = normalize(lib, r.v, { bad: r.bad && !r.v?.series && !r.v?.data, id: ids.get(m[1]) || (/getElementById\(\s*['"]([\w-]+)|(?:\$|querySelector)\(\s*['"]#([\w-]+)|^['"]([\w-]+)/.exec((first || '').trim()) || []).slice(1).find(Boolean) || ids.get((first || '').trim()) });
        if (c) scriptCharts.push(c);
      } catch {}
    }
  };
  find(/new\s+Chart\s*\(/g, 'chartjs', (a) => a[1] || a[0]);
  if (/echarts/.test(t)) find(/(\w+)\s*\.\s*setOption\s*\(/g, 'echarts', (a) => a[0]);
  find(/Plotly\s*\.\s*(?:newPlot|react|plot)\s*\(/g, 'plotly', (a) => a[1]);
  find(/Highcharts\s*\.\s*(?:chart|stockChart)\s*\(/g, 'highcharts', (a) => a[a.length > 1 ? 1 : 0]);
  find(/new\s+ApexCharts\s*\(/g, 'apexcharts', (a) => a[1]);
}
// plain data arrays / JSON blobs
const scriptData = [];
let dataBudget = 300000; // chars of script data kept whole in analysis.json
const shapeOf = (v) => {
  if (Array.isArray(v)) {
    const nums = v.filter((x) => typeof x === 'number').length;
    if (v.length >= 3 && nums >= v.length * 0.7) return 'numbers';
    if (v.length >= 2 && v.every((x) => x && typeof x === 'object')) return Object.values(v[0]).some((x) => typeof x === 'number' || Array.isArray(x)) ? 'records' : null;
    if (v.length >= 3 && v.every((x) => typeof x === 'string')) return 'strings';
    return null;
  }
  if (v && typeof v === 'object') {
    const vs = Object.values(v);
    const nested = vs.filter((x) => Array.isArray(x) || (x && typeof x === 'object')).length;
    return nested && (nested >= vs.length * 0.5 ? JSON.stringify(v).length > 80 : JSON.stringify(v).length > 400) ? 'object' : null;
  }
  return null;
};
const addData = (name, v) => {
  const shape = shapeOf(v);
  if (!shape) return;
  const js = JSON.stringify(v);
  scriptData.push({ name, shape, size: js.length, ...(js.length <= 30000 && (dataBudget -= js.length) > 0 ? { value: v } : { keys: Array.isArray(v) ? v.length + ' items' : Object.keys(v).slice(0, 30), preview: js.slice(0, 400) }) });
};
for (const name of defs.keys()) { const v = getVar(name); if (v !== undefined) addData(name, v); }
for (const b of jsonBlobs) try { addData(b.id || 'json', JSON.parse(b.text)); } catch {}
scriptData.splice(40);

// ---------- split the deck into slides ----------
const SKIPSPLIT = new Set(['script', 'style', 'svg', 'img', 'br', 'span', 'b', 'i', 'a', 'td', 'tr', 'th', 'li', 'canvas', 'p', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'small', 'strong', 'em', 'button', 'label', 'input']);
function findSlides() {
  const groups = new Map();
  for (const e of elems(body, (x) => x.tag !== 'svg' && !HIDE.has(x.tag))) {
    if (SKIPSPLIT.has(e.tag) || !e.parent) continue;
    const cls = classes(e).find((c) => !/^(active|on|current|show|visible|hidden|is-|prev|next|past|future)/.test(c)) || '';
    const key = `${e.parent.tag}|${e.parent.attrs.class || ''}>${e.tag}|${cls}`;
    if (!groups.has(e.parent)) groups.set(e.parent, new Map());
    const g = groups.get(e.parent);
    if (!g.has(key)) g.set(key, []);
    g.get(key).push(e);
  }
  let best = null;
  for (const [p, g] of groups) for (const [k, els] of g) {
    if (els.length < 3) continue;
    // empty shells named slide/page are kept: their content is filled by script
    const full = els.filter((e) => e._tl > 20 && e._n >= 2).length >= els.length * 0.6;
    if (!full && !(/(^|[^a-z])(slide|page|screen|cena|tela)/.test(k.toLowerCase().split('>')[1]) && els.every((e) => e.attrs.id))) continue;
    let depth = 0;
    for (let x = p; x; x = x.parent) depth++;
    const score = Math.min(els.length, 25) + (/slide|page|screen|step|section|cena|tela|tab|pane/.test(k.split('>')[1].toLowerCase()) ? 50 : 0) + (k.split('>')[1].startsWith('section') ? 20 : 0) - depth * 0.5 - (full ? 0 : 30);
    if (!best || score > best.score) best = { key: k, score, els };
  }
  if (best) return { rule: 'repeated siblings: ' + best.key.split('>')[1], els: best.els, name: best.key };
  // long page: split by headings (the container whose children most often hold an h1/h2)
  for (const hl of [['h1', 'h2'], ['h2'], ['h3']]) {
    let top = null;
    for (const c of [body, ...elems(body, (x) => x.tag !== 'svg')]) {
      const hs = c.kids.filter((k) => k.tag !== '#text' && (hl.includes(k.tag) || elems(k).some((x) => hl.includes(x.tag))));
      if (hs.length >= 2 && (!top || hs.length > top.hs.length)) top = { c, hs };
    }
    if (!top) continue;
    const runs = [];
    for (const k of top.c.kids) {
      if (k.tag === '#text' || HIDE.has(k.tag)) continue;
      if (top.hs.includes(k) || !runs.length) runs.push([k]); else runs[runs.length - 1].push(k);
    }
    const els = runs.map((r) => { const s = { tag: 'div', attrs: {}, kids: r, parent: top.c, synthetic: true }; r.forEach((k) => { k.parent = s; }); return s; });
    return { rule: `headings: ${hl.join('/')} inside <${top.c.tag}>`, els };
  }
  return { rule: 'single page (no slide markers, no headings)', els: [body] };
}
const split = findSlides();
// Sub-panes inside the slides (tab panels switched by script): a class that has a display:none rule and repeats among
// siblings shows one pane at a time; only the active one (or the first) is read.
let hiddenPanes = 0;
{
  const slideSet = new Set(split.els);
  for (const p of [body, ...elems(body, (x) => x.tag !== 'svg')]) {
    const g = new Map();
    for (const k of p.kids) if (k.tag !== '#text' && !slideSet.has(k)) for (const c of classes(k)) if (/pane|tab-?content|tabpanel|tab-?view/.test(c)) (g.get(c) || g.set(c, []).get(c)).push(k);
    for (const [c, els] of g) {
      if (els.length < 2 || !rules.some((r) => r.cls.includes(c) && !r.anc.length && r.decls.display === 'none')) continue;
      const keep = els.find((k) => classes(k).some((x) => /^(active|on|show|current|selected|open)$/.test(x))) || els[0];
      for (const k of els) if (k !== keep) { k._h = true; hiddenPanes++; }
    }
  }
}

// ---------- per-slide extraction ----------
const clsHint = (el, re) => classes(el).some((c) => re.test(c));
const hexSat = (c) => { const [r, g, b] = c; const mx = Math.max(r, g, b), mn = Math.min(r, g, b); return mx ? (mx - mn) / mx : 0; };
function rgbOf(v) {
  if (!v) return null;
  v = resolveVars(String(v)).toLowerCase();
  let m = /#([0-9a-f]{8}|[0-9a-f]{6}|[0-9a-f]{3,4})\b/.exec(v);
  if (m) {
    let h = m[1];
    if (h.length <= 4) h = [...h].map((c) => c + c).join('');
    return [parseInt(h.slice(0, 2), 16), parseInt(h.slice(2, 4), 16), parseInt(h.slice(4, 6), 16), h.length === 8 ? parseInt(h.slice(6, 8), 16) / 255 : 1];
  }
  if ((m = /rgba?\(\s*([\d.]+)[\s,]+([\d.]+)[\s,]+([\d.]+)(?:\s*[,/]\s*([\d.]+)(%?))?/.exec(v))) return [+m[1], +m[2], +m[3], m[4] === undefined ? 1 : m[5] ? +m[4] / 100 : +m[4]];
  if ((m = /hsla?\(\s*([\d.]+)(?:deg)?[\s,]+([\d.]+)%[\s,]+([\d.]+)%/.exec(v))) {
    const h = +m[1] / 360, s = +m[2] / 100, l = +m[3] / 100, q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    const f = (t) => { t = (t + 1) % 1; return Math.round(255 * (t < 1 / 6 ? p + (q - p) * 6 * t : t < 0.5 ? q : t < 2 / 3 ? p + (q - p) * (2 / 3 - t) * 6 : p)); };
    return [f(h + 1 / 3), f(h), f(h - 1 / 3), 1];
  }
  return /^(white|#fff)$/.test(v.trim()) ? [255, 255, 255, 1] : /^black$/.test(v.trim()) ? [0, 0, 0, 1] : null;
}
const rgbStr = (c) => `rgb(${c[0]}, ${c[1]}, ${c[2]})`;
const paint = (v) => !v || v === 'none' ? null : colorOf(v) || v.slice(0, 30); // a color, or the raw value when unresolved (var(--x))
const colorOf = (v) => { const c = rgbOf(v); return c && c[3] > 0.05 ? (v.startsWith('#') ? v.toLowerCase().slice(0, 7) : rgbStr(c)) : null; };

// ---- svg charts ----
function svgChart(sv, root0) {
  const vbm = (sv.attrs.viewbox || '').trim().split(/[\s,]+/).map(Number);
  const vb = vbm.length === 4 && vbm.every(Number.isFinite) ? vbm : null;
  const W = vb ? vb[2] : parseFloat(sv.attrs.width) || 0, H = vb ? vb[3] : parseFloat(sv.attrs.height) || 0;
  if (W * H < 12000) return null;
  const sh = { rects: [], texts: [], paths: [], circles: [], lines: [] };
  const n0 = (x) => { const v = parseFloat(x); return Number.isFinite(v) && !/%/.test(x) ? v : NaN; };
  const rec = (n, ox, oy) => {
    for (const k of n.kids) {
      if (k.tag === '#text' || ownHidden(k) || /^(defs|clippath|mask|pattern|lineargradient|radialgradient|symbol|marker|title|desc)$/.test(k.tag)) continue;
      const tm = /translate\(\s*(-?[\d.]+)[\s,]*(-?[\d.]+)?/.exec(k.attrs.transform || '');
      const X = ox + (tm ? +tm[1] : 0), Y = oy + (tm && tm[2] ? +tm[2] : 0), st = styleOf(k), a = k.attrs;
      if (k.tag === 'g' || k.tag === 'a') rec(k, X, Y);
      else if (k.tag === 'rect') {
        const r = { x: X + (n0(a.x) || 0), y: Y + (n0(a.y) || 0), w: n0(a.width), h: n0(a.height), fill: paint(st.fill ?? '#000'), stroke: paint(st.stroke) };
        if (r.w > 2 && r.h > 1 && st.fill !== 'none') sh.rects.push(r);
        else if (r.w > 2 && r.h > 1) sh.lines.push({ x1: r.x, y1: r.y, x2: r.x + r.w, y2: r.y + r.h, box: true });
      } else if (k.tag === 'text') {
        const ts = k.kids.find((c) => c.tag === 'tspan' && c.attrs.x !== undefined);
        const t = textOf(k, true);
        if (t) sh.texts.push({ t, x: X + (n0(a.x ?? ts?.attrs.x) || 0), y: Y + (n0(a.y ?? ts?.attrs.y) || 0), anchor: a['text-anchor'] || st['text-anchor'] || 'start', mid: /middle|central/.test(a['dominant-baseline'] || st['dominant-baseline'] || ''), fill: paint(st.fill) });
      } else if (k.tag === 'path' || k.tag === 'polyline' || k.tag === 'polygon') {
        const pts = pathPoints(a.d || a.points || '', k.tag === 'path');
        if (pts.length) sh.paths.push({ pts: pts.map(([x, y]) => [x + X, y + Y]), closed: /z\s*$/i.test(a.d || '') || k.tag === 'polygon', arcs: /[aA]/.test(a.d || ''), fill: paint(st.fill ?? '#000'), stroke: paint(st.stroke), fo: +(st['fill-opacity'] ?? 1), cmds: (a.d || '').match(/[a-z]/gi)?.length || 0 });
      } else if (k.tag === 'circle') {
        sh.circles.push({ x: X + (n0(a.cx) || 0), y: Y + (n0(a.cy) || 0), r: n0(a.r), fill: paint(st.fill ?? '#000'), stroke: paint(st.stroke), dash: st['stroke-dasharray'] });
      } else if (k.tag === 'line') {
        sh.lines.push({ x1: X + (n0(a.x1) || 0), y1: Y + (n0(a.y1) || 0), x2: X + (n0(a.x2) || 0), y2: Y + (n0(a.y2) || 0), stroke: colorOf(st.stroke) });
      }
    }
  };
  rec(sv, 0, 0);
  // rounded bars drawn as closed paths with few commands count as rects
  for (const p of sh.paths.filter((p) => p.closed && p.fill && !p.stroke && p.cmds <= 14 && !p.arcs)) {
    const xs = p.pts.map((q) => q[0]), ys = p.pts.map((q) => q[1]);
    const r = { x: Math.min(...xs), y: Math.min(...ys), w: Math.max(...xs) - Math.min(...xs), h: Math.max(...ys) - Math.min(...ys), fill: p.fill, path: true };
    if (r.w > 2 && r.h > 1 && r.w < W * 0.5) { sh.rects.push(r); p.bar = true; }
  }
  const tips = [...new Set(elems(sv).map((e) => e.attrs['data-tip'] || e.attrs['data-tooltip'] || (e.tag === 'title' ? textOf(e) : '')).filter(Boolean))];
  const bgBox = (r) => r.w >= W * 0.85 && r.h >= H * 0.7;
  const legend = [];
  let rects = sh.rects.filter((r) => !bgBox(r));
  for (const r of rects.filter((r) => r.w <= 16 && r.h <= 16 && Math.abs(r.w - r.h) <= 3)) {
    const t = sh.texts.filter((q) => q.x >= r.x + r.w && q.x - (r.x + r.w) < 24 && Math.abs(q.y - (r.y + r.h / 2)) < 10 && !NUMC.test(q.t.replace(/\s/g, '')))
      .sort((a, b) => a.x - b.x)[0];
    if (t && rects.filter((q) => q.w <= 16 && q.h <= 16).length <= 8) legend.push({ color: r.fill, label: t.t, _r: r });
  }
  rects = rects.filter((r) => !legend.some((l) => l._r === r));
  const lines = sh.paths.filter((p) => !p.bar && !p.arcs && p.pts.length >= 3 && (p.stroke && !p.fill || p.closed && p.fill) && Math.max(...p.pts.map((q) => q[0])) - Math.min(...p.pts.map((q) => q[0])) > W * 0.25);
  const vbars = rects.filter((r) => r.h > r.w * 0.8), hbars = rects.filter((r) => r.w > r.h * 2);
  const pies = sh.circles.filter((c) => c.dash && c.stroke).length >= 2 || sh.paths.filter((p) => p.arcs && p.fill).length >= 2;
  let kind = 'unknown';
  if (vbars.length >= 4 && lines.length) kind = 'bars+line'; else if (vbars.length >= 4) kind = 'bars'; else if (hbars.length >= 3) kind = 'hbars'; else if (vbars.length >= 3) kind = 'bars';
  else if (pies) kind = 'pie';
  else if (lines.length) kind = sh.circles.length > 2 ? 'line' : 'line_or_area';
  else if (sh.circles.length >= 3) kind = 'scatter';
  const texts = sh.texts;
  // y axis: a column of numeric labels sharing x, linear in y
  let axis = null;
  const numT = texts.filter((t) => NUMC.test(t.t.replace(/\s/g, '')) || /^\(?[-−]?[\d.,]+\)?\s?[kKM%]?$/.test(t.t));
  const cols = new Map();
  for (const t of numT) { const key = Math.round(t.x / 4); (cols.get(key) || cols.set(key, []).get(key)).push(t); }
  const hz = sh.lines.filter((l) => Math.abs(l.y1 - l.y2) < 0.6 && Math.abs(l.x2 - l.x1) > W * 0.2).map((l) => l.y1);
  for (const col of [...cols.values()].sort((a, b) => b.length - a.length)) {
    if (col.length < 3) break;
    // a real axis: the column sits at an outer edge or its labels line up with gridlines (not value labels placed at points)
    const snapped = col.filter((t) => hz.some((y) => Math.abs(y - (t.y - 3.5)) < 5 || Math.abs(y - t.y) < 5)).length;
    if (!(col[0].x < W * 0.15 || col[0].x > W * 0.85 || snapped >= col.length * 0.6)) continue;
    const pts = col.map((t) => { const g = hz.find((y) => Math.abs(y - (t.y - 3.5)) < 5 || Math.abs(y - t.y) < 5); return [g ?? (t.mid ? t.y : t.y - 3.5), num(t.t)]; }).filter((p) => Number.isFinite(p[1]));
    const n = pts.length, mx = pts.reduce((s, p) => s + p[0], 0) / n, my = pts.reduce((s, p) => s + p[1], 0) / n;
    const sxx = pts.reduce((s, p) => s + (p[0] - mx) ** 2, 0), sxy = pts.reduce((s, p) => s + (p[0] - mx) * (p[1] - my), 0), syy = pts.reduce((s, p) => s + (p[1] - my) ** 2, 0);
    if (n >= 3 && sxx > 0 && syy > 0 && sxy * sxy / (sxx * syy) > 0.995) { const a = sxy / sxx; axis = { a, b: my - a * mx, x: col[0].x, ticks: col.map((t) => t.t) }; break; }
  }
  const f = (y) => axis ? +(axis.a * y + axis.b).toFixed(4) : null;
  const y0 = axis ? -axis.b / axis.a : null;
  const axisX = axis ? new Set(cols.get(Math.round(axis.x / 4))) : new Set();
  const catT = texts.filter((t) => !NUMC.test(t.t.replace(/\s/g, '')) && !axisX.has(t));
  const valT = texts.filter((t) => numT.includes(t) && !axisX.has(t));
  const title = (() => { for (let p = sv.parent, k = 0; k < 4 && p && p !== root0.parent; k++, p = p.parent) { const h = p.kids.find((c) => c.tag !== '#text' && c !== sv && !contains(c, sv) && !HIDE.has(c.tag) && c._tl > 3 && c._tl < 120); if (h) return textLines(h)[0] || ''; } return ''; })();
  const out = { source: 'svg', kind, title: title.slice(0, 120), rects: rects.length, paths: sh.paths.length, points: sh.circles.length, labels: texts.slice(0, 40).map((t) => t.t) };
  if (sv.attrs['aria-label']) out.aria = sv.attrs['aria-label'].slice(0, 300);
  if (tips.length) out.tooltips = tips.slice(0, 150);
  if (legend.length) out.legend = legend.map(({ color, label }) => ({ color, label }));
  const colors = [...new Set([...rects.map((r) => r.fill), ...lines.map((p) => p.stroke || p.fill)].filter(Boolean))];
  if (colors.length) out.colors = colors.slice(0, 8);
  if (kind.startsWith('bars')) {
    const bars = vbars.sort((a, b) => a.x - b.x || a.y - b.y).slice(0, 80).map((r) => {
      const cx = r.x + r.w / 2, bot = r.y + r.h;
      const cat = catT.filter((t) => Math.abs(t.x - (t.anchor === 'middle' ? cx : r.x)) < r.w * 1.2 + 8 && t.y > bot - 4).sort((p, q) => p.y - q.y)[0];
      const lab = valT.filter((t) => Math.abs(t.x - cx) < r.w + 10 && t.y < r.y + 4 && t.y > r.y - 30).sort((p, q) => q.y - p.y)[0];
      const est = axis ? f(Math.abs(bot - y0) < Math.abs(r.y - y0) ? r.y : bot) : null;
      return { cat: cat ? cat.t : null, value: lab ? lab.t : est !== null ? String(+est.toPrecision(5)) : null, height_px: Math.round(r.h), ...(r.fill ? { color: r.fill } : {}), ...(lab && est !== null ? { value_est: +est.toPrecision(5) } : {}) };
    });
    out.series = bars;
  } else if (kind === 'hbars') {
    out.series = hbars.sort((a, b) => a.y - b.y || a.x - b.x).slice(0, 60).map((r) => {
      const cy = r.y + r.h / 2;
      const cat = catT.filter((t) => Math.abs(t.y - cy) < r.h + 6 && t.x < r.x + 4).sort((p, q) => q.x - p.x)[0];
      const lab = valT.filter((t) => Math.abs(t.y - cy) < r.h + 6 && t.x >= r.x + r.w - 6).sort((p, q) => p.x - q.x)[0] || valT.filter((t) => Math.abs(t.y - cy) < r.h + 6 && t.x > r.x && t.x < r.x + r.w + 4)[0];
      return { cat: cat ? cat.t : null, value: lab ? lab.t : null, height_px: Math.round(r.w), ...(r.fill ? { color: r.fill } : {}) };
    });
  }
  if (lines.length || (kind === 'scatter')) {
    const cats = catT.filter((t) => t.y > H * 0.6).sort((a, b) => a.x - b.x);
    const lab = (x) => { const c = cats.reduce((b, t) => Math.abs(t.x - x) < Math.abs((b?.x ?? 1e9) - x) ? t : b, null); return c && cats.length > 1 && Math.abs(c.x - x) < Math.max(...cats.map((q, i) => i ? q.x - cats[i - 1].x : 0)) * 0.6 + 4 ? c.t : null; };
    out.lines = lines.slice(0, 8).map((p) => {
      let pts = p.pts;
      if (p.closed && p.fill) { const mxY = Math.max(...pts.map((q) => q[1])); pts = pts.filter((q) => q[1] < mxY - 0.5); }
      if (pts.length > 60) pts = pts.filter((_, i) => i % Math.ceil(pts.length / 60) === 0);
      return { color: p.stroke || p.fill, shape: p.closed && p.fill ? 'area' : 'line', points: pts.map(([x, y]) => ({ x_label: lab(x), ...(axis ? { value: +f(y).toPrecision(5) } : { y: Math.round(y) }) })) };
    });
    if (!axis) out.note = 'no calibratable y axis: line points carry raw svg y (smaller = higher)';
  }
  if (kind === 'pie') {
    const circ = sh.circles.filter((c) => c.dash && c.stroke).map((c) => { const d = c.dash.split(/[\s,]+/).map(parseFloat), l = 2 * Math.PI * c.r; return { color: c.stroke, share: d[0] && l ? +(d[0] / l).toFixed(3) : null }; });
    if (circ.length) out.slices = circ;
  }
  if (axis) out.axis = { ticks: axis.ticks.slice(0, 10) };
  return out;
}
function pathPoints(d, isPath) {
  if (!isPath) return [...d.matchAll(/(-?[\d.]+)[\s,]+(-?[\d.]+)/g)].map((m) => [+m[1], +m[2]]);
  const pts = [];
  let x = 0, y = 0, sx = 0, sy = 0;
  for (const m of d.matchAll(/([a-zA-Z])([^a-zA-Z]*)/g)) {
    const c = m[1], a = (m[2].match(/-?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?/gi) || []).map(Number), rel = c === c.toLowerCase(), C = c.toUpperCase();
    const put = (nx, ny) => { x = rel ? x + nx : nx; y = rel ? y + ny : ny; pts.push([x, y]); };
    if (C === 'M' || C === 'L' || C === 'T') for (let i = 0; i + 1 < a.length; i += 2) { put(a[i], a[i + 1]); if (C === 'M' && !i) { sx = x; sy = y; } }
    else if (C === 'H') for (const v of a) { x = rel ? x + v : v; pts.push([x, y]); }
    else if (C === 'V') for (const v of a) { y = rel ? y + v : v; pts.push([x, y]); }
    else if (C === 'C') for (let i = 0; i + 5 < a.length; i += 6) { const px0 = x, py0 = y; x = px0; y = py0; put(a[i + 4], a[i + 5]); if (rel) { x = px0 + a[i + 4]; y = py0 + a[i + 5]; pts[pts.length - 1] = [x, y]; } }
    else if (C === 'S' || C === 'Q') for (let i = 0; i + 3 < a.length; i += 4) { const px0 = x, py0 = y; put(a[i + 2], a[i + 3]); if (rel) { x = px0 + a[i + 2]; y = py0 + a[i + 3]; pts[pts.length - 1] = [x, y]; } }
    else if (C === 'A') for (let i = 0; i + 6 < a.length; i += 7) put(a[i + 5], a[i + 6]);
    else if (C === 'Z') { x = sx; y = sy; }
  }
  return pts;
}

function extract(root0, idx) {
  // visible elements/leaves (outside svg), document order
  const leaves = [];
  const scan = (n) => {
    for (const k of n.kids) {
      if (k.tag === '#text') { const t = k.text.replace(/\s+/g, ' ').trim(); if (t && !HIDE.has(n.tag)) leaves.push({ t, e: n, i: leaves.length }); continue; }
      if (ownHidden(k) || k.tag === 'svg') continue;
      scan(k);
    }
  };
  scan(root0);
  const info = leaves.map((l) => ({ ...l, fs: fsOf(l.e), up: styleOf(l.e)['text-transform'] === 'uppercase' || (l.t === l.t.toUpperCase() && /[A-Z]/.test(l.t)) }));
  const textual = info.filter((l) => l.t.length >= 6 && !NUM.test(l.t));
  const byFs = [...textual].sort((a, b) => b.fs - a.fs || a.i - b.i);
  const titles = [], seen = new Set();
  for (const l of byFs) {
    let blk = l.e;
    while (blk && blk !== root0 && !/^(h1|h2|h3|p|div)$/.test(blk.tag)) blk = blk.parent;
    blk ||= l.e;
    if (seen.has(blk)) continue;
    seen.add(blk);
    const t = textOf(blk);
    if (t.length < 4 || t.length > 160) continue;
    titles.push({ t, fs: Math.round(l.fs), y: l.i });
    if (titles.length >= 5) break;
  }
  const tEl = byFs[0];
  const kicker = info.filter((l) => (l.up && l.fs <= 13 && l.t.length > 4 || clsHint(l.e, /eyebrow|kicker|overline|^ey$|^kick$/)) && tEl && l.i <= tEl.i).map((l) => l.t).slice(0, 2).join(' · ');

  // KPIs
  const kc = [];
  const all = elems(root0, (x) => !ownHidden(x) && x.tag !== 'svg').filter((e) => !ownHidden(e) && e._tl <= 40);
  for (const e of all) {
    if (e.tag === 'svg' || e.tag === 'td' || e.tag === 'th' || e.tag === 'tr' || e.tag === 'script' || e.tag === 'style') continue;
    const raw = textOf(e);
    if (!raw || raw.length > 24) continue;
    const comp = raw.replace(/\s+/g, '');
    if (!NUMC.test(comp)) continue;
    const fs = maxFs(e);
    if (fs >= 17) kc.push({ e, comp, fs });
  }
  const inner = kc.filter((c) => !kc.some((o) => o !== c && contains(c.e, o.e) && o.comp === c.comp));
  const outer = inner.filter((c) => !inner.some((o) => o !== c && contains(o.e, c.e)));
  const kpis = outer.map((k) => {
    let c = k.e;
    for (let j = 0; j < 5 && c.parent && c.parent !== root0; j++) { if (textLines(c.parent).length > 9) break; c = c.parent; }
    const own = textOf(k.e);
    const lines = textLines(c).filter((x) => x && !own.includes(x));
    return { value: k.comp, font_size: Math.round(k.fs), label: lines.slice(0, 3).join(' | ').slice(0, 160) };
  });

  // charts
  const charts = [];
  const svgs = elems(root0).filter((e) => e.tag === 'svg' && visibleIn(e, root0));
  for (const sv of svgs) { if (sv.parent && sv.parent.tag === 'svg') continue; const c = svgChart(sv, root0); if (c) charts.push(c); }
  for (const cv of elems(root0).filter((e) => e.tag === 'canvas' && visibleIn(e, root0))) {
    const cfg = scriptCharts.find((c) => c.element_id && c.element_id === cv.attrs.id);
    charts.push(cfg ? { ...cfg, bound_to: '#' + cv.attrs.id } : { source: 'canvas', kind: 'unknown', note: 'canvas drawn by script, no readable config' });
  }
  for (const c of scriptCharts) if (c.element_id && !charts.some((x) => x.element_id === c.element_id) && elems(root0).some((e) => e.attrs.id === c.element_id && e.tag !== 'canvas')) charts.push({ ...c, bound_to: '#' + c.element_id });

  // tables
  const tables = elems(root0).filter((e) => e.tag === 'table' && visibleIn(e, root0)).map((t) => {
    const trs = elems(t, (x) => x.tag !== 'table').filter((r) => r.tag === 'tr' && visibleIn(r, t));
    const rows = trs.slice(0, 80).map((r) => r.kids.filter((c) => c.tag === 'td' || c.tag === 'th').map((c) => textOf(c)));
    return { rows, total_rows: trs.length };
  });

  // cards: framed blocks with running text and no chart inside
  const cards = [];
  for (const e of elems(root0, (x) => x.tag !== 'svg' && !ownHidden(x))) {
    if (!/^(div|section|aside|li|article)$/.test(e.tag) || e._tl < 30 || e._tl > 520 || ownHidden(e)) continue;
    const st = styleOf(e), t = textLines(e).join(' | ');
    if (t.length < 30 || t.length > 420) continue;
    const bg = st['background-color'] || st.background;
    const framed = (bg && !/^(none|transparent|0|initial|inherit)/.test(bg) && !/gradient|url/.test(bg) && (rgbOf(bg)?.[3] ?? 0) > 0.05) ||
      parseFloat(st['border-left-width'] || /^(\d+)px/.exec(st['border-left'] || '')?.[1] || 0) >= 2 || /^[1-9]/.test(st['border-top'] || '') ||
      /^[1-9]/.test(st.border || '') || clsHint(e, /^(card|box|callout|note|insight|alert|tile)$|__card|-card$/);
    if (!framed || elems(e).some((x) => /^(svg|canvas|table)$/.test(x.tag))) continue;
    cards.push({ _e: e, text: t.slice(0, 300) });
  }
  const cardsOut = cards.filter((c) => !cards.some((o) => o !== c && contains(c._e, o._e))).slice(0, 12).map(({ _e, ...r }) => r);

  // confidence: what could be hidden from a static read
  const notes = [];
  const emptyMounts = elems(root0).filter((e) => mounts.has(e.attrs.id) && e._tl < 3);
  if (emptyMounts.length) notes.push(`${emptyMounts.length} element(s) filled by script at runtime: ${emptyMounts.slice(0, 4).map((e) => '#' + e.attrs.id).join(', ')}`);
  const cssBars = elems(root0).filter((e) => /^(height|width):\s*[\d.]+%/.test(e.attrs.style || '') && clsHint(e, /bar|fill|col|seg/)).length;
  if (cssBars >= 3) notes.push(`${cssBars} CSS-width/height bars (div charts): values only readable from their labels/text`);
  const ids0 = new Set(elems(root0).map((e) => e.attrs.id).filter(Boolean));
  const built = tplHtml.filter((h) => h.id && ids0.has(h.id)).map((h) => { h.bound = true; return { target: '#' + h.id, texts: h.texts, aria: h.aria }; });
  if (built.length) notes.push(`${built.length} block(s) built by script: only their static text and aria-labels are listed (script_html)`);
  const lines0 = textLines(root0, true);
  if (lines0.join('').length < 40 && !charts.length) notes.push('almost no static text');
  const confidence = lines0.join('').length < 40 && !charts.length ? 'low' : notes.length ? 'medium' : 'high';
  // an empty shell slide (filled by script) is named after its id
  const idTitle = !titles.length && root0.attrs.id ? root0.attrs.id.replace(/^(page|slide|tab|screen)[-_]?/i, '').replace(/[-_]+/g, ' ') : '';
  return { title: titles.length ? titles[0].t : idTitle, ...(idTitle ? { title_source: 'element id' } : {}), title_candidates: titles, kicker, kpis: kpis.slice(0, 16), charts, tables, cards: cardsOut,
    text: lines0.join('\n').slice(0, 1500), ...(built.length ? { script_html: built } : {}), confidence, ...(notes.length ? { confidence_notes: notes } : {}) };
}

const tabs = split.els.map((el, i) => ({ index: i, ...extract(el, i) }));

// A title repeated on many tabs is the deck's header, not the tab's title.
const freq = {};
for (const t of tabs) for (const c of t.title_candidates || []) freq[c.t] = (freq[c.t] || 0) + 1;
for (const t of tabs) {
  const cs = (t.title_candidates || []).filter((c) => freq[c.t] <= Math.max(1, tabs.length * 0.4));
  if (cs.length) t.title = cs[0].t;
}
// script chart configs not bound to any tab
for (const c of scriptCharts) c.bound = tabs.some((t) => t.charts.some((x) => x.element_id === c.element_id && c.element_id));
const looseHtml = tplHtml.filter((h) => !h.bound).slice(0, 30).map(({ id, bound, ...r }) => r);
const loose = scriptCharts.filter((c) => !c.bound).map(({ bound, ...r }) => r);

// ---------- theme and logos ----------
const bodySt = styleOf(body), htmlSt = styleOf(html.find((e) => e.tag === 'html') || body);
const bgVar = Object.keys(cssVars).find((k) => /^--(bg|background|paper|surface-0|canvas|page)$/.test(k));
// background: the first background found going up from the first element inside the first slide (as the browser analyzer)
const bgOfChain = (e) => { for (; e && e.tag !== '#root'; e = e.parent) { const st = styleOf(e), v = st['background-color'] || st.background; if (v && (rgbOf(v)?.[3] ?? 0) > 0.5 && !/gradient|url/.test(v)) return v; } return null; };
const first = split.els[0].kids.find((k) => k.tag !== '#text') || split.els[0];
const bgRaw = [bgOfChain(first), bodySt['background-color'], bodySt.background, htmlSt['background-color'], htmlSt.background, bgVar && cssVars[bgVar]].find((v) => v && rgbOf(v));
const titleEl = html.find((e) => /^(h1|h2)$/.test(e.tag) && !ownHidden(e));
const count = {};
for (const e of elems(body, (x) => !ownHidden(x))) {
  const st = styleOf(e);
  for (const v of [st.color, st['background-color'], st.background, st['border-left-color'], st['border-left'], st.fill, st.stroke]) {
    const c = rgbOf(v);
    if (!c || c[3] < 0.6 || hexSat(c) < 0.35 || /gradient|url\(/.test(v)) continue;
    const k = rgbStr(c);
    count[k] = (count[k] || 0) + 1;
  }
}
let titleAccent = null;
if (titleEl) { const base = styleOf(titleEl).color; for (const sp of elems(titleEl)) { const c = styleOf(sp).color; if (c && c !== base) { titleAccent = rgbStr(rgbOf(c) || [0, 0, 0]); break; } } }
const fontRaw = (styleOf(titleEl || body)['font-family'] || bodySt['font-family'] || cssVars['--font'] || cssVars['--sans'] || '').split(',')[0].replace(/["']/g, '').trim();
const textC = rgbOf(styleOf(titleEl || body).color || bodySt.color || cssVars['--text'] || cssVars['--ink'] || cssVars['--fg']);
const theme = { bg: bgRaw ? rgbStr(rgbOf(bgRaw)) : null, text: textC ? rgbStr(textC) : null, title_accent: titleAccent,
  accents: Object.entries(count).sort((a, b) => b[1] - a[1]).map((x) => x[0]).slice(0, 6), font: fontRaw || null,
  css_vars: Object.fromEntries(Object.entries(cssVars).filter(([, v]) => rgbOf(v)).slice(0, 24)), logos: [] };

const logos = [];
const dh = (el) => { const st = styleOf(el); return parseFloat(st.height) || parseFloat(el.attrs.height) || NaN; };
for (const im of html.filter((e) => e.tag === 'img' && e.attrs.src)) {
  const h = dh(im), src = im.attrs.src;
  if (src.length > 200000 || !(h <= 80 || (Number.isNaN(h) && (clsHint(im, /logo|brand|^lo$/) || /logo/i.test(im.attrs.alt || ''))))) continue;
  logos.push({ alt: im.attrs.alt || '', kind: 'img', src, h: h || null });
}
for (const sv of html.filter((e) => e.tag === 'svg')) {
  const vbm = (sv.attrs.viewbox || '').trim().split(/[\s,]+/).map(Number);
  let h = dh(sv), w = parseFloat(styleOf(sv).width) || parseFloat(sv.attrs.width);
  if (vbm.length === 4 && h && !w) w = h * vbm[2] / vbm[3];
  if (!(h >= 8 && h <= 60 && w >= 30 && w <= 420 && w / h >= 1.4) || elems(sv).some((e) => e.tag === 'text')) continue;
  const xml = SRC.slice(sv.s, sv.e).replace(/^<svg/i, `<svg xmlns="http://www.w3.org/2000/svg"`).replace(/currentColor/g, textC ? rgbStr(textC) : '#000');
  const cl = sv.parent && sv.parent.attrs.class || '';
  logos.push({ alt: sv.attrs['aria-label'] || cl || 'svg', kind: 'svg', svg: xml.replace(/^(<svg[^>]*?)\s(width|height)=["'][^"']*["']/i, '$1').replace(/^<svg/, `<svg width="${Math.round(w)}" height="${Math.round(h)}"`), h });
}
const seenL = new Set();
theme.logos = logos.filter((l) => { const k = (l.src || l.svg).slice(-80); if (seenL.has(k)) return false; seenL.add(k); return true; }).slice(0, 10);
theme.logos.forEach((l, j) => {
  if (l.svg) { l.file = `logo_${j}.svg`; fs.writeFileSync(path.join(out, l.file), l.svg); delete l.svg; return; }
  const m = /^data:image\/([\w+.-]+);base64,(.*)$/s.exec(l.src || ''), m2 = m ? null : /^data:image\/svg\+xml[^,]*,(.*)$/s.exec(l.src || '');
  if (m2) { l.file = `logo_${j}.svg`; fs.writeFileSync(path.join(out, l.file), decodeURIComponent(m2[1])); delete l.src; }
  else if (m) { l.file = `logo_${j}.${{ 'svg+xml': 'svg', jpeg: 'jpg' }[m[1]] || m[1]}`; fs.writeFileSync(path.join(out, l.file), Buffer.from(m[2], 'base64')); delete l.src; }
});

// ---------- limits ----------
const limits = ['static read: no screenshots, no bboxes; numbers read from HTML/SVG/scripts, never from a rendered page'];
const tpl = jsText.join('\n').match(/`[^`]*<(?:div|span|svg|table|tr|td|section)\b[^`]*`/g) || [];
if (tpl.length >= 3) limits.push(`${tpl.length} HTML template strings in scripts: content built at runtime is not visible here; chart/slide data are only available through script_data`);
if (/#slide-|location\.hash|hashchange/.test(jsText.join('\n'))) limits.push('hash-navigated deck: slides may be drawn per hash by script');
const counter = /\b0*1\s*\/\s*(\d{1,3})\b/.exec(textOf(body));
if (counter) limits.push(`page counter suggests ${counter[1]} slides (found ${tabs.length})`);
const lowT = tabs.filter((t) => t.confidence === 'low').length, medT = tabs.filter((t) => t.confidence === 'medium').length;
if (lowT) limits.push(`${lowT} tab(s) with low confidence (almost no static content)`);
if (medT) limits.push(`${medT} tab(s) with medium confidence (parts filled by script or drawn as CSS bars): read the deck HTML for them`);
if (hiddenPanes) limits.push(`${hiddenPanes} sub-panes (tab panels switched by script) skipped: only the active/first pane of each group is read`);
if (tabs.length === 1) limits.push(textOf(body).length < 200 ? 'the page body is (almost) empty: the whole deck is drawn by script, nothing to read statically except script_data' : 'no slide structure found: the whole page is one tab');

const mode = 'static:' + split.rule;
const result = { deck, navigation: mode, split_rule: split.rule, screenshots: false, tabs_count: tabs.length, theme, limits, script_data: scriptData, ...(loose.length ? { script_charts: loose } : {}), ...(looseHtml.length ? { script_html: looseHtml } : {}), tabs };
fs.writeFileSync(path.join(out, 'analysis.json'), JSON.stringify(result, null, 1));

const L = ['# Deck inventory', `${tabs.length} tabs · navigation: ${mode} · STATIC read: no screenshots, no bboxes`, '', ...limits.map((x) => `> ${x}`), ''];
for (const s of tabs) {
  L.push(`## [${pad(s.index)}] ${s.title || '(no title)'}`, `Confidence: ${s.confidence}${s.confidence_notes ? ' (' + s.confidence_notes.join('; ') + ')' : ''}` + (s.kicker ? ` · kicker: ${s.kicker}` : ''));
  for (const k of s.kpis.slice(0, 12)) L.push(`- KPI: **${k.value}** · ${String(k.label || '').slice(0, 140)}`);
  for (const g of s.charts) {
    let line = `- Chart (${g.source}, ${g.kind}): ${g.title || ''}`;
    if (g.aria) line += ` · aria: ${g.aria}`;
    if (g.legend) line += ` · legend: ${g.legend.map((l) => l.label).join(', ')}`;
    if (g.labels) line += ` · labels: ${g.labels.slice(0, 24).join(', ')}`;
    if (g.series) line += ` · bars: ${g.series.map((b) => `${b.cat ?? '?'}=${b.value ?? '?'}`).join(', ')}`;
    for (const l of g.lines || []) line += ` · ${l.shape} ${l.color || ''}: ${l.points.slice(0, 24).map((p) => `${p.x_label ?? '?'}=${p.value ?? p.y}`).join(', ')}`;
    for (const d of (g.datasets || []).slice(0, 6)) line += ` · series ${d.label || ''}: ${JSON.stringify(d.data)}`;
    L.push(line.slice(0, 900));
  }
  for (const t of s.tables.slice(0, 6)) {
    L.push(`- Table (${t.total_rows} rows):`);
    for (const r of t.rows.slice(0, 14)) L.push('  | ' + r.join(' | ').slice(0, 300));
  }
  for (const h of s.script_html || []) L.push(`- Built by script (${h.target}): ${[...h.aria.map((a) => 'aria: ' + a), ...h.texts].join(' | ').slice(0, 500)}`);
  for (const c of s.cards.slice(0, 10)) L.push(`- Card: ${c.text.slice(0, 220)}`);
  const tx = (s.text || '').split(/\s+/).join(' ');
  if (tx) L.push(`- Text: ${tx.slice(0, 600)}`);
  L.push('');
}
if (looseHtml.length) {
  L.push('## Built by script (not tied to a tab)');
  for (const h of looseHtml.slice(0, 12)) L.push(`- ${h.target}: ${[...h.aria.map((a) => 'aria: ' + a), ...h.texts].join(' | ').slice(0, 300)}`);
  L.push('');
}
if (scriptData.length) {
  L.push('## Data in scripts');
  for (const d of scriptData.slice(0, 20)) L.push(`- ${d.name} (${d.shape}, ${d.size} chars): ${(d.value ? JSON.stringify(d.value) : d.preview).slice(0, 300)}`);
  L.push('');
}
fs.writeFileSync(path.join(out, 'inventory.md'), L.join('\n'));

console.log(`${tabs.length} tabs (${mode}) -> ${out}`);
console.log('NOTE: static analysis, no browser: no screenshots and no bboxes; read the deck HTML for anything marked low/medium confidence.');
for (const s of tabs)
  console.log(`  [${pad(s.index)}] ${JSON.stringify((s.title || '').slice(0, 70))} | kpis=${s.kpis.length} charts=${JSON.stringify(s.charts.map((g) => g.kind))} tables=${s.tables.length} cards=${s.cards.length} conf=${s.confidence}`);
