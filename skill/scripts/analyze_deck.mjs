// Analyzes an HTML presentation deck, tab by tab.
//
//   node analyze_deck.mjs deck.html out_dir
//
// Writes to out_dir:
//   analysis.json   per tab: title, kicker, KPIs, charts (labels/series when readable), tables, cards, text,
//                   and the bbox of each item (for slide-camera focus); plus the deck theme and logos
//   inventory.md    the same as a readable list, the base of the deck audit
//   tab_NN.png      1280x720 capture of each tab (tab_NN_full.png when the tab is taller than the screen)
//   grid.jpg        all tabs tiled, 4 per row
//   logo_N.*        logos found in the deck
//
// Strategy: find the group of "slides" (large repeated siblings), navigate them (the deck's own function
// such as go(i)/show(i), or forced visibility via CSS; without slides, scroll the page in screen steps),
// and extract each one. The extraction is a starting point, never the truth: always confirm numbers on
// the tab image and, if needed, in the deck's HTML.
import { chromium } from 'playwright-core';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const [deckArg, outArg] = process.argv.slice(2);
if (!deckArg || !outArg) {
  console.error('usage: node analyze_deck.mjs deck.html out_dir');
  process.exit(2);
}
const deck = path.resolve(deckArg), out = path.resolve(outArg);
fs.mkdirSync(out, { recursive: true });
const pad = (i) => String(i).padStart(2, '0');

const FIND = () => {
  const vw = innerWidth;
  const skip = ['SCRIPT', 'STYLE', 'svg', 'path', 'g', 'rect', 'text', 'line', 'circle', 'tspan', 'defs', 'IMG', 'BR',
    'SPAN', 'B', 'I', 'A', 'TD', 'TR', 'TH', 'LI'];
  const all = [...document.querySelectorAll('body *')].filter((e) => !skip.includes(e.tagName));
  const groups = new Map();
  for (const e of all) {
    const p = e.parentElement;
    if (!p) continue;
    const cls = e.className && typeof e.className === 'string'
      ? e.className.split(/\s+/).filter((c) => c && !/^(active|on|current|show|visible|hidden|is-|prev|next|past|future)/.test(c))[0] || ''
      : '';
    const key = `${p.tagName}|${p.className || ''}>${e.tagName}|${cls}`;
    if (!groups.has(key)) groups.set(key, { parent: p, els: [] });
    groups.get(key).els.push(e);
  }
  let best = null;
  for (const [k, g] of groups) {
    if (g.els.length < 3) continue;
    const pw = g.parent.getBoundingClientRect().width;
    const big = g.els.filter((e) => e.scrollWidth >= vw * 0.5 || e.getBoundingClientRect().width >= vw * 0.5 ||
      (pw >= vw * 0.5 && getComputedStyle(e).display === 'none')).length;
    const txt = g.els.filter((e) => (e.textContent || '').trim().length > 20).length;
    if (big < g.els.length * 0.6 || txt < g.els.length * 0.6) continue;
    let depth = 0;
    for (let x = g.parent; x; x = x.parentElement) depth++;
    const score = g.els.length + (/slide|page|screen|step|section|cena|tela|tab/.test(k.toLowerCase()) ? 50 : 0) +
      (k.includes('SECTION') ? 20 : 0) - depth * 0.5;
    if (!best || score > best.score) best = { key: k, score };
  }
  if (!best) return null;
  const g = groups.get(best.key);
  g.els.forEach((e, i) => e.setAttribute('data-vid-slide', i));
  return { key: best.key, n: g.els.length };
};

