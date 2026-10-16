"""Fit one narration clip per narrated scene into the engine's timeline and mux it into the MP4.
usage: python3 mux_audio.py plan.json video.mp4 clips_dir out.mp4
clips_dir holds s0.mp3, s1.mp3 ... in the order of the narrated scenes."""
import json, re, subprocess, sys, os, unicodedata
plan, video, clips, out = sys.argv[1:5]
P = json.load(open(plan))
def norm(w):
    w = unicodedata.normalize('NFD', w.lower()); w = ''.join(c for c in w if not unicodedata.combining(c))
    return re.sub(r'[^a-z0-9_%$]', '', w)   # as the engine's at::norm
def probe(f):
    return float(subprocess.check_output(['ffprobe','-v','error','-show_entries','format=duration','-of','csv=p=0',f]).decode())
LEAD = 0.35   # the engine starts the speech this long into the scene (timeline::kLead)
t = 0.0; parts = []; k = 0; tmp = os.path.join(clips, 'fit'); os.makedirs(tmp, exist_ok=True)
for s in P['scenes']:
    ws = [w for w in re.sub(r'\[[^\]]*\]', ' ', s.get('speech','')).split() if norm(w)]
    if ws:
        est = sum(1/2.7 + (0.45 if re.search(r'[.!?:]$', w) else 0.15 if w.endswith(',') else 0) for w in ws)
        src = os.path.join(clips, f's{k}.mp3'); trimmed = os.path.join(tmp, f't{k}.wav')
        # trim leading/trailing silence
        subprocess.run(['ffmpeg','-y','-v','error','-i',src,'-af',
            'silenceremove=start_periods=1:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_threshold=-45dB,areverse',
            '-ar','44100','-ac','1',trimmed], check=True)
        d = probe(trimmed); r = max(0.97, min(1.03, d/est))
        fit = os.path.join(tmp, f'f{k}.wav')
        subprocess.run(['ffmpeg','-y','-v','error','-i',trimmed,'-af',f'atempo={r:.4f}',fit], check=True)
        print(f'scene {k}: clip {d:.2f}s, slot {est:.2f}s, tempo x{r:.3f} -> {probe(fit):.2f}s at {t+LEAD:.2f}s')
        parts.append((fit, t + LEAD)); k += 1
        dur = est + 0.3 + 1.0
    else:
        dur = s.get('duration', 4 if s.get('type')=='outro' else 2.6 if s.get('type')=='intro' else 5)
    t = round((t + dur) * 100) / 100   # scene boundaries, rounded as the engine does
inputs = ['-i', video]; filt = []
for i,(f,start) in enumerate(parts):
    inputs += ['-i', f]; filt.append(f'[{i+1}:a]adelay={int(start*1000)}|{int(start*1000)},apad[a{i}]')
filt.append(''.join(f'[a{i}]' for i in range(len(parts))) + f'amix=inputs={len(parts)}:normalize=0,loudnorm=I=-16:TP=-1.5:LRA=11[aout]')
subprocess.run(['ffmpeg','-y','-v','error',*inputs,'-filter_complex',';'.join(filt),'-map','0:v','-map','[aout]',
    '-c:v','copy','-c:a','aac','-b:a','192k','-ar','48000','-shortest','-movflags','+faststart',out], check=True)
print('->', out, f'{probe(out):.2f}s (timeline {t:.2f}s)')
