#include "encode/pipe.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

static std::string quote(const std::string& s) {
    if (s.find_first_of(" \t\"") == std::string::npos) return s;
    std::string q = "\"";
    for (char c : s) q += c == '"' ? std::string("\\\"") : std::string(1, c);
    return q + "\"";
}

FfmpegPipe::FfmpegPipe(int width, int height, float fps, const std::vector<std::string>& outputArgs) {
    std::string cmd = "ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt bgra -s " + std::to_string(width) +
                      "x" + std::to_string(height) + " -framerate " + std::to_string(fps) + " -i -";
    for (const auto& a : outputArgs) cmd += " " + quote(a);
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";  // cmd.exe strips one pair of outer quotes
    pipe_ = popen(cmd.c_str(), "wb");
#else
    pipe_ = popen(cmd.c_str(), "w");
#endif
    if (!pipe_) throw std::runtime_error("could not start ffmpeg");
}

FfmpegPipe::~FfmpegPipe() { close(); }

void FfmpegPipe::write(const void* data, size_t bytes) {
    if (std::fwrite(data, 1, bytes, pipe_) != bytes) throw std::runtime_error("ffmpeg pipe closed early");
}

int FfmpegPipe::close() {
    if (!pipe_) return 0;
    const int rc = pclose(pipe_);
    pipe_ = nullptr;
    return rc;
}

void renderOrdered(int count, int threads, size_t frameBytes, const RenderFn& render, const SinkFn& sink) {
    if (threads < 1) threads = 1;
    const int slots = threads * 2;
    std::vector<std::vector<uint8_t>> buf(slots, std::vector<uint8_t>(frameBytes));
    std::vector<int> ready(slots, -1);  // frame index held by each slot, -1 if free
    std::mutex m;
    std::condition_variable cv;
    std::atomic<int> next{0};
    int written = 0;

    auto worker = [&] {
        for (;;) {
            const int f = next.fetch_add(1);
            if (f >= count) return;
            const int s = f % slots;
            {
                // Wait until the frame that used this slot has been written.
                std::unique_lock lk(m);
                cv.wait(lk, [&] { return f - written < slots; });
            }
            render(f, buf[s].data());
            {
                std::lock_guard lk(m);
                ready[s] = f;
            }
            cv.notify_all();
        }
    };

    std::vector<std::thread> pool;
    for (int i = 0; i < threads; ++i) pool.emplace_back(worker);
    for (int f = 0; f < count; ++f) {
        const int s = f % slots;
        {
            std::unique_lock lk(m);
            cv.wait(lk, [&] { return ready[s] == f; });
        }
        sink(buf[s].data());
        {
            std::lock_guard lk(m);
            ready[s] = -1;
            written = f + 1;
        }
        cv.notify_all();
    }
    for (auto& t : pool) t.join();
}
