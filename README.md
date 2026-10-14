# llm-video-engine

Turns an HTML presentation deck into a short animated video summary.
An LLM reads the deck and writes a render plan (JSON plus assets).
A local C++ engine turns that plan into the video, with no LLM, browser or
network involved.

## Status

Early. The engine renders plans written in the `llm-video-engine/plan@1` format
(`schema/plan.schema.json`, documented in `skill/COMPONENTS.md`), and the skill
in `skill/` analyzes a deck and writes such a plan. Narration audio, vertical
video and the slide-camera scene are not built yet.

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
- `tools/` helper scripts

## License

[MIT](LICENSE).
