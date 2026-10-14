# Plan format (plan@1)

The plan is the only thing the engine reads. It says **what** each scene shows and **when** each element enters
(`at`). Charts are not picked from a menu: each one is built from **marks** (bars, lines, areas, rects, rules,
points, texts, arcs, ribbons, paths) over a scale, so any chart the deck has can be rebuilt with its own data.

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
  when missing. Take them from the deck.
- `logos`: paths relative to the plan. SVG or PNG. `brand` is the full lockup (intro, outro); `brand_mark` the
  small mark for the header (defaults to `brand`). `client_color` recolors every fill of an SVG. `brand_reveal`
  (0–1): the intro first shows only this left fraction of the brand logo (its symbol), then reveals the rest.
- **Tones**, used everywhere a color goes: `accent`, `accent_dim`, `good`, `bad`, `warn`, `blue`, `neutral`,
  `neutral2`, `text`, `text2`, `text3`, `line`, `grid`, `panel`, `bg`, or any `#rrggbb` from the deck
  (e.g. the third shade of a stacked chart).

## Scenes

| `type` | Fields | Notes |
|---|---|---|
| `intro` | `speech` (greeting, optional), `sub`, `duration` (when mute) | Brand logo reveal + glow. No header. |
| `content` | `tab`, `speech`, `kicker`, `title`, `highlight`, `chip`, `body` | Title + a body of components. |
| `slide` | `tab`, `speech`, `image`, `focus`, `zoom_max`, `back` | Camera over a captured tab. |
| `outro` | `text`, `duration` | Brand × client logos. No header. |

`content`:
- `kicker`: `["Bold part", "rest"]`, shown uppercase above the title.
- `title` + `highlight`: the title line; `highlight` is appended in the accent color.
- `chip`: one chip right-aligned on the title row: `{text, tone, at}`.
- `body`: the layout box (below). It fills the area under the title: 1224 × 534 logical px.

`slide`: `image` is a `tab_NN.png` copied next to the plan. `focus: [{bbox: [x, y, w, h], callout, at}]`, with the
bbox in the image's pixels at 1280 px wide (the analyzer's capture size, same as `analysis.json`). The camera zooms
on each bbox in turn (up to `zoom_max`, default 1.7), dims the rest and shows the callout. `back: true` returns to
the full tab at the end.

## Timing: `at`

Every `at` says when an element enters, relative to the scene's speech:
- a **string**: regex matched against the words of `speech` (lowercase, no accents, no punctuation).
  Numbers in speech are words ("cinco vírgula seis"), so match a word, e.g. `"faturou"`, `"^ano$"`.
  Hyphens and accents are removed before matching: "ninety-four" is `ninetyfour`, "orçado" is `orcado`.
- an **object**: `{"word": "regex", "n": 2, "offset": 0.5, "default": 4}` for the n-th match, shifted.
- a **number**: seconds after the speech starts.

An `at` that matches nothing falls back to `default` and is reported.

## Body layout

The body is a tree of boxes. A box has `items`; a component has `type`.

```json
{"dir": "row", "gap": 16, "items": [
  {"grow": 2, "panel": {"title": "Monthly revenue", "sub": "R$ thousand"}, "items": [{"type": "chart", ...}]},
  {"size": 360, "panel": {"title": "Share"}, "items": [{"type": "chart", "coord": "polar", ...}]}
]}
```

| Key | Meaning |
|---|---|
| `dir` | `column` (default) or `row` |
| `items` | boxes or components |
| `gap` | space between items, default 16 |
| `size` | fixed size along the parent's direction, in logical px |
| `grow` | share of the remaining space (default 1) |
| `panel` | `true` or `{title, sub}`: draws a panel behind the box, with an optional title row |
| `at` | the box pops in at this time instead of entering with the scene |
| `float` | on a component: `{anchor: "top-left"|"top-right"|"bottom-left"|"bottom-right", x, y, w}`: placed over the parent box, takes no space |

Use the deck's own arrangement: if the tab has a chart with a side panel of KPIs, the body is a row with a chart
box and a column box.

