---
name: llm-video-engine
description: "Turns an HTML presentation deck into a short narrated video summary: audits the deck, proposes the scenes, writes the script and a mute preview video rendered right here with the bundled Linux engine (bin/linux-x64), then (after approval) narrates it with ElevenLabs and embeds the voice. Use when asked for a video summary, a narrated video, or an animated recap of an HTML deck or report."
---

# llm-video-engine: deck → narrated video

Input: an HTML presentation (tabs or a long page). Output: an MP4 with the narration embedded (ElevenLabs). Along the
way the user approves three things, in this order:

1. **The scene plan** (gate 1) → 2. **the script + a mute preview video** (gate 2) → 3. **the voice** (gate 3, an
AskUserQuestion), and only then ElevenLabs is called. Never spend voice credits before gate 3.

The engine in `bin/linux-x64/` draws every frame natively (no browser, no HTML recreation) and encodes the video with
its own ffmpeg. **Render frames only with that engine.** Never screenshot scenes, rebuild them in HTML or render video
with another tool. The system ffmpeg (`/usr/bin/ffmpeg`) is used only for the audio steps (cutting the MP3, muxing),
because the bundled ffmpeg has no MP3 decoder and no AAC encoder.

You decide **what** the video says and shows. You never write drawing code: each scene is a layout of components,
and every chart is described as layers of marks (bars, lines, areas, rects, rules, points, texts, arcs, ribbons,
paths) with the deck's data (see `COMPONENTS.md`). The engine draws and animates them.

The video is a teaser of the deck: it shows the result and what needs attention, it does not explain every driver.
Script rules: a strong number in the first 10 s, at most 5 spoken numbers, numbers written as words in the speech,
close with the next step or the meeting agenda, and a duration promise that matches the video. A narrated video ends
up about 15% shorter than the engine's estimate (step 9), so for 90 s promise "a little over a minute".

**Reproduce what the deck has.** There is no menu of chart types: whatever chart the deck draws (waterfall, combo,
donut, gauge, Sankey, stacked bars, scenario lines, timeline…), rebuild it from marks with the same data, colors,
highlight and annotations, and animate it so the highlight lands on the word that names it. Never replace a chart
with a different one because it is simpler, and never reuse visuals from another deck or from an earlier run.
KPI cards become `kpi` components and tables (income statements, DRE, rankings) become `table`, with the deck's
labels, numbers and highlighted rows.

## Files in this skill

```
SKILL.md, COMPONENTS.md          this flow and the plan format
schema/plan.schema.json          the plan contract
scripts/analyze_deck.mjs         deck → analysis.json, inventory.md, tab_NN.png, grid.jpg, logo_N
scripts/validate_plan.mjs        schema + `at` words + referenced files (no dependencies)
scripts/package_plan.mjs         validates, then writes video-plan.zip
scripts/split_narration.py       full narration MP3 → one clip per narrated scene (cuts at the [Pause] silences)
scripts/fit_plan_to_audio.py     retimes the plan so each scene lasts as long as its clip
scripts/mux_audio.py             places the clips on the engine's timeline and muxes them into the MP4
bin/linux-x64.tar.xz              llm-video-engine (picks the build for this CPU), ffmpeg, ffprobe, fonts/ (self-contained, no install)
```
Find the skill folder first (the `<skill>` below): the folder that holds this SKILL.md and `scripts/`.
If `scripts/` or `COMPONENTS.md` is missing, the skill was installed without its files: tell the user to upload
the full skill zip, and stop. If the skill folder is read-only, copy it to the scratchpad first (`cp -rL`).

The analyzer needs Node and `playwright-core` with a Chromium. If `node <skill>/scripts/analyze_deck.mjs` fails on
the import, run `npm install --prefix <skill> playwright-core`. It looks for Chrome, Playwright's Chromium, or any
Chromium in the Playwright cache (set `CHROMIUM_PATH` to point at one). The other two Node scripts need only Node;
the Python scripts need Python 3 and the system ffmpeg/ffprobe.

## Flow

### 1. Questions (two AskUserQuestion calls)
First call:
1. Greeting: `"Hi, {name}."` (recommended) · without a name · none.
2. Brand logo: start and end (recommended) · start · end · none. Only when a brand logo is available.
3. Formats: 1080p (recommended) · 1080p + 720p · 720p.
4. Duration: 90 s (recommended) · 60 s · 120 s · free.

