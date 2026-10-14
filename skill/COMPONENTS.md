# Plan format and components (plan@1)

The plan is the only thing the engine reads. It describes **what** each scene shows (components with data)
and **when** each element enters (`at`). The engine owns geometry, layout and animation, so the plan has no
pixel coordinates except optional sizes in the body layout.

Validate against `schema/plan.schema.json`. Unknown keys are rejected so typos show up early.

## Top level

```json
{
  "schema": "llm-video-engine/plan@1",
  "output": "Client - Monthly summary - Sep 2026",
  "config": {"locale": "pt-BR", "formats": ["1080"], "target_duration": 120},
  "theme": {"bg": "#050505", "text": "#FFFFFF", "accent": "#18E27B", "good": "#18E27B", "bad": "#F5564E",
            "warn": "#F5B84E", "panel": "#111113", "blue": "#4E9CF5"},
  "logos": {"brand": "assets/brand.svg", "brand_mark": "assets/mark.svg", "client": "assets/client.svg", "client_color": "#FFFFFF", "client_scale": 0.7},
  "header": {"period": "Client · September 2026 results"},
  "scenes": [ ... ]
}
```

- `formats`: `"1080"` (1920×1080), `"720"` (1280×720, small file). Vertical comes later.
- `theme`: six colors are required (`bg`, `text`, `accent`, `good`, `bad`, `warn`); `panel` and `blue` are derived
  when missing. All other shades (`text2`, `text3`, `line`, `grid`, `accent_dim`, `neutral`) are derived.
- `logos`: paths relative to the plan. SVG or PNG. `brand` is the full lockup (intro, outro); `brand_mark` the small mark for the header (defaults to `brand`). `client_color` recolors every fill of an SVG. `brand_reveal` (0–1): the intro first shows only this left fraction of the brand logo (its symbol), then reveals the rest.
- Tone names used everywhere: `accent`, `good`, `bad`, `warn`, `neutral`, `blue`, `text`.

## Scenes

| `type` | Fields | Notes |
|---|---|---|
| `intro` | `speech` (greeting, optional), `sub`, `duration` (when mute) | Brand logo reveal + glow. No header. |
| `content` | `tab`, `speech`, `kicker`, `title`, `highlight`, `chip`, `body` | Everything else. |
| `slide` | `tab`, `speech`, `image`, `focus[{bbox, callout, at}]` | Camera over a captured tab. |
| `outro` | `text`, `duration` | Brand × client logos. No header. |

`content` fields:
- `kicker`: `["Bold part", "rest"]`, shown uppercase above the title.
- `title` + `highlight`: the title line; `highlight` is appended in the accent color.
- `chip`: one chip right-aligned on the title row: `{text, tone, at}`.
- `body`: the layout box (below). It fills the area under the title: 1224 × 534 logical px.

## Timing: `at`

Every `at` (and `at_*`) says when an element enters, relative to the scene's speech:
- a **string**: regex matched against the words of `speech` (lowercase, no accents, no punctuation).
  Numbers in speech are words ("cinco vírgula seis"), so match a word, e.g. `"faturou"`, `"^ano$"`.
  Hyphens and accents are removed before matching: "ninety-four" is `ninetyfour`, "orçado" is `orcado`.
- an **object**: `{"word": "regex", "n": 2, "offset": 0.5, "default": 4}` for the n-th match, shifted.
- a **number**: seconds after the speech starts.

The element enters 0.25 s before the word by default. When omitted, each component has a sensible default
(usually shortly after the scene starts, staggered). An `at` that matches nothing falls back to `default` and
is reported.

## Body layout

The body is a tree of boxes. A box has `items`; a component has `type`.

```json
{"dir": "column", "gap": 16, "items": [
  {"size": 236, "panel": {"title": "September", "sub": "actual vs budget"}, "items": [{"type": "ruler", ...}]},
  {"panel": {"title": "Year to date"}, "items": [ ... ]}
]}
```

| Key | Meaning |
|---|---|
| `dir` | `column` (default) or `row` |
| `items` | boxes or components |
| `gap` | space between items, default 16 |
| `size` | fixed size along the parent's direction, in logical px |
| `grow` | share of the remaining space (default 1) |
| `panel` | `true` or `{title, sub}`: draws a panel behind the box, with optional title row |
| `at` | the box pops in at this time instead of entering with the scene |
| `float` | on a component: `{anchor: "top-left"|"top-right"|"bottom-left"|"bottom-right", x, y, w}`: placed over the parent box, takes no space |

Text-like components (`number`, `text`, `chips`, `kv`) take their natural height in a column; charts and the
other visuals grow to fill.

## Components