## Page components

Common keys: `type`, `id`, `at`, `size`, `grow`, `float`.
Number formats (`format`): `k`, `mil`, `mi`, `pct`, `x`, `days`, `num`, with `decimals`, `divide`, `prefix`,
`suffix`, `sign` (shows `+`).

| type | fields |
|---|---|
| `number` | `value`, `format`, `decimals`, `prefix`, `suffix`, `font` (font size, default 66), `tone` (`text`), `dur`. Counts up from 0 at `at`. |
| `text` | `text` (inline `<b>bold</b>` allowed), `font` (font size, 14), `tone` (`text2`), `width` (max line width). |
| `chips` | `items: [{text, tone, at}]`, `dir`: `row` (default) or `column`. Default tone `good`. |
| `kv` | `title`, `rows: [[label, value], ...]`. A card with label/value rows. |
| `list` | `items: [{title, text, at}]`, `numbered` (true). Full-width rows; the current row is outlined, earlier rows dim. |
| `chart` | layers of marks, below. Grows to fill its box. |

## `chart`

```json
{"type": "chart",
 "x": {"domain": ["Jan", "Feb", "Mar"]},
 "y": {"min": 0, "max": 600, "step": 200, "suffix": " k"},
 "layers": [ {"mark": "bar", "data": [{"x": "Jan", "y": 420}, ...], "animate": {"at": "revenue"}} ]}
```

### Scales and coordinates

| Key | Meaning |
|---|---|
| `coord` | `cartesian` (default) or `polar` |
| `orient` | `vertical` (default) or `horizontal` (bars grow to the right, categories down the left) |
| `x` | `{type: "band"}` (default; categories from `domain` or from the data in order) or `{type: "linear", min, max, step}`. `labels: false` hides the labels, `emphasis: ["Aug"]` makes those category labels bold. `size` sets the label font size. |
| `y` | value scale: `min`, `max`, `step`; without them the engine picks a round scale over every value drawn. `zero: false` lets it start away from zero. `labels: false`, `grid: false`. Label format: `format`, `divide`, `decimals`, `prefix`, `suffix`. |
| `y2` | a second value scale on the right, for layers with `axis: "y2"` (revenue bars + margin % line). Same keys as `y`, plus `tone` for its labels. Vertical charts only. |
| `polar` | `{start, end}` in degrees clockwise from 12 o'clock (`-90..90` is a top semicircle gauge, `0..360` a full ring), `min`, `max` of the value around the circle (default: the sum of the arc values), `ticks` (values labelled around the edge), `radius`, `cy`, `dx`, and a label format. For points, lines, areas and texts in polar charts, `x` is the angle (a category spread evenly around, or a value on `min..max`) and `y` the distance from the center on `r: {min, max, step}` (default 0 to the largest `y`). `domain` lists the angle categories in order (radar axes); with categories, or with `grid: true`, the engine draws rings, spokes and category labels |
| `margin` | `{left, right, top, bottom}` around the plot, to make room for end labels or annotations |
| `legend` | `[{label, tone, mark: "bar"|"line"|"dash"|"point", pattern: "hatch"}]`, drawn above the plot |
| `layers` | drawn in order, later layers on top |

`x` values in data are categories on a band scale (the name, or its 0-based index) and numbers on a linear one.
On a band scale, `x0`/`x1` on rects and rules span from the left edge of the first category to the right edge of
the second (shade "Jul" to "Sep" with `{"x0": "Jul", "x1": "Sep"}`). For free placement (flows, diagrams) use a
linear `x`, e.g. `{"type": "linear", "min": 0, "max": 10, "labels": false}`.

### Layers

Every layer: `mark`, `data` (array of datums), and optionally `tone`, `opacity`, `transform`, `labels`,
`animate`, `changes`, `axis: "y2"`. A datum's own `tone`, `opacity`, `at` override the layer's.

