// llm-video-engine: renders a plan (schema llm-video-engine/plan@1) to video.
//
//   llm-video-engine render plan.json [--out file.mp4] [--threads N] [--null]
//   llm-video-engine grid   plan.json [--out grid.jpg]            one frame near the end of each scene, tiled
//   llm-video-engine frame  plan.json --at 12.5 [--out f.png]
//   render options: --encoder x264 (default) | qsv (Intel Quick Sync)
// Common: --font file.ttf (default: the bundled Montserrat), --scale 1.5 (pixels per stage unit).
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "core/timeline.h"
#include "encode/pipe.h"
#include "render/text.h"
#include "scenes/build.h"

namespace fs = std::filesystem;

namespace {

// UTF-8 std::string from a path (C++20 u8string() returns std::u8string).
std::string u8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

double secondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

struct Frame {
    int w, h;
    float scale;
    size_t bytes() const { return (size_t)w * h * 4; }
};

void renderFrame(const Video& v, const Fonts& fonts, const Frame& f, float t, uint8_t* pixels) {
    auto surface = SkSurfaces::WrapPixels(SkImageInfo::MakeN32Premul(f.w, f.h), pixels, (size_t)f.w * 4);
    SkCanvas* c = surface->getCanvas();
    c->scale(f.scale, f.scale);
    draw(c, *v.root, t, fonts);
}

std::string safeName(std::string s) {
    for (auto& ch : s)
        if (std::string("<>:\"/\\|?*").find(ch) != std::string::npos) ch = '-';
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: llm-video-engine (render|grid|frame) plan.json [--out path] [--at t] [--threads N] [--font f.ttf] [--null]\n";
        return 2;
    }
    const std::string cmd = argv[1];
    const fs::path planPath = fs::u8path(argv[2]);
    std::string out, font = LVE_DEFAULT_FONT;
    int threads = (int)std::thread::hardware_concurrency();
    float atT = -1;
    bool null = false;
    std::string encoder = "x264";
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--out") out = next();
        else if (a == "--font") font = next();
        else if (a == "--threads") threads = std::stoi(next());
        else if (a == "--at") atT = std::stof(next());
        else if (a == "--null") null = true;
        else if (a == "--encoder") encoder = next();
        else { std::cerr << "unknown option: " << a << "\n"; return 2; }
    }

    try {
        const auto t0 = std::chrono::steady_clock::now();
        std::ifstream f(planPath);
        if (!f) throw std::runtime_error("cannot open plan: " + u8(planPath));
        const auto plan = nlohmann::json::parse(f);
        const fs::path base = planPath.parent_path().empty() ? fs::path(".") : planPath.parent_path();
        const Fonts fonts(font);
        const Frame fr{1920, 1080, 1.5f};
        const Video video = buildVideo(plan, fonts, u8(base), fr.scale);
        for (const auto& w : video.warnings) std::cerr << "warning: " << w << "\n";
        const double setup = secondsSince(t0);
        const std::string stem = safeName(plan.value("output", u8(planPath.stem())));

        if (cmd == "frame") {
            if (atT < 0) throw std::runtime_error("frame needs --at seconds");
            if (out.empty()) out = u8(base / fs::u8path(stem + "_frame.png"));
            std::vector<uint8_t> px(fr.bytes());
            renderFrame(video, fonts, fr, atT, px.data());
            FfmpegPipe pipe(fr.w, fr.h, timeline::kFps, {"-frames:v", "1", out});
            pipe.write(px.data(), px.size());
            if (pipe.close() == 0) std::cout << out << "\n";
            return 0;
        }

        if (cmd == "grid") {
            if (out.empty()) out = u8(base / fs::u8path(stem + "_grid.jpg"));
            const int n = (int)video.B.size() - 1, cols = 3, rows = (n + cols - 1) / cols;
            FfmpegPipe pipe(fr.w, fr.h, timeline::kFps,
                            {"-vf", "scale=640:360,tile=" + std::to_string(cols) + "x" + std::to_string(rows), "-frames:v", "1", "-q:v", "3", out});
            std::vector<uint8_t> px(fr.bytes());
            for (int k = 0; k < n; ++k) {
                renderFrame(video, fonts, fr, std::round((video.B[k + 1] - .6f) * 100) / 100, px.data());
                pipe.write(px.data(), px.size());
            }
            std::fill(px.begin(), px.end(), 0);
            for (int k = n; k < cols * rows; ++k) pipe.write(px.data(), px.size());
            if (pipe.close() == 0) std::cout << out << "\n";
            return 0;
        }

        if (cmd != "render") throw std::runtime_error("unknown command: " + cmd);
        const int frames = (int)(video.duration * timeline::kFps) + 1;
        std::cout << "video " << video.duration << " s, " << frames << " frames, " << threads << " threads, setup " << setup << " s\n";
        const auto t1 = std::chrono::steady_clock::now();
        auto render = [&](int i, uint8_t* px) { renderFrame(video, fonts, fr, i / timeline::kFps, px); };
        int rc = 0;
        if (null) {
            renderOrdered(frames, threads, fr.bytes(), render, [](const uint8_t*) {});
        } else {
            // Every requested format comes out of one ffmpeg process reading the frames once.
            std::vector<std::string> args;
            std::vector<std::string> formats = plan.value("config", nlohmann::json::object()).value("formats", std::vector<std::string>{"1080"});
            const bool one = formats.size() == 1;
            for (const auto& fmt : formats) {
                const std::string path = !out.empty() && one ? out : u8(base / fs::u8path(stem + (one ? "" : " (" + fmt + ")") + ".mp4"));
                // qsv: Intel Quick Sync on the GPU, which leaves the CPU to the renderer.
                const std::vector<std::string> enc = encoder == "qsv"
                    ? std::vector<std::string>{"-c:v", "h264_qsv", "-preset", "medium", "-global_quality", fmt == "1080" ? "20" : "24"}
                    : std::vector<std::string>{"-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", fmt == "1080" ? "18" : "23", "-preset", "medium"};
                if (fmt == "1080") {
                    args.insert(args.end(), enc.begin(), enc.end());
                    args.insert(args.end(), {"-movflags", "+faststart", path});
                }
                else if (fmt == "720") {
                    args.insert(args.end(), {"-vf", "scale=1280:720"});
                    args.insert(args.end(), enc.begin(), enc.end());
                    args.insert(args.end(), {"-profile:v", "main", "-movflags", "+faststart", path});
                }
                else throw std::runtime_error("unknown format: " + fmt);
                std::cout << "-> " << path << "\n";
            }
            FfmpegPipe pipe(fr.w, fr.h, timeline::kFps, args);
            renderOrdered(frames, threads, fr.bytes(), render, [&](const uint8_t* px) { pipe.write(px, fr.bytes()); });
            rc = pipe.close();
        }
        const double run = secondsSince(t1);
        std::cout << (null ? "render " : "render+encode ") << run << " s, " << frames / run << " fps\n";
        return rc;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
