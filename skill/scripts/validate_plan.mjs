// Validates a plan against the plan schema and checks every `at` against the scene's speech.
// No dependencies: it implements the subset of JSON Schema the plan schema uses.
//
//   node validate_plan.mjs plan.json
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
// In the packaged skill the schema sits in <skill>/schema; in the repo, in <repo>/schema.
const schemaPath = [path.join(here, '../schema/plan.schema.json'), path.join(here, '../../schema/plan.schema.json')].find((p) => fs.existsSync(p));
if (!schemaPath) { console.error('plan.schema.json not found next to the skill'); process.exit(2); }
const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));
const planPath = process.argv[2];
if (!planPath) { console.error('usage: node validate_plan.mjs plan.json'); process.exit(2); }
const plan = JSON.parse(fs.readFileSync(planPath, 'utf8'));

const typeOf = (v) => (v === null ? 'null' : Array.isArray(v) ? 'array' : Number.isInteger(v) ? 'integer' : typeof v);
const isType = (v, t) => t === 'number' ? typeof v === 'number' : t === 'integer' ? Number.isInteger(v) : typeOf(v) === t;
const resolve = (s) => {
  while (s && s.$ref) s = s.$ref.slice(2).split('/').reduce((o, k) => o[k], schema);
  return s;
};

// Returns true when `v` matches `s`; pushes messages into `errs` (pass null to check silently).
function check(s, v, at, errs) {
  s = resolve(s);
  if (s === true || s === undefined) return true;
  if (s === false) { errs?.push(`${at} is not allowed`); return false; }
  let ok = true;
  const fail = (m) => { ok = false; errs?.push(`${at} ${m}`); };
  if ('const' in s && v !== s.const) fail(`must be ${JSON.stringify(s.const)}`);
  if (s.enum && !s.enum.some((e) => e === v)) fail(`must be one of ${s.enum.map((e) => JSON.stringify(e)).join(', ')}`);
  if (s.type) {
    const ts = [].concat(s.type);
    if (!ts.some((t) => isType(v, t))) { fail(`must be ${ts.join(' or ')}`); return false; }
  }
  if (typeof v === 'string') {
    if (s.minLength && v.length < s.minLength) fail('must not be empty');
    if (s.pattern && !new RegExp(s.pattern).test(v)) fail(`must match ${s.pattern}`);
  }
  if (typeof v === 'number') {
    if ('minimum' in s && v < s.minimum) fail(`must be >= ${s.minimum}`);
    if ('maximum' in s && v > s.maximum) fail(`must be <= ${s.maximum}`);
    if ('exclusiveMinimum' in s && v <= s.exclusiveMinimum) fail(`must be > ${s.exclusiveMinimum}`);
  }
  if (Array.isArray(v)) {
    if (s.minItems && v.length < s.minItems) fail(`must have at least ${s.minItems} item(s)`);
    if (s.items) v.forEach((x, i) => { if (!check(s.items, x, `${at}/${i}`, errs)) ok = false; });
  }
  if (typeOf(v) === 'object') {
    for (const r of s.required || []) if (!(r in v)) fail(`must have property '${r}'`);
    for (const [k, x] of Object.entries(v)) {
      if (s.properties && k in s.properties) { if (!check(s.properties[k], x, `${at}/${k}`, errs)) ok = false; }
      else if (s.additionalProperties === false) fail(`has unknown property '${k}'`);
      else if (typeof s.additionalProperties === 'object' && !check(s.additionalProperties, x, `${at}/${k}`, errs)) ok = false;
    }
  }
  for (const sub of s.allOf || []) if (!check(sub, v, at, errs)) ok = false;
  if (s.if) {
    const branch = check(s.if, v, at, null) ? s.then : s.else;
    if (branch && !check(branch, v, at, errs)) ok = false;
  }
  if (s.oneOf) {
    const n = s.oneOf.filter((sub) => check(sub, v, at, null)).length;
    if (n !== 1) fail('does not match any allowed form');
  }
  return ok;
}

const errors = [];
check(schema, plan, '', errors);
let problems = 0;
for (const e of [...new Set(errors)]) { console.log('schema ·', e.trim() || '/'); problems++; }

// `at` words: same normalization as the engine (lowercase, no accents, no punctuation).
const norm = (w) => w.toLowerCase().normalize('NFD').replace(/\p{Mn}/gu, '').replace(/[^\p{L}\p{N}_%$]/gu, '');
(plan.scenes || []).forEach((s, k) => {
  const words = (s.speech || '').replace(/\[[^\]]*\]/g, ' ').split(/\s+/).map(norm).filter(Boolean);
  const walk = (o, where) => {
    if (Array.isArray(o)) return o.forEach((v, i) => walk(v, `${where}[${i}]`));
    if (!o || typeof o !== 'object') return;
    for (const [key, v] of Object.entries(o)) {
      if ((key === 'at' || key.startsWith('at_')) && (typeof v === 'string' || (v && typeof v === 'object' && 'word' in v))) {
        const spec = typeof v === 'string' ? { word: v } : v;
        let hits = 0;
        try { const rx = new RegExp(spec.word, 'i'); hits = words.filter((w) => rx.test(w)).length; } catch { hits = -1; }
        if (hits < 0) { console.log(`scene ${k + 1} · AT · ${where}.${key} "${spec.word}" is not a valid regex`); problems++; }
        else if (hits < (spec.n || 1)) { console.log(`scene ${k + 1} · AT · ${where}.${key} "${spec.word}" matches ${hits} word(s), needs ${spec.n || 1}`); problems++; }
      } else walk(v, `${where}.${key}`);
    }
  };
  walk(s, `scene ${k + 1}`);
});

// Duration, estimated like the engine does it (core/at.h, core/timeline.h): 2.7 words/s, pauses at sentence
// ends and commas, 1 s of silence after each narrated scene.
let total = 0;
for (const s of plan.scenes || []) {
  const ws = (s.speech || '').replace(/\[[^\]]*\]/g, ' ').split(/\s+/).filter((w) => norm(w));
  if (ws.length) {
    let t = 0.3 + 1.0;
    for (const w of ws) t += 1 / 2.7 + (/[.!?:]$/.test(w) ? 0.45 : /,$/.test(w) ? 0.15 : 0);
    total += t;
  } else total += s.duration ?? (s.type === 'outro' ? 4 : s.type === 'intro' ? 2.6 : 5);
}
const target = plan.config?.target_duration;
if (target && Math.abs(total - target) > target * 0.15) {
  const words = (plan.scenes || []).reduce((n, s) => n + (s.speech || '').split(/\s+/).filter((w) => norm(w)).length, 0);
  console.log(`plan · DURATION · about ${Math.round(total)} s for target ${target} s (${words} spoken words); ` +
    `${total > target ? 'cut' : 'add'} about ${Math.round(Math.abs(total - target) * 2.7)} words`);
  problems++;
}

// Files the plan references must exist next to it.
const base = path.dirname(path.resolve(planPath));
const files = [...Object.entries(plan.logos || {}).filter(([k, v]) => typeof v === 'string' && !k.endsWith('color')).map(([, v]) => v),
  ...(plan.scenes || []).filter((s) => s.image).map((s) => s.image)];
for (const f of files) if (!fs.existsSync(path.join(base, f))) { console.log(`file · ${f} is referenced but missing`); problems++; }

console.log(problems ? `${problems} problem(s)` : `plan ok (about ${Math.round(total)} s)`);
process.exit(problems ? 1 : 0);