| mark | datum fields | layer options |
|---|---|---|
| `bar` | `x`, `y` (or `y0`/`y1` for a floating bar), `tone`, `pattern: "hatch"` (projections, forecasts), `label` (text replacing the value label), `label_tone` | `size` (bar width as a share of the slot, 0.6), `radius`, `dodge: [i, n]` (this layer is group i of n side-by-side series), `pattern` |
| `line` | `x`, `y` (a datum without `y` breaks the line) | `width`, `dash: [6, 4]`, `markers`, `marker_size`, `closed` (joins the last point to the first; default on in a full-circle polar chart) |
| `area` | `x`, `y`, optional `y0` (lower edge, default the baseline). In polar charts it is the filled shape of the points (radar) | `opacity` (0.22) |
| `rect` | `x0`, `x1`, `y0`, `y1` (missing ones span the whole plot): bands, ranges, shaded periods, Sankey nodes, tanks | `opacity` (0.12), `radius`, `stroke`, `width` |
| `rule` | `y` (horizontal reference line: target, average, minimum; `x0`/`x1` limit it) or `x` (vertical divider: today, actual vs projection), `label`, `label_pos: "start"|"end"`, `tone` | `width`, `dash`, `size` (label font). In polar charts a rule is a **needle**: `value`, `from` (it swings from → value); `length`, `width` |
| `point` | `x`, `y`, `dx`, `dy`, `size`, `shape: "circle"|"diamond"`, `style: "fill"|"ring"`, `ping` (rings out twice: "look here") | same as datum |
| `text` | position by `x`/`y` (+ `dx`, `dy` in px), or `pos: "top-left"|"top"|"top-right"|"left"|"center"|"right"|"bottom-left"|...` in the plot, or in polar charts the center (default) or `theta` + `r` (radius share, 1.14). `text`, or `value` + `from` + `format`… to **count** (also `dur`). `anchor`, `size`, `weight`, `tone`, `box` (a pill behind the text, in that tone) | same as datum, plus `letter_spacing`, `from_dx`/`from_dy` for `slide` |
| `arc` | polar only. `value` (arcs follow each other from `polar.min`) or `theta0`/`theta1` (fixed span), `tone`, `label` | `inner`, `outer` (radius shares, 0.62 and 1), `gap` (degrees between arcs), `track` (tone of the full background ring), `label_pos: "inside"|"outside"`, `label_tone` |
| `ribbon` | `x0`, `y0: [top, bottom]`, `x1`, `y1: [top, bottom]`: a band curving from one span to another (Sankey links, flows) | `opacity` (0.35) |
| `path` | `points: [[x, y], [x, y, dy], [x, y, dy, dx]...]` (the optional px offsets nudge a point), `closed` (fills it), `label` (above the path) | `width`, `opacity`. Brackets, arrows, connectors, custom shapes |

### Transforms

| `transform` | effect |
|---|---|
| `stack` | bars of every layer with `stack` pile up per category, in layer order (stacked bars, one layer per series) |
| `waterfall` | each `y` is a change from the previous running total; a datum with `total: true` is a full bar from zero (its `y`, or the running total if it has none). Labels show the change, or the total on total bars |
| `cumulative` | `y` becomes the running sum (a cumulative line over daily bars) |

### Labels

`labels: true` puts the value on each bar or line point. As an object: `only` (`"last"`, `"first"`, or indices),
`position` (bars: `end`, `inside`, `center`; lines: `top`, `bottom`, `right`), `size`, `weight`, `tone`, and the
number format keys. A datum's `label` replaces its value text.

### Animation

`animate: {enter, at, stagger, dur}` on a layer. `at` is when the first datum enters (default: the chart's `at`,
or shortly after the scene starts), `stagger` the delay between datums, `dur` the length of the entrance.
A datum with its own `at` enters then instead.

| `enter` | what it does | default for |
|---|---|---|
| `grow` | bars and rects grow from their base, arcs sweep, areas and ribbons reveal left to right | bar, area, arc, ribbon, polar rule |
| `draw` | lines and paths draw along their length (dashed lines reveal left to right), rules draw across | line, rule, path |
| `fade` | fades in | rect, text |
| `pop` | scales up with a little overshoot | point |
| `rise` | a horizontal rule rises from the baseline to its value ("the minimum comes up") | |
| `slide` | text slides up into place | |
| `count` | texts with `value` always count; use any enter | |
| `none` | already there when the scene starts | |

