---
name: llm-video-engine
description: "Turns an HTML presentation deck into a render plan for a short narrated video summary. Audits the deck tab by tab, builds the story and the narration script, and writes plan.json (scenes made from components) rendered to MP4 right here with the bundled Linux engine (bin/linux-x64), and also packaged as video-plan.zip with its assets and the TTS text for local re-renders. Use when asked for a video summary, a narrated video, or an animated recap of an HTML deck or report."
---

# llm-video-engine: deck → video plan

Input: an HTML presentation (tabs or a long page). Output: **`video-plan.zip`**, holding `plan.json`
(schema `llm-video-engine/plan@1`), the logos and images it references, and `narration.txt` (the TTS text).
The engine in `bin/linux-x64/` draws every frame natively (no browser, no HTML recreation) and encodes with its own
ffmpeg, so the whole job fits in one chat: plan, check, render, deliver the MP4.

**Render only with that engine.** Never screenshot scenes, rebuild them in HTML or call any other ffmpeg.

You decide **what** the video says and shows. You never write drawing code: each scene is a layout of components,
and every chart is described as layers of marks (bars, lines, areas, rects, rules, points, texts, arcs, ribbons,
paths) with the deck's data (see `COMPONENTS.md`). The engine draws and animates them.

The video is a teaser of the deck: it shows the result and what needs attention, it does not explain every driver.
Script rules: a strong number in the first 10 s, at most 5 spoken numbers, numbers written as words in the speech,
close with the next step or the meeting agenda, and a duration promise that matches the video.

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
bin/linux-x64.tar.xz              llm-video-engine (picks the build for this CPU), ffmpeg, ffprobe, fonts/ (self-contained, no install)
```
Find the skill folder first (the `<skill>` below): the folder that holds this SKILL.md and `scripts/`.
If `scripts/` or `COMPONENTS.md` is missing, the skill was installed without its files: tell the user to upload
the full skill zip, and stop.

The analyzer needs Node and `playwright-core` with a Chromium. If `node <skill>/scripts/analyze_deck.mjs` fails on
the import, run `npm install --prefix <skill> playwright-core`. It looks for Chrome, Playwright's Chromium, or any
Chromium in the Playwright cache (set `CHROMIUM_PATH` to point at one). The other two scripts need only Node.

## Flow

### 1. Questions (two AskUserQuestion calls)
First call:
1. Greeting: `"Hi, {name}."` (recommended) · without a name · none.
2. Brand logo: start and end (recommended) · start · end · none. Only when a brand logo is available.
3. Formats: 1080p (recommended) · 1080p + 720p · 720p.
4. Duration: 90 s (recommended) · 60 s · 120 s · free.

Second call (AskUserQuestion takes at most 4 questions): the motion level. Motion is an option, never the default.
Never show the user engine words (measured, whip, twos, shutter, snap); use the plain labels and descriptions:
5. Motion level, 4 options, each with a one-line description a non-technical person understands:
   - **0 · Calm (recommended)**: smooth fades and slides, the classic look, no extra effects.
   - **1 · A little**: titles build word by word, panels land fast, counters tick, scenes exit quickly.
   - **2 · Intermediate**: everything in level 1, plus quick whip-style cuts between scenes. (Camera jumps onto the numbers happen from level 1 on, in scenes that show the deck page itself.)
   - **3 · Boosted**: everything in level 2, plus motion blur on fast moves and spotlights (the key part of a chart stays sharp while the rest blurs and dims for a moment). Most energetic; renders about 2× slower. (The choppy stop-motion look, `clock: "twos"`, is on hold: never offer or set it.)

   If the user answers "Other" or asks what the options look like, describe the four levels in two lines each
   (what moves, how it feels, which suits a board report: 0–1, a consumer-style recap: 2–3) and ask again.
   What each level sets in the plan is in `COMPONENTS.md` ("Motion levels").

Without a user (scheduled run), use the recommended options and say so at the top of the delivery. Motion is then level 0.

### 2. Analyze the deck
```bash
node <skill>/scripts/analyze_deck.mjs deck.html <work>/analysis
```
It finds how the deck navigates, visits every tab, and writes `inventory.md` (per tab: title, KPIs, charts with
labels and series when readable, tables, cards, text), `analysis.json` (the same with bboxes, plus theme colors and
logos), `tab_NN.png` captures and `grid.jpg`.

The extraction is a starting point. Hand-drawn SVG charts may come out incomplete, and overlapping tabs can leak
items from neighbours. **Confirm every number on the tab image** (read the `tab_NN.png`) and, if needed, in the HTML
(grep for the value). Never invent a number.

### 3. Audit the deck
Read `inventory.md` and **every** tab image. Build:

| Tab | Message (1 sentence) | Supporting numbers | The deck's visual: chart type, series, colors, highlight, annotations | Alert / decision |
|---|---|---|---|---|

Describe each visual precisely enough to rebuild it: e.g. "bars per month, last month in accent, dashed average
line with label, August bar hatched as projection". Read the chart's data from `inventory.md` and the HTML (SVG
paths and chart configs hold the series); read colors from the HTML or the image.

Then the **story** in 3–5 lines: what improved, what got worse, what needs a decision, what goes to the meeting.
The scenes and their order come from the story.

### 4. Propose the scenes (gate 1)
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
**marks in the video** and what animates on which word. Ask what to cut or change.

**Vary the scenes.** Not every scene is a title above one panel with one chart. Across the video mix, as the story
allows: a hook scene (one huge number or one full-bleed chart, `bleed: true`), a side-by-side scene, a short beat
(one KPI and one sentence, 4–6 s), a `slide` scene with camera focus, a chart with a callout. Two neighbouring
scenes never share the same layout. The scene that carries the main message is the full-bleed one.

**The camera goes where the narrator is.** On a chart scene, point the camera at the part the speech names, on the word
that names it (`camera: [{at: "word", target: "id", box: [...]}]`): the bars that fall below the minimum, the last point, one slice.
From level 1 on, every chart scene has at least one move; at level 0 there is none. Mix the `kind` across the video:
`snap` for the hard point, `zoom` (smooth) for a slow build, `blur` for a jump to a different region, `spotlight` (level 3 only, at most two per video) for the one moment that carries the story; the same kind
never three scenes in a row. Give the boxes and charts an `id` and aim with `target` (a body of several panels
makes fractions of the whole body hit the wrong one). Moves return by themselves after about 3 s, so you never write
a reset unless you want it earlier; time each move after its chart has entered.
Add a column "camera" (where, on which word, which kind) to the table below.

Fixed structure: `intro` first when the brand logo opens the video (the greeting is its `speech`), `outro` last,
and the agenda or next-steps scene right before `outro`.

### 5. Script (gate 2)
Vary the rhythm: sentence lengths alternate (a short one after a long one), scenes open in different ways (with the
number, with a contrast, with a question, with the consequence), and at most one scene opens with the name of its metric.
The hook scene's first sentence is the most striking fact of the story, not the context. Name the highlight early in the
scene, so the camera has a word to move on.
Write each scene's `speech` within the word budget: 60 / 90 / 120 s ≈ 130 / 195 / 260 words in total, with
4 / 5 / 6–7 content scenes. Speech runs at 2.7 words/s, and every sentence end and every scene add pauses, so the
budget is lower than words × 2.7. Set `config.target_duration` to the chosen duration, the motion fields to the chosen level (table in `COMPONENTS.md`; level 0 sets none). Camera moves
need a `camera` on the scene (section 4, "The camera goes where the narrator is"; a `slide` scene with `focus` also
moves); a chart scene without one has none. Set `bleed: true` (one chart, `"panel": false`) on the scenes where the
chart is the hero, and `header: false` on others that only need the room: the top bar is optional, not a fixture of every scene. At levels 2 and 3 give at
least two content scenes the `slide` type with camera focus (the deck tab itself), or the camera snaps never show. Write the TTS text with numbers in words and a pause between scenes; it goes into
`<work>/narration.txt` (the packager refuses an empty one).

### 6. Write and validate the plan
Write `<work>/plan.json` following `COMPONENTS.md`. Paths are relative to the plan: copy the logos (from the
analysis, or the ones the user sends) next to it, e.g. `<work>/assets/`. Keep text on screen short: labels, not
sentences.

Before packaging, check by hand:
- every number on screen is in the deck, with the same rounding
- titles fit on one line (about 60 characters with the highlight)
- every `kpi`/`table` value in `mil` or `mi` has `divide` (1000 or 1000000) when the raw number is in reais:
  `format` adds only the unit label, it does not scale (see `COMPONENTS.md`); short values, 8 characters or fewer;
  no `prefix: "R$ "` on `k`/`mil`/`mi`, which already print the currency
- chart `y` scales: round steps, labels with the right decimals, every value inside the scale (or leave `y`
  out and let the engine pick)
- each chart has the deck's highlight, colors and annotations

Then validate until it prints `plan ok`:
```bash
node <skill>/scripts/validate_plan.mjs <work>/plan.json
```
It reports schema errors, `at` regexes that match no word of the speech, referenced files that are missing, and
`DURATION` when the estimated video is more than 15% off `target_duration` (it says how many words to cut or
add; the estimate is the engine's own). It prints the estimated length with `plan ok`.

### 7. Check, render, package and deliver
The engine is self-contained. Once per session:
```bash
mkdir -p /tmp/lve && tar -xJf <skill>/bin/linux-x64.tar.xz -C /tmp/lve && chmod +x /tmp/lve/llm-video-engine /tmp/lve/ffmpeg /tmp/lve/ffprobe
E=/tmp/lve/llm-video-engine; $E --version
```
1. **Layout check**, fix until it prints `plan ok` (lines are the codes in section 8):
   ```bash
   $E validate <work>/plan.json
   ```
2. **Grid**: one frame per scene (`<work>/<output>_grid.jpg`). Read it, compare each scene with its deck tab, fix and
   repeat. This is the visual check; it takes seconds.
3. **Package and hand over the plan first**, so the user sees progress before the render ends:
   ```bash
   node <skill>/scripts/package_plan.mjs <work>/plan.json --narration <work>/narration.txt --out <outputs>/video-plan.zip
   ```
   Then send a short message right away: the plan is ready (scene list, estimated length, the TTS text, link to
   `video-plan.zip`) and the video is rendering now (about a minute for 90 s of video at level 0–2, about twice that
   with blur).
4. **Render** (run it in the background and poll if the tool has a timeout):
   ```bash
   $E render <work>/plan.json --out <outputs>/<name>.mp4 --threads $(nproc)      # add --shutter 6 at level 3
   ```
   With `formats: ["1080","720"]` omit `--out`: both files land next to the plan.
Deliver the MP4(s) and any deck visual that was not reproduced exactly (and why). Order of what the user sees:
plan ready, then `video-plan.zip`, then the video.
The MP4 has no audio: the TTS text is for the voice-over.

If `bin/linux-x64.tar.xz` is missing or the binary does not start (`ldd /tmp/lve/llm-video-engine-x86-64` shows what is missing; `LVE_BUILD=x86-64` forces the baseline build if the AVX2 one crashes), do not improvise a
renderer: deliver `video-plan.zip` and tell the user to run `llm-video-engine render video-plan.zip` locally.

### 8. Fixes from the user
The user may paste lines from `llm-video-engine validate`, like `scene 4 · OVERLAP · '94%' overlaps '100'` or
`scene 2 · AT · 'faturou' matches no word of the speech`, or describe what looks wrong in the grid. Fix only the
scenes named, validate again, render again and deliver the new MP4 and zip.

| Code | Meaning | Usual fix |
|---|---|---|
| `AT` | the `at` regex matches no word of that scene's speech | match a word that is spoken; hyphens and accents are removed before matching |
| `OVERLAP` | two texts overlap at the end of the scene | shorter labels, a smaller `font`, or a `size` for the box |
| `TEXT_OUT` | a text runs off the frame | shorten it (titles above all) |
| `AXIS` | a chart value is outside its `y` scale | widen `y` or remove it to let the engine choose |
| `DURATION` | the video is more than 15% off `target_duration` | trim or extend the speech |
