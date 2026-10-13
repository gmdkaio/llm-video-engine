#pragma once
#include <memory>

#include <nlohmann/json.hpp>

#include "render/graph.h"

struct Fonts;

struct Video {
    std::unique_ptr<Node> root;  // in stage units, 1280x720
    float duration = 0;
};

// Expands a plan into one scene graph with tracks for the whole video.
Video buildVideo(const nlohmann::json& plan, const Fonts& fonts);