`changes: [{at, idx, invert, tone, opacity, pulse}]` on a layer changes its datums later in the scene: `idx`
selects datums (default all; `invert: true` selects all the others), `tone` recolors, `opacity` dims or hides
(0 hides: e.g. swap one center text for another), `pulse` flashes them. This is how the deck's highlight is
animated: dim everything but the month that matters, turn the bars below a threshold red, pulse the outlier.

## Examples (fictitious numbers)

Waterfall with the two biggest drops highlighted on a word:
```json
{"type": "chart", "y": {"suffix": " k"},
 "layers": [{"mark": "bar", "transform": "waterfall", "labels": true,
   "data": [{"x": "Revenue", "y": 900, "total": true, "tone": "neutral"}, {"x": "Taxes", "y": -120, "tone": "bad"},
            {"x": "Payroll", "y": -310, "tone": "bad"}, {"x": "Suppliers", "y": -260, "tone": "bad"},
            {"x": "Other", "y": -40, "tone": "bad"}, {"x": "Result", "total": true}],
   "changes": [{"at": "payroll", "idx": [2, 3], "invert": true, "opacity": 0.35}]}]}
```

Combo: daily bars, cumulative line, dashed projection, target rule:
```json
{"type": "chart", "x": {"domain": ["1", "2", "3", "4", "5", "6", "7", "8"]}, "margin": {"right": 70},
 "legend": [{"label": "Daily", "tone": "neutral"}, {"label": "Cumulative", "mark": "line"}, {"label": "Projection", "mark": "dash", "tone": "text2"}],
 "layers": [
  {"mark": "bar", "tone": "neutral", "data": [{"x": "1", "y": 12}, {"x": "2", "y": 15}, {"x": "3", "y": 9}, {"x": "4", "y": 14}, {"x": "5", "y": 13}]},
  {"mark": "line", "transform": "cumulative", "labels": {"only": "last", "position": "right"}, "animate": {"at": "accumulated"},
   "data": [{"x": "1", "y": 12}, {"x": "2", "y": 15}, {"x": "3", "y": 9}, {"x": "4", "y": 14}, {"x": "5", "y": 13}]},
  {"mark": "line", "tone": "text2", "dash": [6, 4], "animate": {"at": "projection"}, "data": [{"x": "5", "y": 63}, {"x": "8", "y": 100}]},
  {"mark": "rule", "tone": "warn", "dash": [4, 4], "animate": {"enter": "rise", "at": "target"}, "data": [{"y": 110, "label": "Target 110"}]}]}
```

Donut with a counting center:
```json
{"type": "chart", "coord": "polar", "polar": {"start": 0, "end": 360},
 "layers": [
  {"mark": "arc", "inner": 0.7, "gap": 2, "data": [{"value": 62, "tone": "accent", "label": "Retail"}, {"value": 38, "tone": "neutral", "label": "Wholesale"}]},
  {"mark": "text", "size": 40, "weight": 800, "data": [{"value": 62, "format": "pct", "decimals": 0, "dy": 10}]}]}
```

Gauge with bands and a needle:
```json
{"type": "chart", "coord": "polar", "polar": {"start": -90, "end": 90, "min": 0, "max": 4, "ticks": [0, 2, 3, 4], "suffix": "×"},
 "layers": [
  {"mark": "arc", "inner": 0.86, "animate": {"enter": "fade"}, "data": [{"theta0": 0, "theta1": 2, "tone": "good"}, {"theta0": 2, "theta1": 3, "tone": "warn"}, {"theta0": 3, "theta1": 4, "tone": "bad"}]},
  {"mark": "rule", "animate": {"at": "leverage", "dur": 1.5}, "data": [{"value": 1.6, "from": 2.3}]},
  {"mark": "text", "size": 44, "weight": 800, "animate": {"at": "leverage"}, "data": [{"value": 1.6, "from": 2.3, "format": "x", "decimals": 1, "dy": 70, "dur": 1.5}]}]}
```

