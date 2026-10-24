#pragma once
#include <algorithm>
#include <cmath>
namespace ad {
struct TextScroll {
    int lines = 1, page = 1, first = 0;
    int maximum() const {
        return std::max(0, lines - page);
    }
    int clamped(int line) const {
        return std::clamp(line, 0, maximum());
    }
    float thumb(float track) const {
        return std::min(track, std::max(24.f, track * std::min(page, lines) / std::max(1, lines)));
    }
    float position(float track) const {
        return maximum() ? std::max(0.f, track - thumb(track)) * clamped(first) / maximum() : 0;
    }
    int fromPosition(float y, float track) const {
        auto travel = track - thumb(track);
        return travel > 0 ? clamped((int)std::lround(y / travel * maximum())) : 0;
    }
};
} // namespace ad
