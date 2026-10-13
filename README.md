# llm-video-engine

Turns an HTML presentation deck into a short animated video summary.
An LLM reads the deck and writes a render package (a JSON plan plus assets).
A local C++ engine turns that package into the video, with no LLM, browser or
network involved.

## Status

Early planning. There is no code to run yet. The first step is a spike to check
that the native renderer is fast enough to be worth building.

## Layout

- `engine/` C++ render engine
- `schema/` JSON Schemas for the render package
- `themes/default/` neutral default theme pack
- `examples/` example packages, fictitious data only
- `tools/` helper scripts

## License

[MIT](LICENSE).