Sankey-like flow on a linear x (rects for nodes, ribbons for links, texts for names):
```json
{"type": "chart", "x": {"type": "linear", "min": 0, "max": 10, "labels": false}, "y": {"min": 0, "max": 100, "labels": false, "grid": false},
 "layers": [
  {"mark": "rect", "opacity": 1, "animate": {"enter": "grow"}, "data": [{"x0": 1, "x1": 1.2, "y0": 10, "y1": 90}]},
  {"mark": "ribbon", "tone": "bad", "animate": {"at": "costs", "stagger": 0.1},
   "data": [{"x0": 1.2, "y0": [90, 60], "x1": 6, "y1": [85, 55]}, {"x0": 1.2, "y0": [60, 40], "x1": 6, "y1": [50, 30]}]},
  {"mark": "text", "anchor": "start", "data": [{"x": 6.1, "y": 70, "text": "Payroll −30"}, {"x": 6.1, "y": 40, "text": "Rent −20"}]}]}
```

Stacked bars: one layer per series, `transform: "stack"` on each, `legend` with the deck's colors. Scenario lines:
one `line` layer per scenario, a `rule` per threshold, `labels: {"only": "last", "position": "right"}` and a right
margin for the end labels. Actual vs projection: two bar layers (the projection with `pattern: "hatch"`) and an `x`
rule at the split.

## Charts not shown above

There is no list of supported charts: build any chart from what it is made of. Recipes for common ones:

| Chart | Marks |
|---|---|
| scatter, bubble | `point` on a linear `x`; bubble size in each datum's `size` (px radius) |
| dual axis (bars + %) | `bar` layer + `line` layer with `axis: "y2"`, and `y2` with the % format |
| heatmap | `rect` per cell: `x0`=`x1`=the column category, `y0`/`y1` = row index + 0.05 / + 0.95 on `y: {min: 0, max: rows, labels: false, grid: false}`, intensity in `opacity` or `tone`; row names as `text` with `anchor: "end"`, a negative `dx` and a left `margin` |
| Gantt, timeline | `orient: "horizontal"`, `bar` with `y0`/`y1` = start/end per task; `rule` with `y` for today |
| boxplot | whiskers as `path` from min to max per category, `bar` with `y0`/`y1` = quartiles (`size: 0.4`), median as a short `path` using the px offsets |
| funnel, pyramid | closed `path` trapezoids on a linear `x` centered on 0, names as `text` |
| slope chart | one `line` per item over two categories, `markers`, `labels: {"position": "right"}` |
| bullet | wide grey `bar` layers for the ranges, a thin `bar` for the value, a `rule` per target with `x0`=`x1`=its category |
| radar | `coord: "polar"`, `polar: {start: 0, end: 360, r: {max}}`, `area` + `line` with `x` = the axis names |
| histogram | `bar` with `size: 1` (bars touch) |
| treemap, mosaic | `rect` per block on linear `x` and `y` (compute the block edges from the values) |
| range / error bars | `bar` or `path` from low to high, `point` for the value |
| KPI with sparkline | `number` component + a small `chart` with a `line` and `x`/`y` `labels: false` |

If a chart needs something the marks cannot express (a map, a 3D chart, an image), use the closest faithful
composition or a `slide` scene of that tab, and **say so in the delivery message** ("not reproduced exactly:
scene 3, the deck's map is shown as a slide"). Never silently replace a chart.

## Rules for the skill

- **Rebuild the deck's chart as it is**: same chart type, same data points, same colors, same highlight, same
  annotations (targets, averages, splits, callouts). Any chart is a combination of the marks above; never swap
  it for a different chart because it is easier.
- The animation tells the deck's highlight in time with the speech: the highlighted element enters or changes on
  the word that names it.
- Every number on screen must exist in the deck, with the same rounding. Never invent one.
- Keep text on screen short: labels, not sentences.
- Prefer `at` words that are spoken plainly; avoid words whose pronunciation is overridden for TTS.
