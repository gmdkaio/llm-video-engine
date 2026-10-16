// Renders one GIF per measured-motion module for the README, from fictitious plans, and rewrites the
// table between <!-- motion:start --> and <!-- motion:end --> in README.md.
//
//   node tools/readme_motion.mjs [--only whip,dive]
//
// Needs the engine (engine/build/release or engine/build/linux, or llm-video-engine on PATH) and ffmpeg on PATH.
// The GIFs run at 25 fps, not the charts' 15, because these moves live in single frames.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const outDir = path.join(root, '.github/assets/motion');
const built = ['engine/build/release/llm-video-engine.exe', 'engine/build/linux/llm-video-engine']
  .map((p) => path.join(root, p)).find((p) => fs.existsSync(p));
const engine = built ?? 'llm-video-engine';
const only = (() => { const i = process.argv.indexOf('--only'); return i < 0 ? null : process.argv[i + 1].split(','); })();

const theme = { bg: '#07090D', text: '#FFFFFF', accent: '#5B8CFF', good: '#2BC48A', bad: '#F0605D', warn: '#F2B544', panel: '#10141B' };
const brand = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 360 96"><rect x="0" y="12" width="72" height="72" rx="18" fill="#5B8CFF"/>
<path d="M22 60 L36 30 L50 60 Z" fill="#07090D"/><text x="92" y="66" font-family="Montserrat, sans-serif" font-size="46" font-weight="800" fill="#FFFFFF">Northwind</text></svg>`;
const mark = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 72 72"><rect width="72" height="72" rx="18" fill="#5B8CFF"/><path d="M22 52 L36 22 L50 52 Z" fill="#07090D"/></svg>`;

// Scenes reused by several modules.
const revenue = {
  type: 'content', tab: 'Revenue', speech: 'Revenue grew twelve percent over the quarter, driven by the new enterprise plan.',
  kicker: ['Q3', 'revenue'], title: 'Revenue grew', highlight: '12% this quarter', chip: { text: 'Record quarter', tone: 'good', at: 'enterprise' },
  body: { dir: 'row', items: [
    { panel: true, items: [
      { type: 'number', value: 1284500, prefix: '$', at: 'revenue' },
      { type: 'text', text: 'Driven by the <b>enterprise plan</b>, now 41% of new bookings.', at: 'driven' },
      { type: 'chips', items: [{ text: '+12% QoQ', tone: 'good' }, { text: 'Churn 2.1%', tone: 'warn' }] }] },
    { type: 'kv', title: 'Breakdown', rows: [['Enterprise', '$527k'], ['Team', '$498k'], ['Starter', '$259k']], at: 'quarter' }] },
};
const cash = {
  type: 'content', tab: 'Cash', speech: 'Cash closed at four million, three months of fixed costs.',
  kicker: ['Cash', 'September'], title: 'Cash closed at', highlight: '$4.0M',
  body: { dir: 'row', items: [
    { type: 'kpi', label: 'Cash', value: 4, format: 'mi', decimals: 1, sub: '3.2 months of fixed costs', at: 'cash' },
    { type: 'kpi', label: 'Burn', value: 1.25, format: 'mi', decimals: 2, delta: { value: -8, format: 'pct', decimals: 0, label: 'vs Aug', tone: 'good' }, at: 'three' }] },
};
const title = (t, hl, speech) => ({ type: 'content', tab: 'Title', speech, kicker: ['Summary', 'September'], title: t, highlight: hl,
  body: { items: [{ type: 'text', text: speech, at: 1.6 }] } });

// The deck tab for the slide module is itself a frame drawn by the engine.
const tabPlan = { schema: 'llm-video-engine/plan@1', output: 'tab', config: { locale: 'en-US', formats: ['720'] }, theme,
  header: { period: 'Deck · fictitious data' },
  scenes: [{ type: 'content', tab: 'Targets', speech: 'Revenue is past its target and margin is below it.', kicker: ['Targets', 'year to date'],
    title: 'Revenue ahead,', highlight: 'margin behind',
    body: { dir: 'row', items: [
      { type: 'kpi', label: 'Revenue vs target', value: 104, format: 'pct', decimals: 0, delta: { value: 4, suffix: ' pp', tone: 'good' } },
      { type: 'kpi', label: 'Margin vs target', value: 55, format: 'pct', decimals: 0, delta: { value: -5, suffix: ' pp', tone: 'bad' } }] } }] };

