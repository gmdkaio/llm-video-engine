---
name: llm-video-engine
description: "Turns an HTML presentation deck into a short narrated video summary. Audits the deck tab by tab, builds the story and the narration script, and writes a render plan (plan.json) of scenes made from components; the local llm-video-engine engine renders it to MP4. Use when asked for a video summary, a narrated video, or an animated recap of an HTML deck or report."
---

# llm-video-engine: deck → video plan

Input: an HTML presentation (tabs or a long page). Output: `plan.json` (schema `llm-video-engine/plan@1`) plus the
assets it references, rendered locally by the `llm-video-engine` engine into MP4, with numbers counting, charts drawing
themselves and highlights entering on the word that names them.

You decide **what** the video says and shows. The engine decides **how** it is drawn. You never write drawing
code or coordinates: each scene is a layout of components with data (see `COMPONENTS.md`), and every component
is drawn and animated natively.

The video is a teaser of the deck: it shows the result and what needs attention, it does not explain every driver.
Script rules: a strong number in the first 10 s, at most 5 spoken numbers, numbers written as words in the speech,
close with the next step or the meeting agenda, and a duration promise that matches the video.

**No default scenes.** Every content scene is chosen for this deck: the visual comes from what the deck shows,
not from a menu.

## Files

```
skill/
  SKILL.md, COMPONENTS.md
  scripts/analyze_deck.mjs     deck → analysis.json, inventory.md, tab_NN.png, grid.jpg, logo_N
schema/plan.schema.json        the plan contract (repo root)
engine/build/release/llm-video-engine  the renderer
```
Setup once: `npm install` in `skill/` (playwright-core; uses the installed Chrome) and ffmpeg on PATH.

## Flow

### 1. Questions (one AskUserQuestion call)
1. Greeting: `"Hi, {name}."` (recommended) · without a name · none.
2. Brand logo: start and end (recommended) · start · end · none. Only when a brand logo is configured.
3. Formats: 1080p (recommended) · 1080p + 720p · 720p.
4. Duration: 90 s (recommended) · 60 s · 120 s · free.

Without a user (scheduled run), use the recommended options and say so at the top of the delivery.

### 2. Analyze the deck
```bash
node skill/scripts/analyze_deck.mjs deck.html <work>/analysis
```
It finds how the deck navigates, visits every tab, and writes `inventory.md` (per tab: title, KPIs, charts with
labels and series when readable, tables, cards, text), `analysis.json` (the same with bboxes, plus theme colors and
logos), `tab_NN.png` captures and `grid.jpg`.

The extraction is a starting point. Hand-drawn SVG charts may come out incomplete, and overlapping tabs can leak
items from neighbours. **Confirm every number on the tab image** (Read the `tab_NN.png`) and, if needed, in the HTML
(grep for the value). Never invent a number.

### 3. Audit the deck
Read `inventory.md` and **every** tab image. Build:

| Tab | Message (1 sentence) | Supporting numbers | Visual the deck uses and what it highlights | Alert / decision |
|---|---|---|---|---|

Then the **story** in 3–5 lines: what improved, what got worse, what needs a decision, what goes to the meeting.
The scenes and their order come from the story.

### 4. Propose the scenes (gate 1)
For each scene, the visual is chosen in this order:
1. **The deck's own visual, recreated and animated**: same chart logic, same numbers, same highlight
   (the deck shows cash as a flow → the video shows a `flow`).
2. **A clearer visual** when the deck's one is static or too dense for 10–15 s of video
   (actual vs budget as a `ruler`, leverage as a `gauge`, breakeven as a `threshold` over `bars`).
3. **The tab itself** (`slide` scene) with camera focus, when it is already the best visual (tables, maps).

Present a table: scene · source tab · message · numbers (value and where it came from) · **visual in the deck** ·
**visual in the video** (components, and what animates on which word). Ask what to cut or change.

Fixed structure: `intro` first when the brand logo opens the video (the greeting is its `speech`), `outro` last,
and the agenda or next-steps scene right before `outro`.

### 5. Script (gate 2)
Write each scene's `speech` within the word budget: 60 / 90 / 120 s ≈ 160 / 240 / 320 words (2.7 words/s),
4 / 5 / 6–7 content scenes. Then deliver the TTS text in ready-to-paste blocks (numbers in words, a pause between
scenes).

### 6. Write the plan
Write `<work>/plan.json` following `COMPONENTS.md`. Paths are relative to the plan; copy the logos and tab images
you use next to it. Keep text on screen short: labels, not sentences.

Checklist before rendering:
- every number on screen is in the deck, with the same rounding
- each `at` regex matches a word of that scene's `speech`
- titles fit on one line (about 60 characters with the highlight)
- chart `y` scales: round steps, labels with the right decimals

### 7. Preview, then render
```bash
llm-video-engine grid plan.json          # one frame at the end of each scene -> <plan>_grid.jpg
llm-video-engine frame plan.json --at 12.5 --out f.png
llm-video-engine render plan.json        # -> <output>.mp4 (one file per format)
```
Read the grid. Check: numbers and labels match the deck, nothing overlaps, no text overflows, the highlight is
the deck's highlight. The engine prints a warning for every `at` that matched no word; fix those.

Deliver the MP4 and the TTS text.
