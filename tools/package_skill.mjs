// Builds the uploadable skill: dist/llm-video-engine-skill.zip, a folder `llm-video-engine/` with
// SKILL.md, COMPONENTS.md, the scripts (Node and Python) and the plan schema. The repo stays the single source:
// skill/ for the skill files, schema/ for the schema.
//
//   node tools/package_skill.mjs [--name test-name] [--overlay packages/private/<dir>]
//
// --name builds a side-by-side skill: the folder, the zip and the SKILL.md `name:` all take it, so it
// installs next to the released skill instead of replacing it.
// --overlay adds house defaults that stay out of the repo (keep them under the gitignored packages/private/):
// `defaults.md` is inserted into SKILL.md right before "## Files in this skill", and `replace.json`
// ([[from, to], ...]) replaces exact strings in SKILL.md; a `from` that is not found stops the build.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { writeZip } from '../skill/scripts/lib/zip.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const arg = (flag) => { const i = process.argv.indexOf(flag); return i > 0 ? process.argv[i + 1] : undefined; };
const name = arg('--name') ?? 'llm-video-engine';
const overlay = arg('--overlay') && path.resolve(root, arg('--overlay'));
const entries = [];
const add = (src, dst) => entries.push({ name: `${name}/${dst}`, data: fs.readFileSync(path.join(root, src)) });

for (const f of ['COMPONENTS.md', 'package.json']) add(`skill/${f}`, f);
let skillMd = fs.readFileSync(path.join(root, 'skill/SKILL.md'), 'utf8').replace(/\r\n/g, '\n').replace(/^name: .*$/m, `name: ${name}`);
if (overlay) {
  const rep = path.join(overlay, 'replace.json');
  if (fs.existsSync(rep)) {
    for (const [from, to] of JSON.parse(fs.readFileSync(rep, 'utf8'))) {
      if (!skillMd.includes(from)) throw new Error(`overlay: not found in SKILL.md: ${from}`);
      skillMd = skillMd.split(from).join(to);
    }
  }
  const def = path.join(overlay, 'defaults.md');
  if (fs.existsSync(def)) {
    const anchor = '## Files in this skill';
    if (!skillMd.includes(anchor)) throw new Error(`overlay: "${anchor}" not found in SKILL.md`);
    skillMd = skillMd.replace(anchor, `${fs.readFileSync(def, 'utf8').replace(/\r\n/g, '\n').trim()}\n\n${anchor}`);
  }
}
entries.unshift({ name: `${name}/SKILL.md`, data: Buffer.from(skillMd) });
const walk = (dir) => {
  for (const e of fs.readdirSync(path.join(root, dir), { withFileTypes: true })) {
    const rel = `${dir}/${e.name}`;
    if (e.isDirectory()) walk(rel);
    else if (/\.(mjs|js|py)$/.test(e.name)) add(rel, rel.replace(/^skill\//, ''));
  }
};
walk('skill/scripts');
add('schema/plan.schema.json', 'schema/plan.schema.json');

// Linux engine bundle built by .github/workflows/linux-bundle.yml (gh run download -n linux-x64 -D dist/linux-x64).
// Without it the zip is plan-only and the skill falls back to delivering video-plan.zip.
const bundle = path.join(root, 'dist/linux-x64');
// claude.ai rejects skill zips above 30 MB uncompressed, so the binaries travel as one nested tar.xz
// (counted at its compressed size, 26 MB with both engine builds; gzip gives 40) that the skill unpacks once per session.
if (fs.existsSync(path.join(bundle, 'llm-video-engine'))) {
  const tgz = path.join(root, 'dist/linux-x64.tar.xz');
  // Relative paths: GNU tar (Git Bash) reads "C:" as a remote host.
  execFileSync('tar', ['-cJf', 'linux-x64.tar.xz', '-C', 'linux-x64', 'llm-video-engine', 'llm-video-engine-x86-64', 'llm-video-engine-x86-64-v3', 'ffmpeg', 'ffprobe', 'fonts'], { cwd: path.join(root, 'dist') });
  entries.push({ name: `${name}/bin/linux-x64.tar.xz`, data: fs.readFileSync(tgz) });
} else console.warn('warning: dist/linux-x64 not found, building a plan-only skill');

fs.mkdirSync(path.join(root, 'dist'), { recursive: true });
const out = path.join(root, 'dist', `${name}-skill.zip`);
writeZip(out, entries);
console.log(`${out}\n  ${entries.map((e) => e.name).join('\n  ')}`);
