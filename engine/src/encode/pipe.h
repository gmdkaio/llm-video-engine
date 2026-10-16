#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// Raw frames into one external ffmpeg process.
class FfmpegPipe {
public:
    // `outputArgs` go after the rawvideo input, e.g. {"-c:v", "libx264", ..., "out.mp4"}.
    // `pixFmt` is the layout of the frames written: bgra, or yuv420p (planar, w * h * 3 / 2 bytes).
    FfmpegPipe(int width, int height, float fps, const std::vector<std::string>& outputArgs, const std::string& pixFmt = "bgra");
    ~FfmpegPipe();
    void write(const void* data, size_t bytes);
    int close();  // ffmpeg's exit code

private:
    FILE* pipe_ = nullptr;
};

// Renders frames 0..count-1 on `threads` workers and hands them to `sink` in
// order. Each frame is rendered independently, so any worker can take any frame.
using RenderFn = std::function<void(int frame, uint8_t* pixels)>;
using SinkFn = std::function<void(const uint8_t* pixels)>;
void renderOrdered(int count, int threads, size_t frameBytes, const RenderFn& render, const SinkFn& sink);
