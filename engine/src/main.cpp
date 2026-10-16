// llm-video-engine: renders a plan (schema llm-video-engine/plan@1) to video.
//
//   llm-video-engine render plan.json [--out file.mp4] [--threads N] [--null]
//   llm-video-engine grid   plan.json [--out grid.jpg]            one frame near the end of each scene, tiled
//   llm-video-engine frame  plan.json --at 12.5 [--out f.png]
//   llm-video-engine validate plan.json                 schema-level and layout problems, one pasteable line each
// plan.json can also be the video-plan.zip the skill produces (plan + assets); outputs go next to the zip.
//   render options: --encoder x264 (default) | qsv (Intel Quick Sync) | nvenc (NVIDIA)
//   --shutter N: average N sub-frames per frame across a 180-degree shutter (motion blur; 4 drafts, 8 finals)
//   --strip N: rows drawn at a time (default 64, 0 = whole frame); keeps each thread's pixels in cache
// Common: --font file.ttf (default: the bundled Montserrat), --scale 1.5 (pixels per stage unit).
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifdef __GLIBC__
#include <malloc.h>
#endif

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
    int shutter = 1;  // sub-frames averaged per frame (1 = off)
    int strip = 64;   // rows drawn at a time (even; 0 = whole frame)
    size_t bytes() const { return (size_t)w * h * 4; }
    size_t yuvBytes() const { return (size_t)w * h * 3 / 2; }
    int rows() const { return strip > 0 ? strip : h; }
};

// Draws rows [y0, y0 + sh) of the frame at t into dst (BGRA, row stride w * 4).
void drawStrip(const Video& v, const Fonts& fonts, const Frame& f, float t, int y0, int sh, uint8_t* dst) {
    if (v.twos) {  // hold every pose for two frames; shutter sub-frames stay inside the held frame
        int fr = (int)std::floor(t * timeline::kFps + 1e-3f);
        t = (fr - fr % 2) / timeline::kFps;
    }
    auto surface = SkSurfaces::WrapPixels(SkImageInfo::MakeN32Premul(f.w, sh), dst, (size_t)f.w * 4);
    SkCanvas* c = surface->getCanvas();
    c->translate(0, (float)-y0);
    c->scale(f.scale, f.scale);
    setDrawStrip(f.w, f.h, y0);
    draw(c, *v.root, t, fonts);
}

// One strip with the shutter applied: the average of sub-frames spread over a 180-degree shutter (half
// the frame interval from t), so everything blurs by its own speed and still things stay sharp.
void renderStrip(const Video& v, const Fonts& fonts, const Frame& f, float t, int y0, int sh, uint8_t* dst) {
    drawStrip(v, fonts, f, t, y0, sh, dst);
    if (f.shutter <= 1) return;
    const size_t n = (size_t)f.w * sh * 4;
    thread_local std::vector<uint8_t> sub;
    thread_local std::vector<uint16_t> acc;
    sub.resize(n);
    acc.resize(n);
    for (size_t i = 0; i < n; ++i) acc[i] = dst[i];
    for (int j = 1; j < f.shutter; ++j) {
        drawStrip(v, fonts, f, t + .5f / timeline::kFps * j / f.shutter, y0, sh, sub.data());
        for (size_t i = 0; i < n; ++i) acc[i] += sub[i];
    }
    // Rounded average as a fixed-point multiply (a division by a runtime n does not vectorize).
    const uint32_t mul = (65536 + f.shutter - 1) / f.shutter, half = f.shutter / 2;
    for (size_t i = 0; i < n; ++i) dst[i] = (uint8_t)(((acc[i] + half) * mul) >> 16);
}

// In strips: a whole 1080p frame (8 MB) and its full-frame layers do not fit in cache once several
// threads render at once; a strip and its layers do.
void renderFrame(const Video& v, const Fonts& fonts, const Frame& f, float t, uint8_t* bgra) {
    for (int y0 = 0; y0 < f.h; y0 += f.rows())
        renderStrip(v, fonts, f, t, y0, std::min(f.rows(), f.h - y0), bgra + (size_t)y0 * f.w * 4);
}

// BGRA rows to planar YUV 4:2:0, BT.601 limited range (what ffmpeg's own conversion produces by default),
// chroma from each 2x2 block's average. `y0` and `sh` are even.
void toYuv420(const uint8_t* bgra, int w, int h, int y0, int sh, uint8_t* yuv) {
    uint8_t* Y = yuv;
    uint8_t* U = yuv + (size_t)w * h;
    uint8_t* V = U + (size_t)(w / 2) * (h / 2);
    for (int r = 0; r < sh; ++r) {
        const uint8_t* p = bgra + (size_t)r * w * 4;
        uint8_t* y = Y + (size_t)(y0 + r) * w;
        for (int x = 0; x < w; ++x, p += 4) y[x] = (uint8_t)(((66 * p[2] + 129 * p[1] + 25 * p[0] + 128) >> 8) + 16);
    }
    for (int r = 0; r < sh; r += 2) {
        const uint8_t* a = bgra + (size_t)r * w * 4;
        const uint8_t* b = a + (size_t)w * 4;
        uint8_t* u = U + (size_t)((y0 + r) / 2) * (w / 2);
        uint8_t* v = V + (size_t)((y0 + r) / 2) * (w / 2);
        for (int x = 0; x < w / 2; ++x, a += 8, b += 8) {
            const int B = a[0] + a[4] + b[0] + b[4], G = a[1] + a[5] + b[1] + b[5], R = a[2] + a[6] + b[2] + b[6];
            u[x] = (uint8_t)(((-38 * R - 74 * G + 112 * B + 512) >> 10) + 128);
            v[x] = (uint8_t)(((112 * R - 94 * G - 18 * B + 512) >> 10) + 128);
        }
    }
}

