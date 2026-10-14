#pragma once
#include <cctype>
#include <cmath>
#include <cstdint>
#include <regex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/timeline.h"

// `at` resolution without audio. Each word of the speech gets an estimated time
// (the reference renderer's mute timing); an `at` regex is matched against the
// normalized words: lowercase, no accents, no punctuation.
namespace at {

// Lowercase, strip Latin accents, keep letters/digits/_/%/$ (as the reference's norm()).
inline std::string norm(const std::string& w) {
    static const char* latin1 =  // U+00C0..U+00FF without accents ('\0' = drop)
        "aaaaaaaceeeeiiii" "dnooooo\0ouuuuyts" "aaaaaaaceeeeiiii" "dnooooo\0ouuuuyty";
    std::string out;
    for (size_t i = 0; i < w.size();) {
        const unsigned char c = (unsigned char)w[i];
        if (c < 0x80) {
            if (std::isalnum(c) || c == '_' || c == '%' || c == '$') out += (char)std::tolower(c);
            ++i;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < w.size()) {
            const unsigned cp = ((c & 0x1F) << 6) | ((unsigned char)w[i + 1] & 0x3F);
            if (cp >= 0xC0 && cp <= 0xFF && latin1[cp - 0xC0]) out += latin1[cp - 0xC0];
            i += 2;
        } else {
            // Other scripts and punctuation (dashes, quotes, ×): dashes/quotes drop, letters stay as bytes.
            const int len = (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
            const bool punct = c == 0xE2;  // U+2000..U+2FFF: general punctuation, arrows, math
            if (!punct) out.append(w, i, len);
            i += len;
        }
    }
    return out;
}

struct Word {
    std::string raw, norm;
    float t;  // seconds after the speech starts
};

// Words with estimated times; *total gets the speech duration (last word + its pause + 0.3 s).
inline std::vector<Word> words(const std::string& speech, float* total = nullptr) {
    std::string s = std::regex_replace(speech, std::regex("\\[[^\\]]*\\]"), " ");
    std::vector<Word> out;
    float t = 0;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        size_t j = i;
        while (j < s.size() && !std::isspace((unsigned char)s[j])) ++j;
        if (j > i) {
            std::string w = s.substr(i, j - i), n = norm(w);
            if (!n.empty()) {
                out.push_back({w, n, std::round(t * 100) / 100});
                const char last = w.back();
                t += 1 / timeline::kWordsPerSec;
                if (last == '.' || last == '!' || last == '?' || last == ':') t += timeline::kSentencePause;
                else if (last == ',') t += timeline::kCommaPause;
            }
        }
        i = j;
    }
    if (total) *total = t + 0.3f;
    return out;
}

// Resolves `at` specs for one scene into absolute seconds.
struct Resolver {
    std::vector<Word> ws;
    float start = 0;      // speech start in absolute seconds (scene start + lead)
    int scene = 0;
    std::vector<std::string>* warnings = nullptr;

    // dflt: seconds after the speech start, used when spec is missing or matches nothing.
    float operator()(const nlohmann::json& spec, float dflt) const {
        if (spec.is_null()) return start + dflt;
        if (spec.is_number()) return start + spec.get<float>();
        std::string word;
        int n = 1;
        float offset = -0.25f;
        if (spec.is_string()) word = spec.get<std::string>();
        else {
            word = spec.value("word", "");
            n = spec.value("n", 1);
            offset = spec.value("offset", -0.25f);
            dflt = spec.value("default", dflt);
        }
        std::regex rx;
        try {
            rx = std::regex(word, std::regex::ECMAScript | std::regex::icase);
        } catch (const std::regex_error&) {
            if (warnings) warnings->push_back("scene " + std::to_string(scene + 1) + ": bad at regex '" + word + "'");
            return start + dflt;
        }
        int hits = 0;
        for (const auto& w : ws) {
            if (std::regex_search(w.norm, rx) || std::regex_search(w.raw, rx)) {
                if (++hits == n) return start + w.t + offset;
            }
        }
        if (warnings) warnings->push_back("scene " + std::to_string(scene + 1) + ": at '" + word + "' matched no word; using " + std::to_string(dflt) + " s");
        return start + dflt;
    }
};

}  // namespace at
