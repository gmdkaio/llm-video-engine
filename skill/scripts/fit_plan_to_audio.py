"""Retime a plan to real narration clips: the engine paces speech at 2.7 words/s plus punctuation pauses, so for
each narrated scene we glue adjacent words with hyphens (the engine counts a hyphenated group as one word and strips
the hyphen when matching `at`) until its estimate matches the clip. Words that an `at` regex targets are never glued.
usage: python3 fit_plan_to_audio.py plan.json clips_dir out_plan.json"""
import json, re, subprocess, sys, os, unicodedata
plan, clips, out = sys.argv[1:4]
P = json.load(open(plan))
def norm(w):
    w = unicodedata.normalize('NFD', w.lower()); w = ''.join(c for c in w if not unicodedata.combining(c))
    return re.sub(r'[^a-z0-9_%$]', '', w)   # as the engine's at::norm
def est(ws): return sum(1/2.7 + (0.45 if re.search(r'[.!?:]$', w) else 0.15 if w.endswith(',') else 0) for w in ws)
def speech_len(f):
    o = subprocess.run(['ffmpeg','-v','error','-i',f,'-af',
        'silenceremove=start_periods=1:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_threshold=-45dB,areverse',
        '-ac','1','-ar','16000','-f','s16le','-'], capture_output=True, check=True).stdout
    return len(o) / (2 * 16000)
def ats(o, acc):
    if isinstance(o, dict):
        for k, v in o.items():
            if k == 'at':
                if isinstance(v, str): acc.append(v)
                elif isinstance(v, dict) and 'word' in v: acc.append(v['word'])
            else: ats(v, acc)
    elif isinstance(o, list):
        for v in o: ats(v, acc)
    return acc
k = 0
for s in P['scenes']:
    sp = s.get('speech', '')
    ws = [w for w in re.sub(r'\[[^\]]*\]', ' ', sp).split() if norm(w)]
    if not ws: continue
    d = speech_len(os.path.join(clips, f's{k}.mp3')); k += 1
    regs = [re.compile(r) for r in ats(s, [])]
    prot = [any(r.search(norm(w)) for r in regs) for w in ws]
    # groups: list of word lists; glue the pair whose merge keeps groups most even, until estimate <= d
    groups = [[w] for w in ws]; gprot = prot[:]
    def text(g): return '-'.join(x.rstrip(',') for x in g[:-1]) + ('-' if len(g) > 1 else '') + g[-1] if len(g) > 1 else g[0]
    while est([text(g) for g in groups]) > d + 0.05:
        cands = [i for i in range(len(groups)-1) if not gprot[i] and not gprot[i+1]
                 and not re.search(r'[.!?:]$', groups[i][-1])]
        if not cands: break
        i = min(cands, key=lambda i: (len(groups[i]) + len(groups[i+1]), -i % 3))
        groups[i:i+2] = [groups[i] + groups[i+1]]; gprot[i:i+2] = [False]
    new = ' '.join(text(g) for g in groups)
    print(f'scene {k-1}: audio {d:.2f}s, estimate {est(ws):.2f}s -> {est(new.split()):.2f}s')
    s['speech'] = new
json.dump(P, open(out, 'w'), ensure_ascii=False, indent=1)