// A frame for the encoder: each strip is converted to YUV while it is still in cache.
void renderYuv(const Video& v, const Fonts& fonts, const Frame& f, float t, uint8_t* yuv) {
    thread_local std::vector<uint8_t> strip;
    strip.resize((size_t)f.w * f.rows() * 4);
    for (int y0 = 0; y0 < f.h; y0 += f.rows()) {
        const int sh = std::min(f.rows(), f.h - y0);
        renderStrip(v, fonts, f, t, y0, sh, strip.data());
        toYuv420(strip.data(), f.w, f.h, y0, sh, yuv);
    }
}

// Extracts a plan zip into a fresh temp folder and returns the folder.
fs::path extractZip(const fs::path& zip) {
    const fs::path dir = fs::temp_directory_path() / fs::u8path("llm-video-engine-" + u8(zip.stem()));
    fs::remove_all(dir);
    fs::create_directories(dir);
#ifdef _WIN32
    // Windows 10+ ships bsdtar, which reads zip.
    // cmd.exe strips one pair of outer quotes, so the whole command is wrapped once more.
    const std::string cmd = "\"\"%SystemRoot%\\System32\\tar.exe\" -xf \"" + u8(zip) + "\" -C \"" + u8(dir) + "\"\"";
#else
    const std::string cmd = "unzip -q -o \"" + u8(zip) + "\" -d \"" + u8(dir) + "\"";
#endif
    if (std::system(cmd.c_str()) != 0 || !fs::exists(dir / "plan.json"))
        throw std::runtime_error("cannot extract plan.json from " + u8(zip));
    return dir;
}

// Folder of the running executable (for the bundled font in a release package).
fs::path exeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n ? fs::path(std::wstring(buf, n)).parent_path() : fs::path();
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}

// The default font: next to the exe in a release package, else the source tree it was built from.
std::string defaultFont() {
    for (const fs::path& p : {exeDir() / "fonts" / "Montserrat-VF.ttf", exeDir() / "Montserrat-VF.ttf"})
        if (fs::exists(p)) return u8(p);
    return LVE_DEFAULT_FONT;
}

// ffmpeg must be on PATH for every command that writes an image or video.
void requireFfmpeg() {
#ifdef _WIN32
    const int rc = std::system("ffmpeg -version >NUL 2>&1");
#else
    const int rc = std::system("ffmpeg -version >/dev/null 2>&1");
#endif
    if (rc != 0)
        throw std::runtime_error("ffmpeg was not found on PATH. Install it (Windows: winget install Gyan.FFmpeg, "
                                 "Linux: your package manager), then open a new terminal.");
}

