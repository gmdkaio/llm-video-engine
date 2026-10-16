// Renders one GIF per chart type for the README, from fictitious plans, and rewrites the
// table between <!-- charts:start --> and <!-- charts:end --> in README.md.
//
//   node tools/readme_charts.mjs [--only waterfall,radar]
//
// Needs the engine (engine/build/release or engine/build/linux, or llm-video-engine on PATH) and ffmpeg on PATH.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const outDir = path.join(root, '.github/assets/charts');
const built = ['engine/build/release/llm-video-engine.exe', 'engine/build/linux/llm-video-engine']
  .map((p) => path.join(root, p)).find((p) => fs.existsSync(p));
const engine = built ?? 'llm-video-engine';
const only = (() => { const i = process.argv.indexOf('--only'); return i < 0 ? null : process.argv[i + 1].split(','); })();

const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const xy = (xs, ys) => xs.map((x, i) => ({ x, y: ys[i] }));

// Each entry: README title and line, the scene's speech (it sets the timing), and the chart (or a whole body).
const charts = [
  {
    id: 'kpis', title: 'KPI cards', line: 'Values count up, deltas pop in with their arrow and tone, sparklines draw.',
    kicker: ['Summary', 'September'], head: 'Revenue up,', hl: 'margin tighter',
    speech: 'Revenue rose twelve percent, the margin slipped, and cash closed at four million.',
    body: { items: [
      { dir: 'row', size: 200, items: [
        { type: 'kpi', label: 'Net revenue', value: 5.42, format: 'mi', decimals: 2, delta: { value: 12, format: 'pct', decimals: 0, label: 'vs Aug' }, spark: [4.1, 4.3, 4, 4.6, 4.8, 5.42], at: 'revenue' },
        { type: 'kpi', label: 'EBITDA margin', value: 14.2, format: 'pct', delta: { value: -2.1, suffix: ' pp', decimals: 1, label: 'vs Aug' }, highlight: true, at: 'margin' },
        { type: 'kpi', label: 'Cash', value: 4, format: 'mi', decimals: 1, sub: '3.2 months of fixed costs', at: 'cash' }] },
      { dir: 'row', items: [
        { type: 'kpi', label: 'Active customers', value: 1284, delta: { value: 37, label: 'new' }, at: 'revenue' },
        { type: 'kpi', label: 'Days to collect', value: 42, format: 'days', delta: { value: 5, suffix: ' days', tone: 'bad' }, at: 'margin' },
        { type: 'kpi', label: 'Default rate', value: 2.4, format: 'pct', delta: { text: 'stable', tone: 'neutral' }, at: 'cash' }] }] },
  },
  {
    id: 'table', title: 'Income statement table', line: 'Rows enter in order, totals in bold, then the cost line and the month are highlighted.',
    kicker: ['Income statement', 'thousands'], head: 'Net income of', hl: '300k',
    speech: 'Net revenue grew, but the cost of goods grew faster, and net income for September came to three hundred thousand.',
    body: { items: [{ type: 'table', negative: 'bad', lines: true,
      columns: [{ label: 'Account' }, { label: 'Jul' }, { label: 'Aug' }, { label: 'Sep' }, { label: 'Year to date' }, { label: '% revenue', format: 'pct', decimals: 1, tone: 'text3' }],
      rows: [
        { cells: ['Gross revenue', 5800, 6020, 6310, 18130, 116.4], bold: true },
        { cells: ['(−) Deductions', -880, -910, -890, -2680, -16.4], indent: 1 },
        { cells: ['Net revenue', 4920, 5110, 5420, 15450, 100], total: true },
        { cells: ['(−) Cost of goods', -2950, -3120, -3410, -9480, -62.9], indent: 1 },
        { cells: ['Gross profit', 1970, 1990, 2010, 5970, 37.1], total: true },
        { cells: ['(−) Operating expenses', -1180, -1210, -1240, -3630, -22.9], indent: 1 },
        { cells: ['EBITDA', 790, 780, 770, 2340, 14.2], total: true },
        { cells: ['(−) Depreciation and interest', -420, -440, -470, -1330, -8.7], indent: 1 },
        { cells: ['Net income', 370, 340, 300, 1010, 5.5], total: true, tone: 'accent' }],
      changes: [{ at: 'cost', rows: [3], highlight: true }, { at: 'september', cols: [3], highlight: true }] }] },
  },
  {
    id: 'waterfall', title: 'Waterfall', line: 'From opening to closing balance, step by step; the biggest drops dim the rest.',
    kicker: ['Cash', 'March'], head: 'Cash closed', hl: 'at 410k',
    speech: 'We opened March with five hundred thousand. Payroll and suppliers took the most, and we closed at four hundred and ten.',
    chart: { type: 'chart', y: { suffix: 'k' }, layers: [{
      mark: 'bar', transform: 'waterfall', labels: true, animate: { stagger: 0.25 },
      data: [{ x: 'Opening', y: 500, total: true, tone: 'neutral' }, { x: 'Sales', y: 380, tone: 'good' }, { x: 'Taxes', y: -90, tone: 'bad' },
        { x: 'Payroll', y: -210, tone: 'bad' }, { x: 'Suppliers', y: -150, tone: 'bad' }, { x: 'Other', y: -20, tone: 'bad' }, { x: 'Closing', total: true }],
      changes: [{ at: 'payroll', idx: [3, 4], invert: true, opacity: 0.35 }],
    }] },
  },
  {
    id: 'combo', title: 'Bars, cumulative line and target', line: 'Daily bars, the running total drawn over them, a dashed projection and the target rising into place.',
    kicker: ['Sales', 'month to date'], head: 'On track for', hl: 'the target',
    speech: 'Daily sales add up to the accumulated line. The projection reaches the target before month end.',
    chart: { type: 'chart', margin: { right: 70 },
      legend: [{ label: 'Daily', tone: 'neutral' }, { label: 'Accumulated', mark: 'line' }, { label: 'Projection', mark: 'dash', tone: 'text2' }],
      x: { domain: Array.from({ length: 12 }, (_, i) => String(i + 1)) },
      layers: [
        { mark: 'bar', tone: 'neutral', data: xy(['1', '2', '3', '4', '5', '6', '7', '8'], [12, 15, 9, 14, 13, 16, 11, 15]) },
        { mark: 'line', transform: 'cumulative', labels: { only: 'last', position: 'right' }, animate: { at: 'accumulated' },
          data: xy(['1', '2', '3', '4', '5', '6', '7', '8'], [12, 15, 9, 14, 13, 16, 11, 15]) },
        { mark: 'line', tone: 'text2', dash: [6, 4], animate: { at: 'projection' }, data: xy(['8', '12'], [105, 160]) },
        { mark: 'rule', tone: 'warn', dash: [4, 4], animate: { enter: 'rise', at: 'target' }, data: [{ y: 150, label: 'Target 150' }] },
      ] },
  },
  {
    id: 'stacked', title: 'Stacked bars', line: 'Series pile up per category with the deck\'s own shades and a legend.',
    kicker: ['Receivables', 'by due month'], head: 'Most of it is', hl: 'due in April',
    speech: 'Receivables by due month: April holds the most, mainly from older sales.',
    chart: { type: 'chart', y: { suffix: 'k' },
      legend: [{ label: 'Q4 sales', tone: '#1F4FA8' }, { label: 'Q1 sales', tone: '#3D7BE0' }, { label: 'New sales', tone: '#9CC3FF' }],
      layers: [
        { mark: 'bar', transform: 'stack', tone: '#1F4FA8', data: xy(['Apr', 'May', 'Jun'], [220, 90, 20]) },
        { mark: 'bar', transform: 'stack', tone: '#3D7BE0', data: xy(['Apr', 'May', 'Jun'], [140, 110, 40]) },
        // Labels on the top series show each stack's total.
        { mark: 'bar', transform: 'stack', tone: '#9CC3FF', labels: true, data: xy(['Apr', 'May', 'Jun'], [60, 70, 50]).map((d, i) => ({ ...d, label: ['420', '270', '110'][i] })) },
      ] },
  },
  {
    id: 'scenarios', title: 'Scenario lines', line: 'One line per scenario, threshold rules and labels at the line ends.',
    kicker: ['Cash forecast', 'three scenarios'], head: 'Even the low case', hl: 'stays above the minimum',
    speech: 'In the base case cash grows through the year. Even the pessimistic case stays above the minimum.',
    chart: { type: 'chart', margin: { right: 90 }, y: { suffix: 'k' }, layers: [
      { mark: 'rule', tone: 'bad', dash: [4, 4], animate: { enter: 'rise', at: 'minimum' }, data: [{ y: 150, label: 'Minimum 150', label_pos: 'start' }] },
      { mark: 'line', tone: 'text2', labels: { only: 'last', position: 'right' }, data: xy(months.slice(6), [300, 340, 390, 430, 480, 520]).map((d, i, a) => i === a.length - 1 ? { ...d, label: 'High 520' } : d) },
      { mark: 'line', labels: { only: 'last', position: 'right' }, data: xy(months.slice(6), [300, 320, 350, 370, 400, 420]).map((d, i, a) => i === a.length - 1 ? { ...d, label: 'Base 420' } : d) },
      { mark: 'line', tone: 'warn', labels: { only: 'last', position: 'right' }, animate: { at: 'pessimistic' }, data: xy(months.slice(6), [300, 280, 250, 230, 210, 200]).map((d, i, a) => i === a.length - 1 ? { ...d, label: 'Low 200' } : d) },
    ] },
  },
  {
    id: 'donut', title: 'Donut', line: 'Arcs sweep in order and the center counts up to the share that matters.',
    kicker: ['Revenue', 'by channel'], head: 'Online is now', hl: 'most of revenue',
    speech: 'Online sales are now sixty two percent of revenue, ahead of stores and wholesale.',
    chart: { type: 'chart', coord: 'polar', polar: { start: 0, end: 360 }, layers: [
      { mark: 'arc', inner: 0.7, gap: 2, data: [{ value: 62, label: 'Online' }, { value: 25, tone: 'neutral', label: 'Stores' }, { value: 13, tone: 'neutral2', label: 'Wholesale' }] },
      { mark: 'text', size: 44, weight: 800, animate: { at: 'sixty' }, data: [{ value: 62, format: 'pct', decimals: 0, dy: 12 }] },
    ] },
  },
  {
    id: 'gauge', title: 'Gauge', line: 'Colored bands, a needle that swings from last value to this one, and a counter.',
    kicker: ['Debt', 'net debt / EBITDA'], head: 'Leverage down to', hl: '1.6×',
    speech: 'Leverage fell from two point three to one point six times, back in the low band.',
    chart: { type: 'chart', coord: 'polar', polar: { start: -90, end: 90, min: 0, max: 4, ticks: [0, 2, 3, 4], suffix: '×' }, layers: [
      { mark: 'arc', inner: 0.86, animate: { enter: 'fade' }, data: [{ theta0: 0, theta1: 2, tone: 'good' }, { theta0: 2, theta1: 3, tone: 'warn' }, { theta0: 3, theta1: 4, tone: 'bad' }] },
      { mark: 'rule', animate: { at: 'fell', dur: 1.5 }, data: [{ value: 1.6, from: 2.3 }] },
      { mark: 'text', size: 44, weight: 800, animate: { at: 'fell' }, data: [{ value: 1.6, from: 2.3, format: 'x', decimals: 1, dy: 70, dur: 1.5 }] },
    ] },
  },
  {
    id: 'flow', title: 'Flow (Sankey)', line: 'Inflow split into ribbons that curve to each outflow, and what is left over.',
    kicker: ['Cash flow', 'where the money went'], head: 'Of every dollar in,', hl: '22 cents stayed',
    speech: 'Of the money that came in, payroll and suppliers took most of it, and twenty two cents of each dollar stayed.',
    chart: { type: 'chart', x: { type: 'linear', min: 0, max: 10, labels: false }, y: { min: 0, max: 120, labels: false, grid: false }, layers: [
      { mark: 'rect', opacity: 1, animate: { enter: 'grow' }, data: [{ x0: 1, x1: 1.25, y0: 0, y1: 100 }] },
      { mark: 'text', anchor: 'end', size: 13, data: [{ x: 0.85, y: 50, text: 'Inflow 100' }] },
      { mark: 'ribbon', tone: 'bad', animate: { at: 'payroll', stagger: 0.15 }, data: [
        { x0: 1.25, y0: [100, 64], x1: 6.5, y1: [120, 84] }, { x0: 1.25, y0: [64, 36], x1: 6.5, y1: [76, 48] }, { x0: 1.25, y0: [36, 22], x1: 6.5, y1: [40, 26] }] },
      { mark: 'ribbon', animate: { at: 'stayed' }, data: [{ x0: 1.25, y0: [22, 0], x1: 6.5, y1: [22, 0] }] },
      { mark: 'text', anchor: 'start', size: 13, animate: { at: { word: 'payroll', offset: 0.8 }, stagger: 0.15 }, data: [
        { x: 6.65, y: 102, dy: 4, text: 'Payroll  −36' }, { x: 6.65, y: 62, dy: 4, text: 'Suppliers  −28' }, { x: 6.65, y: 33, dy: 4, text: 'Taxes  −14' }] },
      { mark: 'text', anchor: 'start', size: 13, tone: 'accent', animate: { at: 'stayed' }, data: [{ x: 6.65, y: 11, dy: 4, text: 'Left over  +22' }] },
    ] },
  },
  {
    id: 'dual-axis', title: 'Dual axis', line: 'Bars on the left scale, a percentage line on its own scale on the right.',
    kicker: ['Results', 'quarterly'], head: 'Revenue and', hl: 'margin both up',
    speech: 'Revenue grew every quarter but the third, and the margin followed it up to eighteen percent.',
    chart: { type: 'chart', y: { suffix: 'k' }, y2: { suffix: '%', tone: 'warn' },
      legend: [{ label: 'Revenue' }, { label: 'Margin', mark: 'line', tone: 'warn' }],
      layers: [
        { mark: 'bar', data: xy(['Q1', 'Q2', 'Q3', 'Q4'], [1200, 1500, 1100, 1800]) },
        { mark: 'line', axis: 'y2', tone: 'warn', markers: true, labels: { suffix: '%' }, animate: { at: 'margin' }, data: xy(['Q1', 'Q2', 'Q3', 'Q4'], [12, 15, 9, 18]) },
      ] },
  },
  {
    id: 'radar', title: 'Radar', line: 'Two profiles over the same axes, filled shape growing from the center.',
    kicker: ['Survey', 'us vs market'], head: 'Ahead on', hl: 'speed and quality',
    speech: 'Against the market average we lead on speed and quality, and trail on service.',
    chart: { type: 'chart', coord: 'polar', polar: { start: 0, end: 360, r: { max: 5, step: 1 } },
      legend: [{ label: 'Us' }, { label: 'Market', mark: 'dash', tone: 'warn' }],
      layers: [
        { mark: 'line', tone: 'warn', dash: [5, 4], data: xy(['Price', 'Quality', 'Service', 'Speed', 'Reach'], [4, 3, 4, 3, 4]) },
        { mark: 'area', animate: { at: 'lead' }, data: xy(['Price', 'Quality', 'Service', 'Speed', 'Reach'], [3, 4.5, 2.5, 5, 3.5]) },
        { mark: 'line', markers: true, animate: { at: 'lead' }, data: xy(['Price', 'Quality', 'Service', 'Speed', 'Reach'], [3, 4.5, 2.5, 5, 3.5]) },
      ] },
  },
  {
    id: 'scatter', title: 'Scatter and bubbles', line: 'Points on two value scales, bubble size per point, and a ping on the outlier.',
    kicker: ['Stores', 'size vs margin'], head: 'One store is', hl: 'the outlier',
    speech: 'Bigger stores usually earn more, except store nine, which loses money.',
    chart: { type: 'chart', x: { type: 'linear', min: 0, max: 100, step: 25 }, y: { min: -10, max: 40, step: 10, suffix: '%' }, layers: [
      { mark: 'point', data: [[10, 8, 6], [22, 14, 8], [35, 18, 10], [48, 22, 12], [60, 25, 14], [72, 30, 16], [85, 33, 18]].map(([x, y, size]) => ({ x, y, size })) },
      { mark: 'point', tone: 'bad', animate: { at: 'except' }, data: [{ x: 66, y: -5, size: 14 }] },
      { mark: 'point', tone: 'bad', style: 'ring', ping: true, animate: { at: 'loses' }, data: [{ x: 66, y: -5, size: 14 }] },
      { mark: 'text', anchor: 'start', tone: 'bad', animate: { at: 'loses' }, data: [{ x: 66, y: -5, dx: 24, dy: 4, text: 'Store 9' }] },
    ] },
  },
  {
    id: 'heatmap', title: 'Heatmap', line: 'One cell per pair, intensity as opacity, rows labelled on the left.',
    kicker: ['Traffic', 'by weekday and region'], head: 'Busiest on', hl: 'Thursdays',
    speech: 'Traffic peaks on Thursdays in the south and on Wednesdays in the north.',
    chart: { type: 'chart', x: { domain: ['Mon', 'Tue', 'Wed', 'Thu', 'Fri'] }, y: { min: 0, max: 3, labels: false, grid: false }, margin: { left: 70 }, layers: [
      { mark: 'rect', opacity: 1, animate: { stagger: 0.04 },
        data: [[0.2, 0.5, 0.9, 0.4, 0.1], [0.7, 0.3, 0.6, 1, 0.5], [0.1, 0.8, 0.2, 0.3, 0.9]].flatMap((row, r) => row.map((o, i) => ({ x0: i, x1: i, y0: r + 0.05, y1: r + 0.95, opacity: o }))) },
      { mark: 'text', anchor: 'end', data: ['North', 'South', 'East'].map((t, r) => ({ x: 0, y: r + 0.5, dx: -128, dy: 4, text: t })) },
      { mark: 'rect', tone: 'text', opacity: 0, stroke: 'text', width: 2, animate: { at: 'thursdays' }, data: [{ x0: 3, x1: 3, y0: 1.05, y1: 1.95 }] },
    ] },
  },
  {
    id: 'gantt', title: 'Gantt', line: 'Horizontal bars from start to end per task, and a rule for today.',
    kicker: ['Project', 'rollout plan'], head: 'Testing starts', hl: 'next week',
    speech: 'Design is done and the build is halfway. Testing starts next week, and launch is in week eleven.',
    chart: { type: 'chart', orient: 'horizontal', y: { min: 0, max: 12, step: 3, suffix: ' wk' }, layers: [
      { mark: 'bar', animate: { stagger: 0.3 }, data: [{ x: 'Design', y0: 0, y1: 3, tone: 'neutral' }, { x: 'Build', y0: 2, y1: 8 }, { x: 'Testing', y0: 6, y1: 10, tone: 'warn' }, { x: 'Launch', y0: 10, y1: 11, tone: 'good' }] },
      { mark: 'rule', tone: 'bad', dash: [4, 4], animate: { at: 'halfway' }, data: [{ y: 5, label: 'today' }] },
    ] },
  },
  {
    id: 'funnel', title: 'Funnel', line: 'Closed shapes narrowing stage by stage, with the counts inside.',
    kicker: ['Sales funnel', 'March'], head: 'Eight percent of visits', hl: 'became sales',
    speech: 'Of one thousand visits, four hundred and fifty became leads and eighty became sales.',
    chart: { type: 'chart', x: { type: 'linear', min: -100, max: 100, labels: false }, y: { min: 0, max: 4, labels: false, grid: false }, layers: [
      { mark: 'path', opacity: 1, tone: 'accent', animate: { enter: 'slide', stagger: 0.35 },
        data: [[100, 80, 3], [80, 45, 2], [45, 20, 1], [20, 8, 0]].map(([a, b, r]) => ({ closed: true, points: [[-a, r + 0.95], [a, r + 0.95], [b, r + 0.05], [-b, r + 0.05]] })) },
      { mark: 'text', tone: 'bg', animate: { stagger: 0.35 }, data: [['Visits 1,000', 3.5], ['Leads 450', 2.5], ['Trials 200', 1.5], ['Sales 80', 0.5]].map(([text, y]) => ({ x: 0, y, dy: 4, text })) },
    ] },
  },
  {
    id: 'bullet', title: 'Bullet', line: 'Value against its ranges and target, one row per metric.',
    kicker: ['Targets', 'year to date'], head: 'Margin is', hl: 'below target',
    speech: 'Revenue is past its target, but margin is still below it.',
    chart: { type: 'chart', orient: 'horizontal', y: { min: 0, max: 120, step: 20, suffix: '%' }, layers: [
      { mark: 'bar', size: 0.8, tone: 'grid', animate: { enter: 'none' }, data: [{ x: 'Revenue', y: 120 }, { x: 'Margin', y: 120 }] },
      { mark: 'bar', size: 0.8, tone: 'neutral2', animate: { enter: 'fade' }, data: [{ x: 'Revenue', y: 80 }, { x: 'Margin', y: 70 }] },
      { mark: 'bar', size: 0.3, labels: { suffix: '%' }, animate: { stagger: 0.8 }, data: [{ x: 'Revenue', y: 104 }, { x: 'Margin', y: 55, tone: 'bad', at: 'margin' }] },
      { mark: 'rule', tone: 'text', width: 3, data: [{ y: 100, x0: 'Revenue', x1: 'Revenue' }, { y: 60, x0: 'Margin', x1: 'Margin' }] },
    ] },
  },
];

