#pragma once
#include <cctype>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

// Scene timing without audio: duration comes from the narration text.
// Constants are the reference renderer's (pt-BR).
namespace timeline {

constexpr float kFps = 30;
constexpr float kLead = 0.35f;     // narration starts this long after the scene
constexpr float kPad = 1.0f;       // silence after each narrated scene
constexpr float kWordsPerSec = 2.7f;
constexpr float kSentencePause = 0.45f;
constexpr float kCommaPause = 0.15f;

inline bool isWord(const std::string& w) {
    for (unsigned char c : w)
        if (std::isalnum(c) || c >= 0x80 || c == '%' || c == '$') return true;
    return false;
}

inline float speechSeconds(const std::string& speech) {
    std::istringstream in(speech);
    std::string w;
    float t = 0;
    while (in >> w) {
        if (!isWord(w)) continue;
        const char last = w.back();
        t += 1 / kWordsPerSec;
        if (last == '.' || last == '!' || last == '?' || last == ':') t += kSentencePause;
        else if (last == ',') t += kCommaPause;
    }
    return t + 0.3f;
}

// Scene boundaries B[0..n]; scene k runs from B[k] to B[k+1].
inline std::vector<float> boundaries(const std::vector<std::string>& speeches) {
    std::vector<float> b{0};
    for (const auto& s : speeches) {
        const float d = speechSeconds(s) + kPad;
        b.push_back(std::round((b.back() + d) * 100) / 100);
    }
    return b;
}

}  // namespace timeline