Second call (AskUserQuestion takes at most 4 questions), the motion style. Motion is an option, never the default:
5. Motion: classic (recommended; `motion` left out of `config`) · measured (`"motion": "measured"`: word-by-word titles,
   fast landings, stepped counters, accelerating exits, the camera snapping onto the numbers) · measured + whip
   transitions (`"transition": "whip"`, energetic, consumer-style) · measured + twos (`"clock": "twos"`,
   hand-animated look).
   Describe each in one line; for a sober board report suggest classic or measured, not whip. Details in `COMPONENTS.md`.

The voice is asked later, at gate 3 (step 8), never here.

Without a user (scheduled run), use the recommended options (motion classic), stop after step 7 (mute preview +
script) and say so at the top of the delivery: voice credits are only spent after a person approves.

### 2. Analyze the deck
```bash
node <skill>/scripts/analyze_deck.mjs deck.html <work>/analysis
```
It finds how the deck navigates, visits every tab, and writes `inventory.md` (per tab: title, KPIs, charts with
labels and series when readable, tables, cards, text), `analysis.json` (the same with bboxes, plus theme colors and
logos), `tab_NN.png` captures and `grid.jpg`.

The extraction is a starting point. Hand-drawn SVG charts may come out incomplete, and overlapping tabs can leak
items from neighbours. **Confirm every number on the tab image** (read the `tab_NN.png`) and, if needed, in the HTML
(grep for the value; SVG tooltips often hold the exact series). Never invent a number.

### 3. Audit the deck
Read `inventory.md` and **every** tab image. Build:

| Tab | Message (1 sentence) | Supporting numbers | The deck's visual: chart type, series, colors, highlight, annotations | Alert / decision |
|---|---|---|---|---|

Describe each visual precisely enough to rebuild it: e.g. "bars per month, last month in accent, dashed average
line with label, August bar hatched as projection". Read the chart's data from `inventory.md` and the HTML (SVG
paths and chart configs hold the series); read colors from the HTML or the image.

Then the **story** in 3–5 lines: what improved, what got worse, what needs a decision, what goes to the meeting.
The scenes and their order come from the story.

### 4. Propose the scenes (gate 1: the plan)
For each scene, the visual is:
1. **The deck's own visual, rebuilt from marks and animated**: same chart, same numbers, same highlight. Decide
   which marks draw it (e.g. a waterfall is `bar` + `transform: "waterfall"`; a gauge is polar `arc` bands +
   a polar `rule` needle + a counting `text`; a Sankey is `rect` nodes + `ribbon` links + `text` names)
   and what animates on which word.
2. **The tab itself** (`slide` scene) with camera focus, when it has nothing worth rebuilding (maps, images,
   dense text, very large tables). A `slide` is never a substitute for a chart or table that can be rebuilt.
3. You may simplify a chart that is too dense for 10–15 s (fewer categories, one series instead of five), but keep
   its type and its highlight, and say so in the table.

Present a table: scene · source tab · message · numbers (value and where it came from) · **visual in the deck** ·
**marks in the video** and what animates on which word. Ask what to cut or change. Wait for approval.

With measured motion, the camera snaps happen on `slide` scenes with `focus` (each focus point snaps onto its bbox on
its word, then settles). When the user picked a measured option, include at least one `slide` scene whose tab has
something worth pointing at; without one, no snap is ever seen.

Fixed structure: `intro` first when the brand logo opens the video (the greeting is its `speech`), `outro` last,
and the agenda or next-steps scene right before `outro`.

### 5. Script: speech + ElevenLabs text
Write each scene's `speech` within the word budget: 60 / 90 / 120 s ≈ 130 / 195 / 260 words in total, with
4 / 5 / 6–7 content scenes. Speech runs at 2.7 words/s, and every sentence end and every scene add pauses, so the
budget is lower than words × 2.7. Set `config.target_duration` to the chosen duration, and `config.motion` /
`transition` / `clock` to the motion answer (classic: set none of them).