const EXTRACT = (idx) => {
  const root = idx === null ? document.body : document.querySelector(`[data-vid-slide="${idx}"]`);
  const R0 = root.getBoundingClientRect();
  const vis = (e) => {
    const s = getComputedStyle(e), r = e.getBoundingClientRect();
    return s.visibility !== 'hidden' && s.display !== 'none' && +s.opacity > 0.05 && r.width > 2 && r.height > 2;
  };
  const box = (e) => {
    const r = e.getBoundingClientRect();
    return [Math.round(r.left - R0.left), Math.round(r.top - R0.top), Math.round(r.width), Math.round(r.height)];
  };
  const leaves = [];
  const w = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  while (w.nextNode()) {
    const n = w.currentNode, t = n.textContent.replace(/\s+/g, ' ').trim();
    if (!t) continue;
    const e = n.parentElement;
    if (!e || e.closest('svg') || e.closest('script,style') || !vis(e)) continue;
    const s = getComputedStyle(e);
    leaves.push({ t, e, fs: parseFloat(s.fontSize), up: s.textTransform === 'uppercase' || (t === t.toUpperCase() && /[A-Z]/.test(t)), b: box(e) });
  }
  const NUM = /^[+\-−–]?\s?(R\$|US\$|\$|€|£)?\s?[+\-−]?\d[\d.,]*\s?(k|K|mi|Mi|mil|bi|M|MM|bn|%|p\.p\.|pp|x|×|dias|days|d)?$/;
  const textual = leaves.filter((l) => l.t.length >= 6 && !NUM.test(l.t));
  const byFs = [...textual].sort((a, b) => b.fs - a.fs || a.b[1] - b.b[1]);
  const titleEl = byFs[0];
  const titles = [], seen = new Set();
  for (const l of byFs) {
    const blk = l.e.closest('h1,h2,h3,p,div') || l.e;
    if (seen.has(blk)) continue;
    seen.add(blk);
    const t = (blk.innerText || '').replace(/\s+/g, ' ').trim();
    if (t.length < 4 || t.length > 160) continue;
    titles.push({ t, fs: Math.round(l.fs), y: l.b[1] });
    if (titles.length >= 5) break;
  }
  const kicker = leaves.filter((l) => l.up && l.fs <= 13 && l.t.length > 4 && titleEl && l.b[1] <= titleEl.b[1])
    .map((l) => l.t).slice(0, 2).join(' · ');

  // KPIs: large numbers with their nearby label lines.
  const NUMC = /^[+\-−–]?(R\$|US\$|\$|€|£)?[+\-−–]?\d[\d.,]*(k|K|mi|Mi|M|mil|bi|bn|MM|%|p\.p\.|pp|x|×|dias|days|d)?$/;
  const kc = [];
  for (const e of root.querySelectorAll('*')) {
    if (e.closest('svg') || !vis(e)) continue;
    const raw = (e.innerText || '').trim();
    if (!raw || raw.length > 24) continue;
    const comp = raw.replace(/\s+/g, '');
    if (!NUMC.test(comp)) continue;
    const fs = Math.max(...[e, ...e.querySelectorAll('*')].map((x) => parseFloat(getComputedStyle(x).fontSize)));
    if (fs >= 17) kc.push({ e, comp, fs });
  }
  const inner = kc.filter((c) => !kc.some((o) => o !== c && c.e.contains(o.e) && o.comp === c.comp));
  const outer = inner.filter((c) => !inner.some((o) => o !== c && o.e.contains(c.e)));
  const kpis = outer.map((k) => {
    let c = k.e;
    for (let j = 0; j < 5 && c.parentElement && c.parentElement !== root; j++) {
      if ((c.parentElement.innerText || '').split('\n').filter((x) => x.trim()).length > 9) break;
      c = c.parentElement;
    }
    const lines = (c.innerText || '').split('\n').map((x) => x.trim()).filter((x) => x && !k.e.innerText.includes(x));
    return { value: k.comp, font_size: Math.round(k.fs), label: lines.slice(0, 3).join(' | ').slice(0, 160), bbox: box(c) };
  });

  // Charts: SVG (with a bar series when the bars carry value and category labels), Chart.js, ECharts.
  const charts = [];
  for (const s of root.querySelectorAll('svg')) {
    if (!vis(s)) continue;
    const r = s.getBoundingClientRect();
    if (r.width * r.height < 30000) continue;
    const rects = [...s.querySelectorAll('rect')].filter((x) => { const b = x.getBBox ? x.getBBox() : { width: 0, height: 0 }; return b.width > 2 && b.height > 1; });
    const paths = [...s.querySelectorAll('path,polyline')].filter((p) => (p.getAttribute('d') || p.getAttribute('points') || '').length > 40);
    const circles = s.querySelectorAll('circle').length;
    const texts = [...s.querySelectorAll('text')].map((t) => { const q = t.getBoundingClientRect(); return { t: t.textContent.trim(), b: [q.left, q.top] }; }).filter((x) => x.t);
    let title = '';
    for (let p = s.parentElement, k = 0; k < 4 && p && !title; k++, p = p.parentElement) {
      const h = [...p.children].find((ch) => ch !== s && !ch.contains(s) && ch.innerText && ch.innerText.trim().length > 3 && ch.innerText.trim().length < 120);
      if (h) title = h.innerText.trim().split('\n')[0];
    }
    const hb = rects.filter((x) => { const b = x.getBBox(); return b.width > b.height * 2; }).length;
    const vb = rects.filter((x) => { const b = x.getBBox(); return b.height > b.width * 0.8; }).length;
    let kind = 'unknown';
    if (vb >= 4 && paths.length) kind = 'bars+line';
    else if (vb >= 4) kind = 'bars';
    else if (hb >= 3) kind = 'hbars';
    else if (paths.length) kind = circles > 2 ? 'line' : 'line_or_area';
    let series = null;
    if (kind.startsWith('bars')) {
      const bars = rects.map((x) => { const q = x.getBoundingClientRect(); return { x: q.left + q.width / 2, top: q.top, bot: q.bottom, w: q.width, h: q.height }; })
        .filter((b) => b.h > b.w * 0.8).sort((a, b) => a.x - b.x);
      const isNum = (t) => /^[+\-−]?[\d.,]+[k%]?$/.test(t.t);
      const numT = texts.filter(isNum), catT = texts.filter((t) => !isNum(t));
      series = bars.slice(0, 40).map((b) => {
        const cat = catT.filter((t) => Math.abs(t.b[0] - b.x) < b.w * 1.2 + 8 && t.b[1] > b.bot - 4).sort((p, q) => p.b[1] - q.b[1])[0];
        const val = numT.filter((t) => Math.abs(t.b[0] - b.x) < b.w + 10 && t.b[1] < b.top + 4 && t.b[1] > b.top - 30)[0];
        return { cat: cat ? cat.t : null, value: val ? val.t : null, height_px: Math.round(b.h) };
      });
    }
    charts.push({ source: 'svg', kind, title: title.slice(0, 120), rects: rects.length, paths: paths.length, points: circles,
      labels: texts.slice(0, 40).map((t) => t.t), series, bbox: box(s) });
  }
  for (const cv of root.querySelectorAll('canvas')) {
    if (!vis(cv)) continue;
    const r = cv.getBoundingClientRect();
    if (r.width * r.height < 30000) continue;
    let info = { source: 'canvas', kind: 'unknown', bbox: box(cv) };
    try {
      const ch = window.Chart && Chart.getChart ? Chart.getChart(cv) : null;
      if (ch) info = { ...info, source: 'chartjs', kind: ch.config.type, labels: ch.data.labels,
        datasets: ch.data.datasets.map((d) => ({ label: d.label, data: d.data })) };
    } catch (e) {}
    try {
      const ec = window.echarts && (echarts.getInstanceByDom(cv.parentElement) || echarts.getInstanceByDom(cv.parentElement.parentElement));
      if (ec) {
        const o = ec.getOption();
        info = { ...info, source: 'echarts', kind: (o.series[0] || {}).type, labels: (o.xAxis && o.xAxis[0] && o.xAxis[0].data) || null,
          datasets: o.series.map((s) => ({ label: s.name, data: s.data })) };
      }
    } catch (e) {}
    charts.push(info);
  }

  const tables = [];
  for (const t of root.querySelectorAll('table')) {
    if (!vis(t)) continue;
    const rows = [...t.rows].filter((r) => vis(r)).slice(0, 14).map((r) => [...r.cells].map((c) => c.innerText.replace(/\s+/g, ' ').trim()));
    tables.push({ rows, total_rows: t.rows.length, bbox: box(t) });
  }

  // Text cards: framed blocks (background or border) with running text and no chart inside.
  const cards = [];
  for (const e of root.querySelectorAll('div,section,aside,li')) {
    if (!vis(e)) continue;
    const s = getComputedStyle(e), t = (e.innerText || '').trim();
    if (t.length < 30 || t.length > 420) continue;
    const framed = (s.backgroundColor !== 'rgba(0, 0, 0, 0)' && s.backgroundColor !== 'transparent') ||
      parseFloat(s.borderLeftWidth) >= 2 || parseFloat(s.borderTopWidth) >= 1;
    if (!framed || e.querySelector('svg,canvas,table')) continue;
    cards.push({ _e: e, text: t.replace(/\n+/g, ' | ').slice(0, 300), bbox: box(e) });
  }
  const cardsOut = cards.filter((c) => !cards.some((o) => o !== c && c._e.contains(o._e))).slice(0, 12).map(({ _e, ...r }) => r);

  return { title: titles.length ? titles[0].t : '', title_candidates: titles, kicker, kpis: kpis.slice(0, 16), charts, tables,
    cards: cardsOut, text: (root.innerText || '').replace(/\n{2,}/g, '\n').slice(0, 1500),
    bbox_tab: [Math.round(R0.left), Math.round(R0.top), Math.round(R0.width), Math.round(R0.height)] };
};

