// Validates a plan against schema/plan.schema.json and checks every `at` against the scene's speech.
//
//   node validate_plan.mjs plan.json
import Ajv from 'ajv/dist/2020.js';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const schema = JSON.parse(fs.readFileSync(path.join(here, '../../schema/plan.schema.json'), 'utf8'));
const planPath = process.argv[2];
if (!planPath) { console.error('usage: node validate_plan.mjs plan.json'); process.exit(2); }
const plan = JSON.parse(fs.readFileSync(planPath, 'utf8'));

const ajv = new Ajv({ allErrors: true, strict: false });
const validate = ajv.compile(schema);
let problems = 0;
if (!validate(plan)) {
  // oneOf/if-then produce noisy chains; keep the most specific message per location.
  const seen = new Set();
  for (const e of validate.errors) {
    if (['oneOf', 'if', 'anyOf'].includes(e.keyword)) continue;
    const msg = `${e.instancePath || '/'} ${e.message}${e.params.additionalProperty ? `: ${e.params.additionalProperty}` : ''}`;
    if (!seen.has(msg)) { seen.add(msg); console.log('schema ·', msg); problems++; }
  }
}

// `at` words: same normalization as the engine.
const norm = (w) => w.toLowerCase().normalize('NFD').replace(/\p{Mn}/gu, '').replace(/[^\p{L}\p{N}_%$]/gu, '');
plan.scenes.forEach((s, k) => {
  const words = (s.speech || '').replace(/\[[^\]]*\]/g, ' ').split(/\s+/).map(norm).filter(Boolean);
  const walk = (o, where) => {
    if (Array.isArray(o)) return o.forEach((v, i) => walk(v, `${where}[${i}]`));
    if (!o || typeof o !== 'object') return;
    for (const [key, v] of Object.entries(o)) {
      if ((key === 'at' || key.startsWith('at_')) && (typeof v === 'string' || (v && typeof v === 'object' && 'word' in v))) {
        const spec = typeof v === 'string' ? { word: v } : v;
        const rx = new RegExp(spec.word, 'i');
        const hits = words.filter((w) => rx.test(w)).length;
        if (hits < (spec.n || 1)) { console.log(`scene ${k + 1} · AT · ${where}.${key} "${spec.word}" matches ${hits} word(s), needs ${spec.n || 1}`); problems++; }
      } else walk(v, `${where}.${key}`);
    }
  };
  walk(s, `scene ${k + 1}`);
});
console.log(problems ? `${problems} problem(s)` : 'plan ok');
process.exit(problems ? 1 : 0);