std::string safeName(std::string s) {
    for (auto& ch : s)
        if (std::string("<>:\"/\\|?*").find(ch) != std::string::npos) ch = '-';
    return s;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  // messages are UTF-8 (the "·" separators, accented words)
#endif
#ifdef __GLIBC__
    // Layers and strip buffers are large blocks allocated and freed every frame. By default glibc maps
    // each with mmap and unmaps it on free, which serializes the render threads on the kernel's memory
    // map; keeping them in the heap for reuse is ~1.5x faster with 12 threads.
    mallopt(M_MMAP_THRESHOLD, 256 << 20);
    mallopt(M_TRIM_THRESHOLD, 512 << 20);
#endif
    // A release package ships ffmpeg and ffprobe next to the exe: look there first.
    if (const fs::path dir = exeDir(); !dir.empty()) {
        const char* old = std::getenv("PATH");
#ifdef _WIN32
        _putenv_s("PATH", (u8(dir) + ";" + (old ? old : "")).c_str());
#else
        setenv("PATH", (u8(dir) + ":" + (old ? old : "")).c_str(), 1);
#endif
    }
    if (argc == 2 && std::string(argv[1]) == "--version") { std::cout << "llm-video-engine " LVE_VERSION "\n"; return 0; }
    if (argc < 3) {
        std::cerr << "llm-video-engine " LVE_VERSION "\n"
                  << "usage:llm-video-engine (render|grid|frame|validate) plan.json|video-plan.zip [--out path] [--at t] [--threads N] [--font f.ttf] [--null] [--shutter N]\n";
        return 2;
    }
    const std::string cmd = argv[1];
    const fs::path input = fs::u8path(argv[2]);
    std::string out, font = defaultFont();
    int threads = (int)std::thread::hardware_concurrency();
    float atT = -1;
    bool null = false;
    int shutter = 1, strip = 64;
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
        else if (a == "--shutter") shutter = std::clamp(std::stoi(next()), 1, 64);
        else if (a == "--strip") strip = std::max(0, std::stoi(next())) & ~1;  // even, for 4:2:0 chroma rows
        else { std::cerr << "unknown option: " << a << "\n"; return 2; }
    }

    try {
        const auto t0 = std::chrono::steady_clock::now();
        const bool zipped = input.extension() == ".zip";
        const fs::path planPath = zipped ? extractZip(input) / "plan.json" : input;
        std::ifstream f(planPath);
        if (!f) throw std::runtime_error("cannot open plan: " + u8(planPath));
        const auto plan = nlohmann::json::parse(f);
        const fs::path base = planPath.parent_path().empty() ? fs::path(".") : planPath.parent_path();
        // Outputs go next to the plan, or next to the zip it came in.
        const fs::path outDir = zipped ? (input.parent_path().empty() ? fs::path(".") : input.parent_path()) : base;
        const Fonts fonts(font);
        const Frame fr{1920, 1080, 1.5f, shutter, strip};
        const Video video = buildVideo(plan, fonts, u8(base), fr.scale);
        if (cmd == "validate") {
            std::vector<std::string> report = video.warnings;
            for (auto& l : checkLayout(video, fonts)) report.push_back(std::move(l));
            const float target = plan.value("config", nlohmann::json::object()).value("target_duration", 0.f);
            if (target > 0 && std::fabs(video.duration - target) > target * .15f)
                report.push_back("plan \xc2\xb7 DURATION \xc2\xb7 video is " + std::to_string((int)std::lround(video.duration)) + " s, target " +
                                 std::to_string((int)std::lround(target)) + " s");
            for (const auto& l : report) std::cout << l << "\n";
            std::cout << (report.empty() ? std::string("plan ok") : std::to_string(report.size()) + " problem(s)") << " ("
                      << video.B.size() - 1 << " scenes, " << std::lround(video.duration) << " s)\n";
            return report.empty() ? 0 : 1;
        }
        for (const auto& w : video.warnings) std::cerr << "warning: " << w << "\n";
        if (!null) requireFfmpeg();
        const double setup = secondsSince(t0);
        const std::string stem = safeName(plan.value("output", u8(planPath.stem())));

        if (cmd == "frame") {
            if (atT < 0) throw std::runtime_error("frame needs --at seconds");
            if (out.empty()) out = u8(outDir / fs::u8path(stem + "_frame.png"));
            std::vector<uint8_t> px(fr.bytes());
            renderFrame(video, fonts, fr, atT, px.data());
            FfmpegPipe pipe(fr.w, fr.h, timeline::kFps, {"-frames:v", "1", out});
            pipe.write(px.data(), px.size());
            if (pipe.close() == 0) std::cout << out << "\n";
            return 0;
        }

        if (cmd == "grid") {
            if (out.empty()) out = u8(outDir / fs::u8path(stem + "_grid.jpg"));
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
        auto render = [&](int i, uint8_t* yuv) { renderYuv(video, fonts, fr, i / timeline::kFps, yuv); };
        int rc = 0;
        if (null) {
            renderOrdered(frames, threads, fr.yuvBytes(), render, [](const uint8_t*) {});
        } else {
            // Every requested format comes out of one ffmpeg process reading the frames once.
            std::vector<std::string> args;
            std::vector<std::string> formats = plan.value("config", nlohmann::json::object()).value("formats", std::vector<std::string>{"1080"});
            const bool one = formats.size() == 1;
            for (const auto& fmt : formats) {
                const std::string path = !out.empty() && one ? out : u8(outDir / fs::u8path(stem + (one ? "" : " (" + fmt + ")") + ".mp4"));
                // qsv: Intel Quick Sync on the GPU, which leaves the CPU to the renderer.
                // nvenc: NVIDIA's encoder, same idea on an NVIDIA GPU.
                const std::vector<std::string> enc = encoder == "qsv"
                    ? std::vector<std::string>{"-c:v", "h264_qsv", "-preset", "medium", "-global_quality", fmt == "1080" ? "20" : "24"}
                    : encoder == "nvenc"
                    ? std::vector<std::string>{"-c:v", "h264_nvenc", "-pix_fmt", "yuv420p", "-preset", "p5", "-rc", "vbr", "-cq", fmt == "1080" ? "20" : "24", "-b:v", "0"}
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
            FfmpegPipe pipe(fr.w, fr.h, timeline::kFps, args, "yuv420p");
            renderOrdered(frames, threads, fr.yuvBytes(), render, [&](const uint8_t* yuv) { pipe.write(yuv, fr.yuvBytes()); });
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
