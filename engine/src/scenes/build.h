#pragma once
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "render/graph.h"

struct Fonts;

struct Video {
    std::unique_ptr<Node> root;      // in stage units, 1280x720
    float duration = 0;
    std::vector<float> B;            // scene boundaries; scene k runs from B[k] to B[k+1]
    std::vector<std::string> warnings;
    std::vector<const Node*> scenes; // one group per scene, in order
    std::vector<std::string> types;  // scene types, same order
    bool twos = false;               // config.clock "twos": every pose holds two frames
    // Moments worth checking before a render: just after each camera move settles (grid --moves).
    struct Mark { float t; std::string what; };
    std::vector<Mark> marks;
};

// Expands a plan into one scene graph with tracks for the whole video.
// baseDir resolves asset paths; pixelScale is output pixels per stage unit (for logo rasters).
Video buildVideo(const nlohmann::json& plan, const Fonts& fonts, const std::string& baseDir, float pixelScale);

// Layout problems at the end of each scene (text off the stage, overlapping text), as
// "scene N · CODE · message" lines short enough to paste back into the chat.
std::vector<std::string> checkLayout(const Video& video, const Fonts& fonts);

// Runs of at least `minRun` seconds in which nothing inside a scene moves, as "scene N · STILL · ..." lines.
std::vector<std::string> checkStill(const Video& video, float minRun);
