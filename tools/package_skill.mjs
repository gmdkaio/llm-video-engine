// Builds the uploadable skill: dist/llm-video-engine-skill.zip, a folder `llm-video-engine/` with
// SKILL.md, COMPONENTS.md, the scripts and the plan schema. The repo stays the single source:
// skill/ for the skill files, schema/ for the schema.
//
//   node tools/package_skill.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { writeZip } from '../skill/scripts/lib/zip.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const name = 'llm-video-engine';
const entries = [];
const add = (src, dst) => entries.push({ name: `${name}/${dst}`, data: fs.readFileSync(path.join(root, src)) });

for (const f of ['SKILL.md', 'COMPONENTS.md', 'package.json']) add(`skill/${f}`, f);
const walk = (dir) => {
  for (const e of fs.readdirSync(path.join(root, dir), { withFileTypes: true })) {
    const rel = `${dir}/${e.name}`;
    if (e.isDirectory()) walk(rel);
    else if (/\.(mjs|js)$/.test(e.name)) add(rel, rel.replace(/^skill\//, ''));
  }
};
walk('skill/scripts');
add('schema/plan.schema.json', 'schema/plan.schema.json');

// Linux engine bundle built by .github/workflows/linux-bundle.yml (gh run download -n linux-x64 -D dist/linux-x64).
// Without it the zip is plan-only and the skill falls back to delivering video-plan.zip.
const bundle = path.join(root, 'dist/linux-x64');
if (fs.existsSync(path.join(bundle, 'llm-video-engine'))) {
  for (const f of ['llm-video-engine', 'ffmpeg', 'ffprobe', 'fonts/Montserrat-VF.ttf', 'fonts/OFL.txt']) add(`dist/linux-x64/${f}`, `bin/linux-x64/${f}`);
} else console.warn('warning: dist/linux-x64 not found, building a plan-only skill');

fs.mkdirSync(path.join(root, 'dist'), { recursive: true });
const out = path.join(root, 'dist', `${name}-skill.zip`);
writeZip(out, entries);
console.log(`${out}\n  ${entries.map((e) => e.name).join('\n  ')}`);