Then write the **ElevenLabs text** in `<work>/narration.txt`: ONE block for the whole video, the same words as the
`speech` fields scene by scene, with v4 tags in natural language and `[Pause]` on its own line between scenes (step 9
cuts the audio at those pauses). Tags:
- `[Composed, professional narration, warm but measured]`: greeting, context, explanations, the agenda.
- `[Clear, confident delivery]`: results and good news.
- `[More serious, careful]`: points of attention (costs up, negative cash, budget cut).
- A tag holds until the next one: change it only where the delivery changes. No capitals for emphasis, no ellipses.
- Numbers in words, the client's own terms ("orçado", "budget").

Example:
```
[Composed, professional narration, warm but measured] Hi, Ana. This is the August summary, in a little over a minute.
[Pause]
[Clear, confident delivery] August went back to normal after the July peak. Gross revenue fell to four hundred and eighty thousand, below budget, and net income closed at sixty-one thousand.
[Pause]
[Composed, professional narration, warm but measured] What was missing was volume. [More serious, careful] Expenses rose to thirty-seven percent of revenue, well above the recent average.
```

### 6. Write and validate the plan
Write `<work>/plan.json` following `COMPONENTS.md`. Paths are relative to the plan: copy the logos (from the
analysis, or the ones the user sends) next to it, e.g. `<work>/assets/`. `tab` is the label shown in the header nav
("August summary", "Cash"), never `tab_00`. Keep text on screen short: labels, not sentences.

Check by hand:
- every number on screen is in the deck, with the same rounding
- titles fit on one line (about 60 characters with the highlight); the outro `text` stays short ("Performance · Aug/26")
- every `kpi`/`table` value in `mil` or `mi` has `divide` (1000 or 1000000) when the raw number is in reais:
  `format` adds only the unit label, it does not scale (see `COMPONENTS.md`); short values, 8 characters or fewer;
  no `prefix: "R$ "` on `k`/`mil`/`mi`, which already print the currency
- chart `y` scales: round steps, labels with the right decimals, every value inside the scale (or leave `y`
  out and let the engine pick); negative bars get the deck's label ("(11)") and a `y.min` below zero so the label
  clears the axis labels
- each chart has the deck's highlight, colors and annotations

