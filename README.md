# llm-video-engine

Turns an HTML presentation deck into a short animated video summary.
An LLM reads the deck and writes a render plan (JSON plus assets).
A local C++ engine turns that plan into the video, with no LLM, browser or
network involved.

## Status

Early. The engine renders plans written in the `llm-video-engine/plan@1` format
(`schema/plan.schema.json`, documented in `skill/COMPONENTS.md`), and the skill
in `skill/` analyzes a deck and writes such a plan. Narration audio and vertical
video are not built yet.

## Charts

There is no fixed list of chart types. A chart in the plan is layers of marks
(bars, lines, areas, rects, rules, points, texts, arcs, ribbons, paths), so the
LLM can rebuild whatever chart the deck has with its own data and highlight.
Some of them, rendered by the engine from fictitious plans:

<!-- charts:start -->
<table>
<tr>
<td width="50%" valign="top">
<img src=".github/assets/charts/waterfall.gif" alt="Waterfall" width="100%"><br>
<b>Waterfall</b><br>
From opening to closing balance, step by step; the biggest drops dim the rest.
</td>
<td width="50%" valign="top">
<img src=".github/assets/charts/combo.gif" alt="Bars, cumulative line and target" width="100%"><br>
<b>Bars, cumulative line and target</b><br>
Daily bars, the running total drawn over them, a dashed projection and the target rising into place.
</td>
</tr>
<tr>
<td width="50%" valign="top">
<img src=".github/assets/charts/stacked.gif" alt="Stacked bars" width="100%"><br>
<b>Stacked bars</b><br>
Series pile up per category with the deck's own shades and a legend.
</td>
<td width="50%" valign="top">
<img src=".github/assets/charts/scenarios.gif" alt="Scenario lines" width="100%"><br>
<b>Scenario lines</b><br>
One line per scenario, threshold rules and labels at the line ends.
</td>
</tr>
<tr>
<td width="50%" valign="top">
<img src=".github/assets/charts/donut.gif" alt="Donut" width="100%"><br>
<b>Donut</b><br>
Arcs sweep in order and the center counts up to the share that matters.
</td>
<td width="50%" valign="top">
<img src=".github/assets/charts/gauge.gif" alt="Gauge" width="100%"><br>
<b>Gauge</b><br>
Colored bands, a needle that swings from last value to this one, and a counter.
</td>
</tr>
<tr>
<td width="50%" valign="top">
<img src=".github/assets/charts/flow.gif" alt="Flow (Sankey)" width="100%"><br>
<b>Flow (Sankey)</b><br>
Inflow split into ribbons that curve to each outflow, and what is left over.
</td>
<td width="50%" valign="top">
<img src=".github/assets/charts/dual-axis.gif" alt="Dual axis" width="100%"><br>
<b>Dual axis</b><br>
Bars on the left scale, a percentage line on its own scale on the right.
</td>
</tr>
<tr>
<td width="50%" valign="top">
<img src=".github/assets/charts/radar.gif" alt="Radar" width="100%"><br>
<b>Radar</b><br>
Two profiles over the same axes, filled shape growing from the center.
</td>
<td width="50%" valign="top">
<img src=".github/assets/charts/scatter.gif" alt="Scatter and bubbles" width="100%"><br>
<b>Scatter and bubbles</b><br>
Points on two value scales, bubble size per point, and a ping on the outlier.
</td>
</tr>
<tr>
<td width="50%" valign="top">
<img src=".github/assets/charts/heatmap.gif" alt="Heatmap" width="100%"><br>
<b>Heatmap</b><br>
One cell per pair, intensity as opacity, rows labelled on the left.
</td>
<td width="50%" valign="top">
<img src=".github/assets/charts/gantt.gif" alt="Gantt" width="100%"><br>
<b>Gantt</b><br>
Horizontal bars from start to end per task, and a rule for today.
</td>
</tr>
<tr>
<td width="50%" valign="top">
<img src=".github/assets/charts/funnel.gif" alt="Funnel" width="100%"><br>
<b>Funnel</b><br>
Closed shapes narrowing stage by stage, with the counts inside.
</td>
<td width="50%" valign="top">
<img src=".github/assets/charts/bullet.gif" alt="Bullet" width="100%"><br>
<b>Bullet</b><br>
Value against its ranges and target, one row per metric.
</td>
</tr>
</table>
<!-- charts:end -->

The GIFs come from `node tools/readme_charts.mjs`.

## How it is used

1. The LLM, with the skill, reads the deck and returns `video-plan.zip`: the plan,
   the logos it uses and the narration text. It never renders video.
2. On your machine:

```
llm-video-engine validate video-plan.zip   # layout problems, one pasteable line each
llm-video-engine grid video-plan.zip       # one frame per scene
llm-video-engine render video-plan.zip     # the MP4 (add --encoder qsv for Intel Quick Sync)
```

The engine builds to `engine/build/release/`. Add that folder to your PATH once so the
commands above work from anywhere (PowerShell, then open a new terminal):

```
[Environment]::SetEnvironmentVariable('Path', [Environment]::GetEnvironmentVariable('Path', 'User') + ';' + (Resolve-Path engine\build\release), 'User')
```

To install the skill in Claude, build its zip with `node tools/package_skill.mjs`
and upload `dist/llm-video-engine-skill.zip`.

## Benchmark

Render time for the same video, produced two ways on the same machine:

- **HTML path:** the scenes as an HTML/GSAP page, captured frame by frame in
  headless Chrome (seek, JPEG screenshot), then encoded with x264. This is how
  the videos were made before this project.
- **llm-video-engine:** the same scenes as a plan, drawn natively with Skia on
  12 threads and piped to ffmpeg.

Test video: a real 131 s monthly-results summary (9 scenes with charts,
counters and a cash-flow diagram), 1920×1080 at 30 fps, 3,932 frames. Both paths
use the same scene timing. Frames were compared side by side at the end of every
scene and at 8 points mid-animation, and match.

| Path | Encoder | End to end | Speedup |
|---|---|---|---|
| HTML path (headless Chrome) | x264 medium, crf 18 | 627.6 s | 1× |
| llm-video-engine | x264 medium, crf 18 | 125.7 s | 5.0× |
| llm-video-engine | Intel Quick Sync (h264_qsv) | 73.3 s | 8.6× |

Machine: Intel Core i5-1235U (10 cores, 12 threads), 8 GB RAM, Windows 11,
ffmpeg 9.0.1. One run per path; on this laptop, run-to-run variation is around
15%, so read the speedups as approximate.

Where the time goes: the engine draws frames at about 91 fps on its own, so with
x264 the CPU is shared between drawing and encoding. Quick Sync moves encoding
to the integrated GPU (SSIM 0.9992 against 0.9997 for x264 crf 18, file about
30% smaller). Skia is currently built with MSVC, which leaves out its fastest
CPU paths; a clang build is the next thing to try.

Not measured yet: LLM tokens, wall-clock time and iterations per video with and
without the engine.

## Layout

- `engine/` C++ render engine (`llm-video-engine render | grid | frame`)
- `schema/` JSON Schema for the plan
- `skill/` the LLM side: deck analysis, plan format, plan validator
- `themes/default/` default font (Montserrat, OFL)
- `examples/` example plans, fictitious data only
- `tools/` helper scripts (`package_skill.mjs` builds the skill zip)

## License

[MIT](LICENSE).