const THEME = () => {
  const cs = (e) => getComputedStyle(e);
  const bgOf = (e) => {
    for (; e; e = e.parentElement) { const c = cs(e).backgroundColor; if (c && c !== 'rgba(0, 0, 0, 0)' && c !== 'transparent') return c; }
    return 'rgb(255, 255, 255)';
  };
  const sl = document.querySelector('[data-vid-slide]') || document.body;
  const parse = (c) => { const m = c && c.match(/rgba?\(([\d.]+),\s*([\d.]+),\s*([\d.]+)(?:,\s*([\d.]+))?/); return m ? [+m[1], +m[2], +m[3], m[4] === undefined ? 1 : +m[4]] : null; };
  const sat = ([r, g, b]) => { const mx = Math.max(r, g, b), mn = Math.min(r, g, b); return mx === 0 ? 0 : (mx - mn) / mx; };
  const count = {};
  for (const e of document.querySelectorAll('body *')) {
    const s = cs(e);
    if (s.display === 'none') continue;
    for (const c of [s.color, s.backgroundColor, s.borderLeftColor, e.getAttribute && e.getAttribute('fill'), e.getAttribute && e.getAttribute('stroke')]) {
      const p = parse(c);
      if (!p || p[3] < 0.6 || sat(p) < 0.35) continue;
      const k = `rgb(${p[0]}, ${p[1]}, ${p[2]})`;
      count[k] = (count[k] || 0) + 1;
    }
  }
  const accents = Object.entries(count).sort((a, b) => b[1] - a[1]).map((x) => x[0]).slice(0, 6);
  const titleEl = document.querySelector('h1,h2,.h1,.title');
  let titleAccent = null;
  if (titleEl) {
    const base = cs(titleEl).color;
    for (const sp of titleEl.querySelectorAll('*')) { const c = cs(sp).color; if (c !== base) { titleAccent = c; break; } }
  }
  const logos = [...document.querySelectorAll('img')].filter((i) => { const r = i.getBoundingClientRect(); return i.src && i.naturalWidth > 0 && r.height <= 80; })
    .map((i) => ({ alt: i.alt || '', kind: 'img', src: i.src.length > 200000 ? null : i.src, h: i.getBoundingClientRect().height })).filter((x) => x.src);
  for (const sv of document.querySelectorAll('svg')) {
    const r = sv.getBoundingClientRect();
    if (r.height < 8 || r.height > 60 || r.width < 30 || r.width / r.height < 1.4 || r.width > 420) continue;
    if (sv.querySelectorAll('text').length > 0) continue;
    const cl = sv.cloneNode(true);
    cl.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
    cl.setAttribute('width', Math.round(r.width));
    cl.setAttribute('height', Math.round(r.height));
    const xml = new XMLSerializer().serializeToString(cl).replace(/currentColor/g, cs(sv).color);
    const name = sv.getAttribute('aria-label') || (sv.parentElement && typeof sv.parentElement.className === 'string' ? sv.parentElement.className : '') || 'svg';
    logos.push({ alt: name, kind: 'svg', svg: xml, h: r.height });
  }
  const seen = new Set();
  const uniq = logos.filter((l) => { const k = (l.src || l.svg).slice(-80); if (seen.has(k)) return false; seen.add(k); return true; }).slice(0, 10);
  return { bg: bgOf(sl.querySelector('*') || sl), text: cs(titleEl || document.body).color, title_accent: titleAccent,
    accents, font: cs(titleEl || document.body).fontFamily, logos: uniq };
};

const browser = await chromium.launch({ channel: 'chrome', args: ['--hide-scrollbars'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
await page.goto('file:///' + deck.replace(/\\/g, '/'), { waitUntil: 'load' });
await page.waitForTimeout(1800);

const tabs = [];
let mode;
const capture = async (i, info) => {
  info.index = i;
  info.image = `tab_${pad(i)}.png`;
  await page.screenshot({ path: path.join(out, info.image) });
  try {
    const loc = page.locator(`[data-vid-slide="${i}"]`);
    const bb = await loc.boundingBox();
    if (bb && bb.height > 760) {
      info.image_full = `tab_${pad(i)}_full.png`;
      await loc.screenshot({ path: path.join(out, info.image_full) });
    }
  } catch (e) {}
  tabs.push(info);
};

const found = await page.evaluate(FIND);
if (found) {
  let nav = null;
  for (const f of ['go', 'show', 'goTo', 'goto', 'showSlide', 'gotoSlide', 'navigate', 'setSlide', 'irPara', 'slideTo', 'showTab']) {
    if (!(await page.evaluate((f) => typeof window[f] === 'function', f))) continue;
    try {
      await page.evaluate((f) => window[f](1), f);
      await page.waitForTimeout(900);
      const ok = await page.evaluate(() => {
        const e = document.querySelector('[data-vid-slide="1"]'), r = e.getBoundingClientRect(), s = getComputedStyle(e);
        return r.width > 100 && r.height > 100 && s.visibility !== 'hidden' && s.display !== 'none' && +s.opacity > 0.5 && r.left > -50 && r.left < 200;
      });
      await page.evaluate((f) => window[f](0), f);
      await page.waitForTimeout(300);
      if (ok) { nav = f; break; }
    } catch (e) {}
  }
  mode = nav ? `function:${nav}` : 'css';
  for (let i = 0; i < found.n; i++) {
    if (nav) await page.evaluate(([f, i]) => window[f](i), [nav, i]);
    else await page.evaluate((i) => {
      let st = document.getElementById('__vid_css');
      if (!st) { st = document.createElement('style'); st.id = '__vid_css'; document.head.appendChild(st); }
      st.textContent = `[data-vid-slide]{display:none!important} [data-vid-slide="${i}"]{display:block!important;opacity:1!important;visibility:visible!important;transform:none!important;position:relative!important;left:0!important;top:0!important}`;
      document.querySelector(`[data-vid-slide="${i}"]`).scrollIntoView();
    }, i);
    await page.waitForTimeout(1600);
    await capture(i, await page.evaluate(EXTRACT, i));
  }
  if (nav) await page.evaluate((f) => window[f](0), nav);
  else await page.evaluate(() => { const st = document.getElementById('__vid_css'); if (st) st.textContent = ''; });
  await page.waitForTimeout(700);
} else {
  mode = 'scroll';
  const H = await page.evaluate(() => document.documentElement.scrollHeight);
  for (let i = 0, y = 0; y < H && i < 30; i++, y += 720) {
    await page.evaluate((y) => window.scrollTo(0, y), y);
    await page.waitForTimeout(900);
    const info = await page.evaluate(EXTRACT, null);
    info.scroll_y = y;
    await capture(i, info);
  }
}
const theme = await page.evaluate(THEME);
await browser.close();

// A title repeated on many tabs is the deck's header, not the tab's title.
const freq = {};
for (const t of tabs) for (const c of t.title_candidates || []) freq[c.t] = (freq[c.t] || 0) + 1;
for (const t of tabs) {
  const cs = (t.title_candidates || []).filter((c) => freq[c.t] <= Math.max(1, tabs.length * 0.4));
  if (cs.length) t.title = cs[0].t;
}

theme.logos.forEach((l, j) => {
  if (l.svg) {
    l.file = `logo_${j}.svg`;
    fs.writeFileSync(path.join(out, l.file), l.svg);
    delete l.svg;
    return;
  }
  const m = /^data:image\/([\w+.-]+);base64,(.*)$/s.exec(l.src || '');
  const m2 = m ? null : /^data:image\/svg\+xml[^,]*,(.*)$/s.exec(l.src || '');
  if (m2) {
    l.file = `logo_${j}.svg`;
    fs.writeFileSync(path.join(out, l.file), decodeURIComponent(m2[1]));
  } else if (m) {
    l.file = `logo_${j}.${{ 'svg+xml': 'svg', jpeg: 'jpg' }[m[1]] || m[1]}`;
    fs.writeFileSync(path.join(out, l.file), Buffer.from(m[2], 'base64'));
  }
  delete l.src;
});

const result = { deck, navigation: mode, tabs_count: tabs.length, theme, tabs };
fs.writeFileSync(path.join(out, 'analysis.json'), JSON.stringify(result, null, 1));

try {
  const rows = Math.ceil(tabs.length / 4);
  execFileSync('ffmpeg', ['-hide_banner', '-loglevel', 'error', '-y', '-start_number', '0', '-i', path.join(out, 'tab_%02d.png'),
    '-frames:v', '1', '-vf', `scale=320:180,tile=4x${rows}:color=white`, '-q:v', '4', path.join(out, 'grid.jpg')]);
} catch (e) {
  console.warn('grid:', e.message);
}

const L = ['# Deck inventory', `${tabs.length} tabs · navigation: ${mode}`, ''];
for (const s of tabs) {
  L.push(`## [${pad(s.index)}] ${s.title || '(no title)'}`, `Image: \`${s.image}\`` + (s.kicker ? ` · kicker: ${s.kicker}` : ''));
  for (const k of s.kpis.slice(0, 12)) L.push(`- KPI: **${k.value}** · ${String(k.label || '').slice(0, 140)}`);
  for (const g of s.charts) {
    let line = `- Chart (${g.source}, ${g.kind}): ${g.title || ''}`;
    if (g.labels) line += ` · labels: ${g.labels.slice(0, 24).join(', ')}`;
    if (g.series) line += ` · bars: ${g.series.map((b) => `${b.cat ?? '?'}=${b.value ?? '?'}`).join(', ')}`;
    for (const d of (g.datasets || []).slice(0, 6)) line += ` · series ${d.label || ''}: ${JSON.stringify(d.data)}`;
    L.push(line.slice(0, 900));
  }
  for (const t of s.tables.slice(0, 4)) {
    L.push('- Table:');
    for (const r of t.rows.slice(0, 12)) L.push('  | ' + r.join(' | ').slice(0, 300));
  }
  for (const c of s.cards.slice(0, 10)) L.push(`- Card: ${c.text.slice(0, 220)}`);
  const tx = (s.text || '').split(/\s+/).join(' ');
  if (tx) L.push(`- Text: ${tx.slice(0, 600)}`);
  L.push('');
}
fs.writeFileSync(path.join(out, 'inventory.md'), L.join('\n'));

console.log(`${tabs.length} tabs (${mode}) -> ${out}`);
for (const s of tabs)
  console.log(`  [${pad(s.index)}] ${JSON.stringify((s.title || '').slice(0, 70))} | kpis=${s.kpis.length} charts=${JSON.stringify(s.charts.map((g) => g.kind))} tables=${s.tables.length} cards=${s.cards.length}`);