Then validate until it prints `plan ok`:
```bash
node <skill>/scripts/validate_plan.mjs <work>/plan.json
```
It reports schema errors, `at` regexes that match no word of the speech, referenced files that are missing, and
`DURATION` when the estimated video is more than 15% off `target_duration` (it says how many words to cut or
add; the estimate is the engine's own). It prints the estimated length with `plan ok`.

### 7. Mute preview (gate 2: script and video)
The engine is self-contained. Once per session:
```bash
mkdir -p /tmp/lve && tar -xJf <skill>/bin/linux-x64.tar.xz -C /tmp/lve && chmod +x /tmp/lve/llm-video-engine /tmp/lve/ffmpeg /tmp/lve/ffprobe
E=/tmp/lve/llm-video-engine; $E --version
```
1. `$E validate <work>/plan.json` until `plan ok` (codes in section 11).
2. `$E grid <work>/plan.json` (run from `<work>`): one frame per scene in `<output>_grid.jpg`. Read it, compare each
   scene with its deck tab, fix and repeat. `$E frame plan.json --at 14 --out f.jpg` shows a single instant (use it
   on a focus word's time to see where a snap lands).
3. `$E render <work>/plan.json --out "<outputs>/<name> (mute).mp4" --threads $(nproc)` (in the background
   if the tool has a timeout).

Deliver the mute MP4 and paste the full `narration.txt` block in the message. Ask the user to approve the video
and the text, or say what to change. Fix and re-render until both are approved.

### 8. Voice (gate 3, one AskUserQuestion call)
Only after gate 2 is approved, and only when the ElevenLabs connector is present (`creative_generate_speech` via
ToolSearch; without it, deliver the mute MP4 + `narration.txt` and stop):
1. **Voice** · a voice from `creative_list_voices` that fits the deck's language and tone (Recommended) · Other (the user types the name)
2. **Generation** · Generate through the connector now (Recommended) · I'll generate on the ElevenLabs site and send the MP3
3. **Versions** · 1 version (Recommended) · 2 versions to compare

Say in the question text the approximate cost (≈ 1,400 credits per version of a 90 s script) and that the
connector applies only voice and language: stability and similarity are rejected by it; for those, the
user generates on the site (option 2) with the same text and sends the MP3. Find the voice id with
`creative_list_voices`; never type an id from memory.

### 9. Narration and final video
The engine paces each scene at 2.7 words/s plus punctuation pauses (0.35 s lead-in, 1 s tail); real voices are about
20% faster. Never slow the voice down to fit the video (it sounds dragged): retime the plan to the real audio.

1. Generate (option 1): `creative_create_flow`, then `creative_add_flow_node` (`tts`, `eleven_v4`, the chosen
   `voice_id`, `model_parameters: {"language_code": "<lang>"}` with a bare language code such as `pt` or `en` (the v4
   rejects `pt-BR`), prompt = the whole `narration.txt`) and `creative_run_flow_nodes` with the chosen
   `generations_count`. Poll `creative_get_flow_run_status` until completed; download each `content_url` with curl
   to `<work>/audio/full.mp3`. With option 2, use the MP3 the user sends.
2. Split by scene at the `[Pause]` silences, retime, render, mux (from `<work>`):
   ```bash
   python3 <skill>/scripts/split_narration.py plan.json audio/full.mp3 audio      # s0.mp3, s1.mp3 ... (intro included)
   python3 <skill>/scripts/fit_plan_to_audio.py plan.json audio plan_audio.json  # glues words so each scene lasts as long as its clip
   $E validate plan_audio.json
   $E render plan_audio.json --out mute.mp4 --threads $(nproc)
   python3 <skill>/scripts/mux_audio.py plan_audio.json mute.mp4 audio "<outputs>/<name>.mp4"
   ```
   `split_narration.py` prints each scene's span; a `CHECK` line means a cut probably fell inside a scene: confirm with
   `creative_transcribe_audio` (`connect_from` the TTS node, free) or listen, and cut by hand at the right silence.
   The mux log prints each clip's tempo: it must stay within 0.97–1.03. The retime never glues a word that an `at`
   points at, so focus snaps and highlights still land on their words. With `transition`/`clock` set, check in a
   frame that scene starts still match the log's times. With 1080p + 720p, render and mux each format.
3. Check with `ffprobe` that the MP4 has an `aac` stream, and with `-af volumedetect` that the mean is near −16 dB.

If `bin/linux-x64.tar.xz` is missing or the binary does not start (`ldd /tmp/lve/llm-video-engine-x86-64` shows what is missing; `LVE_BUILD=x86-64` forces the baseline build if the AVX2 one crashes), do not improvise a
renderer: deliver `video-plan.zip` and tell the user to run `llm-video-engine render video-plan.zip` locally.

### 10. Deliver
Deliver the narrated MP4, plus the raw MP3 so the user can judge the voice alone. Only when the
user asks: `video-plan.zip` (`node <skill>/scripts/package_plan.mjs <work>/plan.json --narration <work>/narration.txt
--out <outputs>/video-plan.zip`). Report any deck visual that was not reproduced exactly (and why), the real duration
against the opening's promise, the ElevenLabs credits spent, and ask the user to listen to the client's name.

### 11. Fixes from the user
The user may paste lines from `llm-video-engine validate`, like `scene 4 · OVERLAP · '94%' overlaps '100'` or
`scene 2 · AT · 'faturou' matches no word of the speech`, or describe what looks wrong in the grid. Fix only the
scenes named, validate again, render again and deliver the new MP4. A visual fix after the narration reuses the audio
(re-run `fit_plan_to_audio.py`, render, mux); a change to the spoken words needs a new narration and a new gate 3.

| Code | Meaning | Usual fix |
|---|---|---|
| `AT` | the `at` regex matches no word of that scene's speech | match a word that is spoken; hyphens and accents are removed before matching |
| `OVERLAP` | two texts overlap at the end of the scene | shorter labels, a smaller `font`, or a `size` for the box |
| `TEXT_OUT` | a text runs off the frame | shorten it (titles above all) |
| `AXIS` | a chart value is outside its `y` scale | widen `y` or remove it to let the engine choose |
| `DURATION` | the video is more than 15% off `target_duration` | trim or extend the speech |
