// llm-video-engine: renders a plan (schema llm-video-engine/plan@1) to video.
//
//   llm-video-engine render plan.json [--out file.mp4] [--threads N] [--null]
//   llm-video-engine grid   plan.json [--out grid.jpg]            one frame near the end of each scene, tiled
//   llm-video-engine frame  plan.json --at 12.5 [--out f.png]
//   llm-video-engine validate plan.json                 schema-level and layout problems, one pasteable line each
// plan.json can also be the video-plan.zip the skill produces (plan + assets); outputs go next to the zip.
//   render options: --encoder x264 (default) | qsv (Intel Quick Sync)
// Common: --font file.ttf (default: the bundled Montserrat), --scale 1.5 (pixels per stage unit).
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
    size_t bytes() const { return (size_t)w * h * 4; }
};

void renderFrame(const Video& v, const Fonts& fonts, const Frame& f, float t, uint8_t* pixels) {
    auto surface = SkSurfaces::WrapPixels(SkImageInfo::MakeN32Premul(f.w, f.h), pixels, (size_t)f.w * 4);
    SkCanvas* c = surface->getCanvas();
    c->scale(f.scale, f.scale);
    draw(c, *v.root, t, fonts);
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
        throw std::runtime_error("ffmpeg was not found on PATH. Install it (Windows: winget install Gyan.FFmpeg), "
                                 "then open a new terminal.");
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
    // A release package ships ffmpeg and ffprobe next to the exe: look there first.
    if (const fs::path dir = exeDir(); !dir.empty()) {
        const char* old = std::getenv("PATH");
#ifdef _WIN32
        _putenv_s("PATH", (u8(dir) + ";" + (old ? old : "")).c_str());
#else
        setenv("PATH", (u8(dir) + ":" + (old ? old : "")).c_str(), 1);
#endif
    }
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
                  << "usage:llm-video-engine (render|grid|frame|validate) plan.json|video-plan.zip [--out path] [--at t] [--threads N] [--font f.ttf] [--null]\n";
        return 2;
    }
    const std::string cmd = argv[1];
    const fs::path input = fs::u8path(argv[2]);
    std::string out, font = defaultFont();
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
        const bool zipped = input.extension() == ".zip";
        const fs::path planPath = zipped ? extractZip(input) / "plan.json" : input;
        std::ifstream f(planPath);
        if (!f) throw std::runtime_error("cannot open plan: " + u8(planPath));
        const auto plan = nlohmann::json::parse(f);
        const fs::path base = planPath.parent_path().empty() ? fs::path(".") : planPath.parent_path();
        // Outputs go next to the plan, or next to the zip it came in.
        const fs::path outDir = zipped ? (input.parent_path().empty() ? fs::path(".") : input.parent_path()) : base;
        const Fonts fonts(font);
        const Frame fr{1920, 1080, 1.5f};
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
                const std::string path = !out.empty() && one ? out : u8(outDir / fs::u8path(stem + (one ? "" : " (" + fmt + ")") + ".mp4"));
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