Common keys: `type`, `at`, `size`, `grow`, `float`, `id` (charts other components can attach to).
Number formats (`format`): `k`, `mil`, `mi`, `pct`, `x`, `days`, `num` with `decimals`; `prefix`, `suffix`.

### Text and numbers

| type | fields |
|---|---|
| `number` | `value`, `format`, `decimals`, `prefix`, `suffix`, `font` (font size, default 66), `tone` (`text`), `dur`. Counts up from 0 at `at`. |
| `text` | `text` (inline `<b>bold</b>` allowed), `font` (font size, 14), `tone` (`text2`), `width` (max line width). Pops in. |
| `chips` | `items: [{text, tone, at}]`, `dir`: `row` (default) or `column`. Default tone `good`. |
| `kv` | `title`, `rows: [[label, value], ...]`. A card with label/value rows. |
| `list` | `items: [{title, text, at}]`, `numbered` (true). Full-width rows; the current row is outlined, earlier rows dim. |

### Charts

`y` (optional on charts): `{min, max, step, divide, decimals, suffix}`. Without it the engine picks a round scale.
`divide` scales axis labels (e.g. 1000 with suffix `" mi"` shows 2000 as "2 mi").

| type | fields |
|---|---|
| `bars` | `cats`, `values`, `highlight` (indices, or `"last"`; default last), `projection` (indices drawn hatched), `ref` (`{values, at}` dashed reference line), `labels` (true), `label_divide`, `label_decimals`, `stack` (`{values, tone, label}` second segment on top), `focus` (`{idx, at}` dims the other bars), `split` (`{after, labels: [left, right]}` vertical divider), `axis` (false hides gridlines and the scale), `color` (`accent` or `neutral`), `y`, `id` |
| `lines` | `cats`, `series: [{id, name, values, tone}]`, `actual_until` (index: the shared actual part is drawn first in the text color), `actual_label`, `split` (`{after, labels}`), `zero` (`{label, at}` zero line and shaded negative zone), `at_series`, `y`, `id` |
| `stackbar` | horizontal bar of segments: `items: [[label, value, highlight?]]`, `max`, `target` (`{value, label}`), `end_label`, `note` (text under the highlighted segments), `label_divide`, `label_decimals` |
| `ruler` | rows of value against a target: `rows: [{label, sub, value, target, format, decimals, target_label, tag, at}]`. The bar fills to the target, the excess lights up in accent, the tag appears at the end. |
| `meter` | one horizontal bar against a scale: `label`, `value`, `max`, `marker` (`{value, label}`), `value_label`, `note`, `tone` (`neutral`) |
| `rings` | concentric progress rings: `rings: [{pct, label, center, center_sub, at}]`. The centre text follows the latest ring. |
| `gauge` | semicircle gauge with needle: `min`, `max`, `bands: [[from, to, tone]]`, `ticks`, `tick_suffix`, `from`, `to`, `decimals`, `suffix`, `from_label`, `to_label`, `chips` |
| `flow` | cash flow: `sources: {label, total, parts: [[label, value]]}`, `leftover` (`{value, label}`), `outputs: [[label, value, tone]]`, `tank` (`{label, from, to, max, sub, sub2, format, decimals, divide, at}`), `highlight` (`{text, at}` pill under the first source, which pulses), `at_flow` |
| `calendar` | `band`, `day`, `month`, `time` ("13h30" or "13:30"; clock hands are set from it), `sub` |

### Annotations (attach to a chart by `target` = its `id`)

| type | fields |
|---|---|
| `threshold` | `value`, `label`, `tone` (`blue`), `style`: `water` (line rises from the axis and fills below) or `line`; `recolor: {above, below}` recolors the target's bars when the line arrives |
| `callout` | `idx`, `series` (for lines), `value` (anchor height override, e.g. 0), `text`, `tone`, `shape` (`pill` or `diamond`), `place` (`above` or `left`), `pulse` (the bar flashes), `ping` (a ring pings on the point) |
| `bracket` | `from`, `to` (indices), `text`, `tone`, `shade` (tints the range), `place` (`top` or `bars`: just above the tallest bar in range) |
| `note` | `idx`, `lines: [...]` text lines placed to the right of bar `idx`; the first line is bold in `tone` |

Annotations go in the same box as their chart (they draw over it and take no space).

## Rules for the skill

- One idea per scene, at most five spoken numbers in the whole video.
- Recreate the deck's own visual first, with the same numbers and the same highlight. Use a new visual only when
  the deck's one is static or too dense for 10–15 s, and a `slide` scene when the tab itself is the best visual.
- Every number on screen must exist in the deck. Never invent one.
- Prefer `at` words that are spoken plainly; avoid words whose pronunciation is overridden for TTS.
