#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace demo {
struct Letterbox {
    float scale;
    int width, height, left, top;
};
inline Letterbox letterbox(int w, int h, int mw, int mh) {
    if (w <= 0 || h <= 0 || mw <= 0 || mh <= 0) throw std::runtime_error("Invalid image dimensions");
    const float scale = std::min(float(mw) / w, float(mh) / h);
    const int rw = std::clamp(int(std::lround(w * scale)), 1, mw);
    const int rh = std::clamp(int(std::lround(h * scale)), 1, mh);
    return {scale, rw, rh, (mw - rw) / 2, (mh - rh) / 2};
}
}
