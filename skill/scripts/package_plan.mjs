// Packages a validated plan for local rendering: plan.json, every file it references
// (logos, tab images) and the narration text, in one zip.
//
//   node package_plan.mjs plan.json --narration narration.txt [--out video-plan.zip]
//
// The zip is what the user downloads and renders with `llm-video-engine render video-plan.zip`.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { writeZip } from './lib/zip.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const opt = (k) => { const i = args.indexOf(k); return i < 0 ? null : args[i + 1]; };
const planPath = args[0];
if (!planPath) { console.error('usage: node package_plan.mjs plan.json --narration narration.txt [--out video-plan.zip]'); process.exit(2); }

// The TTS text travels with the plan; an empty one means the script step was skipped.
const narration = opt('--narration');
const narrationText = narration && fs.existsSync(narration) ? fs.readFileSync(narration, 'utf8').trim() : '';
if (!narrationText) {
  console.error(narration ? `${narration} is missing or empty: write the TTS text first` : '--narration narration.txt is required');
  process.exit(1);
}

// Never package a plan that does not validate.
try {
  execFileSync(process.execPath, [path.join(here, 'validate_plan.mjs'), planPath], { stdio: 'inherit' });
} catch {
  console.error('fix the problems above before packaging');
  process.exit(1);
}

const base = path.dirname(path.resolve(planPath));
const plan = JSON.parse(fs.readFileSync(planPath, 'utf8'));
const refs = new Set([
  ...Object.entries(plan.logos || {}).filter(([k, v]) => typeof v === 'string' && !k.endsWith('color')).map(([, v]) => v),
  ...(plan.scenes || []).filter((s) => s.image).map((s) => s.image),
]);
const entries = [{ name: 'plan.json', data: fs.readFileSync(planPath) }];
for (const r of refs) entries.push({ name: r.replace(/\\/g, '/'), data: fs.readFileSync(path.join(base, r)) });
entries.push({ name: 'narration.txt', data: Buffer.from(narrationText + '\n', 'utf8') });

const out = opt('--out') || path.join(base, 'video-plan.zip');
writeZip(out, entries);
console.log(`${out}: ${entries.map((e) => e.name).join(', ')}`);
