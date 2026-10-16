"""Cut the full narration MP3 into one clip per narrated scene, at the longest silences (the [Pause] between scenes).
usage: python3 split_narration.py plan.json full.mp3 clips_dir   ->  clips_dir/s0.mp3, s1.mp3 ..."""
import json, re, subprocess, sys, os
plan, full, outdir = sys.argv[1:4]
P = json.load(open(plan)); os.makedirs(outdir, exist_ok=True)
words = [len([w for w in re.sub(r'\[[^\]]*\]', ' ', s.get('speech', '')).split() if re.search(r'\w', w)]) for s in P['scenes']]
words = [n for n in words if n]
dur = float(subprocess.check_output(['ffprobe','-v','error','-show_entries','format=duration','-of','csv=p=0',full]).decode())
log = subprocess.run(['ffmpeg','-hide_banner','-i',full,'-af','silencedetect=noise=-40dB:d=0.3','-f','null','-'], capture_output=True, text=True).stderr
st = [float(x) for x in re.findall(r'silence_start: ([\d.]+)', log)]; en = [float(x) for x in re.findall(r'silence_end: ([\d.]+)', log)]
gaps = [((a+b)/2, b-a) for a, b in zip(st, en) if a > 0.5 and b < dur - 0.3]
# the [Pause] between scenes gives the longest silences: take the N-1 longest, in time order
cuts = sorted(g[0] for g in sorted(gaps, key=lambda g: -g[1])[:len(words)-1])
rate = dur / sum(words)
b = [0.0] + cuts + [dur]
for k in range(len(words)):
    subprocess.run(['ffmpeg','-y','-v','error','-i',full,'-ss',f'{b[k]:.3f}','-to',f'{b[k+1]:.3f}','-c:a','libmp3lame','-b:a','192k',os.path.join(outdir,f's{k}.mp3')], check=True)
    r = (b[k+1]-b[k]) / (words[k]*rate)
    print(f'scene {k}: {b[k]:.2f}-{b[k+1]:.2f}s ({words[k]} words)' + ('  CHECK: length off for its words' if not 0.6 < r < 1.6 else ''))