// Each entry: README title and line, the scenes, config extras, render flags, and the seconds to keep [from, to].
const modules = [
  {
    id: 'words', title: 'Titles, word by word', line: 'Each word lands whole with a short smear: a beat after the first, quick in the middle, a slower step into the last.',
    scenes: [title('Margin slipped', 'two points', 'Revenue grew, but the margin slipped two points.')], keep: [0, 2.6],
  },
  {
    id: 'land', title: 'Panels land, counters step', line: 'Panels arrive in about 12 frames and stop; the counter changes once per frame, in steps that shrink.',
    scenes: [revenue], keep: [0, 4.6],
  },
  {
    id: 'dive', title: 'Intro: oversized logo, then a dive', line: 'The logo arrives too big and settles, then multiplies its scale every frame into the cut.',
    scenes: [{ type: 'intro', sub: 'Quarterly results · fictitious data', duration: 2.8 }, cash], keep: [0, 3.6],
  },
  {
    id: 'exit', title: 'Accelerating exits', line: 'Scenes leave by speeding up, at full speed on the cut, instead of easing out.',
    scenes: [cash, title('Next quarter', 'looks steadier', 'Next quarter looks steadier.')], at: 'cut', keep: [-1.3, 1.2],
  },
  {
    id: 'snap', title: 'Camera snaps', line: 'On a captured deck tab the camera covers most of each move in one frame, then settles.',
    scenes: [{ type: 'slide', tab: 'Deck', image: 'tab.png', speech: 'Here is the deck tab. Revenue cleared its target, and margin is the one to watch.',
      focus: [{ bbox: [40, 250, 590, 330], at: 'revenue', callout: 'Revenue at 104% of target' }, { bbox: [650, 250, 590, 330], at: 'margin', callout: 'Margin at 55%' }] }],
    keep: [1.6, 5.6],
  },
  {
    id: 'whip', title: 'Whip transitions', line: '<code>transition: "whip"</code>: the old scene whips out at full speed and the new one cuts in already moving.',
    config: { transition: 'whip' }, scenes: [cash, revenue], at: 'cut', keep: [-0.8, 1.6],
  },
  {
    id: 'twos', title: 'On twos', line: '<code>clock: "twos"</code> holds every pose for two frames, for a hand-posed look.',
    config: { clock: 'twos' }, scenes: [revenue], keep: [0, 4.6],
  },
  {
    id: 'shutter', title: 'Motion blur', line: '<code>--shutter 8</code> averages sub-frames over a 180° shutter, so fast moves blur by their own speed.',
    config: { transition: 'whip' }, flags: ['--shutter', '8'], scenes: [cash, revenue], at: 'cut', keep: [-0.8, 1.6],
  },
];

fs.mkdirSync(outDir, { recursive: true });
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'lve-motion-'));
fs.writeFileSync(path.join(tmp, 'brand.svg'), brand);
fs.writeFileSync(path.join(tmp, 'mark.svg'), mark);
fs.writeFileSync(path.join(tmp, 'tab.json'), JSON.stringify(tabPlan));
execFileSync(engine, ['frame', path.join(tmp, 'tab.json'), '--at', '30', '--out', path.join(tmp, 'tab.png')], { stdio: 'ignore' });

for (const m of modules) {
  if (only && !only.includes(m.id)) continue;
  const plan = {
    schema: 'llm-video-engine/plan@1', output: m.title, config: { locale: 'en-US', formats: ['720'], motion: 'measured', ...m.config }, theme,
    logos: { brand: 'brand.svg', brand_mark: 'mark.svg' }, header: { period: 'Example · fictitious data' }, scenes: m.scenes,
  };
  const planPath = path.join(tmp, `${m.id}.json`), mp4 = path.join(tmp, `${m.id}.mp4`), gif = path.join(outDir, `${m.id}.gif`);
  fs.writeFileSync(planPath, JSON.stringify(plan, null, 1));
  execFileSync(engine, ['validate', planPath], { stdio: 'inherit' });
  execFileSync(engine, ['render', planPath, '--out', mp4, ...(m.flags ?? [])], { stdio: 'ignore' });
  // `keep` is relative to the video start, or to the first cut (end of the first scene) with at: 'cut'.
  let t0 = 0;
  if (m.at === 'cut') {
    const one = path.join(tmp, `${m.id}-1.json`);
    fs.writeFileSync(one, JSON.stringify({ ...plan, scenes: m.scenes.slice(0, 1) }));
    t0 = sceneEnd(one);
  }
  const [a, b] = m.keep.map((k) => Math.max(0, t0 + k));
  execFileSync('ffmpeg', ['-y', '-v', 'error', '-ss', a.toFixed(3), '-t', (b - a).toFixed(3), '-i', mp4, '-vf',
    'fps=25,scale=640:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=96:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle', gif]);
  console.log(`${path.relative(root, gif)}  ${(fs.statSync(gif).size / 1024).toFixed(0)} KB`);
}
fs.rmSync(tmp, { recursive: true, force: true });

// Length of a one-scene plan, from the engine's own report ("video 7.3 s, ...").
function sceneEnd(planPath) {
  const out = execFileSync(engine, ['render', planPath, '--null'], { encoding: 'utf8' });
  const s = /video ([\d.]+) s/.exec(out);
  if (!s) throw new Error(`no duration in: ${out}`);
  return Number(s[1]);
}

// README table: two modules per row.
const cell = (m) => `<td width="50%" valign="top">
<img src=".github/assets/motion/${m.id}.gif" alt="${m.title}" width="100%"><br>
<b>${m.title}</b><br>
${m.line}
</td>`;
let table = '<table>\n';
for (let i = 0; i < modules.length; i += 2) table += `<tr>\n${modules.slice(i, i + 2).map(cell).join('\n')}\n</tr>\n`;
table += '</table>';
const readme = path.join(root, 'README.md');
const text = fs.readFileSync(readme, 'utf8');
const start = '<!-- motion:start -->', end = '<!-- motion:end -->';
if (!text.includes(start)) throw new Error(`README.md has no ${start} marker`);
fs.writeFileSync(readme, text.replace(new RegExp(`${start}[\\s\\S]*?${end}`), `${start}\n${table}\n${end}`));
console.log('README.md table updated');
