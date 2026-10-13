#pragma once
#include <cmath>
#include <stdexcept>
#include <string>

// Closed-form easings matching GSAP's names. powerN is a polynomial of degree N+1.
enum class EaseKind { Linear, In, Out, InOut, BackOut };

struct Ease {
    EaseKind kind = EaseKind::Out;
    int power = 1;        // GSAP's default ease is power1.out
    float back = 1.70158f;

    float operator()(float t) const {
        if (t <= 0) return 0;
        if (t >= 1) return 1;
        const int n = power + 1;
        switch (kind) {
        case EaseKind::Linear: return t;
        case EaseKind::In:     return std::pow(t, (float)n);
        case EaseKind::Out:    return 1 - std::pow(1 - t, (float)n);
        case EaseKind::InOut:
            return t < 0.5f ? std::pow(2 * t, (float)n) / 2 : 1 - std::pow(2 * (1 - t), (float)n) / 2;
        case EaseKind::BackOut: {
            const float p = t - 1;
            return p * p * ((back + 1) * p + back) + 1;
        }
        }
        return t;
    }

    // "none", "power2.out", "power3.inOut", "back.out(1.6)"
    static Ease parse(const std::string& s) {
        Ease e;
        if (s == "none" || s == "linear") { e.kind = EaseKind::Linear; return e; }
        if (s.rfind("back.out", 0) == 0) {
            e.kind = EaseKind::BackOut;
            auto open = s.find('(');
            if (open != std::string::npos) e.back = std::stof(s.substr(open + 1));
            return e;
        }
        if (s.rfind("power", 0) == 0 && s.size() > 7) {
            e.power = s[5] - '0';
            const std::string dir = s.substr(7);
            if (dir == "in") e.kind = EaseKind::In;
            else if (dir == "out") e.kind = EaseKind::Out;
            else if (dir == "inOut") e.kind = EaseKind::InOut;
            else throw std::runtime_error("unknown ease: " + s);
            return e;
        }
        throw std::runtime_error("unknown ease: " + s);
    }
};
