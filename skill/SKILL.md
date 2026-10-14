---
name: llm-video-engine
description: "Turns an HTML presentation deck into a render plan for a short narrated video summary. Audits the deck tab by tab, builds the story and the narration script, and writes plan.json (scenes made from components) packaged as video-plan.zip with its assets and the TTS text. The user renders it locally with the llm-video-engine engine. Use when asked for a video summary, a narrated video, or an animated recap of an HTML deck or report."
---

# llm-video-engine: deck → video plan

Input: an HTML presentation (tabs or a long page). Output: **`video-plan.zip`**, holding `plan.json`
(schema `llm-video-engine/plan@1`), the logos and images it references, and `narration.txt` (the TTS text).
The user renders the zip on their own machine with the local engine, which draws every frame natively.

**You stop at the zip. Never render, preview or encode video here**: no screenshots of scenes, no HTML
recreations, no ffmpeg. That work belongs to the local engine and costs time and tokens here.

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

## Files in this skill

```
SKILL.md, COMPONENTS.md          this flow and the plan format
schema/plan.schema.json          the plan contract
scripts/analyze_deck.mjs         deck → analysis.json, inventory.md, tab_NN.png, grid.jpg, logo_N
scripts/validate_plan.mjs        schema + `at` words + referenced files (no dependencies)
scripts/package_plan.mjs         validates, then writes video-plan.zip
```
Find the skill folder first (the `<skill>` below): the folder that holds this SKILL.md and `scripts/`.
If `scripts/` or `COMPONENTS.md` is missing, the skill was installed without its files: tell the user to upload
the full skill zip, and stop.

The analyzer needs Node and `playwright-core` with a Chromium. If `node <skill>/scripts/analyze_deck.mjs` fails on
the import, run `npm install --prefix <skill> playwright-core`. It looks for Chrome, Playwright's Chromium, or any
Chromium in the Playwright cache (set `CHROMIUM_PATH` to point at one). The other two scripts need only Node.

## Flow

### 1. Questions (one AskUserQuestion call)
1. Greeting: `"Hi, {name}."` (recommended) · without a name · none.
2. Brand logo: start and end (recommended) · start · end · none. Only when a brand logo is available.
3. Formats: 1080p (recommended) · 1080p + 720p · 720p.
4. Duration: 90 s (recommended) · 60 s · 120 s · free.

Without a user (scheduled run), use the recommended options and say so at the top of the delivery.

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
2. **The tab itself** (`slide` scene) with camera focus, when it has no chart worth rebuilding (tables, maps,
   dense text). A `slide` is never a substitute for a chart that can be rebuilt.
3. You may simplify a chart that is too dense for 10–15 s (fewer categories, one series instead of five), but keep
   its type and its highlight, and say so in the table.

Present a table: scene · source tab · message · numbers (value and where it came from) · **visual in the deck** ·
**marks in the video** and what animates on which word. Ask what to cut or change.

Fixed structure: `intro` first when the brand logo opens the video (the greeting is its `speech`), `outro` last,
and the agenda or next-steps scene right before `outro`.

### 5. Script (gate 2)
Write each scene's `speech` within the word budget: 60 / 90 / 120 s ≈ 130 / 195 / 260 words in total, with
4 / 5 / 6–7 content scenes. Speech runs at 2.7 words/s, and every sentence end and every scene add pauses, so the
budget is lower than words × 2.7. Set `config.target_duration` to the chosen duration. Write the TTS text with numbers in words and a pause between scenes; it goes into
`<work>/narration.txt` (the packager refuses an empty one).

### 6. Write and validate the plan
Write `<work>/plan.json` following `COMPONENTS.md`. Paths are relative to the plan: copy the logos (from the
analysis, or the ones the user sends) next to it, e.g. `<work>/assets/`. Keep text on screen short: labels, not
sentences.

Before packaging, check by hand:
- every number on screen is in the deck, with the same rounding
- titles fit on one line (about 60 characters with the highlight)
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

### 7. Package and deliver
```bash
node <skill>/scripts/package_plan.mjs <work>/plan.json --narration <work>/narration.txt --out <outputs>/video-plan.zip
```
Deliver `video-plan.zip` and, in the message, the TTS text, any deck visual that was not reproduced exactly
(and why), and these local commands:
```
llm-video-engine validate video-plan.zip   # layout check, one line per problem
llm-video-engine grid video-plan.zip       # one frame per scene, to look at before rendering
llm-video-engine render video-plan.zip     # the MP4, next to the zip
```

### 8. Fixes from the user
The user may paste lines from `llm-video-engine validate`, like `scene 4 · OVERLAP · '94%' overlaps '100'` or
`scene 2 · AT · 'faturou' matches no word of the speech`, or describe what looks wrong in the grid. Fix only the
scenes named, validate again, and deliver a new zip.

| Code | Meaning | Usual fix |
|---|---|---|
| `AT` | the `at` regex matches no word of that scene's speech | match a word that is spoken; hyphens and accents are removed before matching |
| `OVERLAP` | two texts overlap at the end of the scene | shorter labels, a smaller `font`, or a `size` for the box |
| `TEXT_OUT` | a text runs off the frame | shorten it (titles above all) |
| `AXIS` | a chart value is outside its `y` scale | widen `y` or remove it to let the engine choose |
| `DURATION` | the video is more than 15% off `target_duration` | trim or extend the speech |
