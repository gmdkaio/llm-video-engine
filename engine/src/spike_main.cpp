// E0 spike: renders a plan of kpi scenes to MP4 and reports timings.
//
//   spike plan.json --font Montserrat.ttf --out out.mp4 [--threads N]
//   spike plan.json --font Montserrat.ttf --null [--threads N]       render only, no encode
//   spike plan.json --font Montserrat.ttf --frame 4.5 --out f.png    one frame
#include <chrono>
#include <cmath>
#include <cstring>
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

namespace {

constexpr int kOutW = 1920, kOutH = 1080;
constexpr float kScale = 1.5f;

void renderFrame(const Video& v, const Fonts& fonts, float t, uint8_t* pixels) {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(kOutW, kOutH);
    auto surface = SkSurfaces::WrapPixels(info, pixels, (size_t)kOutW * 4);
    SkCanvas* c = surface->getCanvas();
    c->scale(kScale, kScale);
    draw(c, *v.root, t, fonts);
}

double secondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
    std::string planPath, font, out;
    int threads = (int)std::thread::hardware_concurrency();
    bool null = false;
    float frameAt = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--font") font = next();
        else if (a == "--out") out = next();
        else if (a == "--threads") threads = std::stoi(next());
        else if (a == "--null") null = true;
        else if (a == "--frame") frameAt = std::stof(next());
        else planPath = a;
    }
    if (planPath.empty() || font.empty() || (out.empty() && !null)) {
        std::cerr << "usage: spike plan.json --font font.ttf (--out out.mp4 | --null) [--threads N] [--frame t]\n";
        return 2;
    }

    try {
        const auto t0 = std::chrono::steady_clock::now();
        std::ifstream f(planPath);
        if (!f) throw std::runtime_error("cannot open plan: " + planPath);
        const auto plan = nlohmann::json::parse(f);
        const Fonts fonts(font);
        const Video video = buildVideo(plan, fonts);
        const double setup = secondsSince(t0);
        const size_t bytes = (size_t)kOutW * kOutH * 4;

        if (frameAt >= 0) {
            std::vector<uint8_t> px(bytes);
            renderFrame(video, fonts, frameAt, px.data());
            FfmpegPipe pipe(kOutW, kOutH, timeline::kFps, {"-frames:v", "1", out});
            pipe.write(px.data(), bytes);
            return pipe.close();
        }

        const int frames = (int)(video.duration * timeline::kFps) + 1;
        std::cout << "video " << video.duration << " s, " << frames << " frames, " << threads << " threads\n";
        const auto t1 = std::chrono::steady_clock::now();
        auto render = [&](int i, uint8_t* px) { renderFrame(video, fonts, i / timeline::kFps, px); };
        int rc = 0;
        if (null) {
            renderOrdered(frames, threads, bytes, render, [](const uint8_t*) {});
        } else {
            FfmpegPipe pipe(kOutW, kOutH, timeline::kFps,
                            {"-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", "-preset", "medium",
                             "-movflags", "+faststart", out});
            renderOrdered(frames, threads, bytes, render, [&](const uint8_t* px) { pipe.write(px, bytes); });
            rc = pipe.close();
        }
        const double run = secondsSince(t1);
        std::cout << "setup " << setup << " s, " << (null ? "render" : "render+encode") << " " << run << " s, "
                  << frames / run << " fps\n";
        return rc;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