const theme = { bg: '#07090D', text: '#FFFFFF', accent: '#5B8CFF', good: '#2BC48A', bad: '#F0605D', warn: '#F2B544', panel: '#10141B' };
fs.mkdirSync(outDir, { recursive: true });
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'lve-readme-'));

for (const c of charts) {
  if (only && !only.includes(c.id)) continue;
  const plan = {
    schema: 'llm-video-engine/plan@1', output: c.title, config: { locale: 'en-US', formats: ['720'] }, theme,
    header: { period: 'Example · fictitious data' },
    scenes: [{ type: 'content', tab: c.title, speech: c.speech, kicker: c.kicker, title: c.head, highlight: c.hl, body: c.body || { items: [c.chart] } }],
  };
  const planPath = path.join(tmp, `${c.id}.json`), mp4 = path.join(tmp, `${c.id}.mp4`), gif = path.join(outDir, `${c.id}.gif`);
  fs.writeFileSync(planPath, JSON.stringify(plan, null, 1));
  execFileSync(engine, ['validate', planPath], { stdio: 'inherit' });
  execFileSync(engine, ['render', planPath, '--out', mp4], { stdio: 'ignore' });
  // Drop the fade-out at the end, then a 2-pass palette GIF at 640 px, 15 fps.
  const dur = Number(execFileSync('ffprobe', ['-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', mp4]).toString());
  execFileSync('ffmpeg', ['-y', '-v', 'error', '-t', String(Math.max(1, dur - 0.5)), '-i', mp4, '-vf',
    'fps=15,scale=640:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=96:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle', gif]);
  console.log(`${path.relative(root, gif)}  ${(fs.statSync(gif).size / 1024).toFixed(0)} KB`);
}
fs.rmSync(tmp, { recursive: true, force: true });

// README table: two charts per row.
const cell = (c) => `<td width="50%" valign="top">
<img src=".github/assets/charts/${c.id}.gif" alt="${c.title}" width="100%"><br>
<b>${c.title}</b><br>
${c.line}
</td>`;
let table = '<table>\n';
for (let i = 0; i < charts.length; i += 2) table += `<tr>\n${charts.slice(i, i + 2).map(cell).join('\n')}\n</tr>\n`;
table += '</table>';
const readme = path.join(root, 'README.md');
const text = fs.readFileSync(readme, 'utf8');
const start = '<!-- charts:start -->', end = '<!-- charts:end -->';
if (!text.includes(start)) throw new Error(`README.md has no ${start} marker`);
fs.writeFileSync(readme, text.replace(new RegExp(`${start}[\\s\\S]*?${end}`), `${start}\n${table}\n${end}`));
console.log('README.md table updated');
